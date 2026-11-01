// Headless self-tests, run with `RizealldaySandbox.exe --run-tests [filter]` (editor builds only).
// They run inside the real editor (real D3D12 device, real EditorSuite) right after initialisation.
// Rules: every file a test writes lives under <temp>/RizealldayTests; the user's project, scenes and
// recent-project list are never touched (EditorSuite::isRunningTests_ blocks AddRecentProject).
#ifdef USE_IMGUI

#include "EditorSuiteInternal.h"
#include "PhysicsComponents.h"
#include "render/LightClusters.h"
#include "base/TextureManager.h"
#include "SceneJson.h"
#include "Tests/TestFramework.h"
#include "base/DirectXCommon.h"
#include "3d/Object3d.h"
#include "3d/AnimationPlayer.h"
#include "EngineUpdate.h"
#include "CharacterController.h"
#include "NavGrid.h"
#include "base/EngineVersion.h"
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
	static RenderTexture& GameViewRT(EditorSuite& s) { return s.gameViewRT_; }
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
	CHECK_MSG(EngineUpdate::ApplyPackage(package, install, &error), error);
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

TEST_CASE(Character, SlidesStepsLandsAndNeverTunnels)
{
	using CharacterController::Box;
	const Box floor{ { -50.0f, -1.0f, -50.0f }, { 50.0f, 0.0f, 50.0f } };
	auto standingAt = [](float x, float z) { return Box{ { x - 0.4f, 0.0f, z - 0.4f }, { x + 0.4f, 1.8f, z + 0.4f } }; };
	const Box wall{ { 2.0f, 0.0f, -10.0f }, { 2.1f, 3.0f, 10.0f } }; // thin wall across X = 2

	// Free move on the floor: full motion, grounded
	auto r = CharacterController::Move(standingAt(0, 0), { 1.0f, 0.0f, 0.5f }, { floor }, 0.3f);
	CHECK_NEAR(r.moved.x, 1.0f, 1e-4f);
	CHECK_NEAR(r.moved.z, 0.5f, 1e-4f);
	CHECK(r.grounded);
	CHECK(!r.hitWall);

	// Diagonal into a wall: stops flush on X, keeps sliding on Z
	r = CharacterController::Move(standingAt(1.0f, 0), { 3.0f, 0.0f, 2.0f }, { floor, wall }, 0.3f);
	CHECK(r.hitWall);
	CHECK_NEAR(1.0f + 0.4f + r.moved.x, 2.0f, 0.01f);
	CHECK_NEAR(r.moved.z, 2.0f, 1e-4f);

	// Very fast motion does not pass through the 10 cm wall
	r = CharacterController::Move(standingAt(0, 0), { 50.0f, 0.0f, 0.0f }, { floor, wall }, 0.3f);
	CHECK(r.hitWall);
	CHECK_MSG(r.moved.x < 1.7f, std::format("moved {:.3f}", r.moved.x));

	// Falling: lands on the floor top, grounded; no tunnelling through it either
	const Box inAir{ { -0.4f, 5.0f, -0.4f }, { 0.4f, 6.8f, 0.4f } };
	r = CharacterController::Move(inAir, { 0.0f, -100.0f, 0.0f }, { floor }, 0.3f);
	CHECK(r.grounded);
	CHECK_NEAR(5.0f + r.moved.y, 0.0f, 0.01f);
	r = CharacterController::Move(inAir, { 0.0f, -1.0f, 0.0f }, { floor }, 0.3f);
	CHECK(!r.grounded); // still in the air

	// Ledges: 0.2 is climbed, 0.5 blocks; nothing is climbed while airborne
	const Box low{ { 1.0f, 0.0f, -5.0f }, { 3.0f, 0.2f, 5.0f } };
	const Box high{ { 1.0f, 0.0f, -5.0f }, { 3.0f, 0.5f, 5.0f } };
	r = CharacterController::Move(standingAt(0, 0), { 1.5f, 0.0f, 0.0f }, { floor, low }, 0.3f);
	CHECK(!r.hitWall);
	CHECK_NEAR(r.moved.x, 1.5f, 1e-3f);
	CHECK_NEAR(r.moved.y, 0.2f, 0.01f);
	CHECK(r.grounded);
	r = CharacterController::Move(standingAt(0, 0), { 1.5f, 0.0f, 0.0f }, { floor, high }, 0.3f);
	CHECK(r.hitWall);
	CHECK_NEAR(r.moved.y, 0.0f, 1e-4f);
	const Box hovering{ { -0.4f, 1.0f, -0.4f }, { 0.4f, 2.8f, 0.4f } };
	const Box crate{ { 1.0f, 0.0f, -5.0f }, { 3.0f, 1.2f, 5.0f } }; // its top is 0.2 above the hovering feet
	r = CharacterController::Move(hovering, { 1.5f, 0.0f, 0.0f }, { crate }, 0.3f);
	CHECK(r.hitWall);

	// Ceiling
	const Box ceiling{ { -5.0f, 2.0f, -5.0f }, { 5.0f, 2.5f, 5.0f } };
	r = CharacterController::Move(standingAt(0, 0), { 0.0f, 1.0f, 0.0f }, { floor, ceiling }, 0.3f);
	CHECK(r.hitCeiling);
	CHECK_NEAR(1.8f + r.moved.y, 2.0f, 0.01f);
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
	fx.hasParticleSystem = true;
	fx.particleEmitContinuously = false;
	fx.particleBurstCount = 12;
	fx.particleMaxParticles = 64;
	fx.particleLifetimeMin = 5.0f;
	fx.particleLifetimeMax = 5.0f;
	const uint32_t fxId = fx.id;
	REQUIRE(s.SavePrefab(fxId, "Spark"));
	A::State(s) = EditorSceneState::Play;

	// EmitParticles on a scene object (deferred to the end of the frame)
	api.emitParticles(&s, fxId, 5);
	GameObject* sceneFx = s.FindGameObjectById(fxId, false);
	CHECK(!sceneFx->particleQuad || sceneFx->particleQuad->GetActiveCount() == 0);
	s.ProcessPendingSpawns();
	sceneFx = s.FindGameObjectById(fxId, false);
	REQUIRE(sceneFx->particleQuad != nullptr);
	CHECK(sceneFx->particleQuad->GetActiveCount() == 5);

	// One-shot prefab: bursts particleBurstCount on spawn, and again (fresh) when reused from the pool
	ScriptGameObjectView* view = s.QueuePrefabSpawn("Spark", { 3.0f, 0.0f, 0.0f });
	REQUIRE(view != nullptr);
	const uint32_t instance = view->id;
	s.ProcessPendingSpawns();
	GameObject* inst = s.FindGameObjectById(instance, false);
	REQUIRE(inst != nullptr);
	REQUIRE(inst->particleQuad != nullptr);
	CHECK_MSG(inst->particleQuad->GetActiveCount() == 12, "spawn burst: " + std::to_string(inst->particleQuad->GetActiveCount()));
	s.QueueDespawn(instance);
	s.ProcessPendingSpawns();
	REQUIRE(s.QueuePrefabSpawn("Spark", { 0.0f, 0.0f, 0.0f }) != nullptr);
	s.ProcessPendingSpawns();
	inst = s.FindGameObjectById(instance, false);
	CHECK_MSG(inst->particleQuad->GetActiveCount() == 12, "reuse burst: " + std::to_string(inst->particleQuad->GetActiveCount()));

	// Continuous flag from script
	api.setParticlesEmitting(&s, fxId, true);
	CHECK(s.FindGameObjectById(fxId, false)->particleEmitContinuously);
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
		"Light", "Particle System", "Mapchip Level Editor", "Audio Source" };
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

/// Renders one real frame and returns the Game view as average RGB (0..255) per grid cell.
/// A coarse grid survives the small rasterization differences between a GPU and WARP.
std::vector<float> CaptureGameViewSignature(EditorSuite& s)
{
	RenderTexture& rt = A::GameViewRT(s);
	Microsoft::WRL::ComPtr<ID3D12Resource> readback;
	uint32_t rowPitch = 0;

	// Self-tests run inside the app's first frame: close it, render one whole frame, and leave a frame open again
	RizeEngine::EndFrame();
	RizeEngine::BeginFrame();
	ID3D12GraphicsCommandList* cmdList = A::Dx(s)->GetCommandList();
	s.RenderViewports(cmdList, false);
	const bool recorded = rt.RecordReadback(cmdList, readback, rowPitch);
	RizeEngine::EndFrame();
	A::Dx(s)->SignalAndWaitFence();
	RizeEngine::BeginFrame();
	if (!recorded) return {};

	const uint8_t* pixels = nullptr;
	if (FAILED(readback->Map(0, nullptr, reinterpret_cast<void**>(const_cast<uint8_t**>(&pixels))))) return {};
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
	s.RenderViewports(A::Dx(s)->GetCommandList(), false);
	other = s.FindGameObjectById(secondId, false)->components.Get<PbrMaterialComponent>("PBR Material");
	CHECK_NEAR(other->metallic, 0.75f, 1e-5f); // the asset's values reached the other object

	CHECK(A::FindMaterial(s, "Assets/Materials/Missing.material.json") == nullptr);
	A::ForgetMaterials(s);
	A::ProjectRoot(s) = savedRoot;
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
	SyncScriptModuleWithProject(); // back to the scripts of the project that was open before the tests
	NewScene();
	isRunningTests_ = false;

	return failed;
}

#endif // USE_IMGUI
