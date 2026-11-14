#include "EditorSuiteInternal.h"
#include "SceneJson.h"
#include "MergeDriver.h"

// Pull while the editor is open: the open scene (or the prefab in Prefab Mode) remembers the text it was last read or
// written as. When a git pull replaces the file, a clean scene reloads, a dirty one is merged with the pulled version
// (SceneMerge, base = the remembered text), and Save merges first instead of overwriting.

namespace {
	using SceneJson::Json;

	std::string ReadWholeFile(const std::string& path, bool* opened = nullptr)
	{
		std::ifstream in(path, std::ios::binary);
		if (opened) *opened = in.is_open();
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}
}

void EditorSuite::RecordOpenFile(const std::string& path, const std::string* writtenText)
{
	openFileRecord_.path = path;
	openFileRecord_.text = writtenText ? *writtenText : ReadWholeFile(path);
	std::error_code ec;
	openFileRecord_.time = std::filesystem::last_write_time(path, ec);
	externalChangeWaitLogged_ = false;
}

void EditorSuite::PollOpenFileChanges(float deltaTime)
{
	if (isRunningTests_ && !externalWatchInTests_) return;
	externalWatchTimer_ += deltaTime;
	if (externalWatchTimer_ < 0.25f) return;
	externalWatchTimer_ = 0.0f;
	// git writes several files during a pull: nothing is read (or .meta written) until it is done
	if (AssetDatabase::GitIsBusy(currentProjectRoot_)) {
		gitWasBusy_ = true;
		return;
	}
	if (gitWasBusy_) {
		gitWasBusy_ = false;
		AssetDb().Refresh(); // .meta files for the assets the pull brought
	}
	if (projectMigrationsPending_) {
		RunProjectFileMigrations();
		EnsureProjectScriptsProps();
	}
	// The repository was created after the project was opened (Visual Studio "Create Git Repository")
	std::error_code gitEc;
	if (!mergeDriverChecked_ && !currentProjectRoot_.empty() && std::filesystem::exists(currentProjectRoot_ / ".git", gitEc)) {
		InstallMergeDriver();
	}
	PollGitHead();
	ApplyOpenFileChange();
	PollMergeReport();
	ApplyAssetFileChanges();
}

bool EditorSuite::ApplyOpenFileChange()
{
	if (IsStandaloneMode() || openFileRecord_.path.empty() || openFileRecord_.path != currentScenePath_) return false;
	std::error_code ec;
	const std::filesystem::file_time_type time = std::filesystem::last_write_time(openFileRecord_.path, ec);
	if (ec || time == openFileRecord_.time) return false; // a deleted file is left alone: Save writes it again
	if (sceneState_ != EditorSceneState::Edit) {
		// Play edits a copy of the scene: nothing is read or written until Stop, then the next poll applies it
		if (!externalChangeWaitLogged_) {
			LogMessage(std::format("[Scene] {} changed on disk; it is applied after Stop.", openFileRecord_.path), 0);
			externalChangeWaitLogged_ = true;
		}
		return false;
	}
	bool opened = false;
	const std::string disk = ReadWholeFile(openFileRecord_.path, &opened);
	if (!opened) return false;
	if (disk == openFileRecord_.text) {
		openFileRecord_.time = time; // touched, not changed
		return false;
	}
	const std::string path = openFileRecord_.path;

	if (prefabMode_.active) {
		if (isSceneDirty_) {
			// The stage is an instance, not the file: the merge happens when the prefab is saved (SavePrefab)
			openFileRecord_.time = time;
			LogMessage(std::format("[Prefab] {} changed on disk (a git pull?). Your unsaved edits stay; Save merges both.", path), 1);
			return false;
		}
		RecordOpenFile(path, &disk);
		ClearPrefabCache();
		ReloadPrefabModeInstance();
		LogMessage(std::format("[Prefab] {} changed on disk (a git pull?): reloaded.", path), 0);
		return true;
	}

	if (isSceneDirty_) {
		if (!MergeOpenSceneWith(disk)) {
			openFileRecord_.time = time; // said once; Save tries again and refuses to overwrite
			return false;
		}
		openFileRecord_.time = time;
		return true;
	}

	const std::vector<uint32_t> selected = SelectedIds();
	if (!LoadScene(path)) {
		openFileRecord_.time = time;
		LogMessage(std::format("[Scene] {} changed on disk but could not be read; the open scene stays.", path), 2);
		return false;
	}
	SelectIds(selected);
	LogMessage(std::format("[Scene] {} changed on disk (a git pull?): reloaded.", path), 0);
	return true;
}

bool EditorSuite::MergeOpenSceneWith(const std::string& theirs)
{
	if (sceneState_ != EditorSceneState::Edit) return false;
	DiscardMergePreviews(); // "mine" is what the report says; the reloaded report starts on mine again
	const SceneMerge::Result result = SceneMerge::Merge(openFileRecord_.text, SerializeSceneToString(), theirs);
	if (!result.ok) {
		LogMessage(std::format("[Scene] {} changed on disk but could not be merged with the open scene: {}", openFileRecord_.path, result.error), 2);
		return false;
	}
	const std::vector<uint32_t> selected = SelectedIds();
	const std::string path = currentScenePath_;
	const std::string name = currentSceneName_;
	PushUndoSnapshot(); // a pending edit is its own step
	if (!DeserializeSceneFromString(result.mergedText)) return false;
	currentScenePath_ = path;
	currentSceneName_ = name;
	SelectIds(selected);
	FinishMergeEdit(); // the merge is one undo step
	openFileRecord_.text = theirs; // the open scene now holds the pulled file plus the unsaved edits
	LogMessage(std::format("[Scene] {} changed on disk (a git pull?): merged it with your unsaved edits ({} conflict(s)). Save to keep both.",
		path, result.conflicts.size()), 1);
	AddMergeResult(result);
	return true;
}

void EditorSuite::AddMergeResult(const SceneMerge::Result& result)
{
	for (const SceneMerge::Note& note : result.notes) LogMessage(std::format("[Merge] {} ({}): {}", note.objectName, note.objectId, note.message), 1);
	if (!result.notes.empty()) showMergeConflictsWindow_ = true;
	if (result.conflicts.empty()) {
		mergeConflicts_.notes.insert(mergeConflicts_.notes.end(), result.notes.begin(), result.notes.end());
		return;
	}
	// Added to the report on disk, which may also hold undecided entries of a merge driver run
	const std::string reportPath = currentScenePath_ + MergeDriver::kReportSuffix;
	if (!MergeDriver::WriteReport(reportPath, result, {})) LogMessage(std::format("[Merge] Could not write the merge report {}.", reportPath), 2);
	LoadMergeReport(); // opens the window, and in Prefab Mode maps the file ids to the stage
}

void EditorSuite::UseRecordedPrefabIds(void* prefabJson) const
{
	Json& prefab = *static_cast<Json*>(prefabJson);
	const std::optional<Json> recorded = SceneJson::Parse(openFileRecord_.text);
	const Json* old = recorded ? SceneJson::Find(*recorded, "objects") : nullptr;
	Json* objects = prefab.contains("objects") ? &prefab["objects"] : nullptr;
	if (!old || !old->is_array() || !objects || !objects->is_array()) return;
	std::unordered_map<int, uint32_t> idByKey;
	for (const Json& o : *old) {
		uint32_t id = 0;
		int key = -1;
		if (o.is_object() && SceneJson::Read(o, "id", id) && SceneJson::Read(o, "prefabNodeKey", key)) idByKey[key] = id;
	}
	std::unordered_set<uint32_t> recordedIds;
	uint32_t nextId = 1;
	for (const auto& entry : idByKey) {
		recordedIds.insert(entry.second);
		nextId = (std::max)(nextId, entry.second + 1);
	}
	for (const Json& o : *objects) {
		uint32_t id = 0;
		if (SceneJson::Read(o, "id", id)) nextId = (std::max)(nextId, id + 1);
	}
	std::unordered_map<uint32_t, uint32_t> newId; // key + 1 -> file id
	for (const Json& o : *objects) {
		uint32_t id = 0;
		int key = -1;
		SceneJson::Read(o, "id", id);
		SceneJson::Read(o, "prefabNodeKey", key);
		if (const auto it = idByKey.find(key); it != idByKey.end()) newId[id] = it->second;
		else if (recordedIds.contains(id)) newId[id] = nextId++; // a new node never takes an id the file already uses
	}
	for (Json& o : *objects) {
		Json* joint = (o.contains("components") && o["components"].is_object() && o["components"].contains("Joint")) ? &o["components"]["Joint"] : nullptr;
		for (auto [holder, field] : { std::pair{ &o, "id" }, { &o, "parentId" }, { &o, "attachedParentId" }, { joint, "connectedObjectId" } }) {
			if (!holder || !holder->is_object()) continue;
			const auto at = holder->find(field);
			if (at == holder->end() || !at->is_number_integer()) continue;
			const int64_t value = at->get<int64_t>();
			if (value <= 0) continue;
			if (const auto it = newId.find(static_cast<uint32_t>(value)); it != newId.end()) (*holder)[field] = it->second;
		}
	}
}

void EditorSuite::ReloadPrefabModeInstance()
{
	std::vector<int> selectedKeys;
	for (uint32_t id : SelectedIds()) {
		if (const GameObject* o = FindGameObjectById(id, false)) selectedKeys.push_back(o->prefabNodeKey);
	}
	// The stage is the default camera + light and the instance (OpenPrefabMode)
	for (const GameObject& obj : gameObjects_) {
		if (obj.type != GameObjectType::Camera && obj.type != GameObjectType::DirectionalLight) QueueDeleteGameObjectById(obj.id);
	}
	ProcessPendingDeletes();
	prefabMode_.rootId = InstantiatePrefabNow(prefabMode_.prefab, { 0.0f, 0.0f, 0.0f });
	selectedObjectIndex_ = -1;
	extraSelection_.clear();
	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		const GameObject& o = gameObjects_[i];
		if (o.prefabSource != prefabMode_.prefab || std::find(selectedKeys.begin(), selectedKeys.end(), o.prefabNodeKey) == selectedKeys.end()) continue;
		if (selectedObjectIndex_ < 0) selectedObjectIndex_ = static_cast<int>(i);
		else extraSelection_.push_back(o.id);
	}
	if (selectedObjectIndex_ < 0) selectedObjectIndex_ = FindGameObjectIndexById(prefabMode_.rootId);
	ResetUndoHistory();
	isSceneDirty_ = false;
}

void EditorSuite::RecordAssetFile(OpenFileRecord& record, const std::filesystem::path& file, const std::string* writtenText)
{
	record.path = file.string();
	record.text = writtenText ? *writtenText : ReadWholeFile(record.path);
	std::error_code ec;
	record.time = std::filesystem::last_write_time(file, ec);
}

bool EditorSuite::AssetFileChangedOnDisk(const OpenFileRecord& record, const std::filesystem::path& file) const
{
	if (record.path.empty() || record.path != file.string()) return false; // never read from here (a new or moved file)
	bool opened = false;
	const std::string disk = ReadWholeFile(record.path, &opened);
	return opened && disk != record.text;
}

bool EditorSuite::KeepPulledVersion(const OpenFileRecord& record, const std::filesystem::path& file, const std::string& ours,
	const char* logTag, const std::string& assetPath, const std::function<void()>& reload)
{
	if (!AssetFileChangedOnDisk(record, file)) return false;
	// Library/ is ignored by git and the asset database, so the copy never travels or gets a .meta
	const std::filesystem::path dir = currentProjectRoot_ / "Library" / "Conflicts";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	std::filesystem::path copy = dir / file.filename();
	for (int n = 2; std::filesystem::exists(copy, ec); ++n) copy = dir / (std::to_string(n) + " " + file.filename().string());
	std::ofstream(copy, std::ios::binary) << ours;
	reload();
	LogMessage(std::format("[{}] {} changed on disk (a git pull?): loaded that version instead of overwriting it. Your edit is kept in {}",
		logTag, assetPath, copy.string()), 1);
	return true;
}

void EditorSuite::ReloadTimeline(const std::string& assetPath)
{
	const std::filesystem::path file = currentProjectRoot_ / std::filesystem::path(assetPath);
	const std::string text = ReadWholeFile(file.string());
	RecordAssetFile(timelineRecords_[assetPath], file, &text);
	Timeline loaded;
	if (!Timeline::FromJson(text, loaded)) {
		LogMessage(std::format("[Timeline] Could not read {}", assetPath), 2);
		return;
	}
	auto& cached = timelines_[assetPath];
	if (cached) *cached = loaded; // the same object: an open Timeline window keeps editing what is shown
	else cached = std::make_shared<Timeline>(loaded);
}

void EditorSuite::ApplyAssetFileChanges()
{
	// Both save every finished edit, so what the editor holds is what was last written: a changed file just reloads
	auto changed = [](OpenFileRecord& record) {
		std::error_code ec;
		const std::filesystem::file_time_type time = std::filesystem::last_write_time(record.path, ec);
		if (record.path.empty() || ec || time == record.time) return false;
		record.time = time;
		bool opened = false;
		return ReadWholeFile(record.path, &opened) != record.text && opened;
	};
	if (!mapTileSetPath_.empty() && changed(tileSetRecord_)) {
		UseMapchipTileSet(mapTileSetPath_);
		LogMessage(std::format("[Mapchip] {} changed on disk (a git pull?): reloaded.", mapTileSetPath_), 0);
	}
	for (auto& [assetPath, record] : timelineRecords_) {
		if (!timelines_.contains(assetPath) || !changed(record)) continue;
		ReloadTimeline(assetPath);
		LogMessage(std::format("[Timeline] {} changed on disk (a git pull?): reloaded.", assetPath), 0);
	}
}
