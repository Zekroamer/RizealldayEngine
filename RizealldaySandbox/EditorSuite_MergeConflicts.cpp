#include "EditorSuiteInternal.h"
#include "SceneJson.h"
#include "MergeDriver.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

// Merge Conflicts window: decides the conflicts the scene merge driver (MergeDriver.cpp) kept as "ours" and wrote to
// `<file>.merge.json`. Each row picks Mine or Theirs; the open scene previews the pick at once (no undo step, no prefab
// override). Apply choices decides every row as one undo step and deletes the report; closing the window without Apply
// shows mine again and keeps the report.

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
	std::error_code ec;
	const std::filesystem::file_time_type reportTime = std::filesystem::last_write_time(reportPath, ec);
	const bool reportChanged = reportPath != mergeReportSeenPath_ || reportTime != mergeReportSeenTime_;
	mergeReportSeenPath_ = reportPath;
	mergeReportSeenTime_ = reportTime;
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
	// The report is gitignored, so it stays when git checks out another branch: it only belongs to the scene text it was made for
	const std::string fileHash = MergeDriver::ReportFileHash(text);
	if (!fileHash.empty()) {
		std::ifstream sceneIn(currentScenePath_, std::ios::binary);
		const std::string sceneText((std::istreambuf_iterator<char>(sceneIn)), std::istreambuf_iterator<char>());
		if (MergeDriver::ContentHash(sceneText) != fileHash) {
			if (reportChanged) {
				LogMessage(std::format("[Merge] {} has a merge report for another version of this scene (another branch?): not shown. "
					"It shows again when that version is checked out.", currentScenePath_), 0);
			}
			mergeConflicts_ = {};
			return;
		}
	}
	mergeConflicts_.reportPath = reportPath;
	mergeConflicts_.fileHash = fileHash;
	mergeConflicts_.showsTheirs.assign(mergeConflicts_.conflicts.size(), 0); // the file holds mine
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
	{
		std::ofstream out(mergeConflicts_.reportPath, std::ios::binary | std::ios::trunc);
		out << MergeDriver::ReportJson(remaining, mergeConflicts_.fileHash);
	}
	mergeReportSeenTime_ = std::filesystem::last_write_time(mergeConflicts_.reportPath, ec); // our own write: the poll must not reload it
}

void EditorSuite::NoteSceneSavedForMergeReport(const std::string& path, const std::string& text)
{
	if (mergeConflicts_.reportPath.empty() || path != currentScenePath_) return;
	mergeConflicts_.fileHash = MergeDriver::ContentHash(text); // the report now belongs to the saved text
	WriteMergeReport();
}

void EditorSuite::PollMergeReport()
{
	if (IsStandaloneMode() || currentScenePath_.empty() || sceneState_ != EditorSceneState::Edit) return;
	const std::string reportPath = currentScenePath_ + MergeDriver::kReportSuffix;
	std::error_code ec;
	const std::filesystem::file_time_type time = std::filesystem::last_write_time(reportPath, ec);
	if (ec || (reportPath == mergeReportSeenPath_ && time == mergeReportSeenTime_)) return;
	// A merge that kept every one of my values leaves the scene file as it was: only the report appears
	DiscardMergePreviews();
	LoadMergeReport();
}

void EditorSuite::FollowMovedSceneReport(const std::string& oldScenePath)
{
	if (oldScenePath.empty()) return; // an untitled scene has no report
	const std::string oldReport = oldScenePath + MergeDriver::kReportSuffix;
	const std::string newReport = currentScenePath_ + MergeDriver::kReportSuffix;
	std::error_code ec;
	if (std::filesystem::exists(oldReport, ec) && !std::filesystem::exists(newReport, ec)) std::filesystem::rename(oldReport, newReport, ec);
	if (!mergeConflicts_.reportPath.empty()) mergeConflicts_.reportPath = newReport;
}

void EditorSuite::FinishMergeEdit()
{
	RebuildHierarchyLinks();
	UpdateWorldMatrices();
	EnsureEditModeScriptInstances();
	RefreshAllScriptObjectViews();
	isSceneDirty_ = true;
	undoMergeAllowed_ = false;
	PushUndoSnapshot(); // one undo step
	undoMergeAllowed_ = false;
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

	const int index = MergeConflictObjectIndex(conflict.objectId);
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

int EditorSuite::MergeConflictObjectIndex(uint32_t reportObjectId)
{
	if (reportObjectId == 0) return -1;
	if (!prefabMode_.active) return FindGameObjectIndexById(reportObjectId);
	const auto key = mergeConflicts_.prefabNodeKeys.find(reportObjectId);
	if (key == mergeConflicts_.prefabNodeKeys.end()) return -1;
	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		if (gameObjects_[i].prefabSource == prefabMode_.prefab && gameObjects_[i].prefabNodeKey == key->second) return static_cast<int>(i);
	}
	return -1;
}

std::unordered_map<uint32_t, int> EditorSuite::MergeConflictCountsByObject()
{
	std::unordered_map<uint32_t, int> counts;
	for (const SceneMerge::Conflict& c : mergeConflicts_.conflicts) {
		if (const int index = MergeConflictObjectIndex(c.objectId); index >= 0) ++counts[gameObjects_[index].id];
	}
	return counts;
}

void EditorSuite::FocusGameObjectInHierarchy(uint32_t id)
{
	const int index = FindGameObjectIndexById(id);
	if (index < 0) return;
	SelectObject(index, false);
	hierarchyScrollToId_ = id;
}

void EditorSuite::PreviewMergeChoices(const std::vector<uint8_t>& showTheirs)
{
	std::vector<uint8_t>& shown = mergeConflicts_.showsTheirs;
	if (sceneState_ != EditorSceneState::Edit || showTheirs.size() != mergeConflicts_.conflicts.size() || shown.size() != showTheirs.size()) return;
	if (shown == showTheirs) return;
	PushUndoSnapshot(); // a pending edit is its own step, never mixed with a preview
	for (size_t i = 0; i < showTheirs.size(); ++i) {
		if (shown[i] == showTheirs[i]) continue;
		const SceneMerge::Conflict& conflict = mergeConflicts_.conflicts[i];
		if (!ApplyMergeValue(conflict, showTheirs[i] != 0)) {
			LogMessage(std::format("[Merge] '{}' of {} could not be shown (the object or field is gone); the open value stays.",
				conflict.field, conflict.objectId ? conflict.objectName : std::string("the scene")), 1);
		}
	}
	shown = showTheirs;
	RebuildHierarchyLinks();
	UpdateWorldMatrices();
	EnsureEditModeScriptInstances();
	RefreshAllScriptObjectViews();
	isSceneDirty_ = true;
	// The baseline takes the preview, so it is neither an undo step nor a prefab override (PushUndoSnapshot diffs against it)
	undoBaseline_ = CaptureUndoState();
	undoMergeAllowed_ = false;
}

void EditorSuite::PreviewMergeChoice(size_t index, bool useTheirs)
{
	if (index >= mergeConflicts_.showsTheirs.size()) return;
	std::vector<uint8_t> choices = mergeConflicts_.showsTheirs;
	choices[index] = useTheirs ? 1 : 0;
	PreviewMergeChoices(choices);
}

void EditorSuite::DiscardMergePreviews()
{
	if (std::find(mergeConflicts_.showsTheirs.begin(), mergeConflicts_.showsTheirs.end(), uint8_t(1)) == mergeConflicts_.showsTheirs.end()) return;
	PreviewMergeChoices(std::vector<uint8_t>(mergeConflicts_.conflicts.size(), 0));
}

void EditorSuite::ApplyMergeChoices()
{
	if (mergeConflicts_.conflicts.empty() || sceneState_ != EditorSceneState::Edit) return;
	const std::vector<uint8_t> chosen = mergeConflicts_.showsTheirs;
	DiscardMergePreviews(); // the undo step goes from mine to the choices
	PushUndoSnapshot();
	for (size_t i = 0; i < mergeConflicts_.conflicts.size(); ++i) {
		const SceneMerge::Conflict& conflict = mergeConflicts_.conflicts[i];
		if (!ApplyMergeValue(conflict, i < chosen.size() && chosen[i] != 0)) {
			LogMessage(std::format("[Merge] '{}' of {} could not be applied (the object or field is gone); the open value stays.",
				conflict.field, conflict.objectId ? conflict.objectName : std::string("the scene")), 1);
		}
	}
	FinishMergeEdit(); // all of them are one undo step
	mergeConflicts_.conflicts.clear();
	mergeConflicts_.showsTheirs.clear();
	WriteMergeReport();
}

std::string EditorSuite::MergeValueText(const std::string& json)
{
	if (json.empty()) return Tr("(missing)");
	const std::optional<Json> v = SceneJson::Parse(json);
	if (!v) return json;
	if (v->is_boolean()) return v->get<bool>() ? Tr("On") : Tr("Off");
	if (v->is_number_float()) return std::format("{:.3f}", v->get<double>());
	if (v->is_string()) return v->get<std::string>();
	if (v->is_array() && !v->empty() && std::all_of(v->begin(), v->end(), [](const Json& e) { return e.is_number(); })) {
		std::string text;
		for (const Json& e : *v) text += std::format("{}{:.3f}", text.empty() ? "" : ", ", e.get<double>());
		return text;
	}
	return v->dump(-1, ' ', false, Json::error_handler_t::replace);
}

void EditorSuite::DrawMergeConflictsWindow()
{
#ifdef USE_IMGUI
	ImGui::SetNextWindowSize(ImVec2(720.0f, 420.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TrId("Merge Conflicts"), &showMergeConflictsWindow_)) {
		ImGui::End();
		return;
	}
	const bool canEdit = sceneState_ == EditorSceneState::Edit;
	if (mergeConflicts_.conflicts.empty() && mergeConflicts_.notes.empty()) {
		ImGui::TextDisabled("%s", Tr("The open scene has no merge conflicts."));
	}
	const size_t count = mergeConflicts_.conflicts.size();
	if (count > 0 && mergeConflicts_.showsTheirs.size() == count) {
		ImGui::TextWrapped("%s", Tr("A git merge changed these fields on both sides. Pick Mine or Theirs for each field: the scene shows your pick right away. Nothing is decided until you press Apply choices."));
		if (!canEdit) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", Tr("Stop Play to decide merge conflicts."));
		ImGui::BeginDisabled(!canEdit);
		bool allMine = false;
		bool allTheirs = false;
		bool apply = false;
		if (ImGui::Button(TrId("All Mine"))) allMine = true;
		ImGui::SetItemTooltip("%s", Tr("Pick your value for every field"));
		ImGui::SameLine();
		if (ImGui::Button(TrId("All Theirs"))) allTheirs = true;
		ImGui::SetItemTooltip("%s", Tr("Pick the other side's value for every field"));
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.26f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.58f, 0.32f, 1.0f));
		if (ImGui::Button(TrId("Apply choices"))) apply = true;
		ImGui::PopStyleColor(2);
		ImGui::SetItemTooltip("%s", Tr("Keep every pick and finish the merge (Ctrl+Z undoes it)"));
		ImGui::EndDisabled();

		// One group per object, in report order; scene settings (object id 0) under their own header
		std::vector<uint32_t> groupOrder;
		std::unordered_map<uint32_t, std::vector<size_t>> groups;
		for (size_t i = 0; i < count; ++i) {
			const uint32_t id = mergeConflicts_.conflicts[i].objectId;
			if (groups[id].empty()) groupOrder.push_back(id);
			groups[id].push_back(i);
		}
		auto colorOf = [](const SceneMerge::Conflict& c, const std::string& json, ImVec4& color) {
			std::string field = c.field.substr(c.field.find_last_of('/') + 1);
			std::transform(field.begin(), field.end(), field.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			if (field.find("color") == std::string::npos && field.find("colour") == std::string::npos) return false;
			const std::optional<Json> v = SceneJson::Parse(json);
			if (!v || !v->is_array() || v->size() < 3 || v->size() > 4) return false;
			float rgba[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			for (size_t k = 0; k < v->size(); ++k) {
				if (!(*v)[k].is_number()) return false;
				rgba[k] = (*v)[k].get<float>();
			}
			color = ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]);
			return true;
		};

		size_t pick = SIZE_MAX;
		bool pickTheirs = false;
		const float listHeight = mergeConflicts_.notes.empty() ? 0.0f : ImGui::GetContentRegionAvail().y * 0.65f;
		ImGui::BeginChild("##MergeRows", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders);
		for (uint32_t objectId : groupOrder) {
			ImGui::PushID(static_cast<int>(objectId));
			const std::vector<size_t>& rows = groups[objectId];
			if (objectId == 0) {
				ImGui::SeparatorText(Tr("Scene settings"));
			} else {
				const int index = MergeConflictObjectIndex(objectId);
				const std::string& name = mergeConflicts_.conflicts[rows.front()].objectName;
				if (index >= 0) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
					if (ImGui::Selectable(std::format("{}  (!)", name).c_str(), IsSelected(gameObjects_[index].id))) FocusGameObjectInHierarchy(gameObjects_[index].id);
					ImGui::PopStyleColor();
					ImGui::SetItemTooltip("%s", Tr("Click to select it in the Hierarchy"));
				} else {
					ImGui::TextDisabled("%s", std::format("{} {}", name, Tr("(not in the scene)")).c_str());
				}
			}
			if (ImGui::BeginTable("##MergeRowsTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
				ImGui::TableSetupColumn(Tr("Field"), ImGuiTableColumnFlags_WidthStretch, 0.8f);
				ImGui::TableSetupColumn(Tr("Mine"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn(Tr("Theirs"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableHeadersRow();
				for (size_t i : rows) {
					const SceneMerge::Conflict& c = mergeConflicts_.conflicts[i];
					const bool theirs = mergeConflicts_.showsTheirs[i] != 0;
					ImGui::PushID(static_cast<int>(i));
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(c.field.c_str());
					for (int side = 0; side < 2; ++side) {
						ImGui::TableNextColumn();
						const bool chosen = (side == 1) == theirs;
						if (chosen) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(46, 110, 64, 140));
						const std::string& json = side == 0 ? c.ours : c.theirs;
						ImGui::PushID(side);
						ImGui::BeginDisabled(!canEdit);
						if (ImGui::RadioButton(MergeValueText(json).c_str(), chosen) && !chosen) {
							pick = i;
							pickTheirs = side == 1;
						}
						ImGui::EndDisabled();
						ImVec4 color;
						if (colorOf(c, json, color)) {
							ImGui::SameLine();
							ImGui::ColorButton("##Swatch", color, ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(ImGui::GetFontSize() * 1.5f, ImGui::GetFontSize()));
						}
						ImGui::PopID();
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			ImGui::Spacing();
			ImGui::PopID();
		}
		ImGui::EndChild();

		// After the rows: these change the list the loop reads
		if (pick != SIZE_MAX) PreviewMergeChoice(pick, pickTheirs);
		if (allMine) PreviewMergeChoices(std::vector<uint8_t>(count, 0));
		if (allTheirs) PreviewMergeChoices(std::vector<uint8_t>(count, 1));
		if (apply) ApplyMergeChoices();
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
