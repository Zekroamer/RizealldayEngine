#include "EditorSuiteInternal.h"
#include "SceneJson.h"
#include "PrefabOverrides.h"

// Prefab file: { "prefabVersion": 1, "name": "...", "objects": [ root, descendants... ] }
// Each entry is a WriteGameObjectJson object; ids/parentIds are remapped to fresh ids on instantiate.
// A variant adds "basePrefab": its nodes found in the base (by prefabNodeKey) keep only "variantOverrides", the rest are added nodes.
// A nested prefab is a subtree whose nodes have "nestedPrefab" + "nestedNode" (its key in that prefab) and "nestedOverrides".
// Loading resolves both from the current files (LoadPrefab), so the cache holds complete objects and nothing else needs to know.
struct EditorSuite::PrefabCache {
	std::unordered_map<std::string, SceneJson::Json> loaded;
	std::unordered_set<std::string> missing; // warned once until the cache is cleared
};

namespace {
	std::string SanitizePrefabName(const std::string& name)
	{
		std::string out;
		for (char c : name) {
			const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ';
			out += ok ? c : '_';
		}
		return out.empty() ? std::string("Prefab") : out;
	}

	const SceneJson::Json* PrefabObjects(const SceneJson::Json& prefab)
	{
		const SceneJson::Json* objects = SceneJson::Find(prefab, "objects");
		return (objects && objects->is_array() && !objects->empty() && (*objects)[0].is_object()) ? objects : nullptr;
	}

	// Copies the prefab root's transform/filter values into a script view (the view is what the spawner edits)
	void FillViewFromPrefabRoot(const SceneJson::Json& root, ScriptGameObjectView& view)
	{
		std::string text;
		if (SceneJson::Read(root, "name", text)) strncpy_s(view.name, text.c_str(), sizeof(view.name) - 1);
		if (SceneJson::Read(root, "tag", text)) strncpy_s(view.tag, text.c_str(), sizeof(view.tag) - 1);
		SceneJson::Read(root, "layer", view.layer);
		view.layer &= 31u;
		SceneJson::Read(root, "collisionMask", view.collisionMask);
		SceneJson::Read(root, "isStatic", view.isStatic);
		view.rotation = { 0.0f, 0.0f, 0.0f };
		view.rotationQuat = Quaternion::Identity();
		view.scale = { 1.0f, 1.0f, 1.0f };
		SceneJson::Read(root, "rotation", view.rotation);
		SceneJson::Read(root, "rotationQuat", view.rotationQuat);
		SceneJson::Read(root, "scale", view.scale);
		view.velocity = { 0.0f, 0.0f, 0.0f };
		view.isActive = true;
		view.isGrounded = false;
		view.isColliding = false;
	}
}

namespace {
	template <typename ResolveFn>
	const SceneJson::Json* LoadPrefab(EditorSuite::PrefabCache& cache, ResolveFn&& resolvePath, const std::string& name, std::string& outError, int depth = 0);

	int NodeKeyOf(const SceneJson::Json& node, int fallback)
	{
		int key = fallback;
		SceneJson::Read(node, "prefabNodeKey", key);
		return key;
	}

	const SceneJson::Json* NodeByKey(const SceneJson::Json& objects, int key)
	{
		for (size_t i = 0; i < objects.size(); ++i) {
			if (objects[i].is_object() && NodeKeyOf(objects[i], static_cast<int>(i)) == key) return &objects[i];
		}
		return nullptr;
	}
}

std::filesystem::path EditorSuite::GetPrefabDirectory() const
{
	if (!currentProjectRoot_.empty()) return currentProjectRoot_ / "Assets" / "Prefabs";
	return std::filesystem::path("Assets") / "Prefabs";
}

std::filesystem::path EditorSuite::ResolvePrefabFile(const std::string& prefabName) const
{
	const std::string fileName = prefabName + ".prefab.json";
	std::filesystem::path path = GetPrefabDirectory() / fileName;
	std::error_code ec;
	if (std::filesystem::exists(path, ec) || currentProjectRoot_.empty()) return path;
	// Moved into a subfolder (or elsewhere inside Assets/) from the Project panel
	if (const std::string moved = AssetDb().FindByFileName(fileName); !moved.empty()) return AssetDb().ToAbsolute(moved);
	return path;
}

void EditorSuite::ClearPrefabCache()
{
	prefabCache_.reset();
}

bool EditorSuite::SavePrefab(uint32_t rootId, const std::string& prefabName, const std::string& variantOf)
{
	using SceneJson::Json;
	RebuildHierarchyLinks();
	const int rootIndex = FindGameObjectIndexById(rootId);
	if (rootIndex < 0) {
		LogMessage(std::format("[Prefab] Cannot save: GameObject {} does not exist.", rootId), 2);
		return false;
	}

	// Root first, then descendants depth-first (parents always precede their children)
	std::vector<size_t> order;
	std::vector<size_t> stack{ static_cast<size_t>(rootIndex) };
	while (!stack.empty()) {
		const size_t index = stack.back();
		stack.pop_back();
		order.push_back(index);
		const auto& kids = gameObjects_[index].childrenIds;
		for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
			const int childIndex = FindGameObjectIndexById(*it);
			if (childIndex >= 0) stack.push_back(static_cast<size_t>(childIndex));
		}
	}

	const std::string name = SanitizePrefabName(prefabName);
	if (!prefabCache_) prefabCache_ = std::make_shared<PrefabCache>();
	const auto resolve = [this](const std::string& n) { return ResolvePrefabFile(n); };
	std::string loadError;
	// A variant stays a variant when it is saved again (Apply, Prefab Mode)
	std::string base = variantOf;
	if (base.empty()) {
		std::error_code existsEc;
		if (std::filesystem::exists(ResolvePrefabFile(name), existsEc)) {
			if (const Json* existing = LoadPrefab(*prefabCache_, resolve, name, loadError)) SceneJson::Read(*existing, "basePrefab", base);
		}
	}
	if (base == name) {
		LogMessage(std::format("[Prefab] '{}' cannot be a variant of itself.", name), 2);
		return false;
	}
	const Json* baseObjects = nullptr;
	if (!base.empty()) {
		const Json* basePrefab = LoadPrefab(*prefabCache_, resolve, base, loadError);
		baseObjects = basePrefab ? PrefabObjects(*basePrefab) : nullptr;
		if (!baseObjects) {
			LogMessage(std::format("[Prefab] Cannot save '{}': its base prefab '{}' is missing.", name, base), 2);
			return false;
		}
	}

	// Nested prefabs: a subtree that is an instance of another prefab (or was one inside an instance of this prefab)
	struct Nested { std::string prefab; int key = -1; };
	std::vector<Nested> nested(order.size());
	std::unordered_map<uint32_t, size_t> orderOf;
	for (size_t i = 0; i < order.size(); ++i) orderOf[gameObjects_[order[i]].id] = i;
	const auto nestedRootInside = [&](size_t i, const std::string& prefab, bool fromInstance) {
		for (size_t at = i; at != 0;) {
			const GameObject& node = gameObjects_[order[at]];
			const bool isNestedRoot = fromInstance ? (node.prefabSource == prefab && node.prefabNodeIndex == 0) : (node.nestedPrefab == prefab && node.nestedNodeKey == 0);
			if (isNestedRoot) return true;
			const auto parent = node.parentId >= 0 ? orderOf.find(static_cast<uint32_t>(node.parentId)) : orderOf.end();
			if (parent == orderOf.end()) return false;
			at = parent->second;
		}
		return false;
	};
	for (size_t i = 1; i < order.size(); ++i) {
		const GameObject& node = gameObjects_[order[i]];
		if (node.prefabSource == name && node.prefabNodeIndex == 0) {
			LogMessage(std::format("[Prefab] Cannot save '{}': it contains an instance of itself.", name), 2);
			return false;
		}
		if (!node.prefabSource.empty() && node.prefabSource != name && node.prefabSource != base && nestedRootInside(i, node.prefabSource, true)) {
			nested[i] = { node.prefabSource, node.prefabNodeKey >= 0 ? node.prefabNodeKey : node.prefabNodeIndex };
		} else if (!node.nestedPrefab.empty() && nestedRootInside(i, node.nestedPrefab, false)) {
			nested[i] = { node.nestedPrefab, node.nestedNodeKey };
		}
		if (nested[i].prefab == name) {
			LogMessage(std::format("[Prefab] Cannot save '{}': it contains an instance of itself.", name), 2);
			return false;
		}
	}

	// Node keys: kept for nodes that already belong to this prefab (or its base), new ones for the rest; the root is always 0
	std::vector<int> keys(order.size(), -1);
	int nextKey = 1;
	if (baseObjects) {
		for (size_t i = 0; i < baseObjects->size(); ++i) nextKey = (std::max)(nextKey, NodeKeyOf((*baseObjects)[i], static_cast<int>(i)) + 1);
	}
	for (size_t i = 1; i < order.size(); ++i) {
		const GameObject& node = gameObjects_[order[i]];
		if ((node.prefabSource == name || (!base.empty() && node.prefabSource == base)) && node.prefabNodeKey > 0) {
			keys[i] = node.prefabNodeKey;
			nextKey = (std::max)(nextKey, node.prefabNodeKey + 1);
		}
	}
	keys[0] = 0;
	for (size_t i = 1; i < order.size(); ++i) {
		if (keys[i] < 0 || std::count(keys.begin(), keys.end(), keys[i]) > 1) keys[i] = nextKey++;
	}
	Json prefab = Json::object();
	prefab["prefabVersion"] = 1;
	prefab["name"] = name;
	if (!base.empty()) prefab["basePrefab"] = base;
	Json objects = Json::array();
	for (size_t i = 0; i < order.size(); ++i) {
		Json o;
		WriteGameObjectJson(gameObjects_[order[i]], &o);
		o.erase("prefab"); // the instance it came from is not part of the prefab
		o.erase("prefabNode");
		o.erase("prefabOverrides");
		o.erase("prefabGuid");
		o.erase("nestedPrefab");
		o.erase("nestedNode");
		o["prefabNodeKey"] = keys[i];
		if (i == 0) {
			o["parentId"] = -1; // the prefab root is always a scene root when instantiated
			o["attachedParentId"] = -1;
		}
		if (const Json* baseNode = baseObjects ? NodeByKey(*baseObjects, keys[i]) : nullptr) {
			std::vector<std::string> differences;
			CollectPrefabOverrideKeys(*baseNode, o, i == 0, differences);
			o["variantOverrides"] = differences;
		} else if (!nested[i].prefab.empty()) {
			o["nestedPrefab"] = nested[i].prefab;
			o["nestedNode"] = nested[i].key;
			std::vector<std::string> differences;
			const Json* inner = LoadPrefab(*prefabCache_, resolve, nested[i].prefab, loadError);
			const Json* innerObjects = inner ? PrefabObjects(*inner) : nullptr;
			if (const Json* innerNode = innerObjects ? NodeByKey(*innerObjects, nested[i].key) : nullptr) {
				CollectPrefabOverrideKeys(*innerNode, o, nested[i].key == 0, differences);
			}
			o["nestedOverrides"] = differences;
		}
		objects.push_back(std::move(o));
	}
	if (baseObjects) {
		std::unordered_set<int> present(keys.begin(), keys.end());
		Json removed = Json::array();
		for (size_t i = 0; i < baseObjects->size(); ++i) {
			const int key = NodeKeyOf((*baseObjects)[i], static_cast<int>(i));
			if (key != 0 && !present.contains(key)) removed.push_back(key);
		}
		if (!removed.empty()) prefab["removedNodes"] = std::move(removed);
	}
	prefab["objects"] = std::move(objects);

	std::error_code ec;
	const std::filesystem::path path = ResolvePrefabFile(name);
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		LogMessage(std::format("[Prefab] Cannot write '{}'.", path.string()), 2);
		return false;
	}
	out << SceneJson::DumpCompact(prefab);
	out.close();

	// Variants and prefabs that nest this one are resolved from it: load them all again
	ClearPrefabCache();
	LogMessage(std::format("[Prefab] Saved '{}' ({} object(s)) to {}", name, order.size(), path.string()), 0);
	return true;
}

namespace {
	using SceneJson::Json;
	constexpr int kMaxPrefabDepth = 8; // nesting / variant chains; deeper = a loop (A inside B inside A)

	uint32_t IdOf(const Json& node)
	{
		uint32_t id = 0;
		SceneJson::Read(node, "id", id);
		return id;
	}

	// A Joint that connects to another node of the same prefab follows that node's id
	void RemapJointJson(Json& node, const std::unordered_map<uint32_t, uint32_t>& ids)
	{
		Json* components = node.contains("components") ? &node["components"] : nullptr;
		if (!components || !components->is_object() || !components->contains("Joint") || !(*components)["Joint"].is_object()) return;
		Json& joint = (*components)["Joint"];
		int connected = 0;
		if (!SceneJson::Read(joint, "connectedObjectId", connected) || connected <= 0) return;
		const auto it = ids.find(static_cast<uint32_t>(connected));
		if (it != ids.end()) joint["connectedObjectId"] = static_cast<int>(it->second);
	}

	// Replaces every nested subtree with the inner prefab as it is now, plus the overrides stored with it
	template <typename LoadFn>
	bool ResolveNested(Json& objects, LoadFn&& load)
	{
		uint32_t nextId = 1;
		int nextKey = 1;
		for (size_t i = 0; i < objects.size(); ++i) {
			nextId = (std::max)(nextId, IdOf(objects[i]) + 1);
			nextKey = (std::max)(nextKey, NodeKeyOf(objects[i], static_cast<int>(i)) + 1);
		}
		std::vector<bool> taken(objects.size(), false);
		Json out = Json::array();
		for (size_t i = 0; i < objects.size(); ++i) {
			if (taken[i]) continue;
			const Json& rootNode = objects[i];
			std::string prefab;
			int nestedKey = -1;
			if (!SceneJson::Read(rootNode, "nestedPrefab", prefab) || prefab.empty() || !SceneJson::Read(rootNode, "nestedNode", nestedKey) || nestedKey != 0) {
				out.push_back(rootNode);
				continue;
			}
			// The nodes of this nested instance: descendants that came from the same inner prefab
			std::unordered_set<uint32_t> members{ IdOf(rootNode) };
			std::unordered_map<int, const Json*> byInnerKey{ { 0, &rootNode } };
			for (size_t j = i + 1; j < objects.size(); ++j) {
				std::string other;
				int key = -1;
				int parent = -1;
				SceneJson::Read(objects[j], "parentId", parent);
				if (taken[j] || parent < 0 || !members.contains(static_cast<uint32_t>(parent))) continue;
				if (!SceneJson::Read(objects[j], "nestedPrefab", other) || other != prefab || !SceneJson::Read(objects[j], "nestedNode", key) || key == 0) continue;
				members.insert(IdOf(objects[j]));
				byInnerKey.emplace(key, &objects[j]);
				taken[j] = true;
			}
			const Json* inner = load(prefab);
			const Json* innerObjects = inner ? SceneJson::Find(*inner, "objects") : nullptr;
			if (!innerObjects || !innerObjects->is_array()) return false;
			std::unordered_map<uint32_t, uint32_t> innerIds; // inner id -> id in this prefab
			const size_t first = out.size();
			for (size_t k = 0; k < innerObjects->size(); ++k) {
				Json node = (*innerObjects)[k];
				const int innerKey = NodeKeyOf(node, static_cast<int>(k));
				const auto outer = byInnerKey.find(innerKey);
				if (outer != byInnerKey.end()) {
					const Json& saved = *outer->second;
					if (const Json* overrides = SceneJson::Find(saved, "nestedOverrides"); overrides && overrides->is_array()) {
						for (const Json& key : *overrides) {
							if (key.is_string()) MergePrefabOverride(node, saved, key.get<std::string>());
						}
						node["nestedOverrides"] = *overrides;
					}
					if (innerKey == 0) {
						// Where the inner prefab sits belongs to the outer one
						for (const char* placement : { "translation", "rotation", "rotationQuat", "parentId", "attachedParentId" }) {
							if (const Json* value = SceneJson::Find(saved, placement)) node[placement] = *value;
						}
					}
					node["id"] = IdOf(saved);
					node["prefabNodeKey"] = NodeKeyOf(saved, -1);
				} else {
					node["id"] = nextId++; // added to the inner prefab since this one was saved
					node["prefabNodeKey"] = nextKey++;
				}
				innerIds[IdOf((*innerObjects)[k])] = IdOf(node);
				node["nestedPrefab"] = prefab;
				node["nestedNode"] = innerKey;
				out.push_back(std::move(node));
			}
			for (size_t k = first; k < out.size(); ++k) RemapJointJson(out[k], innerIds);
			for (size_t k = first + 1; k < out.size(); ++k) {
				for (const char* link : { "parentId", "attachedParentId" }) {
					int value = -1;
					if (!SceneJson::Read(out[k], link, value) || value < 0) continue;
					const auto mapped = innerIds.find(static_cast<uint32_t>(value));
					out[k][link] = mapped != innerIds.end() ? static_cast<int>(mapped->second) : (std::string_view(link) == "parentId" ? static_cast<int>(IdOf(out[first])) : -1);
				}
			}
		}
		objects = std::move(out);
		return true;
	}

	// A variant: the base as it is now, with the variant's overrides and added nodes
	bool ResolveVariant(Json& objects, const Json& variantPrefab, const Json& baseObjects)
	{
		Json result = baseObjects;
		std::unordered_set<uint32_t> usedIds;
		uint32_t nextId = 1;
		for (const Json& node : result) {
			usedIds.insert(IdOf(node));
			nextId = (std::max)(nextId, IdOf(node) + 1);
		}
		for (const Json& node : objects) nextId = (std::max)(nextId, IdOf(node) + 1);
		// Nodes of the base the variant deleted (with whatever hangs below them)
		std::unordered_set<int> removedKeys;
		if (const Json* removed = SceneJson::Find(variantPrefab, "removedNodes"); removed && removed->is_array()) {
			for (const Json& key : *removed) {
				if (key.is_number_integer() && key.get<int>() != 0) removedKeys.insert(key.get<int>());
			}
		}
		if (!removedKeys.empty()) {
			std::unordered_set<uint32_t> removedIds;
			Json kept = Json::array();
			for (size_t r = 0; r < result.size(); ++r) {
				int parent = -1;
				SceneJson::Read(result[r], "parentId", parent);
				if (removedKeys.contains(NodeKeyOf(result[r], static_cast<int>(r))) || (parent >= 0 && removedIds.contains(static_cast<uint32_t>(parent)))) {
					removedIds.insert(IdOf(result[r]));
					continue;
				}
				kept.push_back(std::move(result[r]));
			}
			result = std::move(kept);
		}
		std::unordered_map<uint32_t, uint32_t> ids; // variant id -> id in the result
		std::vector<size_t> fromVariant;             // result nodes whose joint the variant may have set
		const size_t firstAdded = result.size();
		for (size_t i = 0; i < objects.size(); ++i) {
			const Json& node = objects[i];
			const int key = NodeKeyOf(node, static_cast<int>(i));
			Json* target = nullptr;
			for (size_t r = 0; r < firstAdded; ++r) {
				if (NodeKeyOf(result[r], static_cast<int>(r)) == key) { target = &result[r]; break; }
			}
			if (target) {
				if (const Json* overrides = SceneJson::Find(node, "variantOverrides"); overrides && overrides->is_array()) {
					for (const Json& entry : *overrides) {
						if (!entry.is_string()) continue;
						MergePrefabOverride(*target, node, entry.get<std::string>());
						if (entry.get<std::string>().rfind("components/Joint", 0) == 0) fromVariant.push_back(static_cast<size_t>(target - &result[0]));
					}
				}
				ids[IdOf(node)] = IdOf(*target);
				continue;
			}
			Json added = node;
			const uint32_t id = usedIds.contains(IdOf(node)) ? nextId++ : IdOf(node);
			usedIds.insert(id);
			added["id"] = id;
			added.erase("variantOverrides");
			ids[IdOf(node)] = id;
			result.push_back(std::move(added));
		}
		for (size_t r = firstAdded; r < result.size(); ++r) {
			fromVariant.push_back(r);
			for (const char* link : { "parentId", "attachedParentId" }) {
				int value = -1;
				if (!SceneJson::Read(result[r], link, value) || value < 0) continue;
				const auto mapped = ids.find(static_cast<uint32_t>(value));
				result[r][link] = mapped != ids.end() ? static_cast<int>(mapped->second) : (std::string_view(link) == "parentId" ? static_cast<int>(IdOf(result[0])) : -1);
			}
		}
		for (size_t r : fromVariant) RemapJointJson(result[r], ids);
		objects = std::move(result);
		return true;
	}

	template <typename ResolveFn>
	const SceneJson::Json* LoadPrefab(EditorSuite::PrefabCache& cache, ResolveFn&& resolvePath, const std::string& name, std::string& outError, int depth)
	{
		auto it = cache.loaded.find(name);
		if (it != cache.loaded.end()) return &it->second;
		if (cache.missing.contains(name)) return nullptr;

		const std::filesystem::path path = resolvePath(name);
		std::ifstream in(path, std::ios::binary);
		if (!in) {
			cache.missing.insert(name);
			outError = std::format("[Prefab] Prefab '{}' not found ({}).", name, path.string());
			return nullptr;
		}
		std::stringstream buffer;
		buffer << in.rdbuf();
		std::string parseError;
		std::optional<SceneJson::Json> parsed = SceneJson::Parse(buffer.str(), &parseError);
		if (!parsed || !PrefabObjects(*parsed)) {
			cache.missing.insert(name);
			outError = std::format("[Prefab] Prefab '{}' is invalid: {}", name, parseError.empty() ? "no objects" : parseError);
			return nullptr;
		}
		if (depth > kMaxPrefabDepth) {
			cache.missing.insert(name);
			outError = std::format("[Prefab] Prefab '{}' contains itself (through nested prefabs or variants).", name);
			return nullptr;
		}
		const auto load = [&](const std::string& other) { return LoadPrefab(cache, resolvePath, other, outError, depth + 1); };
		Json& objects = (*parsed)["objects"];
		bool resolved = ResolveNested(objects, load);
		std::string base;
		if (resolved && SceneJson::Read(*parsed, "basePrefab", base) && !base.empty()) {
			const Json* basePrefab = load(base);
			const Json* baseObjects = basePrefab ? PrefabObjects(*basePrefab) : nullptr;
			resolved = baseObjects && ResolveVariant(objects, *parsed, *baseObjects);
		}
		if (!resolved || !PrefabObjects(*parsed)) {
			cache.missing.insert(name);
			if (outError.empty()) outError = std::format("[Prefab] Prefab '{}' uses a prefab that is missing.", name);
			return nullptr;
		}
		return &cache.loaded.emplace(name, std::move(*parsed)).first->second;
	}
}

std::string EditorSuite::PrefabBaseOf(const std::string& prefabName)
{
	if (!prefabCache_) prefabCache_ = std::make_shared<PrefabCache>();
	std::string error;
	std::string base;
	if (const SceneJson::Json* prefab = LoadPrefab(*prefabCache_, [this](const std::string& n) { return ResolvePrefabFile(n); }, prefabName, error)) {
		SceneJson::Read(*prefab, "basePrefab", base);
	}
	return base;
}

bool EditorSuite::HasPrefab(const std::string& prefabName)
{
	if (!prefabCache_) prefabCache_ = std::make_shared<PrefabCache>();
	std::string error;
	const bool found = LoadPrefab(*prefabCache_, [this](const std::string& n) { return ResolvePrefabFile(n); }, prefabName, error) != nullptr;
	if (!error.empty()) LogMessage(error, 1);
	return found;
}

// Creates and initializes the script instances of a freshly spawned (or reused) object while the game runs
void EditorSuite::StartScriptsOnSpawnedObject(uint32_t id, bool createInstances)
{
	GameObject* obj = FindGameObjectById(id, false);
	if (!obj) return;
	for (size_t s = 0; s < obj->scripts.size(); ++s) {
		auto& sc = obj->scripts[s];
		if (createInstances) {
			sc.instance.reset();
			if (sc.className.empty()) continue;
			if (!scriptModule_.IsLoaded() || !scriptModule_.IsRegistered(sc.className)) {
				sc.isMissingScript = true;
				continue;
			}
			sc.instance = ScriptInstancePtr(scriptModule_.CreateScript(sc.className), ScriptInstanceDeleter{ &scriptModule_ });
			sc.isMissingScript = false;
		}
		if (!sc.instance) continue;
		ApplySavedFieldsToScript(sc); // pooled objects start again from the prefab's field values
	}

	if (!obj->isActive) return;
	// Objects that appear during Play (spawned, pooled, from LoadScene) start their sound like those of the first scene
	if ((obj->hasAudioSource || obj->type == GameObjectType::AudioSource) && obj->audioPlayOnAwake) StartAudioSource(*obj);
	for (size_t s = 0; s < obj->scripts.size(); ++s) {
		obj = FindGameObjectById(id, false); // re-fetch: scripts must not keep references across callbacks
		if (!obj || s >= obj->scripts.size()) return;
		ScriptBase* instance = obj->scripts[s].instance.get();
		if (!instance) continue;
		ScriptGameObjectView* view = GetOrUpdateScriptViewById(id);
		instance->SetOwner(view, id);
		instance->SetEngineAPI(&engineApi_);
		instance->SetHost(this, this);
		CallScript(instance, id, "Initialize", [&] { instance->Initialize(); });
		if (view) {
			if (GameObject* again = FindGameObjectById(id, false)) SyncViewToGameObject(*view, *again);
		}
	}
}

uint32_t EditorSuite::InstantiatePrefabNow(const std::string& prefabName, const Vector3& position, uint32_t rootId, const ScriptGameObjectView* rootOverride, bool hidden)
{
	using SceneJson::Json;
	if (!prefabCache_) prefabCache_ = std::make_shared<PrefabCache>();
	std::string error;
	const Json* prefab = LoadPrefab(*prefabCache_, [this](const std::string& n) { return ResolvePrefabFile(n); }, prefabName, error);
	if (!error.empty()) LogMessage(error, 1);
	if (!prefab) return 0;
	const Json& objects = *PrefabObjects(*prefab);

	// Old (saved) id -> new id
	std::unordered_map<uint32_t, uint32_t> idMap;
	std::vector<uint32_t> newIds(objects.size(), 0);
	const auto nodeKey = [&](size_t i) {
		int key = static_cast<int>(i);
		if (objects[i].is_object()) SceneJson::Read(objects[i], "prefabNodeKey", key);
		return key;
	};
	for (size_t i = 0; i < objects.size(); ++i) {
		const uint32_t keptId = prefabRebuild_ ? PrefabRebuildId(nodeKey(i)) : 0u;
		newIds[i] = keptId != 0 ? keptId : (i == 0 && rootId != 0) ? rootId : nextObjectId_++;
		uint32_t savedId = 0;
		if (objects[i].is_object() && SceneJson::Read(objects[i], "id", savedId)) idMap[savedId] = newIds[i];
	}

	gameObjects_.reserve(gameObjects_.size() + objects.size());
	for (size_t i = 0; i < objects.size(); ++i) {
		if (!objects[i].is_object()) continue;
		GameObject obj;
		obj.id = newIds[i];
		if (prefabRebuild_) {
			Json node = objects[i];
			MergePrefabRebuild(nodeKey(i), &node); // the instance keeps its overrides
			ReadGameObjectJson(&node, obj);
		} else {
			ReadGameObjectJson(&objects[i], obj);
		}
		obj.prefabSource = prefabName;
		obj.prefabNodeIndex = static_cast<int>(i);
		obj.prefabNodeKey = nodeKey(i);
		if (hidden) {
			obj.isActive = false; // StartScriptsOnSpawnedObject skips Initialize until the instance is spawned
			// The first draw of a model would load it: do that now instead
			if (obj.meshType == GameObject::MeshType::CustomModel && !obj.modelPath.empty()) {
				const std::filesystem::path modelFile(ResolveAssetPath(obj.modelPath));
				Model::CreateFromFile(modelFile.parent_path().string(), modelFile.filename().string());
			}
		}
		if (i == 0) {
			obj.parentId = -1;
			obj.translation = position;
		} else {
			auto parent = (obj.parentId >= 0) ? idMap.find(static_cast<uint32_t>(obj.parentId)) : idMap.end();
			obj.parentId = (parent != idMap.end()) ? static_cast<int>(parent->second) : static_cast<int>(newIds[0]);
		}
		if (obj.attachedParentId >= 0) {
			auto attached = idMap.find(static_cast<uint32_t>(obj.attachedParentId));
			obj.attachedParentId = (attached != idMap.end()) ? static_cast<int>(attached->second) : -1;
		}
		RemapJointTarget(obj, idMap);
		gameObjects_.push_back(std::move(obj));
	}

	if (rootOverride) {
		if (GameObject* root = FindGameObjectById(newIds[0], false)) SyncViewToGameObject(*rootOverride, *root);
	}
	if (objects.size() > 1) {
		RebuildHierarchyLinks();
	}

	const bool gameRunning = (sceneState_ != EditorSceneState::Edit) || IsStandaloneMode();
	if (gameRunning) {
		for (uint32_t id : newIds) StartScriptsOnSpawnedObject(id, true);
		if (!hidden) {
			for (uint32_t id : newIds) BurstOneShotParticles(id);
		}
	} else {
		EnsureEditModeScriptInstances();
	}
	return newIds[0];
}

ScriptGameObjectView* EditorSuite::QueuePrefabSpawn(const char* prefabName, const Vector3& position)
{
	if (!prefabName || prefabName[0] == '\0') return nullptr;
	const std::string name = prefabName;
	if (!prefabCache_) prefabCache_ = std::make_shared<PrefabCache>();
	std::string error;
	const SceneJson::Json* prefab = LoadPrefab(*prefabCache_, [this](const std::string& n) { return ResolvePrefabFile(n); }, name, error);
	if (!error.empty()) LogMessage(error, 1);
	if (!prefab) return nullptr;
	const SceneJson::Json& root = (*PrefabObjects(*prefab))[0];

	PendingSpawn spawn{};
	spawn.prefabName = name;

	// Reuse a despawned instance when one is available
	auto poolIt = prefabPools_.find(name);
	while (poolIt != prefabPools_.end() && !poolIt->second.empty()) {
		const uint32_t id = poolIt->second.back();
		poolIt->second.pop_back();
		const GameObject* pooled = FindGameObjectById(id, false);
		if (!pooled || pooled->isActive || pooled->prefabSource != name || pooled->prefabNodeIndex != 0) continue;

		ScriptGameObjectView* view = GetOrUpdateScriptViewById(id);
		if (!view) continue;
		FillViewFromPrefabRoot(root, *view);
		view->translation = position;
		spawn.id = id;
		spawn.name = pooled->name;
		spawn.reusePooled = true;
		pendingSpawns_.push_back(std::move(spawn));
		return view;
	}

	spawn.id = nextObjectId_++;
	auto view = std::make_unique<ScriptGameObjectView>();
	view->id = spawn.id;
	FillViewFromPrefabRoot(root, *view);
	view->translation = position;
	spawn.name = view->name;
	ScriptGameObjectView* rawView = view.get();
	scriptObjectViews_[spawn.id] = std::move(view);
	pendingSpawns_.push_back(std::move(spawn));
	return rawView;
}

void EditorSuite::QueueDespawn(uint32_t id)
{
	CancelDelayedDespawn(id);
	if (CancelPendingSpawn(id)) return; // never created
	if (std::find(pendingDespawns_.begin(), pendingDespawns_.end(), id) == pendingDespawns_.end()) {
		pendingDespawns_.push_back(id);
	}
}

void EditorSuite::ProcessPendingDespawns()
{
	if (pendingDespawns_.empty()) return;
	std::vector<uint32_t> despawns;
	despawns.swap(pendingDespawns_);

	for (uint32_t id : despawns) {
		GameObject* obj = FindGameObjectById(id, false);
		if (!obj) continue;
		if (obj->prefabSource.empty() || obj->prefabNodeIndex != 0) {
			QueueDeleteGameObjectById(id); // not a prefab root: Despawn == Destroy
			continue;
		}
		if (!obj->isActive) continue; // already pooled

		// Hide the whole instance; scripts of inactive objects do not run and colliders are skipped
		std::vector<uint32_t> stack{ id };
		while (!stack.empty()) {
			GameObject* node = FindGameObjectById(stack.back(), false);
			stack.pop_back();
			if (!node) continue;
			node->isActive = false;
			node->velocity = { 0.0f, 0.0f, 0.0f };
			node->isColliding = false;
			for (uint32_t child : node->childrenIds) stack.push_back(child);
			if (auto viewIt = scriptObjectViews_.find(node->id); viewIt != scriptObjectViews_.end() && viewIt->second) {
				viewIt->second->isActive = false;
			}
		}
		auto& pool = prefabPools_[obj->prefabSource];
		if (std::find(pool.begin(), pool.end(), id) == pool.end()) pool.push_back(id);
	}
}

// Puts a pooled instance back into its prefab state (all nodes) before it is handed out again
void EditorSuite::ResetPooledPrefabInstance(uint32_t rootId, const ScriptGameObjectView* rootView)
{
	GameObject* root = FindGameObjectById(rootId, false);
	if (!root || !prefabCache_) return;
	auto it = prefabCache_->loaded.find(root->prefabSource);
	if (it == prefabCache_->loaded.end()) return;
	const SceneJson::Json& objects = *PrefabObjects(it->second);

	std::vector<uint32_t> instanceIds;
	std::vector<uint32_t> stack{ rootId };
	while (!stack.empty()) {
		GameObject* node = FindGameObjectById(stack.back(), false);
		stack.pop_back();
		if (!node) continue;
		instanceIds.push_back(node->id);
		for (uint32_t child : node->childrenIds) stack.push_back(child);

		const int nodeIndex = node->prefabNodeIndex;
		if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= objects.size()) continue;
		const SceneJson::Json& src = objects[static_cast<size_t>(nodeIndex)];
		node->isActive = true;
		SceneJson::Read(src, "isActive", node->isActive);
		SceneJson::Read(src, "translation", node->translation);
		SceneJson::Read(src, "rotation", node->rotation);
		SceneJson::Read(src, "rotationQuat", node->rotationQuat);
		SceneJson::Read(src, "scale", node->scale);
		SceneJson::Read(src, "layer", node->layer);
		node->layer &= 31u;
		SceneJson::Read(src, "collisionMask", node->collisionMask);
		ReadDataComponentsJson(&src, *node); // e.g. Health back to the prefab value
		node->velocity = { 0.0f, 0.0f, 0.0f };
		node->isGrounded = false;
		node->isColliding = false;
	}

	root = FindGameObjectById(rootId, false);
	if (root && rootView) SyncViewToGameObject(*rootView, *root);
	for (uint32_t id : instanceIds) StartScriptsOnSpawnedObject(id, false);
	for (uint32_t id : instanceIds) BurstOneShotParticles(id);
}

void EditorSuite::ResetPrefabPools()
{
	prefabPools_.clear();
	pendingDespawns_.clear();
	pendingPrewarms_.clear();
}

bool EditorSuite::QueuePrefabPrewarm(const char* prefabName, int count)
{
	const bool gameRunning = (sceneState_ != EditorSceneState::Edit) || IsStandaloneMode();
	if (!gameRunning || !prefabName || prefabName[0] == '\0' || count <= 0) return false;
	if (!prefabCache_) prefabCache_ = std::make_shared<PrefabCache>();
	std::string error;
	const std::string name = prefabName;
	const bool found = LoadPrefab(*prefabCache_, [this](const std::string& n) { return ResolvePrefabFile(n); }, name, error) != nullptr;
	if (!error.empty()) LogMessage(error, 1);
	if (found) pendingPrewarms_.emplace_back(name, (std::min)(count, 4096));
	return found;
}

void EditorSuite::ProcessPendingPrewarms()
{
	if (pendingPrewarms_.empty()) return;
	std::vector<std::pair<std::string, int>> prewarms;
	prewarms.swap(pendingPrewarms_);
	for (const auto& [name, count] : prewarms) {
		for (int i = 0; i < count; ++i) {
			const uint32_t id = InstantiatePrefabNow(name, { 0.0f, 0.0f, 0.0f }, 0, nullptr, true);
			if (id == 0) break;
			prefabPools_[name].push_back(id);
		}
	}
	RefreshAllScriptObjectViews();
}

size_t EditorSuite::GetPooledCount(const std::string& prefabName) const
{
	auto it = prefabPools_.find(prefabName);
	return (it != prefabPools_.end()) ? it->second.size() : 0;
}
