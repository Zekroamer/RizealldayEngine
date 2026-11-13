#include "EditorSuiteInternal.h"
#include "SceneJson.h"
#include "MergeDriver.h"
#include <fstream>
#include <sstream>

// Merge Conflicts window: decides the conflicts the scene merge driver (MergeDriver.cpp) kept as "ours" and wrote to
// `<file>.merge.json`. Each choice is applied to the open scene at once and is one undo step; the report shrinks with
// every decision and is deleted when no conflict is left.

namespace {
	using SceneJson::Json;

	std::vector<std::string> SplitPath(const std::string& path)
	{
		std::vector<std::string> parts;
		std::stringstream in(path);
		for (std::string part; std::getline(in, part, '/');) parts.push_back(part);
		return parts;
	}

	bool ArrayIndex(const std::string& text, size_t& index)
	{
		if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) return false;
		index = std::stoul(text);
		return true;
	}

	/// Sets (or, with no value, removes) the value at a "/"-separated path of SceneMerge; false when the path does not fit
	bool SetAtPath(Json& root, const std::string& path, const std::optional<Json>& value)
	{
		const std::vector<std::string> parts = SplitPath(path);
		if (parts.empty()) return false;
		Json* node = &root;
		for (size_t i = 0; i + 1 < parts.size(); ++i) {
			size_t index = 0;
			if (node->is_object()) {
				if (!node->contains(parts[i])) {
					if (!value) return true; // already absent
					(*node)[parts[i]] = Json::object();
				}
				node = &(*node)[parts[i]];
			} else if (node->is_array() && ArrayIndex(parts[i], index) && index < node->size()) {
				node = &(*node)[index];
			} else {
				return false;
			}
		}
		const std::string& last = parts.back();
		size_t index = 0;
		if (node->is_object()) {
			if (value) (*node)[last] = *value;
			else node->erase(last);
			return true;
		}
		if (node->is_array() && ArrayIndex(last, index) && index < node->size()) {
			(*node)[index] = value ? *value : Json(0); // a grid cell missing on one side reads as empty
			return true;
		}
		return false;
	}
}

void EditorSuite::LoadMergeReport()
{
	mergeConflicts_ = {};
	if (IsStandaloneMode() || currentScenePath_.empty()) return;
	const std::string reportPath = currentScenePath_ + MergeDriver::kReportSuffix;
	std::ifstream in(reportPath, std::ios::binary);
	if (!in.is_open()) return;
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();
	std::string error;
	if (!SceneMerge::ReportFromJson(text, mergeConflicts_.conflicts, mergeConflicts_.notes, &error)) {
		LogMessage(std::format("[Merge] Could not read the merge report {}: {}", reportPath, error), 2);
		mergeConflicts_ = {};
		return;
	}
	mergeConflicts_.reportPath = reportPath;
	if (prefabMode_.active) {
		// Prefab Mode shows an instance with new ids: the report names the ids written in the prefab file
		std::ifstream prefabIn(currentScenePath_, std::ios::binary);
		const std::string prefabText((std::istreambuf_iterator<char>(prefabIn)), std::istreambuf_iterator<char>());
		if (const std::optional<Json> prefab = SceneJson::Parse(prefabText); prefab && prefab->is_object()) {
			if (const Json* objects = SceneJson::Find(*prefab, "objects"); objects && objects->is_array()) { // prefab files list "objects"
				for (const Json& o : *objects) {
					uint32_t id = 0;
					int key = -1;
					if (o.is_object() && SceneJson::Read(o, "id", id) && SceneJson::Read(o, "prefabNodeKey", key)) mergeConflicts_.prefabNodeKeys[id] = key;
				}
			}
		}
	}
	showMergeConflictsWindow_ = true;
	LogMessage(std::format("[Merge] {} has {} merge conflict(s) and {} note(s) from a git merge: decide them in Window > Merge Conflicts.",
		currentScenePath_, mergeConflicts_.conflicts.size(), mergeConflicts_.notes.size()), mergeConflicts_.conflicts.empty() ? 0 : 1);
	WriteMergeReport(); // a report with notes only is done once it is shown
}

void EditorSuite::WriteMergeReport()
{
	if (mergeConflicts_.reportPath.empty()) return;
	std::error_code ec;
	if (mergeConflicts_.conflicts.empty()) {
		std::filesystem::remove(mergeConflicts_.reportPath, ec);
		LogMessage(std::format("[Merge] Every merge conflict is decided; removed {}. Save the scene, then commit the merge.", mergeConflicts_.reportPath), 0);
		mergeConflicts_.reportPath.clear();
		return;
	}
	SceneMerge::Result remaining;
	remaining.ok = true;
	remaining.conflicts = mergeConflicts_.conflicts;
	remaining.notes = mergeConflicts_.notes;
	std::ofstream out(mergeConflicts_.reportPath, std::ios::binary | std::ios::trunc);
	out << SceneMerge::ReportToJson(remaining);
}

bool EditorSuite::ApplyMergeValue(const SceneMerge::Conflict& conflict, bool useTheirs)
{
	const std::string& text = useTheirs ? conflict.theirs : conflict.ours;
	std::optional<Json> value;
	if (!text.empty()) {
		value = SceneJson::Parse(text);
		if (!value) return false;
	}

	if (conflict.objectId == 0) {
		// Scene settings: only what WriteSceneSettingsJson / ReadSceneSettingsJson cover
		const std::string first = SplitPath(conflict.field).front();
		if (first != "skybox" && first != "postProcess" && first != "mapchip") return false;
		Json settings = Json::object();
		WriteSceneSettingsJson(&settings);
		if (!SetAtPath(settings, conflict.field, value)) return false;
		ReadSceneSettingsJson(&settings);
		return true;
	}

	int index = -1;
	if (prefabMode_.active) {
		const auto key = mergeConflicts_.prefabNodeKeys.find(conflict.objectId);
		for (size_t i = 0; key != mergeConflicts_.prefabNodeKeys.end() && i < gameObjects_.size(); ++i) {
			if (gameObjects_[i].prefabSource == prefabMode_.prefab && gameObjects_[i].prefabNodeKey == key->second) index = static_cast<int>(i);
		}
	} else {
		index = FindGameObjectIndexById(conflict.objectId);
	}
	if (index < 0) return false;
	Json o = Json::object();
	WriteGameObjectJson(gameObjects_[index], &o);
	if (!SetAtPath(o, conflict.field, value)) return false;
	// Rebuilt from JSON like Undo does, so a removed key really goes back to its default
	const uint32_t id = gameObjects_[index].id;
	GameObject obj;
	obj.id = id;
	ReadGameObjectJson(&o, obj);
	obj.id = id;
	gameObjects_[index] = std::move(obj);
	return true;
}

bool EditorSuite::ResolveMergeConflict(size_t index, bool useTheirs)
{
	if (index >= mergeConflicts_.conflicts.size() || sceneState_ != EditorSceneState::Edit) return false;
	PushUndoSnapshot(); // a pending edit is its own step
	const SceneMerge::Conflict conflict = mergeConflicts_.conflicts[index];
	if (!ApplyMergeValue(conflict, useTheirs)) {
		LogMessage(std::format("[Merge] '{}' of {} could not be applied (the object or field is gone); the open value stays.",
			conflict.field, conflict.objectId ? conflict.objectName : std::string("the scene")), 1);
	}
	RebuildHierarchyLinks();
	UpdateWorldMatrices();
	EnsureEditModeScriptInstances();
	RefreshAllScriptObjectViews();
	isSceneDirty_ = true;
	undoMergeAllowed_ = false;
	PushUndoSnapshot();
	undoMergeAllowed_ = false;
	mergeConflicts_.conflicts.erase(mergeConflicts_.conflicts.begin() + index);
	WriteMergeReport();
	return true;
}

void EditorSuite::ResolveAllMergeConflicts(bool useTheirs)
{
	if (mergeConflicts_.conflicts.empty() || sceneState_ != EditorSceneState::Edit) return;
	PushUndoSnapshot();
	for (const SceneMerge::Conflict& conflict : mergeConflicts_.conflicts) {
		if (!ApplyMergeValue(conflict, useTheirs)) {
			LogMessage(std::format("[Merge] '{}' of {} could not be applied (the object or field is gone); the open value stays.",
				conflict.field, conflict.objectId ? conflict.objectName : std::string("the scene")), 1);
		}
	}
	RebuildHierarchyLinks();
	UpdateWorldMatrices();
	EnsureEditModeScriptInstances();
	RefreshAllScriptObjectViews();
	isSceneDirty_ = true;
	undoMergeAllowed_ = false;
	PushUndoSnapshot(); // all of them are one undo step
	undoMergeAllowed_ = false;
	mergeConflicts_.conflicts.clear();
	WriteMergeReport();
}

void EditorSuite::DrawMergeConflictsWindow()
{
#ifdef USE_IMGUI
	ImGui::SetNextWindowSize(ImVec2(720.0f, 360.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TrId("Merge Conflicts"), &showMergeConflictsWindow_)) {
		ImGui::End();
		return;
	}
	const bool canEdit = sceneState_ == EditorSceneState::Edit;
	if (mergeConflicts_.conflicts.empty() && mergeConflicts_.notes.empty()) {
		ImGui::TextDisabled("%s", Tr("The open scene has no merge conflicts."));
	}
	if (!mergeConflicts_.conflicts.empty()) {
		ImGui::TextWrapped("%s", Tr("A git merge changed these fields differently on both sides. The scene shows your value until you decide. Save the scene after deciding."));
		if (!canEdit) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", Tr("Stop Play to decide merge conflicts."));
		ImGui::BeginDisabled(!canEdit);
		if (ImGui::Button(TrId("Use Mine for All"))) ResolveAllMergeConflicts(false);
		ImGui::SameLine();
		if (ImGui::Button(TrId("Use Theirs for All"))) ResolveAllMergeConflicts(true);
		ImGui::EndDisabled();

		size_t decide = SIZE_MAX;
		bool decideTheirs = false;
		const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
		const float tableHeight = mergeConflicts_.notes.empty() ? 0.0f : ImGui::GetContentRegionAvail().y * 0.65f;
		if (ImGui::BeginTable("##MergeConflictTable", 5, flags, ImVec2(0.0f, tableHeight))) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn(Tr("Object"));
			ImGui::TableSetupColumn(Tr("Field"));
			ImGui::TableSetupColumn(Tr("Mine"));
			ImGui::TableSetupColumn(Tr("Theirs"));
			ImGui::TableSetupColumn("##MergeChoice", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();
			auto valueText = [](const std::string& v) { return v.empty() ? std::string(Tr("(missing)")) : v; };
			for (size_t i = 0; i < mergeConflicts_.conflicts.size(); ++i) {
				const SceneMerge::Conflict& c = mergeConflicts_.conflicts[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (c.objectId == 0) ImGui::TextUnformatted(Tr("Scene setting"));
				else ImGui::TextUnformatted(std::format("{} ({})", c.objectName, c.objectId).c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(c.field.c_str());
				ImGui::TableNextColumn();
				ImGui::TextWrapped("%s", valueText(c.ours).c_str());
				ImGui::TableNextColumn();
				ImGui::TextWrapped("%s", valueText(c.theirs).c_str());
				ImGui::TableNextColumn();
				ImGui::BeginDisabled(!canEdit);
				if (ImGui::SmallButton(TrId("Use Mine"))) decide = i;
				ImGui::SameLine();
				if (ImGui::SmallButton(TrId("Use Theirs"))) { decide = i; decideTheirs = true; }
				ImGui::EndDisabled();
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (decide != SIZE_MAX) ResolveMergeConflict(decide, decideTheirs); // after the loop: it shrinks the list
	}
	if (!mergeConflicts_.notes.empty()) {
		ImGui::SeparatorText(Tr("Notes"));
		for (const SceneMerge::Note& note : mergeConflicts_.notes) {
			const char* kind = note.kind == SceneMerge::NoteKind::KeptDeleted ? Tr("Kept (deleted on one side)")
				: note.kind == SceneMerge::NoteKind::MovedToRoot ? Tr("Moved to the root")
				: Tr("New id");
			ImGui::Bullet();
			ImGui::TextWrapped("%s", std::format("{}: {} ({}) - {}", kind, note.objectName, note.objectId, note.message).c_str());
		}
	}
	ImGui::End();
#endif
}
