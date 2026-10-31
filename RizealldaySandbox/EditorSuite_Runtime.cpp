#include "EditorSuiteInternal.h"
#include "SceneJson.h"
#include "audio/Audio.h"

// Runtime services for game scripts: scene switching and persistent save data.

// ---------------------------------------------------------------------------
// Scene switching
// ---------------------------------------------------------------------------

std::filesystem::path EditorSuite::ResolveSceneRequest(const std::string& nameOrPath) const
{
	std::filesystem::path p(nameOrPath);
	if (!nameOrPath.ends_with(".json")) {
		// Bare name: "Stage2" -> <project>/Assets/Scenes/Stage2.scene.json
		return GetScenesDirectory() / (nameOrPath + ".scene.json");
	}
	if (p.is_absolute()) return p;
	return std::filesystem::path(ResolveAssetPath(nameOrPath));
}

void EditorSuite::RecordSmokeFrame()
{
	if (++smokeFrameIndex_ <= 30) return; // loading hitches are not the steady state
	const ProfilerSnapshot& frame = Profiler::GetInstance()->GetLastFrame();
	smokeFrameMs_.push_back(frame.frameMs);
	for (const ProfilerScopeResult& scope : frame.cpu) smokeScopeMs_[scope.name] += scope.ms;
	smokeScopeMs_["GPU Frame"] += frame.gpuFrameMs;
	smokeDrawCalls_ = frame.drawCalls;
}

std::string EditorSuite::SmokeTimingReport() const
{
	if (smokeFrameMs_.empty()) return {};
	std::vector<float> sorted = smokeFrameMs_;
	std::sort(sorted.begin(), sorted.end());
	const float count = static_cast<float>(sorted.size());
	float total = 0.0f;
	for (float ms : sorted) total += ms;
	std::string report = std::format("\nFrames {}, avg {:.2f} ms ({:.0f} FPS), p95 {:.2f} ms, max {:.2f} ms, draw calls {}",
		sorted.size(), total / count, 1000.0f * count / total, sorted[static_cast<size_t>(count * 0.95f)], sorted.back(), smokeDrawCalls_);
	for (const auto& [name, ms] : smokeScopeMs_) report += std::format("\n  {}: {:.2f} ms", name, ms / count);
	return report;
}

void EditorSuite::FinishSmokeRun()
{
	std::string error = smokeError_;
	if (error.empty() && !isStandaloneGameMode_) error = "Not running as an exported game (game_build.cfg missing or invalid).";
	if (error.empty() && !scriptModule_.GetLastLoadError().empty()) error = scriptModule_.GetLastLoadError();
	for (const auto& obj : gameObjects_) {
		for (const auto& sc : obj.scripts) {
			if (error.empty() && !sc.className.empty() && !sc.instance) error = std::format("Script '{}' on '{}' did not load.", sc.className, obj.name);
		}
	}
	std::ofstream(kSmokeResultFile, std::ios::trunc) << (error.empty() ? std::format("OK: {} objects", gameObjects_.size()) : "FAILED: " + error) << SmokeTimingReport() << "\n";
	sSelfTestExitCode = error.empty() ? 0 : 1;
	PostQuitMessage(0);
}

bool EditorSuite::KeepAcrossScenes(uint32_t id)
{
	const GameObject* obj = FindGameObjectById(id, false);
	const bool gameRunning = (sceneState_ != EditorSceneState::Edit) || IsStandaloneMode();
	if (!obj || !gameRunning) return false;
	if (obj->parentId != -1) {
		LogMessage(std::format("[Scene] KeepAcrossScenes('{}'): only root objects can be kept (children come along with their root).", obj->name), 1);
		return false;
	}
	persistentIds_.insert(id);
	return true;
}

void EditorSuite::RequestSceneLoad(const std::string& nameOrPath)
{
	if (nameOrPath.empty()) return;
	pendingSceneLoad_ = nameOrPath; // the last request in a frame wins
}

bool EditorSuite::ProcessPendingSceneLoad()
{
	if (pendingSceneLoad_.empty()) return false;
	const std::string request = std::move(pendingSceneLoad_);
	pendingSceneLoad_.clear();

	const std::filesystem::path path = ResolveSceneRequest(request);
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		LogMessage(std::format("[Scene] LoadScene('{}'): file not found ({}).", request, path.string()), 2);
		return false;
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	file.close();

	const bool gameRunning = (sceneState_ != EditorSceneState::Edit) || IsStandaloneMode();
	if (!gameRunning) {
		return LoadScene(path.string());
	}

	// In the editor, Stop must bring back the scene that Play started from
	if (!IsStandaloneMode() && !runtimeSceneSwitched_) {
		SceneJson::Json settings = SceneJson::Json::object();
		WriteSceneSettingsJson(&settings);
		runtimeOriginSettings_ = SceneJson::DumpCompact(settings);
		runtimeOriginScenePath_ = currentScenePath_;
		runtimeOriginSceneName_ = currentSceneName_;
		runtimeOriginDirty_ = isSceneDirty_;
		runtimeSceneSwitched_ = true;
	}

	Audio::GetInstance()->StopAllAudio();
	pendingDeletes_.clear();

	// KeepAcrossScenes objects (with their children) step out before the scene is replaced; their scripts keep running
	std::vector<GameObject> kept;
	if (!persistentIds_.empty()) {
		std::unordered_set<uint32_t> keep;
		for (const auto& o : gameObjects_) {
			if (persistentIds_.count(o.id) != 0) keep.insert(o.id);
		}
		for (bool grew = true; grew;) {
			grew = false;
			for (const auto& o : gameObjects_) {
				if (o.parentId >= 0 && keep.count(static_cast<uint32_t>(o.parentId)) != 0 && keep.insert(o.id).second) grew = true;
			}
		}
		std::vector<GameObject> rest;
		rest.reserve(gameObjects_.size());
		for (auto& o : gameObjects_) (keep.count(o.id) != 0 ? kept : rest).push_back(std::move(o));
		gameObjects_ = std::move(rest);
	}

	// Calls OnDestroy() on the old scene's scripts, clears spawns/pools/UI
	if (!DeserializeSceneFromString(buffer.str())) {
		LogMessage(std::format("[Scene] LoadScene('{}'): invalid scene file.", request), 2);
		for (auto& o : kept) gameObjects_.push_back(std::move(o)); // the old scene stays; so do they
		RebuildHierarchyLinks();
		return false;
	}
	currentScenePath_ = path.string();
	currentSceneName_ = EditorSuiteDetail::SceneNameFromPath(path);
	timeScale_ = 1.0f;
	gameTime_ = 0.0;

	// Same order as StartPlayMode: every object exists before any Initialize() runs
	std::vector<uint32_t> ids;
	ids.reserve(gameObjects_.size());
	for (const auto& obj : gameObjects_) ids.push_back(obj.id);
	for (uint32_t id : ids) StartScriptsOnSpawnedObject(id, true);

	// Kept objects come back, with fresh ids where the new scene already uses theirs
	if (!kept.empty()) {
		std::unordered_set<uint32_t> used;
		for (const auto& o : gameObjects_) used.insert(o.id);
		std::unordered_map<uint32_t, uint32_t> remap;
		for (const auto& o : kept) {
			uint32_t id = o.id;
			while (used.count(id) != 0) id = nextObjectId_++;
			nextObjectId_ = (std::max)(nextObjectId_, id + 1);
			used.insert(id);
			remap[o.id] = id;
		}
		std::unordered_set<uint32_t> roots;
		for (auto& o : kept) {
			if (persistentIds_.count(o.id) != 0) roots.insert(remap[o.id]);
			o.id = remap[o.id];
			if (o.parentId >= 0) {
				auto it = remap.find(static_cast<uint32_t>(o.parentId));
				o.parentId = it != remap.end() ? static_cast<int>(it->second) : -1;
			}
			gameObjects_.push_back(std::move(o));
		}
		persistentIds_ = std::move(roots);
		RebuildHierarchyLinks();
		UpdateWorldMatrices();
	}
	RefreshAllScriptObjectViews();
	// Views of kept objects were dropped while they were out of the scene (and ids may have changed): rebind
	for (auto& o : gameObjects_) {
		for (auto& sc : o.scripts) {
			if (sc.instance) sc.instance->SetOwner(GetOrUpdateScriptViewById(o.id), o.id);
		}
	}

	LogMessage(std::format("[Scene] Switched to '{}' ({} objects).", currentSceneName_, gameObjects_.size()), 0);
	return true;
}

void EditorSuite::RestoreRuntimeSceneOrigin()
{
	if (!runtimeSceneSwitched_) return;
	runtimeSceneSwitched_ = false;
	currentScenePath_ = runtimeOriginScenePath_;
	currentSceneName_ = runtimeOriginSceneName_;
	isSceneDirty_ = runtimeOriginDirty_;
	if (std::optional<SceneJson::Json> settings = SceneJson::Parse(runtimeOriginSettings_)) {
		ReadSceneSettingsJson(&*settings);
	}
	runtimeOriginSettings_.clear();
}

// ---------------------------------------------------------------------------
// Save data: one JSON object of numbers and strings in <project>/SaveData/save.json
// (next to the executable in an exported game). Written on every change.
// ---------------------------------------------------------------------------

std::filesystem::path EditorSuite::GetSaveDataPath() const
{
	const std::filesystem::path root = currentProjectRoot_.empty() ? std::filesystem::path(".") : currentProjectRoot_;
	return root / "SaveData" / "save.json";
}

void EditorSuite::EnsureSaveDataLoaded()
{
	const std::string path = GetSaveDataPath().string();
	if (saveDataLoadedFrom_ == path) return;
	saveDataLoadedFrom_ = path;
	saveNumbers_.clear();
	saveStrings_.clear();

	std::ifstream in(path, std::ios::binary);
	if (!in) return;
	std::stringstream buffer;
	buffer << in.rdbuf();
	std::optional<SceneJson::Json> parsed = SceneJson::Parse(buffer.str());
	if (!parsed || !parsed->is_object()) {
		LogMessage(std::format("[SaveData] '{}' is not valid JSON; starting with empty save data.", path), 1);
		return;
	}
	for (auto it = parsed->begin(); it != parsed->end(); ++it) {
		if (it.value().is_number()) saveNumbers_[it.key()] = it.value().get<double>();
		else if (it.value().is_string()) saveStrings_[it.key()] = it.value().get<std::string>();
	}
}

void EditorSuite::WriteSaveData()
{
	SceneJson::Json j = SceneJson::Json::object();
	for (const auto& [key, value] : saveNumbers_) j[key] = value;
	for (const auto& [key, value] : saveStrings_) j[key] = value;

	const std::filesystem::path path = GetSaveDataPath();
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	// Write a temp file first so a crash mid-write never leaves a truncated save
	const std::filesystem::path tmp = path.string() + ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out) {
			LogMessage(std::format("[SaveData] Cannot write '{}'.", tmp.string()), 2);
			return;
		}
		out << j.dump(1, '\t');
	}
	std::filesystem::rename(tmp, path, ec);
	if (ec) {
		std::filesystem::remove(path, ec);
		std::filesystem::rename(tmp, path, ec);
	}
}

void EditorSuite::SetSaveNumber(const std::string& key, double value)
{
	if (key.empty() || !std::isfinite(value)) return;
	EnsureSaveDataLoaded();
	saveStrings_.erase(key);
	saveNumbers_[key] = value;
	WriteSaveData();
}

double EditorSuite::GetSaveNumber(const std::string& key, double fallback)
{
	EnsureSaveDataLoaded();
	auto it = saveNumbers_.find(key);
	return (it != saveNumbers_.end()) ? it->second : fallback;
}

void EditorSuite::SetSaveString(const std::string& key, const std::string& value)
{
	if (key.empty()) return;
	EnsureSaveDataLoaded();
	saveNumbers_.erase(key);
	saveStrings_[key] = value;
	WriteSaveData();
}

bool EditorSuite::GetSaveString(const std::string& key, std::string& out)
{
	EnsureSaveDataLoaded();
	auto it = saveStrings_.find(key);
	if (it == saveStrings_.end()) return false;
	out = it->second;
	return true;
}

bool EditorSuite::HasSaveKey(const std::string& key)
{
	EnsureSaveDataLoaded();
	return saveNumbers_.contains(key) || saveStrings_.contains(key);
}

void EditorSuite::DeleteAllSaveData()
{
	EnsureSaveDataLoaded();
	saveNumbers_.clear();
	saveStrings_.clear();
	WriteSaveData();
}
