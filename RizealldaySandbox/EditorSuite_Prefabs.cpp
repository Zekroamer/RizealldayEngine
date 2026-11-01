#include "EditorSuiteInternal.h"
#include "SceneJson.h"

// Prefab file: { "prefabVersion": 1, "name": "...", "objects": [ root, descendants... ] }
// Each entry is a WriteGameObjectJson object; ids/parentIds are remapped to fresh ids on instantiate.
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

bool EditorSuite::SavePrefab(uint32_t rootId, const std::string& prefabName)
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
	Json prefab = Json::object();
	prefab["prefabVersion"] = 1;
	prefab["name"] = name;
	Json objects = Json::array();
	for (size_t i = 0; i < order.size(); ++i) {
		Json o;
		WriteGameObjectJson(gameObjects_[order[i]], &o);
		if (i == 0) {
			o["parentId"] = -1; // the prefab root is always a scene root when instantiated
			o["attachedParentId"] = -1;
		}
		objects.push_back(std::move(o));
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

	if (prefabCache_) {
		prefabCache_->loaded.erase(name);
		prefabCache_->missing.erase(name);
	}
	LogMessage(std::format("[Prefab] Saved '{}' ({} object(s)) to {}", name, order.size(), path.string()), 0);
	return true;
}

namespace {
	template <typename ResolveFn>
	const SceneJson::Json* LoadPrefab(EditorSuite::PrefabCache& cache, ResolveFn&& resolvePath, const std::string& name, std::string& outError)
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
		return &cache.loaded.emplace(name, std::move(*parsed)).first->second;
	}
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
	for (size_t i = 0; i < objects.size(); ++i) {
		newIds[i] = (i == 0 && rootId != 0) ? rootId : nextObjectId_++;
		uint32_t savedId = 0;
		if (objects[i].is_object() && SceneJson::Read(objects[i], "id", savedId)) idMap[savedId] = newIds[i];
	}

	gameObjects_.reserve(gameObjects_.size() + objects.size());
	for (size_t i = 0; i < objects.size(); ++i) {
		if (!objects[i].is_object()) continue;
		GameObject obj;
		obj.id = newIds[i];
		ReadGameObjectJson(&objects[i], obj);
		obj.prefabSource = prefabName;
		obj.prefabNodeIndex = static_cast<int>(i);
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
