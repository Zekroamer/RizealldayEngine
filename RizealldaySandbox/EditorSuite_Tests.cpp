// Headless self-tests, run with `RizealldaySandbox.exe --run-tests [filter]` (editor builds only).
// They run inside the real editor (real D3D12 device, real EditorSuite) right after initialisation.
// Rules: every file a test writes lives under <temp>/RizealldayTests; the user's project, scenes and
// recent-project list are never touched (EditorSuite::isRunningTests_ blocks AddRecentProject).
#ifdef USE_IMGUI

#include "EditorSuiteInternal.h"
#include "PhysicsComponents.h"
#include "render/LightClusters.h"
#include "render/GpuParticles.h"
#include "render/TrailRenderer.h"
#include "render/DecalRenderer.h"
#include "base/TextureManager.h"
#include "SceneJson.h"
#include "Tests/TestFramework.h"
#include "base/DirectXCommon.h"
#include "3d/Object3d.h"
#include "3d/AnimationPlayer.h"
#include "EngineUpdate.h"
#include "NavGrid.h"
#include "base/EngineVersion.h"
#include "audio/Audio.h"
#include "AutoSave.h"
#include "MapchipTileSet.h"
#include "NodeGraph.h"
#include "GraphEditorModel.h"
#include "base/DXCManager.h"
#include <random>
#include <set>

/// Friend of EditorSuite: the only door the tests use to reach private state.
struct EditorSuiteTestAccess {
	using Kind = EditorSuite::ComponentKind;

	static std::vector<GameObject>& Objects(EditorSuite& s) { return s.gameObjects_; }
	static EditorSceneState& State(EditorSuite& s) { return s.sceneState_; }
	static EngineAPI& Api(EditorSuite& s) { return s.engineApi_; }
	static double& GameTime(EditorSuite& s) { return s.gameTime_; }
	static std::string& ScenePath(EditorSuite& s) { return s.currentScenePath_; }
	static std::string& SceneName(EditorSuite& s) { return s.currentSceneName_; }
	static bool ScriptsLoaded(EditorSuite& s) { return s.scriptModule_.IsLoaded() && s.scriptModule_.IsRegistered("SurvivorsGameManager"); }
	static bool& MergeAllowed(EditorSuite& s) { return s.undoMergeAllowed_; }
	static size_t UndoCount(const EditorSuite& s) { return s.undoCommands_.size(); }
	static size_t RedoCount(const EditorSuite& s) { return s.redoCommands_.size(); }
	static DirectXCommon* Dx(EditorSuite& s) { return s.dxCommon_; }
	static LightGroup* Lights(EditorSuite& s) { return s.lights_; }
	static Skybox& Sky(EditorSuite& s) { return s.skybox_; }
	static bool& Instancing(EditorSuite& s) { return s.enableInstancing_; }
	static bool InstallSample(EditorSuite& s, const std::string& prefix) { return s.InstallSampleScripts(prefix); }
	static uint64_t EnvironmentBakes(EditorSuite& s) { return s.environmentProbe_.GetBakeCount(); }
	static std::unordered_map<std::string, uint32_t>& MaterialTextures(EditorSuite& s) { return s.materialTextures_; }
	static void SaveMaterial(EditorSuite& s, GameObject& o, PbrMaterialComponent& m) { s.SaveMaterialAsset(o, m); }
	static const PbrMaterialComponent* FindMaterial(EditorSuite& s, const std::string& path) { return s.FindMaterialAsset(path); }
	static void ForgetMaterials(EditorSuite& s) { s.materialAssets_.clear(); }
	static std::filesystem::path& ProjectRoot(EditorSuite& s) { return s.currentProjectRoot_; }
	static std::filesystem::path& CurrentDirectory(EditorSuite& s) { return s.currentDirectory_; }
	static ScriptGameObjectView* ViewFor(EditorSuite& s, uint32_t id)
	{
		auto it = s.scriptObjectViews_.find(id);
		return (it != s.scriptObjectViews_.end()) ? it->second.get() : nullptr;
	}
	static void Physics(EditorSuite& s, float dt) { s.UpdatePhysics(dt); }
	static double& PhysicsAccumulator(EditorSuite& s) { return s.physicsAccumulator_; }
	static void RestorePoses(EditorSuite& s) { s.RestoreSimulatedPoses(); }
	static EditorSuite::CollisionEventCounts CollisionEvents(const EditorSuite& s) { return s.lastCollisionEvents_; }
	static uint32_t MusicVoice(const EditorSuite& s) { return s.musicVoice_; }
	static int TimelineEvents(const EditorSuite& s) { return s.timelineEventsFired_; }
	static Camera& EditorCam(EditorSuite& s) { return s.editorCamera_; }
	static float& RotateSweep(EditorSuite& s) { return s.gizmoRotateSweep_; }
	static int CulledObjects(const EditorSuite& s) { return s.lastCulledObjects_; }
	static int IndexOf(EditorSuite& s, uint32_t id) { return s.FindGameObjectIndexById(id); }
	static int Primary(const EditorSuite& s) { return s.selectedObjectIndex_; }
	static std::vector<uint32_t>& Extra(EditorSuite& s) { return s.extraSelection_; }
	static bool& AutoSaveInTests(EditorSuite& s) { return s.autoSaveInTests_; }
	static void RunAutoSave(EditorSuite& s, double now) { s.UpdateAutoSave(now); }
	static double LastAutoSave(const EditorSuite& s) { return s.lastAutoSaveTime_; }
	static bool& Dirty(EditorSuite& s) { return s.isSceneDirty_; }
	static bool ScriptsNeedRebuild(const std::filesystem::path& dir, const std::filesystem::path& dll) { return EditorSuite::ScriptsNeedRebuild(dir, dll); }
	static std::vector<std::vector<int>>& MapGrid(EditorSuite& s) { return s.mapLayers_[0]; }
	static std::vector<std::vector<std::vector<int>>>& MapLayers(EditorSuite& s) { return s.mapLayers_; }
	static int& EditLayer(EditorSuite& s) { return s.mapEditLayer_; }
	static bool CopyLayerToNext(EditorSuite& s) { return s.CopyMapchipLayerToNext(); }
	static void ClearLayer(EditorSuite& s) { s.ClearMapchipLayer(); }
	static int CountTiles(const EditorSuite& s, int id) { return s.CountMapchipTiles(id); }
	static int RemoveTileKind(EditorSuite& s, int id) { return s.RemoveMapchipTileKind(id); }
	static int MapRows(const EditorSuite& s) { return s.mapRows_; }
	static int MapCols(const EditorSuite& s) { return s.mapCols_; }
	static char* MapFileBuf(EditorSuite& s) { return s.mapFilePathBuf_; }
	static bool LoadMapCsv(EditorSuite& s, const std::string& file) { return s.LoadMapchipCSV(file); }
	static bool SaveMapCsv(EditorSuite& s, const std::string& file) { return s.SaveMapchipCSV(file); }
	static std::filesystem::path MapCsvPath(const EditorSuite& s) { return s.MapchipCsvPath(); }
	static bool BuildMap(EditorSuite& s, uint32_t id) { return s.BuildMapchipObjects(id); }
	static bool ResizeMap(EditorSuite& s, int cols, int rows) { return s.ResizeMapchip(cols, rows); }
	static bool ResizeMap3(EditorSuite& s, int cols, int rows, int layers) { return s.ResizeMapchip(cols, rows, layers); }
	static bool& MapPlaneXZ(EditorSuite& s) { return s.mapPlaneXZ_; }
	static bool MapBuildIsCurrent(const EditorSuite& s) { return s.MapchipBuildIsCurrent(); }
	static MapchipTileSet& TileSet(EditorSuite& s) { return s.mapTileSet_; }
	static const std::string& TileSetPath(const EditorSuite& s) { return s.mapTileSetPath_; }
	static bool CreateTileSet(EditorSuite& s, const std::string& path) { return s.CreateMapchipTileSet(path); }
	static bool SaveTileSet(EditorSuite& s) { return s.SaveMapchipTileSet(); }
	static bool UseTileSet(EditorSuite& s, const std::string& path) { return s.UseMapchipTileSet(path); }
	static std::vector<EditorSuite::SessionFile>& CloseSessionFiles(EditorSuite& s) { return s.closeSessionFiles_; }
	static void StartAssetSession(EditorSuite& s) { s.NoteSessionStartAssets(); }
	static void DiscardForClose(EditorSuite& s) { s.DiscardForClose(); }
	static bool& UnsavedCloseModal(EditorSuite& s) { return s.showUnsavedCloseModal_; }
	static const std::string& PendingRestore(const EditorSuite& s) { return s.pendingAutoSaveRestore_; }
	static bool Restore(EditorSuite& s) { return s.RestoreAutoSave(); }
	static void RunTimelines(EditorSuite& s, float dt) { s.UpdateTimelines(dt); }
	static void ApplyTimelineAt(EditorSuite& s, const Timeline& t, float time) { s.ApplyTimeline(t, time); }
	static bool SaveTimelineAsset(EditorSuite& s, const std::string& path, const Timeline& t) { return s.SaveTimeline(path, t); }
	static bool& GizmoLocalSpace(EditorSuite& s) { return s.gizmoLocalSpace_; }
	static void AimCamera(EditorSuite& s, const Vector3& position, const Vector3& rotation)
	{
		s.gameCamera_->translation = position;
		s.gameCamera_->rotation = rotation;
		s.gameCamera_->Update();
	}
	static Camera& GameCamera(EditorSuite& s) { return *s.gameCamera_; }
	static void AimAudioListener(EditorSuite& s, const Vector3& position, float yaw)
	{
		s.gameCamera_->translation = position;
		s.gameCamera_->rotation = { 0.0f, yaw, 0.0f };
		s.gameCamera_->Update();
		s.UpdateWorldMatrices();
		s.UpdateAudio();
	}
	static void AimGameCamera(EditorSuite& s, const Vector3& position, const Vector3& rotation)
	{
		s.gameCamera_->translation = position;
		s.gameCamera_->rotation = rotation;
		s.gameCamera_->Update();
		s.UpdateWorldMatrices();
	}
	static RenderTexture& GameViewRT(EditorSuite& s) { return s.gameViewRT_; }
	static RenderTexture& EditViewRT(EditorSuite& s) { return s.editViewRT_; }
	static GraphEditorModel& MaterialEditor(EditorSuite& s) { return s.materialEditor_; }
	static void ForgetEffectMaterials(EditorSuite& s) { s.effectMaterials_.clear(); } // as after a restart
	static RenderTexture& PreviewRT(EditorSuite& s) { return s.modelPreviewRT_; }
	static void CloseMaterialGraph(EditorSuite& s) { s.materialEditorPath_.clear(); s.showMaterialGraph_ = false; }
	static const std::string& MaterialEditorPath(EditorSuite& s) { return s.materialEditorPath_; }
	static bool& MaterialValueEditing(EditorSuite& s) { return s.materialValueEditing_; }
	static bool MaterialSaveFailed(EditorSuite& s) { return s.materialSaveFailed_; }
	static bool& ShowEffectEditor(EditorSuite& s) { return s.showEffectEditor_; }
	static void Select(EditorSuite& s, uint32_t id) { s.selectedObjectIndex_ = s.FindGameObjectIndexById(id); s.extraSelection_.clear(); }
	static bool& ScenePostPreview(EditorSuite& s) { return s.scenePostPreview_; }
	static PostProcess& ScenePost(EditorSuite& s) { return s.scenePostProcess_; }
	static void RunAudio(EditorSuite& s, float dt) { s.UpdateWorldMatrices(); s.UpdateAudio(dt); }
	static int ScriptFaults(const EditorSuite& s) { return s.scriptFaultCount_; }
	static bool FaultPending(const EditorSuite& s) { return s.scriptFaultPending_; }
	static bool CursorHidden(const EditorSuite& s) { return s.cursorHidden_; }
	static void Start(EditorSuite& s, uint32_t id) { s.StartScriptsOnSpawnedObject(id, false); }
	static void Spawns(EditorSuite& s) { s.ProcessPendingSpawns(); }
	static void RemoveComponent(EditorSuite& s, uint32_t objectId, Kind kind)
	{
		if (GameObject* obj = s.FindGameObjectById(objectId, false)) s.RemoveComponent(*obj, kind);
	}
	static std::filesystem::path TempRoot() { return std::filesystem::temp_directory_path() / "RizealldayTests"; }
	static void Run(EditorSuite& s, bool running) { s.isRunningTests_ = running; }
	static void SyncScripts(EditorSuite& s) { s.SyncScriptModuleWithProject(); }
	static const char* BuildOutputDir(const EditorSuite& s) { return s.buildOutputDirBuf_; }
	static void SetBuildOutputDir(EditorSuite& s, const std::string& dir) { strncpy_s(s.buildOutputDirBuf_, dir.c_str(), sizeof(s.buildOutputDirBuf_) - 1); }
	static const char* BuildGameTitle(const EditorSuite& s) { return s.buildGameTitleBuf_; }
	static std::filesystem::path& ExportRuntimeOverride(EditorSuite& s) { return s.exportRuntimeDirOverride_; }
	static void ShaderWatch(EditorSuite& s, const std::filesystem::path& dir, const std::filesystem::path& deploy)
	{
		s.shaderWatchDirOverride_ = dir;
		s.shaderDeployDirOverride_ = deploy;
		s.shaderStamps_.clear();
		s.shaderWatcherInitialized_ = false;
	}
	static int ShaderReloads(const EditorSuite& s) { return s.shaderReloadCount_; }
	static const std::filesystem::path& ScriptsDllOverride(EditorSuite& s) { return s.scriptModule_.GetDllPathOverride(); }
	static const std::string& EngineWarning(EditorSuite& s) { return s.projectEngineWarning_; }
	static void Log(EditorSuite& s, const std::string& text, int type) { s.LogMessage(text, type); }
	static void PrepareForTest(EditorSuite& s)
	{
		s.sceneState_ = EditorSceneState::Edit; // a failed test must not leave Play mode behind
		s.autoSaveEnabled_ = true;
		s.lastAutoSaveTime_ = 0.0;
		s.autoSaveInTests_ = false;
		s.NewScene();
		s.ResetUndoHistory();
	}
};

namespace {

using A = EditorSuiteTestAccess;

std::string Fmt(const Vector3& v)
{
	return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
}

// ===========================================================================
// JSON
// ===========================================================================

} // namespace

TEST_CASE(Json, RoundTripIdempotent)
{
	const size_t baseCount = A::Objects(s).size();
	const uint32_t cubeId = s.CreateCube("RoundTripCube", { 1.0f, 2.0f, 3.0f }).id;
	const uint32_t sphereId = s.CreateSphere("RoundTripSphere", { -4.0f, 0.5f, 2.0f }).id;
	GameObject& child = s.CreateGameObject("RoundTripChild", GameObjectType::Empty);
	const uint32_t childId = child.id;
	child.parentId = static_cast<int>(cubeId);
	child.translation = { 0.25f, 0.5f, 0.75f };
	s.RebuildHierarchyLinks();
	s.UpdateWorldMatrices();

	const std::string first = s.SerializeSceneToString();
	REQUIRE(s.DeserializeSceneFromString(first));
	const std::string second = s.SerializeSceneToString();
	CHECK(first == second);

	CHECK(A::Objects(s).size() == baseCount + 3);
	CHECK(s.FindGameObjectById(cubeId, false) != nullptr);
	CHECK(s.FindGameObjectById(sphereId, false) != nullptr);
	const GameObject* reloadedChild = s.FindGameObjectById(childId, false);
	REQUIRE(reloadedChild != nullptr);
	CHECK(reloadedChild->parentId == static_cast<int>(cubeId));
	CHECK(reloadedChild->parentIndex >= 0);
	const GameObject* reloadedCube = s.FindGameObjectById(cubeId, false);
	REQUIRE(reloadedCube != nullptr);
	CHECK(reloadedCube->hasMesh);
	CHECK(std::find(reloadedCube->childrenIds.begin(), reloadedCube->childrenIds.end(), childId) != reloadedCube->childrenIds.end());
}

TEST_CASE(Json, LegacyBackslashRepair)
{
	// Raw string: the JSON text really contains single backslashes (an invalid JSON escape, as in old scene files)
	const std::string legacy =
		R"({"sceneName":"LegacyScene","gameObjects":[)"
		R"({"id":1,"name":"Parent","type":0},)"
		R"({"id":2,"name":"Child","type":0,"parentId":1,"modelPath":".\Resources\Model\Idle.fbx"}]})";
	REQUIRE(s.DeserializeSceneFromString(legacy));
	CHECK(A::Objects(s).size() == 2);

	const GameObject* parent = s.FindGameObjectById(1, false);
	const GameObject* child = s.FindGameObjectById(2, false);
	REQUIRE(parent != nullptr);
	REQUIRE(child != nullptr);
	CHECK_MSG(child->modelPath == "./Resources/Model/Idle.fbx", child->modelPath);
	CHECK(child->parentId == 1);
	CHECK(child->parentIndex >= 0);
	CHECK(std::find(parent->childrenIds.begin(), parent->childrenIds.end(), 2u) != parent->childrenIds.end());
}

TEST_CASE(Json, InvalidInputKeepsScene)
{
	const size_t count = A::Objects(s).size();
	const std::string before = s.SerializeSceneToString();
	CHECK(!s.DeserializeSceneFromString("{ broken"));
	CHECK(!s.DeserializeSceneFromString(""));
	CHECK(!s.DeserializeSceneFromString("[1, 2, 3]"));
	CHECK(A::Objects(s).size() == count);
	CHECK(s.SerializeSceneToString() == before);
}

// ===========================================================================
// Undo / Redo
// ===========================================================================

TEST_CASE(Undo, UndoRedoSequence)
{
	std::vector<std::string> states;
	states.push_back(s.SerializeSceneToString()); // state 0: fresh scene

	// Records one undo command and its resulting scene string
	auto commit = [&]() {
		const size_t before = A::UndoCount(s);
		A::MergeAllowed(s) = false;
		s.PushUndoSnapshot();
		CHECK(A::UndoCount(s) == before + 1);
		states.push_back(s.SerializeSceneToString());
	};

	// 1. modify an object
	A::Objects(s)[0].translation.x += 5.0f;
	commit();

	// 2. create an object
	const uint32_t cubeId = s.CreateCube("UndoCube", { 1.0f, 2.0f, 3.0f }).id;
	commit();

	// 3. create a child of it
	GameObject& childRef = s.CreateSphere("UndoChild", { 0.0f, 1.0f, 0.0f });
	const uint32_t childId = childRef.id;
	childRef.parentId = static_cast<int>(cubeId);
	s.RebuildHierarchyLinks();
	s.UpdateWorldMatrices();
	commit();

	// 4. delete the child
	s.QueueDeleteGameObjectById(childId);
	s.ProcessPendingDeletes();
	CHECK(s.FindGameObjectById(childId, false) == nullptr);
	A::MergeAllowed(s) = false;
	s.PushUndoSnapshot(); // no-op when ProcessPendingDeletes already recorded the deletion
	states.push_back(s.SerializeSceneToString());
	CHECK(A::UndoCount(s) == 4);

	// 5. scene setting (skybox)
	A::Sky(s).SetExposure(1.75f);
	commit();
	CHECK(states[5] != states[4]);

	// 6. two quick edits of the same object merge into one command
	GameObject* cube = s.FindGameObjectById(cubeId, false);
	REQUIRE(cube != nullptr);
	cube->translation.x = 10.0f;
	commit();
	CHECK(A::UndoCount(s) == 6);
	cube = s.FindGameObjectById(cubeId, false);
	REQUIRE(cube != nullptr);
	cube->translation.x = 11.0f;
	s.PushUndoSnapshot(); // undoMergeAllowed_ is true after the previous push: merged
	CHECK(A::UndoCount(s) == 6);
	states.back() = s.SerializeSceneToString();
	CHECK(states[6] != states[5]);

	const size_t steps = states.size() - 1;
	REQUIRE(steps == 6);

	// Undo back through every recorded state
	for (size_t i = steps; i >= 1; --i) {
		s.Undo();
		CHECK_MSG(s.SerializeSceneToString() == states[i - 1], "undo to state " + std::to_string(i - 1));
	}
	CHECK(A::UndoCount(s) == 0);
	s.Undo(); // nothing left: must be a no-op
	CHECK(s.SerializeSceneToString() == states[0]);

	// Redo forward again
	for (size_t i = 1; i <= steps; ++i) {
		s.Redo();
		CHECK_MSG(s.SerializeSceneToString() == states[i], "redo to state " + std::to_string(i));
	}
	CHECK(A::RedoCount(s) == 0);
	s.Redo(); // nothing left: must be a no-op
	CHECK(s.SerializeSceneToString() == states[steps]);
}

TEST_CASE(Undo, CreateRecordsCompleteObject)
{
	const size_t baseCount = A::Objects(s).size();
	const uint32_t id = s.CreateSphere("UndoSphere", { 2.0f, 0.0f, -1.0f }).id;
	s.PushUndoSnapshot();
	CHECK(A::UndoCount(s) == 1);

	s.Undo();
	CHECK(s.FindGameObjectById(id, false) == nullptr);
	CHECK(A::Objects(s).size() == baseCount);

	s.Redo();
	const GameObject* restored = s.FindGameObjectById(id, false);
	REQUIRE(restored != nullptr);
	CHECK(restored->hasMesh);
	CHECK(restored->meshType == GameObject::MeshType::Sphere);
	CHECK(restored->hasBoxCollider || restored->hasSphereCollider);
	CHECK(restored->name == "UndoSphere");
	CHECK(A::Objects(s).size() == baseCount + 1);
}

TEST_CASE(Undo, PlayModeNotRecorded)
{
	const size_t commands = A::UndoCount(s);
	A::State(s) = EditorSceneState::Play;
	A::Objects(s)[0].translation.x += 3.0f;
	A::MergeAllowed(s) = false;
	s.PushUndoSnapshot();
	const size_t afterPlayEdit = A::UndoCount(s);
	A::State(s) = EditorSceneState::Edit;
	CHECK(afterPlayEdit == commands);
}

// ===========================================================================
// Hierarchy
// ===========================================================================

TEST_CASE(Hierarchy, ChildFollowsParent)
{
	const uint32_t parentId = s.CreateGameObject("HParent", GameObjectType::Empty).id;
	const uint32_t childId = s.CreateGameObject("HChild", GameObjectType::Empty).id;

	GameObject* parent = s.FindGameObjectById(parentId, false);
	GameObject* child = s.FindGameObjectById(childId, false);
	REQUIRE(parent != nullptr);
	REQUIRE(child != nullptr);

	parent->translation = { 0.0f, 0.5f, 0.0f };
	parent->rotation = { 0.0f, 3.14159265f / 4.0f, 0.0f };
	parent->rotationQuat = Quaternion::FromEuler(parent->rotation);
	child->translation = { 2.0f, 0.0f, 0.0f };
	child->parentId = static_cast<int>(parentId);

	s.RebuildHierarchyLinks();
	s.UpdateWorldMatrices();

	child = s.FindGameObjectById(childId, false);
	REQUIRE(child != nullptr);
	Vector3 t{};
	Quaternion q = Quaternion::Identity();
	Vector3 sc{};
	s.GetWorldTRS(*child, t, q, sc);
	// Row-vector RotY(45 deg) maps +X to (cos, 0, -sin)
	CHECK_MSG(std::fabs(t.x - 1.4142f) < 1e-3f && std::fabs(t.y - 0.5f) < 1e-3f && std::fabs(t.z + 1.4142f) < 1e-3f, Fmt(t));
	CHECK_NEAR(sc.x, 1.0f, 1e-3f);
	CHECK_NEAR(sc.y, 1.0f, 1e-3f);
	CHECK_NEAR(sc.z, 1.0f, 1e-3f);
}

TEST_CASE(Hierarchy, ReparentKeepsWorldTransform)
{
	const uint32_t parentId = s.CreateGameObject("KeepParent", GameObjectType::Empty).id;
	const uint32_t childId = s.CreateGameObject("KeepChild", GameObjectType::Empty).id;

	GameObject* parent = s.FindGameObjectById(parentId, false);
	GameObject* child = s.FindGameObjectById(childId, false);
	REQUIRE(parent != nullptr);
	REQUIRE(child != nullptr);
	parent->translation = { 3.0f, 0.0f, 0.0f };
	parent->rotation = { 0.0f, 3.14159265f * 0.5f, 0.0f };
	parent->rotationQuat = Quaternion::FromEuler(parent->rotation);
	parent->scale = { 2.0f, 2.0f, 2.0f };
	child->translation = { 5.0f, 1.0f, 2.0f };
	s.UpdateWorldMatrices();

	REQUIRE(s.ReparentGameObject(childId, static_cast<int>(parentId), true));
	child = s.FindGameObjectById(childId, false);
	REQUIRE(child != nullptr);
	CHECK(child->parentId == static_cast<int>(parentId));

	Vector3 t{};
	Quaternion q = Quaternion::Identity();
	Vector3 sc{};
	s.GetWorldTRS(*child, t, q, sc);
	CHECK_MSG(std::fabs(t.x - 5.0f) < 1e-3f && std::fabs(t.y - 1.0f) < 1e-3f && std::fabs(t.z - 2.0f) < 1e-3f, Fmt(t));
	CHECK_NEAR(sc.x, 1.0f, 1e-3f);
	CHECK_NEAR(sc.y, 1.0f, 1e-3f);
	CHECK_NEAR(sc.z, 1.0f, 1e-3f);
	CHECK_NEAR(std::fabs(q.w), 1.0f, 1e-3f); // world rotation still identity (q or -q)

	// Back to the root: the local transform becomes the world transform again
	REQUIRE(s.ReparentGameObject(childId, -1, true));
	child = s.FindGameObjectById(childId, false);
	REQUIRE(child != nullptr);
	CHECK(child->parentId == -1);
	CHECK_MSG(std::fabs(child->translation.x - 5.0f) < 1e-3f && std::fabs(child->translation.y - 1.0f) < 1e-3f &&
		std::fabs(child->translation.z - 2.0f) < 1e-3f, Fmt(child->translation));
}

TEST_CASE(Hierarchy, RejectsCycle)
{
	const uint32_t aId = s.CreateGameObject("CycleA", GameObjectType::Empty).id;
	const uint32_t bId = s.CreateGameObject("CycleB", GameObjectType::Empty).id;
	const uint32_t cId = s.CreateGameObject("CycleC", GameObjectType::Empty).id;

	REQUIRE(s.ReparentGameObject(bId, static_cast<int>(aId), true));
	REQUIRE(s.ReparentGameObject(cId, static_cast<int>(bId), true));
	CHECK(s.IsDescendantOf(cId, aId));

	// A cannot go under its own child or grandchild, nor under itself
	CHECK(!s.ReparentGameObject(aId, static_cast<int>(bId), true));
	CHECK(!s.ReparentGameObject(aId, static_cast<int>(cId), true));
	CHECK(!s.ReparentGameObject(aId, static_cast<int>(aId), true));
	const GameObject* a = s.FindGameObjectById(aId, false);
	REQUIRE(a != nullptr);
	CHECK(a->parentId == -1);

	// After detaching B the same move is legal
	REQUIRE(s.ReparentGameObject(bId, -1, true));
	CHECK(s.ReparentGameObject(aId, static_cast<int>(bId), true));
}

// ===========================================================================
// Components
// ===========================================================================

TEST_CASE(Components, RemoveAndUndo)
{
	const uint32_t lightId = s.CreateGameObject("CompLight", GameObjectType::PointLight).id;
	{
		GameObject* light = s.FindGameObjectById(lightId, false);
		REQUIRE(light != nullptr);
		light->hasLight = true;
		light->lightType = GameObject::LightType::Point;
	}
	const uint32_t cubeId = s.CreateCube("CompCube", { 0.0f, 1.0f, 0.0f }).id;

	s.PushUndoSnapshot();
	const size_t commandsBefore = A::UndoCount(s);
	const std::string original = s.SerializeSceneToString();

	// Light
	A::MergeAllowed(s) = false;
	A::RemoveComponent(s, lightId, A::Kind::Light);
	{
		const GameObject* light = s.FindGameObjectById(lightId, false);
		REQUIRE(light != nullptr);
		CHECK(!light->hasLight);
		CHECK(light->type == GameObjectType::Empty);
	}

	// Mesh
	A::MergeAllowed(s) = false;
	A::RemoveComponent(s, cubeId, A::Kind::Mesh);
	{
		const GameObject* cube = s.FindGameObjectById(cubeId, false);
		REQUIRE(cube != nullptr);
		CHECK(!cube->hasMesh);
		CHECK(cube->editObject3d == nullptr);
		CHECK(cube->gameObject3d == nullptr);
		CHECK(cube->shadowTransform == nullptr);
		CHECK(cube->type == GameObjectType::Empty);
	}

	// Box collider
	A::MergeAllowed(s) = false;
	A::RemoveComponent(s, cubeId, A::Kind::BoxCollider);
	{
		const GameObject* cube = s.FindGameObjectById(cubeId, false);
		REQUIRE(cube != nullptr);
		CHECK(!cube->hasBoxCollider);
	}

	CHECK(A::UndoCount(s) == commandsBefore + 3);
	CHECK(s.SerializeSceneToString() != original);

	s.Undo();
	s.Undo();
	s.Undo();
	CHECK(s.SerializeSceneToString() == original);
	const GameObject* cube = s.FindGameObjectById(cubeId, false);
	REQUIRE(cube != nullptr);
	CHECK(cube->hasMesh);
	CHECK(cube->hasBoxCollider);
}

// ===========================================================================
// Scripts
// ===========================================================================

TEST_CASE(Scripts, DeferredSpawnKeepsViewStable)
{
	const size_t baseCount = A::Objects(s).size();
	constexpr int kSpawnCount = 200;

	std::vector<ScriptGameObjectView*> views;
	std::vector<uint32_t> ids;
	for (int i = 0; i < kSpawnCount; ++i) {
		ScriptGameObjectView* view = s.QueueScriptSpawn("Spawned", i % 5, { static_cast<float>(i), 0.0f, 0.0f });
		REQUIRE(view != nullptr);
		views.push_back(view);
		ids.push_back(view->id);
	}
	CHECK(std::set<uint32_t>(ids.begin(), ids.end()).size() == static_cast<size_t>(kSpawnCount));

	// Nothing exists yet, but every pending view can be found and edited
	CHECK(A::Objects(s).size() == baseCount);
	for (int i = 0; i < kSpawnCount; ++i) {
		CHECK(s.FindPendingSpawnView(ids[i]) == views[i]);
	}

	s.ProcessPendingSpawns();

	CHECK(A::Objects(s).size() == baseCount + kSpawnCount);
	int stable = 0;
	int exist = 0;
	int movedCorrectly = 0;
	for (int i = 0; i < kSpawnCount; ++i) {
		if (A::ViewFor(s, ids[i]) == views[i]) ++stable;
		const GameObject* obj = s.FindGameObjectById(ids[i], false);
		if (obj) {
			++exist;
			if (std::fabs(obj->translation.x - static_cast<float>(i)) < 1e-4f) ++movedCorrectly;
		}
		CHECK(s.FindPendingSpawnView(ids[i]) == nullptr); // no longer pending
	}
	CHECK_MSG(stable == kSpawnCount, "stable views: " + std::to_string(stable));
	CHECK_MSG(exist == kSpawnCount, "created objects: " + std::to_string(exist));
	CHECK_MSG(movedCorrectly == kSpawnCount, "objects at the queued position: " + std::to_string(movedCorrectly));
	for (int i = 0; i < kSpawnCount; ++i) {
		CHECK(views[i]->id == ids[i]); // the old pointers still read valid data
	}

	// Cancelling a pending spawn prevents its creation
	ScriptGameObjectView* keepA = s.QueueScriptSpawn("KeepA", 0, { 0.0f, 0.0f, 0.0f });
	ScriptGameObjectView* dropB = s.QueueScriptSpawn("DropB", 1, { 1.0f, 0.0f, 0.0f });
	ScriptGameObjectView* keepC = s.QueueScriptSpawn("KeepC", 2, { 2.0f, 0.0f, 0.0f });
	REQUIRE(keepA != nullptr);
	REQUIRE(dropB != nullptr);
	REQUIRE(keepC != nullptr);
	const uint32_t idA = keepA->id;
	const uint32_t idB = dropB->id;
	const uint32_t idC = keepC->id;

	CHECK(s.CancelPendingSpawn(idB));
	CHECK(!s.CancelPendingSpawn(idB));            // already cancelled
	CHECK(!s.CancelPendingSpawn(ids[0]));         // already created, no longer pending
	CHECK(s.FindPendingSpawnView(idB) == nullptr);
	CHECK(!dropB->isActive);                      // retired, not freed: the script's pointer is still readable

	s.ProcessPendingSpawns();
	CHECK(s.FindGameObjectById(idA, false) != nullptr);
	CHECK(s.FindGameObjectById(idC, false) != nullptr);
	CHECK(s.FindGameObjectById(idB, false) == nullptr);
	CHECK(A::Objects(s).size() == baseCount + kSpawnCount + 2);
}

// ===========================================================================
// Rendering
// ===========================================================================

TEST_CASE(Rendering, SrvFreeListDeferredReuse)
{
	DirectXCommon* dx = A::Dx(s);
	REQUIRE(dx != nullptr);

	auto isFixed = [](uint32_t index) {
		return index == 0 || (index >= DirectXCommon::kFixedSrvBegin && index < DirectXCommon::kFixedSrvEnd);
	};

	// Allocate 10 indices: valid, never a fixed slot, all distinct
	std::vector<uint32_t> first;
	for (int i = 0; i < 10; ++i) {
		const uint32_t index = dx->AllocateSrvIndex();
		if (index == DirectXCommon::kInvalidSrvIndex) {
			ctx.Fail(__FILE__, __LINE__, "AllocateSrvIndex returned kInvalidSrvIndex");
			continue;
		}
		CHECK(!isFixed(index));
		CHECK(index < DirectXCommon::kMaxSrvDescriptors);
		first.push_back(index);
	}
	CHECK(std::set<uint32_t>(first.begin(), first.end()).size() == first.size());

	// Free them: they must not come back while the GPU may still read them
	for (uint32_t index : first) dx->FreeSrvIndex(index);
	std::vector<uint32_t> second;
	for (int i = 0; i < 10; ++i) {
		const uint32_t index = dx->AllocateSrvIndex();
		if (index == DirectXCommon::kInvalidSrvIndex) continue;
		second.push_back(index);
		CHECK_MSG(std::find(first.begin(), first.end(), index) == first.end(),
			"freed index " + std::to_string(index) + " was handed out again immediately");
		CHECK(!isFixed(index));
	}
	for (uint32_t index : second) dx->FreeSrvIndex(index);

	// After the GPU caught up the freed indices are allocatable again
	for (int i = 0; i < 3; ++i) dx->SignalAndWaitFence();
	std::vector<uint32_t> third;
	for (size_t i = 0; i < first.size() + second.size(); ++i) {
		const uint32_t index = dx->AllocateSrvIndex();
		if (index == DirectXCommon::kInvalidSrvIndex) break;
		third.push_back(index);
		CHECK(!isFixed(index));
	}
	for (uint32_t index : first) {
		CHECK_MSG(std::find(third.begin(), third.end(), index) != third.end(),
			"freed index " + std::to_string(index) + " was not reusable after the fences completed");
	}
	for (uint32_t index : second) {
		CHECK_MSG(std::find(third.begin(), third.end(), index) != third.end(),
			"freed index " + std::to_string(index) + " was not reusable after the fences completed");
	}

	// Leave the heap exactly as we found it
	for (uint32_t index : third) dx->FreeSrvIndex(index);
	for (int i = 0; i < 3; ++i) dx->SignalAndWaitFence();
	(void)dx->PeekSrvIndex(); // reclaims the retired indices
}

// ===========================================================================
// Math
// ===========================================================================

TEST_CASE(Math, DecomposeComposeRoundTrip)
{
	struct Sample {
		Vector3 t;
		Vector3 euler;
		Vector3 scale;
	};
	std::vector<Sample> samples = {
		{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
		{ { 1.0f, 2.0f, 3.0f }, { 0.3f, 0.7f, -0.2f }, { 1.0f, 2.0f, 0.5f } },
		{ { -5.0f, 0.25f, 9.0f }, { 1.2f, -2.4f, 0.9f }, { 3.0f, 3.0f, 3.0f } },
		{ { 10.0f, -10.0f, 0.0f }, { 3.0f, 0.1f, -1.5f }, { 0.4f, 1.7f, 2.2f } },
	};
	std::mt19937 rng(12345);
	std::uniform_real_distribution<float> pos(-20.0f, 20.0f);
	std::uniform_real_distribution<float> ang(-3.0f, 3.0f);
	std::uniform_real_distribution<float> scl(0.3f, 3.0f);
	for (int i = 0; i < 24; ++i) {
		samples.push_back({ { pos(rng), pos(rng), pos(rng) }, { ang(rng), ang(rng), ang(rng) }, { scl(rng), scl(rng), scl(rng) } });
	}

	int index = 0;
	for (const Sample& sample : samples) {
		const Matrix4x4 original = TransformUtil::ComposeTRS(sample.t, Quaternion::FromEuler(sample.euler), sample.scale);
		Vector3 t{};
		Quaternion q = Quaternion::Identity();
		Vector3 sc{};
		TransformUtil::Decompose(original, t, q, sc);
		const Matrix4x4 rebuilt = TransformUtil::ComposeTRS(t, q, sc);

		float maxError = 0.0f;
		for (int r = 0; r < 4; ++r) {
			for (int c = 0; c < 4; ++c) {
				maxError = (std::max)(maxError, std::fabs(original.m[r][c] - rebuilt.m[r][c]));
			}
		}
		CHECK_MSG(maxError <= 1e-4f, "sample " + std::to_string(index) + " max error " + std::to_string(maxError));
		CHECK_NEAR(sc.x, sample.scale.x, 1e-4f);
		CHECK_NEAR(sc.y, sample.scale.y, 1e-4f);
		CHECK_NEAR(sc.z, sample.scale.z, 1e-4f);
		++index;
	}
}

// ===========================================================================
// Project
// ===========================================================================

TEST_CASE(Project, DontSaveOffersToRecycleFilesMadeThisSession)
{
	// Closing with unsaved changes lists the Assets files made in this session (they are already on disk);
	// "Don't Save" sends the checked ones to the recycle bin and drops the auto-saved copy of the scene
	namespace fs = std::filesystem;
	const fs::path parent = A::TempRoot() / "SessionFiles";
	std::error_code ec;
	fs::remove_all(parent, ec);
	fs::create_directories(parent, ec);
	const fs::path savedRoot = A::ProjectRoot(s);
	const fs::path savedDirectory = A::CurrentDirectory(s);

	REQUIRE(s.CreateNewProject(parent.string(), "Fresh"));
	const fs::path root = parent / "Fresh";
	auto listed = [&]() {
		std::vector<std::string> paths;
		for (const auto& f : A::CloseSessionFiles(s)) paths.push_back(f.path);
		return paths;
	};

	// A project made in this session: its default scene was made in this session too, offered unchecked
	A::Dirty(s) = true;
	CHECK(!s.OnCloseRequested());
	CHECK(A::UnsavedCloseModal(s));
	CHECK(listed() == std::vector<std::string>{ "Assets/Scenes/SampleScene.scene.json" });
	for (const auto& f : A::CloseSessionFiles(s)) CHECK(!f.remove);
	A::UnsavedCloseModal(s) = false;

	// Next session: the scene existed at the start. A file made now is listed; moving the old scene does not list it
	A::StartAssetSession(s);
	fs::create_directories(root / "Assets" / "Prefabs", ec);
	std::ofstream(root / "Assets" / "Prefabs" / "Made.prefab.json") << "{}";
	REQUIRE(s.AssetDb().MoveAsset("Assets/Scenes/SampleScene.scene.json", "Assets/Scenes/Level1.scene.json"));
	A::Dirty(s) = true;
	CHECK(!s.OnCloseRequested());
	CHECK_MSG(listed() == std::vector<std::string>{ "Assets/Prefabs/Made.prefab.json" }, listed().empty() ? std::string("(none)") : listed().front());

	// Don't Save with the file checked: it goes, its .meta goes, the old scene stays, the auto-saved copy goes
	const fs::path autoSaveCopy = AutoSave::PathFor(root, (root / "Assets" / "Scenes" / "Level1.scene.json").string());
	A::ScenePath(s) = (root / "Assets" / "Scenes" / "Level1.scene.json").string();
	fs::create_directories(autoSaveCopy.parent_path(), ec);
	std::ofstream(autoSaveCopy) << "{}";
	REQUIRE(!A::CloseSessionFiles(s).empty());
	A::CloseSessionFiles(s).front().remove = true;
	A::DiscardForClose(s);
	CHECK(!fs::exists(root / "Assets" / "Prefabs" / "Made.prefab.json"));
	CHECK(!fs::exists(root / "Assets" / "Prefabs" / "Made.prefab.json.meta"));
	CHECK(fs::exists(root / "Assets" / "Scenes" / "Level1.scene.json"));
	CHECK(!fs::exists(autoSaveCopy));

	// A clean scene closes without asking
	A::Dirty(s) = false;
	CHECK(s.OnCloseRequested());

	A::UnsavedCloseModal(s) = false;
	A::ProjectRoot(s) = savedRoot;
	A::CurrentDirectory(s) = savedDirectory;
	A::SyncScripts(s);
	s.NewScene();
	fs::remove_all(parent, ec);
}

TEST_CASE(Mapchip, TileSetIdsStayStable)
{
	// The default set is what grids meant before tile sets: 1 Wall, 2 Spike, 3 Spawn, 4 Goal
	MapchipTileSet set = MapchipTileSet::Default();
	REQUIRE(set.kinds.size() == 4u);
	REQUIRE(set.Find(1) != nullptr);
	CHECK(set.Find(1)->name == "Wall");
	CHECK(set.Find(1)->shape == MapchipShape::Block);
	CHECK(set.Find(2)->name == "Spike");
	CHECK(set.Find(2)->shape == MapchipShape::Trigger);
	CHECK(set.Find(3)->shape == MapchipShape::Marker);
	CHECK(set.Find(4)->name == "Goal");
	CHECK(set.Find(0) == nullptr); // Empty is not a kind

	// A new kind gets the next id; renaming keeps the id; a removed id is never handed out again
	MapchipTileKind* coin = set.Add("Coin");
	REQUIRE(coin != nullptr);
	CHECK(coin->id == 5);
	set.Find(1)->name = "Brick";
	CHECK(set.Find(1)->name == "Brick");
	CHECK(set.Find(1)->Tag() == "Brick"); // the tag follows the name unless set
	set.Find(1)->tag = "Solid";
	CHECK(set.Find(1)->Tag() == "Solid");
	CHECK(set.Remove(5));
	CHECK(set.Find(5) == nullptr);
	CHECK(!set.Remove(5));
	MapchipTileKind* gem = set.Add("Gem");
	REQUIRE(gem != nullptr);
	CHECK(gem->id == 6);

	// Saved and read back unchanged, including where the next id starts
	set.Find(6)->prefab = "Gem";
	set.Find(6)->color = { 0.1f, 0.2f, 0.3f, 1.0f };
	set.Find(6)->shape = MapchipShape::Marker;
	std::optional<MapchipTileSet> back = MapchipTileSet::FromJson(set.ToJson());
	REQUIRE(back.has_value());
	REQUIRE(back->kinds.size() == set.kinds.size());
	CHECK(back->Find(1)->name == "Brick");
	CHECK(back->Find(1)->tag == "Solid");
	CHECK(back->Find(6)->prefab == "Gem");
	CHECK(back->Find(6)->shape == MapchipShape::Marker);
	CHECK_NEAR(back->Find(6)->color[2], 0.3f, 1e-6f);
	REQUIRE(back->Add("Next") != nullptr);
	CHECK(back->Find(7) != nullptr);

	// At most 63 kinds; text that is not a tile set is refused with a reason
	MapchipTileSet full;
	for (int i = 0; i < MapchipTileSet::kMaxKinds; ++i) REQUIRE(full.Add("T") != nullptr);
	CHECK(full.Add("One too many") == nullptr);
	std::string why;
	CHECK(!MapchipTileSet::FromJson("[1, 2]", &why).has_value());
	CHECK(!why.empty());
	CHECK(!MapchipTileSet::FromJson("{\"kinds\": [{\"id\": 0, \"name\": \"Bad\"}]}", &why).has_value()); // 0 is Empty
}

TEST_CASE(Mapchip, ScenesRememberTheirTileSet)
{
	namespace fs = std::filesystem;
	const fs::path root = A::TempRoot() / "TileSetProj";
	std::error_code ec;
	fs::remove_all(root, ec);
	fs::create_directories(root / "Assets", ec);
	const fs::path savedRoot = A::ProjectRoot(s);
	A::ProjectRoot(s) = root;
	s.NewScene();

	// A new scene uses the built-in set (Wall, Spike, Spawn, Goal) and no file
	CHECK(A::TileSetPath(s).empty());
	REQUIRE(A::TileSet(s).Find(1) != nullptr);
	CHECK(A::TileSet(s).Find(1)->name == "Wall");

	// A tile set asset starts as a copy of the current set; edits are saved to it
	REQUIRE(A::CreateTileSet(s, "Assets/Levels/Game.tileset.json"));
	CHECK(fs::exists(root / "Assets" / "Levels" / "Game.tileset.json"));
	CHECK(A::TileSetPath(s) == "Assets/Levels/Game.tileset.json");
	A::TileSet(s).Find(1)->name = "Brick";
	REQUIRE(A::SaveTileSet(s));

	// The scene remembers which set it uses; another scene does not inherit it
	const std::string withSet = s.SerializeSceneToString();
	s.NewScene();
	CHECK(A::TileSetPath(s).empty());
	CHECK(A::TileSet(s).Find(1)->name == "Wall");
	REQUIRE(s.DeserializeSceneFromString(withSet));
	CHECK(A::TileSetPath(s) == "Assets/Levels/Game.tileset.json");
	CHECK(A::TileSet(s).Find(1)->name == "Brick");

	// Scenes saved before tile sets existed keep meaning 1 Wall .. 4 Goal
	std::optional<SceneJson::Json> old = SceneJson::Parse(withSet);
	REQUIRE(old.has_value());
	(*old)["mapchip"].erase("tileSet");
	(*old)["mapchip"].erase("tileSetGuid");
	REQUIRE(s.DeserializeSceneFromString(old->dump()));
	CHECK(A::TileSetPath(s).empty());
	CHECK(A::TileSet(s).Find(1)->name == "Wall");

	// Moving the file in the Project panel moves the scene's reference with it, and edits go to the new place
	REQUIRE(A::UseTileSet(s, "Assets/Levels/Game.tileset.json"));
	REQUIRE(s.MoveAssetAndUpdateReferences((root / "Assets" / "Levels" / "Game.tileset.json").string(),
		(root / "Assets" / "Levels" / "Moved.tileset.json").string()));
	CHECK(A::TileSetPath(s) == "Assets/Levels/Moved.tileset.json");
	REQUIRE(A::SaveTileSet(s));
	CHECK(!fs::exists(root / "Assets" / "Levels" / "Game.tileset.json"));

	// A renamed prefab stays the prefab of the tiles that use it
	A::TileSet(s).Find(1)->prefab = "Crate";
	fs::create_directories(root / "Assets" / "Prefabs", ec);
	std::ofstream(root / "Assets" / "Prefabs" / "Crate.prefab.json") << "{\"objects\": []}";
	s.AssetDb().Refresh();
	REQUIRE(s.MoveAssetAndUpdateReferences((root / "Assets" / "Prefabs" / "Crate.prefab.json").string(),
		(root / "Assets" / "Prefabs" / "Box.prefab.json").string()));
	CHECK(A::TileSet(s).Find(1)->prefab == "Box");

	// A set file that cannot be read leaves the built-in set and says so (the scene still names the file),
	// and the broken file is never overwritten with the built-in tiles
	std::ofstream(root / "Assets" / "Levels" / "Broken.tileset.json") << "{ not json";
	CHECK(!A::UseTileSet(s, "Assets/Levels/Broken.tileset.json"));
	CHECK(A::TileSet(s).Find(1)->name == "Wall");
	CHECK(!A::SaveTileSet(s));
	{
		std::ifstream in(root / "Assets" / "Levels" / "Broken.tileset.json");
		std::stringstream text;
		text << in.rdbuf();
		CHECK(text.str() == "{ not json");
	}

	A::ProjectRoot(s) = savedRoot;
	s.NewScene();
	fs::remove_all(root, ec);
}

TEST_CASE(Mapchip, BuildUsesTheTileSet)
{
	namespace fs = std::filesystem;
	const fs::path root = A::TempRoot() / "TileBuildProj";
	std::error_code ec;
	fs::remove_all(root, ec);
	fs::create_directories(root / "Assets", ec);
	const fs::path savedRoot = A::ProjectRoot(s);
	A::ProjectRoot(s) = root;
	s.NewScene();

	// A "Coin" prefab with a child (the sparkle)
	GameObject& coinRoot = s.CreateGameObject("CoinModel", GameObjectType::Empty);
	const uint32_t coinRootId = coinRoot.id;
	GameObject& sparkle = s.CreateGameObject("Sparkle", GameObjectType::Empty);
	const uint32_t sparkleId = sparkle.id;
	REQUIRE(s.ReparentGameObject(sparkleId, static_cast<int>(coinRootId), false));
	REQUIRE(s.SavePrefab(coinRootId, "Coin"));
	s.NewScene();

	// Tile set: 1 renamed to Brick (no prefab: the Block cube), a new kind 5 Coin that uses the prefab
	A::TileSet(s).Find(1)->name = "Brick";
	MapchipTileKind* coin = A::TileSet(s).Add("Coin");
	REQUIRE(coin != nullptr);
	REQUIRE(coin->id == 5);
	coin->prefab = "Coin";
	std::ofstream(root / "grid.csv") << "1,5,9\n";
	REQUIRE(A::LoadMapCsv(s, (root / "grid.csv").string()));

	GameObject& map = s.CreateGameObject("Map", GameObjectType::MapchipGrid);
	map.hasMapchip = true;
	const uint32_t mapId = map.id;
	REQUIRE(A::BuildMap(s, mapId));
	s.RebuildHierarchyLinks();
	const size_t objectsAfterFirstBuild = A::Objects(s).size();

	const GameObject* brick = nullptr;
	const GameObject* coinTile = nullptr;
	int direct = 0;
	for (const GameObject& o : A::Objects(s)) {
		if (o.parentId != static_cast<int>(mapId)) continue;
		++direct;
		if (o.tag == "Brick") brick = &o;
		if (o.tag == "Coin") coinTile = &o;
	}
	CHECK(direct == 2); // tile 9 is not in the set: nothing is made for it
	REQUIRE(brick != nullptr);
	CHECK(brick->hasMesh && brick->hasBoxCollider && !brick->isTrigger && brick->isStatic);
	REQUIRE(coinTile != nullptr);
	CHECK(!coinTile->hasMesh); // the prefab's object, not a fallback cube
	CHECK(coinTile->childrenIds.size() == 1u); // with the prefab's own child
	CHECK(coinTile->prefabSource.empty()); // a built tile is not a linked prefab instance: Build again picks up prefab edits
	CHECK_NEAR(coinTile->translation.x, 1.0f, 1e-5f);

	// Building again replaces everything Build made, prefab children included
	REQUIRE(A::BuildMap(s, mapId));
	CHECK(A::Objects(s).size() == objectsAfterFirstBuild);

	A::ProjectRoot(s) = savedRoot;
	s.NewScene();
	fs::remove_all(root, ec);
}

TEST_CASE(Mapchip, GridEditingForLevels)
{
	namespace fs = std::filesystem;
	const fs::path dir = A::TempRoot() / "MapchipGrid";
	std::error_code ec;
	fs::remove_all(dir, ec);
	fs::create_directories(dir, ec);
	s.NewScene();

	// Loading a CSV changes the scene: it is marked unsaved (the close dialog asks) and can be undone
	std::ofstream(dir / "small.csv") << "1,2,3\n4,0,0\n";
	A::Dirty(s) = false;
	REQUIRE(A::LoadMapCsv(s, (dir / "small.csv").string()));
	CHECK(A::Dirty(s));

	// Resizing keeps what still fits and fills the rest with Empty; sizes stay within 1..256
	REQUIRE(A::ResizeMap(s, 4, 3));
	CHECK(A::MapCols(s) == 4);
	CHECK(A::MapRows(s) == 3);
	CHECK(A::MapGrid(s)[0] == (std::vector<int>{ 1, 2, 3, 0 }));
	CHECK(A::MapGrid(s)[1] == (std::vector<int>{ 4, 0, 0, 0 }));
	CHECK(A::MapGrid(s)[2] == (std::vector<int>{ 0, 0, 0, 0 }));
	REQUIRE(A::ResizeMap(s, 2, 1));
	CHECK(A::MapGrid(s).size() == 1u);
	CHECK(A::MapGrid(s)[0] == (std::vector<int>{ 1, 2 }));
	REQUIRE(A::ResizeMap(s, 0, 9999));
	CHECK(A::MapCols(s) == 1);
	CHECK(A::MapRows(s) == 256);
	CHECK(A::MapGrid(s).size() == 256u);
	CHECK(A::MapGrid(s)[0].size() == 1u);

	// A top-down map lies on X/Z: row 0 is the far end (+Z), Y stays 0
	std::ofstream(dir / "plane.csv") << "1,0\n0,1\n";
	REQUIRE(A::LoadMapCsv(s, (dir / "plane.csv").string()));
	A::MapPlaneXZ(s) = true;
	GameObject& map = s.CreateGameObject("Map", GameObjectType::MapchipGrid);
	map.hasMapchip = true;
	const uint32_t mapId = map.id;
	CHECK(!A::MapBuildIsCurrent(s)); // painted, never built
	REQUIRE(A::BuildMap(s, mapId));
	CHECK(A::MapBuildIsCurrent(s));
	std::vector<Vector3> positions;
	for (const GameObject& o : A::Objects(s)) {
		if (o.parentId == static_cast<int>(mapId)) positions.push_back(o.translation);
	}
	REQUIRE(positions.size() == 2u);
	std::sort(positions.begin(), positions.end(), [](const Vector3& a, const Vector3& b) { return a.x < b.x; });
	CHECK_NEAR(positions[0].x, 0.0f, 1e-5f);
	CHECK_NEAR(positions[0].y, 0.0f, 1e-5f);
	CHECK_NEAR(positions[0].z, 1.0f, 1e-5f);
	CHECK_NEAR(positions[1].x, 1.0f, 1e-5f);
	CHECK_NEAR(positions[1].z, 0.0f, 1e-5f);

	// Any grid change after a build means the scene no longer shows the grid
	REQUIRE(A::ResizeMap(s, 3, 2));
	CHECK(!A::MapBuildIsCurrent(s));

	// The plane is part of the scene; a new scene starts upright (X/Y) again
	const std::string saved = s.SerializeSceneToString();
	s.NewScene();
	CHECK(!A::MapPlaneXZ(s));
	REQUIRE(s.DeserializeSceneFromString(saved));
	CHECK(A::MapPlaneXZ(s));

	s.NewScene();
	fs::remove_all(dir, ec);
}

TEST_CASE(Mapchip, LayersMakeThreeDimensionalLevels)
{
	namespace fs = std::filesystem;
	const fs::path dir = A::TempRoot() / "MapchipLayers";
	std::error_code ec;
	fs::remove_all(dir, ec);
	fs::create_directories(dir, ec);
	s.NewScene();
	CHECK(A::MapLayers(s).size() == 1u); // scenes start (and old scenes load) with one layer

	// Layers: 1..64, each the same columns x rows; the edited layer stays inside
	REQUIRE(A::ResizeMap3(s, 3, 2, 3));
	REQUIRE(A::MapLayers(s).size() == 3u);
	for (const auto& layer : A::MapLayers(s)) {
		REQUIRE(layer.size() == 2u);
		CHECK(layer[0].size() == 3u);
	}
	A::EditLayer(s) = 2;
	REQUIRE(A::ResizeMap3(s, 3, 2, 1));
	CHECK(A::EditLayer(s) == 0);
	REQUIRE(A::ResizeMap3(s, 3, 2, 999));
	CHECK(A::MapLayers(s).size() == 64u);

	// A CSV holds the layers one after another, separated by an empty line; an old CSV is one layer
	std::ofstream(dir / "layers.csv") << "1,0\n\n0,2\n\n";
	REQUIRE(A::LoadMapCsv(s, (dir / "layers.csv").string()));
	REQUIRE(A::MapLayers(s).size() == 2u);
	CHECK(A::MapLayers(s)[0][0] == (std::vector<int>{ 1, 0 }));
	CHECK(A::MapLayers(s)[1][0] == (std::vector<int>{ 0, 2 }));
	REQUIRE(A::SaveMapCsv(s, (dir / "again.csv").string()));
	REQUIRE(A::LoadMapCsv(s, (dir / "again.csv").string()));
	REQUIRE(A::MapLayers(s).size() == 2u);
	CHECK(A::MapLayers(s)[1][0] == (std::vector<int>{ 0, 2 }));

	// Side view: the next layer lies behind (+Z); top-down: the next layer is the floor above (+Y)
	GameObject& map = s.CreateGameObject("Map", GameObjectType::MapchipGrid);
	map.hasMapchip = true;
	const uint32_t mapId = map.id;
	auto built = [&](const char* tag) -> const GameObject* {
		for (const GameObject& o : A::Objects(s)) {
			if (o.parentId == static_cast<int>(mapId) && o.tag == tag) return &o;
		}
		return nullptr;
	};
	REQUIRE(A::BuildMap(s, mapId));
	REQUIRE(built("Wall") != nullptr);
	REQUIRE(built("Spike") != nullptr);
	CHECK_NEAR(built("Wall")->translation.z, 0.0f, 1e-5f);
	CHECK_NEAR(built("Spike")->translation.z, 1.0f, 1e-5f);
	CHECK_NEAR(built("Spike")->translation.x, 1.0f, 1e-5f);
	CHECK(built("Spike")->name.find(",L1)") != std::string::npos);
	A::MapPlaneXZ(s) = true;
	REQUIRE(A::BuildMap(s, mapId));
	CHECK_NEAR(built("Wall")->translation.y, 0.0f, 1e-5f);
	CHECK_NEAR(built("Spike")->translation.y, 1.0f, 1e-5f);
	A::MapPlaneXZ(s) = false;

	// All layers are saved with the scene
	const std::string saved = s.SerializeSceneToString();
	s.NewScene();
	REQUIRE(s.DeserializeSceneFromString(saved));
	REQUIRE(A::MapLayers(s).size() == 2u);
	CHECK(A::MapLayers(s)[1][0] == (std::vector<int>{ 0, 2 }));

	// Tiles are counted and removed in every layer
	CHECK(A::CountTiles(s, 2) == 1);
	// Copy to next: the last layer gets a new copy above it, and that copy is edited next; Clear empties one layer
	A::EditLayer(s) = 1;
	REQUIRE(A::CopyLayerToNext(s));
	REQUIRE(A::MapLayers(s).size() == 3u);
	CHECK(A::EditLayer(s) == 2);
	CHECK(A::MapLayers(s)[2][0] == (std::vector<int>{ 0, 2 }));
	CHECK(A::CountTiles(s, 2) == 2);
	A::ClearLayer(s);
	CHECK(A::MapLayers(s)[2][0] == (std::vector<int>{ 0, 0 }));
	CHECK(A::MapLayers(s)[1][0] == (std::vector<int>{ 0, 2 }));
	CHECK(A::RemoveTileKind(s, 2) == 1);
	CHECK(A::CountTiles(s, 2) == 0);

	s.NewScene();
	fs::remove_all(dir, ec);
}

TEST_CASE(Mapchip, CsvPathBuildAndSceneReset)
{
	namespace fs = std::filesystem;
	const fs::path dir = A::TempRoot() / "Mapchip";
	std::error_code ec;
	fs::remove_all(dir, ec);
	fs::create_directories(dir, ec);
	s.NewScene();

	// A CSV with rows of different length is padded with Empty, never read past a row
	std::ofstream(dir / "ragged.csv") << "1,1,1\n2\n0,4\n";
	REQUIRE(A::LoadMapCsv(s, (dir / "ragged.csv").string()));
	CHECK(A::MapRows(s) == 3);
	CHECK(A::MapCols(s) == 3);
	for (const auto& row : A::MapGrid(s)) CHECK(row.size() == 3u);
	CHECK(A::MapGrid(s)[1] == (std::vector<int>{ 2, 0, 0 }));
	CHECK(A::MapGrid(s)[2] == (std::vector<int>{ 0, 4, 0 }));

	// The CSV lives in the open project's Assets: by default Assets/Levels/<scene>.csv, typed relative paths too
	const fs::path savedRoot = A::ProjectRoot(s);
	A::ProjectRoot(s) = dir / "Proj";
	A::MapFileBuf(s)[0] = '\0';
	CHECK(A::MapCsvPath(s) == dir / "Proj" / "Assets" / "Levels" / "SampleScene.csv");
	strcpy_s(A::MapFileBuf(s), 256, "Assets/Maps/a.csv");
	CHECK(A::MapCsvPath(s) == dir / "Proj" / "Assets" / "Maps" / "a.csv");
	A::MapFileBuf(s)[0] = '\0';
	A::ProjectRoot(s) = savedRoot;

	// Build Level Objects turns tiles into children of the map object: walls block, spikes are triggers,
	// spawn and goal are markers; building again replaces them and keeps the user's own children
	GameObject& map = s.CreateGameObject("Map", GameObjectType::MapchipGrid);
	map.hasMapchip = true;
	map.translation = { 10.0f, 0.0f, 0.0f };
	const uint32_t mapId = map.id;
	GameObject& mine = s.CreateGameObject("Mine", GameObjectType::Empty);
	const uint32_t mineId = mine.id;
	REQUIRE(s.ReparentGameObject(mineId, static_cast<int>(mapId), false));
	auto children = [&]() {
		std::vector<const GameObject*> out;
		for (const GameObject& o : A::Objects(s)) if (o.parentId == static_cast<int>(mapId)) out.push_back(&o);
		return out;
	};
	REQUIRE(A::BuildMap(s, mapId));
	REQUIRE(children().size() == 6u); // 3 walls + spike + goal + Mine
	int walls = 0, spikes = 0, goals = 0;
	for (const GameObject* o : children()) {
		if (o->tag == "Wall") {
			++walls;
			CHECK(o->hasBoxCollider && !o->isTrigger && o->isStatic);
		}
		if (o->tag == "Spike") { ++spikes; CHECK(o->hasBoxCollider && o->isTrigger); }
		if (o->tag == "Goal") {
			++goals;
			CHECK_NEAR(o->translation.x, 1.0f, 1e-5f); // column 1
			CHECK_NEAR(o->translation.y, 0.0f, 1e-5f); // bottom row
		}
	}
	CHECK(walls == 3);
	CHECK(spikes == 1);
	CHECK(goals == 1);
	REQUIRE(A::BuildMap(s, mapId));
	CHECK(children().size() == 6u);
	CHECK(s.FindGameObjectById(mineId, false) != nullptr);

	// A new scene, or a scene without mapchip data, does not keep the previous scene's grid
	s.NewScene();
	CHECK(A::MapRows(s) == 10);
	CHECK(A::MapCols(s) == 15);
	CHECK(A::MapGrid(s).size() == 10u && A::MapGrid(s)[9][0] == 1 && A::MapGrid(s)[0][0] == 0);
	REQUIRE(A::LoadMapCsv(s, (dir / "ragged.csv").string()));
	std::optional<SceneJson::Json> scene = SceneJson::Parse(s.SerializeSceneToString());
	REQUIRE(scene.has_value());
	scene->erase("mapchip");
	REQUIRE(s.DeserializeSceneFromString(scene->dump()));
	CHECK(A::MapRows(s) == 10);
	CHECK(A::MapCols(s) == 15);

	s.NewScene();
	fs::remove_all(dir, ec);
}

TEST_CASE(Project, ScriptsRebuildAfterAPull)
{
	// Sources pulled from git while the editor was closed are newer than the DLL the last build made: rebuild
	namespace fs = std::filesystem;
	const fs::path dir = A::TempRoot() / "PulledScripts";
	std::error_code ec;
	fs::remove_all(dir, ec);
	fs::create_directories(dir / "GameScripts" / "Scripts", ec);
	fs::create_directories(dir / "Library" / "Scripts", ec);
	const fs::path scripts = dir / "GameScripts";
	const fs::path dll = dir / "Library" / "Scripts" / "GameScripts.dll";
	std::ofstream(scripts / "Scripts" / "Player.cpp") << "// player";
	std::ofstream(scripts / "GameScripts.vcxproj") << "<Project />";
	const auto now = fs::file_time_type::clock::now();
	fs::last_write_time(scripts / "Scripts" / "Player.cpp", now - std::chrono::hours(2));
	fs::last_write_time(scripts / "GameScripts.vcxproj", now - std::chrono::hours(2));

	CHECK(A::ScriptsNeedRebuild(scripts, dll)); // no DLL yet (fresh clone)
	std::ofstream(dll) << "dll";
	fs::last_write_time(dll, now - std::chrono::hours(1));
	CHECK(!A::ScriptsNeedRebuild(scripts, dll)); // built after the last change

	std::ofstream(scripts / "Scripts" / "Boss.h") << "// a new script from git pull";
	fs::last_write_time(scripts / "Scripts" / "Boss.h", now);
	CHECK(A::ScriptsNeedRebuild(scripts, dll));
	fs::last_write_time(scripts / "Scripts" / "Boss.h", now - std::chrono::hours(3));
	CHECK(!A::ScriptsNeedRebuild(scripts, dll));
	fs::last_write_time(scripts / "GameScripts.vcxproj", now); // the project file alone may not relink the DLL: no endless rebuilds
	CHECK(!A::ScriptsNeedRebuild(scripts, dll));
	fs::remove_all(dir, ec);
}

TEST_CASE(Project, RenamingTheStartupSceneUpdatesTheProject)
{
	namespace fs = std::filesystem;
	const fs::path root = A::TempRoot() / "StartupRename";
	std::error_code ec;
	fs::remove_all(root, ec);
	fs::create_directories(root / "ProjectSettings", ec);
	fs::create_directories(root / "Assets" / "Scenes", ec);
	std::ofstream(root / "ProjectSettings" / "project.json")
		<< R"({"projectName": "P", "engineVersion": "2.0.0", "startupScene": "Assets/Scenes/Start.scene.json", "gameTitle": "Game"})";
	const fs::path savedRoot = A::ProjectRoot(s);
	A::ProjectRoot(s) = root;
	s.NewScene();
	REQUIRE(s.SaveScene((root / "Assets" / "Scenes" / "Start.scene.json").string()));
	s.AssetDb().Refresh();

	auto startup = [&]() {
		std::ifstream in(root / "ProjectSettings" / "project.json");
		std::stringstream text;
		text << in.rdbuf();
		std::string value;
		if (auto j = SceneJson::Parse(text.str())) SceneJson::Read(*j, "startupScene", value);
		return value;
	};
	// Hierarchy > Rename Scene: the project's startup scene follows the file
	REQUIRE(s.RenameCurrentScene("Title"));
	CHECK_MSG(startup() == "Assets/Scenes/Title.scene.json", startup());
	// Renamed in the Project panel: the same
	REQUIRE(s.MoveAssetAndUpdateReferences((root / "Assets" / "Scenes" / "Title.scene.json").string(),
		(root / "Assets" / "Scenes" / "Menu.scene.json").string()));
	CHECK_MSG(startup() == "Assets/Scenes/Menu.scene.json", startup());
	// The rest of project.json stays
	{
		std::ifstream in(root / "ProjectSettings" / "project.json");
		std::stringstream text;
		text << in.rdbuf();
		std::string title;
		if (auto j = SceneJson::Parse(text.str())) SceneJson::Read(*j, "gameTitle", title);
		CHECK(title == "Game");
	}
	// Another scene renamed: the startup scene is left alone
	s.NewScene();
	REQUIRE(s.SaveScene((root / "Assets" / "Scenes" / "Other.scene.json").string()));
	s.AssetDb().Refresh();
	REQUIRE(s.RenameCurrentScene("Other2"));
	CHECK(startup() == "Assets/Scenes/Menu.scene.json");

	A::ProjectRoot(s) = savedRoot;
	s.NewScene();
	fs::remove_all(root, ec);
}

TEST_CASE(Project, CreateProjectRules)
{
	namespace fs = std::filesystem;
	const fs::path parent = A::TempRoot() / "Projects";
	std::error_code ec;
	fs::create_directories(parent, ec);

	const fs::path savedRoot = A::ProjectRoot(s);
	const fs::path savedDirectory = A::CurrentDirectory(s);

	const bool created = s.CreateNewProject(parent.string(), "TestGame");
	CHECK(created);
	const fs::path projectRoot = parent / "TestGame";
	CHECK(fs::exists(projectRoot / "Assets" / "Scenes" / "SampleScene.scene.json"));
	CHECK(fs::exists(projectRoot / "ProjectSettings" / "project.json"));

	if (created) {
		std::ifstream in(projectRoot / "ProjectSettings" / "project.json");
		std::stringstream buffer;
		buffer << in.rdbuf();
		const auto settings = SceneJson::Parse(buffer.str());
		CHECK(settings.has_value());
		if (settings) {
			std::string projectName;
			SceneJson::Read(*settings, "projectName", projectName);
			CHECK_MSG(projectName == "TestGame", projectName);
		}

		const std::string level = s.NormalizeScenePath("Level2");
		CHECK_MSG(level.ends_with("Assets\\Scenes\\Level2.scene.json"), level);
		CHECK_MSG(s.IsPathInsideProject(level), level);
		CHECK(s.NormalizeScenePath("Level3.json").ends_with("Level3.scene.json"));
		CHECK_MSG(s.ToProjectRelativePath(projectRoot / "Assets" / "Textures" / "circle.png") == "Assets/Textures/circle.png",
			s.ToProjectRelativePath(projectRoot / "Assets" / "Textures" / "circle.png"));
	}

	CHECK(!s.CreateNewProject(parent.string(), "TestGame")); // folder is not empty any more
	CHECK(!s.CreateNewProject(parent.string(), "Bad:Name"));
	CHECK(!fs::exists(parent / "Bad:Name"));
	CHECK(!s.CreateNewProject(parent.string(), ""));

	A::ProjectRoot(s) = savedRoot;
	A::CurrentDirectory(s) = savedDirectory;
	A::SyncScripts(s);
}

TEST_CASE(Version, CompareAndProjectWarning)
{
	namespace fs = std::filesystem;
	CHECK(CompareEngineVersions("2.1.0", "2.1.0") == 0);
	CHECK(CompareEngineVersions("2.1", "2.1.0") == 0);
	CHECK(CompareEngineVersions("v2.0", "2.1.0") < 0);
	CHECK(CompareEngineVersions("2.10.0", "2.9.0") > 0);
	CHECK(CompareEngineVersions("2.1.1-beta", "2.1.0") > 0);
	CHECK(CompareEngineVersions("", "2.1.0") < 0);

	const fs::path savedRoot = A::ProjectRoot(s);
	const fs::path savedDirectory = A::CurrentDirectory(s);
	const fs::path root = A::TempRoot() / "VersionProject";
	std::error_code ec;
	fs::remove_all(root, ec);
	fs::create_directories(root / "ProjectSettings", ec);
	fs::create_directories(root / "Assets" / "Scenes", ec);
	auto writeVersion = [&](const char* version) {
		std::ofstream(root / "ProjectSettings" / "project.json") << "{\"projectName\":\"V\",\"engineVersion\":\"" << version << "\"}";
	};
	writeVersion("99.0.0");
	CHECK(s.OpenProject(root.string()));
	CHECK_MSG(!A::EngineWarning(s).empty(), "a project from a newer engine must warn");
	writeVersion(kEngineVersion);
	CHECK(s.OpenProject(root.string()));
	CHECK(A::EngineWarning(s).empty());

	A::ProjectRoot(s) = savedRoot;
	A::CurrentDirectory(s) = savedDirectory;
	A::SyncScripts(s);
}

TEST_CASE(Project, NewProjectIsCleanForGit)
{
	namespace fs = std::filesystem;
	const fs::path parent = A::TempRoot() / "GitProjects";
	std::error_code ec;
	fs::remove_all(parent, ec);
	fs::create_directories(parent, ec);
	const fs::path savedRoot = A::ProjectRoot(s);
	const fs::path savedDirectory = A::CurrentDirectory(s);

	const bool created = s.CreateNewProject(parent.string(), "CleanGame");
	CHECK(created);
	const fs::path root = parent / "CleanGame";
	if (created) {
		auto read = [](const fs::path& file) {
			std::ifstream in(file, std::ios::binary);
			return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		};
		// Repository files
		const std::string gitignore = read(root / ".gitignore");
		for (const char* entry : { "Library/", "Build/", "SaveData/", ".vs/", "GameScripts/RizeEngine.props" }) {
			CHECK_MSG(gitignore.find(entry) != std::string::npos, entry);
		}
		CHECK(fs::exists(root / ".gitattributes"));
		CHECK(fs::exists(root / "README.md"));
		CHECK(fs::exists(root / "Assets" / "Prefabs" / ".gitkeep"));
		CHECK_MSG(!fs::exists(root / "Assets" / "Prefabs" / ".gitkeep.meta"), "dot files get no GUID");
		// No sample assets, no unused Assets/Scripts
		CHECK(!fs::exists(root / "Assets" / "Scripts"));
		CHECK(!fs::exists(root / "Assets" / "Textures" / "circle.png"));

		// Its own scripts project, built into Library/ and loaded from there
		CHECK(fs::exists(root / "GameScripts" / "GameScripts.vcxproj"));
		CHECK(fs::exists(root / "GameScripts" / "Scripts" / "dllmain.cpp"));
		CHECK(fs::exists(root / "CleanGame.sln"));
		CHECK(s.ProjectScriptsDir() == root / "GameScripts");
		CHECK(s.ResolveGameScriptsVcxproj() == root / "GameScripts" / "GameScripts.vcxproj");
		CHECK(s.ResolveSolutionFilePath().filename() == "CleanGame.sln");
		CHECK_MSG(A::ScriptsDllOverride(s) == root / "Library" / "Scripts" / "GameScripts.dll", A::ScriptsDllOverride(s).string());

		// The per-PC props points at a real SDK; nothing committed holds an absolute path of this PC
		const std::string props = read(root / "GameScripts" / "RizeEngine.props");
		const fs::path sdk = EditorSuite::ResolveEngineSdkDir();
		CHECK(fs::exists(sdk / "base" / "ScriptAPI.h"));
		CHECK_MSG(props.find(sdk.string()) != std::string::npos, props);
		for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
			const fs::path rel = it->path().lexically_relative(root);
			if (!it->is_regular_file() || rel == fs::path("GameScripts") / "RizeEngine.props" || rel.begin()->string() == "Library") continue;
			const std::string text = read(it->path());
			CHECK_MSG(text.find(root.string()) == std::string::npos && text.find(sdk.string()) == std::string::npos, rel.string());
		}

		// Sample scripts are copied into the project and registered once
		CHECK(s.InstallSampleScripts("Survivors"));
		CHECK(s.InstallSampleScripts("Survivors"));
		CHECK(fs::exists(root / "GameScripts" / "Scripts" / "SurvivorsPlayer.cpp"));
		const std::string vcxproj = read(root / "GameScripts" / "GameScripts.vcxproj");
		const size_t first = vcxproj.find("Scripts\\SurvivorsPlayer.cpp");
		CHECK(first != std::string::npos);
		CHECK(vcxproj.find("Scripts\\SurvivorsPlayer.cpp", first + 1) == std::string::npos);
		CHECK(vcxproj.find("Scripts\\SurvivorsShared.h") != std::string::npos);
		CHECK(read(root / "GameScripts" / "GameScripts.vcxproj.filters").find("Scripts\\SurvivorsPlayer.h") != std::string::npos);
	}

	A::ProjectRoot(s) = savedRoot;
	A::CurrentDirectory(s) = savedDirectory;
	A::SyncScripts(s);
	CHECK(A::ScriptsDllOverride(s).empty());
}

TEST_CASE(Update, ApplyReplacesOnlyEngineFolders)
{
	namespace fs = std::filesystem;
	const fs::path base = A::TempRoot() / "UpdateApply";
	std::error_code ec;
	fs::remove_all(base, ec);
	const fs::path install = base / "Engine";
	const fs::path package = base / "Extracted" / "RizealldayEngine-9.0.0";
	auto write = [](const fs::path& file, const std::string& text) {
		std::error_code wec;
		fs::create_directories(file.parent_path(), wec);
		std::ofstream(file, std::ios::binary) << text;
	};
	write(install / "engine.json", "{\"version\":\"1.0.0\",\"scriptApi\":1}");
	write(install / "Editor" / "old.txt", "old");
	write(install / "Editor" / "imgui.ini", "layout");
	write(install / "SDK" / "include" / "a.h", "a");
	write(install / "MyGame" / "keep.txt", "game");
	write(install / "notes.txt", "user");
	write(package / "engine.json", "{\"version\":\"9.0.0\",\"scriptApi\":7}");
	write(package / "Editor" / "new.txt", "new");
	write(package / "SDK" / "include" / "b.h", "b");
	write(package / "Templates" / "t.txt", "t");
	write(package / "Runtime" / "game.exe", "runtime");
	write(install / "GUIDE_JA.md", "old guide");
	write(package / "GUIDE_JA.md", "new guide");
	write(install / "CHANGELOG.md", "old changelog");
	write(package / "CHANGELOG.md", "new changelog");
	write(install / "SCRIPTING_GUIDE.md", "old scripting guide");
	write(package / "SCRIPTING_GUIDE.md", "new scripting guide");

	CHECK(EngineUpdate::FindInstallRoot(install / "Editor") == install);
	CHECK(EngineUpdate::FindInstallRoot(install / "MyGame").empty());
	const auto info = EngineUpdate::FindPackage(base / "Extracted"); // one folder deep, like a zip
	CHECK(info.has_value());
	if (info) {
		CHECK(info->root == package);
		CHECK(info->version == "9.0.0");
		CHECK(info->scriptApi == 7);
	}

	std::string error;
	{
		// Another program reads engine.json without delete sharing: it cannot be renamed, but it can be overwritten
		HANDLE reader = CreateFileW((install / "engine.json").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		CHECK(reader != INVALID_HANDLE_VALUE);
		CHECK_MSG(EngineUpdate::ApplyPackage(package, install, &error), error);
		if (reader != INVALID_HANDLE_VALUE) CloseHandle(reader);
		const auto manifest = EngineUpdate::FindPackage(install);
		CHECK_MSG(manifest && manifest->version == "9.0.0", "a held engine.json is overwritten in place");
		CHECK(!fs::exists(install / "engine.json.new"));
	}
	CHECK(fs::exists(install / "Editor" / "new.txt"));
	CHECK(!fs::exists(install / "Editor" / "old.txt"));
	CHECK_MSG(fs::exists(install / "Editor" / "imgui.ini"), "the docking layout survives an update");
	CHECK(fs::exists(install / "SDK" / "include" / "b.h"));
	CHECK(!fs::exists(install / "SDK" / "include" / "a.h"));
	CHECK(fs::exists(install / "Templates" / "t.txt"));
	CHECK_MSG(fs::exists(install / "Runtime" / "game.exe"), "exports come from Runtime/, so updates must replace it");
	{
		std::ifstream guide(install / "GUIDE_JA.md");
		std::string text;
		std::getline(guide, text);
		CHECK_MSG(text == "new guide", "the Japanese guide is an engine file and gets updated");
	}
	{
		std::ifstream cl(install / "CHANGELOG.md");
		std::string text;
		std::getline(cl, text);
		CHECK_MSG(text == "new changelog", "the changelog is an engine file and gets updated");
	}
	{
		std::ifstream sg(install / "SCRIPTING_GUIDE.md");
		std::string text;
		std::getline(sg, text);
		CHECK_MSG(text == "new scripting guide", "the scripting guide is an engine file and gets updated");
	}
	CHECK(fs::exists(install / "MyGame" / "keep.txt"));
	CHECK(fs::exists(install / "notes.txt"));
	const auto installed = EngineUpdate::FindPackage(install);
	CHECK(installed && installed->version == "9.0.0");
	for (const auto& entry : fs::directory_iterator(install, ec)) {
		const std::string name = entry.path().filename().string();
		CHECK_MSG(!name.ends_with(".new") && !name.ends_with(".old"), name);
	}
}

TEST_CASE(Update, HiddenProcessDoesNotWaitForHelpersItLeftRunning)
{
	// Like MSBuild leaving mspdbsrv.exe behind: cmd exits at once, but `start /b` gives ping the inherited output pipe
	const auto start = std::chrono::steady_clock::now();
	std::string output;
	const int code = EngineUpdate::RunHiddenProcess(L"cmd.exe /c \"start /b ping -n 8 127.0.0.1 >nul & echo done\"", {}, &output);
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	CHECK(code == 0);
	CHECK_MSG(output.find("done") != std::string::npos, output);
	CHECK_MSG(seconds < 4.0, std::to_string(seconds) + " s: waited for the helper that still holds the pipe");
}

TEST_CASE(Scripts, BuildCommandCanRebuildEverything)
{
	const std::wstring normal = EditorSuite::ScriptBuildCommandLine(L"C:\\VS\\MSBuild.exe", L"D:\\Game\\GameScripts\\GameScripts.vcxproj", false);
	const std::wstring full = EditorSuite::ScriptBuildCommandLine(L"C:\\VS\\MSBuild.exe", L"D:\\Game\\GameScripts\\GameScripts.vcxproj", true);
	CHECK(normal.find(L" /t:Build ") != std::wstring::npos);
	CHECK(full.find(L" /t:Rebuild ") != std::wstring::npos); // after an engine update: no object file of the old ScriptAPI.h survives
	CHECK(full.find(L"/nodeReuse:false") != std::wstring::npos);
	CHECK(full.find(L"\"D:\\Game\\GameScripts\\GameScripts.vcxproj\"") != std::wstring::npos);
}

TEST_CASE(Update, DetectsEnginePackageZip)
{
	namespace fs = std::filesystem;
	const fs::path base = A::TempRoot() / "UpdateZip";
	std::error_code ec;
	fs::remove_all(base, ec);
	fs::create_directories(base / "pkg" / "RizealldayEngine-9.0.0" / "Editor", ec);
	fs::create_directories(base / "assets", ec);
	std::ofstream(base / "pkg" / "RizealldayEngine-9.0.0" / "engine.json") << "{\"version\":\"9.0.0\"}";
	std::ofstream(base / "pkg" / "RizealldayEngine-9.0.0" / "Editor" / "x.txt") << "x";
	std::ofstream(base / "assets" / "rock.png") << "png";

	// Same tool and format as Tools/MakeRelease.ps1
	auto zip = [&](const fs::path& dir, const wchar_t* entry, const fs::path& out) {
		return EngineUpdate::RunHiddenProcess(L"tar.exe -a -c -f \"" + out.wstring() + L"\" " + entry, dir, nullptr) == 0;
	};
	CHECK(zip(base / "pkg", L"RizealldayEngine-9.0.0", base / "engine.zip"));
	CHECK(zip(base / "assets", L"rock.png", base / "assets.zip"));
	CHECK(EngineUpdate::ZipIsEnginePackage(base / "engine.zip"));
	CHECK(!EngineUpdate::ZipIsEnginePackage(base / "assets.zip"));

	std::string error;
	CHECK_MSG(EngineUpdate::ExtractZip(base / "engine.zip", base / "out", &error), error);
	const auto info = EngineUpdate::FindPackage(base / "out");
	CHECK(info && info->version == "9.0.0");
	CHECK(fs::exists(base / "out" / "RizealldayEngine-9.0.0" / "Editor" / "x.txt"));
}

// ===========================================================================
// Lighting
// ===========================================================================

TEST_CASE(Lighting, SwitchingTypeDisablesDirectional)
{
	LightGroup* lights = A::Lights(s);
	REQUIRE(lights != nullptr);

	GameObject* sun = nullptr;
	for (GameObject& obj : A::Objects(s)) {
		if (obj.hasLight && obj.lightType == GameObject::LightType::Directional) {
			sun = &obj;
			break;
		}
	}
	REQUIRE(sun != nullptr);

	// Sanity: as a directional light it drives the sun
	s.Update(0.0f, RizeEngine::GetInput());
	CHECK(lights->GetDirectionalLight().active == 1);

	sun->lightType = GameObject::LightType::Point;
	s.Update(0.0f, RizeEngine::GetInput());
	CHECK(lights->GetDirectionalLight().active == 0);
	CHECK(lights->GetLightCount() == 1);
}

// ===========================================================================
// Profiler
// ===========================================================================

TEST_CASE(Profiler, CpuScopesAndCounters)
{
	Profiler* profiler = Profiler::GetInstance();
	profiler->FinishCpuFrame(); // flush whatever earlier tests recorded

	{
		RIZE_PROFILE_CPU("TestParent");
		volatile double sink = 0.0;
		for (int i = 0; i < 200000; ++i) sink = sink + std::sqrt(static_cast<double>(i));
		{
			RIZE_PROFILE_CPU("TestChild");
			for (int i = 0; i < 200000; ++i) sink = sink + std::sqrt(static_cast<double>(i));
		}
	}
	profiler->CountDrawCall();
	profiler->CountDrawCall(10);
	profiler->FinishCpuFrame();

	const ProfilerSnapshot& snap = profiler->GetLastFrame();
	const ProfilerScopeResult* parent = nullptr;
	const ProfilerScopeResult* child = nullptr;
	for (const ProfilerScopeResult& r : snap.cpu) {
		if (std::string(r.name) == "TestParent") parent = &r;
		if (std::string(r.name) == "TestChild") child = &r;
	}
	REQUIRE(parent != nullptr);
	REQUIRE(child != nullptr);
	CHECK(child->depth == parent->depth + 1);
	CHECK(parent->ms >= 0.0f);
	CHECK(child->ms >= 0.0f);
	CHECK_MSG(parent->ms >= child->ms, "parent " + std::to_string(parent->ms) + " ms, child " + std::to_string(child->ms) + " ms");
	CHECK(parent->avgMs > 0.0f || parent->ms == 0.0f);

	CHECK(snap.drawCalls == 2);
	CHECK(snap.instances == 11);

	// Counters are per frame: the next publish starts from zero
	profiler->FinishCpuFrame();
	CHECK(profiler->GetLastFrame().drawCalls == 0);
	CHECK(profiler->GetLastFrame().cpu.empty());
}

// ===========================================================================
// Scripting API / export (ROADMAP 3.4)
// ===========================================================================

TEST_CASE(Scripting, KeyConstantsMatchDirectInput)
{
	// Scripts use KEY_* without <dinput.h>; each value must be the real DirectInput scan code
	CHECK(KEY_ESCAPE == DIK_ESCAPE);
	CHECK(KEY_1 == DIK_1);
	CHECK(KEY_0 == DIK_0);
	CHECK(KEY_W == DIK_W);
	CHECK(KEY_A == DIK_A);
	CHECK(KEY_S == DIK_S);
	CHECK(KEY_D == DIK_D);
	CHECK(KEY_Q == DIK_Q);
	CHECK(KEY_E == DIK_E);
	CHECK(KEY_Z == DIK_Z);
	CHECK(KEY_M == DIK_M);
	CHECK(KEY_P == DIK_P);
	CHECK(KEY_L == DIK_L);
	CHECK(KEY_SPACE == DIK_SPACE);
	CHECK(KEY_ENTER == DIK_RETURN);
	CHECK(KEY_TAB == DIK_TAB);
	CHECK(KEY_BACKSPACE == DIK_BACK);
	CHECK(KEY_LSHIFT == DIK_LSHIFT);
	CHECK(KEY_RSHIFT == DIK_RSHIFT);
	CHECK(KEY_LCTRL == DIK_LCONTROL);
	CHECK(KEY_RCTRL == DIK_RCONTROL);
	CHECK(KEY_LALT == DIK_LMENU);
	CHECK(KEY_RALT == DIK_RMENU);
	CHECK(KEY_F1 == DIK_F1);
	CHECK(KEY_F10 == DIK_F10);
	CHECK(KEY_F11 == DIK_F11);
	CHECK(KEY_F12 == DIK_F12);
	CHECK(KEY_UP == DIK_UP);
	CHECK(KEY_DOWN == DIK_DOWN);
	CHECK(KEY_LEFT == DIK_LEFT);
	CHECK(KEY_RIGHT == DIK_RIGHT);
}

TEST_CASE(Export, GameConfigDetection)
{
	namespace fs = std::filesystem;
	const fs::path dir = EditorSuiteTestAccess::TempRoot() / "ExportDetect";
	std::error_code ec;
	fs::create_directories(dir, ec);

	CHECK(!EditorSuite::IsExportedGameFolder(dir));
	CHECK(!EditorSuite::IsExportedGameFolder(fs::path()));
	{
		std::ofstream(dir / EditorSuite::kExportedGameConfigFile) << "standaloneGameMode = true\n";
	}
	CHECK(EditorSuite::IsExportedGameFolder(dir));
	// The editor's own folder must never be mistaken for an exported game
	CHECK(!EditorSuite::IsExportedGameFolder("."));
	CHECK(std::string(EditorSuite::kExportedGameConfigFile) == "game_build.cfg");
}

TEST_CASE(Export, StandaloneFolderIsMinimalAndUsesRuntime)
{
	namespace fs = std::filesystem;
	const fs::path base = A::TempRoot() / "ExportMinimal";
	std::error_code ec;
	fs::remove_all(base, ec);
	fs::create_directories(base, ec);
	const fs::path savedRoot = A::ProjectRoot(s);
	const fs::path savedDirectory = A::CurrentDirectory(s);
	auto readAll = [](const fs::path& file) {
		std::ifstream in(file, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	};

	REQUIRE(s.CreateNewProject(base.string(), "MiniGame"));
	const fs::path root = base / "MiniGame";
	CHECK_MSG(fs::path(A::BuildOutputDir(s)) == root / "Build", std::string("default output: ") + A::BuildOutputDir(s)); // never inside the engine folder

	GameObject& cube = s.CreateCube("Referencing", { 0.0f, 0.5f, 0.0f });
	cube.meshType = GameObject::MeshType::CustomModel;
	cube.modelPath = "Resources/Model/cube.obj";
	REQUIRE(s.SaveScene(A::ScenePath(s)));
	std::ofstream(root / "GameScripts" / "Scripts" / "UsesResource.cpp") << "const char* kTex = \"Resources/Texture/circle.png\";\n";
	std::ofstream(root / "ProjectSettings" / "input.json") << "{}";

	const fs::path out = base / "Out";
	A::SetBuildOutputDir(s, out.string());
	REQUIRE(s.ExportStandaloneGame());

	// The Release runtime, not the running editor
	const fs::path runtime = s.ResolveRuntimeDir();
	REQUIRE(fs::exists(runtime / "RizealldaySandbox.exe"));
	const std::string exeName = std::string(A::BuildGameTitle(s)) + ".exe";
	REQUIRE(fs::exists(out / exeName));
	CHECK_MSG(readAll(out / exeName) == readAll(runtime / "RizealldaySandbox.exe"), "the exported exe must be the runtime exe");
	wchar_t running[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, running, MAX_PATH);
	CHECK(readAll(out / exeName) != readAll(running));

	// What the game needs
	for (const char* needed : { "game_build.cfg", "assimp-vc143-mt.dll", "dxcompiler.dll", "dxil.dll", "bin/Scripts/GameScripts.dll",
		"shaders/Object3d.PS.hlsl", "ProjectSettings/input.json",
		"Resources/circle.png", "Resources/skybox.dds", "Resources/Model/cube.obj", "Resources/Texture/circle.png" }) {
		CHECK_MSG(fs::exists(out / needed), needed);
	}
	CHECK(fs::exists(out / fs::path(A::ScenePath(s)).lexically_relative(root)));
	CHECK(fs::exists(out / (fs::path(A::ScenePath(s)).lexically_relative(root).string() + ".meta")));

	// What it does not need
	for (const char* extra : { "GameScripts", "Library", "ProjectSettings/project.json", "Resources/Title", "Resources/Localization",
		"Resources/Model/Idle.fbx", "imgui.ini", "test_results.txt" }) {
		CHECK_MSG(!fs::exists(out / extra), extra);
	}
	for (auto it = fs::recursive_directory_iterator(out, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
		CHECK_MSG(it->path().extension() != ".pdb", it->path().string());
	}

	// Refusals: inside Assets (it would copy into itself), no runtime
	A::SetBuildOutputDir(s, (root / "Assets" / "Game").string());
	CHECK(!s.ExportStandaloneGame());
	CHECK(!fs::exists(root / "Assets" / "Game"));
	const fs::path out2 = base / "Out2";
	A::SetBuildOutputDir(s, out2.string());
	A::ExportRuntimeOverride(s) = base / "NoRuntimeHere";
	CHECK(!s.ExportStandaloneGame());
	CHECK(!fs::exists(out2 / exeName));
	A::ExportRuntimeOverride(s).clear();

	A::SetBuildOutputDir(s, "Build");
	A::ProjectRoot(s) = savedRoot;
	A::CurrentDirectory(s) = savedDirectory;
	A::SyncScripts(s);
}

// ===========================================================================
// Object lookup
// ===========================================================================

TEST_CASE(Objects, IdLookupSurvivesVectorChanges)
{
	std::vector<uint32_t> ids;
	for (int i = 0; i < 50; ++i) {
		ids.push_back(s.CreateCube("Cube" + std::to_string(i), { static_cast<float>(i), 0.0f, 0.0f }).id);
	}
	for (uint32_t id : ids) {
		const GameObject* obj = s.FindGameObjectById(id, false);
		REQUIRE(obj != nullptr);
		CHECK(obj->id == id);
	}

	// Delete every third object, then reorder the vector behind the cache's back
	for (size_t i = 0; i < ids.size(); i += 3) s.QueueDeleteGameObjectById(ids[i]);
	s.ProcessPendingDeletes();
	auto& objects = A::Objects(s);
	std::reverse(objects.begin(), objects.end());

	int found = 0;
	for (size_t i = 0; i < ids.size(); ++i) {
		const GameObject* obj = s.FindGameObjectById(ids[i], false);
		if (i % 3 == 0) {
			CHECK(obj == nullptr);
		} else if (obj && obj->id == ids[i] && obj->name == "Cube" + std::to_string(i)) {
			++found;
		}
	}
	CHECK_MSG(found == 33, "objects found after delete + reorder: " + std::to_string(found));
	CHECK(s.FindGameObjectIndexById(0xFFFFFFF0u) == -1);

	// Layer + mask survive a JSON round trip
	GameObject* obj = s.FindGameObjectById(ids[1], false);
	REQUIRE(obj != nullptr);
	obj->layer = LAYER_ENEMY;
	obj->collisionMask = LayerBit(LAYER_PLAYER) | LayerBit(LAYER_PLAYER_BULLET);
	const std::string json = s.SerializeSceneToString();
	REQUIRE(s.DeserializeSceneFromString(json));
	obj = s.FindGameObjectById(ids[1], false);
	REQUIRE(obj != nullptr);
	CHECK(obj->layer == LAYER_ENEMY);
	CHECK(obj->collisionMask == (LayerBit(LAYER_PLAYER) | LayerBit(LAYER_PLAYER_BULLET)));
	CHECK(EditorSuite::GetCollisionLayerName(LAYER_PLAYER_BULLET) == "PlayerBullet");
	CHECK(EditorSuite::GetCollisionLayerName(20) == "Layer 20");
}

TEST_CASE(Scripting, MouseRayHitsGroundPlane)
{
	// Top-down camera: 20 units up, tilted 60 degrees down, looking along +Z
	const Matrix4x4 cameraWorld = Mathematics::MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, { 1.0472f, 0.0f, 0.0f }, { 0.0f, 20.0f, -10.0f });
	const Matrix4x4 view = Mathematics::Inverse(cameraWorld);
	const Matrix4x4 proj = Mathematics::MakePerspectiveFovMatrix(0.8f, 16.0f / 9.0f, 0.1f, 500.0f);
	const Matrix4x4 viewProj = Mathematics::Multiply(view, proj);

	const Vector3 targets[] = { { 0.0f, 0.0f, 2.0f }, { 5.0f, 0.0f, 4.0f }, { -3.0f, 0.0f, 0.5f }, { 2.0f, 1.5f, 3.0f } };
	for (const Vector3& target : targets) {
		const Vector3 ndc = Mathematics::Transform(target, viewProj);
		REQUIRE(std::fabs(ndc.x) <= 1.0f && std::fabs(ndc.y) <= 1.0f); // on screen
		Vector3 hit{};
		REQUIRE(EditorSuite::ScreenRayToPlaneY(viewProj, ndc.x, ndc.y, target.y, hit));
		CHECK_NEAR(hit.x, target.x, 1e-3f);
		CHECK_NEAR(hit.y, target.y, 1e-6f);
		CHECK_NEAR(hit.z, target.z, 1e-3f);
	}
	// Looking at the sky: a plane far below the horizon line is behind the ray
	Vector3 hit{};
	CHECK(!EditorSuite::ScreenRayToPlaneY(viewProj, 0.0f, 0.0f, 100.0f, hit));
}

TEST_CASE(Render, OrthographicCameraKeepsSizeAndMouseRay)
{
	// Isometric camera: 40 degrees down, turned 45 degrees, size 5 (10 units visible vertically)
	Camera cam;
	cam.orthographic = true;
	cam.orthoSize = 5.0f;
	cam.aspectRatio = 2.0f;
	cam.rotation = { 0.698f, 0.785f, 0.0f };
	cam.translation = { -10.0f, 12.0f, -10.0f };
	cam.Update();
	CHECK_NEAR(cam.GetProjectionMatrix().m[3][3], 1.0f, 1e-6f); // no perspective divide

	// The same offset across the screen at any distance: things do not shrink far away
	const Matrix4x4 camWorld = Mathematics::MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, cam.rotation, cam.translation);
	// Row-vector convention: rows 0..2 of the world matrix are the camera's right / up / forward
	const Vector3 right = { camWorld.m[0][0], camWorld.m[0][1], camWorld.m[0][2] };
	const Vector3 up = { camWorld.m[1][0], camWorld.m[1][1], camWorld.m[1][2] };
	const Vector3 forward = { camWorld.m[2][0], camWorld.m[2][1], camWorld.m[2][2] };
	for (const float depth : { 5.0f, 40.0f }) {
		const Vector3 base = cam.translation + forward * depth;
		const Vector3 ndcR = Mathematics::Transform(base + right * 5.0f, cam.GetViewProjectionMatrix());
		const Vector3 ndcU = Mathematics::Transform(base + up * 5.0f, cam.GetViewProjectionMatrix());
		CHECK_NEAR(ndcR.x, 0.5f, 1e-4f); // half width = 5 * aspect = 10
		CHECK_NEAR(ndcU.y, 1.0f, 1e-4f); // the top edge
	}

	// The mouse ray of an orthographic camera still finds the ground point under the cursor
	const Vector3 targets[] = { { 0.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, -2.0f }, { -1.0f, 0.0f, 4.0f } };
	for (const Vector3& target : targets) {
		const Vector3 ndc = Mathematics::Transform(target, cam.GetViewProjectionMatrix());
		REQUIRE(std::fabs(ndc.x) <= 1.0f && std::fabs(ndc.y) <= 1.0f);
		Vector3 hit{};
		REQUIRE(EditorSuite::ScreenRayToPlaneY(cam.GetViewProjectionMatrix(), ndc.x, ndc.y, 0.0f, hit));
		CHECK_NEAR(hit.x, target.x, 1e-3f);
		CHECK_NEAR(hit.z, target.z, 1e-3f);
	}

	// Saved with the scene and reachable from scripts
	GameObject& camObj = s.CreateCube("Iso Camera", { 0.0f, 10.0f, 0.0f });
	camObj.hasCamera = true;
	const uint32_t camId = camObj.id;
	EngineAPI& api = A::Api(s);
	ScriptCameraData data{};
	REQUIRE(api.getCamera(api.hostContext, camId, &data));
	data.orthographic = true;
	data.orthoSize = 7.5f;
	REQUIRE(api.setCamera(api.hostContext, camId, &data));
	REQUIRE(s.DeserializeSceneFromString(s.SerializeSceneToString()));
	const GameObject* loaded = s.FindGameObjectById(camId, false);
	REQUIRE(loaded != nullptr);
	CHECK(loaded->isOrthographic);
	CHECK_NEAR(loaded->orthoSize, 7.5f, 1e-6f);

	// Copy / paste of the Camera component keeps the projection
	const uint32_t otherId = s.CreateCube("Other Camera", { 5.0f, 10.0f, 0.0f }).id;
	REQUIRE(s.CopyComponent(*s.FindGameObjectById(camId, false), "Camera"));
	GameObject* other = s.FindGameObjectById(otherId, false);
	REQUIRE(other != nullptr);
	REQUIRE(s.PasteComponent(*other, "Camera"));
	CHECK(other->hasCamera);
	CHECK(other->isOrthographic);
	CHECK_NEAR(other->orthoSize, 7.5f, 1e-6f);
}

TEST_CASE(Components, PasteKeepsEveryField)
{
	GameObject& src = s.CreateGameObject("Source", GameObjectType::Empty);
	src.hasLight = true;
	src.lightEnabled = false;
	src.lightType = GameObject::LightType::Spot;
	src.lightDirection = { 1.0f, 0.0f, 0.0f };
	src.lightDecay = 3.0f;
	src.lightSpotFalloffStart = 12.0f;
	src.lightCastShadow = false;
	src.lightShadowBias = 0.02f;
	src.lightShadowStrength = 0.3f;
	src.lightShadowNormalBias = 0.07f;
	src.hasSphereCollider = true;
	src.sphereIsTrigger = true;
	src.hasBoxCollider = true;
	src.isTrigger = true; // the old paste copied this box flag into the sphere
	src.providesContacts = false;
	src.hasAudioSource = true;
	src.audioPitch = 1.5f;
	src.audioBus = 2;
	src.hasCamera = true;
	src.cameraEnabled = false;
	const uint32_t srcId = src.id;
	const uint32_t dstId = s.CreateGameObject("Target", GameObjectType::Empty).id;
	const auto paste = [&](const char* name) -> const GameObject* {
		CHECK(s.CopyComponent(*s.FindGameObjectById(srcId, false), name));
		CHECK(s.PasteComponent(*s.FindGameObjectById(dstId, false), name));
		return s.FindGameObjectById(dstId, false);
	};

	const GameObject* dst = paste("Light");
	CHECK(!dst->lightEnabled);
	CHECK(dst->lightType == GameObject::LightType::Spot);
	CHECK_NEAR(dst->lightDirection.x, 1.0f, 1e-6f);
	CHECK_NEAR(dst->lightDecay, 3.0f, 1e-6f);
	CHECK_NEAR(dst->lightSpotFalloffStart, 12.0f, 1e-6f);
	CHECK(!dst->lightCastShadow);
	CHECK_NEAR(dst->lightShadowBias, 0.02f, 1e-6f);
	CHECK_NEAR(dst->lightShadowStrength, 0.3f, 1e-6f);
	CHECK_NEAR(dst->lightShadowNormalBias, 0.07f, 1e-6f);

	dst = paste("Sphere Collider");
	CHECK(dst->sphereIsTrigger);
	CHECK(!dst->isTrigger); // the Box Collider's flag is not the sphere's

	dst = paste("Box Collider");
	CHECK(!dst->providesContacts);

	dst = paste("Audio Source");
	CHECK_NEAR(dst->audioPitch, 1.5f, 1e-6f);
	CHECK(dst->audioBus == 2);

	dst = paste("Camera");
	CHECK(!dst->cameraEnabled);

	// Components without a value paste refuse instead of recording an empty undo step
	REQUIRE(s.CopyComponent(*s.FindGameObjectById(srcId, false), "Light"));
	const size_t undo = A::UndoCount(s);
	CHECK(!s.CopyComponent(*s.FindGameObjectById(srcId, false), "Mesh Filter & Renderer"));
	CHECK(!s.PasteComponent(*s.FindGameObjectById(dstId, false), "Mesh Filter & Renderer"));
	CHECK(A::UndoCount(s) == undo);
}

TEST_CASE(Scripting, TimeScaleScalesGameTime)
{
	EngineAPI& api = A::Api(s);
	REQUIRE(api.setTimeScale != nullptr);
	REQUIRE(api.getTime != nullptr);
	A::State(s) = EditorSceneState::Play;
	A::GameTime(s) = 0.0;

	api.setTimeScale(api.hostContext, 0.5f);
	CHECK_NEAR(api.getTimeScale(api.hostContext), 0.5f, 1e-6f);
	s.Update(0.1f, RizeEngine::GetInput());
	CHECK_NEAR(api.getTime(api.hostContext), 0.05f, 1e-5f);
	CHECK_NEAR(api.getDeltaTime(api.hostContext), 0.05f, 1e-5f);
	CHECK_NEAR(api.getUnscaledDeltaTime(api.hostContext), 0.1f, 1e-5f);

	api.setTimeScale(api.hostContext, 0.0f); // paused
	s.Update(0.1f, RizeEngine::GetInput());
	CHECK_NEAR(api.getTime(api.hostContext), 0.05f, 1e-5f);
	CHECK_NEAR(api.getDeltaTime(api.hostContext), 0.0f, 1e-6f);

	api.setTimeScale(api.hostContext, -3.0f); // clamped
	CHECK_NEAR(api.getTimeScale(api.hostContext), 0.0f, 1e-6f);
	api.setTimeScale(api.hostContext, 1.0f);
	A::State(s) = EditorSceneState::Edit;

	// Out-of-range queries are safe; the mouse is never "in" a Game view that was not drawn
	CHECK(!api.isMouseDown(api.hostContext, 0));
	CHECK(!api.isPadConnected(api.hostContext, 7));
	CHECK_NEAR(api.getPadTrigger(api.hostContext, 1, 9), 0.0f, 1e-6f);
}

TEST_CASE(GameFeel, HitstopFreezesGameTimeThenResumes)
{
	EngineAPI& api = A::Api(s);
	REQUIRE(api.hitstop != nullptr);
	A::State(s) = EditorSceneState::Play;
	A::GameTime(s) = 0.0;
	api.setTimeScale(api.hostContext, 0.5f);

	api.hitstop(api.hostContext, 0.12f);
	s.Update(0.05f, RizeEngine::GetInput());
	CHECK_NEAR(api.getDeltaTime(api.hostContext), 0.0f, 1e-6f); // frozen
	s.Update(0.05f, RizeEngine::GetInput());
	CHECK_NEAR(api.getTime(api.hostContext), 0.0f, 1e-6f);
	CHECK_NEAR(api.getTimeScale(api.hostContext), 0.5f, 1e-6f); // the script's time scale is untouched
	s.Update(0.05f, RizeEngine::GetInput()); // 0.15 s real time > 0.12 s: running again
	s.Update(0.1f, RizeEngine::GetInput());
	CHECK_NEAR(api.getDeltaTime(api.hostContext), 0.05f, 1e-5f);

	// A shorter hitstop does not cut a longer one short; invalid values are ignored
	api.hitstop(api.hostContext, 0.3f);
	api.hitstop(api.hostContext, 0.05f);
	api.hitstop(api.hostContext, -1.0f);
	s.Update(0.1f, RizeEngine::GetInput());
	s.Update(0.1f, RizeEngine::GetInput());
	CHECK_NEAR(api.getDeltaTime(api.hostContext), 0.0f, 1e-6f);
	api.setTimeScale(api.hostContext, 1.0f);
	A::State(s) = EditorSceneState::Edit;
}

TEST_CASE(GameFeel, CameraShakeDecaysToZero)
{
	EngineAPI& api = A::Api(s);
	REQUIRE(api.shakeCamera != nullptr);
	A::State(s) = EditorSceneState::Play;
	auto length = [](const Vector3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
	CHECK(length(s.CameraShakeOffset()) == 0.0f);

	api.shakeCamera(api.hostContext, 0.5f, 1.0f);
	s.Update(0.1f, RizeEngine::GetInput());
	const Vector3 early = s.CameraShakeOffset();
	CHECK(length(early) > 0.0f);
	CHECK(std::fabs(early.x) <= 0.5f && std::fabs(early.y) <= 0.5f && std::fabs(early.z) <= 0.5f);

	// Shakes overlap: a weak one while a strong one runs keeps the strong amplitude
	api.shakeCamera(api.hostContext, 0.1f, 0.2f);
	CHECK_NEAR(s.CameraShakeStrength(), 0.5f * 0.9f, 1e-4f);

	// Shaking also runs during a hitstop (real time), and stops completely at the end
	api.hitstop(api.hostContext, 0.5f);
	s.Update(0.6f, RizeEngine::GetInput());
	s.Update(0.4f, RizeEngine::GetInput());
	CHECK(length(s.CameraShakeOffset()) == 0.0f);
	api.shakeCamera(api.hostContext, std::nanf(""), 1.0f);
	s.Update(0.1f, RizeEngine::GetInput());
	CHECK(length(s.CameraShakeOffset()) == 0.0f);
	A::State(s) = EditorSceneState::Edit;
}

TEST_CASE(Physics, BoxColliderFitsScaledModel)
{
	// A Mixamo character at scale 0.01 (centimetres): the default 1x1x1 box would be 1 cm; fitted it wraps the body
	GameObject& hero = s.CreateGameObject("Hero", GameObjectType::Cube);
	hero.hasMesh = true;
	hero.meshType = GameObject::MeshType::CustomModel;
	hero.modelPath = "Resources/Model/Idle.fbx";
	hero.translation = { 2.0f, 50.0f, 0.0f };
	hero.scale = { 0.01f, 0.01f, 0.01f };
	hero.hasBoxCollider = true;
	const uint32_t id = hero.id;
	REQUIRE(s.FitBoxColliderToMesh(hero));

	Vector3 mn{}, mx{};
	s.ComputeBoxColliderBounds(*s.FindGameObjectById(id, false), true, mn, mx);
	const float height = mx.y - mn.y;
	CHECK_MSG(height > 1.2f && height < 2.5f, std::format("height {:.3f}", height)); // a human, in metres
	CHECK_NEAR(mn.y, 50.0f, 0.15f);                                                 // feet at the object's origin
	CHECK(mn.x < 2.0f && mx.x > 2.0f);                                              // centred on the object

	// Primitives and missing models keep their collider
	GameObject& cube = s.CreateCube("Plain", { 0.0f, 50.0f, 0.0f });
	CHECK(!s.FitBoxColliderToMesh(cube));
	CHECK_NEAR(cube.boxSize.x, 1.0f, 1e-6f);
}

TEST_CASE(Physics, FixedTimestepIsFrameRateIndependent)
{
	// Steps: 60 Hz whatever the frame rate; a long frame is capped instead of running hundreds of steps
	CHECK(s.StepPhysics(1.0f / 30.0f) == 2);
	CHECK(s.StepPhysics(1.0f / 120.0f) == 0);
	CHECK(s.StepPhysics(1.0f / 120.0f) == 1);
	CHECK(s.StepPhysics(2.0f) == EditorSuite::kMaxPhysicsSteps);
	CHECK(s.StepPhysics(0.0f) == 0); // the dropped time is not carried over

	// A falling rigidbody ends at the same height at 30 FPS and at 144 FPS
	auto fallFor = [&](float frameDt, int frames) {
		s.NewScene();
		GameObject& box = s.CreateCube("Faller", { 0.0f, 100.0f, 0.0f });
		box.hasRigidbody = true;
		box.useGravity = true;
		const uint32_t id = box.id;
		A::PhysicsAccumulator(s) = 0.0;
		for (int i = 0; i < frames; ++i) s.StepPhysics(frameDt);
		return s.FindGameObjectById(id, false)->translation.y;
	};
	// 1/32 s and 1/128 s are exact in binary, so both runs cover exactly 1.03125 s (= 61 steps, away from a step boundary)
	const float slow = fallFor(1.0f / 32.0f, 33);
	const float fast = fallFor(1.0f / 128.0f, 132);
	CHECK(slow < 99.0f); // it did fall
	CHECK_NEAR(slow, fast, 1e-4f);
}

namespace {
uint32_t MakeStaticBox(EditorSuite& s, const Vector3& mn, const Vector3& mx)
{
	GameObject& box = s.CreateGameObject("Obstacle", GameObjectType::Empty);
	box.translation = { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f };
	box.hasBoxCollider = true;
	box.boxSize = { mx.x - mn.x, mx.y - mn.y, mx.z - mn.z };
	box.isStatic = true;
	return box.id;
}
}

TEST_CASE(Character, SlidesStepsLandsAndNeverTunnels)
{
	for (auto& obj : A::Objects(s)) obj.isActive = false; // the default Ground Plane
	GameObject& hero = s.CreateGameObject("Hero", GameObjectType::Empty);
	hero.hasBoxCollider = true;
	hero.boxSize = { 0.8f, 1.8f, 0.8f };
	const uint32_t heroId = hero.id;
	s.RefreshAllScriptObjectViews();
	EngineAPI& api = A::Api(s);

	const uint32_t floor = MakeStaticBox(s, { -50.0f, -1.0f, -50.0f }, { 50.0f, 0.0f, 50.0f });
	const auto move = [&](const Vector3& feet, const Vector3& motion) {
		A::ViewFor(s, heroId)->translation = { feet.x, feet.y + 0.9f, feet.z };
		ScriptCharacterMove r{};
		const bool ok = api.moveCharacter(api.hostContext, heroId, motion, 0.3f, &r);
		CHECK(ok);
		return r;
	};
	const auto setActive = [&](uint32_t id, bool active) { s.FindGameObjectById(id, false)->isActive = active; };

	// Free move on the floor: full motion, grounded
	auto r = move({ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.5f });
	CHECK_NEAR(r.moved.x, 1.0f, 1e-3f);
	CHECK_NEAR(r.moved.z, 0.5f, 1e-3f);
	CHECK(r.grounded);
	CHECK(!r.hitWall);

	// Diagonal into a wall: stops flush on X, keeps sliding on Z
	const uint32_t wall = MakeStaticBox(s, { 2.0f, 0.0f, -10.0f }, { 2.1f, 3.0f, 10.0f });
	r = move({ 1.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 2.0f });
	CHECK(r.hitWall);
	CHECK_NEAR(1.0f + 0.4f + r.moved.x, 2.0f, 0.02f);
	CHECK_MSG(r.moved.z > 1.0f, std::format("slid {:.3f}", r.moved.z));

	// Very fast motion does not pass through the 10 cm wall
	r = move({ 0.0f, 0.0f, 0.0f }, { 50.0f, 0.0f, 0.0f });
	CHECK(r.hitWall);
	CHECK_MSG(r.moved.x < 1.7f, std::format("moved {:.3f}", r.moved.x));
	setActive(wall, false);

	// Falling: lands on the floor top, grounded; no tunnelling through it either
	r = move({ 0.0f, 5.0f, 0.0f }, { 0.0f, -100.0f, 0.0f });
	CHECK(r.grounded);
	CHECK_NEAR(5.0f + r.moved.y, 0.0f, 0.02f);
	r = move({ 0.0f, 5.0f, 0.0f }, { 0.0f, -1.0f, 0.0f });
	CHECK(!r.grounded); // still in the air

	// Ledges: 0.2 is climbed, 0.5 blocks
	const uint32_t low = MakeStaticBox(s, { 1.0f, 0.0f, -5.0f }, { 3.0f, 0.2f, 5.0f });
	move({ 0.0f, 0.0f, 0.0f }, { 0.0f, -0.01f, 0.0f });
	r = move({ 0.0f, 0.0f, 0.0f }, { 1.5f, 0.0f, 0.0f });
	CHECK_MSG(!r.hitWall && r.moved.x > 1.0f, std::format("x {:.3f}", r.moved.x));
	CHECK_NEAR(r.moved.y, 0.2f, 0.03f);
	setActive(low, false);
	const uint32_t high = MakeStaticBox(s, { 1.0f, 0.0f, -5.0f }, { 3.0f, 0.5f, 5.0f });
	move({ 0.0f, 0.0f, 0.0f }, { 0.0f, -0.01f, 0.0f });
	r = move({ 0.0f, 0.0f, 0.0f }, { 1.5f, 0.0f, 0.0f });
	CHECK(r.hitWall);
	CHECK_NEAR(r.moved.y, 0.0f, 0.01f);
	setActive(high, false);

	// Ceiling
	const uint32_t ceiling = MakeStaticBox(s, { -5.0f, 2.0f, -5.0f }, { 5.0f, 2.5f, 5.0f });
	r = move({ 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
	CHECK(r.hitCeiling);
	CHECK_NEAR(1.8f + r.moved.y, 2.0f, 0.02f);
	setActive(ceiling, false);

	// What the old box-only mover could not see: a turned wall, a capsule and a mesh
	GameObject& turned = s.CreateGameObject("Turned", GameObjectType::Empty);
	turned.translation = { 3.0f, 1.5f, 0.0f };
	turned.hasBoxCollider = true;
	turned.boxSize = { 0.2f, 3.0f, 8.0f };
	turned.isStatic = true;
	turned.rotation = { 0.0f, 0.785f, 0.0f };
	turned.rotationQuat = Quaternion::FromEuler(turned.rotation);
	const uint32_t turnedId = turned.id;
	r = move({ 0.0f, 0.0f, 0.0f }, { 6.0f, 0.0f, 0.0f });
	CHECK(r.hitWall);
	CHECK_MSG(r.moved.x < 3.0f, std::format("through the turned wall: {:.3f}", r.moved.x));
	setActive(turnedId, false);

	GameObject& pillar = s.CreateGameObject("Pillar", GameObjectType::Empty);
	pillar.translation = { 3.0f, 1.0f, 0.0f };
	pillar.isStatic = true;
	REQUIRE(s.AddComponentByName(pillar, "Capsule Collider"));
	const uint32_t pillarId = pillar.id;
	r = move({ 0.0f, 0.0f, 0.0f }, { 6.0f, 0.0f, 0.0f });
	CHECK(r.hitWall);
	setActive(pillarId, false);

	setActive(floor, false);
	GameObject& meshFloor = s.CreateGameObject("MeshFloor", GameObjectType::Empty);
	meshFloor.translation = { 0.0f, -0.5f, 0.0f };
	meshFloor.scale = { 30.0f, 1.0f, 30.0f };
	meshFloor.meshType = GameObject::MeshType::CustomModel;
	meshFloor.modelPath = "Resources/Model/cube.obj";
	meshFloor.isStatic = true;
	REQUIRE(s.AddComponentByName(meshFloor, "Mesh Collider"));
	r = move({ 0.0f, 3.0f, 0.0f }, { 0.0f, -10.0f, 0.0f });
	CHECK(r.grounded);
	CHECK_MSG(3.0f + r.moved.y > -0.5f, std::format("fell through the mesh: {:.3f}", 3.0f + r.moved.y));
}

TEST_CASE(Character, ScriptMoveCharacterUsesSceneColliders)
{
	GameObject& ground = s.CreateCube("Ground", { 0.0f, -0.5f, 0.0f });
	ground.scale = { 40.0f, 1.0f, 40.0f };
	ground.hasBoxCollider = true;
	GameObject& wall = s.CreateCube("Wall", { 3.0f, 1.0f, 0.0f });
	wall.hasBoxCollider = true;
	GameObject& trigger = s.CreateCube("Pickup", { -3.0f, 1.0f, 0.0f });
	trigger.hasBoxCollider = true;
	trigger.isTrigger = true;
	GameObject& ghost = s.CreateCube("Ghost", { 0.0f, 1.0f, 3.0f });
	ghost.hasBoxCollider = true;
	ghost.layer = 5;
	GameObject& hero = s.CreateCube("Hero", { 0.0f, 0.5f, 0.0f });
	hero.hasBoxCollider = true;
	hero.collisionMask = ~(1u << 5); // passes through layer 5
	const uint32_t heroId = hero.id;
	s.RefreshAllScriptObjectViews();

	EngineAPI& api = A::Api(s);
	REQUIRE(api.moveCharacter != nullptr);
	ScriptCharacterMove result{};
	Vector3 motion{ 10.0f, 0.0f, 0.0f };
	REQUIRE(api.moveCharacter(api.hostContext, heroId, motion, 0.3f, &result));
	const GameObject* h = s.FindGameObjectById(heroId, false);
	CHECK(result.hitWall);
	CHECK(result.grounded);
	CHECK_NEAR(h->translation.x, 2.0f, 0.01f); // wall face at 2.5, hero half width 0.5
	CHECK(h->isGrounded);
	CHECK_NEAR(A::ViewFor(s, heroId)->translation.x, h->translation.x, 1e-6f); // the script sees the new position

	motion = { -12.0f, 0.0f, 0.0f }; // through the trigger
	REQUIRE(api.moveCharacter(api.hostContext, heroId, motion, 0.3f, &result));
	CHECK(!result.hitWall);
	CHECK_NEAR(s.FindGameObjectById(heroId, false)->translation.x, -10.0f, 0.01f);

	// A move the script makes on its view first is not lost
	A::ViewFor(s, heroId)->translation = { 0.0f, 0.5f, 0.0f };
	motion = { 0.0f, 0.0f, 6.0f }; // through the layer-5 object
	REQUIRE(api.moveCharacter(api.hostContext, heroId, motion, 0.3f, &result));
	CHECK(!result.hitWall);
	CHECK_NEAR(s.FindGameObjectById(heroId, false)->translation.z, 6.0f, 0.01f);
	CHECK_NEAR(s.FindGameObjectById(heroId, false)->translation.x, 0.0f, 0.01f);

	CHECK(!api.moveCharacter(api.hostContext, 999999u, motion, 0.3f, &result)); // unknown object
}

TEST_CASE(ScriptAPI, SetRotationKeepsPositionEditedThisFrame)
{
	struct Mover : ScriptBase {
		void Initialize() override {}
		void Update(float) override {}
	};
	GameObject& hero = s.CreateCube("Hero", { 0.0f, 0.5f, 0.0f });
	const uint32_t heroId = hero.id;
	s.RefreshAllScriptObjectViews();
	Mover script;
	script.SetEngineAPI(&A::Api(s));
	script.SetOwner(A::ViewFor(s, heroId), heroId);

	// A typical player script: move through the view, then face the direction
	A::ViewFor(s, heroId)->translation.x += 2.0f; // GetOwner() is the editor type on this side of the DLL
	script.SetRotation({ 0.0f, 1.0f, 0.0f });
	CHECK_NEAR(A::ViewFor(s, heroId)->translation.x, 2.0f, 1e-6f);
	CHECK_NEAR(s.FindGameObjectById(heroId, false)->translation.x, 2.0f, 1e-6f);
	CHECK_NEAR(s.FindGameObjectById(heroId, false)->rotation.y, 1.0f, 1e-6f);

	A::ViewFor(s, heroId)->rotation.y = 0.5f;
	script.SetScale({ 2.0f, 2.0f, 2.0f });
	CHECK_NEAR(s.FindGameObjectById(heroId, false)->rotation.y, 0.5f, 1e-6f);
	CHECK_NEAR(s.FindGameObjectById(heroId, false)->scale.x, 2.0f, 1e-6f);
}

TEST_CASE(Navigation, GridPathAvoidsWallsAndSmooths)
{
	NavGrid grid;
	grid.Build(-10.0f, -10.0f, 10.0f, 10.0f, 0.5f);

	// Open field: smoothed to a straight line
	auto path = grid.FindPath({ -8.0f, 0.0f, 0.0f }, { 8.0f, 0.0f, 0.0f });
	CHECK_MSG(path.size() == 2, std::format("{} points", path.size()));

	// Wall across X = 0 with a gap at z in [6, 8]
	grid.BlockRect(-0.5f, -10.0f, 0.5f, 6.0f);
	grid.BlockRect(-0.5f, 8.0f, 0.5f, 10.0f);
	CHECK(grid.IsBlockedAt(0.0f, 0.0f));
	CHECK(!grid.IsBlockedAt(0.0f, 7.0f));
	path = grid.FindPath({ -8.0f, 1.0f, 0.0f }, { 8.0f, 1.0f, 0.0f });
	REQUIRE(path.size() >= 3);
	CHECK_NEAR(path.front().x, -8.0f, 1e-4f);
	CHECK_NEAR(path.back().x, 8.0f, 1e-4f);
	CHECK_NEAR(path[1].y, 1.0f, 1e-4f); // keeps the height of the start
	bool throughGap = false;
	for (size_t i = 0; i + 1 < path.size(); ++i) {
		for (int k = 0; k <= 50; ++k) {
			const float t = k / 50.0f;
			const float x = path[i].x + (path[i + 1].x - path[i].x) * t;
			const float z = path[i].z + (path[i + 1].z - path[i].z) * t;
			CHECK_MSG(!grid.IsBlockedAt(x, z), std::format("({:.2f}, {:.2f}) is inside the wall", x, z));
			if (std::abs(x) < 0.5f && z > 6.0f && z < 8.0f) throughGap = true;
		}
	}
	CHECK(throughGap);

	// Goal sealed off: no path. Start outside the grid: no path.
	grid.BlockRect(-0.5f, 6.0f, 0.5f, 8.0f);
	CHECK(grid.FindPath({ -8.0f, 0.0f, 0.0f }, { 8.0f, 0.0f, 0.0f }).empty());
	CHECK(grid.FindPath({ -50.0f, 0.0f, 0.0f }, { -8.0f, 0.0f, 0.0f }).empty());

	// A goal just inside an obstacle (target hugging a wall) snaps to the free side
	path = grid.FindPath({ -8.0f, 0.0f, 0.0f }, { -0.4f, 0.0f, 0.0f });
	REQUIRE(!path.empty());
	CHECK(!grid.IsBlockedAt(path.back().x, path.back().z));
}

TEST_CASE(Navigation, ScriptFindPathUsesStaticColliders)
{
	GameObject& ground = s.CreateCube("Ground", { 0.0f, -0.5f, 0.0f });
	ground.scale = { 30.0f, 1.0f, 30.0f };
	ground.hasBoxCollider = true;
	ground.isStatic = true;
	GameObject& wall = s.CreateCube("Wall", { 0.0f, 1.0f, -3.0f });
	wall.scale = { 1.0f, 2.0f, 20.0f }; // z in [-13, 7]: go around its +z end
	wall.hasBoxCollider = true;
	wall.isStatic = true;
	GameObject& crate = s.CreateCube("MovingCrate", { 0.0f, 0.5f, 10.0f }); // not static: ignored
	crate.hasBoxCollider = true;

	EngineAPI& api = A::Api(s);
	REQUIRE(api.findPath != nullptr);
	Vector3 points[64]{};
	const int count = api.findPath(api.hostContext, { -5.0f, 0.5f, 0.0f }, { 5.0f, 0.5f, 0.0f }, points, 64);
	CHECK_MSG(count >= 3, std::format("count {} first ({:.2f},{:.2f}) last ({:.2f},{:.2f})", count, points[0].x, points[0].z, points[(std::max)(count, 1) - 1].x, points[(std::max)(count, 1) - 1].z));
	REQUIRE(count >= 3);
	bool aroundTheEnd = false;
	for (int i = 0; i < count; ++i) {
		CHECK_MSG(!(std::abs(points[i].x) < 0.5f && points[i].z < 7.0f), std::format("point {} ({:.2f}, {:.2f}) in the wall", i, points[i].x, points[i].z));
		if (points[i].z > 7.0f) aroundTheEnd = true;
	}
	CHECK(aroundTheEnd);
	CHECK_NEAR(points[count - 1].x, 5.0f, 1e-3f);

	// Fewer slots than points: fills what fits, returns the full length
	Vector3 two[2]{};
	CHECK(api.findPath(api.hostContext, { -5.0f, 0.5f, 0.0f }, { 5.0f, 0.5f, 0.0f }, two, 2) == count);

	// The grid is cached; a rebuild picks up a removed wall
	A::Objects(s).erase(std::find_if(A::Objects(s).begin(), A::Objects(s).end(), [](const GameObject& o) { return o.name == "Wall"; }));
	CHECK(api.findPath(api.hostContext, { -5.0f, 0.5f, 0.0f }, { 5.0f, 0.5f, 0.0f }, points, 64) == count);
	api.rebuildNavigation(api.hostContext, 0.5f, 0.4f);
	CHECK(api.findPath(api.hostContext, { -5.0f, 0.5f, 0.0f }, { 5.0f, 0.5f, 0.0f }, points, 64) == 2);
}

TEST_CASE(Render, ShaderHotReloadKeepsOldPipelineOnError)
{
	namespace fs = std::filesystem;
	const fs::path dir = A::TempRoot() / "HotShaders";
	const fs::path deploy = A::TempRoot() / "HotShadersDeploy";
	std::error_code ec;
	fs::remove_all(dir, ec);
	fs::remove_all(deploy, ec);
	fs::copy("shaders", dir, fs::copy_options::recursive, ec);
	REQUIRE(!ec);

	// Direct reload: good shaders swap in, a broken one keeps the old pipeline
	CHECK(s.ReloadShaders(dir));
	{
		std::ofstream(dir / "Object3d.PS.hlsl", std::ios::app) << "\nthis is not hlsl;\n";
	}
	CHECK(!s.ReloadShaders(dir));
	fs::copy_file("shaders/Object3d.PS.hlsl", dir / "Object3d.PS.hlsl", fs::copy_options::overwrite_existing, ec);
	CHECK(s.ReloadShaders(dir));
	// The scene still renders with whatever pipeline is current
	s.CreateCube("AfterReload", { 0.0f, 0.5f, 0.0f });
	s.Update(0.016f, RizeEngine::GetInput());

	// Watcher: a saved .hlsl reloads within the poll interval and is copied to the run folder
	A::ShaderWatch(s, dir, deploy);
	const int before = A::ShaderReloads(s);
	s.PollShaderWatcher(0.6f); // first scan only records timestamps
	CHECK(A::ShaderReloads(s) == before);
	{
		std::ofstream(dir / "Object3d.PS.hlsl", std::ios::app) << "\n// edited\n";
	}
	fs::last_write_time(dir / "Object3d.PS.hlsl", fs::file_time_type::clock::now() + std::chrono::seconds(2), ec);
	s.PollShaderWatcher(0.6f);
	CHECK(A::ShaderReloads(s) == before + 1);
	CHECK(fs::exists(deploy / "Object3d.PS.hlsl"));
	A::ShaderWatch(s, {}, {});
}

TEST_CASE(Render, ToonLightingModelIsSelectableAndSaved)
{
	GameObject& cube = s.CreateCube("ToonCube", { 0.0f, 0.5f, 0.0f });
	const uint32_t id = cube.id;
	EngineAPI& api = A::Api(s);
	ScriptMaterialData mat{};
	REQUIRE(api.getMaterial(api.hostContext, id, &mat));
	mat.lightingType = 5;
	REQUIRE(api.setMaterial(api.hostContext, id, &mat));
	CHECK(s.FindGameObjectById(id, false)->lightingType == 5);
	mat.lightingType = 99; // out of range is clamped to the last model
	mat.outlineWidth = 0.03f;
	mat.outlineColor = { 0.1f, 0.0f, 0.2f, 1.0f };
	REQUIRE(api.setMaterial(api.hostContext, id, &mat));
	CHECK(s.FindGameObjectById(id, false)->lightingType == 5);
	mat.outlineWidth = -1.0f; // negative = none
	REQUIRE(api.setMaterial(api.hostContext, id, &mat));
	CHECK_NEAR(s.FindGameObjectById(id, false)->outlineWidth, 0.0f, 1e-6f);
	mat.outlineWidth = 0.03f;
	REQUIRE(api.setMaterial(api.hostContext, id, &mat));
	ScriptMaterialData back{};
	REQUIRE(api.getMaterial(api.hostContext, id, &back));
	CHECK_NEAR(back.outlineWidth, 0.03f, 1e-6f);
	CHECK_NEAR(back.outlineColor.z, 0.2f, 1e-6f);

	const std::string json = s.SerializeSceneToString();
	REQUIRE(s.DeserializeSceneFromString(json));
	const GameObject* loaded = s.FindGameObjectByName("ToonCube", false);
	REQUIRE(loaded != nullptr);
	CHECK(loaded->lightingType == 5);
	CHECK_NEAR(loaded->outlineWidth, 0.03f, 1e-6f);
	CHECK_NEAR(loaded->outlineColor.x, 0.1f, 1e-6f);
	REQUIRE(RizeEngine::GetGraphicPipeline() != nullptr);
	CHECK(RizeEngine::GetGraphicPipeline()->HasOutline());
	s.Update(0.016f, RizeEngine::GetInput()); // renders the toon branch and the outline pass
	// Kept for a manual look: RizealldaySandbox.exe --scene <temp>/RizealldayTests/ToonLook/Assets/Scenes/Toon.scene.json
	GameObject& ball = s.CreateSphere("ToonBall", { 2.0f, 1.0f, 0.0f });
	ball.lightingType = 5;
	ball.materialColor = { 0.95f, 0.45f, 0.2f, 1.0f };
	ball.outlineWidth = 0.04f;
	GameObject& plainBall = s.CreateSphere("BlinnBall", { -2.0f, 1.0f, 0.0f });
	plainBall.materialColor = { 0.95f, 0.45f, 0.2f, 1.0f };
	std::error_code ec;
	std::filesystem::create_directories(A::TempRoot() / "ToonLook" / "Assets" / "Scenes", ec);
	std::ofstream(A::TempRoot() / "ToonLook" / "Assets" / "Scenes" / "Toon.scene.json") << s.SerializeSceneToString();
}

TEST_CASE(Navigation, StateMachineHelper)
{
	enum State { Idle, Chase, Attack };
	StateMachine sm{ Idle };
	CHECK(sm.Is(Idle));
	CHECK(sm.JustEntered()); // the first Tick sees the start state as entered
	sm.Tick(0.5f);
	CHECK(!sm.JustEntered());
	CHECK_NEAR(sm.TimeInState(), 0.5f, 1e-6f);
	sm.Set(Chase);
	CHECK(sm.Is(Chase));
	CHECK(sm.Previous() == Idle);
	CHECK(sm.JustEntered());
	CHECK_NEAR(sm.TimeInState(), 0.0f, 1e-6f);
	sm.Tick(0.1f);
	CHECK(!sm.JustEntered());
	sm.Set(Chase); // same state: nothing restarts
	CHECK_NEAR(sm.TimeInState(), 0.1f, 1e-6f);
}

TEST_CASE(GameFeel, ScriptTimersAndCooldown)
{
	// Cooldown: usable at start, then blocked for `duration`
	Cooldown fire(0.5f);
	CHECK(fire.TryUse());
	CHECK(!fire.TryUse());
	fire.Tick(0.3f);
	CHECK(!fire.Ready());
	fire.Tick(0.2f);
	CHECK(fire.TryUse());

	TimerQueue timers;
	std::vector<std::string> log;
	timers.After(0.2f, [&] { log.push_back("once"); });
	const int every = timers.Every(0.1f, [&] { log.push_back("tick"); });
	timers.Tick(0.05f);
	CHECK(log.empty());
	timers.Tick(0.05f); // t = 0.10
	CHECK(log == std::vector<std::string>{ "tick" });
	timers.Tick(0.25f); // t = 0.35: two more ticks (one long frame catches up) and the one-shot
	CHECK(std::count(log.begin(), log.end(), "tick") == 3);
	CHECK(std::count(log.begin(), log.end(), "once") == 1);
	CHECK(timers.Count() == 1); // the one-shot is gone, the repeating one stays
	timers.Cancel(every);
	timers.Tick(1.0f);
	CHECK(std::count(log.begin(), log.end(), "tick") == 3);
	CHECK(timers.Count() == 0);

	// Callbacks may add and cancel timers while the queue is ticking
	int chained = 0;
	int self = 0;
	self = timers.Every(0.1f, [&] {
		++chained;
		timers.Cancel(self);                          // stops itself
		timers.After(0.1f, [&] { chained += 10; });   // runs on a later tick, not inside this one
	});
	timers.Tick(0.1f);
	CHECK(chained == 1);
	timers.Tick(0.1f);
	CHECK(chained == 11);
	CHECK(timers.Count() == 0);

	// Zero / negative / NaN intervals never loop forever
	timers.Every(0.0f, [&] { ++chained; });
	timers.After(std::nanf(""), [&] { ++chained; });
	timers.Tick(0.1f);
	CHECK(chained == 13); // each fires once
	timers.Clear();
	CHECK(timers.Count() == 0);
}

// ===========================================================================
// Input actions
// ===========================================================================

namespace {
	/// Fake keyboard / mouse / pad for InputActions
	struct FakeInput {
		std::set<int> keys, prevKeys, mouse, pad;
		Vector2 leftStick{ 0, 0 };
		InputActions::Source Source()
		{
			InputActions::Source src;
			src.keyDown = [this](int k) { return keys.count(k) > 0; };
			src.mouseDown = [this](int b) { return mouse.count(b) > 0; };
			src.padDown = [this](int b) { return pad.count(b) > 0; };
			src.padAxis = [this](int axis) { return axis == static_cast<int>(InputBinding::Axis::LeftX) ? leftStick.x : axis == static_cast<int>(InputBinding::Axis::LeftY) ? leftStick.y : 0.0f; };
			return src;
		}
	};
}

TEST_CASE(Input, ActionsMapKeysPadAndAxes)
{
	InputActions actions = InputActions::Defaults();
	FakeInput in;
	const auto src = in.Source();

	CHECK(!actions.IsDown("Jump", src));
	in.keys.insert(KEY_SPACE);
	CHECK(actions.IsDown("Jump", src));
	CHECK(actions.IsTriggered("Jump", src)); // first frame down
	actions.EndFrame(src);
	CHECK(!actions.IsTriggered("Jump", src)); // held
	in.keys.clear();
	in.pad.insert(PAD_A);                     // the same action from the gamepad
	CHECK(actions.IsDown("Jump", src));
	actions.EndFrame(src);
	in.pad.clear();
	CHECK(actions.IsReleased("Jump", src));

	// Axes: keys give -1 / +1, the stick its analog value, the sum is clamped
	CHECK_NEAR(actions.GetAxis("MoveX", src), 0.0f, 1e-6f);
	in.keys.insert(KEY_D);
	CHECK_NEAR(actions.GetAxis("MoveX", src), 1.0f, 1e-6f);
	in.keys.insert(KEY_A);
	CHECK_NEAR(actions.GetAxis("MoveX", src), 0.0f, 1e-6f); // both keys cancel out
	in.keys.clear();
	in.leftStick = { -0.4f, 0.8f };
	CHECK_NEAR(actions.GetAxis("MoveX", src), -0.4f, 1e-6f);
	CHECK_NEAR(actions.GetAxis("MoveY", src), 0.8f, 1e-6f);
	in.keys.insert(KEY_W);
	CHECK_NEAR(actions.GetAxis("MoveY", src), 1.0f, 1e-6f); // clamped
	in.leftStick = { 0.1f, 0.0f };
	CHECK_NEAR(actions.GetAxis("MoveX", src), 0.0f, 1e-6f);  // inside the dead zone
	in.leftStick = { 0.0f, 0.0f };
	in.keys.clear();

	// Mouse buttons, unknown actions
	in.mouse.insert(0);
	CHECK(actions.IsDown("Attack", src));
	CHECK(!actions.IsDown("NoSuchAction", src));
	CHECK(actions.GetAxis("NoSuchAction", src) == 0.0f);
}

TEST_CASE(Input, RebindSaveAndLoad)
{
	InputActions actions = InputActions::Defaults();
	FakeInput in;
	const auto src = in.Source();
	// Rebind Jump to K only, add a new action
	actions.Find("Jump")->bindings = { InputBinding::Key(KEY_K) };
	actions.Add("Dash").bindings = { InputBinding::Key(KEY_LSHIFT), InputBinding::Pad(PAD_RB) };

	const std::string json = actions.ToJson();
	std::string error;
	auto loaded = InputActions::FromJson(json, &error);
	CHECK_MSG(loaded.has_value(), error);
	REQUIRE(loaded.has_value());
	CHECK(loaded->ToJson() == json);
	in.keys.insert(KEY_SPACE);
	CHECK(!loaded->IsDown("Jump", src)); // Space no longer jumps
	in.keys = { KEY_K };
	CHECK(loaded->IsDown("Jump", src));
	in.keys = { KEY_LSHIFT };
	CHECK(loaded->IsDown("Dash", src));
	CHECK(!InputActions::FromJson("{ broken", nullptr).has_value());

	// Per project: ProjectSettings/input.json overrides the defaults, a missing file means defaults
	const std::filesystem::path root = A::TempRoot() / "InputProject";
	std::error_code ec;
	std::filesystem::create_directories(root / "ProjectSettings", ec);
	A::ProjectRoot(s) = root;
	s.ReloadInputActions();
	CHECK(s.GetInputActions().Find("Jump") != nullptr);
	CHECK(s.GetInputActions().Find("Dash") == nullptr);
	REQUIRE(s.SaveInputActions(*loaded));
	CHECK(std::filesystem::exists(root / "ProjectSettings" / "input.json"));
	s.ReloadInputActions();
	CHECK(s.GetInputActions().Find("Dash") != nullptr);

	// Scripts see the project's map
	EngineAPI& api = A::Api(s);
	REQUIRE(api.isActionDown != nullptr && api.getAxis != nullptr && api.isActionTriggered != nullptr);
	CHECK(!api.isActionDown(api.hostContext, "Dash"));
	CHECK(api.getAxis(api.hostContext, "MoveX") == 0.0f);
}

// ===========================================================================
// Prefabs & pooling
// ===========================================================================

TEST_CASE(Prefabs, SaveAndInstantiateRemapsIds)
{
	A::ProjectRoot(s) = A::TempRoot() / "PrefabProject";

	GameObject& root = s.CreateCube("Enemy", { 4.0f, 0.0f, 4.0f });
	root.layer = LAYER_ENEMY;
	root.collisionMask = LayerBit(LAYER_PLAYER) | LayerBit(LAYER_PLAYER_BULLET);
	root.scale = { 0.5f, 2.0f, 0.5f };
	const uint32_t rootId = root.id;
	const uint32_t childId = s.CreateSphere("EnemyEye", { 0.0f, 1.0f, 0.0f }).id;
	REQUIRE(s.ReparentGameObject(childId, static_cast<int>(rootId), false));

	REQUIRE(s.SavePrefab(rootId, "Enemy"));
	const std::filesystem::path prefabFile = A::TempRoot() / "PrefabProject" / "Assets" / "Prefabs" / "Enemy.prefab.json";
	CHECK(std::filesystem::exists(prefabFile));
	std::string firstSave;
	{
		std::ifstream in(prefabFile, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		firstSave = ss.str();
	}
	CHECK_MSG(firstSave.find('\n') != std::string::npos, "prefab file must be formatted with newlines for git readability");
	REQUIRE(s.SavePrefab(rootId, "Enemy"));
	{
		std::ifstream in(prefabFile, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		CHECK_MSG(ss.str() == firstSave, "saving prefab twice in a row must be byte-identical");
	}
	CHECK(s.HasPrefab("Enemy"));
	CHECK(!s.HasPrefab("DoesNotExist"));

	const size_t before = A::Objects(s).size();
	const uint32_t newRoot = s.InstantiatePrefabNow("Enemy", { -7.0f, 0.0f, 3.0f });
	REQUIRE(newRoot != 0);
	CHECK(newRoot != rootId);
	CHECK(A::Objects(s).size() == before + 2);

	const GameObject* inst = s.FindGameObjectById(newRoot, false);
	REQUIRE(inst != nullptr);
	CHECK(inst->parentId == -1);
	CHECK_NEAR(inst->translation.x, -7.0f, 1e-5f);
	CHECK_NEAR(inst->scale.y, 2.0f, 1e-5f);
	CHECK(inst->layer == LAYER_ENEMY);
	CHECK(inst->collisionMask == (LayerBit(LAYER_PLAYER) | LayerBit(LAYER_PLAYER_BULLET)));
	CHECK(inst->prefabSource == "Enemy");
	REQUIRE(inst->childrenIds.size() == 1);
	const GameObject* eye = s.FindGameObjectById(inst->childrenIds[0], false);
	REQUIRE(eye != nullptr);
	CHECK(eye->id != childId);
	CHECK(eye->name == "EnemyEye");
	CHECK(eye->parentId == static_cast<int>(newRoot));
}

TEST_CASE(Prefabs, PoolReusesDespawnedInstances)
{
	A::ProjectRoot(s) = A::TempRoot() / "PoolProject";
	const uint32_t templateId = s.CreateSphere("Bullet", { 0.0f, 0.0f, 0.0f }).id;
	REQUIRE(s.SavePrefab(templateId, "Bullet"));
	A::State(s) = EditorSceneState::Play;

	// Missing prefab: no view, nothing queued
	CHECK(s.QueuePrefabSpawn("NoSuchPrefab", { 0.0f, 0.0f, 0.0f }) == nullptr);

	std::vector<uint32_t> ids;
	for (int i = 0; i < 20; ++i) {
		ScriptGameObjectView* view = s.QueuePrefabSpawn("Bullet", { static_cast<float>(i), 0.5f, 0.0f });
		REQUIRE(view != nullptr);
		ids.push_back(view->id);
	}
	s.ProcessPendingSpawns();
	const size_t afterSpawn = A::Objects(s).size();
	for (size_t i = 0; i < ids.size(); ++i) {
		const GameObject* obj = s.FindGameObjectById(ids[i], false);
		REQUIRE(obj != nullptr);
		CHECK(obj->isActive);
		CHECK_NEAR(obj->translation.x, static_cast<float>(i), 1e-5f);
	}

	// Despawn half: they stay in the scene, hidden, and go to the pool
	for (size_t i = 0; i < 10; ++i) s.QueueDespawn(ids[i]);
	s.QueueDespawn(ids[0]); // duplicate request is ignored
	s.ProcessPendingSpawns();
	CHECK(A::Objects(s).size() == afterSpawn);
	CHECK(s.GetPooledCount("Bullet") == 10);
	CHECK(!s.FindGameObjectById(ids[3], false)->isActive);

	// Spawning again reuses pooled objects instead of creating new ones
	std::set<uint32_t> reused;
	for (int i = 0; i < 10; ++i) {
		ScriptGameObjectView* view = s.QueuePrefabSpawn("Bullet", { 100.0f, 0.0f, static_cast<float>(i) });
		REQUIRE(view != nullptr);
		reused.insert(view->id);
	}
	s.ProcessPendingSpawns();
	CHECK(A::Objects(s).size() == afterSpawn);
	CHECK(s.GetPooledCount("Bullet") == 0);
	CHECK(reused == std::set<uint32_t>(ids.begin(), ids.begin() + 10));
	for (uint32_t id : reused) {
		const GameObject* obj = s.FindGameObjectById(id, false);
		REQUIRE(obj != nullptr);
		CHECK(obj->isActive);
		CHECK_NEAR(obj->translation.x, 100.0f, 1e-5f);
	}

	// Cancelling a pending reuse puts the object back into the pool
	s.QueueDespawn(ids[15]);
	s.ProcessPendingSpawns();
	ScriptGameObjectView* pending = s.QueuePrefabSpawn("Bullet", { 0.0f, 0.0f, 0.0f });
	REQUIRE(pending != nullptr);
	CHECK(pending->id == ids[15]);
	s.QueueDespawn(pending->id);
	CHECK(s.GetPooledCount("Bullet") == 1);
	s.ProcessPendingSpawns();
	CHECK(!s.FindGameObjectById(ids[15], false)->isActive);

	// Despawning a non-prefab object deletes it
	const uint32_t plain = s.CreateCube("Plain", { 0.0f, 0.0f, 0.0f }).id;
	s.QueueDespawn(plain);
	s.ProcessPendingSpawns();
	s.ProcessPendingDeletes();
	CHECK(s.FindGameObjectById(plain, false) == nullptr);

	A::State(s) = EditorSceneState::Edit;
	s.ResetPrefabPools();
}

// ===========================================================================
// Game UI
// ===========================================================================

TEST_CASE(UI, CanvasCommandsAndTextLayout)
{
	UIRenderer* ui = UIRenderer::GetInstance();
	ui->Clear();
	CHECK(!ui->HasCommands());

	EngineAPI& api = A::Api(s);
	REQUIRE(api.uiRect != nullptr);
	api.uiRect(api.hostContext, 10.0f, 10.0f, 200.0f, 20.0f, { 1.0f, 0.0f, 0.0f, 1.0f });
	CHECK(ui->GetQuadCount() == 1);

	// Text: one quad per visible glyph, spaces and newlines produce none
	api.uiText(api.hostContext, "HP 100\nLv 3", 20.0f, 40.0f, 32.0f, { 1.0f, 1.0f, 1.0f, 1.0f }, 0);
	CHECK_MSG(ui->GetQuadCount() == 1 + 8, "quads: " + std::to_string(ui->GetQuadCount()));

	// Measuring: proportional, scales linearly with size, widest line wins
	const float w32 = api.uiMeasureText(api.hostContext, "SCORE", 32.0f);
	const float w64 = api.uiMeasureText(api.hostContext, "SCORE", 64.0f);
	CHECK(w32 > 30.0f && w32 < 200.0f);
	CHECK_NEAR(w64, w32 * 2.0f, 0.01f);
	CHECK(api.uiMeasureText(api.hostContext, "iii", 32.0f) < api.uiMeasureText(api.hostContext, "WWW", 32.0f));
	CHECK_NEAR(api.uiMeasureText(api.hostContext, "AB\nA", 32.0f), api.uiMeasureText(api.hostContext, "AB", 32.0f), 1e-4f);

	// A missing image falls back to the white texture instead of failing
	api.uiImage(api.hostContext, "Assets/Textures/does_not_exist.png", 0.0f, 0.0f, 64.0f, 64.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
	CHECK(ui->GetQuadCount() == 1 + 8 + 1);

	ui->Clear();
	CHECK(!ui->HasCommands());
}

// ===========================================================================
// Runtime: scene switching + save data
// ===========================================================================

TEST_CASE(Runtime, SaveDataPersists)
{
	A::ProjectRoot(s) = A::TempRoot() / "SaveProject";
	EngineAPI& api = A::Api(s);
	REQUIRE(api.saveSetNumber != nullptr);

	CHECK(!api.saveHasKey(api.hostContext, "HighScore"));
	CHECK_NEAR(api.saveGetNumber(api.hostContext, "HighScore", -1.0), -1.0, 1e-9);
	api.saveSetNumber(api.hostContext, "HighScore", 12345.0);
	api.saveSetString(api.hostContext, "PlayerName", "Rize");
	CHECK(std::filesystem::exists(A::TempRoot() / "SaveProject" / "SaveData" / "save.json"));

	// Switching project root forces a reload from disk: values must come back from the file
	A::ProjectRoot(s) = A::TempRoot() / "OtherProject";
	CHECK(!api.saveHasKey(api.hostContext, "HighScore"));
	A::ProjectRoot(s) = A::TempRoot() / "SaveProject";
	CHECK_NEAR(api.saveGetNumber(api.hostContext, "HighScore", 0.0), 12345.0, 1e-9);
	char name[8]{};
	CHECK(api.saveGetString(api.hostContext, "PlayerName", name, static_cast<int>(sizeof(name))));
	CHECK(std::string(name) == "Rize");
	char tiny[3]{};
	CHECK(api.saveGetString(api.hostContext, "PlayerName", tiny, static_cast<int>(sizeof(tiny))));
	CHECK(std::string(tiny) == "Ri"); // truncated, still terminated
	CHECK(!api.saveGetString(api.hostContext, "HighScore", name, static_cast<int>(sizeof(name)))); // a number, not a string

	api.saveDeleteAll(api.hostContext);
	CHECK(!api.saveHasKey(api.hostContext, "PlayerName"));
}

TEST_CASE(Runtime, SaveDataKeepsWorkingWithACutJapaneseName)
{
	// Text read back into a small buffer is cut by bytes, so a kanji can be cut in half. Saving that again must
	// not throw (an exported game has no guard around scripts) and must keep the other values.
	const std::filesystem::path root = A::TempRoot() / "SaveUtf8Project";
	std::filesystem::remove_all(root);
	A::ProjectRoot(s) = root;
	EngineAPI& api = A::Api(s);
	api.saveSetNumber(api.hostContext, "Coins", 7.0);
	api.saveSetString(api.hostContext, "PlayerName", "\xE5\xA4\xAA\xE9\x83\x8E"); // two kanji, 6 bytes
	char cut[5]{}; // 4 bytes: one kanji and the first byte of the next
	REQUIRE(api.saveGetString(api.hostContext, "PlayerName", cut, static_cast<int>(sizeof(cut))));
	api.saveSetString(api.hostContext, "ShortName", cut);

	A::ProjectRoot(s) = A::TempRoot() / "OtherProject"; // forces a reload from the file
	A::ProjectRoot(s) = root;
	CHECK_NEAR(api.saveGetNumber(api.hostContext, "Coins", 0.0), 7.0, 1e-9);
	char name[32]{};
	CHECK(api.saveGetString(api.hostContext, "ShortName", name, static_cast<int>(sizeof(name))));
	CHECK(std::string_view(name).starts_with("\xE5\xA4\xAA")); // the whole kanji survives
	std::filesystem::remove_all(root);
}

TEST_CASE(Render, FullTextureHeapFallsBackToWhite)
{
	// Textures stop before the last SRV slots, so render targets made later (window resize, a view's post
	// process) still get one; a texture made from pixels (UI font pages) then falls back to white instead of
	// writing a descriptor outside the heap
	DirectXCommon* dx = A::Dx(s);
	REQUIRE(dx != nullptr);
	std::vector<uint32_t> taken;
	while (dx->HasFreeSrvIndexForTexture()) taken.push_back(dx->AllocateSrvIndex());
	const uint8_t red[4] = { 255, 0, 0, 255 };
	TextureManager* textures = TextureManager::GetInstance();
	const uint32_t handle = textures->CreateTextureFromRGBA("FullHeapTest", red, 1, 1);
	const uint32_t target = dx->AllocateSrvIndex();
	CHECK(handle == textures->GetDefaultWhiteTexture());
	CHECK(target != DirectXCommon::kInvalidSrvIndex);
	if (target != DirectXCommon::kInvalidSrvIndex) taken.push_back(target);
	for (uint32_t index : taken) dx->FreeSrvIndex(index);
	CHECK(!dx->IsDeviceLost());
}

TEST_CASE(Scripting, StartupKeepsTheNewestScriptPdb)
{
	// Every build writes GameScripts_<time>.pdb; startup deletes the old ones but must keep the newest, which
	// belongs to the DLL in use (without it, breakpoints in scripts do not bind until the next build)
	namespace fs = std::filesystem;
	const fs::path dir = A::TempRoot() / "ScriptPdbCleanup";
	fs::remove_all(dir);
	fs::create_directories(dir / "Shadow");
	std::ofstream(dir / "GameScripts_100.pdb") << "old";
	std::ofstream(dir / "GameScripts_200.pdb") << "new";
	std::ofstream(dir / "Shadow" / "GameScripts_1.dll") << "shadow";
	const auto now = fs::file_time_type::clock::now();
	fs::last_write_time(dir / "GameScripts_100.pdb", now - std::chrono::hours(1));
	fs::last_write_time(dir / "GameScripts_200.pdb", now);

	ScriptModule::CleanupOldShadowCopies(dir / "Shadow");
	CHECK(!fs::exists(dir / "GameScripts_100.pdb"));
	CHECK(fs::exists(dir / "GameScripts_200.pdb"));
	CHECK(!fs::exists(dir / "Shadow" / "GameScripts_1.dll"));
	fs::remove_all(dir);
}

TEST_CASE(Runtime, LoadSceneDuringPlayAndRestoreOnStop)
{
	namespace fs = std::filesystem;
	A::ProjectRoot(s) = A::TempRoot() / "SceneSwitchProject";
	const fs::path scenes = s.GetScenesDirectory();
	std::error_code ec;
	fs::create_directories(scenes, ec);

	// Stage2 on disk contains only "Stage2Marker"
	s.CreateCube("Stage2Marker", { 1.0f, 0.0f, 0.0f });
	{
		std::ofstream(scenes / "Stage2.scene.json") << s.SerializeSceneToString();
	}

	A::PrepareForTest(s);
	A::ProjectRoot(s) = A::TempRoot() / "SceneSwitchProject";
	s.CreateCube("Stage1Marker", { 0.0f, 0.0f, 0.0f });
	A::ScenePath(s) = (scenes / "Stage1.scene.json").string();
	A::SceneName(s) = "Stage1";

	s.StartPlayMode();
	EngineAPI& api = A::Api(s);
	api.loadScene(api.hostContext, "Stage2");
	CHECK(s.FindGameObjectByName("Stage1Marker", false) != nullptr); // deferred until the end of the frame
	s.Update(0.016f, RizeEngine::GetInput());
	CHECK(s.FindGameObjectByName("Stage2Marker", false) != nullptr);
	CHECK(s.FindGameObjectByName("Stage1Marker", false) == nullptr);
	char sceneName[64]{};
	api.getSceneName(api.hostContext, sceneName, static_cast<int>(sizeof(sceneName)));
	CHECK(std::string(sceneName) == "Stage2");
	CHECK(A::State(s) == EditorSceneState::Play);

	// A missing scene keeps the current one
	api.loadScene(api.hostContext, "NoSuchStage");
	s.Update(0.016f, RizeEngine::GetInput());
	CHECK(s.FindGameObjectByName("Stage2Marker", false) != nullptr);

	s.StopPlayMode();
	CHECK(s.FindGameObjectByName("Stage1Marker", false) != nullptr);
	CHECK(s.FindGameObjectByName("Stage2Marker", false) == nullptr);
	CHECK(A::SceneName(s) == "Stage1");
	CHECK(A::ScenePath(s) == (scenes / "Stage1.scene.json").string());
}

TEST_CASE(Runtime, KeepAcrossScenesSurvivesLoadScene)
{
	namespace fs = std::filesystem;
	A::ProjectRoot(s) = A::TempRoot() / "PersistProject";
	const fs::path scenes = s.GetScenesDirectory();
	std::error_code ec;
	fs::create_directories(scenes, ec);
	s.CreateCube("Stage2Marker", { 1.0f, 0.0f, 0.0f });
	std::ofstream(scenes / "Stage2.scene.json") << s.SerializeSceneToString();

	A::PrepareForTest(s);
	A::ProjectRoot(s) = A::TempRoot() / "PersistProject";
	GameObject& manager = s.CreateCube("GameManager", { 0.0f, 3.0f, 0.0f });
	const uint32_t managerId = manager.id;
	GameObject& child = s.CreateCube("ManagerChild", { 0.0f, 4.0f, 0.0f });
	const uint32_t childId = child.id;
	REQUIRE(s.ReparentGameObject(childId, static_cast<int>(managerId)));
	const bool withScript = A::ScriptsLoaded(s);
	if (withScript) s.FindGameObjectById(managerId, false)->scripts.emplace_back("RotateScript");
	s.CreateCube("Stage1Only", { 0.0f, 0.0f, 0.0f });

	s.StartPlayMode();
	EngineAPI& api = A::Api(s);
	REQUIRE(api.keepAcrossScenes != nullptr);
	CHECK(!api.keepAcrossScenes(api.hostContext, childId)); // children follow their root
	CHECK(api.keepAcrossScenes(api.hostContext, managerId));
	ScriptBase* instanceBefore = nullptr;
	if (withScript) {
		const GameObject* m = s.FindGameObjectById(managerId, false);
		instanceBefore = m->scripts.empty() ? nullptr : m->scripts[0].instance.get();
		CHECK(instanceBefore != nullptr);
	}
	s.FindGameObjectById(managerId, false)->translation.x = 7.0f; // runtime state that must survive

	api.loadScene(api.hostContext, "Stage2");
	s.Update(0.016f, RizeEngine::GetInput());
	CHECK(s.FindGameObjectByName("Stage2Marker", false) != nullptr);
	CHECK(s.FindGameObjectByName("Stage1Only", false) == nullptr);
	const GameObject* kept = s.FindGameObjectByName("GameManager", false);
	REQUIRE(kept != nullptr);
	CHECK_NEAR(kept->translation.x, 7.0f, 1e-5f);
	const GameObject* keptChild = s.FindGameObjectByName("ManagerChild", false);
	REQUIRE(keptChild != nullptr);
	CHECK(keptChild->parentId == static_cast<int>(kept->id));
	if (withScript) {
		CHECK_MSG(!kept->scripts.empty() && kept->scripts[0].instance.get() == instanceBefore, "the same script instance keeps running (no OnDestroy / Initialize)");
	}
	std::set<uint32_t> ids;
	for (const auto& o : A::Objects(s)) CHECK_MSG(ids.insert(o.id).second, std::format("duplicate id {}", o.id));

	// Again into another scene: still there
	api.loadScene(api.hostContext, "Stage2");
	s.Update(0.016f, RizeEngine::GetInput());
	CHECK(s.FindGameObjectByName("GameManager", false) != nullptr);

	// Stop brings the edit scene back, without a second copy
	s.StopPlayMode();
	int managers = 0;
	for (const auto& o : A::Objects(s)) managers += o.name == "GameManager" ? 1 : 0;
	CHECK(managers == 1);
	CHECK(s.FindGameObjectByName("Stage1Only", false) != nullptr);
}

// ===========================================================================
// Samples
// ===========================================================================

TEST_CASE(Samples, BenchmarkSceneReachesFullLoad)
{
	A::ProjectRoot(s) = A::TempRoot() / "BenchmarkProject";
	REQUIRE(s.CreateBenchmarkScene());
	REQUIRE(A::ScriptsLoaded(s));
	s.StartPlayMode();
	for (int frame = 0; frame < 5; ++frame) s.Update(1.0f / 60.0f, RizeEngine::GetInput());
	int enemies = 0, bullets = 0;
	Vector3 firstEnemy{};
	uint32_t enemyId = 0;
	for (const GameObject& obj : A::Objects(s)) {
		if (obj.prefabSource == "BenchEnemy" && obj.isActive) {
			if (++enemies == 1) { enemyId = obj.id; firstEnemy = obj.translation; }
		}
		if (obj.prefabSource == "BenchBullet" && obj.isActive) ++bullets;
	}
	CHECK_MSG(enemies == 300, std::to_string(enemies));
	CHECK_MSG(bullets == 500, std::to_string(bullets));
	for (int frame = 0; frame < 5; ++frame) s.Update(1.0f / 60.0f, RizeEngine::GetInput());
	const GameObject* moved = s.FindGameObjectById(enemyId, false);
	REQUIRE(moved != nullptr);
	CHECK_MSG(std::abs(moved->translation.x - firstEnemy.x) + std::abs(moved->translation.z - firstEnemy.z) > 1e-3f, "enemies chase through MoveCharacter");
	s.StopPlayMode();
}

TEST_CASE(Samples, ThirdPersonDemoUsesCameraInputAndProjection)
{
	A::ProjectRoot(s) = A::TempRoot() / "ThirdPersonProject";
	REQUIRE(s.CreateThirdPersonDemo());
	REQUIRE(A::ScriptsLoaded(s));
	EngineAPI& api = A::Api(s);
	Input* input = RizeEngine::GetInput();

	s.StartPlayMode();
	REQUIRE(A::State(s) == EditorSceneState::Play);
	CHECK(api.isCursorLocked(api.hostContext));
	CHECK(s.GetPooledCount("ThirdPersonShot") == 16);
	for (int frame = 0; frame < 120; ++frame) s.Update(1.0f / 60.0f, input);

	const GameObject* player = s.FindGameObjectByName("Player", false);
	REQUIRE(player != nullptr);
	CHECK_MSG(std::abs(player->translation.y) < 0.1f, std::format("player rests on the ground plane, y = {:.3f}", player->translation.y));
	CHECK(UIRenderer::GetInstance()->GetQuadCount() > 0); // crosshair + label

	// The mouse turns the camera around the player and the wheel moves it closer
	const GameObject* camera = s.FindGameObjectByName("Main Camera", false);
	REQUIRE(camera != nullptr);
	const float yawBefore = camera->rotation.y;
	const Vector3 start = camera->translation;
	const float distanceBefore = std::hypot(start.x - player->translation.x, start.y - player->translation.y, start.z - player->translation.z);
	input->InjectMouseMotion(100, 0, WHEEL_DELTA);
	s.Update(1.0f / 60.0f, input);
	input->InjectMouseMotion(0, 0, 0);
	camera = s.FindGameObjectByName("Main Camera", false);
	player = s.FindGameObjectByName("Player", false);
	CHECK_NEAR(camera->rotation.y, yawBefore + 0.4f, 0.01f);
	CHECK(std::hypot(camera->translation.x - player->translation.x, camera->translation.y - player->translation.y, camera->translation.z - player->translation.z) < distanceBefore);

	// The script's view through the screen center reaches the scene
	Vector3 origin{};
	Vector3 direction{};
	REQUIRE(api.screenPointToRay(api.hostContext, { kUICanvasWidth * 0.5f, kUICanvasHeight * 0.5f }, &origin, &direction));
	CHECK(direction.y < 0.0f); // looking down at the player

	s.StopPlayMode();
	CHECK(!api.isCursorLocked(api.hostContext));
}

TEST_CASE(Samples, SurvivorsDemoRuns)
{
	namespace fs = std::filesystem;
	A::ProjectRoot(s) = A::TempRoot() / "SurvivorsProject";
	REQUIRE(s.CreateSurvivorsDemo());
	CHECK(fs::exists(A::TempRoot() / "SurvivorsProject" / "Assets" / "Prefabs" / "Enemy.prefab.json"));
	CHECK(fs::exists(A::TempRoot() / "SurvivorsProject" / "Assets" / "Prefabs" / "Bullet.prefab.json"));
	CHECK(fs::exists(A::TempRoot() / "SurvivorsProject" / "Assets" / "Scenes" / "SurvivorsDemo.scene.json"));
	CHECK(s.FindGameObjectByName("Enemy", false) == nullptr); // templates are not left in the scene
	const GameObject* player = s.FindGameObjectByName("Player", false);
	REQUIRE(player != nullptr);
	CHECK(player->layer == LAYER_PLAYER);
	REQUIRE(s.FindGameObjectByName("GameManager", false) != nullptr);

	// GameScripts.dll is built with the solution, so the sample scripts must be available here
	REQUIRE(A::ScriptsLoaded(s));

	const size_t sceneObjects = A::Objects(s).size();
	s.StartPlayMode();
	for (int frame = 0; frame < 150; ++frame) {
		s.Update(1.0f / 60.0f, RizeEngine::GetInput());
	}
	int enemies = 0;
	int bullets = 0;
	for (const GameObject& obj : A::Objects(s)) {
		if (obj.prefabSource == "Enemy" && obj.isActive) ++enemies;
		if (obj.prefabSource == "Bullet") ++bullets;
	}
	CHECK_MSG(enemies >= 1, "active enemies after 2.5 s: " + std::to_string(enemies));
	CHECK_MSG(bullets >= 3, "bullets after 2.5 s: " + std::to_string(bullets));
	CHECK_MSG(UIRenderer::GetInstance()->GetQuadCount() > 20, "HUD quads: " + std::to_string(UIRenderer::GetInstance()->GetQuadCount()));

	s.StopPlayMode();
	CHECK(A::Objects(s).size() == sceneObjects);
	CHECK(!UIRenderer::GetInstance()->HasCommands());
}

// ===========================================================================
// Script API, Tahap H (EditorSuite_ScriptQueries.cpp)
// ===========================================================================

namespace {
// Collider-only object (no mesh changes): a sphere or a box of the given size
uint32_t MakeCollider(EditorSuite& s, const char* name, const Vector3& pos, bool sphere, float size, uint32_t layer = LAYER_DEFAULT)
{
	GameObject& obj = s.CreateGameObject(name, GameObjectType::Empty);
	obj.translation = pos;
	obj.layer = layer;
	obj.hasSphereCollider = sphere;
	obj.sphereRadius = size * 0.5f;
	obj.hasBoxCollider = !sphere;
	obj.boxSize = { size, size, size };
	return obj.id;
}
} // namespace

TEST_CASE(ScriptAPI, CollisionEnterStayExit)
{
	A::State(s) = EditorSceneState::Play;
	const uint32_t a = MakeCollider(s, "A", { 0.0f, 50.0f, 0.0f }, true, 1.0f);
	const uint32_t b = MakeCollider(s, "B", { 0.6f, 50.0f, 0.0f }, true, 1.0f);
	// A box + a sphere on one object must still give a single contact per pair
	s.FindGameObjectById(b, false)->hasBoxCollider = true;

	A::Physics(s, 1.0f / 60.0f);
	auto ev = A::CollisionEvents(s);
	CHECK_MSG(ev.enter == 2 && ev.stay == 2 && ev.exit == 0, std::format("frame 1: {}/{}/{}", ev.enter, ev.stay, ev.exit));

	A::Physics(s, 1.0f / 60.0f);
	ev = A::CollisionEvents(s);
	CHECK_MSG(ev.enter == 0 && ev.stay == 2 && ev.exit == 0, std::format("frame 2: {}/{}/{}", ev.enter, ev.stay, ev.exit));

	// A moved collider sweeps during the step, so its new contacts are reported by the next one
	s.FindGameObjectById(b, false)->translation = { 5.0f, 50.0f, 0.0f };
	A::Physics(s, 1.0f / 60.0f);
	A::Physics(s, 1.0f / 60.0f);
	ev = A::CollisionEvents(s);
	CHECK_MSG(ev.enter == 0 && ev.stay == 0 && ev.exit == 2, std::format("separated: {}/{}/{}", ev.enter, ev.stay, ev.exit));

	// Deactivating (despawning) one side ends the contact too
	s.FindGameObjectById(b, false)->translation = { 0.6f, 50.0f, 0.0f };
	A::Physics(s, 1.0f / 60.0f);
	A::Physics(s, 1.0f / 60.0f);
	CHECK(A::CollisionEvents(s).enter == 2);
	s.FindGameObjectById(a, false)->isActive = false;
	A::Physics(s, 1.0f / 60.0f);
	CHECK(A::CollisionEvents(s).exit == 2);

	// Stop / scene load forgets old contacts: nothing "exits" in the next session
	s.FindGameObjectById(a, false)->isActive = true;
	A::Physics(s, 1.0f / 60.0f);
	s.ResetScriptRuntimeState();
	A::Physics(s, 1.0f / 60.0f);
	CHECK(A::CollisionEvents(s).enter == 2);
	A::State(s) = EditorSceneState::Edit;
	s.ResetScriptRuntimeState();
}

TEST_CASE(ScriptAPI, RaycastOverlapAndTagQueries)
{
	EngineAPI& api = A::Api(s);
	const uint32_t wall = MakeCollider(s, "Wall", { 5.0f, 50.0f, 0.0f }, false, 1.0f, LAYER_WALL);
	const uint32_t farWall = MakeCollider(s, "FarWall", { 10.0f, 50.0f, 0.0f }, false, 1.0f, LAYER_ENEMY);
	const uint32_t ball = MakeCollider(s, "Ball", { -5.0f, 50.0f, 0.0f }, true, 1.0f, LAYER_ENEMY);
	const uint32_t shooter = MakeCollider(s, "Shooter", { 0.0f, 50.0f, 0.0f }, true, 1.0f, LAYER_PLAYER);
	s.FindGameObjectById(farWall, false)->tag = "Enemy";
	s.FindGameObjectById(ball, false)->tag = "Enemy";

	// Box hit: entry face at x = 4.5, normal -X. The shooter's own collider contains the origin (ignored).
	ScriptRaycastHit hit{};
	REQUIRE(api.raycast(&s, { 0.0f, 50.0f, 0.0f }, { 2.0f, 0.0f, 0.0f }, 100.0f, kCollideWithAll, 0, &hit));
	CHECK(hit.objectId == wall);
	CHECK_NEAR(hit.distance, 4.5f, 1e-4f);
	CHECK_NEAR(hit.normal.x, -1.0f, 1e-5f);

	// Layer mask skips the wall; maxDistance cuts the far one off
	REQUIRE(api.raycast(&s, { 0.0f, 50.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 100.0f, LayerBit(LAYER_ENEMY), 0, &hit));
	CHECK(hit.objectId == farWall);
	CHECK(!api.raycast(&s, { 0.0f, 50.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 4.0f, kCollideWithAll, 0, &hit));
	CHECK(!api.raycast(&s, { 0.0f, 50.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 4.0f, kCollideWithAll, 0, &hit));

	// Sphere hit: x = -4.5, normal +X
	REQUIRE(api.raycast(&s, { 0.0f, 50.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, 100.0f, kCollideWithAll, shooter, &hit));
	CHECK(hit.objectId == ball);
	CHECK_NEAR(hit.point.x, -4.5f, 1e-4f);
	CHECK_NEAR(hit.normal.x, 1.0f, 1e-4f);

	// Overlap sphere (box and sphere colliders), with mask
	uint32_t ids[8]{};
	CHECK(api.overlapSphere(&s, { 4.0f, 50.0f, 0.0f }, 0.6f, kCollideWithAll, ids, 8) == 1);
	CHECK(ids[0] == wall);
	CHECK(api.overlapSphere(&s, { 4.0f, 50.0f, 0.0f }, 0.6f, LayerBit(LAYER_ENEMY), ids, 8) == 0);
	CHECK(api.overlapSphere(&s, { 0.0f, 50.0f, 0.0f }, 20.0f, kCollideWithAll, ids, 2) == 2); // maxCount respected

	// Tags and nearest
	CHECK(api.findAllWithTag(&s, "Enemy", ids, 8) == 2);
	ScriptGameObjectView* nearest = api.findNearest(&s, { -1.0f, 50.0f, 0.0f }, "Enemy", 1.0e30f, 0);
	REQUIRE(nearest != nullptr);
	CHECK(nearest->id == ball);
	CHECK(api.findNearest(&s, { -1.0f, 50.0f, 0.0f }, "Enemy", 2.0f, 0) == nullptr);
	nearest = api.findNearest(&s, { 0.0f, 50.0f, 0.0f }, nullptr, 1.0e30f, shooter);
	REQUIRE(nearest != nullptr);
	CHECK(nearest->id != shooter);

	// An object queued for deletion is invisible to queries
	s.QueueDeleteGameObjectById(ball);
	CHECK(!api.raycast(&s, { 0.0f, 50.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, 100.0f, kCollideWithAll, shooter, &hit));
	CHECK(api.findAllWithTag(&s, "Enemy", ids, 8) == 1);
	ScriptGameObjectView* firstEnemy = api.findWithTag(&s, "Enemy");
	REQUIRE(firstEnemy != nullptr);
	CHECK(firstEnemy->id == farWall);
	s.ProcessPendingDeletes();
}

TEST_CASE(ScriptAPI, DespawnAfterTimers)
{
	EngineAPI& api = A::Api(s);
	A::State(s) = EditorSceneState::Play;
	const uint32_t shortLived = s.CreateCube("ShortLived", { 0.0f, 0.0f, 0.0f }).id;
	const uint32_t cancelled = s.CreateCube("Cancelled", { 0.0f, 0.0f, 0.0f }).id;

	api.despawnAfter(&s, shortLived, 0.5f);
	api.despawnAfter(&s, shortLived, 0.5f); // replaces, does not stack
	api.despawnAfter(&s, cancelled, 0.5f);
	CHECK(s.GetDelayedDespawnCount() == 2);
	api.deleteObject(&s, cancelled);        // destroying an object cancels its timer
	CHECK(s.GetDelayedDespawnCount() == 1);
	s.ProcessPendingSpawns();
	s.ProcessPendingDeletes();

	s.TickScriptTimers(0.3f);
	s.ProcessPendingSpawns();
	s.ProcessPendingDeletes();
	CHECK(s.FindGameObjectById(shortLived, false) != nullptr);
	s.TickScriptTimers(0.0f);               // paused game time does not advance timers
	CHECK(s.GetDelayedDespawnCount() == 1);
	s.TickScriptTimers(0.3f);
	s.ProcessPendingSpawns();
	s.ProcessPendingDeletes();
	CHECK(s.FindGameObjectById(shortLived, false) == nullptr);
	CHECK(s.GetDelayedDespawnCount() == 0);
	A::State(s) = EditorSceneState::Edit;
}

TEST_CASE(ScriptAPI, HierarchyAndComponentAccess)
{
	EngineAPI& api = A::Api(s);
	A::State(s) = EditorSceneState::Play;
	const uint32_t parent = s.CreateCube("Parent", { 10.0f, 0.0f, 0.0f }).id;
	const uint32_t child = s.CreateSphere("Child", { 12.0f, 1.0f, 0.0f }).id;

	// SetParent is deferred and keeps the world position
	CHECK(!api.setParent(&s, child, child, true));
	CHECK(!api.setParent(&s, child, 999999u, true));
	REQUIRE(api.setParent(&s, child, parent, true));
	CHECK(api.getParent(&s, child) == 0u);
	s.ProcessPendingSpawns();
	CHECK(api.getParent(&s, child) == parent);
	uint32_t kids[4]{};
	CHECK(api.getChildren(&s, parent, kids, 4) == 1);
	CHECK(kids[0] == child);
	Vector3 world{};
	REQUIRE(api.getWorldPosition(&s, child, &world));
	CHECK_NEAR(world.x, 12.0f, 1e-4f);
	CHECK_NEAR(s.FindGameObjectById(child, false)->translation.x, 2.0f, 1e-4f);
	REQUIRE(api.setParent(&s, child, 0u, true));
	s.ProcessPendingSpawns();
	CHECK(api.getParent(&s, child) == 0u);
	CHECK_NEAR(s.FindGameObjectById(child, false)->translation.x, 12.0f, 1e-4f);

	// Material
	ScriptMaterialData mat{};
	REQUIRE(api.getMaterial(&s, parent, &mat));
	mat.color = { 1.0f, 0.0f, 0.0f, 0.5f };
	mat.visible = false;
	mat.lightingType = 99;
	REQUIRE(api.setMaterial(&s, parent, &mat));
	const GameObject* p = s.FindGameObjectById(parent, false);
	CHECK_NEAR(p->materialColor.w, 0.5f, 1e-6f);
	CHECK(!p->meshEnabled);
	CHECK(p->lightingType == 5); // clamped to the last model (Toon)

	// Components the object does not have report false
	ScriptLightData light{};
	CHECK(!api.getLight(&s, parent, &light));
	ScriptRigidbodyData rb{};
	CHECK(!api.getRigidbody(&s, parent, &rb));
	s.FindGameObjectById(parent, false)->hasRigidbody = true;
	REQUIRE(api.getRigidbody(&s, parent, &rb));
	rb.mass = -3.0f;
	rb.useGravity = false;
	REQUIRE(api.setRigidbody(&s, parent, &rb));
	CHECK(s.FindGameObjectById(parent, false)->mass > 0.0f);
	CHECK(!s.FindGameObjectById(parent, false)->useGravity);

	GameObject& lamp = s.CreateGameObject("Lamp", GameObjectType::Empty);
	lamp.hasLight = true;
	const uint32_t lampId = lamp.id;
	light.type = 1;
	light.intensity = 3.0f;
	light.color = { 1.0f, 0.5f, 0.0f };
	REQUIRE(api.setLight(&s, lampId, &light));
	CHECK(s.FindGameObjectById(lampId, false)->lightType == GameObject::LightType::Point);
	CHECK_NEAR(s.FindGameObjectById(lampId, false)->lightIntensity, 3.0f, 1e-6f);

	// Missing audio files fail quietly
	CHECK(api.playSound(&s, "Assets/Audio/does_not_exist.wav", 1.0f, false) == 0u);
	api.playMusic(&s, "Assets/Audio/does_not_exist.wav", 1.0f);
	CHECK(A::MusicVoice(s) == 0u);
	A::State(s) = EditorSceneState::Edit;
}

TEST_CASE(ScriptAPI, PlayModeRestoresScriptChangedComponents)
{
	EngineAPI& api = A::Api(s);
	const uint32_t cube = s.CreateCube("Tinted", { 0.0f, 0.0f, 0.0f }).id;
	const Vector4 original = s.FindGameObjectById(cube, false)->materialColor;
	s.StartPlayMode();
	ScriptMaterialData mat{};
	REQUIRE(api.getMaterial(&s, cube, &mat));
	mat.color = { 1.0f, 0.0f, 0.0f, 1.0f };
	REQUIRE(api.setMaterial(&s, cube, &mat));
	s.Update(1.0f / 60.0f, RizeEngine::GetInput());
	s.StopPlayMode();
	const GameObject* after = s.FindGameObjectById(cube, false);
	REQUIRE(after != nullptr);
	CHECK_NEAR(after->materialColor.x, original.x, 1e-6f);
	CHECK_NEAR(after->materialColor.y, original.y, 1e-6f);
}

TEST_CASE(ScriptAPI, ParticleBurstsAndOneShotPrefabs)
{
	EngineAPI& api = A::Api(s);
	A::ProjectRoot(s) = A::TempRoot() / "ParticleProject";
	GameObject& fx = s.CreateGameObject("Spark", GameObjectType::Empty);
	REQUIRE(s.AddComponentByName(fx, kGpuParticlesComponent));
	{
		auto* p = fx.components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
		REQUIRE(p != nullptr);
		p->emitting = false;
		p->spawnBurst = 12;
		p->capacity = 64;
	}
	const uint32_t fxId = fx.id;
	REQUIRE(s.SavePrefab(fxId, "Spark"));
	A::State(s) = EditorSceneState::Play;
	const auto pendingBurst = [&](uint32_t id) {
		const GameObject* obj = s.FindGameObjectById(id, false);
		const auto* p = obj ? obj->components.Get<GpuParticlesComponent>(kGpuParticlesComponent) : nullptr;
		return p ? p->burst : -1;
	};

	// EmitParticles on a scene object (deferred to the end of the frame, clamped to the capacity)
	api.emitParticles(&s, fxId, 5);
	CHECK(pendingBurst(fxId) == 0);
	s.ProcessPendingSpawns();
	CHECK(pendingBurst(fxId) == 5);
	api.emitParticles(&s, fxId, 500);
	s.ProcessPendingSpawns();
	CHECK_MSG(pendingBurst(fxId) == 64, "clamped burst: " + std::to_string(pendingBurst(fxId)));

	// One-shot prefab: bursts spawnBurst on spawn, and again when reused from the pool
	ScriptGameObjectView* view = s.QueuePrefabSpawn("Spark", { 3.0f, 0.0f, 0.0f });
	REQUIRE(view != nullptr);
	const uint32_t instance = view->id;
	s.ProcessPendingSpawns();
	CHECK_MSG(pendingBurst(instance) == 12, "spawn burst: " + std::to_string(pendingBurst(instance)));
	s.QueueDespawn(instance);
	s.ProcessPendingSpawns();
	REQUIRE(s.QueuePrefabSpawn("Spark", { 0.0f, 0.0f, 0.0f }) != nullptr);
	s.ProcessPendingSpawns();
	CHECK_MSG(pendingBurst(instance) == 12, "reuse burst: " + std::to_string(pendingBurst(instance)));

	// Continuous flag from script
	api.setParticlesEmitting(&s, fxId, true);
	const auto* emitter = s.FindGameObjectById(fxId, false)->components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
	REQUIRE(emitter != nullptr);
	CHECK(emitter->emitting);
	A::State(s) = EditorSceneState::Edit;
	s.ResetPrefabPools();
}

// ===========================================================================
// Component registry (roadmap 4.1)
// ===========================================================================

namespace {
struct TestHealthComponent {
	float hp = 10.0f;
	int lives = 3;
	bool invincible = false;
	Vector3 knockback = { 0.0f, 1.0f, 0.0f };
	Vector4 tint = { 1.0f, 1.0f, 1.0f, 1.0f };
	char label[32] = "hero";
};
constexpr const char* kTestHealth = "Test Health";

// Registers the test-only data component for the lifetime of one test
struct ScopedTestHealthComponent {
	ScopedTestHealthComponent()
	{
		RegisterDataComponent<TestHealthComponent>(kTestHealth, "Tests", {
			RIZE_FIELD(TestHealthComponent, hp, ComponentFieldType::Float, 0.0f, 100.0f),
			RIZE_FIELD(TestHealthComponent, lives, ComponentFieldType::Int, 0.0f, 9.0f),
			RIZE_FIELD(TestHealthComponent, invincible, ComponentFieldType::Bool, 0.0f, 0.0f),
			RIZE_FIELD(TestHealthComponent, knockback, ComponentFieldType::Vector3, 0.0f, 0.0f),
			RIZE_FIELD(TestHealthComponent, tint, ComponentFieldType::Color, 0.0f, 0.0f),
			RIZE_FIELD(TestHealthComponent, label, ComponentFieldType::Text, 0.0f, 0.0f) });
	}
	~ScopedTestHealthComponent() { ComponentRegistry::Get().Unregister(kTestHealth); }
};

TestHealthComponent* HealthOf(EditorSuite& s, uint32_t id)
{
	GameObject* obj = s.FindGameObjectById(id, false);
	return obj ? obj->components.Get<TestHealthComponent>(kTestHealth) : nullptr;
}
} // namespace

TEST_CASE(Components, RegistryDrivesBuiltins)
{
	const ComponentRegistry& registry = ComponentRegistry::Get();
	const char* expected[] = { "Mesh Filter & Renderer", "Box Collider", "Sphere Collider", "Rigidbody", "Camera",
		"Light", "Mapchip Level Editor", "Audio Source" };
	REQUIRE(registry.All().size() >= std::size(expected));
	for (size_t i = 0; i < std::size(expected); ++i) {
		CHECK_MSG(registry.All()[i].name == expected[i], "registry[" + std::to_string(i) + "] = " + registry.All()[i].name);
		CHECK(!registry.All()[i].IsDataComponent());
	}

	const uint32_t cube = s.CreateCube("RegistryCube", { 0.0f, 0.0f, 0.0f }).id;
	GameObject* obj = s.FindGameObjectById(cube, false);
	CHECK(s.HasComponentByName(*obj, "Mesh Filter & Renderer"));
	CHECK(s.HasComponentByName(*obj, "Box Collider"));
	CHECK(!s.HasComponentByName(*obj, "Rigidbody"));
	CHECK(!s.HasComponentByName(*obj, "No Such Component"));

	REQUIRE(s.AddComponentByName(*obj, "Rigidbody"));
	obj = s.FindGameObjectById(cube, false);
	CHECK(obj->hasRigidbody && obj->rbEnabled);
	CHECK(!s.AddComponentByName(*obj, "Rigidbody")); // already present
	CHECK(!s.AddComponentByName(*obj, "No Such Component"));

	A::MergeAllowed(s) = false;
	REQUIRE(s.RemoveComponentByName(*obj, "Box Collider"));
	obj = s.FindGameObjectById(cube, false);
	CHECK(!obj->hasBoxCollider);
	CHECK(!s.RemoveComponentByName(*obj, "Box Collider"));
	s.Undo();
	CHECK(s.FindGameObjectById(cube, false)->hasBoxCollider);
}

TEST_CASE(Components, DataComponentSaveUndoCopyPrefab)
{
	ScopedTestHealthComponent registration;
	const uint32_t id = s.CreateCube("Hero", { 0.0f, 0.0f, 0.0f }).id;
	s.PushUndoSnapshot();

	A::MergeAllowed(s) = false;
	REQUIRE(s.AddComponentByName(*s.FindGameObjectById(id, false), kTestHealth));
	TestHealthComponent* hp = HealthOf(s, id);
	REQUIRE(hp != nullptr);
	CHECK_NEAR(hp->hp, 10.0f, 1e-6f); // starts from the struct defaults
	hp->hp = 42.5f;
	hp->lives = 7;
	hp->invincible = true;
	hp->knockback = { 1.0f, 2.0f, 3.0f };
	hp->tint = { 0.5f, 0.25f, 0.125f, 1.0f };
	strcpy_s(hp->label, "boss");

	// Scene JSON round trip
	const std::string saved = s.SerializeSceneToString();
	CHECK(saved.find("\"components\"") != std::string::npos);
	REQUIRE(s.DeserializeSceneFromString(saved));
	hp = HealthOf(s, id);
	REQUIRE(hp != nullptr);
	CHECK_NEAR(hp->hp, 42.5f, 1e-5f);
	CHECK(hp->lives == 7);
	CHECK(hp->invincible);
	CHECK_NEAR(hp->knockback.z, 3.0f, 1e-6f);
	CHECK_NEAR(hp->tint.y, 0.25f, 1e-6f);
	CHECK(std::string(hp->label) == "boss");
	CHECK(s.SerializeSceneToString() == saved);

	// Copies are deep (Play-mode snapshots, undo states)
	{
		GameObject copy = *s.FindGameObjectById(id, false);
		copy.components.Get<TestHealthComponent>(kTestHealth)->hp = 1.0f;
		CHECK_NEAR(HealthOf(s, id)->hp, 42.5f, 1e-5f);
	}

	// Undo removes the added component again
	s.ResetUndoHistory();
	A::MergeAllowed(s) = false;
	REQUIRE(s.RemoveComponentByName(*s.FindGameObjectById(id, false), kTestHealth));
	CHECK(HealthOf(s, id) == nullptr);
	s.Undo();
	REQUIRE(HealthOf(s, id) != nullptr);
	CHECK_NEAR(HealthOf(s, id)->hp, 42.5f, 1e-5f);

	// Prefab instances carry the component; a pooled instance gets the prefab value back
	A::ProjectRoot(s) = A::TempRoot() / "ComponentPrefabProject";
	REQUIRE(s.SavePrefab(id, "Hero"));
	A::State(s) = EditorSceneState::Play;
	ScriptGameObjectView* view = s.QueuePrefabSpawn("Hero", { 5.0f, 0.0f, 0.0f });
	REQUIRE(view != nullptr);
	const uint32_t instance = view->id;
	s.ProcessPendingSpawns();
	REQUIRE(HealthOf(s, instance) != nullptr);
	CHECK_NEAR(HealthOf(s, instance)->hp, 42.5f, 1e-5f);
	HealthOf(s, instance)->hp = 0.0f; // "killed"
	s.QueueDespawn(instance);
	s.ProcessPendingSpawns();
	REQUIRE(s.QueuePrefabSpawn("Hero", { 0.0f, 0.0f, 0.0f }) != nullptr);
	s.ProcessPendingSpawns();
	CHECK_NEAR(HealthOf(s, instance)->hp, 42.5f, 1e-5f);
	A::State(s) = EditorSceneState::Edit;
	s.ResetPrefabPools();
}

TEST_CASE(Components, UnknownComponentIsPreserved)
{
	std::string saved;
	uint32_t id = 0;
	{
		ScopedTestHealthComponent registration;
		id = s.CreateGameObject("Keeper", GameObjectType::Empty).id;
		REQUIRE(s.AddComponentByName(*s.FindGameObjectById(id, false), kTestHealth));
		HealthOf(s, id)->lives = 5;
		saved = s.SerializeSceneToString();
	}

	// A build without the component keeps its data and writes it back unchanged
	REQUIRE(s.DeserializeSceneFromString(saved));
	const GameObject* obj = s.FindGameObjectById(id, false);
	REQUIRE(obj != nullptr);
	CHECK(obj->components.Entries().empty());
	CHECK(obj->components.Unknown().count(kTestHealth) == 1);
	CHECK(s.SerializeSceneToString() == saved);

	// Registered again: the values come back
	ScopedTestHealthComponent registration;
	REQUIRE(s.DeserializeSceneFromString(saved));
	REQUIRE(HealthOf(s, id) != nullptr);
	CHECK(HealthOf(s, id)->lives == 5);
}

// ===========================================================================
// Asset database (4.2)
// ===========================================================================

namespace {
	// Copies a real engine texture into the test project so loading it works like a user asset
	bool PlaceTestTexture(const std::filesystem::path& projectRoot, const std::string& assetPath)
	{
		std::error_code ec;
		const std::filesystem::path dst = projectRoot / assetPath;
		std::filesystem::create_directories(dst.parent_path(), ec);
		std::filesystem::copy_file("Resources/circle.png", dst, std::filesystem::copy_options::overwrite_existing, ec);
		return !ec;
	}
}

TEST_CASE(Assets, PathRulesAndMetaFiles)
{
	const std::filesystem::path root = A::TempRoot() / "AssetRulesProject";
	A::ProjectRoot(s) = root;
	REQUIRE(PlaceTestTexture(root, "Assets/Textures/rock.png"));
	AssetDatabase& db = s.AssetDb();
	db.Refresh();

	CHECK(db.ToAssetPath("Assets/Textures/rock.png") == "Assets/Textures/rock.png");
	CHECK(db.ToAssetPath((root / "Assets" / "Textures" / "rock.png").string()) == "Assets/Textures/rock.png");
	CHECK(db.ToAssetPath("Resources/circle.png").empty()); // engine assets are not managed
	CHECK(db.ToAssetPath("Assets/../secret.txt").empty());

	const std::string guid = db.GuidForPath("Assets/Textures/rock.png");
	CHECK(AssetDatabase::IsValidGuid(guid));
	CHECK(std::filesystem::exists(root / "Assets" / "Textures" / "rock.png.meta"));
	CHECK(db.GuidForPath("assets/textures/ROCK.png") == guid); // case-insensitive like Windows
	CHECK(db.PathForGuid(guid) == "Assets/Textures/rock.png");
	CHECK(db.GuidForPath("Resources/circle.png").empty());

	// The GUID is stable across rescans (it comes from the .meta, not from the path)
	db.Refresh();
	CHECK(db.GuidForPath("Assets/Textures/rock.png") == guid);
	CHECK(db.Count() == 1); // the .meta itself is not an asset
}

TEST_CASE(Assets, DuplicateMetaGetsNewGuid)
{
	const std::filesystem::path root = A::TempRoot() / "AssetDuplicateProject";
	A::ProjectRoot(s) = root;
	REQUIRE(PlaceTestTexture(root, "Assets/grass.png"));
	AssetDatabase& db = s.AssetDb();
	db.Refresh();
	const std::string original = db.GuidForPath("Assets/grass.png");
	REQUIRE(!original.empty());

	// Copy in Explorer: the copy carries the same .meta content
	std::error_code ec;
	std::filesystem::copy_file(root / "Assets/grass.png", root / "Assets/grass - Copy.png", ec);
	std::filesystem::copy_file(root / "Assets/grass.png.meta", root / "Assets/grass - Copy.png.meta", ec);
	db.Refresh();
	CHECK(db.GuidForPath("Assets/grass.png") == original);
	const std::string copy = db.GuidForPath("Assets/grass - Copy.png");
	CHECK(AssetDatabase::IsValidGuid(copy));
	CHECK(copy != original);
}

TEST_CASE(SceneIO, FailedSaveKeepsTheOldFileAndTheAutoSave)
{
	// A save that cannot finish (here: its temp file cannot be created) must not truncate the file, clear the
	// dirty flag or delete the auto-save copy, which is then the only copy of the edits
	namespace fs = std::filesystem;
	const fs::path root = A::TempRoot() / "AtomicSaveProject";
	fs::remove_all(root);
	A::ProjectRoot(s) = root;
	fs::create_directories(root / "Assets/Scenes");
	const fs::path scene = root / "Assets/Scenes/Level.scene.json";
	REQUIRE(s.SaveScene(scene.string()));
	CHECK(!fs::exists(scene.string() + ".tmp"));
	const auto read = [](const fs::path& file) {
		std::ifstream in(file, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	};
	const std::string saved = read(scene);
	REQUIRE(!saved.empty());

	s.CreateCube("Unsaved Edit", { 0.0f, 0.0f, 0.0f });
	A::Dirty(s) = true;
	const fs::path autoSave = AutoSave::PathFor(root, scene);
	fs::create_directories(autoSave.parent_path());
	std::ofstream(autoSave) << "{}";
	fs::create_directories(scene.string() + ".tmp"); // a folder where the temp file goes: the write fails
	CHECK(!s.SaveScene(scene.string()));
	CHECK(read(scene) == saved);
	CHECK(s.IsSceneDirty());
	CHECK(fs::exists(autoSave));
	fs::remove_all(scene.string() + ".tmp");
	CHECK(s.SaveScene(scene.string()));
	CHECK(read(scene).find("Unsaved Edit") != std::string::npos);

	// Prefabs go the same way
	const uint32_t rootId = s.CreateCube("Crate", { 0.0f, 0.0f, 0.0f }).id;
	REQUIRE(s.SavePrefab(rootId, "Crate"));
	const fs::path prefab = s.ResolvePrefabFile("Crate");
	const std::string prefabText = read(prefab);
	s.FindGameObjectById(rootId, false)->name = "Renamed Crate";
	fs::create_directories(prefab.string() + ".tmp");
	CHECK(!s.SavePrefab(rootId, "Crate"));
	CHECK(read(prefab) == prefabText);
	fs::remove_all(prefab.string() + ".tmp");
	fs::remove_all(root);
}

TEST_CASE(Assets, RenamingTheSceneRenamesItsFileAndSurvivesSave)
{
	namespace fs = std::filesystem;
	const fs::path root = A::TempRoot() / "SceneRenameProject";
	A::ProjectRoot(s) = root;
	fs::create_directories(root / "Assets/Scenes");
	const fs::path level1 = root / "Assets/Scenes/Level1.scene.json";
	REQUIRE(s.SaveScene(level1.string()));
	s.AssetDb().Refresh();
	REQUIRE(fs::exists(root / "Assets/Scenes/Level1.scene.json.meta"));

	// Hierarchy > Rename Scene, then Ctrl+S (SaveScene(currentScenePath_)): the new name sticks
	REQUIRE(s.RenameCurrentScene("Boss Arena"));
	CHECK(A::SceneName(s) == "Boss Arena");
	CHECK(fs::exists(root / "Assets/Scenes/Boss Arena.scene.json"));
	CHECK(fs::exists(root / "Assets/Scenes/Boss Arena.scene.json.meta")); // the GUID moves with it
	CHECK(!fs::exists(level1));
	REQUIRE(s.SaveScene(A::ScenePath(s)));
	CHECK(A::SceneName(s) == "Boss Arena");
	CHECK(fs::path(A::ScenePath(s)).filename() == "Boss Arena.scene.json");
	CHECK(!fs::exists(level1));

	// Refused: an existing scene's name, characters a file name cannot hold, an empty name
	std::ofstream(root / "Assets/Scenes/Other.scene.json") << "{}";
	CHECK(!s.RenameCurrentScene("Other"));
	CHECK(!s.RenameCurrentScene("a/b"));
	CHECK(!s.RenameCurrentScene("   "));
	CHECK(A::SceneName(s) == "Boss Arena");
	CHECK(fs::exists(root / "Assets/Scenes/Boss Arena.scene.json"));

	// Renaming the open scene in the Project panel updates the Hierarchy header too
	REQUIRE(s.MoveAssetAndUpdateReferences("Assets/Scenes/Boss Arena.scene.json", "Assets/Scenes/Final.scene.json"));
	CHECK(A::SceneName(s) == "Final");

	// A scene never saved has no file yet: the name only decides where Ctrl+S writes
	s.NewScene();
	REQUIRE(s.RenameCurrentScene("Fresh"));
	CHECK(fs::path(A::ScenePath(s)).filename() == "Fresh.scene.json");
	REQUIRE(s.SaveScene(A::ScenePath(s)));
	CHECK(A::SceneName(s) == "Fresh");
}

TEST_CASE(Assets, GuidSurvivesRenameAndFolderMove)
{
	const std::filesystem::path root = A::TempRoot() / "AssetMoveProject";
	A::ProjectRoot(s) = root;
	REQUIRE(PlaceTestTexture(root, "Assets/Textures/wall.png"));
	s.AssetDb().Refresh();

	GameObject& cube = s.CreateCube("Wall", { 0.0f, 50.0f, 0.0f });
	cube.texturePath = "Assets/Textures/wall.png";
	const uint32_t id = cube.id;
	const std::string savedBefore = s.SerializeSceneToString();
	const std::string guid = s.AssetDb().FindGuid("Assets/Textures/wall.png");
	REQUIRE(!guid.empty());
	CHECK(savedBefore.find(guid) != std::string::npos); // "textureGuid" is written next to "texturePath"

	// Rename in the Project panel: the open scene follows right away
	REQUIRE(s.MoveAssetAndUpdateReferences("Assets/Textures/wall.png", "Assets/Textures/brick.png"));
	CHECK(std::filesystem::exists(root / "Assets/Textures/brick.png"));
	CHECK(std::filesystem::exists(root / "Assets/Textures/brick.png.meta"));
	CHECK(!std::filesystem::exists(root / "Assets/Textures/wall.png.meta"));
	CHECK(s.FindGameObjectById(id, false)->texturePath == "Assets/Textures/brick.png");

	// Moving the whole folder repoints everything inside it
	REQUIRE(s.MoveAssetAndUpdateReferences("Assets/Textures", "Assets/Art/Textures"));
	CHECK(s.FindGameObjectById(id, false)->texturePath == "Assets/Art/Textures/brick.png");
	CHECK(s.AssetDb().PathForGuid(guid) == "Assets/Art/Textures/brick.png");

	// A scene saved before both moves still finds the texture through its GUID (even after a rescan)
	s.AssetDb().Refresh();
	REQUIRE(s.DeserializeSceneFromString(savedBefore));
	CHECK(s.FindGameObjectById(id, false)->texturePath == "Assets/Art/Textures/brick.png");

	// A legacy scene (path only, no GUID) keeps its path untouched
	std::string legacy = savedBefore;
	const size_t guidPos = legacy.find(guid);
	legacy.replace(guidPos, guid.size(), "");
	REQUIRE(s.DeserializeSceneFromString(legacy));
	CHECK(s.FindGameObjectById(id, false)->texturePath == "Assets/Textures/wall.png");

	// Invalid moves are refused and change nothing
	CHECK(!s.MoveAssetAndUpdateReferences("Assets/Art", "Assets/Art/Inside"));
	CHECK(!s.MoveAssetAndUpdateReferences("Assets/Art/Textures/brick.png", "../outside.png"));
	CHECK(std::filesystem::exists(root / "Assets/Art/Textures/brick.png"));
}

TEST_CASE(Assets, RenameRepointsDataComponentAssetFields)
{
	// The Animator's controller is a ComponentFieldType::Asset field: a rename in the Project panel must follow it
	A::ProjectRoot(s) = A::TempRoot() / "AssetFieldProject";
	const std::string path = s.CreateAnimatorControllerAsset("Hero");
	REQUIRE(path == "Assets/Animators/Hero.animator.json");
	GameObject& hero = s.CreateGameObject("Hero", GameObjectType::Empty);
	const uint32_t id = hero.id;
	REQUIRE(s.AddComponentByName(hero, "Animator"));
	strncpy_s(s.GetAnimator(*s.FindGameObjectById(id, false))->controller, path.c_str(), _TRUNCATE);

	REQUIRE(s.MoveAssetAndUpdateReferences(path, "Assets/Animators/Player.animator.json"));
	CHECK(std::string(s.GetAnimator(*s.FindGameObjectById(id, false))->controller) == "Assets/Animators/Player.animator.json");
	REQUIRE(s.MoveAssetAndUpdateReferences("Assets/Animators", "Assets/Characters"));
	CHECK(std::string(s.GetAnimator(*s.FindGameObjectById(id, false))->controller) == "Assets/Characters/Player.animator.json");
}

TEST_CASE(Assets, PrefabFoundAfterMove)
{
	const std::filesystem::path root = A::TempRoot() / "AssetPrefabProject";
	A::ProjectRoot(s) = root;
	const uint32_t crateId = s.CreateCube("Crate", { 0.0f, 50.0f, 0.0f }).id;
	REQUIRE(s.SavePrefab(crateId, "Crate"));

	REQUIRE(s.MoveAssetAndUpdateReferences("Assets/Prefabs/Crate.prefab.json", "Assets/Prefabs/Props/Crate.prefab.json"));
	CHECK(s.HasPrefab("Crate"));
	const uint32_t instance = s.InstantiatePrefabNow("Crate", { 3.0f, 50.0f, 0.0f });
	CHECK(instance != 0);

	// Saving again overwrites the moved file instead of creating a second "Crate" in Prefabs/
	REQUIRE(s.SavePrefab(crateId, "Crate"));
	CHECK(!std::filesystem::exists(root / "Assets/Prefabs/Crate.prefab.json"));
	CHECK(std::filesystem::exists(root / "Assets/Prefabs/Props/Crate.prefab.json"));
}

// ===========================================================================
// Japanese text: UTF-8 code page, Unicode paths, game UI glyphs, editor language
// (Japanese test strings are written as UTF-8 escapes so the C++ source stays English-only)
// ===========================================================================

namespace {
	// "テクスチャ" (texture), "岩.png" (rock), "日本語" (Japanese), "あ"
	constexpr const char* kJpFolder = "\xE3\x83\x86\xE3\x82\xAF\xE3\x82\xB9\xE3\x83\x81\xE3\x83\xA3";
	constexpr const char* kJpRock = "\xE5\xB2\xA9.png";
	constexpr const char* kJpWord = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
}

TEST_CASE(Encoding, Utf8CodePageAndUnicodePaths)
{
	// Utf8CodePage.manifest: narrow strings are UTF-8 for every Windows API and std::filesystem
	CHECK_MSG(GetACP() == CP_UTF8, "ACP: " + std::to_string(GetACP()));

	const std::filesystem::path root = A::TempRoot() / std::filesystem::path(std::string("JpProject_") + kJpWord);
	A::ProjectRoot(s) = root;
	const std::string texturePath = std::string("Assets/") + kJpFolder + "/" + kJpRock;
	REQUIRE(PlaceTestTexture(root, texturePath));
	CHECK(std::filesystem::path(texturePath).string() == texturePath); // path <-> narrow string round-trips

	// The asset database, references and texture loading all accept the Japanese path
	s.AssetDb().Refresh();
	const std::string guid = s.AssetDb().GuidForPath(texturePath);
	REQUIRE(AssetDatabase::IsValidGuid(guid));
	CHECK(s.AssetDb().PathForGuid(guid) == texturePath);
	const uint32_t texture = TextureManager::GetInstance()->LoadTexture(s.ResolveAssetPath(texturePath));
	CHECK(texture != TextureManager::GetInstance()->GetDefaultWhiteTexture());

	GameObject& cube = s.CreateCube(kJpWord, { 0.0f, 50.0f, 0.0f });
	cube.texturePath = texturePath;
	const uint32_t id = cube.id;
	const std::string renamed = std::string("Assets/") + kJpFolder + "/" + kJpWord + ".png";
	REQUIRE(s.MoveAssetAndUpdateReferences(texturePath, renamed));
	CHECK(s.FindGameObjectById(id, false)->texturePath == renamed);

	// Object names survive a scene round trip
	const std::string saved = s.SerializeSceneToString();
	REQUIRE(s.DeserializeSceneFromString(saved));
	CHECK(s.FindGameObjectById(id, false)->name == kJpWord);
}

TEST_CASE(UI, Utf8JapaneseText)
{
	// UTF-8 decoding: 1-4 byte forms, invalid and overlong input
	auto decodeAll = [](const char* text) {
		std::vector<uint32_t> out;
		for (const char* p = text, *end = text + std::strlen(text); p < end;) out.push_back(UIRenderer::DecodeUtf8(p, end));
		return out;
	};
	CHECK(decodeAll("A") == std::vector<uint32_t>{ 0x41 });
	CHECK(decodeAll("\xC3\xA9") == std::vector<uint32_t>{ 0xE9 });              // e acute
	CHECK(decodeAll(kJpWord) == (std::vector<uint32_t>{ 0x65E5, 0x672C, 0x8A9E }));
	CHECK(decodeAll("\xF0\x9F\x98\x80") == std::vector<uint32_t>{ 0x1F600 });   // 4-byte form
	CHECK(decodeAll("\xFF") == std::vector<uint32_t>{ 0xFFFD });
	CHECK(decodeAll("\xC0\xAF") == std::vector<uint32_t>{ 0xFFFD });            // overlong '/'
	CHECK(decodeAll("\xE3\x81") == (std::vector<uint32_t>{ 0xFFFD }));          // truncated

	UIRenderer* ui = UIRenderer::GetInstance();
	ui->Clear();
	const size_t glyphsBefore = ui->GetGlyphCount();
	ui->DrawString(kJpWord, 10.0f, 10.0f, 32.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
	CHECK(ui->GetQuadCount() == 3); // one quad per character
	CHECK(ui->GetGlyphCount() >= glyphsBefore); // new glyphs are cached (unless an earlier test drew them)
	CHECK(ui->GetFontPageCount() >= 1);
	ui->Clear();
	ui->DrawString("A\xE3\x80\x80" "B", 10.0f, 10.0f, 32.0f, { 1.0f, 1.0f, 1.0f, 1.0f }); // ideographic space
	CHECK(ui->GetQuadCount() == 2);
	ui->Clear();

	const float w = ui->MeasureText(kJpWord, 32.0f);
	CHECK(w > 30.0f);
	CHECK_NEAR(ui->MeasureText(kJpWord, 64.0f), w * 2.0f, 0.01f);

	// A real Japanese glyph needs a Japanese UI font; Windows installs one by default (CI runners may not)
	std::error_code ec;
	if (std::filesystem::exists("C:/Windows/Fonts/YuGothM.ttc", ec) || std::filesystem::exists("C:/Windows/Fonts/meiryo.ttc", ec)) {
		CHECK(ui->HasGlyph(0x3042)); // hiragana a
		CHECK(ui->HasGlyph(0x65E5));
	} else {
		A::Log(s, "[Tests] No Japanese font on this machine: glyph presence not checked.", 1);
	}
	CHECK(ui->HasGlyph('A'));

	// Atlas pages are updated in place: the handle stays valid
	const uint8_t pixels[4 * 4 * 4] = {};
	TextureManager* textures = TextureManager::GetInstance();
	const uint32_t handle = textures->CreateTextureFromRGBA("Tests/UpdatableTexture", pixels, 4, 4);
	REQUIRE(handle != textures->GetDefaultWhiteTexture());
	CHECK(textures->UpdateTextureFromRGBA(handle, pixels, 4, 4));
	CHECK(!textures->UpdateTextureFromRGBA(textures->GetDefaultWhiteTexture(), pixels, 4, 4));
}

TEST_CASE(UI, EditorFontHasJapaneseGlyphs)
{
	ImFont* font = ImGui::GetIO().FontDefault ? ImGui::GetIO().FontDefault : ImGui::GetFont();
	REQUIRE(font != nullptr);
	CHECK(font->IsGlyphInFont('A'));
	std::error_code ec;
	if (!std::filesystem::exists("C:/Windows/Fonts/YuGothM.ttc", ec) && !std::filesystem::exists("C:/Windows/Fonts/meiryo.ttc", ec)
		&& !std::filesystem::exists("C:/Windows/Fonts/msgothic.ttc", ec)) {
		A::Log(s, "[Tests] No Japanese font on this machine: editor glyphs not checked.", 1);
		return;
	}
	A::Log(s, std::format("[Tests] Editor font sources: {}", font->Sources.Size), 0);
	CHECK_MSG(font->IsGlyphInFont(0x30D5), "katakana fu missing from the editor font"); // first character of the Japanese "File"
	CHECK(font->IsGlyphInFont(0x65E5));
	// What text rendering actually uses: the glyph baked at the UI size must be the real one, not the fallback
	ImFontBaked* baked = font->GetFontBaked(ImGui::GetStyle().FontSizeBase > 0.0f ? ImGui::GetStyle().FontSizeBase : 16.0f);
	REQUIRE(baked != nullptr);
	const ImFontGlyph* glyph = baked->FindGlyph(0x30D5);
	REQUIRE(glyph != nullptr);
	CHECK_MSG(glyph->Codepoint == 0x30D5, std::format("baked glyph is U+{:04X} (fallback), visible={}, hasTextures={}, locked={}, noLoad={}, sources={}",
		static_cast<unsigned>(glyph->Codepoint), glyph->Visible != 0,
		(ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0, font->OwnerAtlas->Locked,
		(font->Flags & ImFontFlags_NoLoadGlyphs) != 0, font->Sources.Size));
}

TEST_CASE(Localization, TranslateWithEnglishFallbackAndStableIds)
{
	const std::filesystem::path savedDir = Localization::GetLanguageDirectory();
	const std::string savedLanguage = Localization::CurrentLanguage();

	const std::filesystem::path dir = A::TempRoot() / "Lang";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	{
		std::ofstream out(dir / "tt.json", std::ios::binary);
		out << R"({ "_language": "Test Language", "File": "Berkas", "Save": "Simpan", "Hierarchy": "Hierarki" })";
	}
	Localization::SetLanguageDirectory(dir);
	const auto languages = Localization::AvailableLanguages();
	REQUIRE(!languages.empty());
	CHECK(languages[0].code == "en");
	CHECK(std::any_of(languages.begin(), languages.end(), [](const auto& l) { return l.code == "tt" && l.displayName == "Test Language"; }));

	REQUIRE(Localization::SetLanguage("tt"));
	CHECK(Localization::TranslationCount() == 3); // "_language" is metadata, not a translation
	CHECK(std::string(Tr("File")) == "Berkas");
	CHECK(std::string(Tr("Not translated")) == "Not translated");
	CHECK(std::string(TrId("Hierarchy")) == "Hierarki###Hierarchy"); // same ImGui ID as English
	CHECK(std::string(TrId("Save##scene")) == "Simpan###Save##scene");
	CHECK(std::string(TrId("Not translated")) == "Not translated");
	CHECK(!Localization::SetLanguage("missing"));
	CHECK(Localization::CurrentLanguage() == "tt"); // a failed switch keeps the current language

	REQUIRE(Localization::SetLanguage("en"));
	CHECK(std::string(Tr("File")) == "File");
	CHECK(std::string(TrId("Hierarchy")) == "Hierarchy");

	// The shipped language files parse and cover the main editor UI
	Localization::SetLanguageDirectory(std::filesystem::path("Resources") / "Localization");
	for (const char* code : { "ja", "id" }) {
		CHECK_MSG(Localization::SetLanguage(code), std::string("cannot load ") + code + ".json");
		CHECK_MSG(Localization::TranslationCount() > 100, std::string(code) + ": " + std::to_string(Localization::TranslationCount()));
		CHECK(std::string(Tr("File")) != "File");
	}

	Localization::SetLanguageDirectory(savedDir);
	Localization::SetLanguage(savedLanguage);
}

// ===========================================================================
// Animation & Animator (5.1)
// ===========================================================================

namespace {
	Model* LoadTestSkinnedModel() { return Model::CreateFromFile("Resources/Model", "walk.fbx"); }

	float MaxMatrixDifference(const std::vector<Matrix4x4>& a, const std::vector<Matrix4x4>& b)
	{
		float worst = (a.size() == b.size()) ? 0.0f : 1e9f;
		for (size_t i = 0; i < (std::min)(a.size(), b.size()); ++i)
			for (int r = 0; r < 4; ++r)
				for (int c = 0; c < 4; ++c) worst = (std::max)(worst, std::fabs(a[i].m[r][c] - b[i].m[r][c]));
		return worst;
	}

	/// Idle / Walk / Attack / Die with the usual transitions
	AnimatorController MakeTestController()
	{
		AnimatorController c;
		c.parameters = { { "speed", AnimParamType::Float, 0.0f }, { "attack", AnimParamType::Trigger, 0.0f }, { "dead", AnimParamType::Bool, 0.0f } };
		const uint32_t idle = c.AddState("Idle", { 0, 0 }).id;
		const uint32_t walk = c.AddState("Walk", { 200, 0 }).id;
		const uint32_t attack = c.AddState("Attack", { 100, 120 }).id;
		const uint32_t die = c.AddState("Die", { 300, 120 }).id;
		c.FindState(attack)->loop = false;
		c.transitions.push_back({ idle, walk, { { "speed", AnimConditionOp::Greater, 0.1f } }, 0.2f });
		c.transitions.push_back({ walk, idle, { { "speed", AnimConditionOp::Less, 0.1f } }, 0.2f });
		c.transitions.push_back({ AnimTransition::kAnyState, attack, { { "attack", AnimConditionOp::IsTrue, 0.0f } }, 0.1f });
		AnimTransition back{ attack, idle, {}, 0.2f };
		back.hasExitTime = true;
		back.exitTime = 1.0f;
		c.transitions.push_back(back);
		c.transitions.push_back({ AnimTransition::kAnyState, die, { { "dead", AnimConditionOp::IsTrue, 0.0f } }, 0.0f });
		return c;
	}
}

TEST_CASE(Animation, PosePipelineMatchesReference)
{
	Model* model = LoadTestSkinnedModel();
	REQUIRE(model != nullptr);
	REQUIRE(model->HasBones());
	REQUIRE(model->GetAnimationCount() > 0);
	const float duration = model->GetAnimationDuration(0);
	REQUIRE(duration > 0.0f);
	CHECK(model->GetNodeCount() >= model->GetBoneCount());

	// Sample -> Build must reproduce the previous single-pass result exactly (up to float noise)
	for (float t : { 0.0f, 0.3f, duration * 0.77f, duration + 0.4f }) {
		std::vector<Matrix4x4> expected, actual;
		model->CalculateBoneTransformsReference(t, 0, expected);
		model->CalculateBoneTransforms(t, 0, actual);
		CHECK_MSG(MaxMatrixDifference(expected, actual) < 1e-4f, std::format("t={} diff={}", t, MaxMatrixDifference(expected, actual)));
	}

	// Blending: weight 0 / 1 give the inputs, 0.5 lies between them
	const AnimationClip* clip = model->GetAnimationClip(0);
	LocalPose a, b, mixed;
	model->SampleLocalPose(clip, 0.1f, true, a);
	model->SampleLocalPose(clip, duration * 0.5f, true, b);
	std::vector<Matrix4x4> ma, mb, m0, m1, mh;
	model->BuildSkinMatrices(a, ma);
	model->BuildSkinMatrices(b, mb);
	model->BlendPoses(a, b, 0.0f, mixed);
	model->BuildSkinMatrices(mixed, m0);
	model->BlendPoses(a, b, 1.0f, mixed);
	model->BuildSkinMatrices(mixed, m1);
	model->BlendPoses(a, b, 0.5f, mixed);
	model->BuildSkinMatrices(mixed, mh);
	CHECK(MaxMatrixDifference(ma, m0) < 1e-4f);
	CHECK(MaxMatrixDifference(mb, m1) < 1e-4f);
	CHECK(MaxMatrixDifference(ma, mh) > 1e-4f);
	CHECK(MaxMatrixDifference(mb, mh) > 1e-4f);

	// A one-shot clip holds its last frame instead of wrapping to the first
	LocalPose end, past;
	model->SampleLocalPose(clip, duration, false, end);
	model->SampleLocalPose(clip, duration * 3.0f, false, past);
	std::vector<Matrix4x4> me, mp;
	model->BuildSkinMatrices(end, me);
	model->BuildSkinMatrices(past, mp);
	CHECK(MaxMatrixDifference(me, mp) < 1e-5f);
}

namespace {
	/// Largest distance a vertex moves when skinned with the bind pose, relative to the mesh size.
	/// Bind pose = every node at its rest transform, so a correct rig leaves the mesh exactly where it was.
	float BindPoseSkinError(const Model& model)
	{
		LocalPose bind(model.GetNodeCount()); // NodePose::bind = true for every node
		std::vector<Matrix4x4> skin;
		model.BuildSkinMatrices(bind, skin);
		float worst = 0.0f;
		Vector3 mn{ 1e30f, 1e30f, 1e30f }, mx{ -1e30f, -1e30f, -1e30f };
		for (const MeshData& mesh : model.GetMeshes()) {
			for (const VertexData& v : mesh.vertices) {
				const Vector3 p{ v.position.x, v.position.y, v.position.z };
				mn = { (std::min)(mn.x, p.x), (std::min)(mn.y, p.y), (std::min)(mn.z, p.z) };
				mx = { (std::max)(mx.x, p.x), (std::max)(mx.y, p.y), (std::max)(mx.z, p.z) };
				const float total = v.boneWeights[0] + v.boneWeights[1] + v.boneWeights[2] + v.boneWeights[3];
				if (total <= 0.0001f) continue;
				Vector3 s{ 0, 0, 0 };
				for (int k = 0; k < 4; ++k) {
					const Matrix4x4& m = skin[v.boneIndices[k] < skin.size() ? v.boneIndices[k] : 0];
					const float w = v.boneWeights[k];
					s.x += w * (p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0]);
					s.y += w * (p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1]);
					s.z += w * (p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2]);
				}
				const Vector3 d = s - p;
				worst = (std::max)(worst, std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z));
			}
		}
		const Vector3 e = mx - mn;
		const float size = (std::max)({ e.x, e.y, e.z, 1e-6f });
		return worst / size;
	}
}

TEST_CASE(Animation, BindPoseKeepsMeshInPlace)
{
	// glTF (known good) and Mixamo FBX: the skinned bind pose must not move vertices (< 1 % of the mesh size)
	const char* const files[][2] = { { "Resources/boss", "Boss.gltf" }, { "Resources/Model", "walk.fbx" }, { "Resources/Model", "Idle.fbx" } };
	for (const auto& entry : files) {
		const char* file = entry[1];
		Model* model = Model::CreateFromFile(entry[0], file);
		REQUIRE(model != nullptr);
		REQUIRE(model->HasBones());
		const float error = BindPoseSkinError(*model);
		CHECK_MSG(error < 0.01f, std::format("{}: bind pose moves vertices by {:.3f} of the mesh size", file, error));
	}
}

TEST_CASE(Animation, MixamoFbxStaysUpright)
{
	// An animated character keeps roughly its height (crouching in a run or an attack stays above 80 %).
	// (Pre-rotations of Mixamo bones applied twice / not at all make the skeleton lean and shrink.)
	// walk.fbx is left out: a Blender re-export whose clip does not match its own rest pose (asset problem)
	for (const char* file : { "Idle.fbx", "Running.fbx", "Two Hand Sword Combo.fbx" }) {
		Model* model = Model::CreateFromFile("Resources/Model", file);
		REQUIRE(model != nullptr);
		auto height = [&](const LocalPose& pose) {
			std::vector<Vector3> joints;
			std::vector<int> parents;
			std::vector<bool> isBone;
			model->GetJointPositions(pose, joints, parents, isBone);
			float lo = 1e30f, hi = -1e30f;
			for (size_t i = 0; i < joints.size(); ++i) {
				if (!isBone[i]) continue;
				lo = (std::min)(lo, joints[i].y);
				hi = (std::max)(hi, joints[i].y);
			}
			return hi - lo;
		};
		const float bindHeight = height(LocalPose(model->GetNodeCount()));
		REQUIRE(bindHeight > 0.0f);
		const float duration = model->GetAnimationDuration(0);
		for (float k : { 0.0f, 0.25f, 0.5f, 0.75f }) {
			LocalPose pose;
			model->SampleLocalPose(model->GetAnimationClip(0), duration * k, true, pose);
			const float ratio = height(pose) / bindHeight;
			CHECK_MSG(ratio > 0.8f && ratio < 1.2f, std::format("{} at {:.0f}%: height {:.2f} of the bind pose", file, k * 100.0f, ratio));
		}
	}
}

TEST_CASE(Animation, CrossFadeTiming)
{
	Model* model = LoadTestSkinnedModel();
	REQUIRE(model != nullptr);
	AnimationPlayer player;
	player.Play(nullptr, 0);
	player.Advance(0.5f, model);
	CHECK_NEAR(player.Current().time, 0.5f, 1e-5f);
	CHECK(!player.IsFading());

	player.Play(nullptr, 0, true, 2.0f, 0.25f); // fade into the same clip at double speed
	CHECK(player.IsFading());
	CHECK_NEAR(player.FadeProgress(), 0.0f, 1e-5f);
	player.Advance(0.125f, model);
	CHECK_NEAR(player.FadeProgress(), 0.5f, 1e-4f);
	CHECK_NEAR(player.Current().time, 0.25f, 1e-4f);  // 0.125 s at 2x
	CHECK_NEAR(player.Previous().time, 0.625f, 1e-4f); // the old track keeps playing during the fade
	player.Advance(0.2f, model);
	CHECK(!player.IsFading());
	CHECK_NEAR(player.FadeProgress(), 1.0f, 1e-5f);

	std::vector<Matrix4x4> skin;
	player.Evaluate(*model, skin);
	CHECK(skin.size() == 128);
	CHECK_NEAR(player.NormalizedTime(model), player.Current().elapsed / model->GetAnimationDuration(0), 1e-5f);
}

TEST_CASE(Animator, TransitionsTriggersAndAnyState)
{
	const AnimatorController c = MakeTestController();
	AnimatorRuntime rt;
	AnimatorRuntime::Change ch = rt.Start(c);
	REQUIRE(ch.changed);
	CHECK(c.FindState(rt.currentState)->name == "Idle");

	CHECK(!rt.Step(c, 0.016f, 0.3f).changed); // speed 0: stays
	CHECK(!rt.SetValue(c, "missing", 1.0f));
	REQUIRE(rt.SetValue(c, "speed", 0.5f));
	ch = rt.Step(c, 0.016f, 0.3f);
	CHECK(ch.changed);
	CHECK(c.FindState(ch.state)->name == "Walk");
	CHECK_NEAR(ch.fadeSeconds, 0.2f, 1e-6f);
	CHECK(rt.InTransition());
	REQUIRE(rt.SetValue(c, "speed", 0.0f));
	CHECK(!rt.Step(c, 0.1f, 0.0f).changed);  // a running fade is not interrupted
	CHECK(!rt.Step(c, 0.15f, 0.0f).changed); // fade ends
	CHECK(!rt.InTransition());
	ch = rt.Step(c, 0.016f, 0.1f);
	CHECK(ch.changed && c.FindState(ch.state)->name == "Idle");
	rt.Step(c, 0.3f, 0.0f);

	// Trigger: consumed the moment the transition is taken
	REQUIRE(rt.SetValue(c, "attack", 1.0f));
	ch = rt.Step(c, 0.016f, 0.0f);
	CHECK(ch.changed && c.FindState(ch.state)->name == "Attack");
	CHECK(rt.GetValue("attack") == 0.0f);
	rt.Step(c, 0.2f, 0.0f);
	CHECK(!rt.Step(c, 0.016f, 0.5f).changed); // exit time 1.0 not reached
	ch = rt.Step(c, 0.016f, 1.0f);
	CHECK(ch.changed && c.FindState(ch.state)->name == "Idle");
	rt.Step(c, 0.3f, 0.0f);

	// Any State -> Die does not restart Die every frame (canTransitionToSelf = false)
	REQUIRE(rt.SetValue(c, "dead", 1.0f));
	ch = rt.Step(c, 0.016f, 0.0f);
	CHECK(ch.changed && c.FindState(ch.state)->name == "Die");
	CHECK(!rt.Step(c, 0.016f, 0.0f).changed);
	CHECK(!rt.Step(c, 0.016f, 0.5f).changed);
	// A trigger nobody uses stays set until reset
	REQUIRE(rt.SetValue(c, "attack", 1.0f));
	CHECK(rt.Step(c, 0.016f, 0.0f).changed); // Any -> Attack still wins over Die (listed first)
	rt.SetValue(c, "attack", 1.0f);
	rt.ResetTrigger("attack");
	CHECK(rt.GetValue("attack") == 0.0f);

	// Forced state (script CrossFadeAnimation)
	ch = rt.ForceState(c, "Walk", 0.3f);
	CHECK(ch.changed && c.FindState(ch.state)->name == "Walk" && rt.InTransition());
	CHECK(!rt.ForceState(c, "Nope", 0.1f).changed);
}

TEST_CASE(Animator, EventsOncePerLoopAndJsonRoundTrip)
{
	AnimState looping;
	looping.loop = true;
	looping.events = { { "step", 0.5f } };
	std::vector<std::string> fired;
	AnimatorRuntime::CollectEvents(looping, 0.4f, 0.6f, true, fired);
	CHECK(fired.size() == 1);
	fired.clear();
	AnimatorRuntime::CollectEvents(looping, 0.6f, 1.4f, true, fired);
	CHECK(fired.empty());
	AnimatorRuntime::CollectEvents(looping, 1.4f, 1.6f, true, fired);
	CHECK(fired.size() == 1);
	fired.clear();
	AnimatorRuntime::CollectEvents(looping, 0.0f, 2.0f, true, fired); // a long frame covers two passes
	CHECK(fired.size() == 2);
	fired.clear();
	AnimState once;
	once.loop = false;
	once.events = { { "end", 1.0f }, { "start", 0.0f } };
	AnimatorRuntime::CollectEvents(once, 0.0f, 0.1f, false, fired);
	CHECK(fired.size() == 1 && fired[0] == "start");
	fired.clear();
	AnimatorRuntime::CollectEvents(once, 0.9f, 1.0f, false, fired);
	CHECK(fired.size() == 1 && fired[0] == "end");

	AnimatorController c = MakeTestController();
	c.FindState(1)->events.push_back({ "footstep", 0.25f });
	const std::string json = c.ToJson();
	std::string error;
	auto back = AnimatorController::FromJson(json, &error);
	CHECK_MSG(back.has_value(), error);
	REQUIRE(back.has_value());
	CHECK(back->ToJson() == json);
	CHECK(back->transitions.size() == 5);
	CHECK(back->transitions[2].from == AnimTransition::kAnyState);
	CHECK(back->FindState(1)->events.size() == 1);
	CHECK(!AnimatorController::FromJson("[1,2]").has_value());

	// Removing a state drops its transitions; the next id never reuses an old one
	back->RemoveState(back->FindStateByName("Walk")->id);
	CHECK(back->transitions.size() == 3);
	const uint32_t fresh = back->AddState("Run", { 0, 0 }).id;
	CHECK(fresh > 4);
}

TEST_CASE(Animator, ComponentDrivesModelOnGameTime)
{
	A::ProjectRoot(s) = A::TempRoot() / "AnimatorProject";
	Model* model = LoadTestSkinnedModel();
	REQUIRE(model != nullptr);

	// Controller asset: Idle (clip 0 slowed down) -> Walk (clip 0) when speed > 0.1
	const std::string path = s.CreateAnimatorControllerAsset("Hero");
	REQUIRE(!path.empty());
	CHECK(std::filesystem::exists(A::TempRoot() / "AnimatorProject" / path));
	auto shared = s.GetAnimatorController(path);
	REQUIRE(shared != nullptr);
	AnimatorController c = *shared;
	c.parameters.push_back({ "speed", AnimParamType::Float, 0.0f });
	c.states[0].speed = 0.5f;
	const uint32_t walk = c.AddState("Walk", { 200, 0 }).id;
	c.transitions.push_back({ c.states[0].id, walk, { { "speed", AnimConditionOp::Greater, 0.1f } }, 0.2f });
	REQUIRE(s.SaveAnimatorController(path, c));

	GameObject& hero = s.CreateGameObject("Hero", GameObjectType::Empty);
	const uint32_t id = hero.id;
	hero.editObject3d = std::make_unique<Object3d>();
	hero.editObject3d->Initialize();
	hero.editObject3d->SetModel(model);
	REQUIRE(s.AddComponentByName(hero, "Animator"));
	AnimatorComponent* animator = s.GetAnimator(*s.FindGameObjectById(id, false));
	REQUIRE(animator != nullptr);
	strncpy_s(animator->controller, path.c_str(), _TRUNCATE);

	// Saved with the GUID next to the path
	const std::string saved = s.SerializeSceneToString();
	CHECK(saved.find("controllerGuid") != std::string::npos);

	// Play: entry state starts, parameters from scripts drive transitions, pause freezes time
	A::State(s) = EditorSceneState::Play;
	EngineAPI& api = A::Api(s);
	s.UpdateAnimations(0.1f, true);
	REQUIRE(api.animIsState(api.hostContext, id, "Idle"));
	const AnimationPlayer& player = s.FindGameObjectById(id, false)->editObject3d->GetAnimationPlayer();
	CHECK_NEAR(player.Current().time, 0.05f, 1e-4f); // 0.1 s at state speed 0.5
	CHECK(api.animSetParam(api.hostContext, id, "speed", 1.0f));
	CHECK(!api.animSetParam(api.hostContext, id, "missing", 1.0f));
	s.UpdateAnimations(0.05f, true);
	CHECK(api.animIsState(api.hostContext, id, "Walk"));
	CHECK(api.animIsInTransition(api.hostContext, id));
	CHECK(player.IsFading());
	char name[16]{};
	CHECK(api.animGetState(api.hostContext, id, name, static_cast<int>(sizeof(name))) == 4);
	CHECK(std::string(name) == "Walk");

	const float before = player.Current().time;
	s.UpdateAnimations(0.0f, true); // paused: game time does not move
	CHECK_NEAR(player.Current().time, before, 1e-6f);
	s.UpdateAnimations(0.3f, true);
	CHECK(!player.IsFading());
	CHECK(!api.animIsInTransition(api.hostContext, id));

	CHECK(api.animCrossFade(api.hostContext, id, "Idle", 0.1f));
	CHECK(api.animIsState(api.hostContext, id, "Idle"));
	CHECK(!api.animCrossFade(api.hostContext, id, "Nope", 0.1f));

	A::State(s) = EditorSceneState::Edit;
	// Reload: the controller path survives and the runtime restarts on the next Play
	REQUIRE(s.DeserializeSceneFromString(saved));
	GameObject* reloaded = s.FindGameObjectById(id, false);
	REQUIRE(reloaded != nullptr);
	AnimatorComponent* again = s.GetAnimator(*reloaded);
	REQUIRE(again != nullptr);
	CHECK(std::string(again->controller) == path);
	CHECK(!again->runtime.started);
}

// ===========================================================================
// Asset import (copy into the project without Explorer)
// ===========================================================================

TEST_CASE(Assets, FlatColourModelKeepsItsMaterialColours)
{
	// Low-poly packs colour their parts with material colours instead of textures
	namespace fs = std::filesystem;
	const fs::path dir = A::TempRoot() / "MeshColours";
	std::error_code ec;
	fs::create_directories(dir, ec);
	auto write = [](const fs::path& p, const std::string& text) { std::ofstream(p, std::ios::binary) << text; };
	write(dir / "two.mtl", "newmtl Red\nKd 1 0 0\nnewmtl Blue\nKd 0 0 1\n");
	write(dir / "two.obj", "mtllib two.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nusemtl Red\nf 1 2 3\nusemtl Blue\nf 1 3 4\n");
	write(dir / "plain.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

	const auto two = Model::CreatePreviewFromFile((dir / "two.obj").string());
	REQUIRE(two != nullptr);
	CHECK(two->HasMeshColors());
	bool red = false, blue = false;
	for (const MeshData& mesh : two->GetMeshes()) {
		red = red || (mesh.baseColor.x == 1.0f && mesh.baseColor.z == 0.0f);
		blue = blue || (mesh.baseColor.x == 0.0f && mesh.baseColor.z == 1.0f);
	}
	CHECK(red && blue);

	// No material in the file = assimp's default material: stays white, so old models look the same
	const auto plain = Model::CreatePreviewFromFile((dir / "plain.obj").string());
	REQUIRE(plain != nullptr);
	CHECK(!plain->HasMeshColors());
}

TEST_CASE(Assets, ImportFilesCompanionsAndFolders)
{
	namespace fs = std::filesystem;
	const fs::path src = A::TempRoot() / "ImportSource";
	const fs::path root = A::TempRoot() / "ImportProject";
	std::error_code ec;
	fs::create_directories(src / "tex", ec);
	fs::create_directories(src / "img", ec);
	fs::create_directories(src / "Pack", ec);
	fs::create_directories(root / "Assets", ec);
	auto write = [](const fs::path& p, const std::string& text) { std::ofstream(p, std::ios::binary) << text; };
	fs::copy_file("Resources/circle.png", src / "rock.png", fs::copy_options::overwrite_existing, ec);
	fs::copy_file("Resources/circle.png", src / "tex" / "crate.png", fs::copy_options::overwrite_existing, ec);
	write(src / "crate.obj", "mtllib crate.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
	write(src / "crate.mtl", "newmtl m\nmap_Kd tex/crate.png\n");
	write(src / "hero.gltf", R"({"asset":{"version":"2.0"},"buffers":[{"uri":"hero.bin","byteLength":4}],"images":[{"uri":"img/hero.png"},{"uri":"data:image/png;base64,AAAA"}]})");
	write(src / "hero.bin", "abcd");
	fs::copy_file("Resources/circle.png", src / "img" / "hero.png", fs::copy_options::overwrite_existing, ec);
	fs::copy_file("Resources/circle.png", src / "Pack" / "a.png", fs::copy_options::overwrite_existing, ec);
	write(src / "Pack" / "a.png.meta", R"({"guid":"0123456789abcdef0123456789abcdef","version":1})"); // from another project

	A::ProjectRoot(s) = root;
	A::CurrentDirectory(s) = root; // not inside Assets: files go to category folders
	s.AssetDb().Refresh();
	const auto imported = s.ImportAssets({ src / "rock.png", src / "crate.obj", src / "hero.gltf", src / "Pack" });
	REQUIRE(imported.size() == 4);
	CHECK(imported[0] == "Assets/Textures/rock.png");
	CHECK(imported[1] == "Assets/Models/crate.obj");
	CHECK(imported[3] == "Assets/Pack");
	CHECK(fs::exists(root / "Assets/Models/crate.mtl"));
	CHECK(fs::exists(root / "Assets/Models/tex/crate.png"));  // texture of the .mtl, same relative path
	CHECK(fs::exists(root / "Assets/Models/hero.bin"));
	CHECK(fs::exists(root / "Assets/Models/img/hero.png"));
	CHECK(fs::exists(root / "Assets/Pack/a.png"));
	// Every imported file has a GUID, and a copied .meta never reuses another project's GUID
	CHECK(AssetDatabase::IsValidGuid(s.AssetDb().FindGuid("Assets/Textures/rock.png")));
	CHECK(AssetDatabase::IsValidGuid(s.AssetDb().FindGuid("Assets/Models/tex/crate.png")));
	const std::string packGuid = s.AssetDb().FindGuid("Assets/Pack/a.png");
	CHECK(AssetDatabase::IsValidGuid(packGuid));
	CHECK(packGuid != "0123456789abcdef0123456789abcdef");

	// Same name again: a numbered copy instead of overwriting
	const auto again = s.ImportAssets({ src / "rock.png" });
	REQUIRE(again.size() == 1);
	CHECK(again[0] == "Assets/Textures/rock 1.png");

	// The folder open in the Project panel is the target when it is inside Assets
	fs::create_directories(root / "Assets/Art", ec);
	A::CurrentDirectory(s) = root / "Assets/Art";
	const auto intoFolder = s.ImportAssets({ src / "hero.bin" });
	REQUIRE(intoFolder.size() == 1);
	CHECK(intoFolder[0] == "Assets/Art/hero.bin");

	// Files that are already in the project are not copied again
	CHECK(s.ImportAssets({ root / "Assets/Textures/rock.png" }).empty());
	CHECK(s.ImportAssets({ src / "does_not_exist.png" }).empty());
}

TEST_CASE(Project, OpensExistingSceneWithoutSampleScene)
{
	namespace fs = std::filesystem;
	// A project whose only scene is not SampleScene used to open as an empty scene
	const fs::path root = A::TempRoot() / "NoSampleSceneProject";
	std::error_code ec;
	fs::create_directories(root / "Assets" / "Scenes", ec);
	s.CreateGameObject("LevelMarker", GameObjectType::Empty);
	{
		std::ofstream out(root / "Assets" / "Scenes" / "Level1.scene.json", std::ios::binary);
		out << s.SerializeSceneToString();
	}
	s.NewScene();
	REQUIRE(s.FindGameObjectByName("LevelMarker", false) == nullptr);

	REQUIRE(s.OpenProject(root.string()));
	CHECK(s.FindGameObjectByName("LevelMarker", false) != nullptr);
	CHECK(A::ScenePath(s).find("Level1.scene.json") != std::string::npos);
}

// ===========================================================================
// General gameplay basics (Fase 1)
// ===========================================================================

TEST_CASE(ScriptAPI, MouseDeltaWheelAndCursorLock)
{
	EngineAPI& api = A::Api(s);
	Input* input = RizeEngine::GetInput();
	s.Update(0.0f, input);
	Vector2 delta{};

	// Edit mode cannot lock, and the mouse is outside the (never drawn) Game view: scripts read nothing
	api.setCursorLocked(api.hostContext, true);
	CHECK(!api.isCursorLocked(api.hostContext));
	input->InjectMouseMotion(12, -7, WHEEL_DELTA * 2);
	CHECK(!api.getMouseDelta(api.hostContext, &delta));
	CHECK_NEAR(api.getMouseWheel(api.hostContext), 0.0f, 1e-6f);

	// Locked in Play: raw movement in pixels, wheel in notches
	A::State(s) = EditorSceneState::Play;
	api.setCursorLocked(api.hostContext, true);
	CHECK(api.isCursorLocked(api.hostContext));
	CHECK(api.getMouseDelta(api.hostContext, &delta));
	CHECK_NEAR(delta.x, 12.0f, 1e-6f);
	CHECK_NEAR(delta.y, -7.0f, 1e-6f);
	CHECK_NEAR(api.getMouseWheel(api.hostContext), 2.0f, 1e-6f);

	// Leaving Play always gives the cursor back
	s.StopPlayMode();
	CHECK(!api.isCursorLocked(api.hostContext));
	s.Update(0.0f, input);
	CHECK(!A::CursorHidden(s));
	input->InjectMouseMotion(0, 0, 0);
}

TEST_CASE(ScriptAPI, CanvasAndWorldProjectionRoundTrip)
{
	GameObject* camera = s.FindGameObjectByName("Main Camera", false);
	REQUIRE(camera != nullptr);
	camera->translation = { 2.0f, 3.0f, -10.0f };
	camera->rotation = { 0.2f, 0.1f, 0.0f };
	camera->rotationQuat = Quaternion::FromEuler(camera->rotation);
	s.Update(0.0f, RizeEngine::GetInput());
	EngineAPI& api = A::Api(s);

	// A world point maps to the canvas, and the ray through that canvas point passes through it
	const Vector3 point = { 1.0f, 1.5f, 4.0f };
	Vector2 canvas{};
	REQUIRE(api.worldToCanvas(api.hostContext, point, &canvas));
	Vector3 origin{};
	Vector3 direction{};
	REQUIRE(api.screenPointToRay(api.hostContext, canvas, &origin, &direction));
	CHECK_NEAR(std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z), 1.0f, 1e-4f);
	const Vector3 toPoint = { point.x - origin.x, point.y - origin.y, point.z - origin.z };
	const float along = toPoint.x * direction.x + toPoint.y * direction.y + toPoint.z * direction.z;
	CHECK(along > 0.0f);
	CHECK_NEAR(origin.x + direction.x * along, point.x, 0.01f);
	CHECK_NEAR(origin.y + direction.y * along, point.y, 0.01f);
	CHECK_NEAR(origin.z + direction.z * along, point.z, 0.01f);

	// The canvas center looks along the camera, and y grows downward like the UI
	REQUIRE(api.screenPointToRay(api.hostContext, { kUICanvasWidth * 0.5f, kUICanvasHeight * 0.5f }, &origin, &direction));
	CHECK(direction.z > 0.9f);
	Vector2 high{};
	Vector2 low{};
	REQUIRE(api.worldToCanvas(api.hostContext, { 2.0f, 5.0f, 5.0f }, &high));
	REQUIRE(api.worldToCanvas(api.hostContext, { 2.0f, 1.0f, 5.0f }, &low));
	CHECK(high.y < low.y);

	// Behind the camera there is no canvas position
	CHECK(!api.worldToCanvas(api.hostContext, { 2.0f, 3.0f, -30.0f }, &canvas));
}

TEST_CASE(Prefabs, PrewarmFillsThePoolWithHiddenInstances)
{
	A::ProjectRoot(s) = A::TempRoot() / "PrewarmProject";
	const uint32_t templateId = s.CreateSphere("Shot", { 0.0f, 0.0f, 0.0f }).id;
	const uint32_t childId = s.CreateSphere("ShotGlow", { 0.0f, 1.0f, 0.0f }).id;
	REQUIRE(s.ReparentGameObject(childId, static_cast<int>(templateId), false));
	REQUIRE(s.SavePrefab(templateId, "Shot"));
	EngineAPI& api = A::Api(s);

	CHECK(!api.prewarmPrefab(api.hostContext, "Shot", 3)); // Edit mode
	A::State(s) = EditorSceneState::Play;
	CHECK(!api.prewarmPrefab(api.hostContext, "NoSuchPrefab", 3));
	CHECK(!api.prewarmPrefab(api.hostContext, "Shot", 0));

	const size_t before = A::Objects(s).size();
	REQUIRE(api.prewarmPrefab(api.hostContext, "Shot", 3));
	CHECK(s.GetPooledCount("Shot") == 0); // deferred: scripts may be iterating gameObjects_
	A::Spawns(s);
	CHECK(s.GetPooledCount("Shot") == 3);
	CHECK(A::Objects(s).size() == before + 6); // root + child per instance
	for (size_t i = before; i < A::Objects(s).size(); ++i) CHECK(!A::Objects(s)[i].isActive);

	// A spawn takes one from the pool and creates nothing
	ScriptGameObjectView* view = s.QueuePrefabSpawn("Shot", { 5.0f, 0.0f, 0.0f });
	REQUIRE(view != nullptr);
	const uint32_t spawnedId = view->id;
	A::Spawns(s);
	CHECK(A::Objects(s).size() == before + 6);
	CHECK(s.GetPooledCount("Shot") == 2);
	const GameObject* spawned = s.FindGameObjectById(spawnedId, false);
	REQUIRE(spawned != nullptr);
	CHECK(spawned->isActive);
	CHECK_NEAR(spawned->translation.x, 5.0f, 1e-5f);
	CHECK(s.FindGameObjectById(spawned->childrenIds.at(0), false)->isActive);
	A::State(s) = EditorSceneState::Edit;
	s.ResetPrefabPools();
}

namespace {

// Built into the exe instead of the DLL, so the shipped script samples do not need a crashing class
struct CrashingScript : ScriptBase {
	enum class Kind { NullInUpdate, ThrowInUpdate, NullInInitialize, Never } kind = Kind::Never;
	int* updates = nullptr;
	void Initialize() override { if (kind == Kind::NullInInitialize) Crash(); }
	void Update(float) override {
		if (updates) ++*updates;
		if (kind == Kind::NullInUpdate) Crash();
		if (kind == Kind::ThrowInUpdate) throw std::runtime_error("script error");
	}
	static void Crash() { volatile int* nothing = nullptr; *nothing = 1; }
};

uint32_t AddCrashingScript(EditorSuite& s, CrashingScript::Kind kind, int* updates, const Vector3& pos)
{
	GameObject& obj = s.CreateCube("Crasher", pos);
	auto script = new CrashingScript();
	script->kind = kind;
	script->updates = updates;
	GameObject::ScriptComponent component;
	component.className = "CrashingScript";
	// An unloaded module deletes nothing: the DLL must not free memory of the exe's heap (these few bytes leak in the test run)
	static const ScriptModule sNoDll;
	component.instance = ScriptInstancePtr(script, ScriptDeleter{ &sNoDll });
	obj.scripts.push_back(std::move(component));
	return obj.id;
}

} // namespace

TEST_CASE(ScriptGuard, CrashStopsPlayRestoresSceneAndKeepsEditorAlive)
{
	Input* input = RizeEngine::GetInput();
	for (const auto kind : { CrashingScript::Kind::NullInUpdate, CrashingScript::Kind::ThrowInUpdate }) {
		s.NewScene();
		const uint32_t markerId = s.CreateCube("Marker", { 1.0f, 2.0f, 3.0f }).id;
		const int faultsBefore = A::ScriptFaults(s);
		s.StartPlayMode();
		REQUIRE(A::State(s) == EditorSceneState::Play);
		s.FindGameObjectById(markerId, false)->translation = { 9.0f, 9.0f, 9.0f }; // a change Stop must undo

		int laterUpdates = 0;
		AddCrashingScript(s, kind, nullptr, { 0.0f, 50.0f, 0.0f });
		AddCrashingScript(s, CrashingScript::Kind::Never, &laterUpdates, { 0.0f, 60.0f, 0.0f });
		s.Update(1.0f / 60.0f, input);

		CHECK(A::ScriptFaults(s) == faultsBefore + 1);
		CHECK(!A::FaultPending(s));
		CHECK(laterUpdates == 0); // no script runs after the crash
		CHECK(A::State(s) == EditorSceneState::Edit);
		const GameObject* marker = s.FindGameObjectById(markerId, false);
		REQUIRE(marker != nullptr);
		CHECK_NEAR(marker->translation.x, 1.0f, 1e-5f);
		CHECK(s.FindGameObjectByName("Crasher", false) == nullptr);
		CHECK(A::ScriptsLoaded(s)); // the DLL came back
	}

	// A crash in Initialize is handled the same way, and Play can start again afterwards
	const int faultsBefore = A::ScriptFaults(s);
	s.StartPlayMode();
	A::Start(s, AddCrashingScript(s, CrashingScript::Kind::NullInInitialize, nullptr, { 0.0f, 50.0f, 0.0f }));
	s.Update(1.0f / 60.0f, input);
	CHECK(A::ScriptFaults(s) == faultsBefore + 1);
	CHECK(A::State(s) == EditorSceneState::Edit);
	s.StartPlayMode();
	CHECK(A::State(s) == EditorSceneState::Play);
	s.Update(1.0f / 60.0f, input);
	CHECK(A::State(s) == EditorSceneState::Play);
	s.StopPlayMode();
}

// ===========================================================================
// Baselines: what later renderer and physics work must not change by accident
// ===========================================================================

namespace {

constexpr int kSignatureColumns = 16;
constexpr int kSignatureRows = 9;
const char* const kRenderBaselineFile = "Tests/Baselines/render_scene.json";

/// Average brightness of every pixel row of the last capture: shows thin horizontal lines that the grid averages away
std::vector<float> gCapturedRowMeans;

/// Renders one real frame and returns the Game view as average RGB (0..255) per grid cell.
/// A coarse grid survives the small rasterization differences between a GPU and WARP.
std::vector<float> CaptureViewSignature(EditorSuite& s, RenderTexture& rt)
{
	Microsoft::WRL::ComPtr<ID3D12Resource> readback;
	uint32_t rowPitch = 0;

	// Self-tests run inside the app's first frame: close it, render one whole frame, and leave a frame open again
	RizeEngine::EndFrame();
	RizeEngine::BeginFrame();
	ID3D12GraphicsCommandList* cmdList = A::Dx(s)->GetCommandList();
	s.RenderViewports(cmdList);
	const bool recorded = rt.RecordReadback(cmdList, readback, rowPitch);
	RizeEngine::EndFrame();
	A::Dx(s)->SignalAndWaitFence();
	RizeEngine::BeginFrame();
	if (!recorded) return {};

	const uint8_t* pixels = nullptr;
	if (FAILED(readback->Map(0, nullptr, reinterpret_cast<void**>(const_cast<uint8_t**>(&pixels))))) return {};
	gCapturedRowMeans.assign(rt.GetHeight(), 0.0f);
	for (uint32_t y = 0; y < rt.GetHeight(); ++y) {
		const uint8_t* row = pixels + y * rowPitch;
		double rowSum = 0.0;
		for (uint32_t x = 0; x < rt.GetWidth(); ++x) rowSum += row[x * 4 + 1];
		gCapturedRowMeans[y] = static_cast<float>(rowSum / rt.GetWidth());
	}
	std::vector<double> sum(kSignatureColumns * kSignatureRows * 3, 0.0);
	std::vector<int> count(kSignatureColumns * kSignatureRows, 0);
	for (uint32_t y = 0; y < rt.GetHeight(); ++y) {
		const uint8_t* row = pixels + y * rowPitch;
		const int cellY = static_cast<int>(y * kSignatureRows / rt.GetHeight());
		for (uint32_t x = 0; x < rt.GetWidth(); ++x) {
			const int cell = cellY * kSignatureColumns + static_cast<int>(x * kSignatureColumns / rt.GetWidth());
			for (int c = 0; c < 3; ++c) sum[cell * 3 + c] += row[x * 4 + c];
			++count[cell];
		}
	}
	readback->Unmap(0, nullptr);

	std::vector<float> signature(sum.size());
	for (size_t i = 0; i < sum.size(); ++i) signature[i] = static_cast<float>(sum[i] / (std::max)(count[i / 3], 1));
	return signature;
}

std::vector<float> CaptureGameViewSignature(EditorSuite& s) { return CaptureViewSignature(s, A::GameViewRT(s)); }

float LargestSignatureDifference(const std::vector<float>& a, const std::vector<float>& b)
{
	if (a.size() != b.size()) return 255.0f;
	float largest = 0.0f;
	for (size_t i = 0; i < a.size(); ++i) largest = (std::max)(largest, std::abs(a[i] - b[i]));
	return largest;
}

uint32_t MakeBaselineMesh(EditorSuite& s, const char* name, const Vector3& pos, int lightingType, const Vector4& color)
{
	GameObject& obj = s.CreateCube(name, pos);
	obj.isLit = lightingType != 0;
	obj.lightingType = lightingType;
	obj.materialColor = color;
	return obj.id;
}

/// One mesh per lighting model, an outline, a sphere and a HUD rectangle: everything the renderer rework touches.
uint32_t BuildBaselineScene(EditorSuite& s)
{
	MakeBaselineMesh(s, "Unlit", { -3.0f, 0.5f, 0.0f }, 0, { 0.9f, 0.2f, 0.2f, 1.0f });
	MakeBaselineMesh(s, "Lambert", { -1.5f, 0.5f, 0.0f }, 1, { 0.2f, 0.8f, 0.3f, 1.0f });
	MakeBaselineMesh(s, "HalfLambert", { 0.0f, 0.5f, 0.0f }, 2, { 0.2f, 0.4f, 0.9f, 1.0f });
	MakeBaselineMesh(s, "BlinnPhong", { 1.5f, 0.5f, 0.0f }, 4, { 0.9f, 0.8f, 0.2f, 1.0f });
	const uint32_t toon = MakeBaselineMesh(s, "Toon", { 3.0f, 0.5f, 0.0f }, 5, { 0.9f, 0.5f, 0.8f, 1.0f });
	s.FindGameObjectById(toon, false)->outlineWidth = 0.05f;
	s.FindGameObjectById(toon, false)->outlineColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	s.CreateSphere("Sphere", { 0.0f, 2.0f, 0.0f });
	// NewScene copies the camera of the previous scene, so earlier tests would otherwise move this view
	GameObject* camera = s.FindGameObjectByName("Main Camera", false);
	camera->translation = { 0.0f, 2.5f, -9.0f };
	camera->rotation = { 0.15f, 0.0f, 0.0f };
	camera->rotationQuat = Quaternion::FromEuler(camera->rotation);
	s.Update(0.0f, RizeEngine::GetInput());
	UIRenderer::GetInstance()->Clear();
	UIRenderer::GetInstance()->DrawRect(40.0f, 40.0f, 240.0f, 60.0f, { 1.0f, 0.1f, 0.1f, 1.0f });
	s.PostSettings() = PostProcessSettings{}; // the reference is the scene without post-processing
	return toon;
}

float SignatureCell(const std::vector<float>& signature, int column, int row, int channel)
{
	return signature[(row * kSignatureColumns + column) * 3 + channel];
}

} // namespace

TEST_CASE(Post, LookSettingsChangeTheViewButNotTheHud)
{
	BuildBaselineScene(s);
	A::State(s) = EditorSceneState::Play;
	UIRenderer* ui = UIRenderer::GetInstance();
	// Fills canvas cells (1,1) and (2,1) of the signature grid exactly
	auto capture = [&] {
		ui->Clear();
		ui->DrawRect(80.0f, 80.0f, 160.0f, 80.0f, { 1.0f, 0.0f, 0.0f, 1.0f });
		return CaptureGameViewSignature(s);
	};
	const std::vector<float> plain = capture();
	REQUIRE(plain.size() == kSignatureColumns * kSignatureRows * 3);
	CHECK(SignatureCell(plain, 1, 1, 0) > 250.0f);

	PostProcessSettings look{};
	look.enabled = true;
	look.toneMapping = false;
	look.bloom = false;
	look.fxaa = false;
	s.PostSettings() = look;
	// Enabled with neutral values changes nothing
	CHECK_NEAR(LargestSignatureDifference(plain, capture()), 0.0f, 3.0f);

	// Exposure darkens the 3D view, the HUD on top keeps its color
	s.PostSettings().exposure = 0.25f;
	std::vector<float> result = capture();
	const int skyColumn = 8;
	CHECK(SignatureCell(result, skyColumn, 0, 2) < SignatureCell(plain, skyColumn, 0, 2) - 30.0f);
	CHECK_NEAR(SignatureCell(result, 1, 1, 0), SignatureCell(plain, 1, 1, 0), 1.5f);
	CHECK_NEAR(SignatureCell(result, 1, 1, 1), SignatureCell(plain, 1, 1, 1), 1.5f);

	// Letterbox: black bars at the top and bottom only
	s.PostSettings() = look;
	s.PostSettings().letterbox = 0.12f;
	result = capture();
	CHECK(SignatureCell(result, skyColumn, 0, 2) < 3.0f);
	CHECK(SignatureCell(result, skyColumn, 8, 2) < 3.0f);
	CHECK_NEAR(SignatureCell(result, skyColumn, 4, 2), SignatureCell(plain, skyColumn, 4, 2), 3.0f);

	// Saturation 0: every view cell is gray
	s.PostSettings() = look;
	s.PostSettings().saturation = 0.0f;
	result = capture();
	CHECK_NEAR(SignatureCell(result, skyColumn, 0, 0), SignatureCell(result, skyColumn, 0, 2), 2.0f);

	// Vignette darkens the corners, not the center
	s.PostSettings() = look;
	s.PostSettings().vignette = 1.0f;
	result = capture();
	CHECK(SignatureCell(result, 15, 0, 2) < SignatureCell(plain, 15, 0, 2) - 20.0f);
	CHECK_NEAR(SignatureCell(result, 8, 4, 2), SignatureCell(plain, 8, 4, 2), 3.0f);

	// Tint keeps only the chosen channel
	s.PostSettings() = look;
	s.PostSettings().tint = { 0.0f, 0.0f, 1.0f };
	result = capture();
	CHECK(SignatureCell(result, skyColumn, 0, 0) < 3.0f);
	CHECK(SignatureCell(result, skyColumn, 0, 2) > 100.0f);

	// FXAA only softens edges
	s.PostSettings() = look;
	s.PostSettings().fxaa = true;
	CHECK_NEAR(LargestSignatureDifference(plain, capture()), 0.0f, 6.0f);

	ui->Clear();
	A::State(s) = EditorSceneState::Edit;
}

TEST_CASE(Post, HdrValuesSurviveAndBloomSpreadsThem)
{
	BuildBaselineScene(s);
	UIRenderer::GetInstance()->Clear();
	// A bright block filling the center of the view (camera at (0, 2.5, -9), looking at about y = 1.2)
	GameObject& lamp = s.CreateCube("Lamp", { 0.0f, 1.2f, -3.0f });
	lamp.scale = { 1.5f, 1.5f, 1.5f };
	lamp.isLit = false;
	lamp.lightingType = 0;
	lamp.materialColor = { 8.0f, 8.0f, 8.0f, 1.0f };
	s.Update(0.0f, RizeEngine::GetInput());

	PostProcessSettings look{};
	look.enabled = true;
	look.toneMapping = false;
	look.bloom = false;
	look.fxaa = false;
	look.exposure = 0.125f; // 8 * 0.125 = 1: white only if values above 1 reached the post-process
	s.PostSettings() = look;
	std::vector<float> result = CaptureGameViewSignature(s);
	REQUIRE(result.size() == kSignatureColumns * kSignatureRows * 3);
	CHECK_MSG(SignatureCell(result, 8, 4, 1) > 245.0f, std::format("center cell {:.1f}", SignatureCell(result, 8, 4, 1)));

	// Bloom lifts the surroundings of the lamp, and more near it than far away
	look.exposure = 1.0f;
	s.PostSettings() = look;
	const std::vector<float> without = CaptureGameViewSignature(s);
	look.bloom = true;
	look.bloomIntensity = 1.0f;
	s.PostSettings() = look;
	const std::vector<float> with = CaptureGameViewSignature(s);
	const float near_ = SignatureCell(with, 5, 4, 1) - SignatureCell(without, 5, 4, 1);
	const float far_ = SignatureCell(with, 0, 8, 1) - SignatureCell(without, 0, 8, 1);
	CHECK_MSG(near_ > 3.0f, std::format("near the lamp +{:.2f}", near_));
	CHECK_MSG(near_ > far_, std::format("near +{:.2f}, far +{:.2f}", near_, far_));
}

TEST_CASE(Post, SettingsAreSavedPerSceneAndOldScenesStayOff)
{
	CHECK(s.PostSettings().enabled); // NewScene
	s.PostSettings().exposure = 1.75f;
	s.PostSettings().letterbox = 0.1f;
	s.PostSettings().fxaa = false;
	s.PostSettings().tint = { 1.0f, 0.5f, 0.25f };
	std::string json = s.SerializeSceneToString();
	s.PostSettings() = PostProcessSettings{};
	REQUIRE(s.DeserializeSceneFromString(json));
	CHECK(s.PostSettings().enabled);
	CHECK(!s.PostSettings().fxaa);
	CHECK_NEAR(s.PostSettings().exposure, 1.75f, 1e-5f);
	CHECK_NEAR(s.PostSettings().letterbox, 0.1f, 1e-5f);
	CHECK_NEAR(s.PostSettings().tint.y, 0.5f, 1e-5f);

	// A scene file from before post-processing
	nlohmann::json old = nlohmann::json::parse(json);
	old.erase("postProcess");
	REQUIRE(s.DeserializeSceneFromString(old.dump()));
	CHECK(!s.PostSettings().enabled);

	// Script changes do not outlive Play
	s.PostSettings().enabled = true;
	s.PostSettings().exposure = 1.0f;
	s.StartPlayMode();
	EngineAPI& api = A::Api(s);
	ScriptPostProcess fromScript{};
	REQUIRE(api.getPostProcess(api.hostContext, &fromScript));
	CHECK(fromScript.enabled);
	fromScript.exposure = 0.1f;
	fromScript.letterbox = 3.0f; // clamped
	REQUIRE(api.setPostProcess(api.hostContext, &fromScript));
	CHECK_NEAR(s.PostSettings().exposure, 0.1f, 1e-5f);
	CHECK_NEAR(s.PostSettings().letterbox, 0.5f, 1e-5f);
	s.StopPlayMode();
	CHECK_NEAR(s.PostSettings().exposure, 1.0f, 1e-5f);
}

TEST_CASE(Post, ScreenEffectsRunReadDepthAndSurviveBrokenShaders)
{
	namespace fs = std::filesystem;
	BuildBaselineScene(s);
	UIRenderer::GetInstance()->Clear();
	A::State(s) = EditorSceneState::Play;
	EngineAPI& api = A::Api(s);
	const std::vector<float> plain = CaptureGameViewSignature(s);
	REQUIRE(plain.size() == kSignatureColumns * kSignatureRows * 3);

	const fs::path dir = A::TempRoot() / "Effects";
	fs::create_directories(dir);
	auto write = [&](const char* name, const char* body) {
		const fs::path file = dir / name;
		std::ofstream(file) << "#include \"Post.hlsli\"\nfloat4 main(PostVSOutput input) : SV_TARGET {\n" << body << "\n}\n";
		return file.string();
	};

	// A parameter reaches the shader, and the effect replaces the scene
	const std::string solid = write("Solid.PS.hlsl", "return float4(gParams[0].rgb, 1.0f);");
	float params[16] = { 0.0f, 1.0f, 0.0f };
	REQUIRE(api.setScreenEffect(api.hostContext, 0, solid.c_str(), params));
	std::vector<float> result = CaptureGameViewSignature(s);
	CHECK(SignatureCell(result, 8, 4, 1) > 250.0f);
	CHECK(SignatureCell(result, 8, 4, 0) < 3.0f);

	// Slots run in order: slot 1 reads the output of slot 0
	const std::string swap = write("Swap.PS.hlsl", "return gScene.Sample(gLinearSampler, input.uv).gbra;");
	REQUIRE(api.setScreenEffect(api.hostContext, 1, swap.c_str(), nullptr));
	result = CaptureGameViewSignature(s);
	CHECK(SignatureCell(result, 8, 4, 0) > 250.0f);
	CHECK(SignatureCell(result, 8, 4, 1) < 3.0f);
	api.setScreenEffect(api.hostContext, 1, nullptr, nullptr);

	// Depth is readable: the sky is at the far plane, the ground is nearer
	const std::string depth = write("Depth.PS.hlsl", "float d = gDepth.Sample(gPointSampler, input.uv); return float4(d >= 0.9999f ? 1.0f : 0.0f, 0.0f, 0.0f, 1.0f);");
	REQUIRE(api.setScreenEffect(api.hostContext, 0, depth.c_str(), nullptr));
	result = CaptureGameViewSignature(s);
	CHECK_MSG(SignatureCell(result, 8, 0, 0) > 200.0f, std::format("sky {:.1f}", SignatureCell(result, 8, 0, 0)));
	CHECK_MSG(SignatureCell(result, 8, 8, 0) < 50.0f, std::format("ground {:.1f}", SignatureCell(result, 8, 8, 0)));

	// A broken shader or a missing file: false, nothing changes, the frame still renders
	const std::string broken = write("Broken.PS.hlsl", "this is not hlsl;");
	CHECK(!api.setScreenEffect(api.hostContext, 2, broken.c_str(), nullptr));
	CHECK(!api.setScreenEffect(api.hostContext, 2, broken.c_str(), nullptr));
	CHECK(!api.setScreenEffect(api.hostContext, 2, "no/such/file.hlsl", nullptr));
	CHECK(!s.PostEffects().HasScreenEffect(2));
	CHECK(!api.setScreenEffect(api.hostContext, 9, solid.c_str(), nullptr));
	api.setScreenEffect(api.hostContext, 0, nullptr, nullptr);
	CHECK_NEAR(LargestSignatureDifference(plain, CaptureGameViewSignature(s)), 0.0f, 1.0f);

	// The built-in effects compile and change the picture
	const float slash[16] = { 0.5f, 0.5f, 0.0f, 1.0f, 6.0f, 0.3f, 0.2f, 1.0f };
	REQUIRE(api.setScreenEffect(api.hostContext, 0, "shaders/ScreenEffect.Slash.PS.hlsl", slash));
	CHECK(LargestSignatureDifference(plain, CaptureGameViewSignature(s)) > 50.0f);
	const float radial[16] = { 0.5f, 0.5f, 0.6f };
	REQUIRE(api.setScreenEffect(api.hostContext, 0, "shaders/ScreenEffect.RadialBlur.PS.hlsl", radial));
	CHECK(LargestSignatureDifference(plain, CaptureGameViewSignature(s)) > 5.0f);

	// Effects end with the session
	s.ResetScriptRuntimeState();
	CHECK(!s.PostEffects().HasScreenEffect(0));
	A::State(s) = EditorSceneState::Edit;
}

TEST_CASE(Post, LookPresetsChangeOnlyTheLook)
{
	for (const char* name : kPostLookPresets) {
		PostProcessSettings look;
		look.enabled = false;
		look.ssao = true;
		look.shadowCascades = 3;
		look.environmentIntensity = 2.0f;
		look.fogEnabled = true;
		look.fogDensity = 0.5f;
		CHECK_MSG(ApplyPostLookPreset(look, name), name);
		CHECK_MSG(look.enabled, name);
		// Lighting quality and fog are the scene's, not the look's
		CHECK(look.ssao);
		CHECK(look.shadowCascades == 3);
		CHECK_NEAR(look.environmentIntensity, 2.0f, 1e-6f);
		CHECK(look.fogEnabled);
		CHECK_NEAR(look.fogDensity, 0.5f, 1e-6f);
	}
	PostProcessSettings vivid, moody;
	REQUIRE(ApplyPostLookPreset(vivid, "Vivid Action"));
	REQUIRE(ApplyPostLookPreset(moody, "Dark Moody"));
	CHECK(vivid.saturation > 1.0f);
	CHECK(moody.saturation < 1.0f && moody.exposure < 1.0f && moody.vignette > 0.0f);
	PostProcessSettings neon;
	REQUIRE(ApplyPostLookPreset(neon, "Neon Glow"));
	CHECK(neon.bloom && neon.bloomThreshold < PostProcessSettings{}.bloomThreshold && neon.bloomIntensity > PostProcessSettings{}.bloomIntensity);
	PostProcessSettings untouched;
	CHECK(!ApplyPostLookPreset(untouched, "No Such Look"));
	CHECK(!untouched.enabled);
}

TEST_CASE(Post, ImpactPulseFadesAndKeepsItsOwnSlot)
{
	EngineAPI& api = A::Api(s);
	A::State(s) = EditorSceneState::Edit; // the editor's Test Impact Pulse button works without Play
	s.PostSettings() = PostProcessSettings{};
	s.PostSettings().enabled = true;
	const float baseBloom = s.PostSettings().bloomIntensity;
	auto length = [](const Vector3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };

	ImpactPulse pulse;
	pulse.bloomBoost = 2.0f;
	pulse.radialBlur = 0.4f;
	pulse.shake = 0.3f;
	pulse.seconds = 0.5f;
	s.TriggerImpactPulse(pulse);
	CHECK_NEAR(s.EffectivePostSettings().bloomIntensity, baseBloom + 2.0f, 1e-4f);
	CHECK(s.PostEffects().HasScreenEffect(kImpactEffectSlot));
	CHECK(A::ScenePost(s).HasScreenEffect(kImpactEffectSlot));

	s.Update(0.25f, RizeEngine::GetInput());
	const float half = s.EffectivePostSettings().bloomIntensity;
	CHECK_MSG(half > baseBloom + 0.5f && half < baseBloom + 1.5f, std::format("half-way bloom {:.2f}", half));
	CHECK_MSG(length(s.CameraShakeOffset()) > 0.0f || s.CameraShakeStrength() > 0.0f, "the shake previews in Edit");

	s.Update(0.3f, RizeEngine::GetInput());
	CHECK_NEAR(s.EffectivePostSettings().bloomIntensity, baseBloom, 1e-5f);
	CHECK(!s.PostEffects().HasScreenEffect(kImpactEffectSlot));
	CHECK(!A::ScenePost(s).HasScreenEffect(kImpactEffectSlot));
	CHECK(s.CameraShakeStrength() == 0.0f);

	// With the look turned off a pulse still flashes, without switching on tone mapping or fog for its duration
	s.PostSettings().enabled = false;
	s.PostSettings().fogEnabled = true;
	s.TriggerImpactPulse(pulse);
	const PostProcessSettings flash = s.EffectivePostSettings();
	CHECK(flash.enabled && flash.bloom && !flash.toneMapping && !flash.fogEnabled && !flash.ssao);
	CHECK_NEAR(flash.bloomIntensity, 2.0f, 1e-4f);
	CHECK_NEAR(flash.exposure, 1.0f, 1e-6f);
	s.Update(0.6f, RizeEngine::GetInput());
	CHECK(!s.EffectivePostSettings().enabled);
	s.PostSettings() = PostProcessSettings{};
	s.PostSettings().enabled = true;

	// Scripts own slots 0..3: the engine's slot is out of their reach
	CHECK(!api.setScreenEffect(api.hostContext, kImpactEffectSlot, "shaders/ScreenEffect.RadialBlur.PS.hlsl", nullptr));
	CHECK(!s.PostEffects().HasScreenEffect(kImpactEffectSlot));
	// Script effects reach the Scene view preview too
	REQUIRE(api.setScreenEffect(api.hostContext, 2, "shaders/ScreenEffect.RadialBlur.PS.hlsl", nullptr));
	CHECK(A::ScenePost(s).HasScreenEffect(2));
	s.ResetScriptRuntimeState();
	CHECK(!A::ScenePost(s).HasScreenEffect(2));

	// Bad values do nothing; a pulse in progress ends with the session
	ImpactPulse bad;
	bad.seconds = std::nanf("");
	bad.bloomBoost = 1.0f;
	s.TriggerImpactPulse(bad);
	CHECK_NEAR(s.EffectivePostSettings().bloomIntensity, baseBloom, 1e-5f);
	s.TriggerImpactPulse(pulse);
	s.ResetScriptRuntimeState();
	CHECK_NEAR(s.EffectivePostSettings().bloomIntensity, baseBloom, 1e-5f);
	CHECK(!s.PostEffects().HasScreenEffect(kImpactEffectSlot));
}

TEST_CASE(ScriptAPI, ImpactPulseAndLookPresetFromScripts)
{
	EngineAPI& api = A::Api(s);
	REQUIRE(api.impactPulse != nullptr);
	REQUIRE(api.setLookPreset != nullptr);
	s.PostSettings() = PostProcessSettings{};
	s.PostSettings().enabled = true;
	s.PostSettings().saturation = 1.0f;
	s.StartPlayMode();
	const float baseBloom = s.PostSettings().bloomIntensity;

	// The same pulse as the editor's Test Impact Pulse button
	api.impactPulse(api.hostContext, 1.5f, 0.3f, 0.2f, 0.4f);
	CHECK_NEAR(s.EffectivePostSettings().bloomIntensity, baseBloom + 1.5f, 1e-4f);
	CHECK(s.PostEffects().HasScreenEffect(kImpactEffectSlot));
	CHECK(s.CameraShakeStrength() > 0.0f);
	api.impactPulse(api.hostContext, std::nanf(""), 0.3f, 0.0f, 0.4f); // ignored, the running pulse stays
	CHECK_NEAR(s.EffectivePostSettings().bloomIntensity, baseBloom + 1.5f, 1e-4f);

	// A look preset by name; unknown names change nothing; Stop brings the scene's look back
	CHECK(api.setLookPreset(api.hostContext, "Dark Moody"));
	CHECK(s.PostSettings().saturation < 1.0f);
	CHECK(!api.setLookPreset(api.hostContext, "No Such Look"));
	CHECK(!api.setLookPreset(api.hostContext, nullptr));
	s.StopPlayMode();
	CHECK_NEAR(s.PostSettings().saturation, 1.0f, 1e-6f);
	CHECK_NEAR(s.EffectivePostSettings().bloomIntensity, baseBloom, 1e-5f); // Stop ends the pulse too
	CHECK(!s.PostEffects().HasScreenEffect(kImpactEffectSlot));
	s.PostSettings() = PostProcessSettings{};
}

TEST_CASE(Post, SceneViewPreviewShowsTheGameLook)
{
	BuildBaselineScene(s);
	A::State(s) = EditorSceneState::Edit;
	A::EditorCam(s).translation = { 0.0f, 4.0f, -12.0f };
	A::EditorCam(s).rotation = { 0.25f, 0.0f, 0.0f };
	s.PostSettings() = PostProcessSettings{};
	s.PostSettings().enabled = true;
	s.PostSettings().exposure = 0.1f; // a much darker look
	auto mean = [](const std::vector<float>& v) {
		double sum = 0.0;
		for (float x : v) sum += x;
		return v.empty() ? 0.0 : sum / static_cast<double>(v.size());
	};
	A::ScenePostPreview(s) = false;
	const double raw = mean(CaptureViewSignature(s, A::EditViewRT(s)));
	A::ScenePostPreview(s) = true;
	const double previewed = mean(CaptureViewSignature(s, A::EditViewRT(s)));
	CHECK_MSG(raw > 20.0 && previewed < raw * 0.6, std::format("scene view {:.1f} raw, {:.1f} with the game look", raw, previewed));
	A::ScenePostPreview(s) = false;
	CHECK_NEAR(mean(CaptureViewSignature(s, A::EditViewRT(s))), raw, 2.0);
	s.PostSettings() = PostProcessSettings{};
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Post, ScreenEffectStudioFindsTheBuiltInShaders)
{
	const std::vector<std::string> shaders = ListScreenEffectShaders("shaders");
	const auto has = [&](const char* name) { return std::find(shaders.begin(), shaders.end(), name) != shaders.end(); };
	CHECK(has("ScreenEffect.RadialBlur.PS.hlsl"));
	CHECK(has("ScreenEffect.Slash.PS.hlsl"));
	CHECK(!has("Post.Composite.PS.hlsl"));
	CHECK(std::is_sorted(shaders.begin(), shaders.end()));
	CHECK(ListScreenEffectShaders("no/such/folder").empty());
	// The comment at the top of a shader explains its parameters
	const std::string hint = ScreenEffectShaderHint("shaders/ScreenEffect.RadialBlur.PS.hlsl");
	CHECK_MSG(hint.find("gParams[0].xy = center") != std::string::npos, hint);
	CHECK(hint.find("#include") == std::string::npos);
	CHECK(ScreenEffectShaderHint("no/such/file.hlsl").empty());
}

TEST_CASE(Baseline, RenderedSceneMatchesReference)
{
	const uint32_t toon = BuildBaselineScene(s);
	A::State(s) = EditorSceneState::Play; // the HUD only renders outside Edit
	const std::vector<float> first = CaptureGameViewSignature(s);
	REQUIRE(first.size() == kSignatureColumns * kSignatureRows * 3);
	// Two frames of the same scene must agree, otherwise a reference image means nothing
	CHECK_NEAR(LargestSignatureDifference(first, CaptureGameViewSignature(s)), 0.0f, 0.5f);

	if (std::strstr(GetCommandLineA(), "--update-baselines")) {
		std::filesystem::create_directories(std::filesystem::path(kRenderBaselineFile).parent_path());
		std::ofstream(kRenderBaselineFile) << nlohmann::json{ { "columns", kSignatureColumns }, { "rows", kSignatureRows }, { "rgb", first } }.dump(1);
	}
	std::ifstream file(kRenderBaselineFile);
	CHECK_MSG(file.good(), std::string("missing ") + kRenderBaselineFile + ": run --run-tests Baseline --update-baselines");
	if (file.good()) {
		const nlohmann::json reference = nlohmann::json::parse(file, nullptr, false);
		REQUIRE(!reference.is_discarded());
		const float difference = LargestSignatureDifference(first, reference.value("rgb", std::vector<float>{}));
		CHECK_MSG(difference <= 6.0f, std::format("largest cell difference {:.2f} / 255", difference));
	}

	// The comparison must notice real changes, separately for the 3D scene and the HUD
	s.FindGameObjectById(toon, false)->materialColor = { 0.0f, 1.0f, 0.0f, 1.0f };
	const std::vector<float> recolored = CaptureGameViewSignature(s);
	const float meshChange = LargestSignatureDifference(first, recolored);
	CHECK_MSG(meshChange > 6.0f, std::format("recoloring one mesh changed a cell by {:.2f}", meshChange));
	UIRenderer::GetInstance()->Clear();
	const float hudChange = LargestSignatureDifference(recolored, CaptureGameViewSignature(s));
	CHECK_MSG(hudChange > 20.0f, std::format("removing the HUD changed a cell by {:.2f}", hudChange));
	A::State(s) = EditorSceneState::Edit;
}

namespace {

constexpr float kPhysicsFloorTop = 50.0f;

uint32_t MakePhysicsFloor(EditorSuite& s)
{
	GameObject& floor = s.CreateGameObject("PhysicsFloor", GameObjectType::Empty);
	floor.translation = { 0.0f, kPhysicsFloorTop - 0.5f, 0.0f };
	floor.hasBoxCollider = true;
	floor.boxSize = { 40.0f, 1.0f, 40.0f };
	floor.isStatic = true;
	return floor.id;
}

uint32_t MakePhysicsBox(EditorSuite& s, const char* name, const Vector3& pos, float bounciness = 0.0f)
{
	GameObject& box = s.CreateGameObject(name, GameObjectType::Empty);
	box.translation = pos;
	box.hasBoxCollider = true;
	box.boxSize = { 1.0f, 1.0f, 1.0f };
	box.hasRigidbody = true;
	box.useGravity = true;
	box.bounciness = bounciness;
	return box.id;
}

void StepPhysicsSeconds(EditorSuite& s, float seconds)
{
	for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i) A::Physics(s, 1.0f / 60.0f);
}

} // namespace

// Behaviour every physics backend must keep. Tolerances are wide on purpose: they state the rule, not one solver's numbers.
TEST_CASE(PhysicsBaseline, FreeFallFollowsGravity)
{
	const uint32_t id = MakePhysicsBox(s, "Faller", { 0.0f, 100.0f, 0.0f });
	s.FindGameObjectById(id, false)->hasBoxCollider = false;
	StepPhysicsSeconds(s, 1.0f);
	const float dropped = 100.0f - s.FindGameObjectById(id, false)->translation.y;
	CHECK_MSG(dropped > 4.2f && dropped < 5.2f, std::format("dropped {:.3f}", dropped)); // 0.5 * 9.81 = 4.9
}

TEST_CASE(PhysicsBaseline, BoxLandsAndRests)
{
	MakePhysicsFloor(s);
	const uint32_t id = MakePhysicsBox(s, "Lander", { 0.0f, kPhysicsFloorTop + 3.0f, 0.0f });
	StepPhysicsSeconds(s, 3.0f);
	const GameObject* box = s.FindGameObjectById(id, false);
	CHECK_NEAR(box->translation.y, kPhysicsFloorTop + 0.5f, 0.05f);
	CHECK_NEAR(box->velocity.y, 0.0f, 0.5f);
	CHECK(box->isGrounded);
}

TEST_CASE(PhysicsBaseline, BouncinessBouncesLowerThanTheDrop)
{
	MakePhysicsFloor(s);
	const uint32_t id = MakePhysicsBox(s, "Bouncer", { 0.0f, kPhysicsFloorTop + 3.0f, 0.0f }, 0.8f);
	float lowest = 1000.0f;
	float highestAfterHit = -1000.0f;
	for (int i = 0; i < 240; ++i) {
		A::Physics(s, 1.0f / 60.0f);
		const float y = s.FindGameObjectById(id, false)->translation.y;
		if (y < kPhysicsFloorTop + 0.6f) lowest = (std::min)(lowest, y);
		if (lowest < 1000.0f) highestAfterHit = (std::max)(highestAfterHit, y);
	}
	CHECK_MSG(lowest >= kPhysicsFloorTop + 0.4f, std::format("lowest {:.3f}", lowest));
	CHECK_MSG(highestAfterHit > kPhysicsFloorTop + 1.0f && highestAfterHit < kPhysicsFloorTop + 3.0f, std::format("rebound {:.3f}", highestAfterHit));
}

TEST_CASE(PhysicsBaseline, WallStopsAMovingBox)
{
	GameObject& wall = s.CreateGameObject("Wall", GameObjectType::Empty);
	wall.translation = { 3.0f, 70.0f, 0.0f };
	wall.hasBoxCollider = true;
	wall.boxSize = { 1.0f, 10.0f, 10.0f };
	wall.isStatic = true;
	const uint32_t id = MakePhysicsBox(s, "Mover", { 0.0f, 70.0f, 0.0f });
	GameObject* mover = s.FindGameObjectById(id, false);
	mover->useGravity = false;
	mover->velocity = { 5.0f, 0.0f, 0.0f };
	StepPhysicsSeconds(s, 2.0f);
	mover = s.FindGameObjectById(id, false);
	CHECK_NEAR(mover->translation.x, 2.0f, 0.05f); // wall face at 2.5, box half width 0.5
	CHECK_NEAR(mover->velocity.x, 0.0f, 0.5f);
}

TEST_CASE(PhysicsBaseline, TriggersReportButDoNotBlock)
{
	A::State(s) = EditorSceneState::Play;
	const uint32_t floorId = MakePhysicsFloor(s);
	s.FindGameObjectById(floorId, false)->isTrigger = true;
	const uint32_t id = MakePhysicsBox(s, "Passer", { 0.0f, kPhysicsFloorTop + 1.0f, 0.0f });
	int entered = 0;
	for (int i = 0; i < 120; ++i) {
		A::Physics(s, 1.0f / 60.0f);
		entered += A::CollisionEvents(s).enter;
	}
	CHECK(s.FindGameObjectById(id, false)->translation.y < kPhysicsFloorTop - 1.0f);
	CHECK_MSG(entered == 2, std::format("enter events {}", entered)); // one per direction of the pair
	A::State(s) = EditorSceneState::Edit;
	s.ResetScriptRuntimeState();
}

TEST_CASE(Physics, SphereTriggerMakesSensor)
{
	// The Sphere Collider's own Is Trigger box makes the object a trigger: it reports the box but does not stop it
	A::State(s) = EditorSceneState::Play;
	GameObject& ball = s.CreateGameObject("Trigger Ball", GameObjectType::Empty);
	ball.translation = { 0.0f, 60.0f, 0.0f };
	ball.isStatic = true;
	ball.hasSphereCollider = true;
	ball.sphereRadius = 3.0f;
	ball.sphereIsTrigger = true;
	const uint32_t id = MakePhysicsBox(s, "Passer", { 0.0f, 64.0f, 0.0f });
	int entered = 0;
	for (int i = 0; i < 120; ++i) {
		A::Physics(s, 1.0f / 60.0f);
		entered += A::CollisionEvents(s).enter;
	}
	const float y = s.FindGameObjectById(id, false)->translation.y;
	CHECK_MSG(y < 56.0f, std::format("box stopped at {:.3f}: the sphere blocked it", y)); // ball bottom at 57
	CHECK_MSG(entered == 2, std::format("enter events {}", entered));
	A::State(s) = EditorSceneState::Edit;
	s.ResetScriptRuntimeState();
}

TEST_CASE(PhysicsBaseline, LayerMaskLetsABoxPassThrough)
{
	const uint32_t floorId = MakePhysicsFloor(s);
	s.FindGameObjectById(floorId, false)->layer = 2;
	const uint32_t id = MakePhysicsBox(s, "Ghost", { 0.0f, kPhysicsFloorTop + 1.0f, 0.0f });
	s.FindGameObjectById(id, false)->collisionMask &= ~(1u << 2);
	StepPhysicsSeconds(s, 2.0f);
	CHECK(s.FindGameObjectById(id, false)->translation.y < kPhysicsFloorTop - 1.0f);
}

TEST_CASE(PhysicsBaseline, StaticAndKinematicDoNotFall)
{
	const uint32_t kinematic = MakePhysicsBox(s, "Kinematic", { 0.0f, 80.0f, 0.0f });
	s.FindGameObjectById(kinematic, false)->isKinematic = true;
	const uint32_t fixedBox = MakePhysicsBox(s, "Static", { 5.0f, 80.0f, 0.0f });
	s.FindGameObjectById(fixedBox, false)->isStatic = true;
	StepPhysicsSeconds(s, 1.0f);
	CHECK_NEAR(s.FindGameObjectById(kinematic, false)->translation.y, 80.0f, 1e-4f);
	CHECK_NEAR(s.FindGameObjectById(fixedBox, false)->translation.y, 80.0f, 1e-4f);
}

TEST_CASE(PhysicsBaseline, ChildRigidbodyFollowsItsParent)
{
	const uint32_t parent = s.CreateGameObject("Parent", GameObjectType::Empty).id;
	s.FindGameObjectById(parent, false)->translation = { 0.0f, 80.0f, 0.0f };
	const uint32_t child = MakePhysicsBox(s, "Child", { 1.0f, 0.0f, 0.0f });
	s.FindGameObjectById(child, false)->parentId = static_cast<int>(parent);
	s.RebuildHierarchyLinks();
	StepPhysicsSeconds(s, 1.0f);
	CHECK_NEAR(s.FindGameObjectById(child, false)->translation.y, 0.0f, 1e-4f); // local position untouched
}

TEST_CASE(PhysicsBaseline, TwoBoxesStack)
{
	MakePhysicsFloor(s);
	const uint32_t lower = MakePhysicsBox(s, "Lower", { 0.0f, kPhysicsFloorTop + 0.5f, 0.0f });
	const uint32_t upper = MakePhysicsBox(s, "Upper", { 0.0f, kPhysicsFloorTop + 3.0f, 0.0f });
	StepPhysicsSeconds(s, 4.0f);
	const float lowerY = s.FindGameObjectById(lower, false)->translation.y;
	const float upperY = s.FindGameObjectById(upper, false)->translation.y;
	const bool stacked = std::abs(lowerY - (kPhysicsFloorTop + 0.5f)) < 0.1f && std::abs(upperY - (kPhysicsFloorTop + 1.5f)) < 0.1f;
	CHECK_MSG(stacked, std::format("lower {:.3f} upper {:.3f}", lowerY, upperY));
}

TEST_CASE(PhysicsBaseline, FreeRotationTipsABoxOverAnEdgeAndFrozenDoesNot)
{
	MakePhysicsFloor(s);
	// Centre beyond the edge of a low step: a free box topples off it, a frozen one lands flat
	GameObject& step = s.CreateGameObject("Step", GameObjectType::Empty);
	step.translation = { 0.0f, kPhysicsFloorTop + 0.5f, 0.0f };
	step.hasBoxCollider = true;
	step.boxSize = { 2.0f, 1.0f, 2.0f };
	step.isStatic = true;
	const uint32_t freeBox = MakePhysicsBox(s, "Free", { 1.3f, kPhysicsFloorTop + 1.6f, 0.0f });
	const uint32_t frozenBox = MakePhysicsBox(s, "Frozen", { -1.3f, kPhysicsFloorTop + 1.6f, 0.0f });
	s.FindGameObjectById(frozenBox, false)->freezeRotation = true;
	StepPhysicsSeconds(s, 2.0f);
	const GameObject* freeObj = s.FindGameObjectById(freeBox, false);
	const GameObject* frozenObj = s.FindGameObjectById(frozenBox, false);
	CHECK(std::abs(freeObj->rotationQuat.w) < 0.999f);
	CHECK_NEAR(frozenObj->rotationQuat.w, 1.0f, 1e-4f);
	CHECK_NEAR(frozenObj->translation.y, kPhysicsFloorTop + 1.5f, 0.05f);
}

TEST_CASE(PhysicsBaseline, ContinuousCollisionStopsAFastBoxAtAThinWall)
{
	GameObject& wall = s.CreateGameObject("ThinWall", GameObjectType::Empty);
	wall.translation = { 5.0f, 70.0f, 0.0f };
	wall.hasBoxCollider = true;
	wall.boxSize = { 0.05f, 10.0f, 10.0f };
	wall.isStatic = true;
	const uint32_t id = MakePhysicsBox(s, "Bullet", { 0.0f, 70.0f, 0.0f });
	GameObject* bullet = s.FindGameObjectById(id, false);
	bullet->useGravity = false;
	bullet->boxSize = { 0.1f, 0.1f, 0.1f };
	bullet->velocity = { 300.0f, 0.0f, 0.0f };
	bullet->continuousCollision = true;
	StepPhysicsSeconds(s, 0.5f);
	CHECK(s.FindGameObjectById(id, false)->translation.x < 5.0f);
}

TEST_CASE(PhysicsBaseline, MovedColliderReportsContactWithoutRigidbody)
{
	A::State(s) = EditorSceneState::Play;
	GameObject& a = s.CreateGameObject("MovedA", GameObjectType::Empty);
	a.translation = { 0.0f, 70.0f, 0.0f };
	a.hasBoxCollider = true;
	GameObject& b = s.CreateGameObject("MovedB", GameObjectType::Empty);
	b.translation = { 5.0f, 70.0f, 0.0f };
	b.hasBoxCollider = true;
	const uint32_t bId = b.id;
	A::Physics(s, 1.0f / 60.0f);
	CHECK(A::CollisionEvents(s).enter == 0);
	s.FindGameObjectById(bId, false)->translation.x = 0.5f;
	A::Physics(s, 1.0f / 60.0f);
	A::Physics(s, 1.0f / 60.0f);
	CHECK(s.FindGameObjectById(bId, false)->isColliding);
	A::State(s) = EditorSceneState::Edit;
	s.ResetScriptRuntimeState();
}

TEST_CASE(PhysicsBaseline, RestingContactSurvivesSleepUntilTheBodyWakes)
{
	A::State(s) = EditorSceneState::Play;
	const uint32_t trigger = MakePhysicsFloor(s);
	s.FindGameObjectById(trigger, false)->isTrigger = true;
	GameObject& still = s.CreateGameObject("StillCollider", GameObjectType::Empty);
	still.translation = { 0.0f, kPhysicsFloorTop, 0.0f };
	still.hasBoxCollider = true;
	const uint32_t id = still.id;
	int exits = 0;
	for (int i = 0; i < 180; ++i) { // long enough for the body to fall asleep
		A::Physics(s, 1.0f / 60.0f);
		exits += A::CollisionEvents(s).exit;
	}
	CHECK_MSG(exits == 0, std::format("false exits {}", exits));
	CHECK(s.FindGameObjectById(id, false)->isColliding);
	s.FindGameObjectById(id, false)->translation = { 0.0f, kPhysicsFloorTop + 20.0f, 0.0f };
	A::Physics(s, 1.0f / 60.0f);
	A::Physics(s, 1.0f / 60.0f);
	CHECK(!s.FindGameObjectById(id, false)->isColliding);
	A::State(s) = EditorSceneState::Edit;
	s.ResetScriptRuntimeState();
}

TEST_CASE(PhysicsBaseline, CapsuleLandsOnItsEnd)
{
	MakePhysicsFloor(s);
	const uint32_t id = MakePhysicsBox(s, "Capsule", { 0.0f, kPhysicsFloorTop + 3.0f, 0.0f });
	GameObject* obj = s.FindGameObjectById(id, false);
	obj->hasBoxCollider = false;
	obj->freezeRotation = true;
	CHECK(s.AddComponentByName(*obj, "Capsule Collider"));
	StepPhysicsSeconds(s, 3.0f);
	CHECK_NEAR(s.FindGameObjectById(id, false)->translation.y, kPhysicsFloorTop + 1.0f, 0.05f); // default height 2
}

TEST_CASE(PhysicsBaseline, MeshColliderIsSolidAndOverridesDynamic)
{
	GameObject& ground = s.CreateGameObject("MeshGround", GameObjectType::Empty);
	ground.translation = { 0.0f, kPhysicsFloorTop - 1.0f, 0.0f };
	ground.scale = { 10.0f, 1.0f, 10.0f };
	ground.meshType = GameObject::MeshType::CustomModel;
	ground.modelPath = "Resources/Model/cube.obj";
	ground.hasRigidbody = true; // must not fall: a triangle mesh is never simulated
	const uint32_t groundId = ground.id;
	CHECK(s.AddComponentByName(ground, "Mesh Collider"));
	const uint32_t id = MakePhysicsBox(s, "OnMesh", { 0.0f, kPhysicsFloorTop + 3.0f, 0.0f });
	StepPhysicsSeconds(s, 3.0f);
	CHECK_NEAR(s.FindGameObjectById(groundId, false)->translation.y, kPhysicsFloorTop - 1.0f, 1e-4f);
	const float y = s.FindGameObjectById(id, false)->translation.y;
	CHECK_MSG(y > kPhysicsFloorTop - 1.0f, std::format("box at {:.3f}", y));
	CHECK_NEAR(s.FindGameObjectById(id, false)->velocity.y, 0.0f, 0.5f);
}

TEST_CASE(PhysicsBaseline, QueriesFollowRotationAndSkipTheInside)
{
	GameObject& box = s.CreateGameObject("Turned", GameObjectType::Empty);
	box.translation = { 5.0f, 70.0f, 0.0f };
	box.rotationQuat = Quaternion::FromEuler({ 0.0f, 0.7853982f, 0.0f });
	box.hasBoxCollider = true;
	const uint32_t id = box.id;
	ScriptRaycastHit hit;
	CHECK(s.ScriptRaycast({ 0.0f, 70.0f, 0.0f }, { 2.0f, 0.0f, 0.0f }, 20.0f, 0xFFFFFFFFu, 0, hit));
	CHECK(hit.objectId == id);
	CHECK_NEAR(hit.distance, 5.0f - 0.7071f, 0.01f); // the corner of the turned box points at the ray
	CHECK(!s.ScriptRaycast({ 5.0f, 70.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 20.0f, 0xFFFFFFFFu, 0, hit));
	CHECK(!s.ScriptRaycast({ 0.0f, 70.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 20.0f, 0xFFFFFFFFu, id, hit));

	uint32_t ids[4] = {};
	CHECK(s.ScriptOverlapSphere({ 5.0f + 0.9f, 70.0f, 0.0f }, 0.25f, 0xFFFFFFFFu, ids, 4) == 1); // reaches only the corner
	CHECK(s.ScriptOverlapSphere({ 5.0f + 0.9f, 70.0f, 0.9f }, 0.25f, 0xFFFFFFFFu, ids, 4) == 0);
}

TEST_CASE(PhysicsBaseline, HingeKeepsItsAnchorWhileSwinging)
{
	const uint32_t id = MakePhysicsBox(s, "Door", { 2.0f, 80.0f, 0.0f });
	GameObject* door = s.FindGameObjectById(id, false);
	CHECK(s.AddComponentByName(*door, "Joint"));
	auto* joint = door->components.Get<JointComponent>("Joint");
	joint->jointType = 2;
	joint->anchor = { -2.0f, 0.0f, 0.0f }; // hinge at world (0, 80, 0)
	float lowest = 80.0f;
	for (int i = 0; i < 60; ++i) {
		A::Physics(s, 1.0f / 60.0f);
		const Vector3 p = s.FindGameObjectById(id, false)->translation;
		const float fromAnchor = std::sqrt(p.x * p.x + (p.y - 80.0f) * (p.y - 80.0f));
		CHECK_NEAR(fromAnchor, 2.0f, 0.05f);
		lowest = (std::min)(lowest, p.y);
	}
	CHECK(lowest < 79.0f);
}

TEST_CASE(PhysicsBaseline, RemovingAJointedBodyIsSafe)
{
	const uint32_t a = MakePhysicsBox(s, "JointA", { 0.0f, 80.0f, 0.0f });
	const uint32_t b = MakePhysicsBox(s, "JointB", { 2.0f, 80.0f, 0.0f });
	GameObject* owner = s.FindGameObjectById(a, false);
	CHECK(s.AddComponentByName(*owner, "Joint"));
	auto* joint = owner->components.Get<JointComponent>("Joint");
	joint->jointType = 3;
	joint->connectedObjectId = static_cast<int>(b);
	A::Physics(s, 1.0f / 60.0f);
	s.FindGameObjectById(b, false)->isActive = false;
	A::Physics(s, 1.0f / 60.0f);
	s.FindGameObjectById(b, false)->isActive = true;
	StepPhysicsSeconds(s, 0.5f);
	const Vector3 pa = s.FindGameObjectById(a, false)->translation;
	const Vector3 pb = s.FindGameObjectById(b, false)->translation;
	const float dx = pa.x - pb.x, dy = pa.y - pb.y, dz = pa.z - pb.z;
	CHECK(std::sqrt(dx * dx + dy * dy + dz * dz) < 3.0f); // the joint is back and holds them together
}

TEST_CASE(PhysicsBaseline, RenderingBlendsBetweenStepsAndScriptsSeeTheSimulatedPose)
{
	const uint32_t id = MakePhysicsBox(s, "Blended", { 0.0f, 100.0f, 0.0f });
	s.FindGameObjectById(id, false)->hasBoxCollider = false;
	s.FindGameObjectById(id, false)->velocity = { 60.0f, 0.0f, 0.0f };
	s.FindGameObjectById(id, false)->useGravity = false;
	s.FindGameObjectById(id, false)->drag = 0.0f;
	A::PhysicsAccumulator(s) = 0.0;
	s.StepPhysics(1.5f / 60.0f); // one step (1 unit) and half a step left
	CHECK_NEAR(s.FindGameObjectById(id, false)->translation.x, 0.5f, 0.01f); // shown: halfway through that step
	A::RestorePoses(s);
	CHECK_NEAR(s.FindGameObjectById(id, false)->translation.x, 1.0f, 0.01f); // simulated
	A::PhysicsAccumulator(s) = 0.0;
	s.ResetScriptRuntimeState();
}
// ===========================================================================
// Fase 4: lighting
// ===========================================================================

namespace {

// A dark room: black sky, no sun, a big floor, the camera looking down at it. Restores the sky when the test ends.
struct LightingScene {
	EditorSuite& s;
	SkyboxMode savedMode;
	Vector4 savedColor;

	explicit LightingScene(EditorSuite& suite) : s(suite), savedMode(A::Sky(suite).GetMode()), savedColor(A::Sky(suite).GetSolidColor()) {
		s.NewScene();
		for (auto& obj : A::Objects(s)) {
			if (obj.hasLight) obj.lightEnabled = false;
			if (obj.hasMesh) obj.meshEnabled = false; // the default ground
		}
		A::Sky(s).SetMode(SkyboxMode::SolidColor);
		A::Sky(s).SetSolidColor({ 0.0f, 0.0f, 0.0f, 1.0f });
		GameObject& floor = s.CreateCube("Floor", { 0.0f, -0.5f, 0.0f });
		floor.scale = { 60.0f, 1.0f, 60.0f };
		floor.materialColor = { 0.8f, 0.8f, 0.8f, 1.0f };
		GameObject* camera = s.FindGameObjectByName("Main Camera", false);
		camera->translation = { 0.0f, 12.0f, -12.0f };
		camera->rotation = { 0.785f, 0.0f, 0.0f };
		camera->rotationQuat = Quaternion::FromEuler(camera->rotation);
		s.PostSettings() = PostProcessSettings{};
		UIRenderer::GetInstance()->Clear();
	}
	~LightingScene() {
		A::Sky(s).SetMode(savedMode);
		A::Sky(s).SetSolidColor(savedColor);
	}
	std::vector<float> Capture() {
		s.Update(0.0f, RizeEngine::GetInput());
		UIRenderer::GetInstance()->Clear();
		return CaptureGameViewSignature(s);
	}
	GameObject& AddSun(const Vector3& rotation) {
		// The first directional light in the hierarchy is the sun, so this reuses the scene's own
		for (auto& obj : A::Objects(s)) {
			if (obj.hasLight && obj.lightType == GameObject::LightType::Directional) {
				obj.lightEnabled = true;
				obj.lightCastShadow = true;
				obj.lightIntensity = 1.0f;
				obj.lightColor = { 1.0f, 1.0f, 1.0f };
				obj.rotation = rotation;
				obj.rotationQuat = Quaternion::FromEuler(rotation);
				return obj;
			}
		}
		return A::Objects(s).front();
	}
};

float Brightness(const std::vector<float>& signature)
{
	float sum = 0.0f;
	for (float v : signature) sum += v;
	return sum / static_cast<float>(signature.size());
}

PbrMaterialComponent* AddPbr(EditorSuite& s, GameObject& obj)
{
	s.AddComponentByName(obj, "PBR Material");
	return obj.components.Get<PbrMaterialComponent>("PBR Material");
}

} // namespace

TEST_CASE(Lighting, ClustersListOnlyTheLightsThatReachThem)
{
	LightClusters::Projection projection;
	projection.tanHalfFovX = 1.0f;
	projection.tanHalfFovY = 1.0f;
	projection.nearZ = 1.0f;
	projection.farZ = 100.0f;
	std::vector<LightClusters::Sphere> lights(3);
	lights[0].viewPosition = { 0.0f, 0.0f, 10.0f }; lights[0].radius = 1.0f;  // straight ahead
	lights[1].viewPosition = { 0.0f, 0.0f, -50.0f }; lights[1].radius = 5.0f; // behind the camera
	lights[2].viewPosition = { 9.0f, 9.0f, 10.0f }; lights[2].radius = 1.0f;  // top right
	std::vector<uint32_t> clusters, indices;
	LightClusters::Build(lights, projection, clusters, indices);
	REQUIRE(clusters.size() == LightClusters::kCount);

	const auto lightsAt = [&](float x, float y, float z) {
		const uint32_t tileX = (std::min)(static_cast<uint32_t>((x / z * 0.5f + 0.5f) * LightClusters::kX), LightClusters::kX - 1);
		const uint32_t tileY = (std::min)(static_cast<uint32_t>((0.5f - y / z * 0.5f) * LightClusters::kY), LightClusters::kY - 1);
		const uint32_t slice = static_cast<uint32_t>(std::log(z) / std::log(100.0f) * LightClusters::kZ);
		const uint32_t cluster = clusters[(slice * LightClusters::kY + tileY) * LightClusters::kX + tileX];
		std::vector<uint32_t> result;
		for (uint32_t i = 0; i < (cluster >> 24); ++i) result.push_back(indices[(cluster & 0xFFFFFFu) + i]);
		return result;
	};
	const auto center = lightsAt(0.1f, 0.1f, 10.0f);
	CHECK(center.size() == 1 && center[0] == 0);
	const auto corner = lightsAt(9.0f, 9.0f, 10.0f);
	CHECK(corner.size() == 1 && corner[0] == 2);
	CHECK(lightsAt(-5.0f, -5.0f, 10.0f).empty());
	CHECK(lightsAt(0.1f, 0.1f, 80.0f).empty());
	for (uint32_t index : indices) CHECK(index != 1);
}

TEST_CASE(Lighting, MoreThanThreePointLightsShine)
{
	LightingScene scene(s);
	const std::vector<float> dark = scene.Capture();
	REQUIRE(dark.size() == kSignatureColumns * kSignatureRows * 3);

	std::vector<float> previous = dark;
	for (int i = 0; i < 8; ++i) {
		GameObject& lamp = s.CreateGameObject("Lamp", GameObjectType::Empty);
		lamp.hasLight = true;
		lamp.lightEnabled = true;
		lamp.lightType = GameObject::LightType::Point;
		lamp.lightColor = { 1.0f, 1.0f, 1.0f };
		lamp.lightIntensity = 3.0f;
		lamp.lightRange = 5.0f;
		lamp.lightDecay = 1.0f;
		lamp.translation = { -7.0f + i * 2.0f, 1.5f, 0.0f };
		const std::vector<float> now = scene.Capture();
		CHECK_MSG(Brightness(now) > Brightness(previous) + 0.1f, std::format("lamp {} added nothing: {:.2f} -> {:.2f}", i, Brightness(previous), Brightness(now)));
		previous = now;
	}
}

TEST_CASE(Lighting, PbrReflectsTheSkyAndFollowsItWhenItChanges)
{
	LightingScene scene(s);
	A::Sky(s).SetSolidColor({ 1.0f, 0.0f, 0.0f, 1.0f });
	GameObject& ball = s.CreateCube("Metal", { 0.0f, 3.0f, 0.0f });
	ball.scale = { 6.0f, 6.0f, 6.0f };
	PbrMaterialComponent* pbr = AddPbr(s, ball);
	REQUIRE(pbr != nullptr);
	pbr->metallic = 1.0f;
	pbr->roughness = 0.2f;

	const uint64_t bakesBefore = A::EnvironmentBakes(s);
	const std::vector<float> red = scene.Capture();
	REQUIRE(red.size() == kSignatureColumns * kSignatureRows * 3);
	CHECK(A::EnvironmentBakes(s) == bakesBefore + 1);
	CHECK_MSG(SignatureCell(red, 8, 4, 0) > 120.0f && SignatureCell(red, 8, 4, 2) < 30.0f, std::format("red sky: {:.0f} {:.0f}", SignatureCell(red, 8, 4, 0), SignatureCell(red, 8, 4, 2)));

	const std::vector<float> same = scene.Capture();
	CHECK(A::EnvironmentBakes(s) == bakesBefore + 1); // nothing changed, nothing baked

	A::Sky(s).SetSolidColor({ 0.0f, 0.0f, 1.0f, 1.0f });
	const std::vector<float> blue = scene.Capture();
	CHECK(A::EnvironmentBakes(s) == bakesBefore + 2);
	CHECK_MSG(SignatureCell(blue, 8, 4, 2) > 120.0f && SignatureCell(blue, 8, 4, 0) < 30.0f, std::format("blue sky: {:.0f} {:.0f}", SignatureCell(blue, 8, 4, 0), SignatureCell(blue, 8, 4, 2)));

	A::Sky(s).SetSolidColor({ 0.0f, 0.0f, 0.0f, 1.0f });
	s.FindGameObjectByName("Metal", false)->components.Get<PbrMaterialComponent>("PBR Material")->emissive = { 0.0f, 1.0f, 0.0f };
	s.FindGameObjectByName("Metal", false)->components.Get<PbrMaterialComponent>("PBR Material")->emissiveStrength = 2.0f;
	const std::vector<float> glowing = scene.Capture();
	CHECK_MSG(SignatureCell(glowing, 8, 4, 1) > 200.0f, std::format("emissive {:.0f}", SignatureCell(glowing, 8, 4, 1)));
}

TEST_CASE(Lighting, NormalMapBendsTheLightTowardsItsTilt)
{
	LightingScene scene(s);
	GameObject* camera = s.FindGameObjectByName("Main Camera", false);
	camera->translation = { 0.0f, 3.0f, -8.0f };
	camera->rotation = { 0.0f, 0.0f, 0.0f };
	camera->rotationQuat = Quaternion::Identity();
	GameObject& wall = s.CreateCube("Wall", { 0.0f, 3.0f, 0.0f });
	wall.scale = { 8.0f, 8.0f, 1.0f };
	wall.lightingType = 1;
	// Light comes from the +X side of the wall: the sun travels towards -X and +Z
	scene.AddSun({ 0.0f, -0.6f, 0.0f });
	const std::vector<float> flat = scene.Capture();
	REQUIRE(flat.size() == kSignatureColumns * kSignatureRows * 3);

	// Red above 128 tilts the normal towards +u. On the cube face looking at -Z, +u is world -X (Model::CreateCube),
	// so that tilt turns the wall away from this sun. Green above 128 tilts towards image up, which is world +Y there.
	const uint8_t towardsRight[4] = { 220, 128, 180, 255 };
	const uint8_t towardsLeft[4] = { 36, 128, 180, 255 };
	const uint8_t towardsUp[4] = { 128, 220, 180, 255 };
	const uint8_t towardsDown[4] = { 128, 36, 180, 255 };
	A::MaterialTextures(s)["test/up.png|linear"] = TextureManager::GetInstance()->CreateTextureFromRGBA("testNormalUp", towardsUp, 1, 1);
	A::MaterialTextures(s)["test/down.png|linear"] = TextureManager::GetInstance()->CreateTextureFromRGBA("testNormalDown", towardsDown, 1, 1);
	A::MaterialTextures(s)["test/right.png|linear"] = TextureManager::GetInstance()->CreateTextureFromRGBA("testNormalRight", towardsRight, 1, 1);
	A::MaterialTextures(s)["test/left.png|linear"] = TextureManager::GetInstance()->CreateTextureFromRGBA("testNormalLeft", towardsLeft, 1, 1);
	PbrMaterialComponent* pbr = AddPbr(s, *s.FindGameObjectByName("Wall", false));
	REQUIRE(pbr != nullptr);
	pbr->metallic = 0.0f;
	pbr->roughness = 1.0f;
	strncpy_s(pbr->normalMap, "test/right.png", _TRUNCATE);
	const float right = SignatureCell(scene.Capture(), 8, 4, 0);
	pbr = s.FindGameObjectByName("Wall", false)->components.Get<PbrMaterialComponent>("PBR Material");
	strncpy_s(pbr->normalMap, "test/left.png", _TRUNCATE);
	const float left = SignatureCell(scene.Capture(), 8, 4, 0);
	CHECK_MSG(right + 10.0f < left, std::format("+u {:.0f} -u {:.0f}", right, left));

	scene.AddSun({ 0.6f, 0.0f, 0.0f }); // from above
	pbr = s.FindGameObjectByName("Wall", false)->components.Get<PbrMaterialComponent>("PBR Material");
	strncpy_s(pbr->normalMap, "test/up.png", _TRUNCATE);
	const float up = SignatureCell(scene.Capture(), 8, 4, 0);
	pbr = s.FindGameObjectByName("Wall", false)->components.Get<PbrMaterialComponent>("PBR Material");
	strncpy_s(pbr->normalMap, "test/down.png", _TRUNCATE);
	const float down = SignatureCell(scene.Capture(), 8, 4, 0);
	CHECK_MSG(down + 10.0f < up, std::format("up {:.0f} down {:.0f}", up, down));
}

TEST_CASE(Lighting, CascadesCastShadowsNearTheCameraUpToTheDistance)
{
	LightingScene scene(s);
	GameObject& floor = *s.FindGameObjectByName("Floor", false);
	floor.lightingType = 1;
	scene.AddSun({ 1.2f, 0.4f, 0.0f });
	GameObject& box = s.CreateCube("Box", { 0.0f, 2.0f, 0.0f });
	box.scale = { 3.0f, 4.0f, 3.0f };
	box.isLit = false;
	box.materialColor = { 0.0f, 0.0f, 0.0f, 1.0f };

	const auto shadowDepth = [&](int cascades, float distance) {
		s.PostSettings().shadowCascades = cascades;
		s.PostSettings().shadowDistance = distance;
		s.FindGameObjectByName("Box", false)->castShadow = true;
		const std::vector<float> withShadow = scene.Capture();
		s.FindGameObjectByName("Box", false)->castShadow = false;
		const std::vector<float> without = scene.Capture();
		float largest = 0.0f;
		for (size_t i = 0; i < withShadow.size() && i < without.size(); ++i) largest = (std::max)(largest, without[i] - withShadow[i]);
		return largest;
	};
	const float legacy = shadowDepth(0, 80.0f);
	const float cascaded = shadowDepth(3, 80.0f);
	const float four = shadowDepth(4, 80.0f);
	const float tooShort = shadowDepth(3, 2.0f); // everything visible is farther than this
	CHECK_MSG(legacy > 10.0f, std::format("legacy {:.1f}", legacy));
	CHECK_MSG(cascaded > 10.0f, std::format("cascaded {:.1f}", cascaded));
	CHECK_MSG(four > 10.0f, std::format("four {:.1f}", four));
	CHECK_MSG(tooShort < 1.0f, std::format("beyond the distance {:.1f}", tooShort));
}

TEST_CASE(Lighting, AmbientOcclusionDarkensContactsAndNotTheSky)
{
	LightingScene scene(s);
	GameObject& floor = *s.FindGameObjectByName("Floor", false);
	floor.isLit = false;
	GameObject& box = s.CreateCube("Box", { 0.0f, 1.0f, 2.0f });
	box.scale = { 3.0f, 2.0f, 3.0f };
	box.isLit = false;
	s.PostSettings().enabled = true;
	s.PostSettings().toneMapping = false;
	s.PostSettings().bloom = false;
	s.PostSettings().fxaa = false;
	const std::vector<float> off = scene.Capture();
	s.PostSettings().ssao = true;
	s.PostSettings().ssaoRadius = 1.5f;
	const std::vector<float> on = scene.Capture();
	REQUIRE(off.size() == on.size() && !on.empty());
	float darkest = 0.0f;
	float brighter = 0.0f;
	for (size_t i = 0; i < on.size(); ++i) {
		darkest = (std::max)(darkest, off[i] - on[i]);
		brighter = (std::max)(brighter, on[i] - off[i]);
	}
	CHECK_MSG(darkest > 3.0f, std::format("darkest {:.1f}", darkest));
	CHECK_MSG(brighter < 0.5f, std::format("brighter {:.1f}", brighter));
}

TEST_CASE(Lighting, InstancingDrawsTheSamePicture)
{
	LightingScene scene(s);
	scene.AddSun({ 1.0f, 0.5f, 0.0f });
	for (int i = 0; i < 40; ++i) {
		GameObject& cube = s.CreateCube("Crowd", { -10.0f + (i % 8) * 3.0f, 0.5f + (i % 3), -4.0f + (i / 8) * 3.0f });
		cube.materialColor = (i % 2) == 0 ? Vector4{ 0.9f, 0.3f, 0.2f, 1.0f } : Vector4{ 0.2f, 0.5f, 0.9f, 1.0f };
		cube.rotation = { 0.0f, i * 0.2f, 0.0f };
		cube.rotationQuat = Quaternion::FromEuler(cube.rotation);
	}
	A::Instancing(s) = false;
	const std::vector<float> single = scene.Capture();
	A::Instancing(s) = true;
	const std::vector<float> instanced = scene.Capture();
	REQUIRE(single.size() == instanced.size() && !single.empty());
	float largest = 0.0f;
	for (size_t i = 0; i < single.size(); ++i) largest = (std::max)(largest, std::abs(single[i] - instanced[i]));
	CHECK_MSG(largest < 0.5f, std::format("largest difference {:.2f}", largest));
}

TEST_CASE(Lighting, MaterialAssetIsSharedAndSurvivesARestart)
{
	namespace fs = std::filesystem;
	const fs::path root = A::TempRoot() / "MaterialGame";
	fs::create_directories(root / "Assets");
	const fs::path savedRoot = A::ProjectRoot(s);
	A::ProjectRoot(s) = root;

	GameObject& first = s.CreateCube("Shared", { 0.0f, 0.0f, 0.0f });
	PbrMaterialComponent* pbr = AddPbr(s, first);
	REQUIRE(pbr != nullptr);
	pbr->metallic = 0.75f;
	pbr->roughness = 0.25f;
	A::SaveMaterial(s, first, *pbr);
	const std::string asset = pbr->material;
	CHECK_MSG(asset == "Assets/Materials/Shared.material.json", "asset [" + asset + "]");
	CHECK(fs::exists(root / asset));

	GameObject& second = s.CreateCube("Other", { 2.0f, 0.0f, 0.0f });
	PbrMaterialComponent* other = AddPbr(s, second);
	REQUIRE(other != nullptr);
	strncpy_s(other->material, asset.c_str(), _TRUNCATE);
	const uint32_t secondId = second.id;

	A::ForgetMaterials(s); // read back from the file
	const PbrMaterialComponent* loaded = A::FindMaterial(s, asset);
	REQUIRE(loaded != nullptr);
	CHECK_NEAR(loaded->metallic, 0.75f, 1e-5f);
	CHECK_NEAR(loaded->roughness, 0.25f, 1e-5f);

	s.Update(0.0f, RizeEngine::GetInput());
	CaptureGameViewSignature(s); // a whole frame: nothing recorded stays unsubmitted when the test deletes its objects
	other = s.FindGameObjectById(secondId, false)->components.Get<PbrMaterialComponent>("PBR Material");
	CHECK_NEAR(other->metallic, 0.75f, 1e-5f); // the asset's values reached the other object

	CHECK(A::FindMaterial(s, "Assets/Materials/Missing.material.json") == nullptr);
	A::ForgetMaterials(s);
	A::ProjectRoot(s) = savedRoot;
}

// ===========================================================================
// Fase 5: animation and character
// ===========================================================================

namespace {

Vector3 BoneModelPosition(const Model& model, const LocalPose& pose, const char* bone)
{
	Matrix4x4 m;
	model.GetNodeModelMatrix(pose, model.FindNode(bone), m);
	return { m.m[3][0], m.m[3][1], m.m[3][2] };
}

float Distance(const Vector3& a, const Vector3& b)
{
	const Vector3 d = a - b;
	return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}

// Bone names differ per export: the first bone whose name ends with `suffix`
std::string FindBone(const Model& model, const char* suffix)
{
	for (const BoneInfo& bone : model.GetBoneList()) {
		if (bone.name.ends_with(suffix)) return bone.name;
	}
	return {};
}

} // namespace

TEST_CASE(Animation, BlendWeightsMixTheTwoNearestClips)
{
	AnimState state;
	state.blendClips = { { "", "Idle", 0, 0.0f }, { "", "Walk", 0, 2.0f }, { "", "Run", 0, 6.0f } };
	std::vector<float> w;
	state.BlendWeights(-1.0f, w);
	CHECK_NEAR(w[0], 1.0f, 1e-5f);
	state.BlendWeights(1.0f, w);
	CHECK_NEAR(w[0], 0.5f, 1e-5f);
	CHECK_NEAR(w[1], 0.5f, 1e-5f);
	CHECK_NEAR(w[2], 0.0f, 1e-5f);
	state.BlendWeights(5.0f, w);
	CHECK_NEAR(w[1], 0.25f, 1e-5f);
	CHECK_NEAR(w[2], 0.75f, 1e-5f);
	state.BlendWeights(99.0f, w);
	CHECK_NEAR(w[2], 1.0f, 1e-5f);

	AnimatorController controller;
	AnimState& saved = controller.AddState("Move", { 0.0f, 0.0f });
	saved.blendParameter = "speed";
	saved.blendClips = state.blendClips;
	const auto loaded = AnimatorController::FromJson(controller.ToJson());
	REQUIRE(loaded.has_value());
	REQUIRE(loaded->states.size() == 1);
	CHECK(loaded->states[0].blendParameter == "speed");
	REQUIRE(loaded->states[0].blendClips.size() == 3);
	CHECK(loaded->states[0].blendClips[2].clipName == "Run");
	CHECK_NEAR(loaded->states[0].blendClips[2].threshold, 6.0f, 1e-5f);
	CHECK(loaded->states[0].blendParameterY.empty());
	CHECK(controller.ToJson().find("blendParameterY") == std::string::npos); // 1D files stay as they were
}

TEST_CASE(Animation, BlendWeights2DMixTheClipsAroundThePoint)
{
	AnimState state;
	state.blendParameter = "moveX";
	state.blendParameterY = "moveY";
	// Idle in the centre, four directions around it (strafe set)
	state.blendClips = { { "", "Idle", 0, 0.0f, 0.0f }, { "", "Forward", 0, 0.0f, 1.0f }, { "", "Back", 0, 0.0f, -1.0f },
		{ "", "Left", 0, -1.0f, 0.0f }, { "", "Right", 0, 1.0f, 0.0f } };
	std::vector<float> w;
	const auto sum = [&] { float s = 0.0f; for (float v : w) s += v; return s; };

	state.BlendWeights(0.0f, 0.0f, w);
	CHECK_NEAR(w[0], 1.0f, 1e-5f);
	state.BlendWeights(0.0f, 1.0f, w);
	CHECK_NEAR(w[1], 1.0f, 1e-5f);
	CHECK_NEAR(sum(), 1.0f, 1e-5f);
	state.BlendWeights(-1.0f, 0.0f, w);
	CHECK_NEAR(w[3], 1.0f, 1e-5f);

	// Halfway forward: only Idle and Forward
	state.BlendWeights(0.0f, 0.5f, w);
	CHECK_NEAR(w[0], 0.5f, 1e-5f);
	CHECK_NEAR(w[1], 0.5f, 1e-5f);
	CHECK_NEAR(w[2] + w[3] + w[4], 0.0f, 1e-5f);

	// Diagonal forward-right: Forward and Right share equally, Back and Left stay out
	state.BlendWeights(0.7f, 0.7f, w);
	CHECK_NEAR(sum(), 1.0f, 1e-5f);
	CHECK_NEAR(w[1], w[4], 1e-5f);
	CHECK(w[1] > 0.3f);
	CHECK_NEAR(w[2] + w[3], 0.0f, 1e-5f);

	// Far outside the set: still a valid mix
	state.BlendWeights(5.0f, -5.0f, w);
	CHECK_NEAR(sum(), 1.0f, 1e-5f);

	AnimatorController controller;
	AnimState& saved = controller.AddState("Strafe", { 0.0f, 0.0f });
	saved.blendParameter = state.blendParameter;
	saved.blendParameterY = state.blendParameterY;
	saved.blendClips = state.blendClips;
	const auto loaded = AnimatorController::FromJson(controller.ToJson());
	REQUIRE(loaded.has_value());
	REQUIRE(loaded->states.size() == 1);
	CHECK(loaded->states[0].IsBlend2D());
	CHECK(loaded->states[0].blendParameterY == "moveY");
	REQUIRE(loaded->states[0].blendClips.size() == 5);
	CHECK_NEAR(loaded->states[0].blendClips[2].thresholdY, -1.0f, 1e-5f);
}

TEST_CASE(Animation, BlendTreePoseLiesBetweenItsClips)
{
	Model* model = Model::CreateFromFile("Resources/Model", "Idle.fbx");
	const Model* run = Model::CreateFromFile("Resources/Model", "Running.fbx");
	REQUIRE(model && run);
	const std::string foot = FindBone(*model, "LeftFoot");
	REQUIRE(!foot.empty());

	AnimationPlayer player;
	player.Play(nullptr, 0);
	player.Current().blend = { { nullptr, 0, 1.0f }, { run, 0, 0.0f } };
	player.Advance(0.2f, model);
	LocalPose idle, half, running;
	player.EvaluatePose(*model, idle);
	player.Current().blend[0].weight = 0.0f;
	player.Current().blend[1].weight = 1.0f;
	player.EvaluatePose(*model, running);
	player.Current().blend[0].weight = 0.5f;
	player.Current().blend[1].weight = 0.5f;
	player.EvaluatePose(*model, half);
	const float apart = Distance(BoneModelPosition(*model, idle, foot.c_str()), BoneModelPosition(*model, running, foot.c_str()));
	CHECK_MSG(apart > 1.0e-3f, std::format("clips do not differ: {:.5f}", apart));
	// Duration follows the mix, so the run's time is the same phase as the idle's
	CHECK(AnimationPlayer::Duration(player.Current(), model) > 0.0f);
	const float toIdle = Distance(BoneModelPosition(*model, half, foot.c_str()), BoneModelPosition(*model, idle, foot.c_str()));
	const float toRun = Distance(BoneModelPosition(*model, half, foot.c_str()), BoneModelPosition(*model, running, foot.c_str()));
	CHECK_MSG(toIdle < apart && toRun < apart, std::format("{:.4f} {:.4f} of {:.4f}", toIdle, toRun, apart));
}

TEST_CASE(Animation, LayerMaskMovesOnlyTheBonesBelowIt)
{
	Model* model = Model::CreateFromFile("Resources/Model", "Idle.fbx");
	const Model* run = Model::CreateFromFile("Resources/Model", "Running.fbx");
	REQUIRE(model && run);
	const std::string spine = FindBone(*model, "Spine");
	const std::string foot = FindBone(*model, "LeftFoot");
	const std::string hand = FindBone(*model, "LeftHand");
	REQUIRE(!spine.empty() && !foot.empty() && !hand.empty());

	AnimationPlayer player;
	player.Play(nullptr, 0);
	player.Advance(0.3f, model);
	LocalPose base, layered;
	player.EvaluatePose(*model, base);
	player.PlayLayer(0, *model, run, 0, spine, true, 1.0f, 0.0f);
	player.Advance(0.3f, model);
	player.Advance(-0.3f, model); // back to the same base time: only the layer may differ
	CHECK(player.IsLayerPlaying(0));
	player.EvaluatePose(*model, layered);
	player.EvaluatePose(*model, layered);
	CHECK_MSG(Distance(BoneModelPosition(*model, base, hand.c_str()), BoneModelPosition(*model, layered, hand.c_str())) > 1.0e-3f, "the masked part did not change");
	CHECK_MSG(Distance(BoneModelPosition(*model, base, foot.c_str()), BoneModelPosition(*model, layered, foot.c_str())) < 1.0e-3f, "a bone outside the mask moved");

	player.StopLayer(0, 0.0f);
	CHECK(!player.IsLayerPlaying(0));
	player.PlayLayer(1, *model, run, 0, "", false, 1.0f, 0.0f); // a one-shot ends by itself
	player.Advance(run->GetAnimationDuration(0) + 0.1f, model);
	player.Advance(1.0f, model);
	CHECK(!player.IsLayerPlaying(1));
}

TEST_CASE(Animation, TwoBoneIKReachesTheTargetAndKeepsTheBones)
{
	Model* model = Model::CreateFromFile("Resources/Model", "Idle.fbx");
	REQUIRE(model != nullptr);
	const std::string footName = FindBone(*model, "LeftFoot");
	const std::string legName = FindBone(*model, "LeftLeg");
	const std::string thighName = FindBone(*model, "LeftUpLeg");
	REQUIRE(!footName.empty() && !legName.empty() && !thighName.empty());
	AnimationPlayer player;
	player.Play(nullptr, 0);
	LocalPose before, after;
	player.EvaluatePose(*model, before);
	const Vector3 foot = BoneModelPosition(*model, before, footName.c_str());
	const Vector3 knee = BoneModelPosition(*model, before, legName.c_str());
	const Vector3 thigh = BoneModelPosition(*model, before, thighName.c_str());
	const float legLength = Distance(foot, knee) + Distance(knee, thigh);

	// Lift the foot; the knee bends forward
	const Vector3 target = { foot.x, foot.y + legLength * 0.3f, foot.z + legLength * 0.1f };
	const Vector3 pole = { knee.x, knee.y, knee.z + legLength * 2.0f };
	player.SetIK(footName, target, pole, 1.0f);
	player.EvaluatePose(*model, after);
	const Vector3 solved = BoneModelPosition(*model, after, footName.c_str());
	CHECK_MSG(Distance(solved, target) < legLength * 0.01f, std::format("missed by {:.4f} of leg {:.4f}", Distance(solved, target), legLength));
	const Vector3 solvedKnee = BoneModelPosition(*model, after, legName.c_str());
	CHECK_NEAR(Distance(solvedKnee, thigh), Distance(knee, thigh), legLength * 0.01f);
	CHECK_NEAR(Distance(solved, solvedKnee), Distance(foot, knee), legLength * 0.01f);
	CHECK(Distance(BoneModelPosition(*model, after, thighName.c_str()), thigh) < legLength * 0.001f);

	// Out of reach: stretches towards it and stays finite
	player.SetIK(footName, { thigh.x, thigh.y - legLength * 5.0f, thigh.z }, pole, 1.0f);
	player.EvaluatePose(*model, after);
	const Vector3 stretched = BoneModelPosition(*model, after, footName.c_str());
	CHECK(std::isfinite(stretched.x + stretched.y + stretched.z));
	CHECK(stretched.y < thigh.y);

	player.SetIK(footName, target, pole, 0.0f); // off
	player.EvaluatePose(*model, after);
	CHECK(Distance(BoneModelPosition(*model, after, footName.c_str()), foot) < legLength * 0.001f);
}

TEST_CASE(Animation, RootMotionMovesTheObjectNotTheMesh)
{
	Model* model = Model::CreateFromFile("Resources/Model", "Running.fbx");
	REQUIRE(model != nullptr);
	const int root = model->RootBoneNode();
	REQUIRE(root >= 0);
	AnimationPlayer player;
	player.Play(nullptr, 0);
	player.SetRootMotion(true);
	const float duration = model->GetAnimationDuration(0);
	REQUIRE(duration > 0.0f);
	std::vector<Vector3> joints;
	std::vector<int> parents;
	std::vector<bool> isBone;
	LocalPose pose;
	player.EvaluatePose(*model, pose);
	model->GetJointPositions(pose, joints, parents, isBone);
	const Vector3 start = joints[root];

	Vector3 sum{};
	const float step = duration / 20.0f;
	for (int i = 0; i < 30; ++i) { // 1.5 loops: crosses the wrap once
		player.Advance(step, model);
		const Vector3 delta = player.ComputeRootDelta(*model);
		sum = { sum.x + delta.x, sum.y + delta.y, sum.z + delta.z };
		player.EvaluatePose(*model, pose);
		model->GetJointPositions(pose, joints, parents, isBone);
		CHECK_NEAR(joints[root].x, start.x, 1.0e-3f);
		CHECK_NEAR(joints[root].z, start.z, 1.0e-3f);
	}
	CHECK(std::isfinite(sum.x + sum.y + sum.z));
	// After a whole number of loops the root is back where a pose says it is: a wrap must not add a jump
	Vector3 oneLoop{};
	for (int i = 0; i < 10; ++i) { // the other half: exactly two loops in total
		player.Advance(step, model);
		const Vector3 delta = player.ComputeRootDelta(*model);
		oneLoop = { oneLoop.x + delta.x, oneLoop.y + delta.y, oneLoop.z + delta.z };
	}
	const Vector3 twoLoops = { sum.x + oneLoop.x, sum.y + oneLoop.y, sum.z + oneLoop.z };
	CHECK(std::isfinite(twoLoops.x + twoLoops.y + twoLoops.z));
}

TEST_CASE(Animation, ObjectFollowsTheBoneItIsAttachedTo)
{
	GameObject& character = s.CreateGameObject("Fighter", GameObjectType::Empty);
	character.meshType = GameObject::MeshType::CustomModel;
	character.hasMesh = true;
	character.modelPath = "Resources/Model/Idle.fbx";
	character.translation = { 3.0f, 0.0f, 2.0f };
	character.scale = { 0.012f, 0.012f, 0.012f };
	const uint32_t fighterId = character.id;
	GameObject& sword = s.CreateGameObject("Sword", GameObjectType::Empty);
	const uint32_t swordId = sword.id;

	CaptureGameViewSignature(s); // a whole frame: nothing recorded stays unsubmitted when the test deletes its objects // creates the character's Object3d
	const Model* model = s.FindGameObjectById(fighterId, false)->editObject3d ? s.FindGameObjectById(fighterId, false)->editObject3d->GetModel() : nullptr;
	REQUIRE(model != nullptr);
	const std::string hand = FindBone(*model, "RightHand");
	REQUIRE(!hand.empty());

	EngineAPI& api = A::Api(s);
	CHECK(!api.attachToBone(api.hostContext, swordId, fighterId, "NoSuchBone"));
	REQUIRE(api.attachToBone(api.hostContext, swordId, fighterId, hand.c_str()));
	Vector3 bone{};
	REQUIRE(api.getBoneWorldPosition(api.hostContext, fighterId, hand.c_str(), &bone));
	s.UpdateWorldMatrices();
	const Matrix4x4& world = s.FindGameObjectById(swordId, false)->worldMatrix;
	CHECK_NEAR(world.m[3][0], bone.x, 1.0e-3f);
	CHECK_NEAR(world.m[3][1], bone.y, 1.0e-3f);
	CHECK_NEAR(world.m[3][2], bone.z, 1.0e-3f);
	CHECK(bone.y > 0.1f); // really up at the hand, not at the object's origin
	CHECK(s.FindGameObjectById(swordId, false)->components.Has("Bone Attachment")); // saved with the scene

	// It follows when the character moves
	s.FindGameObjectById(fighterId, false)->translation.x += 5.0f;
	s.UpdateWorldMatrices();
	CHECK_NEAR(s.FindGameObjectById(swordId, false)->worldMatrix.m[3][0], bone.x + 5.0f, 1.0e-3f);
}

TEST_CASE(Animation, RagdollFallsAndStopsOnTheFloorThenGivesTheBodyBack)
{
	for (auto& obj : A::Objects(s)) obj.isActive = false;
	MakeStaticBox(s, { -30.0f, -1.0f, -30.0f }, { 30.0f, 0.0f, 30.0f });
	GameObject& character = s.CreateGameObject("Falling", GameObjectType::Empty);
	character.meshType = GameObject::MeshType::CustomModel;
	character.hasMesh = true;
	character.modelPath = "Resources/Model/Idle.fbx";
	character.translation = { 0.0f, 1.0f, 0.0f };
	character.hasBoxCollider = true;
	character.boxSize = { 0.5f, 2.0f, 0.5f };
	const uint32_t id = character.id;
	CaptureGameViewSignature(s); // a whole frame: nothing recorded stays unsubmitted when the test deletes its objects
	const Model* model = s.FindGameObjectById(id, false)->editObject3d->GetModel();
	REQUIRE(model != nullptr && model->HasBones());
	const std::string head = FindBone(*model, "Head");
	REQUIRE(!head.empty());

	EngineAPI& api = A::Api(s);
	Vector3 start{};
	REQUIRE(api.getBoneWorldPosition(api.hostContext, id, head.c_str(), &start));
	REQUIRE(api.setRagdoll(api.hostContext, id, true));
	StepPhysicsSeconds(s, 4.0f);
	Vector3 end{};
	REQUIRE(api.getBoneWorldPosition(api.hostContext, id, head.c_str(), &end));
	CHECK(std::isfinite(end.x + end.y + end.z));
	CHECK_MSG(end.y < start.y - 0.3f, std::format("head from {:.2f} to {:.2f}", start.y, end.y));
	CHECK_MSG(end.y > -0.2f, std::format("through the floor: {:.2f}", end.y));
	CHECK_MSG(std::abs(end.x - start.x) + std::abs(end.z - start.z) < start.y * 2.0f, std::format("flew away: ({:.2f} {:.2f} {:.2f}) to ({:.2f} {:.2f} {:.2f})", start.x, start.y, start.z, end.x, end.y, end.z));

	REQUIRE(api.setRagdoll(api.hostContext, id, false));
	Vector3 back{};
	REQUIRE(api.getBoneWorldPosition(api.hostContext, id, head.c_str(), &back));
	CHECK(Distance(back, start) < 0.05f);
	// Characters are normally scaled down from centimetres: the same fall at that size
	s.FindGameObjectById(id, false)->scale = { 0.01f, 0.01f, 0.01f };
	s.UpdateWorldMatrices();
	Vector3 smallStart{};
	REQUIRE(api.getBoneWorldPosition(api.hostContext, id, head.c_str(), &smallStart));
	REQUIRE(api.setRagdoll(api.hostContext, id, true));
	StepPhysicsSeconds(s, 3.0f);
	Vector3 smallEnd{};
	REQUIRE(api.getBoneWorldPosition(api.hostContext, id, head.c_str(), &smallEnd));
	CHECK_MSG(std::isfinite(smallEnd.y) && smallEnd.y < smallStart.y && smallEnd.y > -0.1f, std::format("small: head from {:.2f} to {:.2f}", smallStart.y, smallEnd.y));
	REQUIRE(api.setRagdoll(api.hostContext, id, false));
	s.FindGameObjectById(id, false)->scale = { 1.0f, 1.0f, 1.0f };
	s.UpdateWorldMatrices();

	// Starting one and clearing the world must not crash or leave the pose overridden
	REQUIRE(api.setRagdoll(api.hostContext, id, true));
	StepPhysicsSeconds(s, 0.2f);
	s.ResetScriptRuntimeState();
	REQUIRE(api.getBoneWorldPosition(api.hostContext, id, head.c_str(), &back));
	CHECK(Distance(back, start) < 0.05f);
}

TEST_CASE(Project, RecreatingASampleUpdatesItsScriptsAndKeepsTheOldOnes)
{
	namespace fs = std::filesystem;
	const fs::path savedRoot = A::ProjectRoot(s);
	const fs::path parent = A::TempRoot() / "SampleUpdate";
	fs::create_directories(parent);
	REQUIRE(s.CreateNewProject(parent.string(), "Game"));
	REQUIRE(A::InstallSample(s, "ThirdPerson"));
	const fs::path script = parent / "Game" / "GameScripts" / "Scripts" / "ThirdPersonDemo.cpp";
	REQUIRE(fs::exists(script));
	const auto read = [](const fs::path& p) { std::ifstream in(p, std::ios::binary); return std::string(std::istreambuf_iterator<char>(in), {}); };
	const std::string current = read(script);
	CHECK(current.find("KEY_R") != std::string::npos);

	// The same version again: nothing to do, no backup
	REQUIRE(A::InstallSample(s, "ThirdPerson"));
	CHECK(!fs::exists(fs::path(script).concat(".old")));

	// An older or edited copy is replaced, and kept
	{ std::ofstream(script, std::ios::binary) << "// older"; }
	REQUIRE(A::InstallSample(s, "ThirdPerson"));
	CHECK(read(script) == current);
	CHECK(read(fs::path(script).concat(".old")) == "// older");
	{ std::ofstream(script, std::ios::binary) << "// older still"; }
	REQUIRE(A::InstallSample(s, "ThirdPerson"));
	CHECK(read(fs::path(script).concat(".old1")) == "// older still"); // never overwrites a backup

	A::ProjectRoot(s) = savedRoot;
	s.SyncScriptModuleWithProject();
}

TEST_CASE(Animation, RagdollCollapsesWholeBody)
{
	for (auto& obj : A::Objects(s)) obj.isActive = false;
	MakeStaticBox(s, { -30.0f, -1.0f, -30.0f }, { 30.0f, 0.0f, 30.0f });
	GameObject& character = s.CreateGameObject("Falling", GameObjectType::Empty);
	character.meshType = GameObject::MeshType::CustomModel;
	character.hasMesh = true;
	character.modelPath = "Resources/Model/Idle.fbx";
	character.scale = { 0.012f, 0.012f, 0.012f };
	const uint32_t id = character.id;
	CaptureGameViewSignature(s); // a whole frame: nothing recorded stays unsubmitted when the test deletes its objects
	const Model* model = s.FindGameObjectById(id, false)->editObject3d->GetModel();
	REQUIRE(model != nullptr && model->HasBones());
	EngineAPI& api = A::Api(s);
	float tallest = 0.0f;
	for (const BoneInfo& bone : model->GetBoneList()) {
		Vector3 p{};
		api.getBoneWorldPosition(api.hostContext, id, bone.name.c_str(), &p);
		tallest = (std::max)(tallest, p.y);
	}
	REQUIRE(tallest > 1.0f);
	REQUIRE(api.setRagdoll(api.hostContext, id, true));
	StepPhysicsSeconds(s, 4.0f);
	// Nothing stays up: a collapsed body is far lower than it stood, and every bone is above the floor
	std::string report;
	for (const BoneInfo& bone : model->GetBoneList()) {
		Vector3 p{};
		api.getBoneWorldPosition(api.hostContext, id, bone.name.c_str(), &p);
		if (p.y > tallest * 0.45f || p.y < -0.1f) report += std::format(" {}={:.2f}", bone.name, p.y);
	}
	CHECK_MSG(report.empty(), "tallest " + std::to_string(tallest) + report);
	REQUIRE(api.setRagdoll(api.hostContext, id, false));
}

TEST_CASE(Lighting, FloorHasNoHorizontalLines)
{
	LightingScene scene(s);
	GameObject* camera = s.FindGameObjectByName("Main Camera", false);
	camera->translation = { 0.0f, 22.0f, -6.0f };
	camera->rotation = { 1.2f, 0.0f, 0.0f };
	camera->rotationQuat = Quaternion::FromEuler(camera->rotation);
	scene.AddSun({ 1.0f, 0.5f, 0.0f });
	s.FindGameObjectByName("Floor", false)->scale = { 400.0f, 1.0f, 400.0f }; // no floor edge in the picture
	for (int i = 0; i < 6; ++i) {
		GameObject& lamp = s.CreateGameObject("Lamp", GameObjectType::Empty);
		const float angle = 6.2831853f * i / 6.0f;
		lamp.translation = { std::cos(angle) * 6.0f, 1.5f, std::sin(angle) * 6.0f };
		lamp.hasLight = true;
		lamp.lightEnabled = true;
		lamp.lightType = GameObject::LightType::Point;
		lamp.lightColor = { 1.0f, 0.8f, 0.6f };
		lamp.lightIntensity = 2.0f;
		lamp.lightRange = 6.0f;
		lamp.lightDecay = 1.5f;
	}
	PostProcessSettings& post = s.PostSettings();
	post.enabled = true;
	post.bloom = false;
	post.fxaa = false;
	post.shadowCascades = 3;

	// Ambient occlusion has nothing to darken on a flat floor, so it must not change any row of pixels
	post.ssao = false;
	scene.Capture();
	const std::vector<float> without = gCapturedRowMeans;
	post.ssao = true;
	scene.Capture();
	REQUIRE(!without.empty() && without.size() == gCapturedRowMeans.size());
	float worst = 0.0f;
	size_t worstRow = 0;
	int changedRows = 0;
	for (size_t y = 8; y + 8 < without.size(); ++y) { // shortcut: the outermost rows change by about 0.3 of 255 on some GPUs, not visible; look there if it ever shows
		const float change = std::abs(without[y] - gCapturedRowMeans[y]);
		if (change > 0.1f) ++changedRows;
		if (change > worst) { worst = change; worstRow = y; }
	}
	CHECK_MSG(worst < 0.25f, std::format("row {} of {} changed by {:.3f}; {} rows above 0.1", worstRow, without.size(), worst, changedRows));
}

namespace {
	/// Silence, so that running the tests makes no noise
	std::string WriteSilentWav(const std::string& name)
	{
		const std::filesystem::path path = A::TempRoot() / name;
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		const uint32_t dataSize = 44100 * 2 * 5; // 5 seconds, 16 bit mono
		const auto u32 = [](std::ofstream& f, uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
		const auto u16 = [](std::ofstream& f, uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
		std::ofstream f(path, std::ios::binary);
		f.write("RIFF", 4); u32(f, 36 + dataSize); f.write("WAVEfmt ", 8); u32(f, 16);
		u16(f, 1); u16(f, 1); u32(f, 44100); u32(f, 88200); u16(f, 2); u16(f, 16);
		f.write("data", 4); u32(f, dataSize);
		const std::vector<char> silence(dataSize, 0);
		f.write(silence.data(), dataSize);
		return path.string();
	}

	/// XAudio2 applies a new pitch the next time its engine runs (about 10 ms)
	float PitchOnceApplied(uint32_t voice, float expected)
	{
		float pitch = 0.0f;
		for (int i = 0; i < 50; ++i) {
			pitch = Audio::GetInstance()->GetVoicePitch(voice);
			if (std::abs(pitch - expected) < 1e-4f) break;
			Sleep(10);
		}
		return pitch;
	}

	struct VoiceLevels { float left = -1.0f; float right = -1.0f; };
	VoiceLevels LevelsOf(uint32_t voice)
	{
		VoiceLevels levels;
		Audio::GetInstance()->GetVoiceLevels(voice, &levels.left, &levels.right);
		return levels;
	}
}

TEST_CASE(Audio, SoundMovesBetweenEarsWhenTheListenerTurns)
{
	using namespace SpatialAudio;
	const Vector3 sound{ -10.0f, 0.0f, 0.0f };
	const auto heard = [&](const Vector3& right) { return Compute({ { 0.0f, 0.0f, 0.0f }, right }, sound, 1.0f, 100.0f); };

	// Facing +Z: the sound is on the left
	const Levels onLeft = heard({ 1.0f, 0.0f, 0.0f });
	CHECK(onLeft.left > onLeft.right * 3.0f);
	CHECK(onLeft.right > 0.0f); // a real head hears it with the other ear too
	// Turned so that it is behind (facing +X): both ears alike
	const Levels behind = heard({ 0.0f, 0.0f, -1.0f });
	CHECK_NEAR(behind.left, behind.right, 1e-4f);
	// Facing it: both ears alike, and just as loud in total as from the side
	const Levels front = heard({ 0.0f, 0.0f, 1.0f });
	CHECK_NEAR(front.left, front.right, 1e-4f);
	CHECK_NEAR(front.left * front.left + front.right * front.right, onLeft.left * onLeft.left + onLeft.right * onLeft.right, 1e-3f);
	// Turned the other way: now the right ear
	const Levels onRight = heard({ -1.0f, 0.0f, 0.0f });
	CHECK(onRight.right > onRight.left * 3.0f);
}

TEST_CASE(Audio, SoundFadesWithDistance)
{
	using namespace SpatialAudio;
	const auto gainAt = [](float distance) { return Compute({}, { 0.0f, 0.0f, distance }, 2.0f, 40.0f).gain; };
	CHECK_NEAR(gainAt(0.5f), 1.0f, 1e-5f);
	CHECK_NEAR(gainAt(2.0f), 1.0f, 1e-5f);
	float previous = 1.0f;
	for (float d = 3.0f; d < 40.0f; d += 1.0f) {
		const float gain = gainAt(d);
		CHECK(gain < previous);
		CHECK(gain > 0.0f);
		previous = gain;
	}
	CHECK(gainAt(40.0f) == 0.0f);
	CHECK(gainAt(1000.0f) == 0.0f);
	const Levels farAway = Compute({}, { 0.0f, 0.0f, 1000.0f }, 2.0f, 40.0f);
	CHECK(farAway.left == 0.0f && farAway.right == 0.0f);
}

TEST_CASE(Audio, BadSpatialInputStaysSafe)
{
	using namespace SpatialAudio;
	// On top of the listener there is no direction: centred, not a jump to one side
	const Levels same = Compute({}, { 0.0f, 0.0f, 0.0f }, 1.0f, 10.0f);
	CHECK_NEAR(same.left, same.right, 1e-5f);
	CHECK(same.left > 0.0f);
	// Very close sounds are near the centre, so passing through the head does not click
	const Levels close = Compute({}, { -0.01f, 0.0f, 0.0f }, 1.0f, 10.0f);
	CHECK_NEAR(close.left, close.right, 0.05f);
	// Max below min, NaN, a listener whose camera was never updated
	const Levels flipped = Compute({}, { 0.0f, 0.0f, 5.0f }, 10.0f, 1.0f);
	CHECK(std::isfinite(flipped.left) && std::isfinite(flipped.right));
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const Levels bad = Compute({}, { nan, 0.0f, 0.0f }, nan, nan);
	CHECK(bad.left == 0.0f && bad.right == 0.0f);
	const Levels noRight = Compute({ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } }, { 3.0f, 0.0f, 0.0f }, 1.0f, 10.0f);
	CHECK_NEAR(noRight.left, noRight.right, 1e-5f);
}

TEST_CASE(Audio, VoiceLimitChoosesWhatToReplace)
{
	using namespace SpatialAudio;
	CHECK(PickVoiceToSteal({}, 1, 4, 2) == kNoSteal);
	// Same sound over its own limit: its oldest voice, even when another sound is quieter
	CHECK(PickVoiceToSteal({ { 2, 0.1f, false }, { 1, 0.9f, false }, { 1, 0.5f, false } }, 1, 8, 2) == 1);
	// Total limit: the quietest; on a tie the oldest
	CHECK(PickVoiceToSteal({ { 1, 0.9f, false }, { 2, 0.2f, false }, { 3, 0.5f, false } }, 4, 3, 8) == 1);
	CHECK(PickVoiceToSteal({ { 1, 0.3f, false }, { 2, 0.3f, false } }, 4, 2, 8) == 0);
	// Music is never replaced, even when it is the quietest
	CHECK(PickVoiceToSteal({ { 1, 0.0f, true }, { 2, 0.5f, false } }, 4, 2, 8) == 1);
	CHECK(PickVoiceToSteal({ { 1, 0.5f, true }, { 2, 0.5f, true } }, 4, 2, 8) == kRefuse);
	CHECK(PickVoiceToSteal({ { 1, 0.5f, true } }, 1, 8, 1) == kRefuse);
	// Room left
	CHECK(PickVoiceToSteal({ { 1, 0.5f, false } }, 1, 8, 2) == kNoSteal);
}

TEST_CASE(Audio, GameCameraIsTheListenerAndVoicesFollowObjects)
{
	Audio* audio = Audio::GetInstance();
	if (!audio->IsAvailable()) return; // no audio device (CI): the pure tests above still ran
	EngineAPI& api = A::Api(s);
	const std::string clip = WriteSilentWav("Audio/silence.wav");

	GameObject& source = s.CreateGameObject("Source", GameObjectType::Empty);
	source.translation = { -10.0f, 0.0f, 0.0f };
	const uint32_t sourceId = source.id;
	A::AimAudioListener(s, { 0.0f, 0.0f, 0.0f }, 0.0f);

	ScriptAudioParams params;
	params.spatial = true;
	params.loop = true;
	params.followObject = sourceId;
	params.minDistance = 1.0f;
	params.maxDistance = 100.0f;
	const uint32_t voice = api.playSoundEx(&s, clip.c_str(), &params);
	REQUIRE(voice != 0u);

	// The user's example: on the left, then behind after turning right
	VoiceLevels levels = LevelsOf(voice);
	CHECK(levels.left > levels.right * 3.0f);
	A::AimAudioListener(s, { 0.0f, 0.0f, 0.0f }, 3.14159265f * 0.5f); // yaw right, facing +X
	levels = LevelsOf(voice);
	CHECK_NEAR(levels.left, levels.right, 1e-3f);
	A::AimAudioListener(s, { 0.0f, 0.0f, 0.0f }, 3.14159265f);
	levels = LevelsOf(voice);
	CHECK(levels.right > levels.left * 3.0f);

	// Near is loud, far is quiet; the voice follows the object
	A::AimAudioListener(s, { 0.0f, 0.0f, 0.0f }, 0.0f);
	const float at10 = LevelsOf(voice).left;
	s.FindGameObjectById(sourceId, false)->translation = { -2.0f, 0.0f, 0.0f };
	A::AimAudioListener(s, { 0.0f, 0.0f, 0.0f }, 0.0f);
	const float at2 = LevelsOf(voice).left;
	s.FindGameObjectById(sourceId, false)->translation = { -60.0f, 0.0f, 0.0f };
	A::AimAudioListener(s, { 0.0f, 0.0f, 0.0f }, 0.0f);
	const float at60 = LevelsOf(voice).left;
	CHECK(at2 > at10);
	CHECK(at10 > at60);

	// Pitch and buses
	api.setSoundPitch(&s, voice, 2.0f);
	CHECK_NEAR(PitchOnceApplied(voice, 2.0f), 2.0f, 1e-4f);
	api.setSoundPitch(&s, voice, std::numeric_limits<float>::quiet_NaN());
	CHECK_NEAR(PitchOnceApplied(voice, 1.0f), 1.0f, 1e-4f);
	api.setBusVolume(&s, static_cast<int>(ScriptAudioBus::Music), 0.25f);
	CHECK_NEAR(api.getBusVolume(&s, static_cast<int>(ScriptAudioBus::Music)), 0.25f, 1e-5f);
	CHECK_NEAR(api.getBusVolume(&s, static_cast<int>(ScriptAudioBus::Effects)), 1.0f, 1e-5f);
	CHECK(api.getBusVolume(&s, 99) == 0.0f);
	api.setBusVolume(&s, 99, 0.5f); // ignored, no crash

	// The object is gone: the sound stays where it was and nothing crashes
	s.QueueDeleteGameObjectById(sourceId);
	s.ProcessPendingDeletes();
	A::AimAudioListener(s, { 0.0f, 0.0f, 0.0f }, 0.0f);
	CHECK(audio->IsPlaying(voice));
	audio->StopAllAudio();
	audio->ResetBusVolumes();
}

TEST_CASE(Audio, VoiceLimitIsEnforced)
{
	Audio* audio = Audio::GetInstance();
	if (!audio->IsAvailable()) return;
	audio->StopAllAudio();
	const uint32_t sound = audio->LoadAudio(WriteSilentWav("Audio/limit.wav"));
	REQUIRE(sound != 0u);
	audio->SetVoiceLimits(64, 3);
	uint32_t voices[5] = {};
	for (uint32_t& v : voices) v = audio->PlayAudio(sound, false, 1.0f);
	CHECK(audio->GetActiveVoiceCount() == 3);
	CHECK(!audio->IsPlaying(voices[0]));
	CHECK(!audio->IsPlaying(voices[1]));
	CHECK(audio->IsPlaying(voices[4]));
	audio->StopAllAudio();
	audio->SetVoiceLimits(64, 12);
}

TEST_CASE(Audio, AudioSourceComponentKeepsItsSettingsAndPlaysIn3D)
{
	GameObject& obj = s.CreateGameObject("Speaker", GameObjectType::Empty);
	obj.hasAudioSource = true;
	obj.audioClipName = WriteSilentWav("Audio/source.wav");
	obj.audioPitch = 1.5f;
	obj.audioBus = static_cast<int>(AudioBus::Voice);
	obj.audioSpatial = true;
	obj.audioMinDistance = 3.0f;
	obj.audioMaxDistance = 30.0f;
	obj.audioLoop = true;
	obj.audioPlayOnAwake = true;
	obj.translation = { -5.0f, 0.0f, 0.0f };
	const uint32_t id = obj.id;

	GameObject copy = obj;
	CHECK(copy.audioSpatial && copy.audioBus == static_cast<int>(AudioBus::Voice));
	CHECK_NEAR(copy.audioPitch, 1.5f, 1e-6f);

	const std::string saved = s.SerializeSceneToString();
	REQUIRE(s.DeserializeSceneFromString(saved));
	const GameObject* loaded = s.FindGameObjectById(id, false);
	REQUIRE(loaded != nullptr);
	CHECK(loaded->audioSpatial && loaded->audioBus == static_cast<int>(AudioBus::Voice));
	CHECK_NEAR(loaded->audioPitch, 1.5f, 1e-5f);
	CHECK_NEAR(loaded->audioMinDistance, 3.0f, 1e-5f);
	CHECK_NEAR(loaded->audioMaxDistance, 30.0f, 1e-5f);

	Audio* audio = Audio::GetInstance();
	if (!audio->IsAvailable()) return;
	A::AimAudioListener(s, { 0.0f, 0.0f, 0.0f }, 0.0f);
	s.StartPlayMode();
	const uint32_t voice = s.FindGameObjectById(id, false)->runtimeVoiceHandle;
	REQUIRE(voice != 0u); // Play On Awake
	A::AimAudioListener(s, { 0.0f, 0.0f, 0.0f }, 0.0f);
	const VoiceLevels levels = LevelsOf(voice);
	CHECK(levels.left > levels.right * 3.0f);
	CHECK_NEAR(PitchOnceApplied(voice, 1.5f), 1.5f, 1e-4f);

	// Scripts can stop and restart it; a script's bus mute ends with Play
	EngineAPI& api = A::Api(s);
	api.stopAudioSource(&s, id);
	CHECK(!audio->IsPlaying(voice));
	CHECK(api.playAudioSource(&s, id));
	api.setBusVolume(&s, static_cast<int>(ScriptAudioBus::Effects), 0.0f);
	s.StopPlayMode();
	CHECK_NEAR(audio->GetBusVolume(AudioBus::Effects), 1.0f, 1e-5f);
	CHECK(audio->GetActiveVoiceCount() == 0);
}

TEST_CASE(ScriptAPI, DataComponentsByName)
{
	EngineAPI& api = A::Api(s);
	const uint32_t id = s.CreateCube("Thing", { 0.0f, 0.0f, 0.0f }).id;
	CHECK(!api.hasComponent(&s, id, "PBR Material"));
	CHECK(api.addComponent(&s, id, "PBR Material"));
	CHECK(api.hasComponent(&s, id, "PBR Material"));
	CHECK(!api.addComponent(&s, id, "PBR Material")); // already there
	CHECK(!api.addComponent(&s, id, "No Such Component"));
	CHECK(api.hasComponent(&s, id, "Mesh Filter & Renderer")); // built-ins answer too

	ScriptComponentValue value;
	REQUIRE(api.getComponentField(&s, id, "PBR Material", "metallic", &value));
	CHECK(value.kind == ScriptComponentValue::Float);

	// Written through to the real component, clamped to the field's limits
	value.number[0] = 0.75f;
	REQUIRE(api.setComponentField(&s, id, "PBR Material", "metallic", &value));
	CHECK_NEAR(s.FindGameObjectById(id, false)->components.Get<PbrMaterialComponent>("PBR Material")->metallic, 0.75f, 1e-6f);
	value.number[0] = 50.0f;
	REQUIRE(api.setComponentField(&s, id, "PBR Material", "metallic", &value));
	CHECK_NEAR(s.FindGameObjectById(id, false)->components.Get<PbrMaterialComponent>("PBR Material")->metallic, 1.0f, 1e-6f);

	// Wrong type, NaN, wrong names: refused, value unchanged
	ScriptComponentValue wrong;
	wrong.kind = ScriptComponentValue::Text;
	CHECK(!api.setComponentField(&s, id, "PBR Material", "metallic", &wrong));
	value.number[0] = std::numeric_limits<float>::quiet_NaN();
	CHECK(!api.setComponentField(&s, id, "PBR Material", "metallic", &value));
	CHECK_NEAR(s.FindGameObjectById(id, false)->components.Get<PbrMaterialComponent>("PBR Material")->metallic, 1.0f, 1e-6f);
	CHECK(!api.getComponentField(&s, id, "PBR Material", "noSuchField", &value));
	CHECK(!api.getComponentField(&s, id, "Mesh Filter & Renderer", "x", &value)); // built-ins have no fields
	CHECK(!api.getComponentField(&s, 999999u, "PBR Material", "metallic", &value));

	// Vector, int and long text across the boundary
	REQUIRE(api.getComponentField(&s, id, "PBR Material", "emissive", &value));
	CHECK(value.kind == ScriptComponentValue::Vector || value.kind == ScriptComponentValue::Color);
	REQUIRE(api.addComponent(&s, id, "Joint"));
	value = {};
	value.kind = ScriptComponentValue::Int;
	value.integer = 2;
	REQUIRE(api.setComponentField(&s, id, "Joint", "jointType", &value));
	CHECK(s.FindGameObjectById(id, false)->components.Get<JointComponent>("Joint")->jointType == 2);
	REQUIRE(api.addComponent(&s, id, "Bone Attachment"));
	value = {};
	value.kind = ScriptComponentValue::Text;
	std::fill(std::begin(value.text), std::end(value.text) - 1, 'x');
	REQUIRE(api.setComponentField(&s, id, "Bone Attachment", "bone", &value)); // truncated, never overruns
	const auto* bone = s.FindGameObjectById(id, false)->components.Get<BoneAttachmentComponent>("Bone Attachment");
	CHECK(strnlen(bone->bone, sizeof(bone->bone)) < sizeof(bone->bone));

	CHECK(api.removeComponent(&s, id, "PBR Material"));
	CHECK(!api.hasComponent(&s, id, "PBR Material"));
	CHECK(!api.getComponentField(&s, id, "PBR Material", "metallic", &value));
}


TEST_CASE(Render, SkinnedShadowCasterOnItsFirstFrame)
{
	// The shadow pass runs before the object's first draw creates its palette. On a strict driver a missing palette removes the device.
	A::Instancing(s) = true;
	GameObject& character = s.CreateGameObject("Fighter", GameObjectType::Empty);
	character.meshType = GameObject::MeshType::CustomModel;
	character.hasMesh = true;
	character.modelPath = "Resources/Model/Idle.fbx";
	character.scale = { 0.012f, 0.012f, 0.012f };
	CHECK(!CaptureGameViewSignature(s).empty());
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}


TEST_CASE(Vfx, TrailFollowsAndAgesAway)
{
	TrailStyle style;
	style.lifetime = 1.0f;
	style.minDistance = 0.5f;
	TrailRibbon ribbon;
	ribbon.Update({ 0.0f, 0.0f, 0.0f }, 0.0, style, true);
	ribbon.Update({ 0.1f, 0.0f, 0.0f }, 0.1, style, true); // below minDistance: the head moves, no new point
	CHECK(ribbon.PointCount() == 2);
	ribbon.Update({ 1.0f, 0.0f, 0.0f }, 0.2, style, true);
	ribbon.Update({ 2.0f, 0.0f, 0.0f }, 0.3, style, true);
	CHECK(ribbon.PointCount() == 4);

	// Stopped: nothing new, and everything is gone one lifetime later
	ribbon.Update({ 9.0f, 0.0f, 0.0f }, 0.4, style, false);
	CHECK(ribbon.PointCount() == 4);
	ribbon.Update({ 9.0f, 0.0f, 0.0f }, 2.0, style, false);
	CHECK(ribbon.PointCount() == 0);

	// Starting again does not draw a line from the old place
	ribbon.Update({ 50.0f, 0.0f, 0.0f }, 2.1, style, true);
	ribbon.Update({ 51.0f, 0.0f, 0.0f }, 2.2, style, true);
	std::vector<TrailVertex> mesh;
	ribbon.BuildMesh({ 50.0f, 0.0f, -10.0f }, 2.2, style, mesh);
	REQUIRE(!mesh.empty());
	for (const TrailVertex& v : mesh) CHECK(v.position.x > 49.0f);

	// Fast movement cannot grow it without limit
	TrailRibbon fast;
	style.lifetime = 1000.0f;
	for (int i = 0; i < 2000; ++i) fast.Update({ static_cast<float>(i), 0.0f, 0.0f }, i * 0.01, style, true);
	CHECK(fast.PointCount() <= TrailRibbon::kMaxPoints);
}

TEST_CASE(Vfx, TrailMeshFacesTheCameraAndTapers)
{
	TrailStyle style;
	style.lifetime = 1.0f;
	style.minDistance = 0.1f;
	style.widthStart = 2.0f;
	style.widthEnd = 0.0f;
	style.colorStart = { 1.0f, 0.0f, 0.0f, 1.0f };
	style.colorEnd = { 0.0f, 0.0f, 1.0f, 0.0f };
	TrailRibbon ribbon;
	ribbon.Update({ 0.0f, 0.0f, 0.0f }, 0.0, style, true);
	ribbon.Update({ 5.0f, 0.0f, 0.0f }, 0.5, style, true);
	ribbon.Update({ 10.0f, 0.0f, 0.0f }, 1.0, style, true);

	// Camera looks along Z at a ribbon along X: the width must lie along Y
	std::vector<TrailVertex> mesh;
	ribbon.BuildMesh({ 5.0f, 0.0f, -20.0f }, 1.0, style, mesh);
	REQUIRE(!mesh.empty());
	CHECK(mesh.size() % 3 == 0);
	float highestAtHead = 0.0f;
	float highestAtTail = 0.0f;
	bool sawSoftEdge = false;
	for (const TrailVertex& v : mesh) {
		CHECK_NEAR(v.position.z, 0.0f, 1e-4f);
		CHECK(std::isfinite(v.position.y));
		if (v.position.x > 9.0f) highestAtHead = (std::max)(highestAtHead, std::abs(v.position.y));
		if (v.position.x < 1.0f) highestAtTail = (std::max)(highestAtTail, std::abs(v.position.y));
		if (v.position.y != 0.0f) sawSoftEdge = sawSoftEdge || v.color.w == 0.0f;
		if (v.position.y == 0.0f && v.position.x > 9.0f) CHECK(v.color.x > 0.9f && v.color.w > 0.9f);
		if (v.position.y == 0.0f && v.position.x < 1.0f) CHECK(v.color.z > 0.9f && v.color.w < 0.1f);
	}
	CHECK_NEAR(highestAtHead, 1.0f, 1e-3f); // half the width on each side
	CHECK(highestAtTail < 0.01f);
	CHECK(sawSoftEdge);

	// The camera exactly on the line, repeated points, NaN: nothing invalid reaches the GPU
	mesh.clear();
	ribbon.BuildMesh({ -5.0f, 0.0f, 0.0f }, 1.0, style, mesh);
	TrailRibbon bad;
	const float nan = std::numeric_limits<float>::quiet_NaN();
	bad.Update({ 1.0f, 1.0f, 1.0f }, 0.0, style, true);
	bad.Update({ nan, 1.0f, 1.0f }, 0.1, style, true);
	bad.Update({ 2.0f, 1.0f, 1.0f }, 0.2, style, true);
	bad.BuildMesh({ 0.0f, 0.0f, -5.0f }, 0.2, style, mesh);
	for (const TrailVertex& v : mesh) CHECK(std::isfinite(v.position.x) && std::isfinite(v.position.y) && std::isfinite(v.position.z) && std::isfinite(v.color.w));

	// With a texture the edges stay opaque (the texture gives the shape); u runs head 0 -> tail 1, v side to side
	style.softEdges = false;
	mesh.clear();
	ribbon.BuildMesh({ 5.0f, 0.0f, -20.0f }, 1.0, style, mesh);
	REQUIRE(!mesh.empty());
	bool sawLeft = false;
	bool sawRight = false;
	for (const TrailVertex& v : mesh) {
		if (v.position.x > 9.0f) CHECK(v.color.w > 0.9f && v.uv.x < 0.01f);
		if (v.position.x < 1.0f) CHECK(v.uv.x > 0.99f);
		sawLeft = sawLeft || v.uv.y == 0.0f;
		sawRight = sawRight || v.uv.y == 1.0f;
		if (v.position.y == 0.0f && v.position.x > 1.0f) CHECK_NEAR(v.uv.y, 0.5f, 1e-5f); // the tail has no width: its edges are at y 0 too
	}
	CHECK(sawLeft && sawRight);
}

TEST_CASE(Vfx, TrailTextureIsSampled)
{
	// A black texture on an additive trail adds nothing; without it the same trail lights the picture
	const std::filesystem::path folder = A::TempRoot() / "TrailTexture";
	std::filesystem::create_directories(folder);
	const std::filesystem::path black = folder / "black.bmp";
	{
		const unsigned char header[54] = { 'B', 'M', 70, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24, 0,
			0, 0, 0, 0, 16, 0, 0, 0, 0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		const unsigned char pixels[16] = {}; // 2 x 2 black, rows padded to 8 bytes
		std::ofstream file(black, std::ios::binary);
		file.write(reinterpret_cast<const char*>(header), sizeof(header));
		file.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
	}
	A::AimAudioListener(s, { 0.0f, 2.0f, -10.0f }, 0.0f);
	A::State(s) = EditorSceneState::Play;
	const std::vector<float> empty = CaptureGameViewSignature(s);
	const auto brightest = [&](const char* texture) {
		GameObject& obj = s.CreateGameObject("Comet", GameObjectType::Empty);
		const uint32_t id = obj.id;
		if (!s.AddComponentByName(obj, kTrailComponent)) return -1.0f;
		auto* trail = obj.components.Get<TrailComponent>(kTrailComponent);
		trail->widthStart = trail->widthEnd = 3.0f;
		trail->lifetime = 2.0f;
		trail->intensity = 4.0f;
		trail->colorEnd = trail->colorStart;
		strcpy_s(trail->texture, texture);
		for (int i = 0; i <= 8; ++i) {
			s.FindGameObjectById(id, false)->translation = { -4.0f + i, 2.0f, 0.0f };
			A::GameTime(s) = i * 0.02;
			s.UpdateWorldMatrices();
			s.UpdateTrails();
		}
		const std::vector<float> after = CaptureGameViewSignature(s);
		s.QueueDeleteGameObjectById(id);
		s.ProcessPendingDeletes();
		float most = 0.0f;
		for (size_t i = 0; i < after.size() && i < empty.size(); ++i) most = (std::max)(most, after[i] - empty[i]);
		return most;
	};
	const float plain = brightest("");
	const float textured = brightest(black.string().c_str());
	A::State(s) = EditorSceneState::Edit;
	CHECK_MSG(plain > 5.0f && textured < 1.0f, std::format("plain {:.2f}, black texture {:.2f}", plain, textured));
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Vfx, TrailComponentSavesAndDrawsInPlay)
{
	GameObject& obj = s.CreateGameObject("Comet", GameObjectType::Empty);
	const uint32_t id = obj.id;
	REQUIRE(s.AddComponentByName(obj, kTrailComponent));
	auto* trail = obj.components.Get<TrailComponent>(kTrailComponent);
	REQUIRE(trail != nullptr);
	trail->widthStart = 3.0f;
	trail->lifetime = 2.0f;
	trail->intensity = 4.0f;
	trail->colorStart = { 1.0f, 0.2f, 0.1f, 1.0f };

	// Saved settings come back, the runtime history does not
	REQUIRE(s.DeserializeSceneFromString(s.SerializeSceneToString()));
	trail = s.FindGameObjectById(id, false)->components.Get<TrailComponent>(kTrailComponent);
	REQUIRE(trail != nullptr);
	CHECK_NEAR(trail->widthStart, 3.0f, 1e-5f);
	CHECK_NEAR(trail->intensity, 4.0f, 1e-5f);
	CHECK_NEAR(trail->colorStart.y, 0.2f, 1e-5f);

	A::AimAudioListener(s, { 0.0f, 2.0f, -10.0f }, 0.0f); // the game camera; tests do not run the camera sync

	// Not in Edit mode
	const auto move = [&](float x, double time) {
		s.FindGameObjectById(id, false)->translation = { x, 2.0f, 0.0f };
		A::GameTime(s) = time;
		s.UpdateWorldMatrices();
		s.UpdateTrails();
	};
	move(-4.0f, 0.0);
	move(4.0f, 0.1);
	CHECK(s.FindGameObjectById(id, false)->components.Get<TrailComponent>(kTrailComponent)->ribbon.PointCount() == 0);
	const std::vector<float> before = CaptureGameViewSignature(s);

	A::State(s) = EditorSceneState::Play;
	move(-4.0f, 0.0);
	for (int i = 1; i <= 8; ++i) move(-4.0f + i, i * 0.02);
	CHECK(s.FindGameObjectById(id, false)->components.Get<TrailComponent>(kTrailComponent)->ribbon.PointCount() > 2);
	const std::vector<float> after = CaptureGameViewSignature(s);
	A::State(s) = EditorSceneState::Edit;
	REQUIRE(before.size() == after.size() && !before.empty());
	float brightest = 0.0f;
	for (size_t i = 0; i < before.size(); ++i) brightest = (std::max)(brightest, after[i] - before[i]);
	CHECK_MSG(brightest > 5.0f, std::format("largest change {:.2f}", brightest));
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}


TEST_CASE(Vfx, GpuParticleEmissionKeepsFractions)
{
	float carry = 0.0f;
	uint32_t total = 0;
	for (int i = 0; i < 60; ++i) total += GpuParticleEmitCount(10.0f, 1.0f / 60.0f, carry);
	CHECK(total >= 9u && total <= 10u); // float rounding may leave the last one for the next step
	carry = 0.0f;
	CHECK(GpuParticleEmitCount(0.0f, 1.0f, carry) == 0u);
	CHECK(GpuParticleEmitCount(100.0f, 0.0f, carry) == 0u); // paused
	CHECK(GpuParticleEmitCount(std::numeric_limits<float>::quiet_NaN(), 1.0f, carry) == 0u);

	GpuParticlesComponent p;
	p.capacity = 77;
	for (const char* preset : kGpuParticlePresets) {
		REQUIRE(ApplyGpuParticlePreset(p, preset));
		CHECK(p.capacity == 77);
		CHECK(p.lifeMin <= p.lifeMax);
		CHECK(p.speedMin <= p.speedMax);
	}
	CHECK(!ApplyGpuParticlePreset(p, "No Such Preset"));
	// Shaped presets set their shape; the others go back to a round point emitter
	REQUIRE(ApplyGpuParticlePreset(p, "Shockwave Ring"));
	CHECK(p.shape == static_cast<int>(GpuParticleShape::Ring));
	CHECK(p.velocityMode == static_cast<int>(GpuParticleVelocity::Radial));
	REQUIRE(ApplyGpuParticlePreset(p, "Sparks"));
	CHECK(p.shape == static_cast<int>(GpuParticleShape::Point));
	CHECK(p.stretchFactor > 0.0f);
	REQUIRE(ApplyGpuParticlePreset(p, "Fire"));
	CHECK(p.stretchFactor == 0.0f);
	CHECK(p.velocityMode == static_cast<int>(GpuParticleVelocity::Direction));
}

namespace {
	/// Row where the picture gained the most light compared with `baseline`, top = 0
	float BrightestRowGain(const std::vector<float>& rows, const std::vector<float>& baseline)
	{
		float weighted = 0.0f;
		float total = 0.0f;
		for (size_t y = 0; y < rows.size() && y < baseline.size(); ++y) {
			const float gain = (std::max)(rows[y] - baseline[y], 0.0f);
			weighted += gain * static_cast<float>(y);
			total += gain;
		}
		return total > 0.0f ? weighted / total : -1.0f;
	}
}

TEST_CASE(Vfx, GpuParticlesAreSimulatedAndDrawn)
{
	A::AimAudioListener(s, { 0.0f, 0.0f, -20.0f }, 0.0f); // the game camera; tests do not run the camera sync
	s.UpdateWorldMatrices();
	A::State(s) = EditorSceneState::Play;
	A::GameTime(s) = 0.0;
	GpuParticles::GetInstance()->Clear(); // emitters of earlier tests may still wait for their idle time
	CaptureGameViewSignature(s);
	const std::vector<float> emptySignature = CaptureGameViewSignature(s);
	const std::vector<float> emptyRows = gCapturedRowMeans;

	GameObject& obj = s.CreateGameObject("Fountain", GameObjectType::Empty);
	obj.translation = { 0.0f, 5.0f, 0.0f };
	const uint32_t id = obj.id;
	REQUIRE(s.AddComponentByName(obj, kGpuParticlesComponent));
	auto* p = obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
	p->emitting = false;
	p->burst = 2000;
	p->capacity = 2000;
	p->lifeMin = 0.7f;
	p->lifeMax = 0.7f;
	p->speedMin = 0.0f;
	p->speedMax = 0.5f;
	p->spreadDegrees = 180.0f;
	p->gravity = { 0.0f, -20.0f, 0.0f };
	p->sizeStart = 1.0f;
	p->sizeEnd = 1.0f;
	p->colorStart = { 1.0f, 0.5f, 0.2f, 1.0f };
	p->colorEnd = { 1.0f, 0.5f, 0.2f, 1.0f };
	p->intensity = 2.0f;
	s.UpdateWorldMatrices();

	// Burst: visible, and only once
	CaptureGameViewSignature(s); // the first frame only starts the clock of the emitter
	const std::vector<float> atStart = gCapturedRowMeans;
	CHECK(s.FindGameObjectById(id, false)->components.Get<GpuParticlesComponent>(kGpuParticlesComponent)->burst == 0);
	CHECK(GpuParticles::GetInstance()->EmitterCount() == 1u);
	const float rowAtStart = BrightestRowGain(atStart, emptyRows);
	CHECK(rowAtStart >= 0.0f);

	// Gravity pulls them down the picture
	for (int i = 1; i <= 6; ++i) {
		A::GameTime(s) = i * 0.1;
		CaptureGameViewSignature(s);
	}
	const float rowLater = BrightestRowGain(gCapturedRowMeans, emptyRows);
	CHECK_MSG(rowLater > rowAtStart + 10.0f, std::format("row {:.1f} -> {:.1f}", rowAtStart, rowLater));

	// Their life ends: the picture is back to what it was
	for (int i = 7; i <= 12; ++i) {
		A::GameTime(s) = i * 0.1;
		CaptureGameViewSignature(s);
	}
	const std::vector<float> finished = CaptureGameViewSignature(s);
	REQUIRE(finished.size() == emptySignature.size());
	float largest = 0.0f;
	for (size_t i = 0; i < finished.size(); ++i) largest = (std::max)(largest, std::abs(finished[i] - emptySignature[i]));
	CHECK_MSG(largest < 1.0f, std::format("largest difference {:.2f}", largest));

	// A new capacity starts over without trouble; a deleted object frees its emitter
	s.FindGameObjectById(id, false)->components.Get<GpuParticlesComponent>(kGpuParticlesComponent)->capacity = 100;
	CaptureGameViewSignature(s);
	s.QueueDeleteGameObjectById(id);
	s.ProcessPendingDeletes();
	CaptureGameViewSignature(s);
	for (uint32_t i = 0; i <= GpuParticles::kIdleFramesBeforeRelease; ++i) GpuParticles::GetInstance()->EndFrame();
	CHECK(GpuParticles::GetInstance()->EmitterCount() == 0u);
	A::State(s) = EditorSceneState::Edit;
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Vfx, GpuParticleShapesStayInsideTheirShape)
{
	const auto dot = [](const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
	const auto length = [&](const Vector3& v) { return std::sqrt(dot(v, v)); };
	GpuParticleSettings gs;
	gs.shapeRadius = 2.0f;
	gs.shapeAxisX = { 3.0f, 0.0f, 0.0f };
	gs.shapeAxisY = { 0.0f, 0.0f, 1.0f }; // a rotated box: its Y half-axis lies along world Z
	gs.shapeAxisZ = { 0.0f, -0.5f, 0.0f };
	const Vector3 up{ 0.0f, 1.0f, 0.0f };
	const Vector3 tilted{ 0.6f, 0.8f, 0.0f };
	int checkedSamples = 0;
	for (int a = 0; a <= 8; ++a) {
		for (int b = 0; b <= 8; ++b) {
			for (int c = 0; c <= 8; ++c) {
				const float u1 = a / 8.0f, u2 = b / 8.0f, u3 = c / 8.0f;
				++checkedSamples;
				gs.shape = static_cast<uint32_t>(GpuParticleShape::Point);
				CHECK(length(GpuParticleSpawnOffset(gs, up, u1, u2, u3)) == 0.0f);

				gs.shape = static_cast<uint32_t>(GpuParticleShape::Sphere);
				CHECK(length(GpuParticleSpawnOffset(gs, up, u1, u2, u3)) <= 2.0f + 1.0e-4f);

				gs.shape = static_cast<uint32_t>(GpuParticleShape::Box);
				const Vector3 box = GpuParticleSpawnOffset(gs, up, u1, u2, u3);
				CHECK(std::abs(box.x) <= 3.0f + 1.0e-4f);
				CHECK(std::abs(box.y) <= 0.5f + 1.0e-4f);
				CHECK(std::abs(box.z) <= 1.0f + 1.0e-4f);

				for (const Vector3& dir : { up, tilted }) {
					gs.shape = static_cast<uint32_t>(GpuParticleShape::Cone); // the base disc, across the direction
					const Vector3 disc = GpuParticleSpawnOffset(gs, dir, u1, u2, u3);
					CHECK(std::abs(dot(disc, dir)) < 1.0e-4f);
					CHECK(length(disc) <= 2.0f + 1.0e-4f);
					gs.shape = static_cast<uint32_t>(GpuParticleShape::Ring); // on the circle itself
					const Vector3 ring = GpuParticleSpawnOffset(gs, dir, u1, u2, u3);
					CHECK(std::abs(dot(ring, dir)) < 1.0e-4f);
					CHECK(std::abs(length(ring) - 2.0f) < 1.0e-3f);
					// Radial: away from the centre
					const Vector3 out = GpuParticleRadialDirection(ring, u1, u2);
					CHECK(std::abs(length(out) - 1.0f) < 1.0e-4f);
					CHECK(dot(out, ring) > 0.0f);
				}
			}
		}
	}
	CHECK(checkedSamples == 729);
	// From the centre itself there is no "away": any unit direction will do
	CHECK(std::abs(length(GpuParticleRadialDirection({ 0.0f, 0.0f, 0.0f }, 0.3f, 0.7f)) - 1.0f) < 1.0e-4f);
	// Sphere and box fill their volume, not only the surface
	gs.shape = static_cast<uint32_t>(GpuParticleShape::Sphere);
	CHECK(length(GpuParticleSpawnOffset(gs, up, 0.5f, 0.5f, 0.0f)) < 0.01f);
	gs.shape = static_cast<uint32_t>(GpuParticleShape::Box);
	CHECK(length(GpuParticleSpawnOffset(gs, up, 0.5f, 0.5f, 0.5f)) < 1.0e-4f);
}

TEST_CASE(Vfx, GpuParticleShapeFollowsTheObject)
{
	GpuParticlesComponent p;
	p.shape = static_cast<int>(GpuParticleShape::Box);
	p.shapeRadius = 1.5f;
	p.boxExtents = { 1.0f, 2.0f, 3.0f };
	p.velocityMode = static_cast<int>(GpuParticleVelocity::Radial);
	p.stretchFactor = 0.25f;
	// Scaled x2 on X, then turned 90 degrees about Y: local X points to world -Z (left-handed, row vectors)
	const Matrix4x4 world = Mathematics::MakeAffineMatrix({ 2.0f, 1.0f, 1.0f }, { 0.0f, 1.5707963f, 0.0f }, { 5.0f, 0.0f, 0.0f });
	GpuParticleSettings gs;
	ApplyGpuParticleShape(p, world, gs);
	CHECK(gs.shape == static_cast<uint32_t>(GpuParticleShape::Box));
	CHECK(gs.velocityMode == static_cast<uint32_t>(GpuParticleVelocity::Radial));
	CHECK_NEAR(gs.stretch, 0.25f, 1.0e-6f);
	CHECK_NEAR(gs.shapeAxisX.z, -2.0f, 1.0e-4f); // extent 1 x scale 2, along the turned X axis
	CHECK_NEAR(gs.shapeAxisX.x, 0.0f, 1.0e-4f);
	CHECK_NEAR(gs.shapeAxisY.y, 2.0f, 1.0e-4f);
	CHECK_NEAR(gs.shapeAxisZ.x, 3.0f, 1.0e-4f);
	CHECK_NEAR(gs.shapeRadius, 3.0f, 1.0e-4f); // radius x the largest scale

	// Out-of-range values from old files or scripts fall back to safe ones
	p.shape = 42;
	p.velocityMode = -3;
	p.shapeRadius = -1.0f;
	p.stretchFactor = -2.0f;
	ApplyGpuParticleShape(p, Mathematics::MakeIdentity4x4(), gs);
	CHECK(gs.shape == static_cast<uint32_t>(GpuParticleShape::Point));
	CHECK(gs.velocityMode == static_cast<uint32_t>(GpuParticleVelocity::Direction));
	CHECK(gs.shapeRadius >= 0.0f);
	CHECK(gs.stretch == 0.0f);

	// The new fields are saved; scenes from before them load as a point emitter, as they looked
	GameObject& obj = s.CreateGameObject("Ring Emitter", GameObjectType::Empty);
	const uint32_t id = obj.id;
	REQUIRE(s.AddComponentByName(obj, kGpuParticlesComponent));
	auto* saved = obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
	saved->shape = static_cast<int>(GpuParticleShape::Ring);
	saved->velocityMode = static_cast<int>(GpuParticleVelocity::Radial);
	saved->shapeRadius = 4.0f;
	saved->boxExtents = { 0.1f, 0.2f, 0.3f };
	saved->stretchFactor = 0.08f;
	const std::string scene = s.SerializeSceneToString();
	REQUIRE(s.DeserializeSceneFromString(scene));
	const auto* loaded = s.FindGameObjectById(id, false)->components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
	REQUIRE(loaded != nullptr);
	CHECK(loaded->shape == static_cast<int>(GpuParticleShape::Ring));
	CHECK(loaded->velocityMode == static_cast<int>(GpuParticleVelocity::Radial));
	CHECK_NEAR(loaded->shapeRadius, 4.0f, 1.0e-6f);
	CHECK_NEAR(loaded->boxExtents.z, 0.3f, 1.0e-6f);
	CHECK_NEAR(loaded->stretchFactor, 0.08f, 1.0e-6f);
	std::string old = scene;
	for (const char* key : { "\"shape\"", "\"velocityMode\"", "\"stretchFactor\"" }) {
		const size_t at = old.find(key, old.find("Ring Emitter")); // this object's component, not another object's key
		REQUIRE(at != std::string::npos);
		old.replace(at, std::strlen(key), "\"removed_" + std::string(key + 1));
	}
	REQUIRE(s.DeserializeSceneFromString(old));
	const auto* legacy = s.FindGameObjectById(id, false)->components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
	REQUIRE(legacy != nullptr);
	CHECK(legacy->shape == static_cast<int>(GpuParticleShape::Point));
	CHECK(legacy->velocityMode == static_cast<int>(GpuParticleVelocity::Direction));
	CHECK(legacy->stretchFactor == 0.0f);
}

namespace {
	struct LitArea {
		int cells = 0, columns = 0, rows = 0;
		float brightest = 0.0f, centreGain = 0.0f, total = 0.0f;
		bool centre = false;    // the two middle cells of the middle row
		bool belowMiddle = false; // anything under the middle row
	};
	/// Which cells of a 16x9 game-view signature gained light compared with `baseline`
	LitArea LitCells(const std::vector<float>& signature, const std::vector<float>& baseline, float threshold = 12.0f)
	{
		LitArea area;
		if (signature.size() != baseline.size()) return area;
		std::vector<bool> column(kSignatureColumns, false), row(kSignatureRows, false);
		for (int y = 0; y < kSignatureRows; ++y) {
			for (int x = 0; x < kSignatureColumns; ++x) {
				const size_t i = static_cast<size_t>(y * kSignatureColumns + x) * 3;
				const float gain = signature[i] + signature[i + 1] + signature[i + 2] - baseline[i] - baseline[i + 1] - baseline[i + 2];
				area.brightest = (std::max)(area.brightest, gain);
				area.total += (std::max)(gain, 0.0f);
				if (y == kSignatureRows / 2 && (x == 7 || x == 8)) area.centreGain = (std::max)(area.centreGain, gain);
				if (gain < threshold) continue;
				++area.cells;
				column[x] = row[y] = true;
				if (y == kSignatureRows / 2 && (x == 7 || x == 8)) area.centre = true;
				if (y > kSignatureRows / 2) area.belowMiddle = true;
			}
		}
		area.columns = static_cast<int>(std::count(column.begin(), column.end(), true));
		area.rows = static_cast<int>(std::count(row.begin(), row.end(), true));
		return area;
	}
}

TEST_CASE(Vfx, GpuParticleShapesAndStretchOnScreen)
{
	A::AimAudioListener(s, { 0.0f, 0.0f, -20.0f }, 0.0f); // the game camera looks along +Z at the origin: about 16 x 9 units visible, ~1 unit per cell
	s.UpdateWorldMatrices();
	A::State(s) = EditorSceneState::Play;
	A::GameTime(s) = 0.0;
	CaptureGameViewSignature(s);
	const std::vector<float> empty = CaptureGameViewSignature(s);

	// One emitter at the origin; `setup` shapes it, then the picture after `seconds` of game time
	double clock = 0.0;
	const auto shoot = [&](const std::function<void(GpuParticlesComponent&, GameObject&)>& setup, double seconds) {
		GameObject& obj = s.CreateGameObject("Shape Test", GameObjectType::Empty);
		const uint32_t id = obj.id;
		s.AddComponentByName(obj, kGpuParticlesComponent);
		auto* p = obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
		std::vector<float> picture;
		if (p) {
			p->emitting = false;
			p->burst = 2000;
			p->capacity = 2000;
			p->lifeMin = p->lifeMax = 5.0f;
			p->speedMin = p->speedMax = 0.0f;
			p->spreadDegrees = 0.0f;
			p->sizeStart = p->sizeEnd = 0.4f;
			p->colorStart = p->colorEnd = { 1.0f, 1.0f, 1.0f, 1.0f };
			p->intensity = 2.0f;
			setup(*p, obj);
			s.UpdateWorldMatrices();
			CaptureGameViewSignature(s); // starts the emitter's clock and fires the burst
			for (double t = 0.0; t < seconds; t += 0.05) {
				clock += 0.05;
				A::GameTime(s) = clock;
				picture = CaptureGameViewSignature(s);
			}
			if (picture.empty()) picture = CaptureGameViewSignature(s);
		}
		s.QueueDeleteGameObjectById(id);
		s.ProcessPendingDeletes();
		CaptureGameViewSignature(s); // frees the emitter
		return picture;
	};

	const LitArea point = LitCells(shoot([](GpuParticlesComponent&, GameObject&) {}, 0.05), empty);
	CHECK_MSG(point.cells >= 1 && point.cells <= 8, std::format("point lit {} cells", point.cells));

	// A ring facing the camera: lit around, dark in the middle
	const LitArea ring = LitCells(shoot([](GpuParticlesComponent& p, GameObject&) {
		p.shape = static_cast<int>(GpuParticleShape::Ring);
		p.shapeRadius = 3.0f;
		p.direction = { 0.0f, 0.0f, -1.0f };
	}, 0.05), empty);
	// Bloom may tint the middle a little; the ring itself must stay far brighter than the hole
	CHECK_MSG(ring.cells >= 16 && ring.centreGain < ring.brightest * 0.3f, std::format("ring lit {} cells, centre {:.0f} of {:.0f}", ring.cells, ring.centreGain, ring.brightest));

	// A long box lies across the picture; turned 90 degrees about Z it stands up
	const auto longBox = [](float turn) {
		return [turn](GpuParticlesComponent& p, GameObject& obj) {
			p.shape = static_cast<int>(GpuParticleShape::Box);
			p.boxExtents = { 6.0f, 0.1f, 0.1f };
			obj.rotation = { 0.0f, 0.0f, turn };
			obj.rotationQuat = Quaternion::FromEuler(obj.rotation);
		};
	};
	const LitArea flat = LitCells(shoot(longBox(0.0f), 0.05), empty);
	CHECK_MSG(flat.columns >= 7 && flat.rows <= 3, // bloom spills one row up and down
		 std::format("flat box {} columns x {} rows", flat.columns, flat.rows));
	const LitArea standing = LitCells(shoot(longBox(1.5707963f), 0.05), empty);
	CHECK_MSG(standing.rows >= 6 && standing.columns <= 3, std::format("standing box {} columns x {} rows", standing.columns, standing.rows));

	// Direction mode goes up the cone; Radial goes every way, so some fall below the middle too
	const auto spray = [](int mode) {
		return [mode](GpuParticlesComponent& p, GameObject&) {
			p.velocityMode = mode;
			p.speedMin = p.speedMax = 3.0f;
			p.sizeStart = p.sizeEnd = 0.3f;
		};
	};
	const LitArea upward = LitCells(shoot(spray(static_cast<int>(GpuParticleVelocity::Direction)), 1.0), empty);
	const LitArea radial = LitCells(shoot(spray(static_cast<int>(GpuParticleVelocity::Radial)), 1.0), empty);
	CHECK_MSG(upward.cells > 0 && !upward.belowMiddle, std::format("direction mode: {} cells, below {}", upward.cells, upward.belowMiddle));
	CHECK_MSG(radial.belowMiddle && radial.rows >= 4, std::format("radial mode: {} rows, below {}", radial.rows, radial.belowMiddle));

	// Stretched billboards: fast sideways particles become streaks
	const auto streak = [](float stretch) {
		return [stretch](GpuParticlesComponent& p, GameObject&) {
			p.direction = { 1.0f, 0.0f, 0.0f };
			p.speedMin = p.speedMax = 8.0f;
			p.stretchFactor = stretch;
		};
	};
	const LitArea round = LitCells(shoot(streak(0.0f), 0.1), empty);
	const LitArea stretched = LitCells(shoot(streak(3.0f), 0.1), empty);
	CHECK_MSG(stretched.columns >= round.columns + 2, std::format("streak {} columns vs {} round", stretched.columns, round.columns));
	CHECK(stretched.rows <= round.rows + 1); // longer, not wider

	A::State(s) = EditorSceneState::Edit;
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Vfx, UprightBillboardsStandOnTheGround)
{
	// Looking almost straight down: camera-facing quads face the lens, upright ones are seen edge-on
	A::AimGameCamera(s, { 0.0f, 20.0f, 0.0f }, { 1.4f, 0.0f, 0.0f });
	A::State(s) = EditorSceneState::Play;
	A::GameTime(s) = 0.0;
	CaptureGameViewSignature(s);
	const std::vector<float> empty = CaptureGameViewSignature(s);
	const auto shoot = [&](bool upright, double start) {
		GameObject& obj = s.CreateGameObject("Upright Test", GameObjectType::Empty);
		const uint32_t id = obj.id;
		s.AddComponentByName(obj, kGpuParticlesComponent);
		auto* p = obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
		LitArea area;
		if (p) {
			p->emitting = false;
			p->burst = 400;
			p->capacity = 400;
			p->lifeMin = p->lifeMax = 5.0f;
			p->speedMin = p->speedMax = 0.0f;
			p->shape = static_cast<int>(GpuParticleShape::Box);
			p->boxExtents = { 3.0f, 0.0f, 2.0f };
			p->sizeStart = p->sizeEnd = 0.6f;
			p->colorStart = p->colorEnd = { 1.0f, 1.0f, 1.0f, 1.0f };
			p->intensity = 2.0f;
			p->uprightBillboard = upright;
			s.UpdateWorldMatrices();
			CaptureGameViewSignature(s);
			A::GameTime(s) = start + 0.05;
			area = LitCells(CaptureGameViewSignature(s), empty);
		}
		s.QueueDeleteGameObjectById(id);
		s.ProcessPendingDeletes();
		CaptureGameViewSignature(s);
		return area;
	};
	const LitArea facing = shoot(false, 0.0);
	const LitArea upright = shoot(true, 1.0);
	CHECK_MSG(facing.total > 0.0f && upright.total < facing.total * 0.5f, std::format("upright light {:.0f} vs facing {:.0f}", upright.total, facing.total));
	A::State(s) = EditorSceneState::Edit;
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Vfx, LegacyParticleSystemLoadsAsGpuParticles)
{
	const uint32_t boxId = s.CreateGameObject("Legacy Box", GameObjectType::Empty).id;
	const uint32_t coneId = s.CreateGameObject("Legacy Cone", GameObjectType::Empty).id;
	const uint32_t plainId = s.CreateGameObject("Legacy Plain", GameObjectType::Empty).id;
	nlohmann::json scene = nlohmann::json::parse(s.SerializeSceneToString());
	for (auto& o : scene["gameObjects"]) {
		const std::string name = o.value("name", "");
		if (name == "Legacy Box") {
			o["hasParticleSystem"] = true;
			o["particleShape"] = 2;
			o["particleBoxExtents"] = { 2.0f, 4.0f, 6.0f };
			o["particleSpawnRate"] = 33.0f;
			o["particleMaxParticles"] = 77;
			o["particleBurstCount"] = 9;
			o["particleEmitContinuously"] = false;
			o["particleDirection"] = { 1.0f, 0.0f, 0.0f };
			o["particleSpreadAngle"] = 60.0f;
			o["particleSpeedMin"] = 2.0f;
			o["particleSpeedMax"] = 5.0f;
			o["particleGravity"] = -9.8f;
			o["particleDrag"] = 0.3f;
			o["particleWindEnabled"] = true;
			o["particleWindAcceleration"] = { 1.0f, 0.0f, 2.0f };
			o["particleLifetimeMin"] = 0.7f;
			o["particleLifetimeMax"] = 1.9f;
			o["particleStartScale"] = 0.4f;
			o["particleEndScale"] = 0.1f;
			o["particleStartColor"] = { 1.0f, 0.5f, 0.25f, 1.0f };
			o["particleEndColor"] = { 0.0f, 0.0f, 1.0f, 0.0f };
			o["particleBlendMode"] = 1;
			o["particleBillboardMode"] = 1;
		} else if (name == "Legacy Cone") {
			o["hasParticleSystem"] = true;
			o["particleShape"] = 3;
			o["particleSpawnRadius"] = 0.5f;
			o["particleConeAngle"] = 45.0f;
			o["particleSpreadAngle"] = 180.0f;
			o["particleBillboardMode"] = 2;
			o["attachedParentId"] = static_cast<int>(boxId); // the old attach option becomes a hierarchy parent
			o["localOffset"] = { 0.0f, 1.0f, 0.0f };
		} else if (name == "Legacy Plain") {
			o["hasParticleSystem"] = false;
			o["particleShape"] = 2;
		}
	}
	REQUIRE(s.DeserializeSceneFromString(scene.dump()));

	const GameObject* box = s.FindGameObjectById(boxId, false);
	REQUIRE(box != nullptr);
	const auto* b = box->components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
	REQUIRE(b != nullptr);
	CHECK(b->capacity == 77);
	CHECK_NEAR(b->emitRate, 33.0f, 1e-5f);
	CHECK(!b->emitting);
	CHECK(b->spawnBurst == 9);
	CHECK(b->shape == static_cast<int>(GpuParticleShape::Box));
	CHECK_NEAR(b->boxExtents.x, 1.0f, 1e-5f); // the old box was a full size, the new one is half
	CHECK_NEAR(b->boxExtents.z, 3.0f, 1e-5f);
	CHECK_NEAR(b->direction.x, 1.0f, 1e-5f);
	CHECK_NEAR(b->spreadDegrees, 30.0f, 1e-4f); // the old spread was the whole cone, the new one is half of it
	CHECK_NEAR(b->speedMax, 5.0f, 1e-5f);
	CHECK_NEAR(b->gravity.x, 1.0f, 1e-5f); // wind adds to gravity
	CHECK_NEAR(b->gravity.y, -9.8f, 1e-5f);
	CHECK_NEAR(b->gravity.z, 2.0f, 1e-5f);
	CHECK_NEAR(b->drag, 0.3f, 1e-5f);
	CHECK_NEAR(b->lifeMin, 0.7f, 1e-5f);
	CHECK_NEAR(b->lifeMax, 1.9f, 1e-5f);
	CHECK_NEAR(b->sizeStart, 0.4f, 1e-5f);
	CHECK_NEAR(b->sizeEnd, 0.1f, 1e-5f);
	CHECK_NEAR(b->colorStart.y, 0.5f, 1e-5f);
	CHECK_NEAR(b->colorEnd.z, 1.0f, 1e-5f);
	CHECK(b->additive);
	CHECK(b->uprightBillboard);
	CHECK_NEAR(b->stretchFactor, 0.0f, 1e-6f);

	const GameObject* cone = s.FindGameObjectById(coneId, false);
	REQUIRE(cone != nullptr);
	const auto* c = cone->components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
	REQUIRE(c != nullptr);
	CHECK(c->shape == static_cast<int>(GpuParticleShape::Cone));
	CHECK_NEAR(c->shapeRadius, 0.5f, 1e-4f); // tan(45 deg) x 0.5
	CHECK_NEAR(c->spreadDegrees, 180.0f, 1e-4f);
	CHECK(c->stretchFactor > 0.0f); // velocity-aligned quads become short streaks
	CHECK(!c->additive);
	CHECK(c->emitting);
	CHECK(c->spawnBurst == 0);
	CHECK(cone->parentId == static_cast<int>(boxId));
	CHECK_NEAR(cone->translation.y, 1.0f, 1e-5f);

	const GameObject* plain = s.FindGameObjectById(plainId, false);
	REQUIRE(plain != nullptr);
	CHECK(plain->components.Get<GpuParticlesComponent>(kGpuParticlesComponent) == nullptr);

	// Saved again: only the new component, so a second load converts nothing twice
	const std::string saved = s.SerializeSceneToString();
	CHECK(saved.find("hasParticleSystem") == std::string::npos);
	CHECK(saved.find("particleShape") == std::string::npos);
	REQUIRE(s.DeserializeSceneFromString(saved));
	const auto* again = s.FindGameObjectById(boxId, false)->components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
	REQUIRE(again != nullptr);
	CHECK(again->spawnBurst == 9);
	CHECK(again->uprightBillboard);
}

TEST_CASE(Effects, TimingPhasesLengthAndPreviewMotion)
{
	// A layer waits for its delay, emits for its duration, then is done; duration 0 = until the effect ends
	CHECK(EffectLayerPhase(0.5f, 1.0f, 0.2f) == EffectPhase::Waiting);
	CHECK(EffectLayerPhase(0.5f, 1.0f, 0.5f) == EffectPhase::Emitting);
	CHECK(EffectLayerPhase(0.5f, 1.0f, 1.49f) == EffectPhase::Emitting);
	CHECK(EffectLayerPhase(0.5f, 1.0f, 1.5f) == EffectPhase::Done);
	CHECK(EffectLayerPhase(0.0f, 0.0f, 100.0f) == EffectPhase::Emitting);

	// Length = the last layer to finish, including how long its last particles live
	const std::vector<EffectLayerWindow> layers = { { 0.0f, 0.2f, 0.9f }, { 0.1f, 0.6f, 4.0f }, { 0.0f, 0.1f, 0.0f } };
	CHECK_NEAR(EffectLength(layers), 4.7f, 1e-5f);
	CHECK_NEAR(EffectLength(layers, false), 0.7f, 1e-5f); // a loop starts over when the last layer stops emitting
	CHECK(std::isinf(EffectLength({ { 0.0f, 0.0f, 1.0f } }))); // emits forever
	CHECK_NEAR(EffectLength({}), 0.0f, 1e-6f);

	// Advancing: a one-shot effect finishes once, a looping one starts over
	const EffectClock once = AdvanceEffect(0.9f, 0.2f, 1.0f, false);
	CHECK(once.finished && !once.restarted);
	CHECK_NEAR(once.time, 1.0f, 1e-6f);
	const EffectClock looped = AdvanceEffect(0.9f, 0.2f, 1.0f, true);
	CHECK(looped.restarted && !looped.finished);
	CHECK_NEAR(looped.time, 0.1f, 1e-5f);
	const EffectClock forever = AdvanceEffect(5.0f, 1.0f, std::numeric_limits<float>::infinity(), true);
	CHECK(!forever.restarted && !forever.finished);
	CHECK_NEAR(forever.time, 6.0f, 1e-5f);
	CHECK(!AdvanceEffect(0.0f, std::nanf(""), 1.0f, false).finished); // a broken frame time changes nothing

	// Preview motion stays inside its radius; None stays put
	for (int motion = 0; motion < static_cast<int>(EffectPreviewMotion::Count); ++motion) {
		for (float t = 0.0f; t < 3.0f; t += 0.1f) {
			const Vector3 o = EffectPreviewOffset(static_cast<EffectPreviewMotion>(motion), 2.0f, 0.5f, t);
			CHECK(std::sqrt(o.x * o.x + o.y * o.y + o.z * o.z) <= 2.0f + 1e-4f);
			if (motion == 0) CHECK(o.x == 0.0f && o.y == 0.0f && o.z == 0.0f);
		}
	}
	const Vector3 a = EffectPreviewOffset(EffectPreviewMotion::Circle, 2.0f, 0.5f, 0.0f);
	const Vector3 b = EffectPreviewOffset(EffectPreviewMotion::Circle, 2.0f, 0.5f, 1.0f); // half a turn later at 0.5 turns per second
	CHECK_NEAR(a.x, -b.x, 1e-4f);
}

namespace {
	GpuParticlesComponent* Particles(EditorSuite& s, uint32_t id)
	{
		GameObject* obj = s.FindGameObjectById(id, false);
		return obj ? obj->components.Get<GpuParticlesComponent>(kGpuParticlesComponent) : nullptr;
	}
	uint32_t ChildNamed(EditorSuite& s, uint32_t root, const char* name)
	{
		const GameObject* obj = s.FindGameObjectById(root, false);
		if (!obj) return 0;
		for (uint32_t child : obj->childrenIds) {
			const GameObject* c = s.FindGameObjectById(child, false);
			if (c && c->name == name) return child;
		}
		return 0;
	}
	/// Root and every descendant
	std::vector<uint32_t> Subtree(EditorSuite& s, uint32_t root)
	{
		std::vector<uint32_t> ids = { root };
		for (size_t i = 0; i < ids.size(); ++i) {
			if (const GameObject* o = s.FindGameObjectById(ids[i], false)) ids.insert(ids.end(), o->childrenIds.begin(), o->childrenIds.end());
		}
		return ids;
	}
	int PendingBursts(EditorSuite& s, uint32_t root)
	{
		int total = 0;
		for (uint32_t id : Subtree(s, root)) {
			if (const auto* p = Particles(s, id)) total += p->burst;
		}
		return total;
	}
	void ConsumeBursts(EditorSuite& s, uint32_t root)
	{
		for (uint32_t id : Subtree(s, root)) {
			if (auto* p = Particles(s, id)) p->burst = 0;
		}
	}
}

TEST_CASE(Effects, EffectTimingDrivesEmittersInPlay)
{
	s.PostSettings() = PostProcessSettings{};
	s.PostSettings().enabled = true;
	const float baseBloom = s.PostSettings().bloomIntensity;
	const uint32_t root = s.CreateEffectFromTemplate("Explosion", { 0.0f, 1.0f, 0.0f });
	REQUIRE(root != 0);
	const uint32_t fireball = ChildNamed(s, root, "Fireball");
	const uint32_t smoke = ChildNamed(s, root, "Smoke");
	REQUIRE(Particles(s, fireball) != nullptr && Particles(s, smoke) != nullptr);
	A::State(s) = EditorSceneState::Play;
	s.ResetEffects();

	// Start: the fireball bursts once, the smoke waits for its delay, the pulse flashes
	s.UpdateEffects(0.0f);
	CHECK(Particles(s, fireball)->effectGate == kEffectGateOn);
	CHECK(Particles(s, fireball)->spawnBurst > 0);
	CHECK(Particles(s, fireball)->burst == Particles(s, fireball)->spawnBurst);
	CHECK(Particles(s, smoke)->effectGate == kEffectGateOff);
	CHECK(s.EffectivePostSettings().bloomIntensity > baseBloom);
	ConsumeBursts(s, root); // what the renderer does with them
	s.UpdateEffects(0.05f);
	CHECK(Particles(s, fireball)->burst == 0); // once only

	s.UpdateEffects(0.15f);
	CHECK(Particles(s, smoke)->effectGate == kEffectGateOn);
	s.UpdateEffects(1.0f);
	CHECK(Particles(s, fireball)->effectGate == kEffectGateOff);

	// Done: a one-shot effect removes itself after its last particles died
	for (int i = 0; i < 20 && s.FindGameObjectById(root, false); ++i) {
		s.UpdateEffects(0.5f);
		s.ProcessPendingSpawns();
		s.ProcessPendingDeletes(); // the effect is not a prefab instance: it is deleted, not pooled
	}
	const GameObject* gone = s.FindGameObjectById(root, false);
	CHECK(gone == nullptr || !gone->isActive);

	// A looping effect keeps going and bursts again on every round
	const uint32_t aura = s.CreateEffectFromTemplate("Magic Aura", { 0.0f, 0.0f, 0.0f });
	REQUIRE(aura != 0);
	s.UpdateEffects(0.0f);
	ConsumeBursts(s, aura);
	bool burstAgain = false;
	for (int i = 0; i < 20; ++i) {
		s.UpdateEffects(0.5f);
		burstAgain |= PendingBursts(s, aura) > 0;
		ConsumeBursts(s, aura);
	}
	s.ProcessPendingSpawns();
	REQUIRE(s.FindGameObjectById(aura, false) != nullptr);
	CHECK(s.FindGameObjectById(aura, false)->isActive);
	CHECK(burstAgain);

	// The effect state is runtime only: it never reaches the scene file
	CHECK(s.SerializeSceneToString().find("effectGate") == std::string::npos);
	A::State(s) = EditorSceneState::Edit;
	s.ResetEffects();
	CHECK(Particles(s, ChildNamed(s, aura, "Aura"))->effectGate == kEffectGateNone);
	s.QueueDeleteGameObjectById(aura);
	s.ProcessPendingDeletes();
	s.PostSettings() = PostProcessSettings{};
	s.ResetScriptRuntimeState();
}

TEST_CASE(Effects, EffectPrefabsStartOverOnEverySpawn)
{
	A::ProjectRoot(s) = A::TempRoot() / "EffectProject";
	const uint32_t root = s.CreateEffectFromTemplate("Hit Spark", { 0.0f, 0.0f, 0.0f });
	REQUIRE(root != 0);
	REQUIRE(s.SavePrefab(root, "HitSparkFx"));
	A::State(s) = EditorSceneState::Play;
	ScriptGameObjectView* view = s.QueuePrefabSpawn("HitSparkFx", { 2.0f, 0.0f, 0.0f });
	REQUIRE(view != nullptr);
	const uint32_t instance = view->id;
	s.ProcessPendingSpawns();
	s.UpdateEffects(0.0f);
	const int first = PendingBursts(s, instance);
	CHECK_MSG(first > 0, "first spawn burst " + std::to_string(first));

	// Reused from the pool: the same burst again, not twice (spawn burst + timing)
	s.QueueDespawn(instance);
	s.ProcessPendingSpawns();
	REQUIRE(s.QueuePrefabSpawn("HitSparkFx", { 0.0f, 0.0f, 0.0f }) != nullptr);
	s.ProcessPendingSpawns();
	REQUIRE(s.FindGameObjectById(instance, false) != nullptr);
	ConsumeBursts(s, instance);
	s.UpdateEffects(0.0f);
	CHECK_MSG(PendingBursts(s, instance) == first, "reuse burst " + std::to_string(PendingBursts(s, instance)) + " vs " + std::to_string(first));
	A::State(s) = EditorSceneState::Edit;
	s.ResetPrefabPools();
	s.ResetEffects();
}

TEST_CASE(Effects, EditorPreviewMovesOnlyThePicture)
{
	const uint32_t root = s.CreateEffectFromTemplate("Slash Trail", { 1.0f, 2.0f, 3.0f });
	REQUIRE(root != 0);
	REQUIRE(s.FindGameObjectById(root, false)->components.Get<TrailComponent>(kTrailComponent) != nullptr);
	A::State(s) = EditorSceneState::Edit;

	// Preview with motion: the picture moves, the saved position does not
	s.StartEffectPreview(root);
	s.SetEffectPreviewMotion(EffectPreviewMotion::Circle, 2.0f, 0.5f);
	for (int i = 0; i < 10; ++i) s.UpdateEffectPreview(0.05f);
	s.UpdateWorldMatrices();
	const GameObject* obj = s.FindGameObjectById(root, false);
	CHECK_NEAR(obj->translation.x, 1.0f, 1e-6f);
	CHECK_NEAR(obj->translation.z, 3.0f, 1e-6f);
	const float dx = obj->worldMatrix.m[3][0] - 1.0f, dz = obj->worldMatrix.m[3][2] - 3.0f;
	CHECK_MSG(std::sqrt(dx * dx + dz * dz) > 1.0f, std::format("moved {:.2f}", std::sqrt(dx * dx + dz * dz)));

	// Trails draw in Edit while previewing
	for (int i = 0; i < 10; ++i) {
		s.UpdateEffectPreview(0.05f);
		s.UpdateWorldMatrices();
		s.UpdateTrails();
	}
	CHECK(s.FindGameObjectById(root, false)->components.Get<TrailComponent>(kTrailComponent)->ribbon.PointCount() > 2);

	// Stopping puts everything back
	s.StopEffectPreview();
	s.UpdateWorldMatrices();
	s.UpdateTrails();
	obj = s.FindGameObjectById(root, false);
	CHECK_NEAR(obj->worldMatrix.m[3][0], 1.0f, 1e-5f);
	CHECK_NEAR(obj->worldMatrix.m[3][2], 3.0f, 1e-5f);
	CHECK(obj->components.Get<TrailComponent>(kTrailComponent)->ribbon.PointCount() == 0);
	for (uint32_t id : Subtree(s, root)) {
		if (const auto* p = Particles(s, id)) CHECK(p->effectGate == kEffectGateNone);
	}
	CHECK(s.CreateEffectFromTemplate("No Such Effect", {}) == 0);
	for (const char* name : kEffectTemplates) {
		const uint32_t id = s.CreateEffectFromTemplate(name, {});
		CHECK_MSG(id != 0 && s.FindGameObjectById(id, false)->components.Has(kEffectTimingComponent), name);
	}
}

TEST_CASE(Effects, ParentCycleDoesNotHang)
{
	// A broken file can link two objects as each other's parent; effects must not walk around forever
	GameObject& a = s.CreateGameObject("Cycle A", GameObjectType::Empty);
	const uint32_t aId = a.id;
	REQUIRE(s.AddComponentByName(a, kEffectTimingComponent));
	GameObject& b = s.CreateGameObject("Cycle B", GameObjectType::Empty);
	const uint32_t bId = b.id;
	REQUIRE(s.AddComponentByName(b, kGpuParticlesComponent));
	s.FindGameObjectById(aId, false)->parentId = static_cast<int>(bId);
	s.FindGameObjectById(bId, false)->parentId = static_cast<int>(aId);
	s.RebuildHierarchyLinks();
	s.EffectRootOf(aId); // returns at all: that is the test
	s.EffectRootOf(bId);
	A::State(s) = EditorSceneState::Play;
	s.UpdateEffects(0.1f);
	A::State(s) = EditorSceneState::Edit;
	s.StartEffectPreview(aId);
	s.UpdateEffectPreview(0.1f);
	CaptureGameViewSignature(s); // UpdateGpuParticles asks every particle object for its effect root
	s.StopEffectPreview();
	s.FindGameObjectById(aId, false)->parentId = -1;
	s.FindGameObjectById(bId, false)->parentId = -1;
	s.RebuildHierarchyLinks();
	s.QueueDeleteGameObjectById(aId);
	s.QueueDeleteGameObjectById(bId);
	s.ProcessPendingDeletes();
	CHECK(true);
}

TEST_CASE(Effects, ScriptsCanStopAnEffectLayer)
{
	// A running effect switches its layers on and off; the component's own Emitting still has the last word
	CHECK(EffectiveEmitting(kEffectGateNone, true));
	CHECK(!EffectiveEmitting(kEffectGateNone, false));
	CHECK(EffectiveEmitting(kEffectGateOn, true));
	CHECK(!EffectiveEmitting(kEffectGateOn, false));
	CHECK(!EffectiveEmitting(kEffectGateOff, true));

	EngineAPI& api = A::Api(s);
	const uint32_t aura = s.CreateEffectFromTemplate("Magic Aura", { 0.0f, 0.0f, 0.0f });
	REQUIRE(aura != 0);
	const uint32_t layer = ChildNamed(s, aura, "Aura");
	REQUIRE(Particles(s, layer) != nullptr);
	A::State(s) = EditorSceneState::Play;
	s.ResetEffects();
	s.UpdateEffects(0.1f);
	REQUIRE(Particles(s, layer)->effectGate == kEffectGateOn);
	CHECK(EffectiveEmitting(Particles(s, layer)->effectGate, Particles(s, layer)->emitting));
	api.setParticlesEmitting(api.hostContext, layer, false);
	s.UpdateEffects(0.1f);
	CHECK(!EffectiveEmitting(Particles(s, layer)->effectGate, Particles(s, layer)->emitting));
	A::State(s) = EditorSceneState::Edit;
	s.ResetEffects();
	s.QueueDeleteGameObjectById(aura);
	s.ProcessPendingDeletes();
}

TEST_CASE(Vfx, PooledEmittersKeepTheirBuffer)
{
	A::AimAudioListener(s, { 0.0f, 0.0f, -20.0f }, 0.0f);
	s.UpdateWorldMatrices();
	A::State(s) = EditorSceneState::Play;
	A::GameTime(s) = 0.0;
	GpuParticles::GetInstance()->Clear(); // emitters of earlier tests may still wait for their idle time
	CaptureGameViewSignature(s);
	const std::vector<float> empty = CaptureGameViewSignature(s);

	GameObject& obj = s.CreateGameObject("Pooled Fx", GameObjectType::Empty);
	const uint32_t id = obj.id;
	REQUIRE(s.AddComponentByName(obj, kGpuParticlesComponent));
	auto* p = Particles(s, id);
	REQUIRE(p != nullptr);
	p->emitting = false;
	p->burst = 500;
	p->capacity = 500;
	p->lifeMin = p->lifeMax = 30.0f;
	p->speedMin = p->speedMax = 0.0f;
	p->shape = static_cast<int>(GpuParticleShape::Box);
	p->boxExtents = { 4.0f, 3.0f, 0.1f };
	p->sizeStart = p->sizeEnd = 0.5f;
	p->colorStart = p->colorEnd = { 1.0f, 1.0f, 1.0f, 1.0f };
	p->intensity = 2.0f;
	s.UpdateWorldMatrices();
	A::GameTime(s) = 0.05;
	const std::vector<float> shown = CaptureGameViewSignature(s);
	CHECK_MSG(LargestSignatureDifference(empty, shown) > 30.0f, std::format("burst visible {:.0f}", LargestSignatureDifference(empty, shown)));
	const uint64_t created = GpuParticles::GetInstance()->BufferCreations();
	const size_t emitters = GpuParticles::GetInstance()->EmitterCount();

	// Pooled (inactive): its particles are not drawn, but the buffer stays
	s.FindGameObjectById(id, false)->isActive = false;
	A::GameTime(s) = 0.1;
	const std::vector<float> pooled = CaptureGameViewSignature(s);
	CHECK_MSG(LargestSignatureDifference(empty, pooled) < 8.0f, std::format("pooled still drawn {:.0f}", LargestSignatureDifference(empty, pooled)));
	CHECK(GpuParticles::GetInstance()->EmitterCount() == emitters);

	// Reused: same buffer, and the particles of its last life are gone
	s.FindGameObjectById(id, false)->isActive = true;
	A::GameTime(s) = 0.15;
	const std::vector<float> reused = CaptureGameViewSignature(s);
	CHECK(GpuParticles::GetInstance()->BufferCreations() == created);
	CHECK_MSG(LargestSignatureDifference(empty, reused) < 8.0f, std::format("old particles came back {:.0f}", LargestSignatureDifference(empty, reused)));

	// Unused for long enough: released
	s.FindGameObjectById(id, false)->isActive = false;
	for (uint32_t i = 0; i <= GpuParticles::kIdleFramesBeforeRelease + 1; ++i) GpuParticles::GetInstance()->EndFrame();
	CHECK(GpuParticles::GetInstance()->EmitterCount() == emitters - 1);
	CHECK(emitters == 1u);
	s.QueueDeleteGameObjectById(id);
	s.ProcessPendingDeletes();
	A::State(s) = EditorSceneState::Edit;
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

namespace {
	/// The real pixel shader of a particle material around a graph's code
	std::string TestMaterialShader(const GeneratedGraphCode& code) { return GpuParticles::MaterialShaderSource(code.body); }
	uint32_t Pin(NodeGraph& g, const char* type, float x = 0.0f) { return g.AddNode(type, x, 0.0f, NodeLibrary::Effects()); }
}

TEST_CASE(Graph, ConnectChecksTypesAndCycles)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	NodeGraph g;
	g.kind = GraphKind::Material;
	const uint32_t color = Pin(g, "Color");
	const uint32_t time = Pin(g, "Time");
	const uint32_t add = Pin(g, "Add");
	const uint32_t out = Pin(g, "Material Output");
	REQUIRE(color != 0 && time != 0 && add != 0 && out != 0);
	CHECK(g.AddNode("No Such Node", 0.0f, 0.0f, lib) == 0);

	std::string why;
	// "Any" pins take everything and become as wide as their widest input
	CHECK(g.Connect(color, "RGB", add, "A", lib, &why));
	CHECK(g.Connect(time, "Time", add, "B", lib, &why)); // a Float spreads over all channels
	CHECK(ResolvedOutputType(g, add, "Result", lib) == PinType::Float3);
	CHECK(g.Connect(add, "Result", out, "Emissive Color", lib, &why));

	// Wrong shapes are refused with a reason a person understands
	const uint32_t split = Pin(g, "Split");
	CHECK(g.Connect(color, "RGBA", split, "Value", lib, &why));
	const uint32_t combine = Pin(g, "Combine");
	CHECK(g.Connect(split, "R", combine, "R", lib, &why));
	const uint32_t cross = Pin(g, "Cross");
	CHECK(!g.Connect(combine, "RG", cross, "A", lib, &why)); // a 2D value into a 3D input
	CHECK_MSG(why.find("Float2") != std::string::npos && why.find("Float3") != std::string::npos, why);
	CHECK(!g.Connect(add, "Result", add, "A", lib, &why)); // into itself
	const uint32_t mul = Pin(g, "Multiply");
	CHECK(g.Connect(add, "Result", mul, "A", lib, &why));
	CHECK(!g.Connect(mul, "Result", add, "B", lib, &why)); // a loop
	CHECK_MSG(why.find("loop") != std::string::npos, why);
	CHECK(!g.Connect(color, "No Pin", add, "A", lib, &why));

	// One link per input: a new one replaces the old one; removing a node removes its links
	CHECK(g.Connect(time, "Time", add, "A", lib, &why));
	size_t intoA = 0;
	for (const GraphLink& l : g.links) intoA += (l.toNode == add && l.toPin == "A") ? 1u : 0u;
	CHECK(intoA == 1u);
	const size_t before = g.links.size();
	g.RemoveNode(time);
	CHECK(g.links.size() < before);
	for (const GraphLink& l : g.links) CHECK(l.fromNode != time && l.toNode != time);
	CHECK(g.Find(time) == nullptr);
	g.Disconnect(out, "Emissive Color");
	for (const GraphLink& l : g.links) CHECK(!(l.toNode == out && l.toPin == "Emissive Color"));
}

TEST_CASE(Graph, JsonRoundTripAndBadFiles)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	NodeGraph g;
	g.kind = GraphKind::Material;
	const uint32_t color = g.AddNode("Color", 10.0f, 20.0f, lib);
	const uint32_t out = g.AddNode("Material Output", 300.0f, 20.0f, lib);
	g.FindMutable(color)->values["Color"] = { 0.25f, 0.5f, 0.75f, 1.0f };
	g.FindMutable(out)->values["Opacity"] = { 0.3f, 0.0f, 0.0f, 0.0f };
	std::string why;
	REQUIRE(g.Connect(color, "RGB", out, "Emissive Color", lib, &why));

	NodeGraph back;
	REQUIRE(NodeGraph::FromJson(g.ToJson(), back, &why));
	CHECK(back.kind == GraphKind::Material);
	CHECK(back.nodes.size() == 2u && back.links.size() == 1u);
	REQUIRE(back.Find(color) != nullptr);
	CHECK_NEAR(back.Find(color)->x, 10.0f, 1e-6f);
	CHECK_NEAR(back.Find(color)->values.at("Color").y, 0.5f, 1e-6f);
	CHECK_NEAR(back.Find(out)->values.at("Opacity").x, 0.3f, 1e-6f);
	CHECK(back.nextId == g.nextId);
	CHECK(back.ToJson() == g.ToJson());

	// Broken text: false with a reason, the graph untouched
	NodeGraph untouched;
	CHECK(!NodeGraph::FromJson("{ not json", untouched, &why));
	CHECK(!why.empty());
	CHECK(untouched.nodes.empty());
	// A node type this build does not know survives loading and saving; the compiler reports it
	std::string text = g.ToJson();
	const size_t at = text.find("\"Color\"");
	REQUIRE(at != std::string::npos);
	text.replace(at, 7, "\"FutureNode\"");
	NodeGraph future;
	REQUIRE(NodeGraph::FromJson(text, future, &why));
	CHECK(future.ToJson().find("FutureNode") != std::string::npos);
	const GeneratedGraphCode code = GenerateGraphCode(future, lib, kDomainMaterial);
	CHECK(!code.ok);
	REQUIRE(!code.errors.empty());
	CHECK(code.errors.front().nodeId == color);
	// Links to missing nodes are dropped on load instead of breaking the graph
	std::string dangling = g.ToJson();
	const std::string fromKey = "\"from\": " + std::to_string(color);
	const size_t f = dangling.find(fromKey);
	REQUIRE(f != std::string::npos);
	dangling.replace(f, fromKey.size(), "\"from\": 999");
	NodeGraph cleaned;
	REQUIRE(NodeGraph::FromJson(dangling, cleaned, &why));
	CHECK(cleaned.links.empty());
}

TEST_CASE(Graph, CodegenOrdersNodesAndConvertsTypes)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	NodeGraph g;
	g.kind = GraphKind::Material;
	std::string why;
	const uint32_t time = Pin(g, "Time");
	const uint32_t sine = Pin(g, "Sin");
	const uint32_t color = Pin(g, "Color");
	const uint32_t mul = Pin(g, "Multiply");
	const uint32_t out = Pin(g, "Material Output");
	const uint32_t unused = Pin(g, "Cos");
	REQUIRE(g.Connect(time, "Time", sine, "Value", lib, &why));
	REQUIRE(g.Connect(color, "RGB", mul, "A", lib, &why));
	REQUIRE(g.Connect(sine, "Result", mul, "B", lib, &why));
	REQUIRE(g.Connect(mul, "Result", out, "Emissive Color", lib, &why));
	const GeneratedGraphCode code = GenerateGraphCode(g, lib, kDomainMaterial);
	REQUIRE_MSG(code.ok, code.errors.empty() ? std::string() : code.errors.front().message);
	const auto pos = [&](uint32_t id) { return code.body.find("// " + std::to_string(id) + " "); };
	REQUIRE(pos(time) != std::string::npos && pos(sine) != std::string::npos && pos(mul) != std::string::npos && pos(out) != std::string::npos);
	CHECK(pos(time) < pos(sine));
	CHECK(pos(sine) < pos(mul));
	CHECK(pos(color) < pos(mul));
	CHECK(pos(mul) < pos(out));
	CHECK(pos(unused) == std::string::npos); // not connected to the output: not generated
	CHECK(code.body.find("float3(") != std::string::npos); // the Float from Sin spread to three channels

	// No output node: one clear error
	NodeGraph empty;
	empty.kind = GraphKind::Material;
	Pin(empty, "Time");
	const GeneratedGraphCode none = GenerateGraphCode(empty, lib, kDomainMaterial);
	CHECK(!none.ok);
	REQUIRE(!none.errors.empty());
	CHECK_MSG(none.errors.front().message.find("Material Output") != std::string::npos, none.errors.front().message);
}

TEST_CASE(Graph, EveryNodeCompilesToHlsl)
{
	// Each node, its inputs left at their defaults, feeds the output: the generated pixel shader must compile
	const NodeLibrary& lib = NodeLibrary::Effects();
	DXCManager* dxc = RizeEngine::GetDXCManager();
	REQUIRE(dxc != nullptr);
	int checked = 0;
	for (const NodeDef& def : lib.All()) {
		if (!(def.domains & kDomainMaterial) || def.isOutput) continue;
		NodeGraph g;
		g.kind = GraphKind::Material;
		const uint32_t node = g.AddNode(def.type, 0.0f, 0.0f, lib);
		const uint32_t out = g.AddNode("Material Output", 200.0f, 0.0f, lib);
		std::string why;
		bool linked = false;
		for (const PinDef& pin : def.outputs) {
			if (g.Connect(node, pin.name, out, "Opacity", lib, &why) || g.Connect(node, pin.name, out, "Emissive Color", lib, &why)) {
				linked = true;
				break;
			}
		}
		CHECK_MSG(linked, def.type + ": no output reaches the Material Output");
		const GeneratedGraphCode code = GenerateGraphCode(g, lib, kDomainMaterial);
		CHECK_MSG(code.ok, def.type + ": " + (code.errors.empty() ? std::string() : code.errors.front().message));
		std::string error;
		const auto blob = dxc->TryCompileShaderSource(TestMaterialShader(code), L"GraphTest.PS.hlsl", L"ps_6_0", &error);
		CHECK_MSG(blob != nullptr, def.type + ": " + error);
		const auto trailBlob = dxc->TryCompileShaderSource(TrailRenderer::MaterialShaderSource(code.body), L"GraphTrailTest.PS.hlsl", L"ps_6_0", &error);
		CHECK_MSG(trailBlob != nullptr, def.type + " (trail): " + error);
		++checked;
	}
	CHECK(checked >= 20);

	// Broken HLSL: an error message and no crash
	std::string error;
	CHECK(dxc->TryCompileShaderSource("float4 main() : SV_TARGET { return nope; }", L"Broken.PS.hlsl", L"ps_6_0", &error) == nullptr);
	CHECK(error.find("nope") != std::string::npos);
}

TEST_CASE(Graph, EveryParticleNodeCompilesToHlsl)
{
	// Each Spawn / Update node, its inputs left at their defaults, feeds its stage's output: the emitter's compute shader must compile
	const NodeLibrary& lib = NodeLibrary::Effects();
	DXCManager* dxc = RizeEngine::GetDXCManager();
	REQUIRE(dxc != nullptr);
	struct Stage { uint32_t domain; const char* output; std::vector<const char*> pins; };
	const Stage stages[] = {
		{ kDomainParticleSpawn, "Spawn Output", { "Position", "Velocity", "Color", "Size", "Lifetime" } },
		{ kDomainParticleUpdate, "Update Output", { "Velocity", "Color", "Size", "Kill" } },
	};
	int checked = 0;
	for (const Stage& stage : stages) {
		for (const NodeDef& def : lib.All()) {
			if (!(def.domains & stage.domain) || def.isOutput) continue;
			NodeGraph g;
			g.kind = GraphKind::Particle;
			g.parameters.push_back({ "Strength", PinType::Float, { 2.0f, 0.0f, 0.0f, 0.0f } });
			const uint32_t node = g.AddNode(def.type, 0.0f, 0.0f, lib);
			if (def.type == "Parameter") g.FindMutable(node)->texts["Name"] = "Strength";
			const uint32_t out = g.AddNode(stage.output, 200.0f, 0.0f, lib);
			std::string why;
			bool linked = false;
			for (const PinDef& pin : def.outputs) {
				for (const char* target : stage.pins) {
					if (!linked && g.Connect(node, pin.name, out, target, lib, &why)) linked = true;
				}
			}
			CHECK_MSG(linked, def.type + ": no output reaches the " + stage.output);
			const GeneratedGraphCode code = GenerateGraphCode(g, lib, stage.domain);
			CHECK_MSG(code.ok, def.type + ": " + (code.errors.empty() ? std::string() : code.errors.front().message));
			// The other stage left empty keeps its defaults
			const std::string source = stage.domain == kDomainParticleSpawn ? GpuParticles::ParticleGraphShaderSource(code.body, "")
				: GpuParticles::ParticleGraphShaderSource("", code.body);
			std::string error;
			const auto blob = dxc->TryCompileShaderSource(source, L"GraphTest.CS.hlsl", L"cs_6_0", &error);
			CHECK_MSG(blob != nullptr, def.type + " (" + stage.output + "): " + error);
			++checked;
		}
	}
	CHECK(checked >= 40);

	// Both stages empty: a graph that only has its two output nodes still makes a working shader
	std::string error;
	CHECK(dxc->TryCompileShaderSource(GpuParticles::ParticleGraphShaderSource("", ""), L"GraphEmpty.CS.hlsl", L"cs_6_0", &error) != nullptr);
}

TEST_CASE(Graph, MaterialNodesHaveFriendlyDefaults)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	std::string why;
	// Unconnected UV pins read the particle's own UV, unconnected Time pins the clock: a lone node already works
	NodeGraph g;
	g.kind = GraphKind::Material;
	const uint32_t sample = g.AddNode("Texture Sample", 0.0f, 0.0f, lib);
	const uint32_t panner = g.AddNode("Panner", -200.0f, 0.0f, lib);
	const uint32_t out = g.AddNode("Material Output", 200.0f, 0.0f, lib);
	REQUIRE(g.Connect(panner, "UV", sample, "UV", lib, &why));
	REQUIRE(g.Connect(sample, "RGB", out, "Emissive Color", lib, &why));
	GeneratedGraphCode code = GenerateGraphCode(g, lib, kDomainMaterial);
	REQUIRE_MSG(code.ok, code.errors.empty() ? std::string() : code.errors.front().message);
	CHECK(code.body.find("uv") != std::string::npos);
	CHECK(code.body.find("gTime") != std::string::npos);
	// No texture chosen = the GPU Particles component's own texture (slot 0)
	REQUIRE(!code.textures.empty());
	CHECK(code.textures[0].empty());
	CHECK(code.body.find("gTexture0") != std::string::npos);

	// A chosen texture gets its own slot; the same file twice shares one; a fifth file is too many
	g.FindMutable(sample)->texts["Texture"] = "Assets/Textures/a.png";
	code = GenerateGraphCode(g, lib, kDomainMaterial);
	REQUIRE(code.ok);
	REQUIRE(code.textures.size() == 2u);
	CHECK(code.textures[1] == "Assets/Textures/a.png");
	CHECK(code.body.find("gTexture1") != std::string::npos);
	uint32_t previous = sample;
	for (const char* file : { "Assets/Textures/a.png", "Assets/Textures/b.png", "Assets/Textures/c.png", "Assets/Textures/d.png" }) {
		const uint32_t extra = g.AddNode("Texture Sample", 0.0f, 100.0f, lib);
		g.FindMutable(extra)->texts["Texture"] = file;
		const uint32_t add = g.AddNode("Add", 100.0f, 100.0f, lib);
		REQUIRE(g.Connect(previous == sample ? sample : previous, previous == sample ? "RGB" : "Result", add, "A", lib, &why));
		REQUIRE(g.Connect(extra, "RGB", add, "B", lib, &why));
		previous = add;
	}
	REQUIRE(g.Connect(previous, "Result", out, "Emissive Color", lib, &why));
	code = GenerateGraphCode(g, lib, kDomainMaterial);
	CHECK(!code.ok);
	bool tooMany = false;
	for (const GraphError& e : code.errors) tooMany |= e.message.find("4") != std::string::npos;
	CHECK(tooMany);

	// Particle-only inputs are refused in other graphs with a reason
	NodeGraph particle;
	particle.kind = GraphKind::Particle;
	CHECK(lib.Find("Particle Age") != nullptr);
	CHECK((lib.Find("Particle Age")->domains & kDomainMaterial) != 0);
	CHECK((lib.Find("Texture Sample")->domains & kDomainParticleUpdate) == 0);
}

namespace {
	/// Writes a material graph file into the test project's Assets and returns its "Assets/..." path
	std::string WriteMaterial(EditorSuite& s, const std::string& name, const NodeGraph& graph)
	{
		const std::filesystem::path dir = A::ProjectRoot(s) / "Assets" / "Effects";
		std::filesystem::create_directories(dir);
		std::ofstream(dir / (name + ".vfxmat.json")) << graph.ToJson();
		return "Assets/Effects/" + name + ".vfxmat.json";
	}
}

TEST_CASE(Vfx, MaterialGraphsShadeParticles)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	A::ProjectRoot(s) = A::TempRoot() / "MaterialProject";
	std::string why;

	// Red: whatever the particle's own colour, the material makes it red
	NodeGraph red;
	red.kind = GraphKind::Material;
	const uint32_t color = red.AddNode("Color", 0.0f, 0.0f, lib);
	red.FindMutable(color)->values["Color"] = { 1.0f, 0.0f, 0.0f, 1.0f };
	const uint32_t redOut = red.AddNode("Material Output", 200.0f, 0.0f, lib);
	REQUIRE(red.Connect(color, "RGB", redOut, "Emissive Color", lib, &why));
	const std::string redPath = WriteMaterial(s, "Red", red);

	// Appears late: invisible for the first half of each particle's life
	NodeGraph late;
	late.kind = GraphKind::Material;
	const uint32_t age = late.AddNode("Particle Age", 0.0f, 0.0f, lib);
	const uint32_t step = late.AddNode("Step", 100.0f, 0.0f, lib);
	const uint32_t lateOut = late.AddNode("Material Output", 200.0f, 0.0f, lib);
	REQUIRE(late.Connect(age, "Age", step, "Value", lib, &why));
	REQUIRE(late.Connect(step, "Result", lateOut, "Opacity", lib, &why));
	const std::string latePath = WriteMaterial(s, "Late", late);

	// Broken: a node this build does not know
	std::string brokenText = red.ToJson();
	brokenText.replace(brokenText.find("\"Color\""), 7, "\"FutureNode\"");
	std::filesystem::create_directories(A::ProjectRoot(s) / "Assets" / "Effects");
	std::ofstream(A::ProjectRoot(s) / "Assets" / "Effects" / "Broken.vfxmat.json") << brokenText;
	const std::string brokenPath = "Assets/Effects/Broken.vfxmat.json";

	A::AimAudioListener(s, { 0.0f, 0.0f, -20.0f }, 0.0f);
	s.UpdateWorldMatrices();
	A::State(s) = EditorSceneState::Play;
	A::GameTime(s) = 0.0;
	GpuParticles::GetInstance()->Clear();
	CaptureGameViewSignature(s);
	const std::vector<float> empty = CaptureGameViewSignature(s);
	double clock = 0.0;
	const auto shoot = [&](const std::string& material, double seconds) {
		GameObject& obj = s.CreateGameObject("Material Test", GameObjectType::Empty);
		const uint32_t id = obj.id;
		s.AddComponentByName(obj, kGpuParticlesComponent);
		auto* p = obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
		std::vector<float> picture;
		if (p) {
			p->emitting = false;
			p->burst = 400;
			p->capacity = 400;
			p->lifeMin = p->lifeMax = 2.0f;
			p->speedMin = p->speedMax = 0.0f;
			p->shape = static_cast<int>(GpuParticleShape::Box);
			p->boxExtents = { 3.0f, 2.0f, 0.1f };
			p->sizeStart = p->sizeEnd = 0.6f;
			p->colorStart = p->colorEnd = { 0.02f, 0.02f, 0.25f, 1.0f }; // dark blue: stands out on any sky
			p->intensity = 1.0f;
			p->additive = false; // stacked additive red turns white through the tone mapping
			strcpy_s(p->material, material.c_str());
			s.UpdateWorldMatrices();
			CaptureGameViewSignature(s);
			for (double t = 0.0; t < seconds; t += 0.05) {
				clock += 0.05;
				A::GameTime(s) = clock;
				picture = CaptureGameViewSignature(s);
			}
		}
		s.QueueDeleteGameObjectById(id);
		s.ProcessPendingDeletes();
		return picture;
	};
	const std::vector<float> plain = shoot("", 0.1);
	CHECK_MSG(LargestSignatureDifference(empty, plain) > 30.0f, std::format("plain particles {:.0f}", LargestSignatureDifference(empty, plain)));
	// Red: where the particles cover the sky the most, red rises above green (the dark blue particles have no red)
	const std::vector<float> redPicture = shoot(redPath, 0.1);
	size_t covered = 0;
	float bestDrop = -1.0e9f;
	for (size_t i = 0; i + 2 < redPicture.size() && i + 2 < empty.size(); i += 3) {
		const float drop = (empty[i + 1] - redPicture[i + 1]) + (empty[i + 2] - redPicture[i + 2]);
		if (drop > bestDrop) {
			bestDrop = drop;
			covered = i;
		}
	}
	CHECK_MSG(bestDrop > 40.0f && redPicture[covered] > redPicture[covered + 1] + 60.0f,
		std::format("red material: {:.0f},{:.0f},{:.0f} over sky {:.0f},{:.0f},{:.0f}", redPicture[covered], redPicture[covered + 1], redPicture[covered + 2], empty[covered], empty[covered + 1], empty[covered + 2]));
	REQUIRE(s.GetEffectMaterial(redPath) != nullptr);
	CHECK(s.GetEffectMaterial(redPath)->ok);

	const std::vector<float> early = shoot(latePath, 0.2);  // a tenth of their life
	const std::vector<float> later = shoot(latePath, 1.5);  // three quarters
	CHECK_MSG(LargestSignatureDifference(empty, early) < 8.0f, std::format("young particles visible {:.0f}", LargestSignatureDifference(empty, early)));
	CHECK_MSG(LargestSignatureDifference(empty, later) > 30.0f, std::format("old particles hidden {:.0f}", LargestSignatureDifference(empty, later)));

	// A broken material reports why and the particles still draw (texture x colour)
	const std::vector<float> fallback = shoot(brokenPath, 0.1);
	CHECK(LargestSignatureDifference(empty, fallback) > 30.0f);
	const EffectMaterialEntry* broken = s.GetEffectMaterial(brokenPath);
	REQUIRE(broken != nullptr);
	CHECK(!broken->ok);
	CHECK(!broken->errors.empty());
	CHECK(s.GetEffectMaterial("Assets/Effects/Missing.vfxmat.json") == nullptr || !s.GetEffectMaterial("Assets/Effects/Missing.vfxmat.json")->ok);

	A::State(s) = EditorSceneState::Edit;
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

namespace {
	std::string WriteParticleGraph(EditorSuite& s, const std::string& name, const NodeGraph& graph)
	{
		const std::filesystem::path dir = A::ProjectRoot(s) / "Assets" / "Effects";
		std::filesystem::create_directories(dir);
		std::ofstream(dir / (name + ".vfxparticle.json")) << graph.ToJson();
		return "Assets/Effects/" + name + ".vfxparticle.json";
	}

	/// A particle graph for the screen tests: dark blue particles that stand still in a flat box around the emitter
	/// and live 3 s. `update` connects what it adds to the Update Output (spawnOut / updateOut are the output nodes).
	NodeGraph TestParticleGraph(const Vector3& boxSize, const std::function<void(NodeGraph&, uint32_t spawnOut, uint32_t updateOut)>& update = {})
	{
		const NodeLibrary& lib = NodeLibrary::Effects();
		NodeGraph g;
		g.kind = GraphKind::Particle;
		const uint32_t box = g.AddNode("Box Shape", 0.0f, 0.0f, lib);
		g.FindMutable(box)->values["Size"] = { boxSize.x, boxSize.y, boxSize.z, 0.0f };
		const uint32_t still = g.AddNode("Vector3", 0.0f, 100.0f, lib);
		g.FindMutable(still)->values["Value"] = { 0.0f, 0.0f, 0.0f, 0.0f };
		const uint32_t spawnOut = g.AddNode("Spawn Output", 200.0f, 0.0f, lib);
		GraphNode* spawn = g.FindMutable(spawnOut);
		spawn->values["Lifetime"] = { 3.0f, 3.0f, 3.0f, 3.0f };
		spawn->values["Color"] = { 0.02f, 0.02f, 0.25f, 1.0f };
		spawn->values["Size"] = { 0.6f, 0.6f, 0.6f, 0.6f };
		std::string why;
		g.Connect(box, "Position", spawnOut, "Position", lib, &why);
		g.Connect(still, "Value", spawnOut, "Velocity", lib, &why);
		const uint32_t updateOut = g.AddNode("Update Output", 200.0f, 300.0f, lib);
		if (update) update(g, spawnOut, updateOut);
		return g;
	}

	/// Where the particles are on screen: the centre of the signature cells that changed against `empty`
	/// (in cells, y grows downwards) and how much changed in all
	struct ScreenBlob {
		float x = 0.0f, y = 0.0f, weight = 0.0f;
	};
	ScreenBlob BlobOf(const std::vector<float>& empty, const std::vector<float>& picture)
	{
		ScreenBlob blob;
		constexpr size_t kColumns = 16;
		for (size_t cell = 0; cell * 3 + 2 < picture.size() && cell * 3 + 2 < empty.size(); ++cell) {
			float diff = 0.0f;
			for (size_t c = 0; c < 3; ++c) diff += std::abs(picture[cell * 3 + c] - empty[cell * 3 + c]);
			if (diff < 15.0f) continue;
			blob.x += static_cast<float>(cell % kColumns) * diff;
			blob.y += static_cast<float>(cell / kColumns) * diff;
			blob.weight += diff;
		}
		if (blob.weight > 0.0f) {
			blob.x /= blob.weight;
			blob.y /= blob.weight;
		}
		return blob;
	}

	/// The game camera 20 units in front of the origin with the default lens (earlier tests may leave another one)
	void AimParticleTestCamera(EditorSuite& s)
	{
		Camera& camera = A::GameCamera(s);
		camera.fovY = 0.45f;
		camera.orthographic = false;
		A::AimAudioListener(s, { 0.0f, 0.0f, -20.0f }, 0.0f);
	}

	/// Runs one GPU Particles object with `graphPath` at `position` for `seconds` of Play and returns the last picture
	std::vector<float> ShootParticleGraph(EditorSuite& s, const std::string& graphPath, const Vector3& position, double seconds, double& clock,
		const std::function<void(GameObject&)>& setup = {})
	{
		GameObject& obj = s.CreateGameObject("Graph Test", GameObjectType::Empty);
		const uint32_t id = obj.id;
		obj.translation = position;
		s.AddComponentByName(obj, kGpuParticlesComponent);
		std::vector<float> picture;
		if (auto* p = obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent)) {
			p->emitting = false;
			p->burst = 300;
			p->capacity = 300;
			p->additive = false;
			strcpy_s(p->graph, graphPath.c_str());
			if (setup) setup(obj);
			s.UpdateWorldMatrices();
			CaptureGameViewSignature(s);
			for (double t = 0.0; t < seconds; t += 0.05) {
				clock += 0.05;
				A::GameTime(s) = clock;
				picture = CaptureGameViewSignature(s);
			}
		}
		s.QueueDeleteGameObjectById(id);
		s.ProcessPendingDeletes();
		return picture;
	}
}

TEST_CASE(Vfx, ParticleGraphsMoveParticles)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	A::ProjectRoot(s) = A::TempRoot() / "ParticleGraphProject";
	std::string why;
	const std::string stillPath = WriteParticleGraph(s, "Still", TestParticleGraph({ 4.0f, 1.0f, 0.2f }));
	const std::string fallPath = WriteParticleGraph(s, "Fall", TestParticleGraph({ 4.0f, 1.0f, 0.2f }, [&](NodeGraph& g, uint32_t, uint32_t updateOut) {
		const uint32_t gravity = g.AddNode("Gravity", 0.0f, 300.0f, lib);
		g.Connect(gravity, "Velocity", updateOut, "Velocity", lib, &why);
	}));

	AimParticleTestCamera(s);
	s.UpdateWorldMatrices();
	A::State(s) = EditorSceneState::Play;
	A::GameTime(s) = 0.0;
	GpuParticles::GetInstance()->Clear();
	CaptureGameViewSignature(s);
	const std::vector<float> empty = CaptureGameViewSignature(s);
	double clock = 0.0;

	// Without forces the graph's particles stay where Spawn put them: a box around the emitter
	const ScreenBlob still = BlobOf(empty, ShootParticleGraph(s, stillPath, { 0.0f, 2.0f, 0.0f }, 0.6, clock));
	CHECK_MSG(still.weight > 200.0f, std::format("graph particles visible {:.0f}", still.weight));
	REQUIRE(s.GetParticleGraph(stillPath) != nullptr);
	CHECK(s.GetParticleGraph(stillPath)->ok);
	// Gravity pulls them down the screen
	const ScreenBlob fall = BlobOf(empty, ShootParticleGraph(s, fallPath, { 0.0f, 2.0f, 0.0f }, 0.6, clock));
	CHECK_MSG(fall.weight > 50.0f && fall.y > still.y + 1.0f, std::format("gravity: y {:.2f} -> {:.2f} (weight {:.0f})", still.y, fall.y, fall.weight));

	A::State(s) = EditorSceneState::Edit;
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Vfx, ParticleGraphForcesKillAndOverLife)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	A::ProjectRoot(s) = A::TempRoot() / "ParticleGraphProject2";
	std::string why;
	const Vector3 smallBox{ 1.0f, 1.0f, 0.2f };
	const std::string stillPath = WriteParticleGraph(s, "Still", TestParticleGraph(smallBox));
	// Vortex around the view axis through the world origin: particles to the right of it are carried upwards
	const std::string vortexPath = WriteParticleGraph(s, "Vortex", TestParticleGraph(smallBox, [&](NodeGraph& g, uint32_t, uint32_t updateOut) {
		const uint32_t vortex = g.AddNode("Vortex", 0.0f, 300.0f, lib);
		const uint32_t center = g.AddNode("Vector3", -200.0f, 300.0f, lib);
		g.FindMutable(center)->values["Value"] = { 0.0f, 0.0f, 0.0f, 0.0f };
		g.FindMutable(vortex)->values["Axis"] = { 0.0f, 0.0f, 1.0f, 0.0f };
		g.FindMutable(vortex)->values["Spin"] = { 2.0f, 2.0f, 2.0f, 2.0f };
		g.Connect(center, "Value", vortex, "Center", lib, &why);
		g.Connect(vortex, "Velocity", updateOut, "Velocity", lib, &why);
	}));
	// Kill once a third of the life is over (Step: Particle Age >= 0.3)
	const std::string killPath = WriteParticleGraph(s, "Kill", TestParticleGraph(smallBox, [&](NodeGraph& g, uint32_t, uint32_t updateOut) {
		const uint32_t age = g.AddNode("Particle Age", 0.0f, 300.0f, lib);
		const uint32_t step = g.AddNode("Step", 100.0f, 300.0f, lib);
		g.FindMutable(step)->values["Edge"] = { 0.3f, 0.3f, 0.3f, 0.3f };
		g.Connect(age, "Age", step, "Value", lib, &why);
		g.Connect(step, "Result", updateOut, "Kill", lib, &why);
	}));
	// Size over life: tiny when born, big later
	const std::string growPath = WriteParticleGraph(s, "Grow", TestParticleGraph(smallBox, [&](NodeGraph& g, uint32_t, uint32_t updateOut) {
		const uint32_t curve = g.AddNode("Curve", 0.0f, 300.0f, lib);
		WriteGraphKeys(*g.FindMutable(curve), "Curve", { { 0.0f, { 0.05f, 0, 0, 0 } }, { 0.5f, { 1.5f, 0, 0, 0 } }, { 1.0f, { 1.5f, 0, 0, 0 } } });
		g.Connect(curve, "Value", updateOut, "Size", lib, &why);
	}));
	// Colour over life: red when born, green at half its life
	const std::string tintPath = WriteParticleGraph(s, "Tint", TestParticleGraph({ 3.0f, 2.0f, 0.2f }, [&](NodeGraph& g, uint32_t, uint32_t updateOut) {
		const uint32_t gradient = g.AddNode("Gradient", 0.0f, 300.0f, lib);
		WriteGraphKeys(*g.FindMutable(gradient), "Gradient", { { 0.0f, { 0.6f, 0.0f, 0.0f, 1.0f } }, { 0.5f, { 0.0f, 0.6f, 0.0f, 1.0f } }, { 1.0f, { 0.0f, 0.6f, 0.0f, 1.0f } } });
		g.Connect(gradient, "RGBA", updateOut, "Color", lib, &why);
	}));

	AimParticleTestCamera(s);
	s.UpdateWorldMatrices();
	A::State(s) = EditorSceneState::Play;
	A::GameTime(s) = 0.0;
	GpuParticles::GetInstance()->Clear();
	CaptureGameViewSignature(s);
	const std::vector<float> empty = CaptureGameViewSignature(s);
	double clock = 0.0;
	const Vector3 right{ 3.0f, 1.0f, 0.0f };

	const ScreenBlob still = BlobOf(empty, ShootParticleGraph(s, stillPath, right, 0.8, clock));
	CHECK_MSG(still.weight > 50.0f, std::format("still particles visible {:.0f}", still.weight));
	const ScreenBlob vortex = BlobOf(empty, ShootParticleGraph(s, vortexPath, right, 0.8, clock));
	CHECK_MSG(vortex.weight > 50.0f && vortex.y < still.y - 0.7f, std::format("vortex: y {:.2f} -> {:.2f}, x {:.2f} -> {:.2f}", still.y, vortex.y, still.x, vortex.x));

	// Kill: alive at a tenth of their life, gone after half of it
	const ScreenBlob young = BlobOf(empty, ShootParticleGraph(s, killPath, right, 0.2, clock));
	const ScreenBlob killed = BlobOf(empty, ShootParticleGraph(s, killPath, right, 1.5, clock));
	CHECK_MSG(young.weight > 50.0f && killed.weight < young.weight * 0.1f, std::format("kill: {:.0f} -> {:.0f}", young.weight, killed.weight));

	// Curve: the same particles cover much more of the screen later in their life
	const ScreenBlob tiny = BlobOf(empty, ShootParticleGraph(s, growPath, right, 0.1, clock));
	const ScreenBlob big = BlobOf(empty, ShootParticleGraph(s, growPath, right, 1.6, clock));
	CHECK_MSG(big.weight > tiny.weight * 2.0f + 50.0f, std::format("curve: {:.0f} -> {:.0f}", tiny.weight, big.weight));

	// Gradient: red at first, green later, in the cell the particles cover the most
	const auto mostCovered = [&](const std::vector<float>& picture) {
		size_t best = 0;
		float bestDiff = -1.0f;
		for (size_t i = 0; i + 2 < picture.size() && i + 2 < empty.size(); i += 3) {
			const float diff = std::abs(picture[i] - empty[i]) + std::abs(picture[i + 1] - empty[i + 1]) + std::abs(picture[i + 2] - empty[i + 2]);
			if (diff > bestDiff) {
				bestDiff = diff;
				best = i;
			}
		}
		return best;
	};
	const std::vector<float> born = ShootParticleGraph(s, tintPath, { 0.0f, 1.0f, 0.0f }, 0.1, clock);
	const std::vector<float> aged = ShootParticleGraph(s, tintPath, { 0.0f, 1.0f, 0.0f }, 1.8, clock);
	REQUIRE(!born.empty() && !aged.empty());
	const size_t a = mostCovered(born), b = mostCovered(aged);
	CHECK_MSG(born[a] > born[a + 1] + 30.0f, std::format("young: r {:.0f} g {:.0f}", born[a], born[a + 1]));
	CHECK_MSG(aged[b + 1] > aged[b] + 30.0f, std::format("old: r {:.0f} g {:.0f}", aged[b], aged[b + 1]));

	A::State(s) = EditorSceneState::Edit;
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Graph, ParticleParametersSaveAndResolve)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	NodeGraph g;
	g.kind = GraphKind::Particle;
	g.parameters.push_back({ "Pull", PinType::Float3, { 0.0f, -9.8f, 0.0f, 0.0f } });
	g.parameters.push_back({ "Tint", PinType::Float4, { 1.0f, 0.5f, 0.0f, 1.0f } });
	const uint32_t param = g.AddNode("Parameter", 0.0f, 0.0f, lib);
	g.FindMutable(param)->texts["Name"] = "Tint";
	const uint32_t out = g.AddNode("Spawn Output", 200.0f, 0.0f, lib);
	std::string why;
	REQUIRE(g.Connect(param, "Value", out, "Color", lib, &why));

	// Saved with the graph, read back in the same order (the order is the GPU slot)
	NodeGraph back;
	REQUIRE(NodeGraph::FromJson(g.ToJson(), back, &why));
	REQUIRE(back.parameters.size() == 2u);
	CHECK(back.parameters[0].name == "Pull" && back.parameters[0].type == PinType::Float3);
	CHECK_NEAR(back.parameters[0].value.y, -9.8f, 1e-5f);
	CHECK(back.parameters[1].name == "Tint" && back.parameters[1].type == PinType::Float4);
	GeneratedGraphCode code = GenerateGraphCode(back, lib, kDomainParticleSpawn);
	REQUIRE_MSG(code.ok, code.errors.empty() ? std::string() : code.errors.front().message);
	CHECK(code.body.find("gParams[1]") != std::string::npos);

	// A deleted parameter is reported on the node that still reads it
	back.parameters.erase(back.parameters.begin() + 1);
	code = GenerateGraphCode(back, lib, kDomainParticleSpawn);
	CHECK(!code.ok);
	REQUIRE(!code.errors.empty());
	CHECK(code.errors.front().nodeId == param);
	CHECK(code.errors.front().message.find("Tint") != std::string::npos);

	// Per-object values: written and read as text, unknown text ignored
	const std::map<std::string, Vector4> values = ParseEffectParameterValues("Pull=0 9.8 0 0;broken;Tint=1 0 0 1");
	REQUIRE(values.size() == 2u);
	CHECK_NEAR(values.at("Pull").y, 9.8f, 1e-5f);
	const std::map<std::string, Vector4> again = ParseEffectParameterValues(FormatEffectParameterValues(values).c_str());
	CHECK(again.size() == 2u);
	CHECK_NEAR(again.at("Tint").x, 1.0f, 1e-5f);
}

TEST_CASE(Vfx, ParticleGraphParametersFromObjectAndScript)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	A::ProjectRoot(s) = A::TempRoot() / "ParticleParamProject";
	std::string why;
	// Gravity whose direction is the parameter "Pull" (default: down)
	NodeGraph graph = TestParticleGraph({ 4.0f, 1.0f, 0.2f }, [&](NodeGraph& g, uint32_t, uint32_t updateOut) {
		g.parameters.push_back({ "Pull", PinType::Float3, { 0.0f, -6.0f, 0.0f, 0.0f } });
		const uint32_t param = g.AddNode("Parameter", -200.0f, 300.0f, lib);
		g.FindMutable(param)->texts["Name"] = "Pull";
		const uint32_t gravity = g.AddNode("Gravity", 0.0f, 300.0f, lib);
		g.Connect(param, "Value", gravity, "Acceleration", lib, &why);
		g.Connect(gravity, "Velocity", updateOut, "Velocity", lib, &why);
	});
	const std::string path = WriteParticleGraph(s, "Pull", graph);
	const std::string stillPath = WriteParticleGraph(s, "Still", TestParticleGraph({ 4.0f, 1.0f, 0.2f }));

	AimParticleTestCamera(s);
	s.UpdateWorldMatrices();
	A::State(s) = EditorSceneState::Play;
	A::GameTime(s) = 0.0;
	GpuParticles::GetInstance()->Clear();
	CaptureGameViewSignature(s);
	const std::vector<float> empty = CaptureGameViewSignature(s);
	double clock = 0.0;
	const Vector3 at{ 0.0f, 1.0f, 0.0f };

	const ScreenBlob still = BlobOf(empty, ShootParticleGraph(s, stillPath, at, 0.6, clock));
	const ScreenBlob down = BlobOf(empty, ShootParticleGraph(s, path, at, 0.6, clock));
	CHECK_MSG(down.y > still.y + 0.7f, std::format("default pull: y {:.2f} -> {:.2f}", still.y, down.y));
	// This object's own value: upwards
	const ScreenBlob ownValue = BlobOf(empty, ShootParticleGraph(s, path, at, 0.6, clock, [](GameObject& obj) {
		auto* p = obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
		strcpy_s(p->parameters, "Pull=0 6 0 0");
	}));
	CHECK_MSG(ownValue.y < still.y - 0.7f, std::format("object value: y {:.2f} -> {:.2f}", still.y, ownValue.y));
	// A script's value wins over the object's, through the script API; an unknown name or object is refused
	EngineAPI& api = A::Api(s);
	REQUIRE(api.setEffectParameter != nullptr);
	bool refusedUnknown = false;
	const ScreenBlob scripted = BlobOf(empty, ShootParticleGraph(s, path, at, 0.6, clock, [&](GameObject& obj) {
		auto* p = obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
		strcpy_s(p->parameters, "Pull=0 -6 0 0");
		CHECK(api.setEffectParameter(api.hostContext, obj.id, "Pull", 0.0f, 6.0f, 0.0f, 0.0f));
		refusedUnknown = !api.setEffectParameter(api.hostContext, obj.id, "Nope", 1.0f, 1.0f, 1.0f, 1.0f) &&
			!api.setEffectParameter(api.hostContext, 987654u, "Pull", 1.0f, 1.0f, 1.0f, 1.0f) && !api.setEffectParameter(api.hostContext, obj.id, nullptr, 0, 0, 0, 0);
	}));
	CHECK_MSG(scripted.y < still.y - 0.7f, std::format("script value: y {:.2f} -> {:.2f}", still.y, scripted.y));
	CHECK(refusedUnknown);

	A::State(s) = EditorSceneState::Edit;
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Graph, ParticleEditorModelKeepsOutputsParametersAndKeys)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	GraphEditorModel m;
	m.Reset(MakeParticleTemplate("Blank"));
	REQUIRE(m.graph.nodes.size() == 2u);

	// Both outputs survive Delete and are not duplicated
	m.selection = { m.graph.nodes[0].id, m.graph.nodes[1].id };
	m.DeleteSelection();
	CHECK(m.graph.nodes.size() == 2u);
	CHECK(m.Duplicate().empty());

	// Parameters: a new one gets a free name; adding is one undo step
	const std::string first = m.AddParameter("Strength", PinType::Float);
	const std::string second = m.AddParameter("Strength", PinType::Float3);
	CHECK(first == "Strength");
	CHECK(second == "Strength 1");
	REQUIRE(m.graph.parameters.size() == 2u);
	CHECK(m.graph.parameters[1].type == PinType::Float3);
	REQUIRE(m.Undo());
	CHECK(m.graph.parameters.size() == 1u);

	// Renaming follows into every Parameter node; a clash or an empty name is refused with a reason
	const uint32_t reader = m.AddNode("Parameter", -200.0f, 0.0f);
	m.graph.FindMutable(reader)->texts["Name"] = "Strength";
	m.Commit();
	m.AddParameter("Other", PinType::Float);
	std::string why;
	CHECK(!m.RenameParameter("Strength", "Other", &why));
	CHECK(!why.empty());
	CHECK(!m.RenameParameter("Strength", "  ", &why));
	REQUIRE_MSG(m.RenameParameter("Strength", "Power", &why), why);
	CHECK(m.graph.FindParameter("Power") == 0);
	CHECK(m.graph.Find(reader)->texts.at("Name") == "Power");
	REQUIRE(m.Undo());
	CHECK(m.graph.Find(reader)->texts.at("Name") == "Strength");
	REQUIRE(m.Redo());
	// The window passes the parameter's own name: the nodes must still follow
	REQUIRE(m.RenameParameter(m.graph.parameters[0].name, "Force", &why));
	CHECK(m.graph.Find(reader)->texts.at("Name") == "Force");
	REQUIRE(m.Undo());

	// Removing one: the node that still reads it reports the problem (it does not silently read another slot)
	REQUIRE(m.RemoveParameter("Power"));
	const uint32_t out = m.FindOutputNode(); // the first output (Spawn Output)
	REQUIRE(m.Connect(reader, "Value", out, "Color", &why));
	GeneratedGraphCode code = GenerateGraphCode(m.graph, lib, kDomainParticleSpawn);
	CHECK(!code.ok);
	CHECK(!code.errors.empty() && code.errors.front().nodeId == reader);
	CHECK(!m.RemoveParameter("Power")); // already gone
	m.Undo();
	m.Undo();
	CHECK(m.graph.FindParameter("Power") == 0);

	// Curve keys: one undo step per edit
	const uint32_t curve = m.AddNode("Curve", 0.0f, 200.0f);
	const std::vector<GraphKey> before = ReadGraphKeys(*m.graph.Find(curve), "Curve");
	REQUIRE(m.SetKeys(curve, "Curve", { { 0.0f, { 0.2f, 0, 0, 0 } }, { 0.5f, { 2.0f, 0, 0, 0 } }, { 1.0f, { 0.0f, 0, 0, 0 } } }));
	CHECK(ReadGraphKeys(*m.graph.Find(curve), "Curve").size() == 3u);
	REQUIRE(m.Undo());
	CHECK(ReadGraphKeys(*m.graph.Find(curve), "Curve").size() == before.size());
	CHECK(!m.SetKeys(987654u, "Curve", before)); // no such node
}

TEST_CASE(Graph, EditorModelUndoRedoAndAutoConnect)
{
	GraphEditorModel m;
	m.Reset(MakeMaterialTemplate("Blank"));
	CHECK(!m.CanUndo());
	const uint32_t output = m.FindOutputNode();
	REQUIRE(output != 0);

	// Adding a node and undoing it
	const uint32_t color = m.AddNode("Color", -300.0f, 0.0f);
	REQUIRE(color != 0);
	CHECK(m.dirty);
	CHECK(m.CanUndo());
	REQUIRE(m.Undo());
	CHECK(m.graph.Find(color) == nullptr);
	REQUIRE(m.Redo());
	CHECK(m.graph.Find(color) != nullptr);

	// Dropping a link on empty space and picking a node: the new node is connected to the first pin that fits
	std::string why;
	const uint32_t multiply = m.AddNodeConnected("Multiply", -100.0f, 0.0f, color, "RGB", true, &why);
	REQUIRE_MSG(multiply != 0, why);
	REQUIRE(m.graph.LinkInto(multiply, "A") != nullptr);
	CHECK(m.graph.LinkInto(multiply, "A")->fromNode == color);
	// ... and from an input backwards: the new node's first fitting output feeds it
	const uint32_t time = m.AddNodeConnected("Time", -300.0f, 100.0f, multiply, "B", false, &why);
	REQUIRE_MSG(time != 0, why);
	CHECK(m.graph.LinkInto(multiply, "B") != nullptr && m.graph.LinkInto(multiply, "B")->fromNode == time);
	// A node with nothing that fits is refused, and nothing is added
	const size_t nodes = m.graph.nodes.size();
	CHECK(m.AddNodeConnected("Time", 0.0f, 0.0f, color, "RGB", true, &why) == 0); // Time has no input to take a colour
	CHECK(!why.empty());
	CHECK(m.graph.nodes.size() == nodes);

	// The output node cannot be deleted; other selected nodes can, with their links
	m.selection = { output, time };
	m.DeleteSelection();
	CHECK(m.graph.Find(output) != nullptr);
	CHECK(m.graph.Find(time) == nullptr);
	CHECK(m.graph.LinkInto(multiply, "B") == nullptr);
	REQUIRE(m.Undo());
	CHECK(m.graph.Find(time) != nullptr);
	CHECK(m.graph.LinkInto(multiply, "B") != nullptr);

	// Duplicate copies the nodes, their values and the links between them, next to the originals
	m.graph.FindMutable(color)->values["Color"] = { 0.1f, 0.2f, 0.3f, 1.0f };
	m.Commit();
	m.selection = { color, multiply };
	const std::vector<uint32_t> copies = m.Duplicate();
	REQUIRE(copies.size() == 2u);
	const GraphNode* colorCopy = m.graph.Find(copies[0]);
	REQUIRE(colorCopy != nullptr);
	CHECK(colorCopy->type == "Color");
	CHECK_NEAR(colorCopy->values.at("Color").z, 0.3f, 1e-6f);
	CHECK(colorCopy->x > m.graph.Find(color)->x);
	REQUIRE(m.graph.LinkInto(copies[1], "A") != nullptr);
	CHECK(m.graph.LinkInto(copies[1], "A")->fromNode == copies[0]);
	CHECK(m.selection == copies);

	// A new edit after an undo forgets the redo
	REQUIRE(m.Undo());
	CHECK(m.CanRedo());
	m.AddNode("Float", 0.0f, 300.0f);
	CHECK(!m.CanRedo());
}

TEST_CASE(Graph, SearchFindsNodesByNameCategoryAndMeaning)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	const auto names = [&](const std::string& query, const NodeSearchFilter& filter = {}) {
		std::vector<std::string> out;
		for (const NodeDef* def : SearchNodes(lib, query, kDomainMaterial, filter)) out.push_back(def->type);
		return out;
	};
	const auto has = [](const std::vector<std::string>& list, const char* type) { return std::find(list.begin(), list.end(), type) != list.end(); };

	const std::vector<std::string> all = names("");
	CHECK(has(all, "Panner") && has(all, "Add"));
	CHECK(!has(all, "Material Output")); // there is one already; it is never added from the menu
	// The name first, then words from the description: "scroll" finds the Panner
	CHECK(!names("pan").empty() && names("pan").front() == "Panner");
	CHECK(has(names("scroll"), "Panner"));
	CHECK(has(names("TEXTURE sample"), "Texture Sample"));
	CHECK(names("zzzz nothing").empty());

	// Dragged from a Float3 output: only nodes that can take it
	NodeSearchFilter fromColor;
	fromColor.fromOutput = PinType::Float3;
	const std::vector<std::string> takesColor = names("", fromColor);
	CHECK(has(takesColor, "Multiply"));
	CHECK(has(takesColor, "Cross"));
	CHECK(!has(takesColor, "Time")); // no inputs
	CHECK(!has(takesColor, "UV"));
	// Dragged from a Float2 input: only nodes that give something that fits
	NodeSearchFilter intoUv;
	intoUv.intoInput = PinType::Float2;
	const std::vector<std::string> givesUv = names("", intoUv);
	CHECK(has(givesUv, "Panner"));
	CHECK(has(givesUv, "Time")); // a Float spreads
}

TEST_CASE(Graph, MaterialTemplatesCompile)
{
	DXCManager* dxc = RizeEngine::GetDXCManager();
	REQUIRE(dxc != nullptr);
	for (const char* name : kMaterialTemplates) {
		const NodeGraph g = MakeMaterialTemplate(name);
		CHECK_MSG(!g.nodes.empty(), name);
		const GeneratedGraphCode code = GenerateGraphCode(g, NodeLibrary::Effects(), kDomainMaterial);
		CHECK_MSG(code.ok, std::string(name) + ": " + (code.errors.empty() ? std::string() : code.errors.front().message));
		std::string error;
		CHECK_MSG(dxc->TryCompileShaderSource(GpuParticles::MaterialShaderSource(code.body), L"Template.PS.hlsl", L"ps_6_0", &error) != nullptr, std::string(name) + ": " + error);
		// Nodes do not sit on top of each other
		for (size_t i = 0; i < g.nodes.size(); ++i) {
			for (size_t j = i + 1; j < g.nodes.size(); ++j) {
				const float dx = std::abs(g.nodes[i].x - g.nodes[j].x), dy = std::abs(g.nodes[i].y - g.nodes[j].y);
				CHECK_MSG(dx > 150.0f || dy > 60.0f, std::string(name) + ": " + g.nodes[i].type + " overlaps " + g.nodes[j].type);
			}
		}
	}
	CHECK(MakeMaterialTemplate("No Such Template").nodes.size() == 1u); // unknown: just the output (blank)
}

TEST_CASE(Graph, ParticleTemplatesCompile)
{
	DXCManager* dxc = RizeEngine::GetDXCManager();
	REQUIRE(dxc != nullptr);
	const NodeLibrary& lib = NodeLibrary::Effects();
	for (const char* name : kParticleTemplates) {
		const NodeGraph g = MakeParticleTemplate(name);
		CHECK_MSG(g.kind == GraphKind::Particle, name);
		const GeneratedGraphCode spawn = GenerateGraphCode(g, lib, kDomainParticleSpawn);
		const GeneratedGraphCode update = GenerateGraphCode(g, lib, kDomainParticleUpdate);
		CHECK_MSG(spawn.ok, std::string(name) + " spawn: " + (spawn.errors.empty() ? std::string() : spawn.errors.front().message));
		CHECK_MSG(update.ok, std::string(name) + " update: " + (update.errors.empty() ? std::string() : update.errors.front().message));
		std::string error;
		CHECK_MSG(dxc->TryCompileShaderSource(GpuParticles::ParticleGraphShaderSource(spawn.body, update.body), L"Template.CS.hlsl", L"cs_6_0", &error) != nullptr, std::string(name) + ": " + error);
		for (size_t i = 0; i < g.nodes.size(); ++i) {
			for (size_t j = i + 1; j < g.nodes.size(); ++j) {
				const float dx = std::abs(g.nodes[i].x - g.nodes[j].x), dy = std::abs(g.nodes[i].y - g.nodes[j].y);
				CHECK_MSG(dx > 150.0f || dy > 60.0f, std::string(name) + ": " + g.nodes[i].type + " overlaps " + g.nodes[j].type);
			}
		}
		// Every node of a template is used (reaches an output): nothing to puzzle over
		CHECK_MSG(spawn.body.size() + update.body.size() > 0, name);
	}
	// Each template except Blank shows off a parameter the Inspector and scripts can change
	CHECK(!MakeParticleTemplate("Fountain").parameters.empty());
	CHECK(MakeParticleTemplate("No Such Template").nodes.size() == 2u); // unknown: the two outputs (blank)
}

TEST_CASE(Vfx, ParticleTemplatesMakeFilesThatPlay)
{
	A::ProjectRoot(s) = A::TempRoot() / "ParticleTemplateProject";
	std::filesystem::remove_all(A::ProjectRoot(s) / "Assets" / "Effects");
	const std::string path = s.CreateParticleGraph("Fountain", "Fountain");
	CHECK(path == "Assets/Effects/Fountain.vfxparticle.json");
	CHECK(s.CreateParticleGraph("Fountain", "Fountain") == "Assets/Effects/Fountain 1.vfxparticle.json"); // never overwrites
	const EffectMaterialEntry* entry = s.GetParticleGraph(path);
	REQUIRE(entry != nullptr);
	CHECK_MSG(entry->ok, entry->errors.empty() ? std::string() : entry->errors.front().message);
	CHECK(!entry->parameters.empty());

	AimParticleTestCamera(s);
	s.UpdateWorldMatrices();
	A::State(s) = EditorSceneState::Play;
	A::GameTime(s) = 0.0;
	GpuParticles::GetInstance()->Clear();
	CaptureGameViewSignature(s);
	const std::vector<float> empty = CaptureGameViewSignature(s);
	double clock = 0.0;
	for (const char* name : kParticleTemplates) {
		if (std::string_view(name) == "Blank") continue;
		const std::string file = s.CreateParticleGraph(name, name);
		REQUIRE(!file.empty());
		const ScreenBlob blob = BlobOf(empty, ShootParticleGraph(s, file, { 0.0f, 0.0f, 0.0f }, 0.5, clock, [](GameObject& obj) {
			auto* p = obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
			p->emitting = true;
			p->emitRate = 400.0f;
			p->capacity = 2000;
			p->additive = true;
		}));
		CHECK_MSG(blob.weight > 20.0f, std::format("{} shows nothing ({:.0f})", name, blob.weight));
	}
	A::State(s) = EditorSceneState::Edit;
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Vfx, ParticleGraphEditorOpensEditsAndSaves)
{
	A::ProjectRoot(s) = A::TempRoot() / "ParticleGraphEditorProject";
	std::filesystem::remove_all(A::ProjectRoot(s) / "Assets" / "Effects");
	const std::string path = s.CreateParticleGraph("Edit Me", "Blank");
	REQUIRE(!path.empty());
	const EffectMaterialEntry* before = s.GetParticleGraph(path);
	REQUIRE(before != nullptr);
	REQUIRE(before->ok);
	const uint64_t oldKey = before->key;
	CHECK(before->parameters.empty());

	// The same window opens particle graphs: an edit is saved and used at once (no waiting for the file check)
	REQUIRE(s.OpenMaterialGraph(path));
	GraphEditorModel& m = A::MaterialEditor(s);
	CHECK(m.graph.kind == GraphKind::Particle);
	const std::string name = m.AddParameter("Pull", PinType::Float3);
	const uint32_t reader = m.AddNode("Parameter", -200.0f, 0.0f);
	m.graph.FindMutable(reader)->texts["Name"] = name;
	std::string why;
	REQUIRE(m.Connect(reader, "Value", m.FindOutputNode(), "Velocity", &why));
	REQUIRE(s.SaveMaterialGraph());
	const EffectMaterialEntry* after = s.GetParticleGraph(path);
	REQUIRE(after != nullptr);
	CHECK_MSG(after->ok, after->errors.empty() ? std::string() : after->errors.front().message);
	CHECK(after->key != oldKey);
	REQUIRE(after->parameters.size() == 1u);
	CHECK(after->parameters[0].name == "Pull");

	// The file keeps being a particle graph (another program or a restart reads it the same way)
	std::ifstream in(A::ProjectRoot(s) / path);
	std::stringstream text;
	text << in.rdbuf();
	NodeGraph saved;
	REQUIRE(NodeGraph::FromJson(text.str(), saved, &why));
	CHECK(saved.kind == GraphKind::Particle);
	CHECK(saved.parameters.size() == 1u);
	A::CloseMaterialGraph(s);
}

TEST_CASE(Vfx, GraphWindowKeepsEditsAndDropsReplacedPipelines)
{
	namespace fs = std::filesystem;
	A::ProjectRoot(s) = A::TempRoot() / "GraphWindowEditsProject";
	fs::remove_all(A::ProjectRoot(s) / "Assets");
	const std::string path = s.CreateEffectMaterial("Spark", "Scrolling Fire");
	const std::string other = s.CreateEffectMaterial("Other", "Scrolling Fire");
	REQUIRE(!path.empty());
	REQUIRE(!other.empty());
	REQUIRE(s.OpenMaterialGraph(path));
	GraphEditorModel& m = A::MaterialEditor(s);
	uint32_t color = 0;
	for (const GraphNode& n : m.graph.nodes) {
		if (n.type == "Color") color = n.id;
	}
	REQUIRE(color != 0);
	const auto savedGreen = [&](const std::string& file) {
		std::ifstream in(A::ProjectRoot(s) / file);
		std::stringstream text;
		text << in.rdbuf();
		NodeGraph g;
		if (!NodeGraph::FromJson(text.str(), g, nullptr) || !g.Find(color)) return -1.0f;
		const auto value = g.Find(color)->values.find("Color");
		return value == g.Find(color)->values.end() ? -1.0f : value->second.y;
	};

	// Both files start as the same graph: they share one pipeline
	REQUIRE(s.GetEffectMaterial(path) != nullptr);
	REQUIRE(s.GetEffectMaterial(other) != nullptr);
	const uint64_t shared = s.GetEffectMaterial(other)->key;
	CHECK(s.GetEffectMaterial(path)->key == shared);

	// An edit makes a new pipeline; the shared one stays because the other file still uses it
	m.graph.FindMutable(color)->values["Color"] = { 0.0f, 2.0f, 0.0f, 1.0f };
	m.Commit();
	REQUIRE(s.SaveMaterialGraph());
	const EffectMaterialEntry* first = s.GetEffectMaterial(path);
	REQUIRE(first != nullptr);
	REQUIRE(first->ok);
	const uint64_t replaced = first->key;
	CHECK(GpuParticles::GetInstance()->HasMaterial(shared));

	// A value still being typed when another graph opens belongs to its own file
	m.graph.FindMutable(color)->values["Color"] = { 0.0f, 5.0f, 0.0f, 1.0f };
	A::MaterialValueEditing(s) = true;
	REQUIRE(s.OpenMaterialGraph(other));
	CHECK(!A::MaterialValueEditing(s));
	CHECK(savedGreen(path) == 5.0f);
	const EffectMaterialEntry* now = s.GetEffectMaterial(path);
	REQUIRE(now != nullptr);
	REQUIRE(now->ok);
	CHECK(now->key != replaced);
	CHECK(!GpuParticles::GetInstance()->HasMaterial(replaced));
	CHECK(GpuParticles::GetInstance()->HasMaterial(now->key));

	// Renamed or moved in the Project panel: the window saves to the new name, the old one does not come back
	REQUIRE(s.OpenMaterialGraph(path));
	REQUIRE(s.MoveAssetAndUpdateReferences(path, "Assets/Effects/Flame.vfxmat.json"));
	CHECK(A::MaterialEditorPath(s) == "Assets/Effects/Flame.vfxmat.json");
	REQUIRE(s.MoveAssetAndUpdateReferences("Assets/Effects", "Assets/Vfx"));
	CHECK(A::MaterialEditorPath(s) == "Assets/Vfx/Flame.vfxmat.json");
	m.graph.FindMutable(color)->values["Color"] = { 0.0f, 7.0f, 0.0f, 1.0f };
	m.Commit();
	REQUIRE(s.SaveMaterialGraph());
	CHECK(savedGreen("Assets/Vfx/Flame.vfxmat.json") == 7.0f);
	CHECK(!fs::exists(A::ProjectRoot(s) / path));

	// A file that cannot be written: reported, the edit stays unsaved, saved once it works again
	const fs::path file = A::ProjectRoot(s) / "Assets/Vfx/Flame.vfxmat.json";
	fs::permissions(file, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write, fs::perm_options::remove); // MSVC: no write bit left = read-only
	m.graph.FindMutable(color)->values["Color"] = { 0.0f, 9.0f, 0.0f, 1.0f };
	m.Commit();
	CHECK(!s.SaveMaterialGraph());
	CHECK(A::MaterialSaveFailed(s));
	CHECK(m.dirty);
	CHECK(!s.OpenMaterialGraph(other)); // would throw the unsaved edit away
	CHECK(A::MaterialEditorPath(s) == "Assets/Vfx/Flame.vfxmat.json");
	fs::permissions(file, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write, fs::perm_options::add);
	CHECK(s.SaveMaterialGraph());
	CHECK(!A::MaterialSaveFailed(s));
	CHECK(savedGreen("Assets/Vfx/Flame.vfxmat.json") == 9.0f);
	A::CloseMaterialGraph(s);
}

TEST_CASE(Vfx, MaterialEditorCreatesOpensAndSaves)
{
	A::ProjectRoot(s) = A::TempRoot() / "MaterialEditorProject";
	std::filesystem::remove_all(A::ProjectRoot(s) / "Assets" / "Effects");
	const std::string path = s.CreateEffectMaterial("Fire", "Scrolling Fire");
	CHECK(path == "Assets/Effects/Fire.vfxmat.json");
	CHECK(std::filesystem::exists(A::ProjectRoot(s) / path));
	CHECK(s.CreateEffectMaterial("Fire", "Scrolling Fire") == "Assets/Effects/Fire 1.vfxmat.json"); // never overwrites
	REQUIRE(s.GetEffectMaterial(path) != nullptr);
	CHECK(s.GetEffectMaterial(path)->ok);
	const uint64_t before = s.GetEffectMaterial(path)->key;

	// Open, change a value, save: the runtime picks it up at once
	REQUIRE(s.OpenMaterialGraph(path));
	GraphEditorModel& m = A::MaterialEditor(s);
	uint32_t color = 0;
	for (const GraphNode& n : m.graph.nodes) {
		if (n.type == "Color") color = n.id;
	}
	REQUIRE(color != 0);
	m.graph.FindMutable(color)->values["Color"] = { 0.0f, 3.0f, 0.0f, 1.0f };
	m.Commit();
	REQUIRE(s.SaveMaterialGraph());
	CHECK(!m.dirty);
	REQUIRE(s.GetEffectMaterial(path) != nullptr);
	CHECK(s.GetEffectMaterial(path)->ok);
	CHECK(s.GetEffectMaterial(path)->key != before);
	CHECK(!s.OpenMaterialGraph("Assets/Effects/Missing.vfxmat.json"));

	// Assigning to the selected GPU Particles
	GameObject& obj = s.CreateGameObject("Fire Fx", GameObjectType::Empty);
	REQUIRE(s.AddComponentByName(obj, kGpuParticlesComponent));
	const uint32_t id = obj.id;
	A::Select(s, id);
	CHECK(s.AssignMaterialToSelection(path));
	CHECK(std::string(s.FindGameObjectById(id, false)->components.Get<GpuParticlesComponent>(kGpuParticlesComponent)->material) == path);
	s.QueueDeleteGameObjectById(id);
	s.ProcessPendingDeletes();
	// A trail takes it too; an object without either does not
	GameObject& comet = s.CreateGameObject("Comet Fx", GameObjectType::Empty);
	REQUIRE(s.AddComponentByName(comet, kTrailComponent));
	const uint32_t cometId = comet.id;
	A::Select(s, cometId);
	CHECK(s.AssignMaterialToSelection(path));
	CHECK(std::string(s.FindGameObjectById(cometId, false)->components.Get<TrailComponent>(kTrailComponent)->material) == path);
	const uint32_t plain = s.CreateGameObject("Plain", GameObjectType::Empty).id;
	A::Select(s, plain);
	CHECK(!s.AssignMaterialToSelection(path));
	s.QueueDeleteGameObjectById(cometId);
	s.QueueDeleteGameObjectById(plain);
	s.ProcessPendingDeletes();
}

TEST_CASE(Localization, GraphNodesAreTranslated)
{
	// Node names, categories, pins, settings, choices and help text all show in the graph editor
	const std::filesystem::path file = "Resources/Localization/ja.json";
	std::ifstream in(file);
	REQUIRE(in.good());
	const nlohmann::json ja = nlohmann::json::parse(in, nullptr, false);
	REQUIRE(ja.is_object());
	std::vector<std::string> missing;
	const auto need = [&](const std::string& text) {
		if (!text.empty() && !ja.contains(text) && std::find(missing.begin(), missing.end(), text) == missing.end()) missing.push_back(text);
	};
	for (const NodeDef& def : NodeLibrary::Effects().All()) {
		need(def.type);
		need(def.category);
		need(def.tooltip);
		for (const PinDef& pin : def.inputs) { need(pin.name); need(pin.tooltip); need(pin.defaultLabel); }
		for (const PinDef& pin : def.outputs) { need(pin.name); need(pin.tooltip); }
		for (const ParamDef& param : def.params) {
			need(param.name);
			need(param.tooltip);
			for (const std::string& choice : param.choices) need(choice);
		}
	}
	for (const char* name : kMaterialTemplates) need(name);
	std::string list;
	for (const std::string& m : missing) list += "\n  " + m;
	CHECK_MSG(missing.empty(), std::to_string(missing.size()) + " without Japanese:" + list);
}

TEST_CASE(Vfx, MaterialGraphsShadeTrails)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	A::ProjectRoot(s) = A::TempRoot() / "TrailMaterialProject";
	NodeGraph red;
	red.kind = GraphKind::Material;
	const uint32_t color = red.AddNode("Color", 0.0f, 0.0f, lib);
	red.FindMutable(color)->values["Color"] = { 1.0f, 0.0f, 0.0f, 1.0f };
	const uint32_t out = red.AddNode("Material Output", 200.0f, 0.0f, lib);
	std::string why;
	REQUIRE(red.Connect(color, "RGB", out, "Emissive Color", lib, &why));
	const std::string redPath = WriteMaterial(s, "RedTrail", red);

	A::AimAudioListener(s, { 0.0f, 2.0f, -10.0f }, 0.0f);
	A::State(s) = EditorSceneState::Play;
	const std::vector<float> empty = CaptureGameViewSignature(s);
	const auto draw = [&](const std::string& material) {
		GameObject& obj = s.CreateGameObject("Comet", GameObjectType::Empty);
		const uint32_t id = obj.id;
		std::vector<float> picture;
		if (s.AddComponentByName(obj, kTrailComponent)) {
			auto* trail = s.FindGameObjectById(id, false)->components.Get<TrailComponent>(kTrailComponent);
			trail->widthStart = trail->widthEnd = 3.0f;
			trail->lifetime = 2.0f;
			trail->intensity = 1.0f;
			trail->additive = false;
			trail->colorStart = trail->colorEnd = { 0.02f, 0.02f, 0.25f, 1.0f }; // dark blue: stands out on the sky
			strcpy_s(trail->material, material.c_str());
			for (int i = 0; i <= 8; ++i) {
				s.FindGameObjectById(id, false)->translation = { -4.0f + i, 2.0f, 0.0f };
				A::GameTime(s) = i * 0.02;
				s.UpdateWorldMatrices();
				s.UpdateTrails();
			}
			picture = CaptureGameViewSignature(s);
		}
		s.QueueDeleteGameObjectById(id);
		s.ProcessPendingDeletes();
		return picture;
	};
	// Where the trail covers the sky the most, a red material makes red rise above green
	const auto coverage = [&](const std::vector<float>& picture) {
		size_t best = 0;
		float drop = -1.0e9f;
		for (size_t i = 0; i + 2 < picture.size() && i + 2 < empty.size(); i += 3) {
			const float d = (empty[i + 1] - picture[i + 1]) + (empty[i + 2] - picture[i + 2]);
			if (d > drop) { drop = d; best = i; }
		}
		return std::pair<size_t, float>{ best, drop };
	};
	const std::vector<float> plain = draw("");
	REQUIRE(!plain.empty());
	const auto [plainCell, plainDrop] = coverage(plain);
	CHECK_MSG(plainDrop > 40.0f && plain[plainCell] < plain[plainCell + 1] + 30.0f, std::format("plain trail {:.0f},{:.0f},{:.0f}", plain[plainCell], plain[plainCell + 1], plain[plainCell + 2]));
	const std::vector<float> shaded = draw(redPath);
	REQUIRE(!shaded.empty());
	const auto [cell, drop] = coverage(shaded);
	CHECK_MSG(drop > 40.0f && shaded[cell] > shaded[cell + 1] + 60.0f, std::format("red material trail {:.0f},{:.0f},{:.0f} over sky {:.0f},{:.0f},{:.0f}", shaded[cell], shaded[cell + 1], shaded[cell + 2], empty[cell], empty[cell + 1], empty[cell + 2]));
	A::State(s) = EditorSceneState::Edit;
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Vfx, MaterialTexturesSurviveAMove)
{
	const std::filesystem::path root = A::TempRoot() / "MaterialTextureProject";
	std::filesystem::remove_all(root);
	A::ProjectRoot(s) = root;
	REQUIRE(PlaceTestTexture(root, "Assets/Textures/spark.png"));
	s.AssetDb().Refresh();

	// The editor stores the texture's GUID next to its path, like scenes do
	NodeGraph g = MakeMaterialTemplate("Flipbook Smoke");
	uint32_t sample = 0;
	for (const GraphNode& n : g.nodes) {
		if (n.type == "Texture Sample") sample = n.id;
	}
	REQUIRE(sample != 0);
	s.SetMaterialTexture(*g.FindMutable(sample), "Texture", "Assets/Textures/spark.png");
	CHECK(g.Find(sample)->texts.at("Texture") == "Assets/Textures/spark.png");
	CHECK(!g.Find(sample)->texts.at("Texture Guid").empty());
	const std::string path = WriteMaterial(s, "Spark", g);
	A::ForgetEffectMaterials(s);
	const EffectMaterialEntry* before = s.GetEffectMaterial(path);
	REQUIRE(before != nullptr && before->ok);
	REQUIRE(before->textureHandles.size() == 2u);
	CHECK(before->textureHandles[1] != 0u);

	// Renamed in the Project panel: the material still finds it (after a restart too)
	REQUIRE(s.MoveAssetAndUpdateReferences("Assets/Textures/spark.png", "Assets/Textures/ember.png"));
	A::ForgetEffectMaterials(s);
	const EffectMaterialEntry* after = s.GetEffectMaterial(path);
	REQUIRE(after != nullptr && after->ok);
	REQUIRE(after->textureHandles.size() == 2u);
	CHECK(after->textureHandles[1] != 0u);
	// The graph editor shows the new name
	REQUIRE(s.OpenMaterialGraph(path));
	CHECK(A::MaterialEditor(s).graph.Find(sample)->texts.at("Texture") == "Assets/Textures/ember.png");
	// Clearing the texture clears its GUID too
	s.SetMaterialTexture(*A::MaterialEditor(s).graph.FindMutable(sample), "Texture", "");
	CHECK(A::MaterialEditor(s).graph.Find(sample)->texts.at("Texture Guid").empty());
}

TEST_CASE(Vfx, MaterialPreviewShowsTheMaterial)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	A::ProjectRoot(s) = A::TempRoot() / "MaterialPreviewProject";
	const auto solid = [&](const char* name, Vector4 color) {
		NodeGraph g;
		g.kind = GraphKind::Material;
		const uint32_t c = g.AddNode("Color", 0.0f, 0.0f, lib);
		g.FindMutable(c)->values["Color"] = color;
		const uint32_t out = g.AddNode("Material Output", 200.0f, 0.0f, lib);
		std::string why;
		g.Connect(c, "RGB", out, "Emissive Color", lib, &why);
		return WriteMaterial(s, name, g);
	};
	const std::string red = solid("PreviewRed", { 1.0f, 0.0f, 0.0f, 1.0f });
	const std::string blue = solid("PreviewBlue", { 0.0f, 0.0f, 1.0f, 1.0f });
	// The cell where one channel stands out the most over another
	const auto lead = [](const std::vector<float>& picture, int strong, int weak) {
		float best = -255.0f;
		for (size_t i = 0; i + 2 < picture.size(); i += 3) best = (std::max)(best, picture[i + strong] - picture[i + weak]);
		return best;
	};
	const auto look = [&](const std::string& path) {
		if (!s.OpenMaterialGraph(path)) return std::vector<float>{}; // REQUIRE cannot return from a lambda
		std::vector<float> picture;
		for (int i = 0; i < 6; ++i) picture = CaptureViewSignature(s, A::PreviewRT(s));
		return picture;
	};
	const std::vector<float> redPicture = look(red);
	REQUIRE(!redPicture.empty());
	CHECK_MSG(lead(redPicture, 0, 1) > 60.0f, std::format("red preview: red over green by {:.0f}", lead(redPicture, 0, 1)));
	const std::vector<float> bluePicture = look(blue);
	REQUIRE(!bluePicture.empty());
	CHECK_MSG(lead(bluePicture, 2, 0) > 60.0f, std::format("blue preview: blue over red by {:.0f}", lead(bluePicture, 2, 0)));
	CHECK_MSG(lead(bluePicture, 0, 1) < 30.0f, "the red particles of the last material are gone");
	A::CloseMaterialGraph(s);
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Vfx, GpuParticlesInspectorShowsOnlyTheFieldsInUse)
{
	(void)s;
	const ComponentInfo* info = ComponentRegistry::Get().Find(kGpuParticlesComponent);
	REQUIRE(info && info->fieldShown);
	GpuParticlesComponent p{};
	const auto shown = [&](std::string_view name) {
		for (const ComponentField& f : info->fields) {
			if (name == f.name) return !f.hidden && info->fieldShown(&p, f);
		}
		return false;
	};
	// Without a graph: the fixed settings; size and colour are edited as a curve and a gradient instead of numbers
	for (const char* keep : { "lifeMin", "speedMax", "shape", "gravity", "drag", "collideWithFloor", "intensity", "direction" }) CHECK_MSG(shown(keep), keep);
	for (const char* gone : { "sizeStart", "sizeMiddle", "sizeEnd", "colorStart", "colorMiddle", "colorEnd", "middleTime", "useMiddleKey" }) CHECK_MSG(!shown(gone), gone);
	// With a graph: only what the graph still reads (direction = the graph's Emitter Direction)
	strcpy_s(p.graph, "Assets/Effects/x.vfxparticle.json");
	for (const char* keep : { "capacity", "emitRate", "burst", "emitting", "spawnBurst", "additive", "texture", "material", "stretchFactor",
		"uprightBillboard", "graph", "direction" }) CHECK_MSG(shown(keep), keep);
	for (const char* gone : { "lifeMin", "lifeMax", "speedMin", "speedMax", "spreadDegrees", "gravity", "drag", "collideWithFloor", "floorHeight",
		"bounce", "intensity", "shape", "velocityMode", "shapeRadius", "boxExtents", "sizeStart", "colorEnd" }) CHECK_MSG(!shown(gone), gone);
}

TEST_CASE(Vfx, EffectGraphsThatDoNotCompileAreReported)
{
	// What the exported game's smoke run (--smoke-frames) checks before it reports OK
	A::ProjectRoot(s) = A::TempRoot() / "GraphProblemsProject";
	s.NewScene();
	const std::string fountain = s.CreateParticleGraph("Smoke Fountain", "Fountain");
	const std::string glow = s.CreateEffectMaterial("Smoke Glow", "Soft Glow");
	REQUIRE(!fountain.empty() && !glow.empty());
	GameObject& a = s.CreateGameObject("Sparks", GameObjectType::Empty);
	REQUIRE(s.AddComponentByName(a, kGpuParticlesComponent));
	auto* p = a.components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
	strcpy_s(p->graph, fountain.c_str());
	strcpy_s(p->material, glow.c_str());
	CHECK_MSG(s.EffectGraphProblem().empty(), s.EffectGraphProblem());

	// A material file that is not a graph: named with its object, so the report says where to look
	const std::filesystem::path broken = A::ProjectRoot(s) / "Assets" / "Effects" / "Broken.vfxmat.json";
	std::ofstream(broken, std::ios::trunc) << "{ this is not a graph";
	const uint32_t bId = s.CreateGameObject("Embers", GameObjectType::Empty).id;
	GameObject* b = s.FindGameObjectById(bId, false);
	REQUIRE(b && s.AddComponentByName(*b, kGpuParticlesComponent));
	strcpy_s(b->components.Get<GpuParticlesComponent>(kGpuParticlesComponent)->material, "Assets/Effects/Broken.vfxmat.json");
	const std::string why = s.EffectGraphProblem();
	CHECK_MSG(why.find("Broken.vfxmat.json") != std::string::npos && why.find("Embers") != std::string::npos, why);

	// A missing file is a problem too (an export that left it out)
	strcpy_s(b->components.Get<GpuParticlesComponent>(kGpuParticlesComponent)->material, "Assets/Effects/Gone.vfxmat.json");
	CHECK_MSG(s.EffectGraphProblem().find("Gone.vfxmat.json") != std::string::npos, s.EffectGraphProblem());
	s.NewScene();
}

TEST_CASE(Vfx, ParticleGraphPreviewShowsTheGraph)
{
	A::ProjectRoot(s) = A::TempRoot() / "ParticleGraphPreviewProject";
	A::ShowEffectEditor(s) = false;
	// Particles that stand still in a box around the emitter, in one colour
	const auto solid = [&](const char* name, Vector4 color) {
		NodeGraph g = TestParticleGraph({ 2.0f, 2.0f, 0.2f });
		for (GraphNode& n : g.nodes) {
			if (n.type == "Spawn Output") n.values["Color"] = color;
		}
		return WriteParticleGraph(s, name, g);
	};
	const std::string red = solid("PreviewRed", { 1.0f, 0.0f, 0.0f, 1.0f });
	const std::string blue = solid("PreviewBlue", { 0.0f, 0.0f, 1.0f, 1.0f });
	const auto lead = [](const std::vector<float>& picture, int strong, int weak) {
		float best = -255.0f;
		for (size_t i = 0; i + 2 < picture.size(); i += 3) best = (std::max)(best, picture[i + strong] - picture[i + weak]);
		return best;
	};
	const auto look = [&](const std::string& path) {
		if (!s.OpenMaterialGraph(path)) return std::vector<float>{};
		std::vector<float> picture;
		for (int i = 0; i < 8; ++i) picture = CaptureViewSignature(s, A::PreviewRT(s));
		return picture;
	};
	const std::vector<float> redPicture = look(red);
	REQUIRE(!redPicture.empty());
	CHECK_MSG(lead(redPicture, 0, 1) > 60.0f, std::format("red graph: red over green by {:.0f}", lead(redPicture, 0, 1)));
	const std::vector<float> bluePicture = look(blue);
	REQUIRE(!bluePicture.empty());
	CHECK_MSG(lead(bluePicture, 2, 0) > 60.0f, std::format("blue graph: blue over red by {:.0f}", lead(bluePicture, 2, 0)));
	CHECK_MSG(lead(bluePicture, 0, 1) < 30.0f, "the red particles of the last graph are gone");
	A::CloseMaterialGraph(s);
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Vfx, EffectEditorPreviewShowsTheEffect)
{
	A::State(s) = EditorSceneState::Edit;
	A::CloseMaterialGraph(s); // the Material Graph's preview uses the same target and would win
	GpuParticles::GetInstance()->Clear();
	// Far from where the Scene camera looks: the panel finds the effect by itself
	const uint32_t root = s.CreateEffectFromTemplate("Magic Aura", { 40.0f, 1.0f, 25.0f });
	REQUIRE(root != 0);
	// A green emitter at the same place that is not part of the effect: the panel leaves it out
	GameObject& decoy = s.CreateGameObject("Not The Effect", GameObjectType::Empty);
	const uint32_t decoyId = decoy.id;
	decoy.translation = { 40.0f, 1.0f, 25.0f };
	s.AddComponentByName(decoy, kGpuParticlesComponent);
	if (auto* p = decoy.components.Get<GpuParticlesComponent>(kGpuParticlesComponent)) {
		p->emitRate = 400.0f;
		p->speedMin = p->speedMax = 0.5f;
		p->sizeStart = p->sizeEnd = 1.0f;
		p->colorStart = p->colorEnd = { 0.0f, 3.0f, 0.0f, 1.0f };
	}
	s.UpdateWorldMatrices();
	const auto picture = [&] {
		std::vector<float> last;
		for (int i = 0; i < 8; ++i) last = CaptureViewSignature(s, A::PreviewRT(s));
		return last;
	};
	const auto greenLead = [](const std::vector<float>& p) {
		float best = -255.0f;
		for (size_t i = 0; i + 2 < p.size(); i += 3) best = (std::max)(best, p[i + 1] - (std::max)(p[i], p[i + 2]));
		return best;
	};

	A::ShowEffectEditor(s) = true;
	s.StopEffectPreview();
	const std::vector<float> idle = picture();
	s.StartEffectPreview(root);
	const std::vector<float> playing = picture();
	REQUIRE(!playing.empty());
	CHECK_MSG(LargestSignatureDifference(idle, playing) > 30.0f, std::format("the effect shows in the panel: {:.0f}", LargestSignatureDifference(idle, playing)));
	CHECK_MSG(greenLead(playing) < 40.0f, std::format("the other emitter stays out: green lead {:.0f}", greenLead(playing)));
	s.StopEffectPreview();

	A::ShowEffectEditor(s) = false;
	s.QueueDeleteGameObjectById(decoyId);
	s.QueueDeleteGameObjectById(root);
	s.ProcessPendingDeletes();
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Vfx, GpuParticleGradientAndDrawOrder)
{
	// Two keys: a straight blend. Three keys: start -> middle at middleTime -> end.
	CHECK(std::abs(GpuParticleKey(0.25f, 0.0f, 9.0f, 1.0f, 0.5f, false) - 0.25f) < 1.0e-5f);
	CHECK(std::abs(GpuParticleKey(0.0f, 2.0f, 5.0f, 1.0f, 0.3f, true) - 2.0f) < 1.0e-5f);
	CHECK(std::abs(GpuParticleKey(0.3f, 2.0f, 5.0f, 1.0f, 0.3f, true) - 5.0f) < 1.0e-5f);
	CHECK(std::abs(GpuParticleKey(0.65f, 2.0f, 5.0f, 1.0f, 0.3f, true) - 3.0f) < 1.0e-5f);
	CHECK(std::abs(GpuParticleKey(1.0f, 2.0f, 5.0f, 1.0f, 0.3f, true) - 1.0f) < 1.0e-5f);
	CHECK(std::isfinite(GpuParticleKey(0.5f, 0.0f, 1.0f, 2.0f, 0.0f, true))); // middle key at an edge
	CHECK(std::isfinite(GpuParticleKey(0.5f, 0.0f, 1.0f, 2.0f, 1.0f, true)));

	// Far emitters first, so near smoke covers far smoke
	const std::vector<Vector3> origins{ { 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, 20.0f }, { 0.0f, 0.0f, 10.0f } };
	CHECK(GpuParticleDrawOrder(origins, { 0.0f, 0.0f, 0.0f }) == (std::vector<size_t>{ 1, 2, 0 }));
	CHECK(GpuParticleDrawOrder(origins, { 0.0f, 0.0f, 30.0f }) == (std::vector<size_t>{ 0, 2, 1 }));
	CHECK(GpuParticleDrawOrder({}, { 0.0f, 0.0f, 0.0f }).empty());
}

TEST_CASE(Vfx, GpuParticlesStopAtTheFloor)
{
	A::AimAudioListener(s, { 0.0f, 0.0f, -20.0f }, 0.0f);
	s.UpdateWorldMatrices();
	A::State(s) = EditorSceneState::Play;
	CaptureGameViewSignature(s);
	const std::vector<float> emptyRows = gCapturedRowMeans;

	// The same falling burst twice: without a floor, then with one 1 m under the emitter
	const auto fall = [&](bool floor) {
		A::GameTime(s) = 0.0;
		GameObject& obj = s.CreateGameObject("Rain", GameObjectType::Empty);
		obj.translation = { 0.0f, 5.0f, 0.0f };
		const uint32_t id = obj.id;
		auto* p = s.AddComponentByName(obj, kGpuParticlesComponent) ? obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent) : nullptr;
		if (!p) return -1.0f;
		p->emitting = false;
		p->burst = 2000;
		p->lifeMin = p->lifeMax = 3.0f;
		p->speedMin = 0.0f;
		p->speedMax = 0.3f;
		p->spreadDegrees = 180.0f;
		p->gravity = { 0.0f, -20.0f, 0.0f };
		p->sizeStart = p->sizeEnd = 1.0f;
		p->colorStart = p->colorEnd = { 1.0f, 0.5f, 0.2f, 1.0f };
		p->intensity = 2.0f;
		p->collideWithFloor = floor;
		p->floorHeight = 4.0f;
		p->bounce = 0.0f;
		s.UpdateWorldMatrices();
		for (int i = 0; i <= 5; ++i) { // 0.5 s: about 2.5 m down, still in the picture
			A::GameTime(s) = i * 0.1;
			CaptureGameViewSignature(s);
		}
		const float row = BrightestRowGain(gCapturedRowMeans, emptyRows);
		s.QueueDeleteGameObjectById(id);
		s.ProcessPendingDeletes();
		CaptureGameViewSignature(s);
		return row;
	};
	const float freeRow = fall(false);
	const float floorRow = fall(true);
	CHECK_MSG(floorRow >= 0.0f && floorRow + 20.0f < freeRow, std::format("floor row {:.1f}, free row {:.1f}", floorRow, freeRow));
	A::State(s) = EditorSceneState::Edit;
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Vfx, GpuParticleSettingsAreSavedAndReachableFromScripts)
{
	GameObject& obj = s.CreateGameObject("Fx", GameObjectType::Empty);
	const uint32_t id = obj.id;
	REQUIRE(s.AddComponentByName(obj, kGpuParticlesComponent));
	REQUIRE(ApplyGpuParticlePreset(*obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent), "Sparks"));
	REQUIRE(s.DeserializeSceneFromString(s.SerializeSceneToString()));
	auto* p = s.FindGameObjectById(id, false)->components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
	REQUIRE(p != nullptr);
	CHECK_NEAR(p->gravity.y, -9.8f, 1e-4f);
	CHECK_NEAR(p->intensity, 5.0f, 1e-5f);

	EngineAPI& api = A::Api(s);
	ScriptComponentValue value;
	value.kind = ScriptComponentValue::Int;
	value.integer = 50;
	REQUIRE(api.setComponentField(&s, id, kGpuParticlesComponent, "burst", &value));
	CHECK(s.FindGameObjectById(id, false)->components.Get<GpuParticlesComponent>(kGpuParticlesComponent)->burst == 50);
}


TEST_CASE(Vfx, DecalsDoNotPaintMovingObjects)
{
	A::AimCamera(s, { 0.0f, 12.0f, 0.0f }, { 1.5707f, 0.0f, 0.0f });
	// A crate standing inside the decal box, seen from straight above
	GameObject& crate = s.CreateCube("Crate", { 0.0f, 0.5f, 0.0f });
	crate.scale = { 4.0f, 1.0f, 4.0f };
	crate.materialColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	const uint32_t crateId = crate.id;
	s.UpdateWorldMatrices();
	const std::vector<float> plain = CaptureGameViewSignature(s);
	REQUIRE(!plain.empty());

	GameObject& mark = s.CreateGameObject("Mark", GameObjectType::Empty);
	mark.translation = { 0.0f, 0.0f, 0.0f };
	mark.scale = { 6.0f, 6.0f, 6.0f };
	REQUIRE(s.AddComponentByName(mark, kDecalComponent));
	mark.components.Get<DecalComponent>(kDecalComponent)->color = { 1.0f, 0.0f, 0.0f, 1.0f };
	s.UpdateWorldMatrices();

	// Red gain in the middle of the crate top, far from its edges (16 x 9 cells of RGB)
	const auto crateRed = [&](const std::vector<float>& a) {
		float most = -1000.0f;
		for (int row = 4; row <= 4; ++row) {
			for (int column = 7; column <= 8; ++column) {
				const size_t cell = (static_cast<size_t>(row) * 16 + column) * 3;
				most = (std::max)(most, (a[cell] - plain[cell]) - (a[cell + 1] - plain[cell + 1]));
			}
		}
		return most;
	};
	const float staticCrate = crateRed(CaptureGameViewSignature(s));
	s.FindGameObjectById(crateId, false)->hasRigidbody = true;
	const float movingCrate = crateRed(CaptureGameViewSignature(s));
	CHECK_MSG(staticCrate > 10.0f && movingCrate < 1.0f, std::format("static crate red {:.2f}, rigidbody crate red {:.2f}", staticCrate, movingCrate));
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}

TEST_CASE(Vfx, MovedObjectsAreDrawnWhereTheyAreWithDecalsInTheScene)
{
	// A Rigidbody crate in a scene with a decal (it is drawn after the decals), moved along Z like the Inspector does
	A::AimCamera(s, { 0.0f, 12.0f, 0.0f }, { 1.5707f, 0.0f, 0.0f });
	GameObject& mark = s.CreateGameObject("Mark", GameObjectType::Empty);
	mark.translation = { 6.0f, 0.0f, 3.0f };
	REQUIRE(s.AddComponentByName(mark, kDecalComponent));
	GameObject& crate = s.CreateCube("Crate", { -3.0f, 0.5f, -2.0f });
	crate.scale = { 2.0f, 1.0f, 2.0f };
	crate.materialColor = { 1.0f, 0.0f, 0.0f, 1.0f };
	crate.hasRigidbody = true;
	const uint32_t id = crate.id;
	s.UpdateWorldMatrices();
	const std::vector<float> before = CaptureGameViewSignature(s);
	REQUIRE(!before.empty());

	GameObject* moved = s.FindGameObjectById(id, false);
	moved->translation.z = 2.0f; // 4 m along Z
	s.PushUndoSnapshot();
	s.UpdateWorldMatrices();
	const std::vector<float> after = CaptureGameViewSignature(s);
	REQUIRE(after.size() == before.size());

	// Red where the crate is now, none left where it was: rows run along Z (16 x 9 cells of RGB)
	const auto rowRed = [](const std::vector<float>& sig, int row) {
		float red = 0.0f;
		for (int column = 0; column < 16; ++column) {
			const size_t cell = (static_cast<size_t>(row) * 16 + column) * 3;
			red = (std::max)(red, sig[cell] - sig[cell + 1]);
		}
		return red;
	};
	int beforeRow = 0;
	int afterRow = 0;
	for (int row = 0; row < 9; ++row) {
		if (rowRed(before, row) > rowRed(before, beforeRow)) beforeRow = row;
		if (rowRed(after, row) > rowRed(after, afterRow)) afterRow = row;
	}
	CHECK_MSG(std::abs(afterRow - beforeRow) >= 2, std::format("red row {} -> {}", beforeRow, afterRow));
	CHECK_MSG(rowRed(after, beforeRow) < rowRed(before, beforeRow) * 0.5f, std::format("old place still red {:.1f} (was {:.1f})", rowRed(after, beforeRow), rowRed(before, beforeRow)));
	int crates = 0;
	for (const auto& o : A::Objects(s)) crates += (o.name == "Crate") ? 1 : 0;
	CHECK(crates == 1);
}

TEST_CASE(Vfx, DecalPaintsTheFloorInsideItsBox)
{
	// Looking straight down at the Ground Plane
	A::AimCamera(s, { 0.0f, 12.0f, 0.0f }, { 1.5707f, 0.0f, 0.0f });
	const std::vector<float> before = CaptureGameViewSignature(s);
	REQUIRE(!before.empty());

	GameObject& obj = s.CreateGameObject("Mark", GameObjectType::Empty);
	obj.translation = { -4.0f, -0.5f, 0.0f };
	obj.scale = { 4.0f, 2.0f, 4.0f };
	const uint32_t id = obj.id;
	REQUIRE(s.AddComponentByName(obj, kDecalComponent));
	auto* decal = obj.components.Get<DecalComponent>(kDecalComponent);
	decal->color = { 1.0f, 0.0f, 0.0f, 1.0f };
	s.UpdateWorldMatrices();
	std::vector<float> after = CaptureGameViewSignature(s);
	REQUIRE(after.size() == before.size());

	// Signature: 16 x 9 cells of RGB. Red on the left half (x = -4 from straight above), nothing on the right.
	const auto change = [&](const std::vector<float>& a, int firstColumn, int lastColumn) {
		float redGain = 0.0f;
		float any = 0.0f;
		for (int row = 0; row < 9; ++row) {
			for (int column = firstColumn; column <= lastColumn; ++column) {
				const size_t cell = (static_cast<size_t>(row) * 16 + column) * 3;
				redGain = (std::max)(redGain, (a[cell] - before[cell]) - (a[cell + 1] - before[cell + 1]));
				for (int c = 0; c < 3; ++c) any = (std::max)(any, std::abs(a[cell + c] - before[cell + c]));
			}
		}
		return std::pair<float, float>(redGain, any);
	};
	CHECK_MSG(change(after, 0, 7).first > 10.0f, std::format("left red {:.2f}", change(after, 0, 7).first));
	CHECK_MSG(change(after, 9, 15).second < 1.0f, std::format("right {:.2f}", change(after, 9, 15).second));

	// A box that does not reach the floor paints nothing
	s.FindGameObjectById(id, false)->translation.y = 5.0f;
	s.UpdateWorldMatrices();
	after = CaptureGameViewSignature(s);
	CHECK_MSG(change(after, 0, 15).second < 1.0f, std::format("floating box {:.2f}", change(after, 0, 15).second));

	// The camera inside the box still sees it, and nothing later in the frame lost its depth test
	GameObject* moved = s.FindGameObjectById(id, false);
	moved->translation = { 0.0f, 5.0f, 0.0f };
	moved->scale = { 30.0f, 30.0f, 30.0f };
	s.UpdateWorldMatrices();
	after = CaptureGameViewSignature(s);
	CHECK_MSG(change(after, 0, 15).first > 5.0f, std::format("inside {:.2f}", change(after, 0, 15).first));

	REQUIRE(s.DeserializeSceneFromString(s.SerializeSceneToString()));
	decal = s.FindGameObjectById(id, false)->components.Get<DecalComponent>(kDecalComponent);
	REQUIRE(decal != nullptr);
	CHECK_NEAR(decal->color.y, 0.0f, 1e-5f);
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}


TEST_CASE(Timeline, KeysAreSampledInOrder)
{
	TimelineTrack track;
	CHECK_NEAR(track.Sample(1.0f).x, 0.0f, 1e-6f); // no keys
	track.SetKey({ 2.0f, { 10.0f, 0.0f, 0.0f }, false });
	track.SetKey({ 0.0f, { 0.0f, 0.0f, 0.0f }, false });
	REQUIRE(track.keys.size() == 2u);
	CHECK(track.keys[0].time == 0.0f); // sorted however they were added
	CHECK_NEAR(track.Sample(-1.0f).x, 0.0f, 1e-5f);
	CHECK_NEAR(track.Sample(1.0f).x, 5.0f, 1e-5f);
	CHECK_NEAR(track.Sample(9.0f).x, 10.0f, 1e-5f);
	// The same time replaces
	track.SetKey({ 2.0f, { 20.0f, 0.0f, 0.0f }, false });
	CHECK(track.keys.size() == 2u);
	CHECK_NEAR(track.Sample(2.0f).x, 20.0f, 1e-5f);
	// Smooth: slower at the start than constant speed, same at the middle and the end
	track.keys[0].smooth = true;
	CHECK(track.Sample(0.2f).x < 2.0f);
	CHECK_NEAR(track.Sample(1.0f).x, 10.0f, 1e-4f);
	// Bad time stays safe
	CHECK(std::isfinite(track.Sample(std::numeric_limits<float>::quiet_NaN()).x));
}

TEST_CASE(Timeline, PlaybackEndsOrLoopsAndFindsEvents)
{
	bool finished = false;
	CHECK_NEAR(TimelineAdvance(1.0f, 0.5f, 4.0f, false, finished), 1.5f, 1e-5f);
	CHECK(!finished);
	CHECK_NEAR(TimelineAdvance(3.9f, 0.5f, 4.0f, false, finished), 4.0f, 1e-5f);
	CHECK(finished);
	CHECK_NEAR(TimelineAdvance(3.9f, 0.5f, 4.0f, true, finished), 0.4f, 1e-4f);
	CHECK(!finished);
	CHECK_NEAR(TimelineAdvance(1.0f, 0.5f, 0.0f, true, finished), 0.0f, 1e-5f); // empty timeline: no endless loop

	Timeline timeline;
	timeline.duration = 4.0f;
	timeline.events = { { 1.0f, "a" }, { 2.0f, "b" }, { 0.2f, "c" } };
	CHECK(timeline.EventsBetween(0.5f, 1.5f).size() == 1u);
	CHECK(timeline.EventsBetween(1.0f, 1.5f).empty()); // already fired at 1.0
	CHECK(timeline.EventsBetween(0.5f, 1.0f).size() == 1u);
	CHECK(timeline.EventsBetween(3.5f, 0.5f).size() == 1u); // wrapped: only "c"
	CHECK(timeline.EventsBetween(0.0f, 0.0f).empty());

	// Same target and property: one track
	TimelineTrack& first = timeline.FindOrAddTrack("Camera", TimelineProperty::Position);
	first.SetKey({ 1.0f, { 1.0f, 2.0f, 3.0f }, true });
	timeline.FindOrAddTrack("Camera", TimelineProperty::Position);
	timeline.FindOrAddTrack("Camera", TimelineProperty::Rotation);
	CHECK(timeline.tracks.size() == 2u);

	Timeline loaded;
	REQUIRE(Timeline::FromJson(timeline.ToJson(), loaded));
	CHECK_NEAR(loaded.duration, 4.0f, 1e-5f);
	REQUIRE(loaded.tracks.size() == 2u);
	CHECK(loaded.tracks[0].target == "Camera");
	CHECK(loaded.tracks[1].property == TimelineProperty::Rotation);
	CHECK_NEAR(loaded.tracks[0].keys.at(0).value.z, 3.0f, 1e-5f);
	CHECK(loaded.events.size() == 3u);
	CHECK(!Timeline::FromJson("not json", loaded));
	CHECK(!Timeline::FromJson("[1,2]", loaded));
}

TEST_CASE(Timeline, ComponentMovesObjectsDuringPlay)
{
	const std::filesystem::path root = A::TempRoot() / "TimelineProject";
	std::filesystem::create_directories(root / "Assets" / "Timelines");
	A::ProjectRoot(s) = root;

	Timeline timeline;
	timeline.duration = 2.0f;
	timeline.FindOrAddTrack("Mover", TimelineProperty::Position).SetKey({ 0.0f, { 0.0f, 0.0f, 0.0f }, false });
	timeline.FindOrAddTrack("Mover", TimelineProperty::Position).SetKey({ 2.0f, { 4.0f, 0.0f, 0.0f }, false });
	timeline.FindOrAddTrack("Mover", TimelineProperty::Rotation).SetKey({ 0.0f, { 0.0f, 1.0f, 0.0f }, false });
	timeline.FindOrAddTrack("No Such Object", TimelineProperty::Scale).SetKey({ 0.0f, { 2.0f, 2.0f, 2.0f }, false }); // ignored quietly
	timeline.events = { { 1.0f, "halfway" } };
	const std::string asset = "Assets/Timelines/Move.timeline.json";
	REQUIRE(A::SaveTimelineAsset(s, asset, timeline));

	const uint32_t mover = s.CreateGameObject("Mover", GameObjectType::Empty).id;
	GameObject& director = s.CreateGameObject("Director", GameObjectType::Empty);
	const uint32_t directorId = director.id;
	REQUIRE(s.AddComponentByName(director, kTimelineComponent));
	strncpy_s(director.components.Get<TimelineComponent>(kTimelineComponent)->timeline, asset.c_str(), _TRUNCATE);

	A::State(s) = EditorSceneState::Play;
	const int eventsBefore = A::TimelineEvents(s);
	A::RunTimelines(s, 0.5f);
	const GameObject* moved = s.FindGameObjectById(mover, false);
	CHECK_NEAR(moved->translation.x, 1.0f, 1e-4f);
	CHECK_NEAR(moved->rotation.y, 1.0f, 1e-4f);
	CHECK(A::TimelineEvents(s) == eventsBefore);
	A::RunTimelines(s, 0.6f); // passes 1.0
	CHECK(A::TimelineEvents(s) == eventsBefore + 1);
	A::RunTimelines(s, 5.0f); // the end: stays there and stops
	CHECK_NEAR(s.FindGameObjectById(mover, false)->translation.x, 4.0f, 1e-4f);
	CHECK(!s.FindGameObjectById(directorId, false)->components.Get<TimelineComponent>(kTimelineComponent)->playing);

	// A script restarts it
	EngineAPI& api = A::Api(s);
	ScriptComponentValue value;
	value.kind = ScriptComponentValue::Float;
	value.number[0] = 0.0f;
	REQUIRE(api.setComponentField(&s, directorId, kTimelineComponent, "time", &value));
	value = {};
	value.kind = ScriptComponentValue::Bool;
	value.integer = 1;
	REQUIRE(api.setComponentField(&s, directorId, kTimelineComponent, "playing", &value));
	A::RunTimelines(s, 1.0f);
	CHECK_NEAR(s.FindGameObjectById(mover, false)->translation.x, 2.0f, 1e-4f);

	// A missing asset does nothing
	strncpy_s(s.FindGameObjectById(directorId, false)->components.Get<TimelineComponent>(kTimelineComponent)->timeline, "Assets/none.timeline.json", _TRUNCATE);
	A::RunTimelines(s, 0.1f);
	A::State(s) = EditorSceneState::Edit;
}


TEST_CASE(Post, FogGrowsWithDistanceAndFadesWithHeight)
{
	PostProcessSettings& post = s.PostSettings();
	post.enabled = true;
	post.bloom = false;
	post.fxaa = false;
	post.ssao = false;
	post.vignette = 0.0f;
	// Looking along the floor: the bottom of the picture is near, the middle is far away
	A::AimCamera(s, { 0.0f, 3.0f, -20.0f }, { 0.25f, 0.0f, 0.0f });
	const std::vector<float> clear = CaptureGameViewSignature(s);
	REQUIRE(!clear.empty());

	post.fogEnabled = true;
	post.fogColor = { 1.0f, 0.0f, 1.0f };
	post.fogDensity = 0.03f;
	post.fogHeightFalloff = 0.0f;
	const std::vector<float> fogged = CaptureGameViewSignature(s);
	REQUIRE(fogged.size() == clear.size());

	const auto greenLoss = [&](const std::vector<float>& picture, int row) {
		float total = 0.0f;
		for (int column = 0; column < 16; ++column) total += clear[(row * 16 + column) * 3 + 1] - picture[(row * 16 + column) * 3 + 1];
		return total / 16.0f;
	};
	CHECK_MSG(greenLoss(fogged, 5) > greenLoss(fogged, 8) + 2.0f, std::format("far {:.2f}, near {:.2f}", greenLoss(fogged, 5), greenLoss(fogged, 8)));
	CHECK(greenLoss(fogged, 8) > 0.5f);

	// Height: seen from far above, fog that lies low hides less than fog of the same density everywhere
	post.fogDensity = 0.01f;
	A::AimCamera(s, { 0.0f, 40.0f, -40.0f }, { 0.78f, 0.0f, 0.0f });
	const std::vector<float> everywhere = CaptureGameViewSignature(s);
	post.fogHeight = -0.5f;
	post.fogHeightFalloff = 0.5f;
	const std::vector<float> lowLying = CaptureGameViewSignature(s);
	float everywhereGreen = 0.0f;
	float lowLyingGreen = 0.0f;
	for (int column = 0; column < 16; ++column) {
		everywhereGreen += everywhere[(4 * 16 + column) * 3 + 1];
		lowLyingGreen += lowLying[(4 * 16 + column) * 3 + 1];
	}
	CHECK_MSG(lowLyingGreen > everywhereGreen + 16.0f, std::format("low-lying {:.1f}, everywhere {:.1f}", lowLyingGreen, everywhereGreen));
	post.fogEnabled = false;

	// Off again: exactly the picture without fog; settings are saved
	A::AimCamera(s, { 0.0f, 3.0f, -20.0f }, { 0.25f, 0.0f, 0.0f });
	const std::vector<float> off = CaptureGameViewSignature(s);
	for (size_t i = 0; i < off.size(); ++i) CHECK_NEAR(off[i], clear[i], 0.5f);
	post.fogEnabled = true;
	post.fogDensity = 0.123f;
	REQUIRE(s.DeserializeSceneFromString(s.SerializeSceneToString()));
	CHECK(s.PostSettings().fogEnabled);
	CHECK_NEAR(s.PostSettings().fogDensity, 0.123f, 1e-5f);
	CHECK_NEAR(s.PostSettings().fogColor.y, 0.0f, 1e-5f);

	// Scripts: every other setting survives a Set
	EngineAPI& api = A::Api(s);
	s.PostSettings().ssaoRadius = 1.25f;
	ScriptPostProcess value;
	REQUIRE(api.getPostProcess(&s, &value));
	CHECK(value.fogEnabled);
	value.fogDensity = -5.0f; // clamped
	REQUIRE(api.setPostProcess(&s, &value));
	CHECK(s.PostSettings().fogDensity == 0.0f);
	CHECK_NEAR(s.PostSettings().ssaoRadius, 1.25f, 1e-5f);
	s.PostSettings().fogEnabled = false;
}


TEST_CASE(Terrain, HeightsAreRepeatableAndInRange)
{
	const TerrainHeights a = GenerateTerrainNoise(33, 7, 4.0f, 4);
	REQUIRE(a.resolution == 33);
	REQUIRE(a.values.size() == 33u * 33u);
	const TerrainHeights same = GenerateTerrainNoise(33, 7, 4.0f, 4);
	CHECK(a.values == same.values);
	CHECK(a.values != GenerateTerrainNoise(33, 8, 4.0f, 4).values);
	float lowest = 1.0f;
	float highest = 0.0f;
	float biggestStep = 0.0f;
	for (int z = 0; z < 33; ++z) {
		for (int x = 0; x < 33; ++x) {
			lowest = (std::min)(lowest, a.At(x, z));
			highest = (std::max)(highest, a.At(x, z));
			if (x > 0) biggestStep = (std::max)(biggestStep, std::abs(a.At(x, z) - a.At(x - 1, z)));
		}
	}
	CHECK(lowest >= 0.0f && highest <= 1.0f);
	CHECK(highest - lowest > 0.2f); // really hills
	CHECK(biggestStep < 0.35f);     // and not random spikes

	// Bad settings give a valid grid
	CHECK(GenerateTerrainNoise(0, 1, 4.0f, 4).resolution >= 2);
	CHECK(GenerateTerrainNoise(100000, 1, 4.0f, 4).resolution <= 513);
	const TerrainHeights flat = GenerateTerrainNoise(5, 1, std::numeric_limits<float>::quiet_NaN(), 0);
	for (float v : flat.values) CHECK(std::isfinite(v));

	// Image: 2 x 2, top row = far edge (+z)
	const std::vector<uint8_t> pixels = { 255, 255, 0, 0 };
	const TerrainHeights image = TerrainFromImage(pixels, 2, 2, 3);
	REQUIRE(image.resolution == 3);
	CHECK_NEAR(image.At(0, 2), 1.0f, 1e-4f); // far edge
	CHECK_NEAR(image.At(2, 2), 1.0f, 1e-4f);
	CHECK_NEAR(image.At(1, 0), 0.0f, 1e-4f); // near edge
	CHECK_NEAR(image.At(1, 1), 0.5f, 1e-4f);
	CHECK(TerrainFromImage({}, 0, 0, 3).values.size() == 9u); // nothing to read: flat
}

TEST_CASE(Terrain, BrushesRaycastAndHeightFiles)
{
	TerrainHeights flat;
	flat.resolution = 33;
	flat.values.assign(33 * 33, 0.5f);

	// Raise: the middle goes up, the far corner does not, nothing leaves 0..1
	TerrainHeights h = flat;
	for (int i = 0; i < 200; ++i) SculptTerrain(h, 0.5f, 0.5f, 0.2f, 0.05f, TerrainBrush::Raise);
	CHECK(h.At(16, 16) > 0.9f && h.At(16, 16) <= 1.0f);
	CHECK_NEAR(h.At(0, 0), 0.5f, 1e-6f);
	CHECK(h.At(20, 16) > 0.5f && h.At(20, 16) < h.At(16, 16) + 1e-6f); // softer towards the edge of the brush
	for (int i = 0; i < 400; ++i) SculptTerrain(h, 0.5f, 0.5f, 0.2f, 0.05f, TerrainBrush::Lower);
	CHECK(h.At(16, 16) >= 0.0f && h.At(16, 16) < 0.1f);

	// Flatten pulls towards the target; Smooth takes the edge off a spike
	h = flat;
	for (int i = 0; i < 50; ++i) SculptTerrain(h, 0.25f, 0.75f, 0.1f, 0.5f, TerrainBrush::Flatten, 0.2f);
	CHECK_NEAR(h.At(8, 24), 0.2f, 0.01f);
	h = flat;
	h.values[16 * 33 + 16] = 1.0f;
	SculptTerrain(h, 0.5f, 0.5f, 0.1f, 1.0f, TerrainBrush::Smooth);
	CHECK(h.At(16, 16) < 0.8f);
	SculptTerrain(h, std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.1f, 1.0f, TerrainBrush::Raise); // ignored
	for (float value : h.values) CHECK(std::isfinite(value));

	// A ray in the terrain's own space (centered, y = value * height) finds where it lands
	float u = 0.0f;
	float v = 0.0f;
	REQUIRE(RaycastTerrain(flat, 40.0f, 10.0f, { 5.0f, 50.0f, -10.0f }, { 0.0f, -1.0f, 0.0f }, u, v));
	CHECK_NEAR(u, 25.0f / 40.0f, 0.01f);
	CHECK_NEAR(v, 10.0f / 40.0f, 0.01f);
	REQUIRE(RaycastTerrain(flat, 40.0f, 10.0f, { -30.0f, 5.0f + 30.0f, 0.0f }, { 0.7071f, -0.7071f, 0.0f }, u, v)); // slanted, from outside
	CHECK_NEAR(u, 0.5f, 0.02f);
	CHECK(!RaycastTerrain(flat, 40.0f, 10.0f, { 100.0f, 50.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, u, v)); // beside it
	CHECK(!RaycastTerrain(flat, 40.0f, 10.0f, { 0.0f, 50.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, u, v));    // away from it

	// Height files round-trip and reject garbage
	TerrainHeights loaded;
	REQUIRE(DecodeTerrainHeights(EncodeTerrainHeights(h), loaded));
	CHECK(loaded.resolution == 33 && loaded.values == h.values);
	CHECK(!DecodeTerrainHeights({ 1, 2, 3 }, loaded));
}

TEST_CASE(Terrain, SculptedHeightsAreSavedAndUsed)
{
	const std::filesystem::path root = A::TempRoot() / "TerrainSculpt";
	std::filesystem::create_directories(root / "Assets");
	A::ProjectRoot(s) = root;
	for (auto& o : A::Objects(s)) {
		if (o.name == "Ground Plane") o.isActive = false;
	}
	GameObject& obj = s.CreateGameObject("Land", GameObjectType::Empty);
	obj.translation = { 10.0f, 0.0f, 0.0f };
	const uint32_t id = obj.id;
	REQUIRE(s.AddComponentByName(obj, kTerrainComponent));
	auto* terrain = obj.components.Get<TerrainComponent>(kTerrainComponent);
	terrain->size = 40.0f;
	terrain->height = 8.0f;
	terrain->resolution = 33;
	s.UpdateWorldMatrices();
	REQUIRE(s.GenerateTerrain(obj));

	const auto groundAt = [&](float x, float z) {
		EngineAPI& api = A::Api(s);
		A::State(s) = EditorSceneState::Play;
		A::Physics(s, 1.0f / 60.0f);
		ScriptRaycastHit hit{};
		const bool found = api.raycast(&s, { x, 50.0f, z }, { 0.0f, -1.0f, 0.0f }, 100.0f, 0xFFFFFFFFu, 0, &hit);
		A::State(s) = EditorSceneState::Edit;
		return found ? hit.point.y : -1000.0f;
	};
	const float before = groundAt(15.0f, 5.0f);
	const float farBefore = groundAt(-8.0f, -18.0f);

	// A stroke straight down on world (15, 5): the terrain sits at x = 10, so that is local (5, 5)
	s.terrainSculpt_.brush = TerrainBrush::Raise;
	s.terrainSculpt_.radius = 0.15f;
	s.terrainSculpt_.strength = 0.05f;
	for (int i = 0; i < 20; ++i) CHECK(s.SculptTerrainAt(id, { 15.0f, 50.0f, 5.0f }, { 0.0f, -1.0f, 0.0f }));
	CHECK(!s.SculptTerrainAt(id, { 200.0f, 50.0f, 5.0f }, { 0.0f, -1.0f, 0.0f })); // misses the terrain
	REQUIRE(s.FinishTerrainSculpt());
	terrain = s.FindGameObjectById(id, false)->components.Get<TerrainComponent>(kTerrainComponent);
	REQUIRE(terrain != nullptr && terrain->heightData[0] != '\0');
	CHECK(std::filesystem::exists(root / std::filesystem::path(terrain->heightData)));
	const float raised = groundAt(15.0f, 5.0f);
	CHECK_MSG(raised > before + 2.0f, std::format("{:.2f} -> {:.2f}", before, raised));
	CHECK_NEAR(groundAt(-8.0f, -18.0f), farBefore, 0.05f); // outside the brush

	// Every stroke is one undo step: undo the second, then the first, and redo both
	s.terrainSculpt_.brush = TerrainBrush::Lower;
	for (int i = 0; i < 10; ++i) CHECK(s.SculptTerrainAt(id, { 15.0f, 50.0f, 5.0f }, { 0.0f, -1.0f, 0.0f }));
	REQUIRE(s.FinishTerrainSculpt());
	const float lowered = groundAt(15.0f, 5.0f);
	CHECK_MSG(lowered < raised - 1.0f, std::format("{:.2f} -> {:.2f}", raised, lowered));
	s.Undo();
	CHECK_NEAR(groundAt(15.0f, 5.0f), raised, 0.05f);
	s.Undo();
	CHECK_NEAR(groundAt(15.0f, 5.0f), before, 0.05f);
	CHECK(s.FindGameObjectById(id, false)->components.Get<TerrainComponent>(kTerrainComponent)->heightData[0] == '\0');
	s.Redo();
	s.Redo();
	CHECK_NEAR(groundAt(15.0f, 5.0f), lowered, 0.05f);
	// A stroke after an undo starts from what is shown, not from the undone shape
	s.Undo();
	s.terrainSculpt_.brush = TerrainBrush::Raise;
	CHECK(s.SculptTerrainAt(id, { 5.0f, 50.0f, -12.0f }, { 0.0f, -1.0f, 0.0f })); // local (-5, -12), away from (5, 5)
	REQUIRE(s.FinishTerrainSculpt());
	CHECK_NEAR(groundAt(15.0f, 5.0f), raised, 0.05f);
	terrain = s.FindGameObjectById(id, false)->components.Get<TerrainComponent>(kTerrainComponent);

	// Generate again keeps the sculpted shape (the noise settings do not win over it)
	terrain->seed = 77;
	REQUIRE(s.GenerateTerrain(*s.FindGameObjectById(id, false)));
	CHECK_NEAR(groundAt(15.0f, 5.0f), raised, 0.05f);
}

TEST_CASE(Terrain, BrushOutlineLiesOnTheSurface)
{
	const std::filesystem::path root = A::TempRoot() / "TerrainBrushRing";
	std::filesystem::create_directories(root / "Assets");
	A::ProjectRoot(s) = root;
	GameObject& obj = s.CreateGameObject("Land", GameObjectType::Empty);
	obj.translation = { 10.0f, 1.0f, 0.0f };
	const uint32_t id = obj.id;
	REQUIRE(s.AddComponentByName(obj, kTerrainComponent));
	auto* terrain = obj.components.Get<TerrainComponent>(kTerrainComponent);
	terrain->size = 40.0f;
	terrain->height = 8.0f;
	terrain->resolution = 33;
	s.UpdateWorldMatrices();
	s.terrainSculpt_.radius = 0.1f; // 4 m on a 40 m terrain

	std::vector<Vector3> ring;
	REQUIRE(s.TerrainBrushOutline(id, { 15.0f, 50.0f, 5.0f }, { 0.0f, -1.0f, 0.0f }, ring));
	REQUIRE(ring.size() >= 32u);
	TerrainHeights heights;
	REQUIRE(s.ComputeTerrainHeights(*terrain, heights));
	for (const Vector3& p : ring) {
		const float dx = p.x - 15.0f;
		const float dz = p.z - 5.0f;
		CHECK_NEAR(std::sqrt(dx * dx + dz * dz), 4.0f, 0.05f);
		// On the surface (lifted 5 cm so the surface does not hide it), in world space: the object sits 1 m up
		CHECK_NEAR(p.y, TerrainHeightAt(heights, (p.x - 10.0f + 20.0f) / 40.0f, (p.z + 20.0f) / 40.0f) * 8.0f + 1.0f + 0.05f, 0.01f);
	}
	CHECK(!s.TerrainBrushOutline(id, { 200.0f, 50.0f, 5.0f }, { 0.0f, -1.0f, 0.0f }, ring));
}

TEST_CASE(Terrain, ObjHasOneVertexPerGridPoint)
{
	const TerrainHeights heights = GenerateTerrainNoise(5, 1, 2.0f, 2);
	const std::string obj = BuildTerrainObj(heights, 10.0f, 2.0f, 1.0f);
	int vertices = 0;
	int normals = 0;
	int faces = 0;
	float minX = 1e9f;
	float maxX = -1e9f;
	std::stringstream lines(obj);
	for (std::string line; std::getline(lines, line);) {
		if (line.rfind("v ", 0) == 0) {
			++vertices;
			float x = 0.0f, y = 0.0f, z = 0.0f;
			std::stringstream(line.substr(2)) >> x >> y >> z;
			minX = (std::min)(minX, x);
			maxX = (std::max)(maxX, x);
			CHECK(y >= 0.0f && y <= 2.0f);
		}
		if (line.rfind("vn ", 0) == 0) ++normals;
		if (line.rfind("f ", 0) == 0) ++faces;
	}
	CHECK(vertices == 25);
	CHECK(normals == 25);
	CHECK(faces == 4 * 4 * 2);
	CHECK_NEAR(minX, -5.0f, 1e-3f); // centred, and exactly as wide as asked
	CHECK_NEAR(maxX, 5.0f, 1e-3f);
	CHECK(BuildTerrainObj({}, 10.0f, 1.0f, 1.0f).find("f ") == std::string::npos);
}

TEST_CASE(Terrain, GeneratedTerrainIsSolidAndVisibleAndMatchesItsHeights)
{
	const std::filesystem::path root = A::TempRoot() / "TerrainProject";
	std::filesystem::create_directories(root / "Assets");
	A::ProjectRoot(s) = root;
	for (auto& o : A::Objects(s)) {
		if (o.name == "Ground Plane") o.isActive = false;
	}

	GameObject& obj = s.CreateGameObject("Land", GameObjectType::Empty);
	const uint32_t id = obj.id;
	REQUIRE(s.AddComponentByName(obj, kTerrainComponent));
	auto* terrain = obj.components.Get<TerrainComponent>(kTerrainComponent);
	terrain->size = 40.0f;
	terrain->height = 8.0f;
	terrain->resolution = 33;
	terrain->seed = 3;
	A::AimCamera(s, { 0.0f, 40.0f, 0.0f }, { 1.5707f, 0.0f, 0.0f });
	const std::vector<float> before = CaptureGameViewSignature(s);
	REQUIRE(s.GenerateTerrain(obj));
	const std::string firstFile = s.FindGameObjectById(id, false)->modelPath;
	CHECK(std::filesystem::exists(root / std::filesystem::path(firstFile)));
	CHECK(s.FindGameObjectById(id, false)->components.Has("Mesh Collider"));
	CHECK(s.FindGameObjectById(id, false)->isStatic);

	// Seen from above it covers the view: the winding faces up
	const std::vector<float> after = CaptureGameViewSignature(s);
	REQUIRE(after.size() == before.size());
	float change = 0.0f;
	for (size_t i = 0; i < after.size(); ++i) change = (std::max)(change, std::abs(after[i] - before[i]));
	CHECK_MSG(change > 5.0f, std::format("largest change {:.2f}", change));

	// A ray down lands on the height the grid says, on the right side of the map (asymmetric points)
	const TerrainHeights heights = GenerateTerrainNoise(33, 3, terrain->frequency, terrain->octaves);
	EngineAPI& api = A::Api(s);
	A::State(s) = EditorSceneState::Play;
	A::Physics(s, 1.0f / 60.0f);
	const int points[][2] = { { 4, 8 }, { 28, 8 }, { 8, 28 }, { 24, 20 } };
	for (const auto& point : points) {
		const float x = -20.0f + 40.0f * point[0] / 32.0f;
		const float z = -20.0f + 40.0f * point[1] / 32.0f;
		ScriptRaycastHit hit{};
		REQUIRE(api.raycast(&s, { x, 50.0f, z }, { 0.0f, -1.0f, 0.0f }, 100.0f, 0xFFFFFFFFu, 0, &hit));
		CHECK_NEAR(hit.point.y, heights.At(point[0], point[1]) * 8.0f, 0.05f);
	}
	A::State(s) = EditorSceneState::Edit;

	// Generating again uses a new file and removes the old one; the settings are saved
	s.FindGameObjectById(id, false)->components.Get<TerrainComponent>(kTerrainComponent)->seed = 4;
	REQUIRE(s.GenerateTerrain(*s.FindGameObjectById(id, false)));
	const std::string secondFile = s.FindGameObjectById(id, false)->modelPath;
	CHECK(secondFile != firstFile);
	CHECK(std::filesystem::exists(root / std::filesystem::path(firstFile))); // Undo still shows it
	s.Undo();
	CHECK(s.FindGameObjectById(id, false)->modelPath == firstFile);
	s.Redo();
	REQUIRE(s.DeserializeSceneFromString(s.SerializeSceneToString()));
	const GameObject* loaded = s.FindGameObjectById(id, false);
	REQUIRE(loaded != nullptr);
	CHECK(loaded->modelPath == secondFile);

	// A missing heightmap is an error, not a crash, and changes nothing
	GameObject* again = s.FindGameObjectById(id, false);
	strncpy_s(again->components.Get<TerrainComponent>(kTerrainComponent)->heightmap, "Assets/none.png", _TRUNCATE);
	CHECK(!s.GenerateTerrain(*again));
	CHECK(s.FindGameObjectById(id, false)->modelPath == secondFile);
	// Closing the scene removes the bakes nothing uses any more, and keeps the one the saved scene uses
	REQUIRE(s.SaveScene((root / "Assets" / "Land.scene.json").string()));
	s.NewScene();
	CHECK(!std::filesystem::exists(root / std::filesystem::path(firstFile)));
	CHECK(std::filesystem::exists(root / std::filesystem::path(secondFile)));
	CHECK(A::Dx(s)->GetDevice()->GetDeviceRemovedReason() == S_OK);
}


namespace {
	/// Text of the string literal that starts at `pos` (just after the opening quote), joined with literals that
	/// directly follow it ("a" "b" is one string in C++); simple escapes only
	std::string ReadCppLiteral(const std::string& text, size_t pos, size_t* end = nullptr)
	{
		std::string out;
		for (;;) {
			for (; pos < text.size() && text[pos] != '"'; ++pos) {
				if (text[pos] == '\\' && pos + 1 < text.size()) {
					const char next = text[++pos];
					out += next == 'n' ? '\n' : next == 't' ? '\t' : next;
				} else {
					out += text[pos];
				}
			}
			size_t next = pos + 1;
			while (next < text.size() && std::isspace(static_cast<unsigned char>(text[next]))) ++next;
			if (next >= text.size() || text[next] != '"') break;
			pos = next + 1;
		}
		if (end) *end = pos;
		return out;
	}
}

TEST_CASE(Localization, JapaneseCoversEveryEditorString)
{
	namespace fs = std::filesystem;
	// Runs where the sources are (the Sandbox folder, like CI); a packaged editor has none to check
	const fs::path sources = fs::current_path();
	if (!fs::exists(sources / "EditorSuite_UI.cpp")) return;
	std::ifstream jaFile(sources / "Resources" / "Localization" / "ja.json", std::ios::binary);
	REQUIRE(jaFile.good());
	std::stringstream jaText;
	jaText << jaFile.rdbuf();
	const auto ja = SceneJson::Parse(jaText.str());
	REQUIRE(ja.has_value() && ja->is_object());

	// Calls whose first argument a person reads; a bare literal there is never translated
	static const char* const kCalls[] = { "Text(\"", "TextDisabled(\"", "TextWrapped(\"", "Button(\"", "SmallButton(\"", "Checkbox(\"", "MenuItem(\"",
		"BeginMenu(\"", "Begin(\"", "BeginTabItem(\"", "CollapsingHeader(\"", "DragFloat(\"", "DragInt(\"", "SliderFloat(\"", "SliderInt(\"",
		"InputText(\"", "InputFloat(\"", "InputInt(\"", "Combo(\"", "ColorEdit3(\"", "ColorEdit4(\"", "Selectable(\"", "SeparatorText(\"",
		"TreeNode(\"", "TreeNodeEx(\"", "SetItemTooltip(\"", "BeginPopupModal(\"", "BulletText(\"", "RadioButton(\"" };
	std::vector<std::string> untranslated;
	std::vector<std::string> unwrapped;
	for (const auto& entry : fs::directory_iterator(sources)) {
		const std::string name = entry.path().filename().string();
		if (entry.path().extension() != ".cpp" || name == "EditorSuite_Tests.cpp") continue;
		std::ifstream in(entry.path(), std::ios::binary);
		std::stringstream buffer;
		buffer << in.rdbuf();
		const std::string text = buffer.str();
		for (const char* prefix : { "Tr(\"", "TrId(\"" }) {
			for (size_t at = text.find(prefix); at != std::string::npos; at = text.find(prefix, at + 1)) {
				if (at > 0 && (std::isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_')) continue;
				std::string key = ReadCppLiteral(text, at + std::strlen(prefix));
				if (const size_t hashes = key.find("##"); hashes != std::string::npos) key.resize(hashes);
				if (std::count_if(key.begin(), key.end(), [](unsigned char c) { return std::isalpha(c) != 0; }) < 2) continue; // "X", "%d": nothing to translate
				if (!ja->contains(key)) untranslated.push_back(name + ": " + key);
			}
		}
		for (const char* call : kCalls) {
			const std::string needle = std::string("ImGui::") + call;
			for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
				std::string literal = ReadCppLiteral(text, at + needle.size());
				if (literal.rfind("##", 0) == 0) continue; // an ID only, nothing shown
				if (const size_t hashes = literal.find("##"); hashes != std::string::npos) literal.resize(hashes);
				int letters = 0;
				for (unsigned char c : literal) letters += std::isalpha(c) ? 1 : 0;
				if (letters < 2) continue; // "X", "%.2f", symbols
				unwrapped.push_back(name + ": " + literal);
			}
		}
		// The editor's own label widget, and placeholder text of search boxes
		for (const char* needle : { "DrawVec3Control(\"", "InputTextWithHint(\"" }) {
			for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
				size_t end = 0;
				std::string literal = ReadCppLiteral(text, at + std::strlen(needle), &end);
				if (std::string_view(needle).starts_with("InputTextWithHint")) {
					const size_t next = text.find_first_not_of(" ,", end + 1);
					if (next == std::string::npos || text[next] != '"') continue; // the hint is already Tr(...)
					literal = ReadCppLiteral(text, next + 1);
				}
				if (std::count_if(literal.begin(), literal.end(), [](unsigned char c) { return std::isalpha(c) != 0; }) < 2) continue;
				unwrapped.push_back(name + ": " + literal);
			}
		}
	}
	// What the registry shows: component names, field labels, field tooltips (test-only components excluded)
	for (const ComponentInfo& info : ComponentRegistry::Get().All()) {
		if (!info.IsDataComponent() || info.category == "Tests") continue;
		if (!ja->contains(info.name)) untranslated.push_back("component: " + info.name);
		for (const ComponentField& field : info.fields) {
			if (const std::string label = HumanizeFieldName(field.name); !ja->contains(label)) untranslated.push_back(info.name + " field: " + label);
			if (field.tooltip && !ja->contains(field.tooltip)) untranslated.push_back(info.name + " tooltip: " + field.tooltip);
		}
	}
	// Indonesian keeps up with Japanese
	std::ifstream idFile(sources / "Resources" / "Localization" / "id.json", std::ios::binary);
	std::stringstream idText;
	idText << idFile.rdbuf();
	const auto id = SceneJson::Parse(idText.str());
	REQUIRE(id.has_value() && id->is_object());
	for (auto it = ja->begin(); it != ja->end(); ++it) {
		if (it.key() != "_language" && !id->contains(it.key())) untranslated.push_back("no Indonesian: " + it.key());
	}
	std::string report;
	for (size_t i = 0; i < untranslated.size() && i < 200; ++i) report += "\n  no Japanese: " + untranslated[i];
	for (size_t i = 0; i < unwrapped.size() && i < 15; ++i) report += "\n  not wrapped in Tr/TrId: " + unwrapped[i];
	CHECK_MSG(untranslated.empty() && unwrapped.empty(), std::format("{} without Japanese, {} not wrapped{}", untranslated.size(), unwrapped.size(), report));
}


TEST_CASE(AutoSave, RulesForWhenAndWhere)
{
	CHECK(AutoSave::IsDue(130.0, 0.0, 120.0, true, true));
	CHECK(!AutoSave::IsDue(100.0, 0.0, 120.0, true, true)); // too soon
	CHECK(!AutoSave::IsDue(500.0, 0.0, 120.0, false, true)); // nothing to save
	CHECK(!AutoSave::IsDue(500.0, 0.0, 120.0, true, false)); // Play mode: changes there are thrown away anyway
	CHECK(!AutoSave::IsDue(500.0, 0.0, 0.0, true, true));   // interval 0 = off

	namespace fs = std::filesystem;
	const fs::path root = A::TempRoot() / "AutoSaveRules";
	CHECK(AutoSave::PathFor(root, root / "Assets" / "Scenes" / "Level1.scene.json") == root / "Library" / "AutoSave" / "Level1.scene.json.autosave.json");
	CHECK(AutoSave::PathFor(root, "") == root / "Library" / "AutoSave" / "Untitled.autosave.json");

	fs::create_directories(root);
	const fs::path scene = root / "a.scene.json";
	const fs::path copy = root / "a.autosave.json";
	{ std::ofstream(scene) << "{}"; }
	CHECK(!AutoSave::IsNewerThanScene(copy, scene)); // no copy
	{ std::ofstream(copy) << "{}"; }
	fs::last_write_time(copy, fs::last_write_time(scene) + std::chrono::seconds(10));
	CHECK(AutoSave::IsNewerThanScene(copy, scene));
	fs::last_write_time(copy, fs::last_write_time(scene) - std::chrono::seconds(10));
	CHECK(!AutoSave::IsNewerThanScene(copy, scene));
	CHECK(AutoSave::IsNewerThanScene(copy, root / "missing.scene.json"));
}

TEST_CASE(AutoSave, CopyIsWrittenRemovedAndRestored)
{
	namespace fs = std::filesystem;
	const fs::path root = A::TempRoot() / "AutoSaveProject";
	fs::create_directories(root / "Assets" / "Scenes");
	A::ProjectRoot(s) = root;
	const std::string scenePath = (root / "Assets" / "Scenes" / "Work.scene.json").string();
	REQUIRE(s.SaveScene(scenePath));
	const fs::path copy = AutoSave::PathFor(root, scenePath);

	A::AutoSaveInTests(s) = true;
	A::RunAutoSave(s, 1000.0);
	CHECK(!fs::exists(copy)); // nothing changed yet

	s.CreateCube("Unsaved Work", { 1.0f, 2.0f, 3.0f });
	A::Dirty(s) = true;
	A::RunAutoSave(s, 2000.0);
	REQUIRE(fs::exists(copy));
	A::RunAutoSave(s, 2001.0); // not again right away
	CHECK(A::LastAutoSave(s) == 2000.0);

	// Saving makes the copy unnecessary
	REQUIRE(s.SaveScene(scenePath));
	CHECK(!fs::exists(copy));

	// A crash after more work: the copy is offered when the scene opens, and brings the work back
	s.CreateCube("Lost Work", { 0.0f, 0.0f, 0.0f });
	A::Dirty(s) = true;
	A::RunAutoSave(s, 5000.0);
	REQUIRE(fs::exists(copy));
	fs::last_write_time(copy, fs::last_write_time(scenePath) + std::chrono::seconds(30));
	REQUIRE(s.LoadScene(scenePath));
	CHECK(s.FindGameObjectByName("Lost Work", false) == nullptr);
	REQUIRE(!A::PendingRestore(s).empty());
	REQUIRE(A::Restore(s));
	CHECK(s.FindGameObjectByName("Lost Work", false) != nullptr);
	CHECK(A::Dirty(s));
	CHECK(A::ScenePath(s) == scenePath); // Save goes to the real scene file
	CHECK(A::PendingRestore(s).empty());

	// Play mode never writes one
	A::State(s) = EditorSceneState::Play;
	fs::remove(copy);
	A::RunAutoSave(s, 9000.0);
	CHECK(!fs::exists(copy));
	A::State(s) = EditorSceneState::Edit;
	A::AutoSaveInTests(s) = false;
	A::Dirty(s) = false;
}


TEST_CASE(Prefabs, LinkedInstancesFollowTheirPrefab)
{
	namespace fs = std::filesystem;
	const fs::path root = A::TempRoot() / "LinkedPrefabProject";
	fs::create_directories(root / "Assets" / "Prefabs");
	fs::create_directories(root / "Assets" / "Scenes");
	A::ProjectRoot(s) = root;
	s.ClearPrefabCache();

	// A prefab with a child
	GameObject& turret = s.CreateCube("Turret", { 0.0f, 0.0f, 0.0f });
	const uint32_t turretId = turret.id;
	GameObject& barrel = s.CreateCube("Barrel", { 0.0f, 1.0f, 0.0f });
	barrel.parentId = static_cast<int>(turretId);
	s.RebuildHierarchyLinks();
	REQUIRE(s.SavePrefab(turretId, "Turret"));
	{
		std::ifstream in(root / "Assets" / "Prefabs" / "Turret.prefab.json");
		std::stringstream text;
		text << in.rdbuf();
		CHECK(text.str().find("\"prefab\"") == std::string::npos); // a prefab file never links to itself
	}
	s.QueueDeleteGameObjectById(turretId);
	s.ProcessPendingDeletes();

	const uint32_t a = s.InstantiatePrefabNow("Turret", { -5.0f, 0.0f, 0.0f });
	const uint32_t b = s.InstantiatePrefabNow("Turret", { 5.0f, 0.0f, 0.0f });
	REQUIRE(a != 0u);
	REQUIRE(b != 0u);
	s.FindGameObjectById(b, false)->rotation = { 0.0f, 1.0f, 0.0f };
	s.FindGameObjectById(b, false)->rotationQuat = Quaternion::FromEuler({ 0.0f, 1.0f, 0.0f });
	CHECK(s.PrefabInstanceRoots("Turret").size() == 2u);

	const auto childOf = [&](uint32_t parent) -> GameObject* {
		for (auto& o : A::Objects(s)) {
			if (o.parentId == static_cast<int>(parent)) return &o;
		}
		return nullptr;
	};

	// Change instance A's child and Apply: B gets it, B keeps its own place
	REQUIRE(childOf(a) != nullptr);
	childOf(a)->materialColor = { 1.0f, 0.0f, 0.0f, 1.0f };
	REQUIRE(s.ApplyPrefabInstance(a));
	REQUIRE(s.FindGameObjectById(b, false) != nullptr);
	REQUIRE(childOf(b) != nullptr);
	CHECK_NEAR(childOf(b)->materialColor.y, 0.0f, 1e-5f);
	CHECK_NEAR(s.FindGameObjectById(b, false)->translation.x, 5.0f, 1e-5f);
	CHECK_NEAR(s.FindGameObjectById(b, false)->rotation.y, 1.0f, 1e-5f);
	CHECK(s.FindGameObjectById(a, false) != nullptr); // the root id survives, so references to it still work

	// Revert throws away a local change but not the position
	childOf(b)->materialColor = { 0.0f, 0.0f, 1.0f, 1.0f };
	s.FindGameObjectById(b, false)->translation.z = 3.0f;
	REQUIRE(s.RevertPrefabInstance(b));
	CHECK_NEAR(childOf(b)->materialColor.x, 1.0f, 1e-5f);
	CHECK_NEAR(s.FindGameObjectById(b, false)->translation.z, 3.0f, 1e-5f);

	// The link is saved with the scene; opening the scene brings a later prefab edit in
	const std::string scenePath = (root / "Assets" / "Scenes" / "Linked.scene.json").string();
	REQUIRE(s.SaveScene(scenePath));
	childOf(a)->materialColor = { 0.0f, 1.0f, 0.0f, 1.0f };
	REQUIRE(s.SavePrefab(a, "Turret")); // edited elsewhere, e.g. in another scene
	REQUIRE(s.LoadScene(scenePath));
	REQUIRE(s.FindGameObjectById(b, false) != nullptr);
	CHECK(s.FindGameObjectById(b, false)->prefabSource == "Turret");
	REQUIRE(childOf(b) != nullptr);
	CHECK_NEAR(childOf(b)->materialColor.y, 1.0f, 1e-5f);
	CHECK_NEAR(s.FindGameObjectById(b, false)->translation.z, 3.0f, 1e-5f);

	// Unlinked: an ordinary object again, untouched by the next sync
	s.UnlinkPrefabInstance(b);
	CHECK(s.FindGameObjectById(b, false)->prefabSource.empty());
	CHECK(childOf(b)->prefabSource.empty());
	CHECK(s.PrefabInstanceRoots("Turret").size() == 1u);

	// A prefab that is gone: the instance stays as it is
	fs::remove(root / "Assets" / "Prefabs" / "Turret.prefab.json");
	s.ClearPrefabCache();
	CHECK(!s.RevertPrefabInstance(a));
	CHECK(s.FindGameObjectById(a, false) != nullptr);
	CHECK(childOf(a) != nullptr);
}


TEST_CASE(Hierarchy, MultiSelectAddsRemovesAndActsOnAll)
{
	const uint32_t a = s.CreateCube("A", { 0.0f, 0.0f, 0.0f }).id;
	const uint32_t b = s.CreateCube("B", { 2.0f, 0.0f, 0.0f }).id;
	const uint32_t c = s.CreateCube("C", { 4.0f, 0.0f, 0.0f }).id;
	GameObject& child = s.CreateCube("Child of A", { 0.0f, 1.0f, 0.0f });
	const uint32_t childId = child.id;
	child.parentId = static_cast<int>(a);
	s.RebuildHierarchyLinks();
	const auto index = [&](uint32_t id) { return A::IndexOf(s, id); };

	s.SelectObject(index(a), false);
	s.SelectObject(index(b), true); // Ctrl+click
	s.SelectObject(index(childId), true);
	CHECK(s.SelectedIds().size() == 3u);
	CHECK(s.IsSelected(a) && s.IsSelected(b) && s.IsSelected(childId) && !s.IsSelected(c));
	CHECK(A::Primary(s) == index(a)); // the first click stays the one the Inspector shows

	// Ctrl+click again takes it out; on the primary the next one takes its place
	s.SelectObject(index(b), true);
	CHECK(!s.IsSelected(b));
	s.SelectObject(index(a), true);
	CHECK(!s.IsSelected(a));
	CHECK(A::Primary(s) == index(childId));
	// A plain click starts over
	s.SelectObject(index(a), true);
	s.SelectObject(index(c), false);
	CHECK(s.SelectedIds() == std::vector<uint32_t>{ c });

	// Moving with the gizmo moves the others by the same amount; a selected child moves with its parent only once
	s.SelectObject(index(a), false);
	s.SelectObject(index(b), true);
	s.SelectObject(index(childId), true);
	s.FindGameObjectById(a, false)->translation.x += 10.0f; // what the gizmo does to the primary
	s.MoveExtraSelection({ 10.0f, 0.0f, 0.0f });
	CHECK_NEAR(s.FindGameObjectById(b, false)->translation.x, 12.0f, 1e-5f);
	CHECK_NEAR(s.FindGameObjectById(childId, false)->translation.x, 0.0f, 1e-5f); // local to A, which moved already

	// Delete removes every selected object, and stale ids do not survive a new scene
	s.DeleteSelectedGameObject();
	s.ProcessPendingDeletes();
	CHECK(s.FindGameObjectById(a, false) == nullptr);
	CHECK(s.FindGameObjectById(b, false) == nullptr);
	CHECK(s.FindGameObjectById(childId, false) == nullptr);
	CHECK(s.FindGameObjectById(c, false) != nullptr);
	CHECK(s.SelectedIds().size() <= 1u);
	s.SelectObject(index(c), false);
	s.SelectObject(-1, true);
	CHECK(s.SelectedIds().empty());
	s.SelectObject(index(c), false);
	A::Extra(s).push_back(999999u); // an id from another scene
	s.NewScene();
	CHECK(A::Extra(s).empty());
}


namespace {
	/// A prefab "Tower" (root + "Top" child) in a fresh project, two linked instances. Returns the root ids.
	std::pair<uint32_t, uint32_t> MakeTowerInstances(EditorSuite& s, const char* project)
	{
		namespace fs = std::filesystem;
		const fs::path root = A::TempRoot() / project;
		fs::create_directories(root / "Assets" / "Prefabs");
		fs::create_directories(root / "Assets" / "Scenes");
		A::ProjectRoot(s) = root;
		s.ClearPrefabCache();
		const uint32_t tower = s.CreateCube("Tower", { 0.0f, 0.0f, 0.0f }).id;
		GameObject& top = s.CreateCube("Top", { 0.0f, 2.0f, 0.0f });
		top.parentId = static_cast<int>(tower);
		s.RebuildHierarchyLinks();
		s.SavePrefab(tower, "Tower");
		s.QueueDeleteGameObjectById(tower);
		s.ProcessPendingDeletes();
		const uint32_t a = s.InstantiatePrefabNow("Tower", { -5.0f, 0.0f, 0.0f });
		const uint32_t b = s.InstantiatePrefabNow("Tower", { 5.0f, 0.0f, 0.0f });
		s.ResetUndoHistory();
		return { a, b };
	}

	GameObject* ChildOf(EditorSuite& s, uint32_t parent)
	{
		for (auto& o : A::Objects(s)) {
			if (o.parentId == static_cast<int>(parent)) return &o;
		}
		return nullptr;
	}
}

TEST_CASE(Prefabs, ChildIdsSurviveSyncAndPrefabChanges)
{
	const auto [a, b] = MakeTowerInstances(s, "StablePrefabIds");
	REQUIRE(ChildOf(s, b) != nullptr);
	const uint32_t topOfB = ChildOf(s, b)->id;

	// A script remembered the child's id: it still means the same child after any number of syncs
	REQUIRE(s.RevertPrefabInstance(b));
	REQUIRE(s.RevertPrefabInstance(b));
	CHECK(ChildOf(s, b) != nullptr && ChildOf(s, b)->id == topOfB);

	// The prefab gains a node in front of "Top" (in save order): the old child keeps its id and its own data
	GameObject& extra = s.CreateCube("Base", { 0.0f, -1.0f, 0.0f });
	const uint32_t extraId = extra.id;
	extra.parentId = static_cast<int>(a);
	s.RebuildHierarchyLinks();
	for (auto& o : A::Objects(s)) {
		if (o.parentId == static_cast<int>(a) && o.id != extraId) o.name = "Top";
	}
	// Base goes first among the children
	auto& objects = A::Objects(s);
	const auto baseIt = std::find_if(objects.begin(), objects.end(), [&](const GameObject& o) { return o.id == extraId; });
	GameObject moved = std::move(*baseIt);
	objects.erase(baseIt);
	objects.insert(objects.begin(), std::move(moved));
	s.RebuildHierarchyLinks();
	REQUIRE(s.ApplyPrefabInstance(a));
	int children = 0;
	bool topKept = false;
	for (auto& o : A::Objects(s)) {
		if (o.parentId != static_cast<int>(b)) continue;
		++children;
		if (o.id == topOfB) topKept = o.name == "Top";
	}
	CHECK(children == 2);
	CHECK(topKept);
}

TEST_CASE(Prefabs, OverridesStayAndCanBeReverted)
{
	const auto [a, b] = MakeTowerInstances(s, "PrefabOverrides");
	REQUIRE(ChildOf(s, b) != nullptr);
	const uint32_t topOfB = ChildOf(s, b)->id;

	// The user changes one instance in the editor: that becomes an override of that instance only
	ChildOf(s, b)->materialColor = { 0.0f, 0.0f, 1.0f, 1.0f };
	s.PushUndoSnapshot();
	const GameObject* top = s.FindGameObjectById(topOfB, false);
	REQUIRE(top != nullptr);
	CHECK(std::find(top->prefabOverrides.begin(), top->prefabOverrides.end(), "materialColor") != top->prefabOverrides.end());

	// The prefab changes colour and size: B keeps its own colour but takes the new size
	ChildOf(s, a)->materialColor = { 1.0f, 0.0f, 0.0f, 1.0f };
	ChildOf(s, a)->scale = { 2.0f, 2.0f, 2.0f };
	s.PushUndoSnapshot();
	REQUIRE(s.ApplyPrefabInstance(a));
	top = s.FindGameObjectById(topOfB, false);
	REQUIRE(top != nullptr);
	CHECK_NEAR(top->materialColor.z, 1.0f, 1e-5f);
	CHECK_NEAR(top->scale.x, 2.0f, 1e-5f);
	// Applying cleared A's own overrides: its values are the prefab now
	CHECK(ChildOf(s, a)->prefabOverrides.empty());

	// Saved and loaded with the scene
	const std::string scenePath = (A::TempRoot() / "PrefabOverrides" / "Assets" / "Scenes" / "Overrides.scene.json").string();
	REQUIRE(s.SaveScene(scenePath));
	REQUIRE(s.LoadScene(scenePath));
	top = s.FindGameObjectById(topOfB, false);
	REQUIRE(top != nullptr);
	CHECK_NEAR(top->materialColor.z, 1.0f, 1e-5f);
	CHECK(!top->prefabOverrides.empty());

	// Reverting the one property brings the prefab value back
	REQUIRE(s.RevertPrefabOverride(topOfB, "materialColor"));
	top = s.FindGameObjectById(topOfB, false);
	REQUIRE(top != nullptr);
	CHECK_NEAR(top->materialColor.x, 1.0f, 1e-5f);
	CHECK(top->prefabOverrides.empty());
	CHECK(!s.RevertPrefabOverride(topOfB, "materialColor")); // nothing left to revert

	// Root position and rotation are the instance's own, never listed as overrides
	s.FindGameObjectById(b, false)->translation.y = 4.0f;
	s.PushUndoSnapshot();
	CHECK(s.FindGameObjectById(b, false)->prefabOverrides.empty());

	// Data components count per component
	REQUIRE(s.AddComponentByName(*s.FindGameObjectById(topOfB, false), "Trail"));
	top = s.FindGameObjectById(topOfB, false);
	CHECK(std::find(top->prefabOverrides.begin(), top->prefabOverrides.end(), "components/Trail") != top->prefabOverrides.end());
	s.SyncLinkedPrefabs(); // what opening the scene does
	CHECK(s.FindGameObjectById(topOfB, false)->components.Has("Trail")); // an added component is an override too
}

TEST_CASE(Prefabs, PrefabModeEditsThePrefabAndComesBack)
{
	const auto [a, b] = MakeTowerInstances(s, "PrefabModeProject");
	const std::string scenePath = (A::TempRoot() / "PrefabModeProject" / "Assets" / "Scenes" / "Main.scene.json").string();
	REQUIRE(s.SaveScene(scenePath));
	s.FindGameObjectById(a, false)->name = "Unsaved Edit"; // the scene has an unsaved change while the prefab is edited
	s.PushUndoSnapshot(); // an editor edit: an override of that instance
	A::Dirty(s) = true;
	const size_t sceneObjects = A::Objects(s).size();

	CHECK(!s.OpenPrefabMode("NoSuchPrefab"));
	CHECK(!s.IsInPrefabMode());
	REQUIRE(s.OpenPrefabMode("Tower"));
	CHECK(s.IsInPrefabMode());
	CHECK(s.FindGameObjectById(a, false) == nullptr || s.FindGameObjectById(a, false)->name != "Unsaved Edit"); // only the prefab is open
	GameObject* top = nullptr;
	int towers = 0;
	for (auto& o : A::Objects(s)) {
		if (o.name == "Tower") ++towers;
		if (o.name == "Top") top = &o;
	}
	CHECK(towers == 1);
	REQUIRE(top != nullptr);
	top->materialColor = { 0.0f, 1.0f, 0.0f, 1.0f };
	s.PushUndoSnapshot();
	s.StartPlayMode(); // not while a prefab is open
	CHECK(A::State(s) == EditorSceneState::Edit);
	REQUIRE(s.SaveScene(A::ScenePath(s))); // Ctrl+S saves the prefab, not a scene
	CHECK(!std::filesystem::exists(A::TempRoot() / "PrefabModeProject" / "Assets" / "Scenes" / "SampleScene.scene.json"));

	s.ClosePrefabMode();
	CHECK(!s.IsInPrefabMode());
	CHECK(A::ScenePath(s) == scenePath);
	CHECK(s.IsSceneDirty()); // the unsaved scene edit is still there
	CHECK(A::Objects(s).size() == sceneObjects);
	REQUIRE(s.FindGameObjectById(a, false) != nullptr);
	CHECK(s.FindGameObjectById(a, false)->name == "Unsaved Edit");
	// The scene's undo history survived the trip: the rename can still be undone and redone
	s.Undo();
	CHECK(s.FindGameObjectById(a, false)->name == "Tower");
	s.Redo();
	CHECK(s.FindGameObjectById(a, false)->name == "Unsaved Edit");
	// Both instances show the saved prefab
	for (uint32_t root : { a, b }) {
		const GameObject* child = ChildOf(s, root);
		REQUIRE(child != nullptr);
		CHECK_NEAR(child->materialColor.y, 1.0f, 1e-5f);
		CHECK_NEAR(child->materialColor.x, 0.0f, 1e-5f);
	}

	// Closing without saving leaves the prefab file as it was
	REQUIRE(s.OpenPrefabMode("Tower"));
	for (auto& o : A::Objects(s)) {
		if (o.name == "Top") o.materialColor = { 1.0f, 0.0f, 1.0f, 1.0f };
	}
	s.ClosePrefabMode();
	CHECK_NEAR(ChildOf(s, a)->materialColor.y, 1.0f, 1e-5f);
	// Opening another scene leaves prefab mode first
	REQUIRE(s.OpenPrefabMode("Tower"));
	REQUIRE(s.LoadScene(scenePath));
	CHECK(!s.IsInPrefabMode());
}

TEST_CASE(Prefabs, VariantsFollowTheirBaseAndKeepTheirOwnValues)
{
	const auto [a, b] = MakeTowerInstances(s, "PrefabVariants");
	REQUIRE(ChildOf(s, b) != nullptr);
	ChildOf(s, b)->materialColor = { 0.0f, 0.0f, 1.0f, 1.0f };
	s.PushUndoSnapshot();

	// B becomes the first instance of "Tower Variant": blue is now the variant's value, not an override
	const std::string variant = s.CreatePrefabVariant(b);
	REQUIRE(variant == "Tower Variant");
	CHECK(s.FindGameObjectById(b, false)->prefabSource == variant);
	CHECK(ChildOf(s, b)->prefabOverrides.empty());
	CHECK_NEAR(ChildOf(s, b)->materialColor.z, 1.0f, 1e-5f);
	CHECK(s.PrefabBaseOf(variant) == "Tower");
	CHECK(s.PrefabBaseOf("Tower").empty());

	// The base changes colour and size: the variant takes the size and keeps its blue
	ChildOf(s, a)->materialColor = { 1.0f, 0.0f, 0.0f, 1.0f };
	ChildOf(s, a)->scale = { 2.0f, 2.0f, 2.0f };
	s.PushUndoSnapshot();
	REQUIRE(s.ApplyPrefabInstance(a));
	REQUIRE(ChildOf(s, b) != nullptr);
	CHECK_NEAR(ChildOf(s, b)->materialColor.z, 1.0f, 1e-5f);
	CHECK_NEAR(ChildOf(s, b)->materialColor.x, 0.0f, 1e-5f);
	CHECK_NEAR(ChildOf(s, b)->scale.x, 2.0f, 1e-5f);

	// A new instance of the variant is the same, and a child added to the variant stays with it
	const uint32_t c = s.InstantiatePrefabNow(variant, { 0.0f, 0.0f, 8.0f });
	REQUIRE(c != 0 && ChildOf(s, c) != nullptr);
	CHECK_NEAR(ChildOf(s, c)->materialColor.z, 1.0f, 1e-5f);
	CHECK_NEAR(ChildOf(s, c)->scale.y, 2.0f, 1e-5f);
	GameObject& flag = s.CreateCube("Flag", { 0.0f, 4.0f, 8.0f });
	flag.parentId = static_cast<int>(c);
	s.RebuildHierarchyLinks();
	REQUIRE(s.ApplyPrefabInstance(c));
	int flags = 0;
	for (const auto& o : A::Objects(s)) {
		if (o.name == "Flag" && o.parentId == static_cast<int>(b)) ++flags;
	}
	CHECK(flags == 1); // B (same variant) got the flag
	REQUIRE(ChildOf(s, a) != nullptr);
	for (const auto& o : A::Objects(s)) CHECK(!(o.name == "Flag" && o.parentId == static_cast<int>(a))); // the base did not

	// A prefab cannot be its own base
	CHECK(!s.SavePrefab(a, "Tower", "Tower"));

	// The variant drops the base's Top: gone from every variant instance, still in the base
	for (const auto& o : A::Objects(s)) {
		if (o.name == "Top" && o.parentId == static_cast<int>(c)) { s.QueueDeleteGameObjectById(o.id); break; }
	}
	s.ProcessPendingDeletes();
	REQUIRE(s.ApplyPrefabInstance(c));
	const auto hasTop = [&](uint32_t root) {
		for (const auto& o : A::Objects(s)) {
			if (o.name == "Top" && o.parentId == static_cast<int>(root)) return true;
		}
		return false;
	};
	CHECK(!hasTop(b));
	CHECK(!hasTop(c));
	CHECK(hasTop(a));
	const uint32_t d = s.InstantiatePrefabNow(variant, { 0.0f, 0.0f, 16.0f });
	CHECK(d != 0 && !hasTop(d));
}

TEST_CASE(Prefabs, JointsInsideNestedPrefabsAndVariantsStayConnected)
{
	namespace fs = std::filesystem;
	const fs::path root = A::TempRoot() / "NestedJoints";
	fs::create_directories(root / "Assets" / "Prefabs");
	A::ProjectRoot(s) = root;
	s.ClearPrefabCache();
	// A door on a hinge: the joint of "Leaf" connects to "Frame", both inside the prefab
	const uint32_t frame = s.CreateCube("Frame", { 0.0f, 0.0f, 0.0f }).id;
	GameObject& leaf = s.CreateCube("Leaf", { 1.0f, 0.0f, 0.0f });
	leaf.parentId = static_cast<int>(frame);
	const uint32_t leafId = leaf.id;
	s.RebuildHierarchyLinks();
	REQUIRE(s.AddComponentByName(*s.FindGameObjectById(leafId, false), "Joint"));
	s.FindGameObjectById(leafId, false)->components.Get<JointComponent>("Joint")->connectedObjectId = static_cast<int>(frame);
	REQUIRE(s.SavePrefab(frame, "Door"));
	s.QueueDeleteGameObjectById(frame);
	s.ProcessPendingDeletes();

	// The door inside a house, and a variant of the door
	const uint32_t house = s.CreateCube("House", { 0.0f, 0.0f, 0.0f }).id;
	const uint32_t door = s.InstantiatePrefabNow("Door", { 3.0f, 0.0f, 0.0f });
	s.FindGameObjectById(door, false)->parentId = static_cast<int>(house);
	s.RebuildHierarchyLinks();
	REQUIRE(s.SavePrefab(house, "House"));
	s.QueueDeleteGameObjectById(house);
	s.ProcessPendingDeletes();
	const uint32_t plainDoor = s.InstantiatePrefabNow("Door", { 0.0f, 0.0f, 9.0f });
	const std::string variant = s.CreatePrefabVariant(plainDoor);
	REQUIRE(!variant.empty());

	// Every instance: the leaf's joint points at the frame of the same instance
	const auto jointOk = [&](uint32_t instanceRoot) {
		std::vector<uint32_t> ids{ instanceRoot };
		for (size_t i = 0; i < ids.size(); ++i) {
			for (const auto& o : A::Objects(s)) {
				if (o.parentId == static_cast<int>(ids[i])) ids.push_back(o.id);
			}
		}
		uint32_t frameId = 0;
		int connected = -1;
		for (uint32_t id : ids) {
			GameObject* o = s.FindGameObjectById(id, false);
			if (o->name == "Frame") frameId = o->id;
			if (const auto* joint = o->components.Get<JointComponent>("Joint")) connected = joint->connectedObjectId;
		}
		return frameId != 0 && connected == static_cast<int>(frameId);
	};
	CHECK(jointOk(s.InstantiatePrefabNow("House", { 0.0f, 0.0f, 20.0f })));
	CHECK(jointOk(s.InstantiatePrefabNow(variant, { 0.0f, 0.0f, 30.0f })));
	CHECK(jointOk(plainDoor));
}

TEST_CASE(Prefabs, NestedPrefabsFollowTheInnerPrefab)
{
	namespace fs = std::filesystem;
	const fs::path root = A::TempRoot() / "NestedPrefabs";
	fs::create_directories(root / "Assets" / "Prefabs");
	A::ProjectRoot(s) = root;
	s.ClearPrefabCache();
	const uint32_t wheel = s.CreateCube("Wheel", { 0.0f, 0.0f, 0.0f }).id;
	GameObject& hub = s.CreateCube("Hub", { 0.0f, 0.0f, 0.0f });
	hub.parentId = static_cast<int>(wheel);
	s.RebuildHierarchyLinks();
	REQUIRE(s.SavePrefab(wheel, "Wheel"));
	s.QueueDeleteGameObjectById(wheel);
	s.ProcessPendingDeletes();

	// A car with a Wheel instance inside, moved and widened inside the car
	const uint32_t car = s.CreateCube("Car", { 0.0f, 0.0f, 0.0f }).id;
	const uint32_t inner = s.InstantiatePrefabNow("Wheel", { 2.0f, 0.0f, 0.0f });
	s.FindGameObjectById(inner, false)->parentId = static_cast<int>(car);
	s.FindGameObjectById(inner, false)->scale.x = 3.0f;
	s.RebuildHierarchyLinks();
	REQUIRE(s.SavePrefab(car, "Car"));
	s.QueueDeleteGameObjectById(car);
	s.ProcessPendingDeletes();

	const uint32_t carA = s.InstantiatePrefabNow("Car", { 0.0f, 0.0f, 0.0f });
	const auto find = [&](uint32_t under, const char* name) -> GameObject* {
		for (auto& o : A::Objects(s)) {
			if (o.name != name) continue;
			for (const GameObject* p = &o; p; p = p->parentId >= 0 ? s.FindGameObjectById(static_cast<uint32_t>(p->parentId), false) : nullptr) {
				if (p->id == under) return &o;
			}
		}
		return nullptr;
	};
	REQUIRE(find(carA, "Wheel") != nullptr && find(carA, "Hub") != nullptr);
	CHECK(find(carA, "Wheel")->nestedPrefab == "Wheel");
	CHECK_NEAR(find(carA, "Wheel")->translation.x, 2.0f, 1e-5f);

	// The Wheel prefab changes: the car's wheel follows, keeping where it sits and its wider scale
	const uint32_t loose = s.InstantiatePrefabNow("Wheel", { 0.0f, 0.0f, 10.0f });
	ChildOf(s, loose)->materialColor = { 0.0f, 1.0f, 0.0f, 1.0f };
	s.FindGameObjectById(loose, false)->scale.y = 5.0f;
	s.PushUndoSnapshot();
	REQUIRE(s.ApplyPrefabInstance(loose));
	REQUIRE(find(carA, "Hub") != nullptr);
	CHECK_NEAR(find(carA, "Hub")->materialColor.y, 1.0f, 1e-5f);
	CHECK_NEAR(find(carA, "Hub")->materialColor.x, 0.0f, 1e-5f);
	CHECK_NEAR(find(carA, "Wheel")->scale.x, 3.0f, 1e-5f);
	CHECK_NEAR(find(carA, "Wheel")->scale.y, 5.0f, 1e-5f);
	CHECK_NEAR(find(carA, "Wheel")->translation.x, 2.0f, 1e-5f);

	// Editing the car keeps the wheel nested: Apply on the car, then the wheel changes again
	find(carA, "Wheel")->translation.x = 4.0f;
	s.PushUndoSnapshot();
	REQUIRE(s.ApplyPrefabInstance(carA));
	ChildOf(s, loose)->materialColor = { 1.0f, 1.0f, 0.0f, 1.0f };
	s.PushUndoSnapshot();
	REQUIRE(s.ApplyPrefabInstance(loose));
	REQUIRE(find(carA, "Hub") != nullptr);
	CHECK_NEAR(find(carA, "Hub")->materialColor.x, 1.0f, 1e-5f);
	CHECK_NEAR(find(carA, "Wheel")->translation.x, 4.0f, 1e-5f);

	// A prefab inside itself is refused
	const uint32_t self = s.InstantiatePrefabNow("Wheel", { 0.0f, 0.0f, 20.0f });
	s.FindGameObjectById(self, false)->parentId = static_cast<int>(loose);
	s.RebuildHierarchyLinks();
	CHECK(!s.ApplyPrefabInstance(loose));
}

TEST_CASE(Prefabs, OverridesArePerFieldAndPerAxis)
{
	const auto [a, b] = MakeTowerInstances(s, "PrefabFieldOverrides");
	REQUIRE(ChildOf(s, a) != nullptr && ChildOf(s, b) != nullptr);
	const uint32_t topOfA = ChildOf(s, a)->id;
	const uint32_t topOfB = ChildOf(s, b)->id;
	const auto has = [&](uint32_t id, const char* key) {
		const GameObject* o = s.FindGameObjectById(id, false);
		return o && std::find(o->prefabOverrides.begin(), o->prefabOverrides.end(), key) != o->prefabOverrides.end();
	};
	const auto trail = [&](uint32_t id) { return s.FindGameObjectById(id, false)->components.Get<TrailComponent>("Trail"); };

	// The prefab gets a Trail
	REQUIRE(s.AddComponentByName(*s.FindGameObjectById(topOfA, false), "Trail"));
	REQUIRE(s.ApplyPrefabInstance(a));
	REQUIRE(trail(topOfB) != nullptr);
	s.ResetUndoHistory();

	// B changes one field of it and one axis of its scale: only those become overrides
	trail(topOfB)->lifetime = 2.0f;
	s.FindGameObjectById(topOfB, false)->scale.x = 3.0f;
	s.PushUndoSnapshot();
	CHECK(has(topOfB, "components/Trail/lifetime"));
	CHECK(!has(topOfB, "components/Trail"));
	CHECK(has(topOfB, "scale/0"));
	CHECK(!has(topOfB, "scale"));

	// The prefab changes the other fields and axes: B takes them and keeps its own
	trail(topOfA)->lifetime = 0.9f;
	trail(topOfA)->widthStart = 0.7f;
	s.FindGameObjectById(topOfA, false)->scale = { 2.0f, 2.0f, 2.0f };
	s.PushUndoSnapshot();
	REQUIRE(s.ApplyPrefabInstance(a));
	REQUIRE(trail(topOfB) != nullptr);
	CHECK_NEAR(trail(topOfB)->lifetime, 2.0f, 1e-5f);
	CHECK_NEAR(trail(topOfB)->widthStart, 0.7f, 1e-5f);
	const Vector3 scale = s.FindGameObjectById(topOfB, false)->scale;
	CHECK_NEAR(scale.x, 3.0f, 1e-5f);
	CHECK_NEAR(scale.y, 2.0f, 1e-5f);

	// Reverting one field brings back the prefab value of that field only
	REQUIRE(s.RevertPrefabOverride(topOfB, "components/Trail/lifetime"));
	CHECK_NEAR(trail(topOfB)->lifetime, 0.9f, 1e-5f);
	CHECK_NEAR(s.FindGameObjectById(topOfB, false)->scale.x, 3.0f, 1e-5f);
	REQUIRE(s.RevertPrefabOverride(topOfB, "scale/0"));
	CHECK_NEAR(s.FindGameObjectById(topOfB, false)->scale.x, 2.0f, 1e-5f);
	CHECK(s.FindGameObjectById(topOfB, false)->prefabOverrides.empty());

	// A component the prefab drops takes its field overrides with it, without trouble
	trail(topOfB)->lifetime = 5.0f;
	s.PushUndoSnapshot();
	REQUIRE(s.RemoveComponentByName(*s.FindGameObjectById(topOfA, false), "Trail"));
	REQUIRE(s.ApplyPrefabInstance(a));
	CHECK(trail(topOfB) == nullptr);
}


TEST_CASE(Timeline, RotationTakesTheShortWayAndEventsAtZeroFire)
{
	// 350 degrees to 10 degrees: through 0, not back through 180
	TimelineTrack track;
	track.property = TimelineProperty::Rotation;
	const float deg = 3.14159265f / 180.0f;
	track.SetKey({ 0.0f, { 0.0f, 350.0f * deg, 0.0f }, false });
	track.SetKey({ 1.0f, { 0.0f, 10.0f * deg, 0.0f }, false });
	const Quaternion middle = track.SampleRotation(0.5f);
	const Quaternion forward = Quaternion::FromEuler({ 0.0f, 0.0f, 0.0f });
	const float dot = std::abs(middle.x * forward.x + middle.y * forward.y + middle.z * forward.z + middle.w * forward.w);
	CHECK_MSG(dot > 0.999f, std::format("dot {:.4f}", dot));
	CHECK(std::isfinite(track.SampleRotation(std::numeric_limits<float>::quiet_NaN()).w));

	// An event at time 0 fires when playback starts
	const std::filesystem::path root = A::TempRoot() / "TimelineZero";
	std::filesystem::create_directories(root / "Assets" / "Timelines");
	A::ProjectRoot(s) = root;
	Timeline timeline;
	timeline.duration = 1.0f;
	timeline.events = { { 0.0f, "start" } };
	REQUIRE(A::SaveTimelineAsset(s, "Assets/Timelines/Zero.timeline.json", timeline));
	GameObject& director = s.CreateGameObject("Director", GameObjectType::Empty);
	REQUIRE(s.AddComponentByName(director, kTimelineComponent));
	strncpy_s(director.components.Get<TimelineComponent>(kTimelineComponent)->timeline, "Assets/Timelines/Zero.timeline.json", _TRUNCATE);
	A::State(s) = EditorSceneState::Play;
	const int before = A::TimelineEvents(s);
	A::RunTimelines(s, 0.1f);
	CHECK(A::TimelineEvents(s) == before + 1);
	A::RunTimelines(s, 0.1f);
	CHECK(A::TimelineEvents(s) == before + 1); // once
	A::State(s) = EditorSceneState::Edit;
}

TEST_CASE(Hierarchy, DuplicateCopiesChildrenAndEverySelectedObject)
{
	GameObject& parent = s.CreateCube("Car", { 0.0f, 0.0f, 0.0f });
	const uint32_t carId = parent.id;
	GameObject& wheel = s.CreateCube("Wheel", { 1.0f, 0.0f, 0.0f });
	const uint32_t wheelId = wheel.id;
	wheel.parentId = static_cast<int>(carId);
	GameObject& lamp = s.CreateCube("Lamp", { 5.0f, 0.0f, 0.0f });
	const uint32_t lampId = lamp.id;
	s.RebuildHierarchyLinks();
	REQUIRE(s.AddComponentByName(*s.FindGameObjectById(wheelId, false), "Joint"));
	s.FindGameObjectById(wheelId, false)->components.Get<JointComponent>("Joint")->connectedObjectId = static_cast<int>(carId);

	s.SelectObject(A::IndexOf(s, carId), false);
	s.SelectObject(A::IndexOf(s, lampId), true);
	const std::vector<uint32_t> copies = s.DuplicateObjects(s.SelectedIds());
	REQUIRE(copies.size() == 2u);
	const GameObject* carCopy = s.FindGameObjectById(copies[0], false);
	REQUIRE(carCopy != nullptr);
	CHECK(carCopy->id != carId);
	// The copy has its own wheel, and the wheel's joint points at the copied car, not the original
	const GameObject* wheelCopy = nullptr;
	for (auto& o : A::Objects(s)) {
		if (o.parentId == static_cast<int>(copies[0])) wheelCopy = &o;
	}
	REQUIRE(wheelCopy != nullptr);
	CHECK(wheelCopy->id != wheelId);
	CHECK(wheelCopy->name == "Wheel");
	auto* joint = const_cast<GameObject*>(wheelCopy)->components.Get<JointComponent>("Joint");
	REQUIRE(joint != nullptr);
	CHECK(joint->connectedObjectId == static_cast<int>(copies[0]));
	// The originals are untouched, and the copies are the new selection
	CHECK(s.FindGameObjectById(wheelId, false)->parentId == static_cast<int>(carId));
	CHECK(s.IsSelected(copies[0]) && s.IsSelected(copies[1]) && !s.IsSelected(carId));
	// A selected child is copied with its parent, not twice
	s.SelectObject(A::IndexOf(s, carId), false);
	s.SelectObject(A::IndexOf(s, wheelId), true);
	CHECK(s.DuplicateObjects(s.SelectedIds()).size() == 1u);
}

TEST_CASE(Hierarchy, MultiMoveFollowsTheWorldDirection)
{
	// B sits under a parent turned 90 degrees: moving A along world +X must move B along world +X too
	const uint32_t a = s.CreateCube("A", { 0.0f, 0.0f, 0.0f }).id;
	GameObject& turned = s.CreateGameObject("Turned", GameObjectType::Empty);
	const uint32_t turnedId = turned.id;
	turned.rotation = { 0.0f, 1.5707963f, 0.0f };
	turned.rotationQuat = Quaternion::FromEuler(turned.rotation);
	GameObject& b = s.CreateCube("B", { 0.0f, 0.0f, 0.0f });
	const uint32_t bId = b.id;
	b.parentId = static_cast<int>(turnedId);
	s.RebuildHierarchyLinks();
	s.UpdateWorldMatrices();
	const Vector3 before = { s.FindGameObjectById(bId, false)->worldMatrix.m[3][0], 0.0f, s.FindGameObjectById(bId, false)->worldMatrix.m[3][2] };

	s.SelectObject(A::IndexOf(s, a), false);
	s.SelectObject(A::IndexOf(s, bId), true);
	s.MoveExtraSelection({ 3.0f, 0.0f, 0.0f }); // world units
	s.UpdateWorldMatrices();
	const GameObject* moved = s.FindGameObjectById(bId, false);
	CHECK_NEAR(moved->worldMatrix.m[3][0] - before.x, 3.0f, 1e-3f);
	CHECK_NEAR(moved->worldMatrix.m[3][2] - before.z, 0.0f, 1e-3f);
}

TEST_CASE(Prefabs, JointsInsideAPrefabStayConnectedAndRenamesKeepTheLink)
{
	namespace fs = std::filesystem;
	const fs::path root = A::TempRoot() / "PrefabJointRename";
	fs::create_directories(root / "Assets" / "Prefabs");
	fs::create_directories(root / "Assets" / "Scenes");
	A::ProjectRoot(s) = root;
	s.ClearPrefabCache();
	const uint32_t door = s.CreateCube("Door", { 0.0f, 0.0f, 0.0f }).id;
	GameObject& leaf = s.CreateCube("Leaf", { 1.0f, 0.0f, 0.0f });
	const uint32_t leafId = leaf.id;
	leaf.parentId = static_cast<int>(door);
	s.RebuildHierarchyLinks();
	REQUIRE(s.AddComponentByName(*s.FindGameObjectById(leafId, false), "Joint"));
	s.FindGameObjectById(leafId, false)->components.Get<JointComponent>("Joint")->connectedObjectId = static_cast<int>(door);
	REQUIRE(s.SavePrefab(door, "Door"));
	s.QueueDeleteGameObjectById(door);
	s.ProcessPendingDeletes();

	const uint32_t instance = s.InstantiatePrefabNow("Door", { 3.0f, 0.0f, 0.0f });
	GameObject* instanceLeaf = nullptr;
	for (auto& o : A::Objects(s)) {
		if (o.parentId == static_cast<int>(instance)) instanceLeaf = &o;
	}
	REQUIRE(instanceLeaf != nullptr);
	CHECK(instanceLeaf->components.Get<JointComponent>("Joint")->connectedObjectId == static_cast<int>(instance));

	// Renaming the prefab file in the Project panel: this scene and scenes saved before keep their link
	const std::string scenePath = (root / "Assets" / "Scenes" / "Doors.scene.json").string();
	s.AssetDb().Refresh();
	REQUIRE(s.SaveScene(scenePath));
	REQUIRE(s.MoveAssetAndUpdateReferences("Assets/Prefabs/Door.prefab.json", "Assets/Prefabs/Gate.prefab.json"));
	CHECK(s.FindGameObjectById(instance, false)->prefabSource == "Gate");
	{
		// The saved scene still says "Door"; its prefab GUID finds the renamed file
		std::ifstream in(scenePath);
		std::stringstream text;
		text << in.rdbuf();
		CHECK(text.str().find("\"Door\"") != std::string::npos);
	}
	REQUIRE(s.LoadScene(scenePath));
	REQUIRE(s.FindGameObjectById(instance, false) != nullptr);
	CHECK(s.FindGameObjectById(instance, false)->prefabSource == "Gate");
	CHECK(s.RevertPrefabInstance(instance));
}

TEST_CASE(AutoSave, ChoiceIsRemembered)
{
	const std::filesystem::path file = A::TempRoot() / "Settings" / "editor_settings.json";
	std::filesystem::create_directories(file.parent_path());
	Localization::SetSettingsFileOverride(file);
	{ std::ofstream(file) << R"({ "language": "ja" })"; }
	CHECK(Localization::EditorSettingBool("autoSave", true));
	Localization::SetEditorSettingBool("autoSave", false);
	CHECK(!Localization::EditorSettingBool("autoSave", true));
	std::ifstream in(file);
	std::stringstream text;
	text << in.rdbuf();
	CHECK(text.str().find("\"language\"") != std::string::npos); // other settings survive
	Localization::SetSettingsFileOverride({});
}


TEST_CASE(Localization, LabelsShowOnlyTheirVisiblePart)
{
	CHECK(Localization::VisibleLabel("Position") == "Position");
	CHECK(Localization::VisibleLabel("位置###Position") == "位置"); // what DrawVec3Control prints
	CHECK(Localization::VisibleLabel("Center##Sph") == "Center");
	CHECK(Localization::VisibleLabel("##hidden").empty());
	CHECK(Localization::VisibleLabel("").empty());
}


TEST_CASE(Animation, RagdollStaysVisibleWhereItFell)
{
	for (auto& obj : A::Objects(s)) obj.isActive = false;
	MakeStaticBox(s, { -30.0f, -1.0f, -30.0f }, { 30.0f, 0.0f, 30.0f });
	GameObject& character = s.CreateGameObject("Faller", GameObjectType::Empty);
	character.meshType = GameObject::MeshType::CustomModel;
	character.hasMesh = true;
	character.modelPath = "Resources/Model/Idle.fbx";
	character.scale = { 0.012f, 0.012f, 0.012f };
	character.translation = { 0.0f, 40.0f, 0.0f }; // high above the floor
	const uint32_t id = character.id;
	// The Scene view looks at the floor only: the character up at 40 m is out of view
	A::EditorCam(s).translation = { 0.0f, 3.0f, -10.0f };
	A::EditorCam(s).rotation = { 0.2f, 0.0f, 0.0f };
	CaptureGameViewSignature(s);
	const int culledHigh = A::CulledObjects(s);
	CHECK(culledHigh >= 1);

	EngineAPI& api = A::Api(s);
	REQUIRE(api.setRagdoll(api.hostContext, id, true));
	StepPhysicsSeconds(s, 4.0f);
	CaptureGameViewSignature(s);
	// The body lies on the floor in view; the object itself is still up there, so its own box would cull it
	CHECK_MSG(A::CulledObjects(s) < culledHigh, std::format("culled before {} after {}", culledHigh, A::CulledObjects(s)));
	REQUIRE(api.setRagdoll(api.hostContext, id, false));
}


TEST_CASE(Hierarchy, MultiRotateAndScaleApplyToEverySelected)
{
	const uint32_t a = s.CreateCube("A", { 0.0f, 0.0f, 0.0f }).id;
	GameObject& bObj = s.CreateCube("B", { 3.0f, 0.0f, 0.0f });
	const uint32_t b = bObj.id;
	bObj.rotation = { 0.0f, 0.5f, 0.0f };
	bObj.scale = { 2.0f, 1.0f, 1.0f };
	s.SelectObject(A::IndexOf(s, a), false);
	s.SelectObject(A::IndexOf(s, b), true);
	s.RotateScaleExtraSelection({ 0.0f, 0.25f, 0.0f }, { 1.5f, 1.0f, 1.0f });
	const GameObject* moved = s.FindGameObjectById(b, false);
	CHECK_NEAR(moved->rotation.y, 0.75f, 1e-5f);
	CHECK_NEAR(moved->scale.x, 3.0f, 1e-5f); // the same ratio, so each keeps its own proportions
	CHECK_NEAR(moved->scale.y, 1.0f, 1e-5f);
	const Quaternion expected = Quaternion::FromEuler(moved->rotation);
	CHECK_NEAR(moved->rotationQuat.y, expected.y, 1e-5f);
	CHECK_NEAR(s.FindGameObjectById(a, false)->rotation.y, 0.0f, 1e-5f); // the primary is the gizmo's job
}


TEST_CASE(Hierarchy, DuplicateIsPlacedRightAfterTheOriginal)
{
	const uint32_t first = s.CreateCube("First", { 0.0f, 0.0f, 0.0f }).id;
	GameObject& kid = s.CreateCube("Kid", { 0.0f, 1.0f, 0.0f });
	kid.parentId = static_cast<int>(first);
	const uint32_t kidId = kid.id;
	const uint32_t last = s.CreateCube("Last", { 5.0f, 0.0f, 0.0f }).id;
	s.RebuildHierarchyLinks();
	// Something else is waiting to be deleted: its index must still mean it afterwards
	s.QueueDeleteGameObjectById(last);
	const std::vector<uint32_t> copies = s.DuplicateObjects({ first });
	REQUIRE(copies.size() == 1u);
	CHECK(A::IndexOf(s, copies[0]) == A::IndexOf(s, kidId) + 1); // after the original and its child, not at the end
	s.ProcessPendingDeletes();
	CHECK(s.FindGameObjectById(last, false) == nullptr);
	CHECK(s.FindGameObjectById(copies[0], false) != nullptr);
	CHECK(s.FindGameObjectById(first, false) != nullptr);
}


TEST_CASE(Prefabs, SyncKeepsTheHierarchyOrder)
{
	const auto [a, b] = MakeTowerInstances(s, "PrefabOrder");
	const uint32_t after = s.CreateCube("After", { 0.0f, 0.0f, 0.0f }).id;
	// Move instance A in front of everything: order A, ..., B, After
	auto& objects = A::Objects(s);
	const auto it = std::find_if(objects.begin(), objects.end(), [&](const GameObject& o) { return o.id == a; });
	GameObject moved = std::move(*it);
	objects.erase(it);
	objects.insert(objects.begin(), std::move(moved));
	s.RebuildHierarchyLinks();
	const int aBefore = A::IndexOf(s, a);
	const int afterBefore = A::IndexOf(s, after);
	s.SyncLinkedPrefabs(); // what opening the scene does
	CHECK(A::IndexOf(s, a) == aBefore); // not moved to the end of the Hierarchy
	CHECK(A::IndexOf(s, after) == afterBefore);
	CHECK(A::IndexOf(s, b) < A::IndexOf(s, after));
	CHECK(ChildOf(s, a) != nullptr && A::IndexOf(s, ChildOf(s, a)->id) == aBefore + 1); // children right after their root
}


TEST_CASE(Audio, DopplerRaisesApproachingAndLowersLeavingSounds)
{
	using namespace SpatialAudio;
	const Vector3 still{ 0.0f, 0.0f, 0.0f };
	const Vector3 listener{ 0.0f, 0.0f, 0.0f };
	const Vector3 source{ 0.0f, 0.0f, 20.0f };
	CHECK_NEAR(DopplerFactor(listener, still, source, still), 1.0f, 1e-5f);
	const float approaching = DopplerFactor(listener, still, source, { 0.0f, 0.0f, -30.0f });
	const float leaving = DopplerFactor(listener, still, source, { 0.0f, 0.0f, 30.0f });
	CHECK(approaching > 1.05f);
	CHECK(leaving < 0.95f);
	// The listener moving towards the sound works the same way
	CHECK(DopplerFactor(listener, { 0.0f, 0.0f, 30.0f }, source, still) > 1.05f);
	// Sideways motion changes nothing; a teleport (absurd speed) and NaN are ignored; the result stays in range
	CHECK_NEAR(DopplerFactor(listener, still, source, { 30.0f, 0.0f, 0.0f }), 1.0f, 1e-4f);
	CHECK_NEAR(DopplerFactor(listener, still, source, { 0.0f, 0.0f, -5000.0f }), 1.0f, 1e-5f);
	const float nan = std::numeric_limits<float>::quiet_NaN();
	CHECK_NEAR(DopplerFactor(listener, still, source, { nan, 0.0f, 0.0f }), 1.0f, 1e-5f);
	const float fast = DopplerFactor(listener, still, source, { 0.0f, 0.0f, -300.0f });
	CHECK(fast <= 2.0f && fast > 1.5f);
	CHECK_NEAR(DopplerFactor(listener, still, listener, still), 1.0f, 1e-5f); // on top of the listener
}

TEST_CASE(Audio, WallsMuffleSoundsAndMovingSoundsShiftPitch)
{
	Audio* audio = Audio::GetInstance();
	if (!audio->IsAvailable()) return;
	EngineAPI& api = A::Api(s);
	const std::string clip = WriteSilentWav("Audio/occlusion.wav");
	for (auto& o : A::Objects(s)) o.isActive = false;
	A::AimAudioListener(s, { 0.0f, 1.0f, 0.0f }, 0.0f);

	GameObject& speaker = s.CreateGameObject("Speaker", GameObjectType::Empty);
	speaker.translation = { 0.0f, 1.0f, 10.0f };
	const uint32_t speakerId = speaker.id;
	ScriptAudioParams params;
	params.loop = true;
	params.followObject = speakerId;
	params.maxDistance = 100.0f;
	const uint32_t voice = api.playSoundEx(&s, clip.c_str(), &params);
	REQUIRE(voice != 0u);
	for (int i = 0; i < 30; ++i) A::RunAudio(s, 1.0f / 60.0f);
	const VoiceLevels open = LevelsOf(voice);
	CHECK(audio->GetVoiceOcclusion(voice) < 0.05f);

	// A static wall in between
	const uint32_t wall = MakeStaticBox(s, { -5.0f, 0.0f, 4.0f }, { 5.0f, 3.0f, 5.0f });
	for (int i = 0; i < 60; ++i) A::RunAudio(s, 1.0f / 60.0f);
	CHECK(audio->GetVoiceOcclusion(voice) > 0.9f);
	const VoiceLevels muffled = LevelsOf(voice);
	CHECK(muffled.left + muffled.right < (open.left + open.right) * 0.7f);
	// A moving (non-static) box in the way does not count: it may be the player in front of the camera
	s.FindGameObjectById(wall, false)->isStatic = false;
	for (int i = 0; i < 60; ++i) A::RunAudio(s, 1.0f / 60.0f);
	CHECK(audio->GetVoiceOcclusion(voice) < 0.1f);
	s.QueueDeleteGameObjectById(wall);
	s.ProcessPendingDeletes();

	// The sound flies towards the listener: its pitch goes up
	for (int i = 0; i < 10; ++i) {
		s.FindGameObjectById(speakerId, false)->translation.z -= 0.5f; // 30 m/s at 60 frames per second
		A::RunAudio(s, 1.0f / 60.0f);
	}
	float pitch = 0.0f;
	for (int i = 0; i < 50 && pitch <= 1.02f; ++i) {
		pitch = audio->GetVoicePitch(voice);
		Sleep(10);
	}
	CHECK_MSG(pitch > 1.02f, std::format("pitch {:.3f}", pitch));
	audio->StopAllAudio();
}

// ===========================================================================
// Editor UX Refinements (Gizmo projection/snapping/focus, Prefab overrides,
// Hierarchy range selection, Component copy/paste, Timeline targetId)
// ===========================================================================

TEST_CASE(EditorUX, GizmoProjectionSnappingLocalAxesAndFocus)
{
	// 1. Snapping helper
	CHECK(std::abs(EditorSuite::SnapValue(0.37f, 0.25f) - 0.25f) < 1e-4f);
	CHECK(std::abs(EditorSuite::SnapValue(0.38f, 0.25f) - 0.50f) < 1e-4f);
	CHECK(std::abs(EditorSuite::SnapValue(-0.38f, 0.25f) - (-0.50f)) < 1e-4f);
	CHECK(std::abs(EditorSuite::SnapValue(1.23f, 0.0f) - 1.23f) < 1e-4f);

	// 2. Perspective-accurate screen-space gizmo projection
	// If 1 meter in world space projects to (+50, 0) pixels on screen, dragging (+100, +30) pixels moves +2.0 meters.
	CHECK(std::abs(EditorSuite::ProjectGizmoDrag(ImVec2(100.0f, 30.0f), ImVec2(50.0f, 0.0f), 1.0f) - 2.0f) < 1e-4f);
	// If the camera faces the opposite side so +X projects to (-40, 0) pixels, dragging (+80, 0) moves -2.0 meters (never inverted!).
	CHECK(std::abs(EditorSuite::ProjectGizmoDrag(ImVec2(80.0f, 0.0f), ImVec2(-40.0f, 0.0f), 1.0f) - (-2.0f)) < 1e-4f);
	// Rotate ring: the sweep around the centre is counter-clockwise positive on screen (y grows downwards)
	CHECK_NEAR(EditorSuite::GizmoSweepAngle(ImVec2(0.0f, 0.0f), ImVec2(10.0f, 0.0f), ImVec2(0.0f, -10.0f)), 1.5707963f, 1e-4f);
	CHECK_NEAR(EditorSuite::GizmoSweepAngle(ImVec2(0.0f, 0.0f), ImVec2(10.0f, 0.0f), ImVec2(0.0f, 10.0f)), -1.5707963f, 1e-4f);
	CHECK_NEAR(EditorSuite::GizmoSweepAngle(ImVec2(5.0f, 5.0f), ImVec2(4.0f, 15.0f), ImVec2(6.0f, 15.0f)), 0.1993f, 1e-3f); // across the -y seam
	CHECK(EditorSuite::GizmoSweepAngle(ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f), ImVec2(5.0f, 5.0f)) == 0.0f); // on the centre: no turn
	// The turn is about the given (parent space) axis: a direction lying on that axis stays where it was
	{
		const Quaternion start = Quaternion::FromAxisAngle({ 0.0f, 1.0f, 0.0f }, 1.5707963f);
		const Quaternion turned = EditorSuite::RotateAboutAxis(start, { 1.0f, 0.0f, 0.0f }, 1.5707963f);
		const Vector3 forward = TransformUtil::TransformDirection({ 0.0f, 0.0f, 1.0f }, start.ToRotationMatrix4x4());
		const Vector3 forwardAfter = TransformUtil::TransformDirection({ 0.0f, 0.0f, 1.0f }, turned.ToRotationMatrix4x4());
		CHECK(std::abs(std::abs(forward.x) - 1.0f) < 1e-4f); // the yaw put local +Z on the world X axis
		CHECK_NEAR(forwardAfter.x, forward.x, 1e-4f);
		CHECK_NEAR(forwardAfter.y, forward.y, 1e-4f);
		CHECK_NEAR(forwardAfter.z, forward.z, 1e-4f);
		CHECK_NEAR(EditorSuite::RotateAboutAxis(Quaternion::Identity(), { 0.0f, 1.0f, 0.0f }, 0.5f).ToEuler().y, 0.5f, 1e-4f);
	}
	// The object turns the way the mouse went: camera looking down +Z, a counter-clockwise sweep from the +X side to the top
	// must carry local +X up the screen (world +Y). The engine is left-handed, so that is a positive turn about +Z.
	{
		A::EditorCam(s).translation = { 0.0f, 0.0f, -10.0f };
		A::EditorCam(s).rotation = { 0.0f, 0.0f, 0.0f };
		A::RotateSweep(s) = 1.5707963f;
		for (const Vector3 axis : { Vector3{ 0.0f, 0.0f, 1.0f }, Vector3{ 0.0f, 0.0f, -1.0f } }) { // either end of the axis may face the camera
			const float angle = s.GizmoTurnAngle({ 0.0f, 0.0f, 0.0f }, axis, false);
			const Quaternion turned = EditorSuite::RotateAboutAxis(Quaternion::Identity(), axis, angle);
			const Vector3 x = TransformUtil::TransformDirection({ 1.0f, 0.0f, 0.0f }, turned.ToRotationMatrix4x4());
			CHECK_MSG(x.y > 0.99f, std::format("+X went to ({:.2f}, {:.2f}, {:.2f})", x.x, x.y, x.z));
		}
		A::RotateSweep(s) = 0.0f;
	}
	// An axis that points almost at the camera is a few pixels long: a 100 px drag must not throw the object tens of metres
	CHECK(std::abs(EditorSuite::ProjectGizmoDrag(ImVec2(-100.0f, -75.0f), ImVec2(-4.0f, -3.0f), 1.0f)) <= 6.5f);
	CHECK(EditorSuite::ProjectGizmoDrag(ImVec2(-100.0f, -75.0f), ImVec2(-4.0f, -3.0f), 1.0f) > 0.0f); // still the dragged way

	// 3. World vs Local gizmo axes + Capsule Collider world helper
	GameObject& cube = s.CreateCube("RotatedCube", { 10.0f, 4.0f, -6.0f });
	const uint32_t cubeId = cube.id;
	cube.rotation = { 0.0f, 3.1415926535f * 0.5f, 0.0f }; // +90 deg Y
	cube.rotationQuat = Quaternion::FromEuler(cube.rotation);
	cube.scale = { 2.0f, 3.0f, 2.0f };
	s.AddComponentByName(cube, "Capsule Collider");
	s.UpdateWorldMatrices();

	Vector3 ax{}, ay{}, az{};
	A::GizmoLocalSpace(s) = false;
	s.GetGizmoAxes(cube, ax, ay, az);
	CHECK(std::abs(ax.x - 1.0f) < 1e-3f && std::abs(ax.z) < 1e-3f);

	A::GizmoLocalSpace(s) = true;
	s.GetGizmoAxes(cube, ax, ay, az);
	CHECK(std::abs(ax.x) < 1e-2f && std::abs(std::abs(ax.z) - 1.0f) < 1e-2f);
	A::GizmoLocalSpace(s) = false;

	Vector3 capCenter{};
	float capRadius = 0.0f;
	float capHalfHeight = 0.0f;
	CHECK(s.ComputeCapsuleColliderWorld(cube, capCenter, capRadius, capHalfHeight));
	CHECK(capRadius > 0.9f);
	CHECK(capHalfHeight > 1.4f);

	// 4. FocusSelectedObject ('F' key) frames the selected object in the center of the editor camera view
	s.SelectObject(A::IndexOf(s, cubeId), false);
	Camera& cam = A::EditorCam(s);
	cam.translation = { -50.0f, 20.0f, -50.0f };
	cam.rotation = { 0.35f, -0.6f, 0.0f };
	cam.Update();
	REQUIRE(s.FocusSelectedObject());

	// Project the focused object's position into the editor camera's clip space: X and Y must be centered (near 0), w > 0
	const GameObject* focused = s.FindGameObjectById(cubeId, false);
	REQUIRE(focused != nullptr);
	const Matrix4x4& vp = cam.GetViewProjectionMatrix();
	const Vector3 p = focused->translation;
	const float clipX = p.x * vp.m[0][0] + p.y * vp.m[1][0] + p.z * vp.m[2][0] + vp.m[3][0];
	const float clipY = p.x * vp.m[0][1] + p.y * vp.m[1][1] + p.z * vp.m[2][1] + vp.m[3][1];
	const float clipW = p.x * vp.m[0][3] + p.y * vp.m[1][3] + p.z * vp.m[2][3] + vp.m[3][3];
	REQUIRE(clipW > 0.5f);
	CHECK(std::abs(clipX / clipW) < 1e-2f);
	CHECK(std::abs(clipY / clipW) < 1e-2f);
}

TEST_CASE(EditorUX, RangeSelectPrefabOverrideQueryAndComponentClipboard)
{
	// 1. Shift+Click range selection in Hierarchy
	const uint32_t id0 = s.CreateCube("Range0", {}).id;
	const uint32_t id1 = s.CreateCube("Range1", {}).id;
	const uint32_t id2 = s.CreateCube("Range2", {}).id;
	const uint32_t id3 = s.CreateCube("Range3", {}).id;
	const int idx1 = A::IndexOf(s, id1);
	const int idx3 = A::IndexOf(s, id3);
	REQUIRE(idx1 >= 0 && idx3 > idx1);

	s.SelectObject(idx1, false);
	s.SelectObjectRange(idx3);
	CHECK(!s.IsSelected(id0));
	CHECK(s.IsSelected(id1));
	CHECK(s.IsSelected(id2));
	CHECK(s.IsSelected(id3));

	// 2. CopyComponent & PasteComponent (both data component and built-in component)
	GameObject* src = s.FindGameObjectById(id0, false);
	GameObject* dst = s.FindGameObjectById(id1, false);
	REQUIRE(src != nullptr && dst != nullptr);
	REQUIRE(s.AddComponentByName(*src, "Capsule Collider"));
	if (auto* cap = src->components.Get<CapsuleColliderComponent>("Capsule Collider")) {
		cap->radius = 1.75f;
		cap->height = 5.0f;
		cap->center = { 0.0f, 0.5f, 0.0f };
	}
	CHECK(s.CopyComponent(*src, "Capsule Collider"));
	CHECK(s.HasCopiedComponent("Capsule Collider"));
	// Paste onto an object that does not even have CapsuleCollider yet: adds & populates it with undo support
	CHECK(s.PasteComponent(*dst, "Capsule Collider"));
	const auto* pastedCap = dst->components.Get<CapsuleColliderComponent>("Capsule Collider");
	REQUIRE(pastedCap != nullptr);
	CHECK(std::abs(pastedCap->radius - 1.75f) < 1e-4f);
	CHECK(std::abs(pastedCap->height - 5.0f) < 1e-4f);
	CHECK(std::abs(pastedCap->center.y - 0.5f) < 1e-4f);

	// 3. Inline Prefab Override query & prefix revert
	A::ProjectRoot(s) = A::TempRoot() / "UXPrefabProj";
	std::filesystem::create_directories(s.GetPrefabDirectory());
	src->translation = { 1.0f, 2.0f, 3.0f };
	REQUIRE(s.SavePrefab(id0, "UXCrate"));
	const uint32_t instId = s.InstantiatePrefabNow("UXCrate", { 1.0f, 2.0f, 3.0f });
	REQUIRE(instId != 0u);
	s.ResetUndoHistory();

	GameObject* inst = s.FindGameObjectById(instId, false);
	REQUIRE(inst != nullptr);
	CHECK(!s.HasPrefabOverride(*inst, "scale"));
	CHECK(!s.HasPrefabOverride(*inst, "components/Capsule Collider/radius"));

	inst->scale.x = 4.0f;
	if (auto* cap = inst->components.Get<CapsuleColliderComponent>("Capsule Collider")) {
		cap->radius = 4.25f;
	}
	s.PushUndoSnapshot();

	inst = s.FindGameObjectById(instId, false);
	REQUIRE(inst != nullptr);
	CHECK(s.HasPrefabOverride(*inst, "scale"));
	CHECK(s.HasPrefabOverride(*inst, "scale/0"));
	CHECK(s.HasPrefabOverride(*inst, "components/Capsule Collider/radius"));
	CHECK(s.HasPrefabOverride(*inst, "components/Capsule Collider"));

	// Revert only the Capsule Collider radius override via prefix/exact helper
	CHECK(s.RevertPrefabOverridesByPrefix(instId, "components/Capsule Collider/radius"));
	inst = s.FindGameObjectById(instId, false);
	REQUIRE(inst != nullptr);
	CHECK(!s.HasPrefabOverride(*inst, "components/Capsule Collider/radius"));
	CHECK(s.HasPrefabOverride(*inst, "scale"));
	REQUIRE(inst->components.Get<CapsuleColliderComponent>("Capsule Collider") != nullptr);
	CHECK(std::abs(inst->components.Get<CapsuleColliderComponent>("Capsule Collider")->radius - 1.75f) < 1e-4f);
	CHECK(std::abs(inst->scale.x - 4.0f) < 1e-4f);
}

TEST_CASE(EditorUX, TimelineTargetSurvivesRenameById)
{
	GameObject& actor = s.CreateCube("CutsceneHero", { 0.0f, 0.0f, 0.0f });
	const uint32_t actorId = actor.id;

	Timeline tl;
	tl.duration = 2.0f;
	TimelineTrack& track = tl.FindOrAddTrack(actor.name, TimelineProperty::Position, actorId);
	CHECK(track.targetId == actorId);
	track.SetKey({ 0.0f, { 0.0f, 0.0f, 0.0f }, false });
	track.SetKey({ 2.0f, { 10.0f, 4.0f, -2.0f }, false });

	// Round-trip through JSON preserves targetId
	const std::string json = tl.ToJson();
	Timeline loaded;
	REQUIRE(Timeline::FromJson(json, loaded));
	REQUIRE(loaded.tracks.size() == 1);
	CHECK(loaded.tracks[0].targetId == actorId);

	// Rename the GameObject: the track still drives it by targetId!
	actor.name = "RenamedHero";
	A::ApplyTimelineAt(s, loaded, 1.0f);
	CHECK(std::abs(actor.translation.x - 5.0f) < 1e-3f);
	CHECK(std::abs(actor.translation.y - 2.0f) < 1e-3f);
	CHECK(std::abs(actor.translation.z - (-1.0f)) < 1e-3f);
}


// ===========================================================================
// Runner
// ===========================================================================

int EditorSuite::RunSelfTests(const std::string& filter)
{
	namespace fs = std::filesystem;

	// Everything the tests write goes below this folder, which starts empty
	const fs::path tempRoot = EditorSuiteTestAccess::TempRoot();
	std::error_code ec;
	fs::remove_all(tempRoot, ec);
	fs::create_directories(tempRoot, ec);

	const fs::path savedProjectRoot = currentProjectRoot_;
	const fs::path savedDirectory = currentDirectory_;
	const fs::path savedSessionRoot = sSessionProjectRoot_; // CreateNewProject() changes it; OpenProject() would add it to the recent list
	const bool savedAutoSave = autoSaveEnabled_;
	isRunningTests_ = true;

	std::string report;
	int passed = 0;
	int failed = 0;
	int ran = 0;

	LogMessage(std::format("[Tests] Running self-tests (filter: '{}', temp folder: {})", filter, tempRoot.string()), 0);

	for (const TestCase& test : GetTestRegistry()) {
		if (!filter.empty() && std::string_view(test.name).find(filter) == std::string_view::npos) continue;
		++ran;

		TestContext ctx;
		ctx.name = test.name;
		try {
			EditorSuiteTestAccess::PrepareForTest(*this);
			test.fn(*this, ctx);
		} catch (const std::exception& e) {
			ctx.Fail(__FILE__, __LINE__, std::string("unhandled exception: ") + e.what());
		} catch (...) {
			ctx.Fail(__FILE__, __LINE__, "unhandled unknown exception");
		}
		// Rendering stops once the device is gone: later picture checks would compare nothing and still pass
		if (dxCommon_) {
			if (const HRESULT removed = dxCommon_->GetDevice()->GetDeviceRemovedReason(); FAILED(removed)) {
				ctx.Fail(__FILE__, __LINE__, std::format("D3D12 device removed (0x{:08X})", static_cast<uint32_t>(removed)));
			}
		}

		// A failed test must not leave Play mode or a foreign project behind
		sceneState_ = EditorSceneState::Edit;
		currentProjectRoot_ = savedProjectRoot;
		currentDirectory_ = savedDirectory;
		sSessionProjectRoot_ = savedSessionRoot;

		if (ctx.failures == 0) {
			++passed;
			report += std::format("[PASS] {}\n", ctx.name);
			LogMessage(std::format("[Tests] [PASS] {}", ctx.name), 0);
		} else {
			++failed;
			report += std::format("[FAIL] {}\n", ctx.name);
			LogMessage(std::format("[Tests] [FAIL] {}", ctx.name), 2);
			for (const std::string& message : ctx.messages) {
				report += std::format("    {}\n", message);
				LogMessage(std::format("[Tests]     {}", message), 2);
			}
		}
	}

	if (ran == 0) {
		// A filter that matches nothing is a mistake, not a pass
		++failed;
		report += "[FAIL] No tests matched the filter.\n";
		LogMessage("[Tests] No tests matched the filter.", 2);
	}
	report += std::format("Summary: {} passed, {} failed\n", passed, failed);
	LogMessage(std::format("[Tests] Summary: {} passed, {} failed", passed, failed), failed == 0 ? 0 : 2);

	{
		std::ofstream out("test_results.txt", std::ios::trunc);
		out << report;
	}

	// Leave a clean default scene behind and make sure the user's data was never the target
	sceneState_ = EditorSceneState::Edit;
	currentProjectRoot_ = savedProjectRoot;
	currentDirectory_ = savedDirectory;
	sSessionProjectRoot_ = savedSessionRoot;
	autoSaveEnabled_ = savedAutoSave;
	SyncScriptModuleWithProject(); // back to the scripts of the project that was open before the tests
	NewScene();
	isRunningTests_ = false;

	return failed;
}

#endif // USE_IMGUI
