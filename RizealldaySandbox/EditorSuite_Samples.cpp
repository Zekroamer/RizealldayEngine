#include "EditorSuiteInternal.h"
#include "PhysicsComponents.h"
#include "PbrMaterial.h"

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
	if (AddComponentByName(shot, kTrailComponent)) {
		auto* trail = shot.components.Get<TrailComponent>(kTrailComponent);
		trail->lifetime = 0.3f;
		trail->widthStart = 0.25f;
		trail->colorStart = { 1.0f, 0.7f, 0.2f, 1.0f };
		trail->colorEnd = { 1.0f, 0.2f, 0.1f, 0.0f };
		trail->intensity = 3.0f;
	}
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
	// Materials and lights: metal to smooth plastic, a glowing ball, and more lamps than the old limit of three
	for (int i = 0; i < 5; ++i) {
		GameObject& ball = CreateSphere("Material Ball", { -4.0f + i * 2.0f, 1.0f, 6.0f });
		ball.hasSphereCollider = false;
		ball.isStatic = true;
		ball.materialColor = { 0.9f, 0.75f, 0.4f, 1.0f };
		if (AddComponentByName(ball, "PBR Material")) {
			auto* pbr = ball.components.Get<PbrMaterialComponent>("PBR Material");
			pbr->metallic = i < 3 ? 1.0f : 0.0f;
			pbr->roughness = 0.1f + 0.2f * static_cast<float>(i % 3);
			if (i == 4) {
				pbr->emissive = { 0.2f, 0.8f, 1.0f };
				pbr->emissiveStrength = 4.0f;
			}
		}
	}
	for (int i = 0; i < 6; ++i) {
		GameObject& lamp = CreateGameObject("Lamp", GameObjectType::Empty);
		const float angle = 6.2831853f * i / 6.0f;
		lamp.translation = { std::cos(angle) * 6.0f, 1.5f, std::sin(angle) * 6.0f };
		lamp.hasLight = true;
		lamp.lightEnabled = true;
		lamp.lightType = GameObject::LightType::Point;
		lamp.lightColor = (i % 2) == 0 ? Vector3{ 1.0f, 0.5f, 0.2f } : Vector3{ 0.3f, 0.6f, 1.0f };
		lamp.lightIntensity = 2.0f;
		lamp.lightRange = 6.0f;
		lamp.lightDecay = 1.5f;
	}

	// A character: blend tree between idle and run, a cube held in the hand. The player script drops it as a ragdoll with R.
	GameObject& dummy = CreateGameObject("Dummy", GameObjectType::Empty);
	dummy.translation = { 3.0f, 0.0f, 6.0f };
	dummy.scale = { 0.012f, 0.012f, 0.012f };
	dummy.hasMesh = true;
	dummy.meshEnabled = true;
	dummy.meshType = GameObject::MeshType::CustomModel;
	dummy.modelPath = "Resources/Model/Idle.fbx";
	const uint32_t dummyId = dummy.id;
	const std::string controllerPath = CreateAnimatorControllerAsset("Dummy");
	if (!controllerPath.empty() && AddComponentByName(*FindGameObjectById(dummyId, false), "Animator")) {
		AnimatorController controller;
		controller.parameters.push_back({ "speed", AnimParamType::Float, 0.5f });
		AnimState& move = controller.AddState("Move", { 0.0f, 0.0f });
		move.blendParameter = "speed";
		move.blendClips = { { "", "", 0, 0.0f }, { "Resources/Model/Running.fbx", "", 0, 1.0f } };
		SaveAnimatorController(controllerPath, controller);
		auto* animator = FindGameObjectById(dummyId, false)->components.Get<AnimatorComponent>("Animator");
		strncpy_s(animator->controller, controllerPath.c_str(), _TRUNCATE);
	}
	GameObject& held = CreateCube("Held Item", { 0.0f, 10.0f, 0.0f });
	held.scale = { 8.0f, 40.0f, 8.0f };
	held.hasBoxCollider = false;
	held.parentId = static_cast<int>(dummyId);
	if (AddComponentByName(held, "Bone Attachment")) {
		strncpy_s(held.components.Get<BoneAttachmentComponent>("Bone Attachment")->bone, "mixamorig:RightHand", _TRUNCATE);
	}

	// A looping 3D sound: turn the camera to move it between the ears, walk away to fade it
	GameObject& speaker = CreateCube("Speaker", { -7.0f, 0.5f, -4.0f });
	speaker.isStatic = true;
	speaker.materialColor = { 0.9f, 0.2f, 0.8f, 1.0f };
	speaker.hasAudioSource = true;
	speaker.audioClipName = "Resources/Sound/titlebgm.wav";
	speaker.audioLoop = true;
	speaker.audioPlayOnAwake = true;
	speaker.audioSpatial = true;
	speaker.audioMinDistance = 2.0f;
	speaker.audioMaxDistance = 25.0f;

	// Effects: a fire simulated on the GPU
	GameObject& fire = CreateGameObject("Fire", GameObjectType::Empty);
	fire.translation = { 7.0f, -0.4f, -4.0f };
	if (AddComponentByName(fire, kGpuParticlesComponent)) {
		ApplyGpuParticlePreset(*fire.components.Get<GpuParticlesComponent>(kGpuParticlesComponent), "Fire");
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
