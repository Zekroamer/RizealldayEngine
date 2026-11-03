#include "EditorSuiteInternal.h"
#include "SceneJson.h"
#include "PrefabOverrides.h"

// Linked prefab instances (Fase 8). An instance remembers its prefab (prefabSource, prefabNodeKey, saved with the scene).
// Edits the user makes to an instance in the editor become overrides (prefabOverrides, found by the undo diff):
// "key" (a whole value), "scale/0" (one axis), "components/Trail" (added or removed), "components/Trail/lifetime" (one field).
// A sync rebuilds the instance from the prefab file and puts the overrides back; Revert rebuilds it without them.
// Kept by every rebuild: the ids of nodes the prefab still has, the root's position, rotation and parent.

struct EditorSuite::PrefabRebuild {
	struct Node {
		uint32_t id = 0;
		SceneJson::Json json; // the node as it was, for its overrides
	};
	std::unordered_map<int, Node> nodes; // by node key
	bool keepOverrides = true;
};

namespace {
	// Never overrides: identity and links are the instance's by nature
	bool IsLinkKey(const std::string& key)
	{
		return key == "id" || key == "prefab" || key == "prefabNode" || key == "prefabNodeKey" || key == "prefabOverrides" ||
			key == "prefabGuid" || key == "parentId" || key == "attachedParentId" ||
			key == "nestedPrefab" || key == "nestedNode" || key == "nestedOverrides" || key == "variantOverrides";
	}

	// The root's place in the scene belongs to the instance
	bool IsRootPlacementKey(const std::string& key)
	{
		return key == "translation" || key == "rotation" || key == "rotationQuat";
	}

	// Overridden one axis at a time, like the Transform fields. Rotation stays whole: it is saved twice (euler + quaternion).
	bool IsPerAxisKey(const std::string& key)
	{
		return key == "translation" || key == "scale";
	}
}

bool MergePrefabOverride(SceneJson::Json& node, const SceneJson::Json& old, const std::string& key)
{
	using SceneJson::Json;
	if (key.rfind("components/", 0) == 0) {
		const std::string rest = key.substr(11);
		const size_t slash = rest.find('/');
		const std::string component = rest.substr(0, slash);
		const Json* oldComponents = SceneJson::Find(old, "components");
		const Json* value = (oldComponents && oldComponents->is_object()) ? SceneJson::Find(*oldComponents, component.c_str()) : nullptr;
		if (!node.contains("components") || !node["components"].is_object()) node["components"] = Json::object();
		Json& components = node["components"];
		if (slash == std::string::npos) {
			// The whole component: added, changed or removed by the instance
			if (value) components[component] = *value;
			else components.erase(component);
			return true;
		}
		const std::string field = rest.substr(slash + 1);
		const Json* oldField = (value && value->is_object()) ? SceneJson::Find(*value, field.c_str()) : nullptr;
		if (!oldField || !components.contains(component) || !components[component].is_object()) return false;
		components[component][field] = *oldField;
		return true;
	}
	if (const size_t slash = key.find('/'); slash != std::string::npos) {
		const std::string name = key.substr(0, slash);
		const size_t axis = static_cast<size_t>(std::strtoul(key.c_str() + slash + 1, nullptr, 10));
		const Json* value = SceneJson::Find(old, name.c_str());
		if (!value || !value->is_array() || axis >= value->size() || !node.contains(name) || !node[name].is_array() || axis >= node[name].size()) return false;
		node[name][axis] = (*value)[axis];
		return true;
	}
	const Json* value = SceneJson::Find(old, key.c_str());
	if (!value) return false;
	node[key] = *value;
	return true;
}

void CollectPrefabOverrideKeys(const SceneJson::Json& before, const SceneJson::Json& after, bool isRoot, std::vector<std::string>& overrides)
{
	if (!before.is_object() || !after.is_object()) return;
	const auto listed = [&](const std::string& key) { return std::find(overrides.begin(), overrides.end(), key) != overrides.end(); };
	const auto add = [&](const std::string& key) {
		if (!listed(key)) overrides.push_back(key);
	};
	// Both present: the fields or axes that differ, unless the whole value is an override already
	const auto addParts = [&](const std::string& prefix, const SceneJson::Json& a, const SceneJson::Json& b) {
		if (listed(prefix)) return;
		if (a.is_object() && b.is_object()) {
			std::set<std::string> fields;
			for (auto it = a.begin(); it != a.end(); ++it) fields.insert(it.key());
			for (auto it = b.begin(); it != b.end(); ++it) fields.insert(it.key());
			for (const std::string& field : fields) {
				const SceneJson::Json* fa = SceneJson::Find(a, field.c_str());
				const SceneJson::Json* fb = SceneJson::Find(b, field.c_str());
				if ((fa == nullptr) != (fb == nullptr) || (fa && *fa != *fb)) add(prefix + "/" + field);
			}
		} else if (a.is_array() && b.is_array() && a.size() == b.size()) {
			for (size_t i = 0; i < a.size(); ++i) {
				if (a[i] != b[i]) add(prefix + "/" + std::to_string(i));
			}
		} else {
			add(prefix);
		}
	};
	std::set<std::string> keys;
	for (auto it = before.begin(); it != before.end(); ++it) keys.insert(it.key());
	for (auto it = after.begin(); it != after.end(); ++it) keys.insert(it.key());
	for (const std::string& key : keys) {
		if (IsLinkKey(key) || (isRoot && IsRootPlacementKey(key))) continue;
		const SceneJson::Json* a = SceneJson::Find(before, key.c_str());
		const SceneJson::Json* b = SceneJson::Find(after, key.c_str());
		if (key == "components") {
			std::set<std::string> components;
			if (a && a->is_object()) for (auto it = a->begin(); it != a->end(); ++it) components.insert(it.key());
			if (b && b->is_object()) for (auto it = b->begin(); it != b->end(); ++it) components.insert(it.key());
			for (const std::string& name : components) {
				const SceneJson::Json* ca = (a && a->is_object()) ? SceneJson::Find(*a, name.c_str()) : nullptr;
				const SceneJson::Json* cb = (b && b->is_object()) ? SceneJson::Find(*b, name.c_str()) : nullptr;
				if ((ca == nullptr) != (cb == nullptr)) add("components/" + name);
				else if (ca && *ca != *cb) addParts("components/" + name, *ca, *cb);
			}
			continue;
		}
		if ((a == nullptr) != (b == nullptr)) add(key);
		else if (a && *a != *b) {
			if (IsPerAxisKey(key)) addParts(key, *a, *b);
			else add(key);
		}
	}
}

std::vector<uint32_t> EditorSuite::PrefabInstanceRoots(const std::string& prefabName) const
{
	std::vector<uint32_t> roots;
	for (const GameObject& obj : gameObjects_) {
		if (obj.prefabNodeIndex == 0 && !obj.prefabSource.empty() && (prefabName.empty() || obj.prefabSource == prefabName)) roots.push_back(obj.id);
	}
	return roots;
}

uint32_t EditorSuite::PrefabRebuildId(int nodeKey) const
{
	if (!prefabRebuild_) return 0;
	const auto it = prefabRebuild_->nodes.find(nodeKey);
	return it != prefabRebuild_->nodes.end() ? it->second.id : 0u;
}

void EditorSuite::MergePrefabRebuild(int nodeKey, void* json) const
{
	if (!prefabRebuild_ || !prefabRebuild_->keepOverrides) return;
	const auto it = prefabRebuild_->nodes.find(nodeKey);
	if (it == prefabRebuild_->nodes.end()) return;
	SceneJson::Json& node = *static_cast<SceneJson::Json*>(json);
	const SceneJson::Json& old = it->second.json;
	const SceneJson::Json* overrides = SceneJson::Find(old, "prefabOverrides");
	if (!overrides || !overrides->is_array()) return;
	SceneJson::Json kept = SceneJson::Json::array();
	for (const auto& entry : *overrides) {
		if (entry.is_string() && MergePrefabOverride(node, old, entry.get<std::string>())) kept.push_back(entry);
	}
	node["prefabOverrides"] = kept;
}

bool EditorSuite::RebuildPrefabInstance(uint32_t rootId, bool keepOverrides)
{
	const GameObject* root = FindGameObjectById(rootId, false);
	if (!root || root->prefabSource.empty() || root->prefabNodeIndex != 0) return false;
	const std::string prefab = root->prefabSource;
	if (!HasPrefab(prefab)) return false; // gone: keep the instance as it is

	// What the instance is now, node by node
	PrefabRebuild rebuild;
	rebuild.keepOverrides = keepOverrides;
	std::vector<uint32_t> pending{ rootId };
	while (!pending.empty()) {
		const GameObject* node = FindGameObjectById(pending.back(), false);
		pending.pop_back();
		if (!node || node->prefabSource != prefab) continue;
		const int key = node->prefabNodeKey >= 0 ? node->prefabNodeKey : node->prefabNodeIndex;
		if (key >= 0 && !rebuild.nodes.contains(key)) {
			PrefabRebuild::Node& saved = rebuild.nodes[key];
			saved.id = node->id;
			WriteGameObjectJson(*node, &saved.json);
		}
		for (const GameObject& child : gameObjects_) {
			if (child.parentId == static_cast<int>(node->id)) pending.push_back(child.id);
		}
	}

	const Vector3 translation = root->translation;
	const Vector3 rotation = root->rotation;
	const Quaternion rotationQuat = root->rotationQuat;
	const int parentId = root->parentId;
	const bool wasSelected = selectedObjectIndex_ == FindGameObjectIndexById(rootId);

	// Where the instance sits in the Hierarchy: the rebuilt one goes back there, not to the end
	const int rootIndex = FindGameObjectIndexById(rootId);
	int removedBefore = 0;
	for (const auto& [key, node] : rebuild.nodes) {
		if (node.id != rootId && FindGameObjectIndexById(node.id) < rootIndex) ++removedBefore;
	}
	QueueDeleteGameObjectById(rootId); // with its children
	ProcessPendingDeletes();
	const size_t firstNew = gameObjects_.size();
	prefabRebuild_ = &rebuild;
	const uint32_t rebuiltId = InstantiatePrefabNow(prefab, translation, rootId);
	prefabRebuild_ = nullptr;
	if (rebuiltId != rootId) return false;
	const size_t insertAt = static_cast<size_t>((std::max)(rootIndex - removedBefore, 0));
	if (insertAt < firstNew) {
		std::rotate(gameObjects_.begin() + static_cast<std::ptrdiff_t>(insertAt), gameObjects_.begin() + static_cast<std::ptrdiff_t>(firstNew), gameObjects_.end());
	}
	GameObject* rebuilt = FindGameObjectById(rootId, false);
	rebuilt->rotation = rotation;
	rebuilt->rotationQuat = rotationQuat;
	rebuilt->parentId = parentId;
	RebuildHierarchyLinks();
	UpdateWorldMatrices();
	if (wasSelected) selectedObjectIndex_ = FindGameObjectIndexById(rootId);
	isSceneDirty_ = true;
	suppressOverrideRecording_ = true; // the prefab changed these objects, not the user
	PushUndoSnapshot();
	suppressOverrideRecording_ = false;
	return true;
}

bool EditorSuite::RevertPrefabInstance(uint32_t rootId)
{
	return RebuildPrefabInstance(rootId, false);
}

bool EditorSuite::RevertPrefabOverride(uint32_t objectId, const std::string& key)
{
	GameObject* obj = FindGameObjectById(objectId, false);
	if (!obj || obj->prefabSource.empty()) return false;
	const auto it = std::find(obj->prefabOverrides.begin(), obj->prefabOverrides.end(), key);
	if (it == obj->prefabOverrides.end()) return false;
	obj->prefabOverrides.erase(it);
	const GameObject* root = obj;
	while (root->prefabNodeIndex != 0 && root->parentId >= 0) {
		const GameObject* parent = FindGameObjectById(static_cast<uint32_t>(root->parentId), false);
		if (!parent || parent->prefabSource != obj->prefabSource) break;
		root = parent;
	}
	return RebuildPrefabInstance(root->id, true);
}

bool EditorSuite::HasPrefabOverride(const GameObject& obj, std::string_view keyOrPrefix) const
{
	if (obj.prefabSource.empty() || keyOrPrefix.empty()) return false;
	for (const std::string& ov : obj.prefabOverrides) {
		if (ov == keyOrPrefix) return true;
		if (ov.size() > keyOrPrefix.size() &&
			std::string_view(ov).substr(0, keyOrPrefix.size()) == keyOrPrefix &&
			ov[keyOrPrefix.size()] == '/') {
			return true;
		}
	}
	return false;
}

bool EditorSuite::RevertPrefabOverridesByPrefix(uint32_t objectId, std::string_view keyOrPrefix)
{
	GameObject* obj = FindGameObjectById(objectId, false);
	if (!obj || obj->prefabSource.empty() || keyOrPrefix.empty()) return false;
	const size_t before = obj->prefabOverrides.size();
	obj->prefabOverrides.erase(
		std::remove_if(obj->prefabOverrides.begin(), obj->prefabOverrides.end(), [&](const std::string& ov) {
			if (ov == keyOrPrefix) return true;
			if (ov.size() > keyOrPrefix.size() &&
				std::string_view(ov).substr(0, keyOrPrefix.size()) == keyOrPrefix &&
				ov[keyOrPrefix.size()] == '/') {
				return true;
			}
			return false;
		}),
		obj->prefabOverrides.end());
	if (obj->prefabOverrides.size() == before) return false;

	const GameObject* root = obj;
	while (root->prefabNodeIndex != 0 && root->parentId >= 0) {
		const GameObject* parent = FindGameObjectById(static_cast<uint32_t>(root->parentId), false);
		if (!parent || parent->prefabSource != obj->prefabSource) break;
		root = parent;
	}
	return RebuildPrefabInstance(root->id, true);
}

bool EditorSuite::ApplyPrefabInstance(uint32_t rootId)
{
	const GameObject* root = FindGameObjectById(rootId, false);
	if (!root || root->prefabSource.empty() || root->prefabNodeIndex != 0) return false;
	const std::string prefab = root->prefabSource;
	if (!SavePrefab(rootId, prefab)) return false;
	// This instance's values are the prefab's now: nothing of it is an override any more
	std::vector<uint32_t> pending{ rootId };
	while (!pending.empty()) {
		GameObject* node = FindGameObjectById(pending.back(), false);
		pending.pop_back();
		if (!node) continue;
		node->prefabOverrides.clear();
		for (const GameObject& child : gameObjects_) {
			if (child.parentId == static_cast<int>(node->id)) pending.push_back(child.id);
		}
	}
	// Every instance, this one too: children added to it become part of the prefab and get their node keys
	// Every linked instance: variants of this prefab and prefabs that nest it change with it
	for (uint32_t instance : PrefabInstanceRoots("")) RebuildPrefabInstance(instance, true);
	LogMessage(std::format("[Prefab] Applied to '{}' and updated its instances.", prefab), 0);
	return true;
}

void EditorSuite::UnlinkPrefabInstance(uint32_t rootId)
{
	GameObject* root = FindGameObjectById(rootId, false);
	if (!root || root->prefabSource.empty()) return;
	const std::string prefab = root->prefabSource;
	std::vector<uint32_t> pending{ rootId };
	while (!pending.empty()) {
		const uint32_t id = pending.back();
		pending.pop_back();
		GameObject* obj = FindGameObjectById(id, false);
		if (!obj || obj->prefabSource != prefab) continue;
		obj->prefabSource.clear();
		obj->prefabNodeIndex = -1;
		obj->prefabNodeKey = -1;
		obj->prefabOverrides.clear();
		for (const GameObject& child : gameObjects_) {
			if (child.parentId == static_cast<int>(id)) pending.push_back(child.id);
		}
	}
	isSceneDirty_ = true;
	PushUndoSnapshot();
}

std::string EditorSuite::CreatePrefabVariant(uint32_t rootId)
{
	GameObject* root = FindGameObjectById(rootId, false);
	if (!root || root->prefabSource.empty() || root->prefabNodeIndex != 0) return {};
	const std::string base = root->prefabSource;
	std::string name = base + " Variant";
	std::error_code ec;
	for (int n = 2; std::filesystem::exists(ResolvePrefabFile(name), ec); ++n) name = std::format("{} Variant {}", base, n);
	if (!SavePrefab(rootId, name, base)) return {};
	// This instance now shows the variant: what it overrode is the variant's own value
	std::vector<uint32_t> pending{ rootId };
	while (!pending.empty()) {
		GameObject* node = FindGameObjectById(pending.back(), false);
		pending.pop_back();
		if (!node || node->prefabSource != base) continue;
		node->prefabSource = name;
		node->prefabOverrides.clear();
		for (const GameObject& child : gameObjects_) {
			if (child.parentId == static_cast<int>(node->id)) pending.push_back(child.id);
		}
	}
	RebuildPrefabInstance(rootId, false);
	LogMessage(std::format("[Prefab] Created variant '{}' of '{}'.", name, base), 0);
	return name;
}

bool EditorSuite::OpenPrefabMode(const std::string& prefabName)
{
	if (sceneState_ != EditorSceneState::Edit) {
		LogMessage("[Prefab] Stop Play before opening a prefab.", 1);
		return false;
	}
	if (prefabMode_.active) {
		if (prefabMode_.prefab == prefabName) return true;
		ClosePrefabMode();
	}
	ClearPrefabCache();
	if (!HasPrefab(prefabName)) return false;
	PrefabModeState state;
	state.prefab = prefabName;
	state.sceneJson = SerializeSceneToString();
	state.scenePath = currentScenePath_;
	state.sceneName = currentSceneName_;
	state.sceneDirty = isSceneDirty_;
	PushUndoSnapshot(); // a pending edit belongs to the scene history
	prefabModeUndo_ = std::move(undoCommands_);
	prefabModeRedo_ = std::move(redoCommands_);

	// An empty stage: the default camera and light only, so the prefab is lit
	keepTerrainGarbage_ = true;
	NewScene();
	keepTerrainGarbage_ = false;
	for (const GameObject& obj : gameObjects_) {
		if (obj.type != GameObjectType::Camera && obj.type != GameObjectType::DirectionalLight) QueueDeleteGameObjectById(obj.id);
	}
	ProcessPendingDeletes();
	state.rootId = InstantiatePrefabNow(prefabName, { 0.0f, 0.0f, 0.0f });
	if (state.rootId == 0) {
		DeserializeSceneFromString(state.sceneJson);
		currentScenePath_ = state.scenePath;
		currentSceneName_ = state.sceneName;
		ResetUndoHistory();
		undoCommands_ = std::move(prefabModeUndo_);
		redoCommands_ = std::move(prefabModeRedo_);
		isSceneDirty_ = state.sceneDirty;
		return false;
	}
	prefabMode_ = std::move(state);
	prefabMode_.active = true;
	currentScenePath_ = ResolvePrefabFile(prefabName).string();
	currentSceneName_ = prefabName;
	selectedObjectIndex_ = FindGameObjectIndexById(prefabMode_.rootId);
	ResetUndoHistory();
	isSceneDirty_ = false;
	LogMessage(std::format("[Prefab] Editing '{}'. Save writes the prefab; Back to Scene returns.", prefabName), 0);
	return true;
}

bool EditorSuite::SavePrefabMode()
{
	if (!prefabMode_.active) return false;
	const GameObject* root = FindGameObjectById(prefabMode_.rootId, false);
	if (!root) {
		LogMessage("[Prefab] The prefab root was deleted: nothing to save.", 2);
		return false;
	}
	if (!SavePrefab(prefabMode_.rootId, prefabMode_.prefab)) return false;
	isSceneDirty_ = false;
	return true;
}

void EditorSuite::ClosePrefabMode()
{
	if (!prefabMode_.active) return;
	PrefabModeState state = std::move(prefabMode_);
	prefabMode_ = {};
	DeserializeSceneFromString(state.sceneJson);
	currentScenePath_ = state.scenePath;
	currentSceneName_ = state.sceneName;
	SyncLinkedPrefabs(); // the instances show what was saved
	ResetUndoHistory(); // the baseline is the scene as it is now; the sync itself is not an undo step
	undoCommands_ = std::move(prefabModeUndo_);
	redoCommands_ = std::move(prefabModeRedo_);
	isSceneDirty_ = state.sceneDirty; // a sync is not an edit: the scene file stores links, not copies
}

void EditorSuite::DrawPrefabModeBar()
{
#ifdef USE_IMGUI
	if (!prefabMode_.active) return;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.24f, 0.42f, 1.0f));
	ImGui::BeginChild("##PrefabModeBar", ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing() + 4.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
	ImGui::AlignTextToFramePadding();
	ImGui::Text(Tr("Prefab Mode: %s%s"), prefabMode_.prefab.c_str(), isSceneDirty_ ? " *" : "");
	ImGui::SameLine();
	if (ImGui::SmallButton(TrId("Save Prefab"))) SavePrefabMode();
	ImGui::SetItemTooltip("%s", Tr("Writes the prefab file (Ctrl+S). Instances in the scene follow when you go back."));
	ImGui::SameLine();
	if (ImGui::SmallButton(TrId("Back to Scene"))) ClosePrefabMode();
	ImGui::SetItemTooltip("%s", Tr("Returns to the scene. Prefab edits that were not saved are dropped."));
	ImGui::EndChild();
	ImGui::PopStyleColor();
#endif
}

void EditorSuite::SyncLinkedPrefabs()
{
	ClearPrefabCache(); // files may have changed since they were read
	for (uint32_t root : PrefabInstanceRoots("")) RebuildPrefabInstance(root, true);
}

void EditorSuite::RecordPrefabOverrides(uint32_t objectId, const std::string& beforeJson, const std::string& afterJson)
{
	GameObject* obj = FindGameObjectById(objectId, false);
	if (!obj || obj->prefabSource.empty()) return;
	const auto before = SceneJson::Parse(beforeJson);
	const auto after = SceneJson::Parse(afterJson);
	if (!before || !after || !before->is_object() || !after->is_object()) return;
	CollectPrefabOverrideKeys(*before, *after, obj->prefabNodeIndex == 0, obj->prefabOverrides);
}

void EditorSuite::DrawPrefabLinkInspector(GameObject& obj)
{
#ifdef USE_IMGUI
	if (obj.prefabSource.empty()) return;
	// The root of the instance this object belongs to
	const GameObject* root = &obj;
	while (root->prefabNodeIndex != 0 && root->parentId >= 0) {
		const GameObject* parent = FindGameObjectById(static_cast<uint32_t>(root->parentId), false);
		if (!parent || parent->prefabSource != obj.prefabSource) break;
		root = parent;
	}
	const uint32_t rootId = root->id;
	const uint32_t objectId = obj.id;
	const std::string prefab = obj.prefabSource;
	const std::vector<std::string> overrides = obj.prefabOverrides;
	if (const std::string base = PrefabBaseOf(prefab); !base.empty()) {
		ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), Tr("Prefab: %s (variant of %s)"), prefab.c_str(), base.c_str());
	} else {
		ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), Tr("Prefab: %s"), prefab.c_str());
	}
	ImGui::SameLine();
	if (ImGui::SmallButton(TrId("Apply##Prefab"))) ApplyPrefabInstance(rootId);
	ImGui::SetItemTooltip("%s", Tr("Writes this instance to the prefab file and updates every instance in the scene."));
	ImGui::SameLine();
	if (ImGui::SmallButton(TrId("Revert##Prefab"))) RevertPrefabInstance(rootId);
	ImGui::SetItemTooltip("%s", Tr("Rebuilds this instance from the prefab file and drops its overrides. The position, rotation and parent stay."));
	ImGui::SameLine();
	if (ImGui::SmallButton(TrId("Unlink##Prefab"))) UnlinkPrefabInstance(rootId);
	ImGui::SetItemTooltip("%s", Tr("Makes this an ordinary object: later prefab edits no longer reach it."));
	if (!prefabMode_.active) {
		ImGui::SameLine();
		if (ImGui::SmallButton(TrId("Open##Prefab"))) {
			OpenPrefabMode(prefab);
			ImGui::Separator();
			return; // the objects of this Inspector are gone
		}
		ImGui::SetItemTooltip("%s", Tr("Edits the prefab alone (Prefab Mode)."));
		ImGui::SameLine();
		if (ImGui::SmallButton(TrId("Create Variant##Prefab"))) CreatePrefabVariant(rootId);
		ImGui::SetItemTooltip("%s", Tr("Saves this instance as a new prefab that follows this one and keeps what this instance changed."));
	}
	if (!overrides.empty()) {
		ImGui::TextDisabled(Tr("Overrides of this object (kept when the prefab changes):"));
		for (const std::string& key : overrides) {
			ImGui::PushID(key.c_str());
			if (ImGui::SmallButton(TrId("Revert##Override"))) RevertPrefabOverride(objectId, key);
			ImGui::SameLine();
			// "materialColor" -> "Material Color", "scale/0" -> "Scale X", "components/Trail/lifetime" -> "Trail: Lifetime", translated
			std::string shown;
			if (key.rfind("components/", 0) == 0) {
				const std::string rest = key.substr(11);
				const size_t slash = rest.find('/');
				shown = Tr(rest.substr(0, slash).c_str());
				if (slash != std::string::npos) shown += std::string(": ") + Tr(HumanizeFieldName(rest.substr(slash + 1).c_str()).c_str());
			} else if (const size_t slash = key.find('/'); slash != std::string::npos) {
				static const char* const kAxes[] = { "X", "Y", "Z", "W" };
				const size_t axis = std::strtoul(key.c_str() + slash + 1, nullptr, 10);
				shown = std::string(Tr(HumanizeFieldName(key.substr(0, slash).c_str()).c_str())) + " " + (axis < 4 ? kAxes[axis] : "?");
			} else {
				shown = Tr(HumanizeFieldName(key.c_str()).c_str());
			}
			ImGui::TextUnformatted(shown.c_str());
			ImGui::PopID();
		}
	}
	ImGui::Separator();
#else
	(void)obj;
#endif
}
