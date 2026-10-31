#include "EditorSuiteInternal.h"
#include "PhysicsComponents.h"

// Sample content generators (GameObject > Samples). Everything they create is ordinary scene /
// prefab data, so the result can be edited freely afterwards.

namespace {
	void AddScriptComponent(GameObject& obj, const char* className)
	{
		obj.scripts.emplace_back(className);
		obj.hasScript = true;
		obj.scriptClassName = obj.scripts.front().className;
	}
}

bool EditorSuite::CreateThirdPersonDemo()
{
	if (currentProjectRoot_.empty() || sceneState_ != EditorSceneState::Edit) {
		LogMessage("[Samples] Open a project and stop Play mode before creating a sample.", 1);
		return false;
	}
	if (!InstallSampleScripts("ThirdPerson")) return false;

	NewScene(); // Main Camera + Directional Light + Ground Plane
	GameObject& shot = CreateSphere("ThirdPersonShot", { 0.0f, 0.0f, 0.0f });
	shot.scale = { 0.25f, 0.25f, 0.25f };
	shot.hasSphereCollider = false;
	shot.hasBoxCollider = false;
	shot.isLit = false;
	shot.materialColor = { 1.0f, 0.8f, 0.2f, 1.0f };
	const uint32_t shotId = shot.id;
	const bool saved = SavePrefab(shotId, "ThirdPersonShot");
	QueueDeleteGameObjectById(shotId);
	ProcessPendingDeletes();
	if (!saved) return false;

	// A ring of pillars to walk around and shoot at
	for (int i = 0; i < 8; ++i) {
		const float angle = 6.2831853f * i / 8.0f;
		GameObject& pillar = CreateCube("Pillar", { std::cos(angle) * 9.0f, 1.0f, std::sin(angle) * 9.0f });
		pillar.scale = { 1.5f, 3.0f, 1.5f };
		pillar.isStatic = true;
		pillar.materialColor = { 0.35f, 0.55f, 0.85f, 1.0f };
	}
	// Physics to play with: a stack to knock over and a door on a hinge
	for (int i = 0; i < 4; ++i) {
		GameObject& crate = CreateCube("Crate", { 4.0f, 0.5f + i * 1.01f, 3.0f });
		crate.hasRigidbody = true;
		crate.mass = 2.0f;
		crate.materialColor = { 0.8f, 0.6f, 0.3f, 1.0f };
	}
	GameObject& door = CreateCube("Door", { -4.0f, 1.3f, 3.0f });
	door.scale = { 2.0f, 2.4f, 0.2f };
	door.hasRigidbody = true;
	door.useGravity = false;
	door.drag = 1.0f;
	door.materialColor = { 0.4f, 0.75f, 0.45f, 1.0f };
	if (AddComponentByName(door, "Joint")) {
		auto* joint = door.components.Get<JointComponent>("Joint");
		joint->jointType = 2;
		joint->anchor = { -0.5f, 0.0f, 0.0f }; // left edge, in local units
		joint->axis = { 0.0f, 1.0f, 0.0f };
	}
	GameObject& player = CreateCube("Player", { 0.0f, 2.0f, 0.0f });
	player.materialColor = { 0.9f, 0.35f, 0.25f, 1.0f };
	AddScriptComponent(player, "ThirdPersonPlayer");
	for (auto& obj : gameObjects_) {
		if (obj.hasCamera || obj.type == GameObjectType::Camera) AddScriptComponent(obj, "ThirdPersonCamera");
	}
	RebuildHierarchyLinks();
	UpdateWorldMatrices();
	EnsureEditModeScriptInstances();
	currentSceneName_ = "ThirdPersonDemo";
	const bool ok = SaveScene((GetScenesDirectory() / "ThirdPersonDemo.scene.json").string());
	ResetUndoHistory();
	return ok;
}

bool EditorSuite::CreateBenchmarkScene()
{
	if (currentProjectRoot_.empty() || sceneState_ != EditorSceneState::Edit) return false;
	if (!InstallSampleScripts("Bench")) return false;

	NewScene();
	GameObject& enemy = CreateCube("BenchEnemy", { 0.0f, 0.5f, 0.0f });
	enemy.layer = LAYER_ENEMY;
	enemy.collisionMask = LayerBit(LAYER_PLAYER) | LayerBit(LAYER_PLAYER_BULLET);
	enemy.materialColor = { 0.85f, 0.22f, 0.2f, 1.0f };
	AddScriptComponent(enemy, "BenchEnemy");
	const uint32_t enemyId = enemy.id;
	GameObject& bullet = CreateSphere("BenchBullet", { 0.0f, 0.5f, 0.0f });
	bullet.layer = LAYER_PLAYER_BULLET;
	bullet.collisionMask = LayerBit(LAYER_ENEMY);
	bullet.scale = { 0.3f, 0.3f, 0.3f };
	bullet.materialColor = { 1.0f, 0.85f, 0.25f, 1.0f };
	bullet.hasSphereCollider = true;
	AddScriptComponent(bullet, "BenchBullet");
	const uint32_t bulletId = bullet.id;
	const bool saved = SavePrefab(enemyId, "BenchEnemy") && SavePrefab(bulletId, "BenchBullet");
	QueueDeleteGameObjectById(enemyId);
	QueueDeleteGameObjectById(bulletId);
	ProcessPendingDeletes();
	if (!saved) return false;

	// Everything in view: nothing is saved by culling
	for (auto& obj : gameObjects_) {
		if (obj.hasCamera || obj.type == GameObjectType::Camera) {
			obj.translation = { 0.0f, 55.0f, -30.0f };
			obj.rotation = { 1.05f, 0.0f, 0.0f };
			obj.rotationQuat = Quaternion::FromEuler(obj.rotation);
		}
		if (obj.hasMesh && obj.meshType == GameObject::MeshType::Plane) {
			obj.translation = { 0.0f, 0.0f, 0.0f };
			obj.scale = { 40.0f, 1.0f, 40.0f };
		}
	}
	GameObject& player = CreateCube("Player", { 0.0f, 0.5f, 0.0f });
	player.layer = LAYER_PLAYER;
	player.collisionMask = LayerBit(LAYER_ENEMY);
	GameObject& spawner = CreateGameObject("BenchSpawner", GameObjectType::Empty);
	AddScriptComponent(spawner, "BenchSpawner");
	RebuildHierarchyLinks();
	UpdateWorldMatrices();
	EnsureEditModeScriptInstances();
	currentSceneName_ = "Benchmark";
	const bool ok = SaveScene((GetScenesDirectory() / "Benchmark.scene.json").string());
	ResetUndoHistory();
	return ok;
}

bool EditorSuite::CreateSurvivorsDemo()
{
	if (currentProjectRoot_.empty() || sceneState_ != EditorSceneState::Edit) {
		LogMessage("[Samples] Open a project and stop Play mode before creating a sample.", 1);
		return false;
	}
	// A project with its own scripts needs the sample's script sources (they build in the background)
	if (!InstallSampleScripts("Survivors")) return false;

	NewScene(); // Main Camera + Directional Light

	// --- Prefab templates (saved, then removed from the scene) ---
	uint32_t enemyId = 0;
	{
		GameObject& enemy = CreateCube("Enemy", { 0.0f, 0.5f, 0.0f });
		enemy.materialColor = { 0.85f, 0.22f, 0.2f, 1.0f };
		enemy.scale = { 0.9f, 0.9f, 0.9f };
		enemy.tag = "Enemy";
		enemy.layer = LAYER_ENEMY;
		enemy.collisionMask = LayerBit(LAYER_PLAYER) | LayerBit(LAYER_PLAYER_BULLET); // enemies ignore each other
		enemy.hasSphereCollider = true;
		enemy.sphereRadius = 0.55f;
		AddScriptComponent(enemy, "SurvivorsEnemy");
		enemyId = enemy.id;
	}
	uint32_t bulletId = 0;
	{
		GameObject& bullet = CreateSphere("Bullet", { 0.0f, 0.5f, 0.0f });
		bullet.materialColor = { 1.0f, 0.85f, 0.25f, 1.0f };
		bullet.scale = { 0.3f, 0.3f, 0.3f };
		bullet.tag = "Bullet";
		bullet.layer = LAYER_PLAYER_BULLET;
		bullet.collisionMask = LayerBit(LAYER_ENEMY);
		bullet.hasSphereCollider = true;
		bullet.sphereRadius = 0.8f; // scaled by 0.3 -> 0.24 world units
		AddScriptComponent(bullet, "SurvivorsBullet");
		bulletId = bullet.id;
	}
	const bool prefabsSaved = SavePrefab(enemyId, "Enemy") && SavePrefab(bulletId, "Bullet");
	QueueDeleteGameObjectById(enemyId);
	QueueDeleteGameObjectById(bulletId);
	ProcessPendingDeletes();
	if (!prefabsSaved) {
		return false;
	}

	// --- Scene ---
	for (auto& obj : gameObjects_) {
		if (obj.hasCamera || obj.type == GameObjectType::Camera) {
			obj.translation = { 0.0f, 18.5f, -10.0f };
			obj.rotation = { 1.05f, 0.0f, 0.0f };
			obj.rotationQuat = Quaternion::FromEuler(obj.rotation);
			AddScriptComponent(obj, "SurvivorsCamera");
			break;
		}
	}
	{
		// Reuse the default scene's ground plane if there is one
		GameObject* ground = nullptr;
		for (auto& obj : gameObjects_) {
			if (obj.hasMesh && obj.meshType == GameObject::MeshType::Plane) { ground = &obj; break; }
		}
		if (!ground) ground = &CreatePlane("Ground", { 0.0f, 0.0f, 0.0f });
		ground->name = "Ground";
		ground->translation = { 0.0f, 0.0f, 0.0f };
		ground->scale = { 12.0f, 1.0f, 12.0f };
		ground->materialColor = { 0.28f, 0.42f, 0.3f, 1.0f };
		ground->isStatic = true;
	}
	{
		GameObject& player = CreateCube("Player", { 0.0f, 0.5f, 0.0f });
		player.materialColor = { 0.25f, 0.55f, 1.0f, 1.0f };
		player.tag = "Player";
		player.layer = LAYER_PLAYER;
		player.collisionMask = LayerBit(LAYER_ENEMY);
		player.hasBoxCollider = true;
		AddScriptComponent(player, "SurvivorsPlayer");
	}
	{
		GameObject& manager = CreateGameObject("GameManager", GameObjectType::Empty);
		AddScriptComponent(manager, "SurvivorsGameManager");
	}
	RebuildHierarchyLinks();
	UpdateWorldMatrices();
	EnsureEditModeScriptInstances();

	const std::string scenePath = (GetScenesDirectory() / "SurvivorsDemo.scene.json").string();
	currentSceneName_ = "SurvivorsDemo";
	if (!SaveScene(scenePath)) {
		LogMessage("[Samples] Could not save the Survivors demo scene.", 2);
		return false;
	}
	ResetUndoHistory();
	LogMessage(std::format("[Samples] Survivors demo created: {} (press Play).", scenePath), 0);
	return true;
}
