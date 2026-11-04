#pragma once
#include <d3d12.h>
#include "ParticleQuad.h"
#include "base/RenderTexture.h"
#include "base/DirectXCommon.h"
#include "render/PostProcess.h"
#include "physics/PhysicsWorld.h"
#include "render/EnvironmentProbe.h"
#include "PbrMaterial.h"
#include "VfxComponents.h"
#include "Timeline.h"
#include "Terrain.h"
#include "3d/Camera.h"
#include "3d/ShadowMap.h"
#include "3d/Skybox.h"
#include "3d/WorldTransform.h"
#include "base/DXCManager.h"
#include "math/Vector2.h"
#include "math/Vector3.h"
#include "math/Vector4.h"
#include "math/Matrix4x4.h"
#include "math/Quaternion.h"
#include "math/Frustum.h"
#include "base/ScriptAPI.h"
#include "base/ScriptBase.h"
#include "base/ScriptModule.h"
#include "ComponentRegistry.h"
#include "AssetDatabase.h"
#include "AnimatorController.h"
#include "InputActions.h"
#include "NavGrid.h"
#ifdef USE_IMGUI
#include <imgui.h>
#else
struct ImVec2 {
	float x = 0.0f;
	float y = 0.0f;
	constexpr ImVec2() = default;
	constexpr ImVec2(float _x, float _y) : x(_x), y(_y) {}
};
#endif
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <memory>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

class DirectXCommon;
class Input;
class LightGroup;
class Object3d;
class Model;

enum class EditorSceneState {
	Edit,
	Play,
	Pause
};

enum class GizmoType {
	None = -1,
	Hand = 0,
	Move = 1,
	Rotate = 2,
	Scale = 3,
	Bounds = 4,
	Collider = 5
};

enum class GizmoAxis {
	None = -1,
	X = 0,
	Y = 1,
	Z = 2,
	Center = 3
};

enum class GameObjectType {
	Empty,
	Cube,
	Sphere,
	Plane,
	Quad,
	Camera,
	DirectionalLight,
	PointLight,
	SpotLight,
	ParticleSystem,
	MapchipGrid,
	Sprite,
	AudioSource,
	Canvas
};

class AnimationPlayer;
struct GameObject {
	GameObject();
	~GameObject();
	GameObject(const GameObject& other);
	GameObject(GameObject&& other) noexcept;
	GameObject& operator=(const GameObject& other);
	GameObject& operator=(GameObject&& other) noexcept;

	uint32_t id = 0;
	std::string name = "GameObject";
	bool isActive = true;
	bool isStatic = false;
	std::string tag = "Untagged";
	uint32_t layer = 0;              // 0..31, names in ScriptLayer (ScriptAPI.h) / GetCollisionLayerName()
	uint32_t collisionMask = 0xFFFFFFFF;
	// Link to a prefab (saved with the scene): pooling and linked instances
	std::string prefabSource;
	int prefabNodeIndex = -1; // position in the prefab file
	int prefabNodeKey = -1;   // stable identity of the node: survives nodes added to or removed from the prefab
	std::vector<std::string> prefabOverrides; // JSON keys this instance keeps over the prefab ("materialColor", "components/Trail")
	std::string nestedPrefab; // part of a prefab placed inside the prefab of this instance (nested prefab)
	int nestedNodeKey = -1;   // its node key in that inner prefab
	GameObjectType type = GameObjectType::Empty;
	int parentIndex = -1;
	int parentId = -1;
	std::vector<uint32_t> childrenIds;

	// Transform Component
	Vector3 translation = { 0.0f, 0.0f, 0.0f };
	Vector3 rotation = { 0.0f, 0.0f, 0.0f }; // Radians
	Vector3 scale = { 1.0f, 1.0f, 1.0f };
	Quaternion rotationQuat = Quaternion::Identity();
	// Runtime only (not serialized): world matrix, refreshed by EditorSuite::UpdateWorldMatrices().
	// translation / rotation / rotationQuat / scale above are LOCAL (relative to the parent).
	Matrix4x4 worldMatrix = Mathematics::MakeIdentity4x4();

	// Mesh Filter & Mesh Renderer
	bool hasMesh = false;
	bool meshEnabled = true;
	enum class MeshType { Cube, Sphere, Plane, Quad, CustomModel };
	MeshType meshType = MeshType::Cube;
	Vector4 materialColor = { 0.8f, 0.8f, 0.82f, 1.0f };
	uint32_t textureHandle = 0;
	bool isLit = true;
	int lightingType = 4; // 0=Unlit, 1=Lambert, 2=HalfLambert, 3=Phong, 4=BlinnPhong
	float shininess = 32.0f;
	Vector3 specularColor = { 1.0f, 1.0f, 1.0f };
	bool castShadow = true;
	bool receiveShadow = true;
	bool useGeometryShader = false;
	int geometryShaderEffect = 0; // 0=None, 1=Standard Pass-Through, 2=Explode Faces Along Normals
	float explodeAmount = 0.5f;
	std::string texturePath = "";
	std::string modelPath = "";
	Vector2 uvOffset = { 0.0f, 0.0f };
	Vector2 uvTiling = { 1.0f, 1.0f };
	float uvRotation = 0.0f;
	float outlineWidth = 0.0f;                         // Mesh Renderer outline (world units, 0 = none)
	Vector4 outlineColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	std::unique_ptr<Object3d> editObject3d;
	std::unique_ptr<Object3d> gameObject3d;
	std::unique_ptr<WorldTransform> shadowTransform;

	// Animator / Skeletal Animation Component
	int animationIndex = 0;
	float animationSpeed = 1.0f;
	bool animationLoop = true;
	bool animationPlaying = true;

	// Box Collider Component
	bool hasBoxCollider = false;
	bool boxColliderEnabled = true;
	bool isTrigger = false;
	bool providesContacts = true;
	Vector3 boxCenter = { 0.0f, 0.0f, 0.0f };
	Vector3 boxSize = { 1.0f, 1.0f, 1.0f };
	bool isColliding = false;

	// Sphere Collider Component
	bool hasSphereCollider = false;
	bool sphereColliderEnabled = true;
	bool sphereIsTrigger = false;
	Vector3 sphereCenter = { 0.0f, 0.0f, 0.0f };
	float sphereRadius = 0.5f;

	// Camera Component
	bool hasCamera = false;
	bool cameraEnabled = true;
	float fov = 45.0f;
	float nearClip = 0.1f;
	float farClip = 1000.0f;
	bool isOrthographic = false;
	float orthoSize = 10.0f; // half the visible height (orthographic)

	// Light Component
	bool hasLight = false;
	bool lightEnabled = true;
	enum class LightType { Directional, Point, Spot };
	LightType lightType = LightType::Directional;
	Vector3 lightColor = { 1.0f, 0.95f, 0.9f };
	float lightIntensity = 1.0f;
	Vector3 lightDirection = { 0.5f, -1.0f, 0.5f };
	float lightRange = 10.0f;
	float lightDecay = 2.0f;
	float lightSpotAngle = 30.0f;
	float lightSpotFalloffStart = 20.0f;
	bool lightCastShadow = true;
	float lightShadowBias = 0.002f;
	float lightShadowStrength = 1.0f;
	float lightShadowNormalBias = 0.02f;

	// Particle System Component
	bool hasParticleSystem = false;
	int attachToIndex = -1;
	int attachedParentId = -1;
	Vector3 localOffset = { 0.0f, 0.0f, 0.0f };
	int particlePreset = 0; // 0: Custom, 1: Smoke, 2: Water Fountain, 3: Fireworks, 4: Explosion, 5: Torch

	// Particle System Parameters
	int particleShape = 1; // 0: Point, 1: Sphere, 2: Box, 3: Cone
	float particleSpawnRadius = 0.5f;
	Vector3 particleBoxExtents = { 1.0f, 1.0f, 1.0f };
	float particleConeAngle = 25.0f; // degrees

	float particleSpawnRate = 20.0f; // particles per second
	int particleMaxParticles = 100;
	int particleBurstCount = 15;
	// Off: no continuous spawning; prefab instances burst particleBurstCount on spawn, scripts call EmitParticles
	bool particleEmitContinuously = true;

	Vector3 particleDirection = { 0.0f, 1.0f, 0.0f };
	float particleSpreadAngle = 30.0f; // degrees (0-180)
	float particleSpeedMin = 1.0f;
	float particleSpeedMax = 3.0f;

	float particleGravity = 0.0f;
	float particleDrag = 0.05f;

	bool particleWindEnabled = false;
	Vector3 particleWindAcceleration = { 15.0f, 0.0f, 0.0f };

	float particleLifetimeMin = 1.5f;
	float particleLifetimeMax = 3.0f;

	float particleStartScale = 0.25f;
	float particleEndScale = 0.5f;

	Vector4 particleStartColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	Vector4 particleEndColor = { 1.0f, 1.0f, 1.0f, 0.0f };

	int particleBlendMode = 0; // 0: Normal Alpha, 1: Additive Glow
	int particleBillboardMode = 0; // 0: Camera Facing, 1: Y-Axis Cylindrical, 2: Velocity Aligned

	std::shared_ptr<ParticleQuad> particleQuad;

	// Mapchip Component
	bool hasMapchip = false;

	// Audio Source Component
	bool hasAudioSource = false;
	std::string audioClipName = "None";
	float audioVolume = 1.0f;
	bool audioPlayOnAwake = false;
	bool audioLoop = false;
	float audioPitch = 1.0f;
	int audioBus = 0; // AudioBus
	bool audioSpatial = false;
	float audioMinDistance = 1.0f;
	float audioMaxDistance = 50.0f;
	uint32_t runtimeSoundHandle = 0;
	uint32_t runtimeVoiceHandle = 0;

	// C++ Script Component
	struct ScriptSavedFieldValue {
		ScriptFieldType type = ScriptFieldType::Float;
		float floatVal = 0.0f;
		int intVal = 0;
		bool boolVal = false;
		Vector3 vec3Val = { 0.0f, 0.0f, 0.0f };
		std::string strVal = "";
	};

	struct ScriptComponent {
		std::string className = "";
		ScriptInstancePtr instance = nullptr;
		std::map<std::string, ScriptSavedFieldValue> savedFields;
		bool isMissingScript = false;
		bool isCompiling = false;

		ScriptComponent() = default;
		explicit ScriptComponent(const std::string& name) : className(name), instance(nullptr) {}
		ScriptComponent(const ScriptComponent& other)
			: className(other.className),
			  instance(nullptr),
			  savedFields(other.savedFields),
			  isMissingScript(other.isMissingScript),
			  isCompiling(other.isCompiling) {}
		ScriptComponent& operator=(const ScriptComponent& other) {
			if (this != &other) {
				className = other.className;
				instance.reset();
				savedFields = other.savedFields;
				isMissingScript = other.isMissingScript;
				isCompiling = other.isCompiling;
			}
			return *this;
		}
		ScriptComponent(ScriptComponent&&) noexcept = default;
		ScriptComponent& operator=(ScriptComponent&&) noexcept = default;
	};
	bool hasScript = false;
	std::string scriptClassName = "";
	std::vector<ScriptComponent> scripts;

	// Data components from the component registry (ComponentRegistry.h), e.g. Health
	ComponentStore components;

	// Rigidbody Component
	bool hasRigidbody = false;
	bool rbEnabled = true;
	float mass = 1.0f;
	float drag = 0.05f;
	float gravityScale = 1.0f;
	bool useGravity = true;
	bool isKinematic = false;
	float bounciness = 0.1f;
	float friction = 0.5f;
	bool freezeRotation = false;
	bool continuousCollision = false;
	Vector3 velocity = { 0.0f, 0.0f, 0.0f };
	bool isGrounded = false;
};

class EditorSuite : public IScriptHost
{
public:
	EditorSuite();
	~EditorSuite() override;

	void Initialize(
		DirectXCommon* dxCommon,
		Camera* gameCamera,
		LightGroup* lights,
		ParticleQuad* particleSystem
	);

	void Update(float deltaTime, Input* input);

	// Render scene to EditViewRenderTexture and GameViewRenderTexture
	void RenderViewports(ID3D12GraphicsCommandList* cmdList, bool showParticles);

	// Draw all Editor GUI panels (Hierarchy, Inspector, Viewports, Project, Console)
	void DrawEditorUI(float deltaTime, bool& showParticles);

	EditorSceneState GetSceneState() const { return sceneState_; }
	Camera& GetEditorCamera() { return editorCamera_; }

	// Creation & Lookup helpers
	GameObject& CreateGameObject(const std::string& name, GameObjectType type);
	GameObject& CreateCube(const std::string& name = "Cube", const Vector3& pos = { 0.0f, 0.0f, 0.0f });
	GameObject& CreateSphere(const std::string& name = "Sphere", const Vector3& pos = { 0.0f, 0.0f, 0.0f });
	GameObject& CreatePlane(const std::string& name = "Plane", const Vector3& pos = { 0.0f, 0.0f, 0.0f });
	GameObject& CreateQuad(const std::string& name = "Textured Quad", const Vector3& pos = { 0.0f, 0.0f, 0.0f });
	GameObject* FindGameObjectById(uint32_t id, bool warnIfMissing = true);
	// O(1) id -> index lookup. The cache is validated on every hit and rebuilt on a miss,
	// so code that mutates gameObjects_ does not have to invalidate it.
	int FindGameObjectIndexById(uint32_t id);
	static std::string GetCollisionLayerName(uint32_t layer);
	GameObject* FindGameObjectByName(const char* name, bool warnIfMissing = true);
	GameObject* FindGameObjectByName(const std::string& name, bool warnIfMissing = true) {
		return FindGameObjectByName(name.c_str(), warnIfMissing);
	}
	// Transform hierarchy (EditorSuite_Hierarchy.cpp). parentId is the source of truth;
	// parentIndex / childrenIds / worldMatrix are derived caches.
	void RebuildHierarchyLinks();
	void UpdateWorldMatrices();
	bool IsDescendantOf(uint32_t candidateId, uint32_t ancestorId) const;
	bool ReparentGameObject(uint32_t childId, int newParentId, bool keepWorldTransform = true);
	void GetWorldTRS(const GameObject& obj, Vector3& t, Quaternion& q, Vector3& s) const;

	void DeleteSelectedGameObject();
	void QueueDeleteGameObject(int index);
	void QueueDeleteGameObjectById(uint32_t id) override;
	void ProcessPendingDeletes();
	void LogScriptMessage(const char* msg, int logType = 0) override {
		if (msg) LogMessage(msg, logType);
	}

	bool HasActiveParticleSystem(Vector3* outEmitterPos = nullptr) const;
	void StartPlayMode();
	void StopPlayMode();
	void UpdateScripts(float deltaTime);
	// Collision events (EditorSuite_ScriptQueries.cpp): contacts are recorded during the collision check,
	// then OnCollisionEnter / OnCollision / OnCollisionExit are dispatched once per object pair.
	void RecordCollisionContact(uint32_t selfId, uint32_t otherId);
	void DispatchCollisionEvents();
	enum class CollisionEvent { Stay, Enter, Exit };
	void DispatchScriptCollision(uint32_t selfId, uint32_t otherId, CollisionEvent kind = CollisionEvent::Stay);
	// Animator (EditorSuite_Animator.cpp, roadmap 5.1)
	static void RegisterAnimatorComponent();
	static void RegisterPhysicsComponents();
	static void RegisterMaterialComponent();
	static void RegisterVfxComponents();
	static void RegisterTimelineComponent();
	static void RegisterTerrainComponent();
	/// During Play: advances every Timeline component and applies its tracks and events
	void UpdateTimelines(float deltaTime);
	std::shared_ptr<Timeline> GetTimeline(const std::string& assetPath);
	bool SaveTimeline(const std::string& assetPath, const Timeline& timeline);
	std::string CreateTimelineAsset(const std::string& name);
	void ApplyTimeline(const Timeline& timeline, float time);
	void DrawTimelineWindow();
	/// Bakes the object's Terrain component into a mesh file and makes it the object's model. False: nothing changed.
	/// heights: what to bake (a sculpt in progress); null = from the component (sculpted data, heightmap or noise)
	/// preview: a bake shown while dragging a brush (replaced, not kept for Undo)
	bool GenerateTerrain(GameObject& obj, const TerrainHeights* heights = nullptr, bool preview = false);
	/// Removes old terrain bakes and height files that Undo kept, once nothing in the scene uses them (scene closed)
	void CollectTerrainGarbage();
	std::vector<std::string> terrainGarbage_;
	std::unordered_set<std::string> terrainPreviewBakes_;
	bool keepTerrainGarbage_ = false; // Prefab Mode keeps the scene aside: its bakes are still used
	bool ComputeTerrainHeights(const TerrainComponent& terrain, TerrainHeights& out);
	/// One brush dab where the world ray meets the terrain object. False when it misses.
	bool SculptTerrainAt(uint32_t objectId, const Vector3& rayOrigin, const Vector3& rayDirection);
	/// Writes the sculpted heights next to the scene's assets and bakes the mesh once more
	bool FinishTerrainSculpt();
	/// Points of the brush circle on the surface (world space), around where the ray meets the terrain
	bool TerrainBrushOutline(uint32_t objectId, const Vector3& rayOrigin, const Vector3& rayDirection, std::vector<Vector3>& ring);
	bool HitSculptedTerrain(const GameObject& obj, const TerrainComponent& terrain, const Vector3& rayOrigin, const Vector3& rayDirection, float& u, float& v);
	void UpdateTerrainSculptInput(const ImVec2& imagePos, const ImVec2& imageSize, bool hovered);
	struct TerrainSculptState {
		bool active = false; // Inspector "Sculpt": left drag in the Scene view paints
		TerrainBrush brush = TerrainBrush::Raise;
		float radius = 0.08f;   // part of the terrain's width
		float strength = 0.02f; // per dab
		float flattenTo = 0.5f;
		uint32_t objectId = 0;
		TerrainHeights heights; // being sculpted
		std::string source;     // the heightData they were loaded from
		bool changed = false;
		double lastBake = 0.0;
	};
	TerrainSculptState terrainSculpt_;
	/// Once a frame on game time: heads follow their objects. Trails only run in Play.
	void UpdateTrails();
	void DrawEffects(ID3D12GraphicsCommandList* cmdList, const Camera& camera, RenderTexture& target);
	/// Decals, then the objects RenderScene3D held back (decalExcluded_). Call right after RenderScene3D.
	void DrawDecals(ID3D12GraphicsCommandList* cmdList, const Camera& camera, RenderTexture& target);
	/// Moving things (Rigidbody, skinned models) are drawn after the decals, so a mark on the floor never paints a character
	static bool ReceivesDecals(const GameObject& obj, const Model* model);
	std::vector<Object3d*> decalExcluded_; // filled by RenderScene3D, drawn by DrawDecals
	/// Once per RenderViewports, outside any render pass
	void UpdateGpuParticles(ID3D12GraphicsCommandList* cmdList);
	/// Moves skeletal animation time: Animator state machines when `runStateMachines` (Play), else the Inspector clip
	/// (or the Animator's entry / previewed state). Called from Update with game time, never from rendering.
	void UpdateAnimations(float deltaTime, bool runStateMachines);
	AnimatorComponent* GetAnimator(GameObject& obj);
	/// Shared controller asset (cached, reloaded when the file changes on disk). nullptr when missing or invalid.
	std::shared_ptr<AnimatorController> GetAnimatorController(const std::string& assetPath);
	bool SaveAnimatorController(const std::string& assetPath, const AnimatorController& controller);
	/// Creates Assets/Animators/<name>.animator.json (unique name) and returns its project-relative path ("" on failure)
	std::string CreateAnimatorControllerAsset(const std::string& name);
	void ApplyAnimatorState(GameObject& obj, const AnimatorComponent& animator, const AnimState& state, float fadeSeconds);
	Model* ResolveClipModelPath(const std::string& clipModel);
	void ApplyRootMotion(GameObject& obj, AnimationPlayer& player, const Model& model);
	GameObject* FindAnimatedObject(uint32_t id, Model*& outModel);
	bool ResolveBoneAttachment(const GameObject& child, const GameObject& parent, Matrix4x4& outBoneMatrix) const;
	void UpdateRagdolls();
	std::vector<float> blendWeights_;
	struct Ragdoll {
		uint32_t handle = 0;
		struct Part {
			int node = -1;
			Matrix4x4 boneFromBody{}; // the bone's world matrix relative to its body
		};
		std::vector<Part> parts;
		// Where the bodies are (world): the object stays where it was, so culling must use this instead
		Vector3 boundsMin{ 0.0f, 0.0f, 0.0f };
		Vector3 boundsMax{ 0.0f, 0.0f, 0.0f };
		bool hasBounds = false;
	};
	std::unordered_map<uint32_t, Ragdoll> ragdolls_;
	/// World box for culling: the ragdoll bodies while one is active, else the model box placed by the object
	AABB CullingBounds(const GameObject& obj, const Model& model, const Matrix4x4& world, bool animated) const;
	/// Model that owns a state's clip: nullptr = the object's own model
	Model* ResolveClipModel(const AnimState& state);
	void DispatchScriptAnimationEvent(uint32_t objectId, const char* eventName);
	// Fixed timestep (EditorSuite_Physics.cpp): physics, collision events and rigidbodies advance in steps of exactly 1/60 s
	static constexpr double kFixedPhysicsStep = 1.0 / 60.0;
	static constexpr int kMaxPhysicsSteps = 5; // a longer frame drops the rest instead of falling further behind
	/// Runs as many fixed steps as `deltaTime` (plus the leftover from earlier frames) covers. Returns the step count.
	int StepPhysics(float deltaTime);
	/// Box collider = the mesh bounds of a custom model (centre and size, so scale 0.01 models get a correct box). False for primitives / no model.
	bool FitBoxColliderToMesh(GameObject& obj);
	// Input actions (EditorSuite_InputActions.cpp): <project>/ProjectSettings/input.json, defaults without the file
	void ReloadInputActions();
	const InputActions& GetInputActions(); // follows currentProjectRoot_
	bool SaveInputActions(const InputActions& actions);
	InputActions::Source InputSource() const;
	void DrawInputActionsWindow();
	// Game feel (EditorSuite_ScriptQueries.cpp). Both run on real time and reset on Play / Stop / scene load.
	/// Shakes the game camera with `strength` (world units) fading to 0 over `seconds`; a weaker shake never cuts a stronger one
	void ShakeCamera(float strength, float seconds);
	/// Freezes game time for `seconds` (scripts keep running with deltaTime 0); timeScale is not changed
	void Hitstop(float seconds);
	float CameraShakeStrength() const { return shakeLeft_ > 0.0f && shakeDuration_ > 0.0f ? shakeStrength_ * shakeLeft_ / shakeDuration_ : 0.0f; }
	Vector3 CameraShakeOffset() const;
	void DrawAnimatorWindow();
	void DrawSkeletonOverlay(const ImVec2& vpPos, const ImVec2& vpSize);
	// Animator window helpers (EditorSuite_AnimatorWindow.cpp)
	void AnimatorCommit();
	void AnimatorLoadWorking(const std::string& assetPath);
	void DrawAnimatorCanvas(AnimatorController& c, const AnimatorRuntime* live, const ImVec2& size);
	void DrawAnimatorSidePanel(AnimatorController& c, AnimatorComponent* liveAnimator, GameObject* liveObject);
	// World-space collider shapes shared by physics and script queries
	void ComputeBoxColliderBounds(const GameObject& obj, bool useAbsSize, Vector3& outMin, Vector3& outMax) const;
	void ComputeSphereColliderWorld(const GameObject& obj, Vector3& outCenter, float& outRadius) const;
	bool ComputeCapsuleColliderWorld(const GameObject& obj, Vector3& outCenter, float& outRadius, float& outHalfHeight) const;
	Vector3 GetWorldPositionOf(const GameObject& obj) const;
	// Gizmo math, snapping, local/world axes, and camera framing (EditorSuite_Gizmos.cpp)
	static float SnapValue(float value, float step);
	/// Signed angle (radians) from `from` to `to` around `center` on screen; counter-clockwise is positive
	static float GizmoSweepAngle(const ImVec2& center, const ImVec2& from, const ImVec2& to);
	/// start turned by angle about axis (in the space start is expressed in)
	static Quaternion RotateAboutAxis(const Quaternion& start, const Vector3& axis, float angle);
	float GizmoTurnAngle(const Vector3& center, const Vector3& worldAxis, bool snap) const;
	static float ProjectGizmoDrag(const ImVec2& mouseDelta, const ImVec2& screenAxisFromOrigin, float probeMeters = 1.0f);
	void GetGizmoAxes(const GameObject& obj, Vector3& outAxisX, Vector3& outAxisY, Vector3& outAxisZ) const;
	bool FocusSelectedObject();
	bool AcceptQueryHit(uint32_t id, uint32_t layerMask, uint32_t ignoreId, const std::unordered_set<uint32_t>& excluded) const;
	bool ScriptRaycast(const Vector3& origin, const Vector3& direction, float maxDistance, uint32_t layerMask, uint32_t ignoreId, ScriptRaycastHit& outHit);
	bool ScriptPlayAnimationLayer(uint32_t id, int layer, const char* clipName, const char* maskBone, bool loop, float speed, float fadeSeconds);
	bool ScriptStopAnimationLayer(uint32_t id, int layer, float fadeSeconds);
	bool ScriptGetBoneWorldPosition(uint32_t id, const char* bone, Vector3& out);
	bool ScriptSetBoneIK(uint32_t id, const char* endBone, const Vector3& worldTarget, const Vector3& worldPole, float weight);
	bool ScriptAttachToBone(uint32_t childId, uint32_t parentId, const char* bone);
	bool ScriptSetRagdoll(uint32_t id, bool enabled);
	int ScriptOverlapSphere(const Vector3& center, float radius, uint32_t layerMask, uint32_t* outIds, int maxCount);
	/// MoveCharacter (PhysicsWorld, Jolt CharacterVirtual) against every other non-trigger collider the object collides with
	bool ScriptMoveCharacter(uint32_t id, const Vector3& motion, float stepHeight, ScriptCharacterMove& out);
	/// A* path over the navigation grid (static colliders above the floor block); returns the full point count
	int ScriptFindPath(const Vector3& from, const Vector3& to, Vector3* outPoints, int maxPoints);
	void BuildNavGrid(const Vector3& agentPosition);
	const GameObject* FindNearestObject(const Vector3& position, const char* tag, float maxDistance, uint32_t ignoreId) const;
	// Script requests that run outside script callbacks: timers, reparenting, particle bursts, music
	void SetupEngineAPIExtensions();
	void ResetScriptRuntimeState();
	void TickScriptTimers(float deltaTime);
	void ProcessDeferredScriptRequests();
	void BurstOneShotParticles(uint32_t id);
	void CancelDelayedDespawn(uint32_t id);
	size_t GetDelayedDespawnCount() const { return delayedDespawns_.size(); }
	void SavePlayModeSnapshot();
	void RestorePlayModeSnapshot();

	// GameScripts DLL Hot Reload, Reflection & Build Pipeline
	void SetupEngineAPI();
	void HotReloadScripts();
	std::string SerializeAllScriptFieldsToJson();
	void DeserializeAllScriptFieldsFromJson(const std::string& json);
	void CaptureScriptFieldsToSaved(GameObject::ScriptComponent& sc);
	void ApplySavedFieldsToScript(GameObject::ScriptComponent& sc);
	void EnsureEditModeScriptInstances();
	void DestroyAllScriptInstances();
	void SyncGameObjectToView(const GameObject& src, ScriptGameObjectView& dst);
	void SyncViewToGameObject(const ScriptGameObjectView& src, GameObject& dst);
	void RefreshAllScriptObjectViews();
	ScriptGameObjectView* GetOrUpdateScriptViewById(uint32_t id);
	ScriptGameObjectView* FindPendingSpawnView(uint32_t id);
	ScriptGameObjectView* QueueScriptSpawn(const char* name, int primitiveType, const Vector3& position);
	// Prefabs + pooling (EditorSuite_Prefabs.cpp)
	std::filesystem::path GetPrefabDirectory() const;
	/// <Prefabs>/<name>.prefab.json, or wherever a prefab with that file name was moved to inside Assets/
	std::filesystem::path ResolvePrefabFile(const std::string& prefabName) const;
	/// variantOf: save as a variant of that prefab (only the differences are its own). Empty keeps a variant a variant.
	bool SavePrefab(uint32_t rootId, const std::string& prefabName, const std::string& variantOf = {});
	/// The base of a prefab variant, "" for an ordinary prefab
	std::string PrefabBaseOf(const std::string& prefabName);
	/// Saves the instance as "<prefab> Variant" and links it to the variant. Returns the variant name, "" on failure.
	std::string CreatePrefabVariant(uint32_t rootId);
	/// additive = Ctrl+click: toggles the object; otherwise it becomes the only selection. index -1 clears.
	void SelectObject(int index, bool additive);
	/// Shift+click range selection between the current primary selection and toIndex (inclusive).
	void SelectObjectRange(int toIndex);
	std::vector<uint32_t> SelectedIds() const; // primary first
	bool IsSelected(uint32_t id) const;
	/// The gizmo moved the primary by delta (local units): the other selected objects follow, except children of selected objects
	void MoveExtraSelection(const Vector3& delta);
	/// The gizmo turned or scaled the primary: the others turn by the same Euler angles and scale by the same ratio
	void RotateScaleExtraSelection(const Vector3& eulerDelta, const Vector3& scaleRatio);
	/// Copies objects with their children (ids, parents and joints inside the copy remapped); selects and returns the new roots.
	/// An id whose ancestor is also in the list is copied with that ancestor.
	std::vector<uint32_t> DuplicateObjects(const std::vector<uint32_t>& ids);
	/// A Joint connected to an object of a copied group follows the copy (prefab instances, duplicates)
	static void RemapJointTarget(GameObject& obj, const std::unordered_map<uint32_t, uint32_t>& idMap);
	// Linked prefab instances (EditorSuite_PrefabLinks.cpp)
	std::vector<uint32_t> PrefabInstanceRoots(const std::string& prefabName) const; // "" = every prefab
	/// Rebuilds from the prefab file and drops the overrides (Inspector "Revert")
	bool RevertPrefabInstance(uint32_t rootId);
	bool RebuildPrefabInstance(uint32_t rootId, bool keepOverrides);
	struct PrefabRebuild; // ids and override values of an instance being rebuilt
	const PrefabRebuild* prefabRebuild_ = nullptr;
	bool suppressOverrideRecording_ = false; // syncs change objects without the user
	uint32_t PrefabRebuildId(int nodeKey) const;
	void MergePrefabRebuild(int nodeKey, void* json) const;
	/// Drops one override of one object and rebuilds its instance. False when it had no such override.
	bool RevertPrefabOverride(uint32_t objectId, const std::string& key);
	/// True when the linked prefab object has an exact override `keyOrPrefix` or any override starting with `keyOrPrefix + "/"`.
	bool HasPrefabOverride(const GameObject& obj, std::string_view keyOrPrefix) const;
	/// Reverts all overrides on `objectId` matching `keyOrPrefix` (exact or `keyOrPrefix + "/"` prefix) and rebuilds its instance.
	bool RevertPrefabOverridesByPrefix(uint32_t objectId, std::string_view keyOrPrefix);
	/// Marks what an edit changed on linked objects as overrides (called by PushUndoSnapshot)
	void RecordPrefabOverrides(uint32_t objectId, const std::string& beforeJson, const std::string& afterJson);
	bool ApplyPrefabInstance(uint32_t rootId);
	void UnlinkPrefabInstance(uint32_t rootId);
	void SyncLinkedPrefabs();
	void DrawPrefabLinkInspector(GameObject& obj);
	// Prefab Mode: the prefab alone in the editor. Saving (Ctrl+S too) writes the prefab file; closing brings the scene back as it was.
	bool OpenPrefabMode(const std::string& prefabName);
	bool SavePrefabMode();
	/// Back to the scene; prefab edits that were not saved are dropped
	void ClosePrefabMode();
	bool IsInPrefabMode() const { return prefabMode_.active; }
	void DrawPrefabModeBar();
	struct PrefabModeState {
		bool active = false;
		std::string prefab;
		uint32_t rootId = 0;
		std::string sceneJson; // the scene while the prefab is open
		std::string scenePath;
		std::string sceneName;
		bool sceneDirty = false;
	};
	PrefabModeState prefabMode_;
	bool HasPrefab(const std::string& prefabName);
	void ClearPrefabCache();
	// Creates the prefab's objects right away (editor use / deferred spawn processing). Returns the root id or 0.
	uint32_t InstantiatePrefabNow(const std::string& prefabName, const Vector3& position, uint32_t rootId = 0, const ScriptGameObjectView* rootOverride = nullptr, bool hidden = false);
	void StartScriptsOnSpawnedObject(uint32_t id, bool createInstances);
	void ResetPooledPrefabInstance(uint32_t rootId, const ScriptGameObjectView* rootView);
	// Script entry points: deferred like QueueScriptSpawn. Reuses a despawned instance when one is pooled.
	ScriptGameObjectView* QueuePrefabSpawn(const char* prefabName, const Vector3& position);
	// Prefab instances go back to their pool (hidden, scripts paused); other objects are deleted
	void QueueDespawn(uint32_t id);
	void ProcessPendingDespawns();
	void ResetPrefabPools();
	size_t GetPooledCount(const std::string& prefabName) const;
	PostProcessSettings& PostSettings() { return postSettings_; }
	const PostProcess& PostEffects() const { return postProcess_; }
	// Script entry point: hidden instances go into the pool in ProcessPendingSpawns (no Initialize until they are spawned)
	bool QueuePrefabPrewarm(const char* prefabName, int count);
	void ProcessPendingPrewarms();

	// Runtime scene switching + save data (EditorSuite_Runtime.cpp)
	std::filesystem::path ResolveSceneRequest(const std::string& nameOrPath) const;
	void RequestSceneLoad(const std::string& nameOrPath); // deferred to the end of the frame
	/// LoadScene from scripts leaves this root object and its children in place (Play only; children: false)
	bool KeepAcrossScenes(uint32_t id);
	bool ProcessPendingSceneLoad();
	void RestoreRuntimeSceneOrigin();
	std::filesystem::path GetSaveDataPath() const;
	void SetSaveNumber(const std::string& key, double value);
	double GetSaveNumber(const std::string& key, double fallback);
	void SetSaveString(const std::string& key, const std::string& value);
	bool GetSaveString(const std::string& key, std::string& out);
	bool HasSaveKey(const std::string& key);
	void DeleteAllSaveData();

	// GameObject > Samples (EditorSuite_Samples.cpp)
	bool CreateSurvivorsDemo();
	/// Fixed load for performance runs: 300 chasing enemies + 500 bullets (scripts BenchScripts.*, not in the sample menu)
	bool CreateBenchmarkScene();
	bool CreateThirdPersonDemo();
	bool CancelPendingSpawn(uint32_t id);
	void ProcessPendingSpawns();
	void RetireScriptView(uint32_t id);
	void ClearRetiredScriptViews();

	void TriggerScriptBuild();
	void PollScriptSourceWatcher(float deltaTime);
	/// Recompiles the Object3d shaders from `dir`; the old pipeline stays when anything fails (logged)
	bool ReloadShaders(const std::filesystem::path& dir);
	/// Every 0.5 s: a changed .hlsl/.hlsli in RizealldayEngine/shaders (or ./shaders) is copied to ./shaders and reloaded
	void PollShaderWatcher(float deltaTime);
	void DrainScriptBuildLogs();
	void OpenFileInVisualStudio(const std::filesystem::path& filePath);
	std::filesystem::path ResolveGameScriptsDir() const;
	std::filesystem::path ResolveGameScriptsVcxproj() const;
	std::filesystem::path ResolveSolutionFilePath() const;
	std::string ResolveMsBuildExe();

	// Per-project scripts (EditorSuite_ProjectScripts.cpp): <project>/GameScripts/GameScripts.vcxproj builds
	// <project>/Library/Scripts/GameScripts.dll. Projects without it keep using the engine workspace scripts.
	/// <project>/GameScripts when the open project has its own scripts project, else empty
	std::filesystem::path ProjectScriptsDir() const;
	/// Folder with base/ScriptAPI.h: <install>/SDK/include in a packaged engine, <workspace>/RizealldayEngine in the source tree
	static std::filesystem::path ResolveEngineSdkDir();
	/// <install>/Templates in a packaged engine, <workspace>/Templates in the source tree
	static std::filesystem::path ResolveEngineTemplatesDir();
	/// Creates GameScripts/ (vcxproj, filters, dllmain.cpp, RizeEngine.props) and <Name>.sln in a project
	bool CreateProjectScripts(const std::filesystem::path& projectRoot, const std::string& projectName, std::string* error);
	/// (Re)writes GameScripts/RizeEngine.props with this PC's SDK path; true when it changed
	bool EnsureProjectScriptsProps();
	/// Loads the DLL of the open project (or the workspace DLL) and builds it when it does not exist yet
	void SyncScriptModuleWithProject();
	/// Adds Scripts\<name>.cpp / .h (the ones that exist) to the scripts vcxproj and .filters
	void AddScriptFilesToVcxproj(const std::vector<std::string>& classNames);
	/// Copies engine sample scripts (e.g. Survivors*) into a per-project scripts folder. True when they are there now.
	bool InstallSampleScripts(const std::string& prefix);

	// Engine update (EditorSuite_Update.cpp): Help > Install Engine Update... or a release zip dropped on the window
	/// Extracts the zip and opens the confirmation modal (false + log when it is not an engine package)
	bool BeginEngineUpdate(const std::filesystem::path& zip);
	void DrawEngineUpdateModal();
	void DrawAboutModal();
	void DrawHelpMenu();

	// Scene Management & Serialization (.scene.json)
	std::string SerializeSceneToString() const;
	bool DeserializeSceneFromString(const std::string& json);

	// Per-object / scene-settings JSON (shared by scene files and the undo system).
	// The void* parameters are SceneJson::Json* so this header does not depend on nlohmann/json.
	void WriteGameObjectJson(const GameObject& obj, void* outJson) const;
	void ReadGameObjectJson(const void* json, GameObject& obj);
	void WriteSceneSettingsJson(void* outJson) const;
	void ReadSceneSettingsJson(const void* json);
	bool SaveScene(const std::string& filePath);
	bool LoadScene(const std::string& filePath);
	void NewScene();
	void DiscardUnsavedChanges();
	const std::string& GetCurrentSceneName() const { return currentSceneName_; }
	bool IsSceneDirty() const { return isSceneDirty_; }
	void SetOnRequestTitleScene(std::function<void()> callback) { onRequestTitleScene_ = std::move(callback); }

	// Project Management & Native Folder Picker
	static std::string OpenNativeFolderDialog();
	// Asset import (EditorSuite_Import.cpp): copies files / folders into <project>/Assets without Explorer
	static std::vector<std::string> OpenNativeFilesDialog();
	/// Folder an imported file goes to: the Project panel folder when it is inside Assets/, else Assets/<Textures|Models|Audio|...>
	std::filesystem::path DefaultImportFolder(const std::filesystem::path& source) const;
	/// Copies `sources` (files or folders) into `targetDir` (empty = DefaultImportFolder per file), with companion files
	/// (.mtl + textures of .obj, .bin + images of .gltf, <name>.fbm of .fbx). Returns the imported project-relative paths.
	std::vector<std::string> ImportAssets(const std::vector<std::filesystem::path>& sources, const std::filesystem::path& targetDir = {});
	void ProcessPendingImports();
	/// Recent projects list (Project Hub, Open Project, File > Open Recent). True when a project was opened.
	bool DrawRecentProjectsList(float height);
	bool CreateNewProject(const std::string& parentFolder, const std::string& projectName);
	bool OpenProject(const std::string& folderPath);

	// Project-relative asset paths: stored relative to the project root (or the working directory
	// when no project is open) and resolved back to a loadable path when the asset is loaded
	std::string ToProjectRelativePath(const std::filesystem::path& path) const;
	std::string ResolveAssetPath(const std::string& path) const;
	std::filesystem::path GetScenesDirectory() const;
	std::string NormalizeScenePath(const std::string& input) const;
	void ResetEditorHistory();
	bool IsPathInsideProject(const std::filesystem::path& path) const;

	// Asset database (AssetDatabase.h, EditorSuite_AssetBrowser.cpp): GUIDs in .meta files keep references alive
	// across renames. AssetDb() follows currentProjectRoot_ (rescans when the project changes).
	AssetDatabase& AssetDb() const;
	/// Moves / renames an asset inside Assets/ and repoints every reference in the open scene
	bool MoveAssetAndUpdateReferences(const std::string& from, const std::string& to);
	bool DeleteAssetToRecycleBin(const std::string& path);
	/// Replaces `from` (file or folder prefix) with `to` in the asset paths of the open scene; returns the change count
	int RepointAssetReferences(const std::string& from, const std::string& to);

	// Project Hub (startup "create or open a project" popup) and recent-project list
	void DrawProjectHub();

	// Close confirmation: WinApp asks OnCloseRequested() on WM_CLOSE / File > Exit.
	// With unsaved scene changes it returns false and opens the Save / Don't Save / Cancel popup.
	bool OnCloseRequested();

	// Exported (standalone) games are recognised by the game_build.cfg the exporter writes next to the exe
	static constexpr const char* kExportedGameConfigFile = "game_build.cfg";
	static std::filesystem::path GetExecutableDirectory();
	static bool IsExportedGameFolder(const std::filesystem::path& dir);
	/// True when this process was started from an exported game folder (working dir or exe dir)
	static bool IsExportedGameLaunch();
	void DrawUnsavedChangesModal();
	bool showUnsavedCloseModal_ = false;
	std::string unsavedCloseError_;
	void LoadRecentProjects();
	void AddRecentProject(const std::filesystem::path& projectRoot);

	// Standalone Game Exporter & Preview Mode
	bool ExportStandaloneGame();
	/// Compiles the scripts (Release) for an export. False = failed; `outDll` stays empty when there is no scripts project.
	bool BuildScriptsForExport(std::filesystem::path& outDll);
	/// Folder with the Release exe + DLLs that exports are made from: <install>/Runtime, or bin/Release in the source tree
	std::filesystem::path ResolveRuntimeDir() const;
	bool IsStandaloneMode() const {
#ifdef USE_IMGUI
		return isStandaloneGameMode_ || isStandalonePreview_;
#else
		return true;
#endif
	}
	void SetStandaloneGameMode(bool enable) { isStandaloneGameMode_ = enable; }
	void ToggleStandalonePreview();

	// Undo / Redo (32-state in-memory ring buffer)
	/// Records what changed since the last recorded state as one undo command (call after every edit).
	void PushUndoSnapshot();
	void Undo();
	void Redo();
	/// Clears undo/redo and makes the current scene the new baseline (scene load, new scene, project switch).
	void ResetUndoHistory();

	// Built-in Playable 3D Tetris Template
	void LoadTetrisTemplate();
	void UpdateTetrisGame(float deltaTime, Input* input);
	void RestartTetrisGame();

#ifdef USE_IMGUI
	/// Headless self-tests (`--run-tests [filter]`, EditorSuite_Tests.cpp). Returns the number of failed tests.
	int RunSelfTests(const std::string& filter);
#endif
	/// Process exit code of a `--run-tests` run (0 = all passed, 1 = failures). Always 0 otherwise.
	static inline int sSelfTestExitCode = 0;

private:
	// Gives the self-tests (EditorSuite_Tests.cpp) access to private state
	friend struct EditorSuiteTestAccess;
	/// True while the self-tests run: nothing may touch the user's recent-project list
	bool isRunningTests_ = false;

	void UpdatePhysics(float deltaTime);
	bool BuildPhysicsBody(const GameObject& obj, Physics::BodyDesc& desc);
	std::shared_ptr<const Physics::TriangleMesh> LoadCollisionMesh(const GameObject& obj, const Vector3& scale);
	void SubmitPhysicsJoints();
	void SyncPhysicsForQueries();
	void RestoreSimulatedPoses();
	void ApplyRenderPoses(float alpha);
	void CalculateLightViewProjection();
	void RenderShadowPass(ID3D12GraphicsCommandList* cmdList, const Camera& camera);
	struct ShadowCascade {
		Matrix4x4 lightViewProjection{};
		float end = 0.0f;
		float depthScale = 1.0f;
		float normalScale = 1.0f;
	};
	int ComputeShadowCascades(const Camera& camera, ShadowCascade outCascades[4]);
	Model* ResolveMeshModel(const GameObject& obj);
	void RenderScene3D(ID3D12GraphicsCommandList* cmdList, Camera& camera, bool isEditView);
	void ApplyUnity6DarkTheme();
	// Height of the Play/Build toolbar below the main menu bar; panels are laid out under it
	static constexpr float kTopBarHeight = 40.0f;
	void DrawMainMenuBar();
	void DrawTopPlayBar();
	void DrawHierarchyPanel();
	void DrawInspectorAndDetailsPanel(bool& showParticles);

	// Removable Inspector components (Transform and Scripts are handled separately)
	enum class ComponentKind { Mesh, BoxCollider, SphereCollider, Rigidbody, Camera, Light, ParticleSystem, AudioSource, Mapchip };
	/// Component header with a close (x) button and a right-click "Remove Component" menu.
	/// A removal is only queued here and applied after the Inspector finished drawing.
	bool DrawComponentHeader(const char* label, ComponentKind kind);
	void RemoveComponent(GameObject& obj, ComponentKind kind);
	int pendingComponentRemoval_ = -1; // ComponentKind queued by DrawComponentHeader, -1 = none
	std::string pendingDataComponentRemoval_; // data component name queued for removal, "" = none

	// Component registry glue (EditorSuite_Components.cpp) + built-in Inspector bodies (EditorSuite_InspectorComponents.cpp)
	static void RegisterBuiltinComponents();
	void DrawComponentInspectors(GameObject& obj);
	void DrawAddComponentMenuItems(GameObject& obj);
	void DrawDataComponentInspector(GameObject& obj, const ComponentInfo& info);
	void DrawMeshComponentInspector(GameObject& obj);
	void DrawBoxColliderComponentInspector(GameObject& obj);
	void DrawSphereColliderComponentInspector(GameObject& obj);
	void DrawRigidbodyComponentInspector(GameObject& obj);
	void DrawCameraComponentInspector(GameObject& obj);
	void DrawLightComponentInspector(GameObject& obj);
	void DrawParticleSystemComponentInspector(GameObject& obj);
	void DrawAudioSourceComponentInspector(GameObject& obj);
public:
	// By registry name, built-in or data component. Add/Remove record undo like the Inspector does.
	bool HasComponentByName(const GameObject& obj, const std::string& name) const;
	bool AddComponentByName(GameObject& obj, const std::string& name);
	bool RemoveComponentByName(GameObject& obj, const std::string& name);
	bool CopyComponent(const GameObject& obj, const std::string& name);
	bool PasteComponent(GameObject& obj, const std::string& name);
	bool HasCopiedComponent(const std::string& name = "") const;
	// "components" object of a scene entry (Json* kept opaque like WriteGameObjectJson)
	void WriteDataComponentsJson(const GameObject& obj, void* outObjectJson) const;
	void ReadDataComponentsJson(const void* objectJson, GameObject& obj) const;
private:
	void DrawViewportsPanel();
	void DrawProjectAndConsolePanel();
	void DrawCppScriptGeneratorModal();
	void DrawNewProjectModal();
	void DrawOpenProjectModal();
	void DrawBuildSettingsModal();
	void DrawSaveSceneAsModal();
	void DrawOpenSceneModal();
	void DrawSkyboxSettingsUI();
	void DrawPostProcessSettingsUI();
	void RefreshAvailableCubemaps();
	void DrawTetrisHUD(const ImVec2& vpPos, const ImVec2& vpSize);
	void SelectAssetForPreview(const std::filesystem::path& assetPath);
	void ReleasePreviewResources();

	// Viewport Overlays and Gizmos
	void DrawSceneViewOverlays(const ImVec2& vpPos, const ImVec2& vpSize);
	void DrawFloatingToolStrip(const ImVec2& vpPos, const ImVec2& vpSize);
	void DrawOrientationCompass(const ImVec2& vpPos, const ImVec2& vpSize);
	void DrawFloatingBottomBar(const ImVec2& vpPos, const ImVec2& vpSize);
	void DrawTransformGizmo(const ImVec2& vpPos, const ImVec2& vpSize);
	void DrawColliderWireframes(const ImVec2& vpPos, const ImVec2& vpSize);
	void DrawCameraFrustum(const ImVec2& vpPos, const ImVec2& vpSize, const GameObject& camObj);
	void DrawLightGizmo(const ImVec2& vpPos, const ImVec2& vpSize, const GameObject& lightObj);

	// 3D Math Projection Helper
	bool WorldToScreen(const Vector3& worldPos, const Camera& cam, const ImVec2& vpPos, const ImVec2& vpSize, ImVec2& outScreenPos, float& outDepth);

	// Hazel-style Vec3 UI control with Reset buttons for X, Y, Z
	bool DrawVec3Control(const std::string& label, Vector3& values, float resetValue = 0.0f, float columnWidth = 85.0f, float displayScale = 1.0f, const char* overrideKey = nullptr);
	void DrawPrefabOverrideSideline(GameObject& obj, const char* overrideKey);

	// Hierarchy Popup Menu
	void DrawHierarchyCreationMenu();

	// Mapchip Editor inside Inspector / Details
	void DrawMapchipInspectorSection();
	bool SaveMapchipCSV(const std::string& filePath);
	bool LoadMapchipCSV(const std::string& filePath);
	bool ExportMapchipJSON(const std::string& filePath);

	// Asset Browser File System helpers
	void DrawDirectoryTreeNode(const std::filesystem::path& dirPath);
	void RefreshAssetBrowser();

	// C++ Class Generator I/O
	bool GenerateCppScriptFiles(const std::string& className, const std::string& folderPath, bool includeCollision);

	void LogMessage(const std::string& msg, int logType = 0); // 0: Info, 1: Warning, 2: Error

private:
	DirectXCommon* dxCommon_ = nullptr;
	Camera* gameCamera_ = nullptr;
	Camera editorCamera_{};
	LightGroup* lights_ = nullptr;
	ParticleQuad* particleSystem_ = nullptr;
	Input* currentInput_ = nullptr;
	float currentDeltaTime_ = 0.016f;  // scaled by timeScale_ while the game runs
	float unscaledDeltaTime_ = 0.016f;
	// Game feel (EditorSuite_ScriptQueries.cpp): ShakeCamera / Hitstop from scripts
	float shakeStrength_ = 0.0f;
	float shakeDuration_ = 0.0f;
	float shakeLeft_ = 0.0f;
	float shakeTime_ = 0.0f;
	float hitstopLeft_ = 0.0f;
	double physicsAccumulator_ = 0.0; // leftover time for the next fixed physics step
	std::unique_ptr<Physics::PhysicsWorld> physicsWorld_; // created on first use
	bool physicsQueryDirty_ = true; // objects may have moved since the physics world last saw them
	std::unordered_map<std::string, std::shared_ptr<const Physics::TriangleMesh>> collisionMeshes_; // by model path + scale
	struct RenderPose {
		Vector3 previousPosition{};
		Quaternion previousRotation = Quaternion::Identity();
		Vector3 simPosition{};
		Quaternion simRotation = Quaternion::Identity();
	};
	std::unordered_map<uint32_t, RenderPose> renderPoses_; // rigidbodies; simulated pose kept while the blended one is shown
	bool renderPosesApplied_ = false;
	// Navigation grid for FindPath, built from static colliders on first use (EditorSuite_ScriptQueries.cpp)
	NavGrid navGrid_;
	bool navGridValid_ = false;
	float navCellSize_ = 0.5f;
	float navAgentRadius_ = 0.4f;
	float navFloorY_ = 0.0f;  // floor height the grid was built for
	// Input actions
	InputActions inputActions_ = InputActions::Defaults();
	std::filesystem::path inputActionsRoot_;
	bool showInputActionsWindow_ = false;
	std::optional<InputActions> inputWindowWorking_; // edited copy in Options > Input Actions
	std::filesystem::path inputWindowRoot_;
	std::string inputCaptureAction_; // waiting for a key to bind to this action
	bool inputWindowDirty_ = false;
	char inputNewActionBuf_[64] = "";
	float timeScale_ = 1.0f;           // set by scripts (SetTimeScale); reset when Play starts/stops
	double gameTime_ = 0.0;            // scaled seconds since Play started
	// Where the Game view image was drawn last frame (x, y, width, height in client pixels); 0 width = unknown
	float gameViewRect_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	void RecordGameViewRect(float x, float y, float w, float h) {
		gameViewRect_[0] = x; gameViewRect_[1] = y; gameViewRect_[2] = w; gameViewRect_[3] = h;
	}
	// Mouse position inside the Game view in pixels (0,0 = top-left); false if the mouse is outside or the view is hidden
	bool GetGameViewMousePosition(float& outX, float& outY, float& outW, float& outH) const;
	// Intersects the game camera's ray through the mouse with the horizontal plane y = planeY
	bool GetMouseWorldOnPlane(float planeY, Vector3& outPoint) const;
	bool ScreenPointToRay(const Vector2& canvasPos, Vector3& outOrigin, Vector3& outDirection) const;
	bool WorldToCanvas(const Vector3& worldPos, Vector2& outCanvasPos) const;
	// Cursor lock requested by a script; applied every frame while the window has focus (EditorSuite_ScriptQueries.cpp)
	bool cursorLocked_ = false;
	bool cursorHidden_ = false;
	bool MouseInputAllowed() const; // locked, or inside the game view
	void UpdateCursorLock();

	// Script callbacks. Editor builds turn a crash in a script into a stopped Play session; Release keeps the crash dump.
#ifdef USE_IMGUI
	static bool RunScriptGuarded(void (*fn)(void*), void* ctx, unsigned long& outCode);
	void OnScriptFault(ScriptBase* instance, uint32_t objectId, const char* callback, unsigned long code);
	void HandleScriptFault(); // at a safe point in Update: stop Play, restore the scene, reload the DLL
	bool scriptFaultPending_ = false;
	std::string scriptFaultMessage_;
	int scriptFaultCount_ = 0;
#endif
	template <typename F>
	void CallScript(ScriptBase* instance, uint32_t objectId, const char* callback, F&& fn)
	{
#ifdef USE_IMGUI
		if (scriptFaultPending_) return; // nothing else runs until the session is stopped
		using Fn = std::remove_reference_t<F>;
		unsigned long code = 0;
		if (!RunScriptGuarded([](void* ctx) { (*static_cast<Fn*>(ctx))(); }, &fn, code)) {
			OnScriptFault(instance, objectId, callback, code);
		}
#else
		(void)instance; (void)objectId; (void)callback;
		fn();
#endif
	}
public:
	// Pure helper (tested): ray through NDC (x right, y up) intersected with y = planeY
	static bool ScreenRayToPlaneY(const Matrix4x4& viewProjection, float ndcX, float ndcY, float planeY, Vector3& outPoint);
private:
	DXCManager dxc_{};
	uint32_t particleTextureHandle_ = 0;

	// Particle System Helpers
	void EnsureParticleQuadInitialized(GameObject& obj);
	void SyncGameObjectToParticleQuad(GameObject& obj);
	void ApplyParticlePreset(GameObject& obj, int presetIndex);

	RenderTexture editViewRT_{};
	RenderTexture gameViewRT_{};
	RenderTexture modelPreviewRT_{};
	// Every view renders linear HDR here; PostProcess resolves it into the 8-bit targets above (Release: the back buffer)
	RenderTexture editSceneRT_{};
	RenderTexture gameSceneRT_{};
	RenderTexture previewSceneRT_{};
	uint32_t gameSceneSrvIndex_ = DirectXCommon::kInvalidSrvIndex;
	uint32_t gameDepthSrvIndex_ = DirectXCommon::kInvalidSrvIndex;
	void InitializeGameSceneTarget(uint32_t width, uint32_t height);
	PostProcess postProcess_{};
	EnvironmentProbe environmentProbe_{};
	bool enableInstancing_ = true;
	struct MaterialAssetEntry {
		PbrMaterialComponent values;
		bool dirty = false;
		bool failed = false;
	};
	std::unordered_map<std::string, MaterialAssetEntry> materialAssets_; // by asset path; every object using one follows it
	std::unordered_map<std::string, uint32_t> materialTextures_;
	const PbrMaterialComponent* FindMaterialAsset(const std::string& assetPath);
	bool WriteMaterialAssetFile(const std::string& assetPath, const PbrMaterialComponent& values);
	void SaveMaterialAsset(GameObject& obj, PbrMaterialComponent& material);
	uint32_t LoadMaterialTexture(const char* assetPath, bool linear);
	bool ApplyPbrMaterial(GameObject& obj, Material& material, MaterialMaps& maps, uint32_t& albedoHandle);
	PostProcessSettings postSettings_{}; // saved per scene
	PostProcessSettings playModePostSettings_{};
	Camera previewCamera_{};
	std::unique_ptr<LightGroup> previewLights_;
	std::unique_ptr<Model> previewModel_;
	std::unique_ptr<Object3d> previewObject3d_;
	std::string currentPreviewAssetPath_ = "";
	uint32_t currentPreviewTextureHandle_ = 0;
	uint32_t previewVertexCount_ = 0;
	uint32_t previewTriangleCount_ = 0;
	float previewModelScale_ = 1.0f;
	Vector3 previewModelOffset_ = { 0.0f, 0.0f, 0.0f };
	float previewRotationY_ = 0.0f;
	bool focusDetailsTab_ = false;
	Skybox skybox_{};
	bool showLightingSettings_ = false;
	bool showProfiler_ = false;
	void DrawProfilerWindow();
	int selectedCubemapIndex_ = 0;
	std::vector<std::string> availableCubemaps_;
	ShadowMap shadowMap_{};
	WorldTransform shadowWorldTransform_{};
	Matrix4x4 lightViewProjectionMatrix_{};

	EditorSceneState sceneState_ = EditorSceneState::Edit;

	// GameObjects
	std::vector<GameObject> gameObjects_;
	std::unordered_map<uint32_t, size_t> idIndexCache_; // see FindGameObjectIndexById
	int selectedObjectIndex_ = 0;
	std::vector<uint32_t> extraSelection_; // multi-select beyond the primary, by id (EditorSuite_Selection.cpp)
	uint32_t nextObjectId_ = 1;
	char hierarchySearchBuf_[128] = "";
	std::vector<int> pendingDeletes_;
	int pendingDuplicateIndex_ = -1;
	// Hierarchy cycle warning guard (one log per cycle set) and rigidbody-on-child warning guard
	bool hierarchyCycleWarned_ = false;
	std::unordered_set<uint32_t> childRigidbodyWarned_;

	// Objects instantiated by scripts are queued here and created at a safe point,
	// so gameObjects_ never reallocates while a script callback is running.
	struct PendingSpawn {
		uint32_t id = 0;
		std::string name;
		int primitiveType = 0;
		std::string prefabName;  // non-empty: instantiate this prefab instead of a primitive
		bool reusePooled = false; // id refers to an existing, despawned prefab instance
	};
	std::vector<PendingSpawn> pendingSpawns_;
	std::vector<std::pair<std::string, int>> pendingPrewarms_;

	// Prefabs (EditorSuite_Prefabs.cpp). Files: <project>/Assets/Prefabs/<Name>.prefab.json
public:
	struct PrefabCache; // parsed prefab JSON by name (defined in the .cpp)
private:
	std::shared_ptr<PrefabCache> prefabCache_;
	std::unordered_map<std::string, std::vector<uint32_t>> prefabPools_; // despawned root ids per prefab
	std::vector<uint32_t> pendingDespawns_;

	// Runtime scene switching (EditorSuite_Runtime.cpp)
	std::string pendingSceneLoad_;
	std::unordered_set<uint32_t> persistentIds_; // KeepAcrossScenes roots, cleared on Play / Stop
	bool runtimeSceneSwitched_ = false; // editor Play mode loaded another scene; Stop restores the origin
	std::string runtimeOriginScenePath_;
	std::string runtimeOriginSceneName_;
	std::string runtimeOriginSettings_;
	bool runtimeOriginDirty_ = false;

	// Save data (loaded lazily, written on change)
	void EnsureSaveDataLoaded();
	void WriteSaveData();
	std::string saveDataLoadedFrom_;
	std::map<std::string, double> saveNumbers_;
	std::map<std::string, std::string> saveStrings_;
	// Set while ProcessPendingSpawns() runs: skips undo snapshots, selection changes and per-object logs
	bool isSpawningFromScript_ = false;

	// Scene State & Serialization
	std::string currentSceneName_ = "SampleScene";
	std::string currentScenePath_ = "Assets/Scenes/SampleScene.scene.json";
	bool isSceneDirty_ = false;
	bool isEditingSceneName_ = false;
	char sceneNameEditBuf_[128] = "SampleScene";

	// Scene Modals
	bool showSaveSceneAsModal_ = false;
	char saveSceneAsPathBuf_[256] = "Assets/Scenes/SampleScene.scene.json";
	bool showOpenSceneModal_ = false;
	char openScenePathBuf_[256] = "Assets/Scenes/SampleScene.scene.json";
	std::function<void()> onRequestTitleScene_;

	// Undo / Redo System (32-state ring buffer storing JSON scene string)
	// ---- Command-based undo ----
	// A command stores only the GameObjects that were created / deleted / modified (as their scene JSON),
	// plus the scene settings (skybox, mapchip) when they changed. Undo/Redo patch just those objects.
	struct UndoObjectChange {
		uint32_t id = 0;
		int indexBefore = -1;   // position in gameObjects_ before the edit (-1: did not exist)
		int indexAfter = -1;    // position after the edit (-1: deleted)
		std::string before;     // JSON before (empty when created)
		std::string after;      // JSON after (empty when deleted)
	};
	struct UndoCommand {
		std::vector<UndoObjectChange> changes;
		std::string settingsBefore;  // empty when the scene settings did not change
		std::string settingsAfter;
		double timeSeconds = 0.0;    // when it was recorded (used to merge continuous drags)
	};
	struct UndoSceneState {
		std::vector<uint32_t> order;                          // object ids in gameObjects_ order
		std::unordered_map<uint32_t, std::string> objects;    // id -> object JSON
		std::string settings;                                 // skybox + mapchip JSON
	};
	UndoSceneState CaptureUndoState() const;
	void ApplyUndoCommand(const UndoCommand& cmd, bool undo);

	std::vector<UndoCommand> undoCommands_;
	std::vector<UndoCommand> redoCommands_;
	std::vector<UndoCommand> prefabModeUndo_; // the scene's history while a prefab is open
	std::vector<UndoCommand> prefabModeRedo_;
	UndoSceneState undoBaseline_;
	bool undoMergeAllowed_ = false; // false right after Undo/Redo/reset so the next edit starts a new command
	bool undoPushPending_ = false;  // an edit was made that PushUndoSnapshot() has not recorded yet
	static constexpr size_t kMaxUndoCommands = 200;
	// Edits of the same objects closer together than this are merged into one command (e.g. one slider drag)
	static constexpr double kUndoMergeWindowSeconds = 0.6;

	// Standalone Game Exporter & Preview Mode
	bool isStandaloneGameMode_ = false;
	bool isStandalonePreview_ = false;
	bool showBuildSettingsModal_ = false;
	char buildOutputDirBuf_[512] = "Build";
	std::filesystem::path exportRuntimeDirOverride_; // self-tests only
	// `--smoke-frames <N>`: run N frames, write smoke_result.txt, exit 0 / 1 (Tools/SmokeTestExport.ps1)
	static constexpr const char* kSmokeResultFile = "smoke_result.txt";
	int smokeFramesLeft_ = 0;
	std::string smokeError_;
	void FinishSmokeRun();
	void RecordSmokeFrame();
	std::string SmokeTimingReport() const;
	int smokeFrameIndex_ = 0;
	std::vector<float> smokeFrameMs_;
	std::map<std::string, double> smokeScopeMs_;
	uint32_t smokeDrawCalls_ = 0;
	char buildGameTitleBuf_[128] = "RizealldayGame";
	int buildResolutionIndex_ = 0; // 0: 1920x1080, 1: 1280x720, 2: 2560x1440
	bool buildFullscreen_ = false;
	char buildStartupSceneBuf_[256] = "Assets/Scenes/SampleScene.scene.json";

	// New / Open Project Wizard State
	bool showNewProjectModal_ = false;
	bool newProjectInitGit_ = true;        // New Project: git init + first commit
	std::string projectEngineWarning_;     // project.json names a newer engine than this editor
	// Engine update from a release zip (EditorSuite_Update.cpp)
	bool showEngineUpdateModal_ = false;
	bool showAboutModal_ = false;
	std::filesystem::path pendingUpdateRoot_;   // extracted package (holds engine.json)
	std::string pendingUpdateVersion_;
	int pendingUpdateScriptApi_ = 0;
	std::string engineUpdateError_;
	char projectNameBuf_[128] = "NewGameProject";
	char projectFolderBuf_[512] = "D:\\Projects";
	bool showOpenProjectModal_ = false;
	char openProjectFolderBuf_[512] = "D:\\Projects\\NewGameProject";
	std::filesystem::path currentProjectRoot_ = "";
	std::string projectModalError_;
	bool showProjectHub_ = false;
	std::vector<std::filesystem::path> recentProjects_;
	int selectedRecentProject_ = -1;
	// Project of this editor session; EditorSuite is re-created on Title <-> Editor transitions
	static inline std::filesystem::path sSessionProjectRoot_;
	mutable std::unordered_map<std::string, std::string> resolvedAssetCache_;
	mutable std::filesystem::path resolvedAssetRoot_;

	// Physics State & Play Mode Snapshots
	struct TransformSnapshot {
		uint32_t id = 0;
		Vector3 translation = { 0.0f, 0.0f, 0.0f };
		Vector3 rotation = { 0.0f, 0.0f, 0.0f };
		Vector3 scale = { 1.0f, 1.0f, 1.0f };
		Quaternion rotationQuat = Quaternion::Identity();
		Vector3 velocity = { 0.0f, 0.0f, 0.0f };
		bool isGrounded = false;
	};
	std::vector<TransformSnapshot> playModeSnapshots_;
	std::vector<GameObject> playModeObjectSnapshots_;
	bool simulatePhysicsInEditMode_ = false;
	bool stepSingleFrame_ = false;

	// Viewport & Gizmo State
	GizmoType currentGizmo_ = GizmoType::Move;
	GizmoAxis activeGizmoAxis_ = GizmoAxis::None;
	bool isDraggingGizmo_ = false;
	ImVec2 gizmoDragStartMouse_{};
	ImVec2 gizmoDragStartAxis_{}; // screen direction of the dragged axis when the drag began
	Quaternion gizmoStartRotationQuat_ = Quaternion::Identity();
	ImVec2 gizmoRotatePrevMouse_{};
	float gizmoRotateSweep_ = 0.0f; // radians swept around the centre since the rotate drag began
	Vector3 gizmoStartTranslation_{};
	Vector3 gizmoStartRotation_{};
	Vector3 gizmoStartScale_{};
	Vector3 gizmoStartLocalOffset_{};

	bool showGrid_ = true;
	bool showGizmos_ = true;
	bool showWireframe_ = false;
	bool enableLighting_ = true;
	bool enableFrustumCulling_ = true;
	int lastVisibleObjects_ = 0;
	int lastCulledObjects_ = 0;
	bool isEditingColliderBounds_ = false;

	// Navigation
	bool isFlightNavigating_ = false;
	ImVec2 prevMousePos_{};

	// Mapchip Data
	int mapRows_ = 10;
	int mapCols_ = 15;
	float mapTileSize_ = 1.0f;
	int selectedBrushTile_ = 1; // 0: Empty, 1: Wall, 2: Spike, 3: Spawn, 4: Goal
	std::vector<std::vector<int>> mapGrid_;
	char mapFilePathBuf_[256] = "Resources/levels/stage01.csv";
	char mapJsonPathBuf_[256] = "Resources/levels/stage01.json";
	bool renderMapchip3DBlocks_ = true;

	// C++ Script Generator & Hot Reload State
	ScriptModule scriptModule_{};
	EngineAPI engineApi_{};
	// Script views keyed by GameObject id. Heap-allocated so pointers handed to scripts stay valid
	// while gameObjects_ grows or shrinks. Views of deleted objects are retired (isActive = false)
	// instead of freed, and only released when no script can still hold them.
	std::unordered_map<uint32_t, std::unique_ptr<ScriptGameObjectView>> scriptObjectViews_;
	std::vector<std::unique_ptr<ScriptGameObjectView>> retiredScriptViews_;
	static constexpr size_t kMaxRetiredScriptViews = 4096;

	// Collision contacts (directed pair keys: (selfId << 32) | otherId), this frame and the previous one
	std::vector<uint64_t> frameContacts_;
	std::unordered_set<uint64_t> frameContactSet_;
	std::vector<uint64_t> previousContacts_;
	std::unordered_set<uint64_t> previousContactSet_;
	struct DelayedDespawn {
		uint32_t id = 0;
		float remaining = 0.0f;
	};
	std::vector<DelayedDespawn> delayedDespawns_;
	struct PendingReparent {
		uint32_t childId = 0;
		int parentId = -1;
		bool keepWorldTransform = true;
	};
	std::vector<PendingReparent> pendingReparents_;
	struct PendingParticleBurst {
		uint32_t id = 0;
		int count = 0;
		bool restart = false; // clear live particles first (pooled one-shot effects)
	};
	std::vector<PendingParticleBurst> pendingParticleBursts_;
	// Pairs dispatched by the last DispatchCollisionEvents() (counted even when no script listens)
	struct CollisionEventCounts {
		int enter = 0;
		int stay = 0;
		int exit = 0;
	};
	CollisionEventCounts lastCollisionEvents_;
	uint32_t musicVoice_ = 0;
	std::string musicPath_;
	struct AudioFollower { uint32_t voice; uint32_t objectId; };
	std::vector<AudioFollower> audioFollowers_;
	/// Plays the object's Audio Source. preview = flat, for listening to the clip in the Inspector.
	bool StartAudioSource(GameObject& obj, bool preview = false);
	/// Once a frame after the camera sync: the game camera is the listener, 3D voices follow their objects
	void UpdateAudio(float deltaTime = 0.0f);
	/// A Static collider between the ears and the sound (the collider of the owner itself does not count)
	bool IsSoundOccluded(const Vector3& ears, const Vector3& source, uint32_t ownerId);

	bool showScriptGenModal_ = false;
	int scriptGenTargetObjectIndex_ = -1; // >= 0 when opened via [Add Component -> Script -> New Script...]
	char scriptClassNameBuf_[128] = "PlayerController";
	char scriptOutputDirBuf_[256] = "GameScripts/Scripts";
	bool scriptIncludeCollision_ = true;
	std::string scriptGenErrorMsg_ = "";

	bool autoBuildScripts_ = true;
	std::atomic<bool> isBuildingScripts_{ false };
	std::atomic<bool> queuedScriptRebuild_{ false };
	std::atomic<int> lastScriptBuildStatus_{ 0 }; // 0: Idle/Ready, 1: Succeeded, 2: Failed
	std::thread scriptBuildThread_;
	std::mutex buildLogMutex_;
	struct PendingBuildLog {
		std::string line;
		int type = 0;
	};
	std::vector<PendingBuildLog> pendingBuildLogs_;
	char msbuildFallbackPathBuf_[512] = "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe";
	std::string resolvedMsBuildPath_ = "";

	// Source File Watcher State (GameScripts/Scripts/*.cpp|*.h)
	float scriptSourcePollTimer_ = 0.0f;
	float scriptSourceDebounceTimer_ = 0.0f;
	bool scriptSourceChangePending_ = false;
	bool scriptSourceWatcherInitialized_ = false;
	// Shader hot reload (EditorSuite_Shaders.cpp). Overrides are for the self-tests.
	std::filesystem::path shaderWatchDirOverride_;
	std::filesystem::path shaderDeployDirOverride_;
	std::map<std::string, std::filesystem::file_time_type> shaderStamps_;
	bool shaderWatcherInitialized_ = false;
	float shaderPollTimer_ = 0.0f;
	int shaderReloadCount_ = 0;
	std::map<std::string, std::filesystem::file_time_type> scriptSourceTimestamps_;

	// Asset Browser State
	std::filesystem::path currentDirectory_ = "Resources";
	std::filesystem::path selectedAssetPath_ = "";
	char assetSearchBuf_[128] = "";
	mutable AssetDatabase assetDatabase_;
	// Animator (EditorSuite_Animator.cpp / EditorSuite_AnimatorWindow.cpp)
	struct CachedAnimatorController {
		std::shared_ptr<AnimatorController> controller;
		std::filesystem::file_time_type stamp{};
		std::chrono::steady_clock::time_point checkedAt{};
	};
	std::unordered_map<std::string, CachedAnimatorController> animatorControllers_;
	bool showAnimatorWindow_ = false;
	bool showTimelineWindow_ = false;
	// Auto-save (EditorSuite_AutoSave.cpp)
	bool autoSaveEnabled_ = true;
	double autoSaveIntervalSeconds_ = 120.0;
	double lastAutoSaveTime_ = 0.0;
	bool autoSaveInTests_ = false;
	std::string pendingAutoSaveRestore_; // copy offered by the modal
	void UpdateAutoSave(double now);
	void DiscardAutoSave(const std::string& scenePath);
	void CheckForAutoSave(const std::string& scenePath);
	bool RestoreAutoSave();
	void DrawAutoSaveRestoreModal();
	std::unordered_map<std::string, std::shared_ptr<Timeline>> timelines_; // by asset path
	int timelineEventsFired_ = 0;
	float timelineCursor_ = 0.0f;
	bool timelinePreview_ = false;
	bool timelinePreviewPlaying_ = false;
	bool timelinePreviewLoop_ = true;
	int timelineSelectedTrack_ = -1;
	int timelineSelectedKey_ = -1;
	struct TimelineRestore { uint32_t id; Vector3 translation; Vector3 rotation; Vector3 scale; };
	std::vector<TimelineRestore> timelineRestore_;
	// Gizmo space & snapping + Inspector component search/clipboard
	bool gizmoLocalSpace_ = false;
	bool gizmoSnapEnabled_ = false;
	float gizmoMoveSnap_ = 0.5f;
	float gizmoRotateSnapDeg_ = 15.0f;
	float gizmoScaleSnap_ = 0.1f;
	char addComponentSearchBuf_[128] = "";
	std::string copiedComponentName_;
	std::string copiedComponentJson_;
	bool showSkeletonOverlay_ = false;
	uint32_t animatorPreviewObject_ = 0; // Edit mode: object whose Animator previews animatorPreviewState_
	uint32_t animatorPreviewState_ = 0;
	// Animator window (canvas state and selection)
	std::string animatorWindowAsset_;     // controller being edited
	Vector2 animatorPan_{ 300.0f, 200.0f };
	float animatorZoom_ = 1.0f;
	uint32_t animatorSelectedState_ = 0;
	int animatorSelectedTransition_ = -1;
	bool animatorSelectedEntry_ = false;
	uint32_t animatorLinkFrom_ = 0;       // dragging a new transition from this state (or AnimTransition::kAnyState)
	bool animatorLinking_ = false;
	bool animatorDraggingNode_ = false;
	std::vector<std::string> animatorUndo_; // controller JSON snapshots (window-local undo)
	std::vector<std::string> animatorRedo_;
	std::string animatorCommittedJson_;   // JSON of the version on disk (undo baseline)
	std::optional<AnimatorController> animatorWorking_; // edited copy; saved by AnimatorCommit()
	std::string animatorWorkingAsset_;
	bool animatorDirty_ = false;
	std::vector<std::string> pendingImports_; // dropped from Explorer, imported in Update
	std::filesystem::path assetRenameTarget_;
	char assetRenameBuf_[260] = "";
	std::filesystem::path assetDeleteTarget_;
	bool openAssetRenamePopup_ = false;
	bool openAssetDeletePopup_ = false;

	// Console State
	struct LogEntry {
		std::string message;
		int type = 0; // 0: Info, 1: Warning, 2: Error
		std::string timestamp;
	};
	std::vector<LogEntry> consoleLogs_;
	int infoCount_ = 0;
	int warningCount_ = 0;
	int errorCount_ = 0;
	bool collapseLogs_ = false;

	// Right panel active tab
	int rightPanelTab_ = 0; // 0: Inspector, 1: Details
	int bottomPanelTab_ = 0; // 0: Project, 1: Console

	// Built-in Playable 3D Tetris Game Mode
	bool isTetrisActive_ = false;
	int tetrisBoard_[20][10] = {}; // 0 = empty, 1..7 = tetromino block type
	uint32_t tetrisBlockIds_[20][10] = {}; // GameObject ID per grid cell
	int currentTetrominoType_ = 0; // 0: I, 1: O, 2: T, 3: S, 4: Z, 5: J, 6: L
	int currentTetrominoRot_ = 0;  // 0, 1, 2, 3
	int currentTetrominoX_ = 4;
	int currentTetrominoY_ = 18;
	float tetrisFallTimer_ = 0.0f;
	float tetrisFallInterval_ = 0.7f;
	int tetrisScore_ = 0;
	int tetrisLinesCleared_ = 0;
	int tetrisLevel_ = 1;
	bool tetrisGameOver_ = false;
	std::vector<uint32_t> currentPieceCubeIds_;
};
