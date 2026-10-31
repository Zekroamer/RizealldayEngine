#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include "math/Vector2.h"
#include "math/Vector3.h"
#include "math/Vector4.h"
#include "base/ScriptTimers.h"
#include "math/Matrix4x4.h"
#include "math/Quaternion.h"

#ifdef GAMESCRIPTS_EXPORTS
#define GAMESCRIPTS_API extern "C" __declspec(dllexport)
#else
#define GAMESCRIPTS_API extern "C" __declspec(dllimport)
#endif

/// Plain C-compatible GameObject view structure used across the DLL boundary.
/// Contains no std::string, std::vector, or DirectX 12 resources.
struct ScriptGameObjectView {
	uint32_t id = 0;
	char name[128] = "GameObject";
	char tag[64] = "Untagged";
	bool isActive = true;
	bool isStatic = false;
	uint32_t layer = 0;                     // ScriptLayer value (0..31)
	uint32_t collisionMask = 0xFFFFFFFFu;   // bit N set = collides with layer N (use LayerBit())

	Vector3 translation = { 0.0f, 0.0f, 0.0f };
	Vector3 rotation = { 0.0f, 0.0f, 0.0f };
	Quaternion rotationQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
	Vector3 scale = { 1.0f, 1.0f, 1.0f };
	Vector3 velocity = { 0.0f, 0.0f, 0.0f };

	bool isGrounded = false;
	bool isColliding = false;
	bool dirtyFromScript = false;
};

#ifdef GAMESCRIPTS_EXPORTS
using GameObject = ScriptGameObjectView;
#else
struct GameObject;
#endif

/// Collision layers. Two colliders only collide when each one's collisionMask contains the other's layer bit.
/// Names are shown in the Inspector; layers 8..31 are free for game use.
enum ScriptLayer : uint32_t {
	LAYER_DEFAULT = 0,
	LAYER_PLAYER = 1,
	LAYER_ENEMY = 2,
	LAYER_PLAYER_BULLET = 3,
	LAYER_WALL = 4,
	LAYER_TRIGGER = 5,
	LAYER_ENEMY_BULLET = 6,
	LAYER_PICKUP = 7,
	LAYER_NAMED_COUNT = 8,
};
constexpr uint32_t LayerBit(uint32_t layer) { return 1u << (layer & 31u); }
constexpr uint32_t kCollideWithAll = 0xFFFFFFFFu;

/// Keyboard key codes for IsKeyDown / IsKeyTriggered (DirectInput scan codes, so scripts need no <dinput.h>).
/// Values equal the matching DIK_* constants; a self-test checks this.
enum ScriptKey : int {
	KEY_ESCAPE = 0x01,
	KEY_1 = 0x02, KEY_2 = 0x03, KEY_3 = 0x04, KEY_4 = 0x05, KEY_5 = 0x06,
	KEY_6 = 0x07, KEY_7 = 0x08, KEY_8 = 0x09, KEY_9 = 0x0A, KEY_0 = 0x0B,
	KEY_BACKSPACE = 0x0E, KEY_TAB = 0x0F,
	KEY_Q = 0x10, KEY_W = 0x11, KEY_E = 0x12, KEY_R = 0x13, KEY_T = 0x14, KEY_Y = 0x15,
	KEY_U = 0x16, KEY_I = 0x17, KEY_O = 0x18, KEY_P = 0x19,
	KEY_ENTER = 0x1C, KEY_LCTRL = 0x1D,
	KEY_A = 0x1E, KEY_S = 0x1F, KEY_D = 0x20, KEY_F = 0x21, KEY_G = 0x22, KEY_H = 0x23,
	KEY_J = 0x24, KEY_K = 0x25, KEY_L = 0x26,
	KEY_LSHIFT = 0x2A,
	KEY_Z = 0x2C, KEY_X = 0x2D, KEY_C = 0x2E, KEY_V = 0x2F, KEY_B = 0x30, KEY_N = 0x31, KEY_M = 0x32,
	KEY_RSHIFT = 0x36, KEY_LALT = 0x38, KEY_SPACE = 0x39,
	KEY_F1 = 0x3B, KEY_F2 = 0x3C, KEY_F3 = 0x3D, KEY_F4 = 0x3E, KEY_F5 = 0x3F,
	KEY_F6 = 0x40, KEY_F7 = 0x41, KEY_F8 = 0x42, KEY_F9 = 0x43, KEY_F10 = 0x44,
	KEY_F11 = 0x57, KEY_F12 = 0x58,
	KEY_RCTRL = 0x9D, KEY_RALT = 0xB8,
	KEY_UP = 0xC8, KEY_LEFT = 0xCB, KEY_RIGHT = 0xCD, KEY_DOWN = 0xD0,
};

/// Plain transform data passed across the EngineAPI function-pointer table.
struct ScriptTransformData {
	Vector3 translation = { 0.0f, 0.0f, 0.0f };
	Vector3 rotation = { 0.0f, 0.0f, 0.0f };
	Quaternion rotationQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
	Vector3 scale = { 1.0f, 1.0f, 1.0f };
};

/// Result of Raycast (objectId 0 = nothing hit)
struct ScriptRaycastHit {
	uint32_t objectId = 0;
	float distance = 0.0f;
	Vector3 point = { 0.0f, 0.0f, 0.0f };
	Vector3 normal = { 0.0f, 1.0f, 0.0f };
};

/// Result of MoveCharacter
struct ScriptCharacterMove {
	Vector3 moved = { 0.0f, 0.0f, 0.0f }; // displacement actually applied
	bool grounded = false;                // standing on a collider after the move
	bool hitWall = false;                 // horizontal motion was blocked
	bool hitCeiling = false;              // upward motion was blocked
};

/// Scene look readable/writable from scripts (damage flash, slow-motion grading, cinematic bars). Stop restores the scene's values.
struct ScriptPostProcess {
	bool enabled = false;
	bool toneMapping = true;
	bool bloom = true;
	bool fxaa = true;
	float exposure = 1.0f;
	float bloomThreshold = 1.0f;
	float bloomIntensity = 0.4f;
	float contrast = 1.0f;
	float saturation = 1.0f;
	Vector3 tint = { 1.0f, 1.0f, 1.0f };
	float vignette = 0.0f;
	float letterbox = 0.0f; // fraction of the height covered at the top and at the bottom
};

/// Mesh Renderer settings readable/writable from scripts (hit flash, fade, hide)
struct ScriptMaterialData {
	Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
	bool visible = true;         // Mesh Renderer enabled
	bool lit = true;
	int lightingType = 4;        // 0 = Unlit, 1 = Lambert, 2 = Half-Lambert, 3 = Phong, 4 = Blinn-Phong, 5 = Toon
	float shininess = 32.0f;
	Vector2 uvOffset = { 0.0f, 0.0f };
	Vector2 uvTiling = { 1.0f, 1.0f };
	float outlineWidth = 0.0f;  // world units, 0 = no outline (looks best with lightingType 5 = Toon)
	Vector4 outlineColor = { 0.0f, 0.0f, 0.0f, 1.0f };
};

/// Light component
struct ScriptLightData {
	bool enabled = true;
	int type = 0;                // 0 = Directional, 1 = Point, 2 = Spot
	Vector3 color = { 1.0f, 1.0f, 1.0f };
	float intensity = 1.0f;
	float range = 10.0f;
	float spotAngle = 30.0f;     // degrees
};

/// Rigidbody component (velocity itself is ScriptGameObjectView::velocity)
struct ScriptRigidbodyData {
	bool enabled = true;
	float mass = 1.0f;
	float drag = 0.05f;
	float gravityScale = 1.0f;
	bool useGravity = true;
	bool isKinematic = false;
	float bounciness = 0.1f;
};

/// Skeletal animation playback of a model
struct ScriptAnimationData {
	int clipIndex = 0;
	float speed = 1.0f;
	bool loop = true;
	bool playing = true;
};

/// Camera component
struct ScriptCameraData {
	float fov = 45.0f;           // degrees
	float nearClip = 0.1f;
	float farClip = 1000.0f;
	bool orthographic = false;
};

/// Function-pointer table passed from the Engine to GameScripts.dll on load.
struct EngineAPI {
	void* hostContext = nullptr;

	// Object lookup (returns plain ScriptGameObjectView*; nullptr if missing/deleted)
	ScriptGameObjectView* (*findObjectById)(void* ctx, uint32_t id, bool warnIfMissing) = nullptr;
	ScriptGameObjectView* (*findObjectByName)(void* ctx, const char* name, bool warnIfMissing) = nullptr;

	// Transform accessors
	bool (*getTransform)(void* ctx, uint32_t id, ScriptTransformData* outTransform) = nullptr;
	bool (*setTransform)(void* ctx, uint32_t id, const ScriptTransformData* inTransform) = nullptr;

	// Console logging (0: Info, 1: Warning, 2: Error)
	void (*log)(void* ctx, const char* message, int logType) = nullptr;

	// Deferred object deletion
	void (*deleteObject)(void* ctx, uint32_t id) = nullptr;

	// Input queries: keyCode is a DirectInput scan code. Use the KEY_* constants below
	// (plain ASCII such as 'W' does NOT work: 'W' == 0x57 == the scan code of F11).
	bool (*isKeyDown)(void* ctx, int keyCode) = nullptr;
	bool (*isKeyTriggered)(void* ctx, int keyCode) = nullptr;

	// Frame delta time
	float (*getDeltaTime)(void* ctx) = nullptr;

	// Instantiate a new GameObject in the scene (primitiveType: 0=Cube, 1=Sphere, 2=Plane, 3=Quad, 4=Empty)
	ScriptGameObjectView* (*instantiate)(void* ctx, const char* name, int primitiveType, Vector3 position) = nullptr;

	// Mouse: button 0 = left, 1 = right, 2 = middle. Position is in Game view pixels (0,0 = top-left).
	bool (*isMouseDown)(void* ctx, int button) = nullptr;
	bool (*isMouseTriggered)(void* ctx, int button) = nullptr;
	bool (*getMousePosition)(void* ctx, Vector2* outPixels, Vector2* outViewSize) = nullptr;
	// Game camera ray through the mouse hits the plane y = planeY (top-down aiming)
	bool (*getMouseWorldOnPlane)(void* ctx, float planeY, Vector3* outPoint) = nullptr;

	// Gamepad (XInput, pad 0..3). button = ScriptPadButton. Sticks are -1..1 (Y up = +1), triggers 0..1.
	bool (*isPadConnected)(void* ctx, int pad) = nullptr;
	bool (*isPadButtonDown)(void* ctx, int button, int pad) = nullptr;
	bool (*isPadButtonTriggered)(void* ctx, int button, int pad) = nullptr;
	Vector2 (*getPadStick)(void* ctx, int stick, int pad) = nullptr;   // stick 0 = left, 1 = right
	float (*getPadTrigger)(void* ctx, int side, int pad) = nullptr;    // side 0 = left, 1 = right
	void (*setPadVibration)(void* ctx, int pad, float left, float right) = nullptr;

	// Time. getDeltaTime() is already scaled; timeScale 0 pauses scripts' dt, physics and animation.
	float (*getUnscaledDeltaTime)(void* ctx) = nullptr;
	float (*getTime)(void* ctx) = nullptr;              // scaled seconds since Play started
	float (*getTimeScale)(void* ctx) = nullptr;
	void (*setTimeScale)(void* ctx, float scale) = nullptr;

	// Prefabs (Assets/Prefabs/<name>.prefab.json). Deferred like instantiate; reuses despawned instances.
	ScriptGameObjectView* (*instantiatePrefab)(void* ctx, const char* prefabName, Vector3 position) = nullptr;
	// Prefab instance -> hidden and returned to its pool; any other object -> deleted
	void (*despawn)(void* ctx, uint32_t id) = nullptr;

	// Game UI on a 1280 x 720 canvas (origin top-left), drawn over the game view in every build.
	// Call every frame (immediate mode). align: 0 = left, 1 = center, 2 = right.
	void (*uiRect)(void* ctx, float x, float y, float w, float h, Vector4 color) = nullptr;
	void (*uiText)(void* ctx, const char* text, float x, float y, float size, Vector4 color, int align) = nullptr;
	void (*uiImage)(void* ctx, const char* texturePath, float x, float y, float w, float h, Vector4 color) = nullptr;
	float (*uiMeasureText)(void* ctx, const char* text, float size) = nullptr;

	// Scene switching: "Stage2" -> Assets/Scenes/Stage2.scene.json (or a project-relative .json path).
	// Happens at the end of the frame; all scripts of the old scene get OnDestroy().
	void (*loadScene)(void* ctx, const char* sceneNameOrPath) = nullptr;
	// Name of the scene that is running (e.g. "Stage1"); returns the length, writes a terminated string
	int (*getSceneName)(void* ctx, char* out, int outSize) = nullptr;

	// Persistent save data (SaveData/save.json in the project / game folder), written immediately
	void (*saveSetNumber)(void* ctx, const char* key, double value) = nullptr;
	double (*saveGetNumber)(void* ctx, const char* key, double fallback) = nullptr;
	void (*saveSetString)(void* ctx, const char* key, const char* value) = nullptr;
	// Copies the string into out (truncated, always terminated); returns false if the key is missing
	bool (*saveGetString)(void* ctx, const char* key, char* out, int outSize) = nullptr;
	bool (*saveHasKey)(void* ctx, const char* key) = nullptr;
	void (*saveDeleteAll)(void* ctx) = nullptr;

	// Audio (paths relative to the project, e.g. "Assets/Audio/shot.wav"). Returns a voice handle (0 = failed).
	uint32_t (*playSound)(void* ctx, const char* path, float volume, bool loop) = nullptr;
	void (*stopSound)(void* ctx, uint32_t voice) = nullptr;
	void (*setSoundVolume)(void* ctx, uint32_t voice, float volume) = nullptr;
	bool (*isSoundPlaying)(void* ctx, uint32_t voice) = nullptr;
	void (*preloadSound)(void* ctx, const char* path) = nullptr;
	// One looping music track at a time; playing the same track again only changes the volume
	void (*playMusic)(void* ctx, const char* path, float volume) = nullptr;
	void (*stopMusic)(void* ctx) = nullptr;

	// Particle System component. Both take effect at the end of the frame.
	void (*emitParticles)(void* ctx, uint32_t id, int count) = nullptr;
	void (*setParticlesEmitting)(void* ctx, uint32_t id, bool emitting) = nullptr;

	// Despawn (or destroy) id after `seconds` of game time; calling again replaces the timer
	void (*despawnAfter)(void* ctx, uint32_t id, float seconds) = nullptr;

	// Mesh Renderer
	bool (*getMaterial)(void* ctx, uint32_t id, ScriptMaterialData* out) = nullptr;
	bool (*setMaterial)(void* ctx, uint32_t id, const ScriptMaterialData* in) = nullptr;
	bool (*setTexture)(void* ctx, uint32_t id, const char* texturePath) = nullptr;

	// Scene queries against active Box / Sphere colliders. layerMask: bit N = include layer N.
	// Objects spawned this frame are found from the next frame on.
	bool (*raycast)(void* ctx, Vector3 origin, Vector3 direction, float maxDistance, uint32_t layerMask, uint32_t ignoreId, ScriptRaycastHit* outHit) = nullptr;
	int (*overlapSphere)(void* ctx, Vector3 center, float radius, uint32_t layerMask, uint32_t* outIds, int maxCount) = nullptr;
	// Tag searches over active objects (no collider needed). tag nullptr / "" in findNearest = any object.
	ScriptGameObjectView* (*findWithTag)(void* ctx, const char* tag) = nullptr;
	int (*findAllWithTag)(void* ctx, const char* tag, uint32_t* outIds, int maxCount) = nullptr;
	ScriptGameObjectView* (*findNearest)(void* ctx, Vector3 position, const char* tag, float maxDistance, uint32_t ignoreId) = nullptr;

	// Hierarchy. parentId 0 = make it a root. setParent takes effect at the end of the frame.
	bool (*setParent)(void* ctx, uint32_t childId, uint32_t parentId, bool keepWorldTransform) = nullptr;
	uint32_t (*getParent)(void* ctx, uint32_t id) = nullptr;
	int (*getChildren)(void* ctx, uint32_t id, uint32_t* outIds, int maxCount) = nullptr;
	bool (*getWorldPosition)(void* ctx, uint32_t id, Vector3* out) = nullptr;

	// Other components (false when the object has no such component)
	bool (*getLight)(void* ctx, uint32_t id, ScriptLightData* out) = nullptr;
	bool (*setLight)(void* ctx, uint32_t id, const ScriptLightData* in) = nullptr;
	bool (*getRigidbody)(void* ctx, uint32_t id, ScriptRigidbodyData* out) = nullptr;
	bool (*setRigidbody)(void* ctx, uint32_t id, const ScriptRigidbodyData* in) = nullptr;
	bool (*getAnimation)(void* ctx, uint32_t id, ScriptAnimationData* out) = nullptr;
	bool (*setAnimation)(void* ctx, uint32_t id, const ScriptAnimationData* in) = nullptr;
	bool (*getCamera)(void* ctx, uint32_t id, ScriptCameraData* out) = nullptr;
	bool (*setCamera)(void* ctx, uint32_t id, const ScriptCameraData* in) = nullptr;

	// Animator component (API v4): parameters, states, crossfade. Names are UTF-8, copied by the engine.
	bool (*animSetParam)(void* ctx, uint32_t id, const char* name, float value) = nullptr; // bool/trigger: 0 or 1
	float (*animGetParam)(void* ctx, uint32_t id, const char* name) = nullptr;
	bool (*animResetTrigger)(void* ctx, uint32_t id, const char* name) = nullptr;
	bool (*animIsState)(void* ctx, uint32_t id, const char* stateName) = nullptr;
	int (*animGetState)(void* ctx, uint32_t id, char* outName, int outSize) = nullptr; // length, -1 = no Animator
	bool (*animCrossFade)(void* ctx, uint32_t id, const char* stateName, float seconds) = nullptr;
	float (*animGetNormalizedTime)(void* ctx, uint32_t id) = nullptr; // 0..1 first pass, 2.5 = middle of the 3rd loop
	bool (*animIsInTransition)(void* ctx, uint32_t id) = nullptr;

	// Game feel (API v5). Real time: they keep working during a hitstop or with timeScale 0.
	void (*shakeCamera)(void* ctx, float strength, float seconds) = nullptr;
	void (*hitstop)(void* ctx, float seconds) = nullptr;

	// Input actions (API v5): names from Options > Input Actions (ProjectSettings/input.json)
	bool (*isActionDown)(void* ctx, const char* action) = nullptr;
	bool (*isActionTriggered)(void* ctx, const char* action) = nullptr;
	bool (*isActionReleased)(void* ctx, const char* action) = nullptr;
	float (*getAxis)(void* ctx, const char* action) = nullptr; // -1..1

	// Character controller (API v6): moves an object's collider through the scene's box / sphere colliders
	bool (*moveCharacter)(void* ctx, uint32_t id, Vector3 motion, float stepHeight, ScriptCharacterMove* out) = nullptr;

	// Navigation (API v7): A* over a grid made from Static colliders
	int (*findPath)(void* ctx, Vector3 from, Vector3 to, Vector3* outPoints, int maxPoints) = nullptr; // full point count
	void (*rebuildNavigation)(void* ctx, float cellSize, float agentRadius) = nullptr;

	// Scenes (API v8): the object (a root) and its children survive LoadScene; their scripts keep running
	bool (*keepAcrossScenes)(void* ctx, uint32_t id) = nullptr;

	// Camera input and projection (API v10). Canvas = the 1280x720 UI canvas stretched over the game view.
	bool (*getMouseDelta)(void* ctx, Vector2* outPixels) = nullptr;
	float (*getMouseWheel)(void* ctx) = nullptr; // notches, positive = away from the user
	void (*setCursorLocked)(void* ctx, bool locked) = nullptr;
	bool (*isCursorLocked)(void* ctx) = nullptr;
	bool (*screenPointToRay)(void* ctx, Vector2 canvasPos, Vector3* outOrigin, Vector3* outDirection) = nullptr;
	bool (*worldToCanvas)(void* ctx, Vector3 worldPos, Vector2* outCanvasPos) = nullptr;
	bool (*prewarmPrefab)(void* ctx, const char* prefabName, int count) = nullptr;

	// Post-processing and screen effects (API v10)
	bool (*getPostProcess)(void* ctx, ScriptPostProcess* out) = nullptr;
	bool (*setPostProcess)(void* ctx, const ScriptPostProcess* value) = nullptr;
	bool (*setScreenEffect)(void* ctx, int slot, const char* shaderFile, const float* params16) = nullptr; // shaderFile null = clear the slot
};

/// Engine <-> GameScripts.dll compatibility check. Bump kScriptApiVersion when the meaning of the API changes;
/// layout changes of EngineAPI / ScriptGameObjectView / ScriptBase are caught automatically by the sizes.
/// The fingerprint is defined at the end of this header (it needs the complete ScriptBase).
constexpr uint32_t kScriptApiVersion = 10; // 4: Animator, 5: ShakeCamera / Hitstop / input actions, 6: MoveCharacter, 7: FindPath, 8: KeepAcrossScenes, 9: material outline, 10: camera input + projection + prewarm + post-processing

/// Size of the UI canvas used by the UI* helpers (stretched over the game view)
constexpr float kUICanvasWidth = 1280.0f;
constexpr float kUICanvasHeight = 720.0f;

/// XInput button bits for isPadButtonDown / IsPadButtonDown (same values as XINPUT_GAMEPAD_*).
enum ScriptPadButton : int {
	PAD_DPAD_UP = 0x0001, PAD_DPAD_DOWN = 0x0002, PAD_DPAD_LEFT = 0x0004, PAD_DPAD_RIGHT = 0x0008,
	PAD_START = 0x0010, PAD_BACK = 0x0020, PAD_LEFT_THUMB = 0x0040, PAD_RIGHT_THUMB = 0x0080,
	PAD_LB = 0x0100, PAD_RB = 0x0200,
	PAD_A = 0x1000, PAD_B = 0x2000, PAD_X = 0x4000, PAD_Y = 0x8000,
};

/// Supported exposed field types for Inspector reflection and hot-reload serialization.
enum class ScriptFieldType : int32_t {
	Float = 0,
	Int = 1,
	Bool = 2,
	Vector3 = 3,
	Vec3 = 3,
	CharBuffer = 4
};

/// Plain metadata descriptor for a single EXPOSE_FIELD member.
struct ScriptFieldInfo {
	const char* name = "";
	ScriptFieldType type = ScriptFieldType::Float;
	void* dataPtr = nullptr;
	uint32_t sizeInBytes = 0;
};

/// Convenience alias for fixed-size text buffers in EXPOSE_FIELD.
using ScriptText = char[128];

class EditorSuite;
/// Fallback host interface. It intentionally exposes no object lookup: on the engine side GameObject is the
/// editor struct, which must never reach the DLL. Scripts look objects up through EngineAPI (views only).
class IScriptHost {
public:
	virtual ~IScriptHost() = default;
	virtual void QueueDeleteGameObjectById(uint32_t id) = 0;
	virtual void LogScriptMessage(const char* msg, int logType = 0) = 0;
};

/// Base class for all C++ GameObject scripts in GameScripts.dll.
/// Never holds std::string or std::vector members so layout is 100% ABI-independent across DLL boundaries.
class ScriptBase {
public:
	static constexpr int kMaxExposedFields = 64;

	ScriptBase() = default;
	virtual ~ScriptBase() = default;

	virtual void Initialize() {}
	virtual void Update(float deltaTime) { (void)deltaTime; }
	/// Every physics frame while touching `other` (including the first one)
	virtual void OnCollision(GameObject& other) { (void)other; }
	/// First frame of a contact / last contact ended (also when `other` was despawned or deactivated).
	/// Fired for triggers too; check other.layer / other.tag to tell them apart.
	virtual void OnCollisionEnter(GameObject& other) { (void)other; }
	virtual void OnCollisionExit(GameObject& other) { (void)other; }
	/// An animation event of the object's Animator fired (event name set in the Animator window, e.g. "hit")
	virtual void OnAnimationEvent(const char* eventName) { (void)eventName; }
	virtual void OnDestroy() {}
	virtual void OnAfterReload() {}

	/// Only a ScriptGameObjectView can become the owner. In the DLL GameObject is the same type;
	/// on the engine side there is deliberately no overload taking the editor GameObject.
	void SetOwner(ScriptGameObjectView* ownerView, uint32_t id = 0) {
		GameObject* ownerObj = reinterpret_cast<GameObject*>(ownerView);
		gameObject = ownerObj;
		owner = ownerObj;
		gameObject_ = ownerObj;
		if (id != 0 || !ownerView) {
			ownerId_ = id;
		}
	}

	GameObject* GetOwner() const { return gameObject_; }
	GameObject* GetGameObject() const { return gameObject_; }
	uint32_t GetOwnerId() const { return ownerId_; }

	void SetEngineAPI(const EngineAPI* api) { engineApi_ = api; }
	const EngineAPI* GetEngineAPI() const { return engineApi_; }

	void SetHost(IScriptHost* host, EditorSuite* suite = nullptr) {
		host_ = host;
		editorSuite_ = suite;
	}
	IScriptHost* GetHost() const { return host_; }
	EditorSuite* GetEditorSuite() const { return editorSuite_; }

	// Exposed field registration (called automatically by EXPOSE_FIELD)
	void RegisterExposedField(const char* fieldName, ScriptFieldType type, void* dataPtr, uint32_t sizeInBytes) {
		if (!fieldName || !dataPtr || exposedFieldCount_ >= kMaxExposedFields) {
			return;
		}
		exposedFields_[exposedFieldCount_].name = fieldName;
		exposedFields_[exposedFieldCount_].type = type;
		exposedFields_[exposedFieldCount_].dataPtr = dataPtr;
		exposedFields_[exposedFieldCount_].sizeInBytes = sizeInBytes;
		++exposedFieldCount_;
	}

	int GetExposedFieldCount() const { return exposedFieldCount_; }
	const ScriptFieldInfo* GetExposedField(int index) const {
		if (index < 0 || index >= exposedFieldCount_) return nullptr;
		return &exposedFields_[index];
	}

	// EngineAPI convenience helpers for scripts (lookups only go through EngineAPI, which returns views)
	GameObject* FindGameObjectById(uint32_t id) const {
		if (engineApi_ && engineApi_->findObjectById) {
			return reinterpret_cast<GameObject*>(engineApi_->findObjectById(engineApi_->hostContext, id, true));
		}
		return nullptr;
	}

	GameObject* FindGameObjectByName(const char* name) const {
		if (!name) return nullptr;
		if (engineApi_ && engineApi_->findObjectByName) {
			return reinterpret_cast<GameObject*>(engineApi_->findObjectByName(engineApi_->hostContext, name, true));
		}
		return nullptr;
	}

	bool GetTransform(uint32_t id, ScriptTransformData& outTransform) const {
		if (engineApi_ && engineApi_->getTransform) {
			return engineApi_->getTransform(engineApi_->hostContext, id, &outTransform);
		}
		return false;
	}

	bool SetTransform(uint32_t id, const ScriptTransformData& inTransform) const {
		if (engineApi_ && engineApi_->setTransform) {
			return engineApi_->setTransform(engineApi_->hostContext, id, &inTransform);
		}
		return false;
	}

	Vector3 GetPosition() const {
		if (gameObject_) {
			const auto* view = reinterpret_cast<const ScriptGameObjectView*>(gameObject_);
			return view->translation;
		}
		ScriptTransformData t{};
		GetTransform(ownerId_, t);
		return t.translation;
	}

	void SetPosition(const Vector3& pos) {
		if (gameObject_) {
			auto* view = reinterpret_cast<ScriptGameObjectView*>(gameObject_);
			view->translation = pos;
			view->dirtyFromScript = true;
		}
		ScriptTransformData t{};
		if (GetTransform(ownerId_, t)) {
			t.translation = pos;
			SetTransform(ownerId_, t);
		}
	}

	Vector3 GetRotation() const {
		if (gameObject_) {
			const auto* view = reinterpret_cast<const ScriptGameObjectView*>(gameObject_);
			return view->rotation;
		}
		ScriptTransformData t{};
		GetTransform(ownerId_, t);
		return t.rotation;
	}

	void SetRotation(const Vector3& euler) {
		if (gameObject_) {
			auto* view = reinterpret_cast<ScriptGameObjectView*>(gameObject_);
			view->rotation = euler;
			view->rotationQuat = Quaternion::FromEuler(euler);
			view->dirtyFromScript = true;
		}
		ScriptTransformData t{};
		if (GetTransform(ownerId_, t)) {
			t.rotation = euler;
			t.rotationQuat = Quaternion::FromEuler(euler);
			SetTransform(ownerId_, t);
		}
	}

	Vector3 GetScale() const {
		if (gameObject_) {
			const auto* view = reinterpret_cast<const ScriptGameObjectView*>(gameObject_);
			return view->scale;
		}
		ScriptTransformData t{};
		GetTransform(ownerId_, t);
		return t.scale;
	}

	void SetScale(const Vector3& s) {
		if (gameObject_) {
			auto* view = reinterpret_cast<ScriptGameObjectView*>(gameObject_);
			view->scale = s;
			view->dirtyFromScript = true;
		}
		ScriptTransformData t{};
		if (GetTransform(ownerId_, t)) {
			t.scale = s;
			SetTransform(ownerId_, t);
		}
	}

	void RequestDeleteGameObject(uint32_t id) {
		if (engineApi_ && engineApi_->deleteObject) {
			engineApi_->deleteObject(engineApi_->hostContext, id);
		} else if (host_) {
			host_->QueueDeleteGameObjectById(id);
		}
	}
	void DestroyGameObject(uint32_t id) { RequestDeleteGameObject(id); }

	void RequestDeleteSelf() {
		if (ownerId_ != 0) {
			RequestDeleteGameObject(ownerId_);
		}
	}
	void DestroySelf() { RequestDeleteSelf(); }

	void Log(const char* message, int logType = 0) const {
		if (!message) return;
		if (engineApi_ && engineApi_->log) {
			engineApi_->log(engineApi_->hostContext, message, logType);
		} else if (host_) {
			host_->LogScriptMessage(message, logType);
		}
	}

	bool IsKeyDown(int keyCode) const {
		if (engineApi_ && engineApi_->isKeyDown) {
			return engineApi_->isKeyDown(engineApi_->hostContext, keyCode);
		}
		return false;
	}

	bool IsKeyTriggered(int keyCode) const {
		if (engineApi_ && engineApi_->isKeyTriggered) {
			return engineApi_->isKeyTriggered(engineApi_->hostContext, keyCode);
		}
		return false;
	}

	float GetDeltaTime() const {
		if (engineApi_ && engineApi_->getDeltaTime) {
			return engineApi_->getDeltaTime(engineApi_->hostContext);
		}
		return 0.016f;
	}

	GameObject* Instantiate(const char* name, int primitiveType = 0, Vector3 position = { 0.0f, 0.0f, 0.0f }) const {
		if (engineApi_ && engineApi_->instantiate) {
			return reinterpret_cast<GameObject*>(engineApi_->instantiate(engineApi_->hostContext, name ? name : "InstantiatedObject", primitiveType, position));
		}
		return nullptr;
	}

	// ---- Prefabs & pooling ----------------------------------------------------
	/// Spawns Assets/Prefabs/<prefabName>.prefab.json at position (created at the end of this frame;
	/// the returned view can be edited right away). Reuses a despawned instance when available, in which
	/// case its scripts get their prefab field values back and Initialize() runs again.
	GameObject* InstantiatePrefab(const char* prefabName, Vector3 position = { 0.0f, 0.0f, 0.0f }) const {
		if (engineApi_ && engineApi_->instantiatePrefab && prefabName) {
			return reinterpret_cast<GameObject*>(engineApi_->instantiatePrefab(engineApi_->hostContext, prefabName, position));
		}
		return nullptr;
	}
	/// Returns a prefab instance to its pool (cheap, use for bullets/enemies/pickups); other objects are destroyed
	void Despawn(uint32_t id) const {
		if (engineApi_ && engineApi_->despawn) engineApi_->despawn(engineApi_->hostContext, id);
	}
	void DespawnSelf() const {
		if (ownerId_ != 0) Despawn(ownerId_);
	}

	// ---- Scenes & save data -------------------------------------------------
	/// Switches to Assets/Scenes/<name>.scene.json at the end of this frame (e.g. LoadScene("GameOver"))
	void LoadScene(const char* sceneNameOrPath) const {
		if (engineApi_ && engineApi_->loadScene && sceneNameOrPath) engineApi_->loadScene(engineApi_->hostContext, sceneNameOrPath);
	}
	/// Current scene name, e.g. "Stage1" (empty string if unavailable)
	const char* GetSceneName() const {
		static char buffer[128];
		buffer[0] = '\0';
		if (engineApi_ && engineApi_->getSceneName) engineApi_->getSceneName(engineApi_->hostContext, buffer, static_cast<int>(sizeof(buffer)));
		return buffer;
	}
	void SaveInt(const char* key, int value) const { SaveFloatImpl(key, static_cast<double>(value)); }
	void SaveFloat(const char* key, float value) const { SaveFloatImpl(key, static_cast<double>(value)); }
	int LoadInt(const char* key, int fallback = 0) const { return static_cast<int>(LoadFloatImpl(key, static_cast<double>(fallback))); }
	float LoadFloat(const char* key, float fallback = 0.0f) const { return static_cast<float>(LoadFloatImpl(key, static_cast<double>(fallback))); }
	void SaveText(const char* key, const char* value) const {
		if (engineApi_ && engineApi_->saveSetString && key && value) engineApi_->saveSetString(engineApi_->hostContext, key, value);
	}
	/// Copies the saved string into out; returns false (and leaves out empty) when the key is missing
	bool LoadText(const char* key, char* out, int outSize) const {
		if (out && outSize > 0) out[0] = '\0';
		return engineApi_ && engineApi_->saveGetString && key && out && engineApi_->saveGetString(engineApi_->hostContext, key, out, outSize);
	}
	bool HasSaveKey(const char* key) const {
		return engineApi_ && engineApi_->saveHasKey && key && engineApi_->saveHasKey(engineApi_->hostContext, key);
	}
	void DeleteAllSaveData() const {
		if (engineApi_ && engineApi_->saveDeleteAll) engineApi_->saveDeleteAll(engineApi_->hostContext);
	}

	// ---- Game UI (1280 x 720 canvas, call every frame from Update) ----------
	void UIRect(float x, float y, float w, float h, Vector4 color) const {
		if (engineApi_ && engineApi_->uiRect) engineApi_->uiRect(engineApi_->hostContext, x, y, w, h, color);
	}
	/// size = line height in canvas pixels; align 0 = left, 1 = center, 2 = right (x is the anchor)
	void UIText(const char* text, float x, float y, float size = 32.0f, Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f }, int align = 0) const {
		if (engineApi_ && engineApi_->uiText && text) engineApi_->uiText(engineApi_->hostContext, text, x, y, size, color, align);
	}
	/// texturePath relative to the project folder, e.g. "Assets/Textures/heart.png" (loaded once, then cached)
	void UIImage(const char* texturePath, float x, float y, float w, float h, Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f }) const {
		if (engineApi_ && engineApi_->uiImage && texturePath) engineApi_->uiImage(engineApi_->hostContext, texturePath, x, y, w, h, color);
	}
	float UIMeasureText(const char* text, float size = 32.0f) const {
		return (engineApi_ && engineApi_->uiMeasureText && text) ? engineApi_->uiMeasureText(engineApi_->hostContext, text, size) : 0.0f;
	}
	/// HP / XP bar: value01 is clamped to 0..1
	void UIProgressBar(float x, float y, float w, float h, float value01,
		Vector4 fill = { 0.2f, 0.85f, 0.3f, 1.0f }, Vector4 back = { 0.0f, 0.0f, 0.0f, 0.6f }) const {
		const float v = value01 < 0.0f ? 0.0f : (value01 > 1.0f ? 1.0f : value01);
		UIRect(x, y, w, h, back);
		UIRect(x, y, w * v, h, fill);
	}
	/// Mouse position on the UI canvas; false when the mouse is outside the game view
	bool GetMouseCanvasPosition(Vector2& outCanvas) const {
		Vector2 pixels{};
		Vector2 size{};
		if (!GetMousePosition(pixels, &size) || size.x <= 0.0f || size.y <= 0.0f) return false;
		outCanvas = { pixels.x / size.x * kUICanvasWidth, pixels.y / size.y * kUICanvasHeight };
		return true;
	}
	/// Button: returns true when clicked with the mouse, or when `selected` and Enter / Space / pad A is pressed
	/// (keyboard/gamepad menus keep their own selected index and pass selected = (index == i)).
	bool UIButton(const char* label, float x, float y, float w, float h, bool selected = false, float textSize = 28.0f) const {
		Vector2 m{};
		const bool hover = GetMouseCanvasPosition(m) && m.x >= x && m.x < x + w && m.y >= y && m.y < y + h;
		const bool highlighted = hover || selected;
		UIRect(x, y, w, h, highlighted ? Vector4{ 0.95f, 0.75f, 0.2f, 0.95f } : Vector4{ 0.1f, 0.1f, 0.12f, 0.85f });
		UIText(label, x + w * 0.5f, y + (h - textSize) * 0.5f, textSize,
			highlighted ? Vector4{ 0.05f, 0.05f, 0.05f, 1.0f } : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }, 1);
		if (hover && IsMouseTriggered(0)) return true;
		return selected && (IsKeyTriggered(KEY_ENTER) || IsKeyTriggered(KEY_SPACE) || IsPadButtonTriggered(PAD_A));
	}

	// ---- Mouse --------------------------------------------------------------
	bool IsMouseDown(int button = 0) const {
		return engineApi_ && engineApi_->isMouseDown && engineApi_->isMouseDown(engineApi_->hostContext, button);
	}
	bool IsMouseTriggered(int button = 0) const {
		return engineApi_ && engineApi_->isMouseTriggered && engineApi_->isMouseTriggered(engineApi_->hostContext, button);
	}
	/// Mouse position in Game view pixels; false when the mouse is outside the Game view
	bool GetMousePosition(Vector2& outPixels, Vector2* outViewSize = nullptr) const {
		Vector2 size{};
		const bool ok = engineApi_ && engineApi_->getMousePosition && engineApi_->getMousePosition(engineApi_->hostContext, &outPixels, &size);
		if (outViewSize) *outViewSize = size;
		return ok;
	}
	/// Point on the ground plane y = planeY under the mouse (aim direction for top-down shooters)
	bool GetMouseWorldPosition(Vector3& outPoint, float planeY = 0.0f) const {
		return engineApi_ && engineApi_->getMouseWorldOnPlane && engineApi_->getMouseWorldOnPlane(engineApi_->hostContext, planeY, &outPoint);
	}
	/// Mouse movement this frame in raw pixels (right / down positive). Zero unless the cursor is locked or inside the game view.
	Vector2 GetMouseDelta() const {
		Vector2 delta{};
		if (engineApi_ && engineApi_->getMouseDelta) engineApi_->getMouseDelta(engineApi_->hostContext, &delta);
		return delta;
	}
	/// Wheel notches this frame (positive = away from the user), with the same condition as GetMouseDelta
	float GetMouseWheel() const {
		return (engineApi_ && engineApi_->getMouseWheel) ? engineApi_->getMouseWheel(engineApi_->hostContext) : 0.0f;
	}
	/// Hides the cursor and keeps it inside the game view (first / third person cameras). Ends with Play;
	/// Esc also releases it in the editor. Read the movement with GetMouseDelta.
	void SetCursorLocked(bool locked) const {
		if (engineApi_ && engineApi_->setCursorLocked) engineApi_->setCursorLocked(engineApi_->hostContext, locked);
	}
	bool IsCursorLocked() const {
		return engineApi_ && engineApi_->isCursorLocked && engineApi_->isCursorLocked(engineApi_->hostContext);
	}
	/// World ray of the game camera through a UI canvas position. Use with Raycast for 3D picking.
	bool ScreenPointToRay(const Vector2& canvasPos, Vector3& outOrigin, Vector3& outDirection) const {
		return engineApi_ && engineApi_->screenPointToRay && engineApi_->screenPointToRay(engineApi_->hostContext, canvasPos, &outOrigin, &outDirection);
	}
	/// UI canvas position of a world point (name tags, health bars). False when it is behind the camera; the result may be off screen.
	bool WorldToCanvas(const Vector3& worldPos, Vector2& outCanvasPos) const {
		return engineApi_ && engineApi_->worldToCanvas && engineApi_->worldToCanvas(engineApi_->hostContext, worldPos, &outCanvasPos);
	}
	/// Creates `count` hidden instances in the prefab's pool at the end of this frame, so later spawns do not hitch.
	/// Their Initialize runs when they are spawned. Play only; false if the prefab does not exist.
	bool PrewarmPrefab(const char* prefabName, int count) const {
		return engineApi_ && engineApi_->prewarmPrefab && prefabName && engineApi_->prewarmPrefab(engineApi_->hostContext, prefabName, count);
	}

	// ---- Post-processing ----------------------------------------------------
	bool GetPostProcess(ScriptPostProcess& out) const {
		return engineApi_ && engineApi_->getPostProcess && engineApi_->getPostProcess(engineApi_->hostContext, &out);
	}
	bool SetPostProcess(const ScriptPostProcess& value) const {
		return engineApi_ && engineApi_->setPostProcess && engineApi_->setPostProcess(engineApi_->hostContext, &value);
	}
	/// Runs a pixel shader over the HDR scene (slots 0..3, in order) until ClearScreenEffect or Stop. Files: Assets/... of the
	/// project or the built-in shaders/ScreenEffect.*.PS.hlsl. params16 fills gParams[4] (see shaders/Post.hlsli); nullptr keeps
	/// the previous values, so calling it every frame to animate is cheap. False if the shader does not compile (see Console).
	bool SetScreenEffect(int slot, const char* shaderFile, const float* params16 = nullptr) const {
		return engineApi_ && engineApi_->setScreenEffect && shaderFile && engineApi_->setScreenEffect(engineApi_->hostContext, slot, shaderFile, params16);
	}
	void ClearScreenEffect(int slot) const {
		if (engineApi_ && engineApi_->setScreenEffect) engineApi_->setScreenEffect(engineApi_->hostContext, slot, nullptr, nullptr);
	}

	// ---- Gamepad ------------------------------------------------------------
	bool IsPadConnected(int pad = 0) const {
		return engineApi_ && engineApi_->isPadConnected && engineApi_->isPadConnected(engineApi_->hostContext, pad);
	}
	bool IsPadButtonDown(int button, int pad = 0) const {
		return engineApi_ && engineApi_->isPadButtonDown && engineApi_->isPadButtonDown(engineApi_->hostContext, button, pad);
	}
	bool IsPadButtonTriggered(int button, int pad = 0) const {
		return engineApi_ && engineApi_->isPadButtonTriggered && engineApi_->isPadButtonTriggered(engineApi_->hostContext, button, pad);
	}
	Vector2 GetLeftStick(int pad = 0) const {
		return (engineApi_ && engineApi_->getPadStick) ? engineApi_->getPadStick(engineApi_->hostContext, 0, pad) : Vector2{};
	}
	Vector2 GetRightStick(int pad = 0) const {
		return (engineApi_ && engineApi_->getPadStick) ? engineApi_->getPadStick(engineApi_->hostContext, 1, pad) : Vector2{};
	}
	float GetPadTrigger(int side, int pad = 0) const {
		return (engineApi_ && engineApi_->getPadTrigger) ? engineApi_->getPadTrigger(engineApi_->hostContext, side, pad) : 0.0f;
	}
	void SetPadVibration(float left, float right, int pad = 0) const {
		if (engineApi_ && engineApi_->setPadVibration) engineApi_->setPadVibration(engineApi_->hostContext, pad, left, right);
	}

	/// Movement input from WASD / arrow keys / left stick: x = right, y = forward (+Z in a top-down view).
	/// Length is at most 1, so diagonal movement is not faster.
	Vector2 GetMoveAxis(int pad = 0) const {
		float x = 0.0f;
		float y = 0.0f;
		if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) x += 1.0f;
		if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) x -= 1.0f;
		if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) y += 1.0f;
		if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) y -= 1.0f;
		const Vector2 stick = GetLeftStick(pad);
		x += stick.x;
		y += stick.y;
		const float len = std::sqrt(x * x + y * y);
		if (len > 1.0f) { x /= len; y /= len; }
		return { x, y };
	}

	// ---- Time ---------------------------------------------------------------
	float GetUnscaledDeltaTime() const {
		return (engineApi_ && engineApi_->getUnscaledDeltaTime) ? engineApi_->getUnscaledDeltaTime(engineApi_->hostContext) : GetDeltaTime();
	}
	/// Scaled seconds since Play started
	float GetTime() const {
		return (engineApi_ && engineApi_->getTime) ? engineApi_->getTime(engineApi_->hostContext) : 0.0f;
	}
	float GetTimeScale() const {
		return (engineApi_ && engineApi_->getTimeScale) ? engineApi_->getTimeScale(engineApi_->hostContext) : 1.0f;
	}
	/// 0 = pause the game (level-up menu), 1 = normal, 0.3 = slow motion. Takes effect next frame.
	void SetTimeScale(float scale) const {
		if (engineApi_ && engineApi_->setTimeScale) engineApi_->setTimeScale(engineApi_->hostContext, scale);
	}

	// In the helpers below, id 0 means "this script's own object".

	// ---- Audio --------------------------------------------------------------
	/// Plays a sound effect once (or looped). Returns a voice handle for StopAudio / SetAudioVolume.
	uint32_t PlayAudio(const char* path, float volume = 1.0f, bool loop = false) const {
		return (engineApi_ && engineApi_->playSound && path) ? engineApi_->playSound(engineApi_->hostContext, path, volume, loop) : 0u;
	}
	void StopAudio(uint32_t voice) const {
		if (engineApi_ && engineApi_->stopSound && voice != 0) engineApi_->stopSound(engineApi_->hostContext, voice);
	}
	void SetAudioVolume(uint32_t voice, float volume) const {
		if (engineApi_ && engineApi_->setSoundVolume && voice != 0) engineApi_->setSoundVolume(engineApi_->hostContext, voice, volume);
	}
	bool IsAudioPlaying(uint32_t voice) const {
		return engineApi_ && engineApi_->isSoundPlaying && voice != 0 && engineApi_->isSoundPlaying(engineApi_->hostContext, voice);
	}
	/// Decodes the file now (call in Initialize) so the first PlayAudio does not stall
	void PreloadAudio(const char* path) const {
		if (engineApi_ && engineApi_->preloadSound && path) engineApi_->preloadSound(engineApi_->hostContext, path);
	}
	/// Background music: loops, replaces the current track (same track = only the volume changes)
	void PlayMusic(const char* path, float volume = 1.0f) const {
		if (engineApi_ && engineApi_->playMusic && path) engineApi_->playMusic(engineApi_->hostContext, path, volume);
	}
	void StopMusic() const {
		if (engineApi_ && engineApi_->stopMusic) engineApi_->stopMusic(engineApi_->hostContext);
	}

	// ---- Particles & timers -------------------------------------------------
	/// One-shot burst from a Particle System component (end of frame)
	void EmitParticles(int count, uint32_t id = 0) const {
		if (engineApi_ && engineApi_->emitParticles) engineApi_->emitParticles(engineApi_->hostContext, ResolveId(id), count);
	}
	/// Turns continuous emission on/off ("Emit Continuously" in the Inspector)
	void SetParticlesEmitting(bool emitting, uint32_t id = 0) const {
		if (engineApi_ && engineApi_->setParticlesEmitting) engineApi_->setParticlesEmitting(engineApi_->hostContext, ResolveId(id), emitting);
	}
	/// Despawn after a delay (bullet lifetime, effect cleanup). Despawn() / Destroy cancel the timer.
	void DespawnAfter(float seconds, uint32_t id = 0) const {
		if (engineApi_ && engineApi_->despawnAfter) engineApi_->despawnAfter(engineApi_->hostContext, ResolveId(id), seconds);
	}

	// ---- Material -----------------------------------------------------------
	bool GetMaterial(ScriptMaterialData& out, uint32_t id = 0) const {
		return engineApi_ && engineApi_->getMaterial && engineApi_->getMaterial(engineApi_->hostContext, ResolveId(id), &out);
	}
	bool SetMaterial(const ScriptMaterialData& in, uint32_t id = 0) const {
		return engineApi_ && engineApi_->setMaterial && engineApi_->setMaterial(engineApi_->hostContext, ResolveId(id), &in);
	}
	/// Mesh color (RGBA). Example hit flash: SetColor({1,0.2f,0.2f,1}) then back after 0.1 s.
	bool SetColor(const Vector4& color, uint32_t id = 0) const {
		ScriptMaterialData m{};
		if (!GetMaterial(m, id)) return false;
		m.color = color;
		return SetMaterial(m, id);
	}
	Vector4 GetColor(uint32_t id = 0) const {
		ScriptMaterialData m{};
		return GetMaterial(m, id) ? m.color : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
	}
	bool SetVisible(bool visible, uint32_t id = 0) const {
		ScriptMaterialData m{};
		if (!GetMaterial(m, id)) return false;
		m.visible = visible;
		return SetMaterial(m, id);
	}
	/// texturePath relative to the project, e.g. "Assets/Textures/enemy_hurt.png"
	bool SetTexture(const char* texturePath, uint32_t id = 0) const {
		return engineApi_ && engineApi_->setTexture && texturePath && engineApi_->setTexture(engineApi_->hostContext, ResolveId(id), texturePath);
	}

	// ---- Scene queries ------------------------------------------------------
	/// Closest Box / Sphere collider along the ray (direction need not be normalized).
	/// Colliders that contain the origin are ignored, and so is this object when ignoreSelf is true.
	bool Raycast(const Vector3& origin, const Vector3& direction, float maxDistance, ScriptRaycastHit& outHit,
		uint32_t layerMask = kCollideWithAll, bool ignoreSelf = true) const {
		outHit = ScriptRaycastHit{};
		return engineApi_ && engineApi_->raycast &&
			engineApi_->raycast(engineApi_->hostContext, origin, direction, maxDistance, layerMask, ignoreSelf ? ownerId_ : 0u, &outHit);
	}
	/// Ids of objects whose collider touches the sphere (this object included). Returns the number written.
	int OverlapSphere(const Vector3& center, float radius, uint32_t* outIds, int maxCount, uint32_t layerMask = kCollideWithAll) const {
		return (engineApi_ && engineApi_->overlapSphere && outIds && maxCount > 0)
			? engineApi_->overlapSphere(engineApi_->hostContext, center, radius, layerMask, outIds, maxCount) : 0;
	}
	/// First active object with this tag (nullptr if none)
	GameObject* FindWithTag(const char* tag) const {
		return (engineApi_ && engineApi_->findWithTag && tag)
			? reinterpret_cast<GameObject*>(engineApi_->findWithTag(engineApi_->hostContext, tag)) : nullptr;
	}
	int FindAllWithTag(const char* tag, uint32_t* outIds, int maxCount) const {
		return (engineApi_ && engineApi_->findAllWithTag && tag && outIds && maxCount > 0)
			? engineApi_->findAllWithTag(engineApi_->hostContext, tag, outIds, maxCount) : 0;
	}
	/// Nearest active object with the tag (nullptr / "" = any) within maxDistance of position
	GameObject* FindNearest(const Vector3& position, const char* tag, float maxDistance = 1.0e30f, uint32_t ignoreId = 0) const {
		return (engineApi_ && engineApi_->findNearest)
			? reinterpret_cast<GameObject*>(engineApi_->findNearest(engineApi_->hostContext, position, tag, maxDistance, ignoreId)) : nullptr;
	}
	/// Nearest object with the tag, measured from this object (never returns this object)
	GameObject* FindNearestWithTag(const char* tag, float maxDistance = 1.0e30f) const {
		return FindNearest(GetWorldPosition(), tag, maxDistance, ownerId_);
	}

	// ---- Hierarchy ----------------------------------------------------------
	/// Parents this object under parentId (0 = root). keepWorldTransform keeps it where it is on screen.
	bool SetParent(uint32_t parentId, bool keepWorldTransform = true) const {
		return SetParentOf(ownerId_, parentId, keepWorldTransform);
	}
	bool SetParentOf(uint32_t childId, uint32_t parentId, bool keepWorldTransform = true) const {
		return engineApi_ && engineApi_->setParent && engineApi_->setParent(engineApi_->hostContext, childId, parentId, keepWorldTransform);
	}
	/// 0 = no parent
	uint32_t GetParentId(uint32_t id = 0) const {
		return (engineApi_ && engineApi_->getParent) ? engineApi_->getParent(engineApi_->hostContext, ResolveId(id)) : 0u;
	}
	int GetChildren(uint32_t* outIds, int maxCount, uint32_t id = 0) const {
		return (engineApi_ && engineApi_->getChildren && outIds && maxCount > 0)
			? engineApi_->getChildren(engineApi_->hostContext, ResolveId(id), outIds, maxCount) : 0;
	}
	/// World position (translation is local to the parent for child objects)
	Vector3 GetWorldPosition(uint32_t id = 0) const {
		Vector3 p{};
		if (engineApi_ && engineApi_->getWorldPosition && engineApi_->getWorldPosition(engineApi_->hostContext, ResolveId(id), &p)) return p;
		return (id == 0 || id == ownerId_) ? GetPosition() : p;
	}

	// ---- Components ---------------------------------------------------------
	bool GetLight(ScriptLightData& out, uint32_t id = 0) const {
		return engineApi_ && engineApi_->getLight && engineApi_->getLight(engineApi_->hostContext, ResolveId(id), &out);
	}
	bool SetLight(const ScriptLightData& in, uint32_t id = 0) const {
		return engineApi_ && engineApi_->setLight && engineApi_->setLight(engineApi_->hostContext, ResolveId(id), &in);
	}
	bool GetRigidbody(ScriptRigidbodyData& out, uint32_t id = 0) const {
		return engineApi_ && engineApi_->getRigidbody && engineApi_->getRigidbody(engineApi_->hostContext, ResolveId(id), &out);
	}
	bool SetRigidbody(const ScriptRigidbodyData& in, uint32_t id = 0) const {
		return engineApi_ && engineApi_->setRigidbody && engineApi_->setRigidbody(engineApi_->hostContext, ResolveId(id), &in);
	}
	/// velocity += impulse / mass on this object (knockback, jump). Needs a Rigidbody to move by physics.
	void AddImpulse(const Vector3& impulse) {
		ScriptRigidbodyData rb{};
		const float mass = (GetRigidbody(rb) && rb.mass > 0.0001f) ? rb.mass : 1.0f;
		if (gameObject_) {
			auto* view = reinterpret_cast<ScriptGameObjectView*>(gameObject_);
			view->velocity = { view->velocity.x + impulse.x / mass, view->velocity.y + impulse.y / mass, view->velocity.z + impulse.z / mass };
			view->dirtyFromScript = true;
		}
	}
	bool GetAnimation(ScriptAnimationData& out, uint32_t id = 0) const {
		return engineApi_ && engineApi_->getAnimation && engineApi_->getAnimation(engineApi_->hostContext, ResolveId(id), &out);
	}
	bool SetAnimation(const ScriptAnimationData& in, uint32_t id = 0) const {
		return engineApi_ && engineApi_->setAnimation && engineApi_->setAnimation(engineApi_->hostContext, ResolveId(id), &in);
	}
	/// Switches the model's animation clip (by index in the model file)
	bool PlayAnimation(int clipIndex, bool loop = true, float speed = 1.0f, uint32_t id = 0) const {
		ScriptAnimationData a{};
		a.clipIndex = clipIndex;
		a.loop = loop;
		a.speed = speed;
		a.playing = true;
		return SetAnimation(a, id);
	}
	bool GetCamera(ScriptCameraData& out, uint32_t id = 0) const {
		return engineApi_ && engineApi_->getCamera && engineApi_->getCamera(engineApi_->hostContext, ResolveId(id), &out);
	}
	bool SetCamera(const ScriptCameraData& in, uint32_t id = 0) const {
		return engineApi_ && engineApi_->setCamera && engineApi_->setCamera(engineApi_->hostContext, ResolveId(id), &in);
	}

	// ---- Animator (Animator component + controller made in Window > Animator) ----
	bool SetAnimFloat(const char* name, float value, uint32_t id = 0) const { return AnimSet(name, value, id); }
	bool SetAnimInt(const char* name, int value, uint32_t id = 0) const { return AnimSet(name, static_cast<float>(value), id); }
	bool SetAnimBool(const char* name, bool value, uint32_t id = 0) const { return AnimSet(name, value ? 1.0f : 0.0f, id); }
	/// Fires a trigger: it stays set until a transition uses it (or ResetAnimTrigger)
	bool SetAnimTrigger(const char* name, uint32_t id = 0) const { return AnimSet(name, 1.0f, id); }
	bool ResetAnimTrigger(const char* name, uint32_t id = 0) const {
		return engineApi_ && engineApi_->animResetTrigger && engineApi_->animResetTrigger(engineApi_->hostContext, ResolveId(id), name);
	}
	float GetAnimFloat(const char* name, uint32_t id = 0) const {
		return (engineApi_ && engineApi_->animGetParam) ? engineApi_->animGetParam(engineApi_->hostContext, ResolveId(id), name) : 0.0f;
	}
	bool GetAnimBool(const char* name, uint32_t id = 0) const { return GetAnimFloat(name, id) != 0.0f; }
	int GetAnimInt(const char* name, uint32_t id = 0) const { return static_cast<int>(std::lround(GetAnimFloat(name, id))); }
	/// True while the Animator is in (or fading into) the state
	bool IsAnimState(const char* stateName, uint32_t id = 0) const {
		return engineApi_ && engineApi_->animIsState && engineApi_->animIsState(engineApi_->hostContext, ResolveId(id), stateName);
	}
	/// Current state name; "" without an Animator
	std::string GetAnimState(uint32_t id = 0) const {
		char buf[128] = {};
		if (!engineApi_ || !engineApi_->animGetState || engineApi_->animGetState(engineApi_->hostContext, ResolveId(id), buf, static_cast<int>(sizeof(buf))) < 0) return {};
		return buf;
	}
	/// Jumps to a state, ignoring transitions (e.g. a scripted cutscene); seconds = crossfade time
	bool CrossFadeAnimation(const char* stateName, float seconds = 0.2f, uint32_t id = 0) const {
		return engineApi_ && engineApi_->animCrossFade && engineApi_->animCrossFade(engineApi_->hostContext, ResolveId(id), stateName, seconds);
	}
	float GetAnimNormalizedTime(uint32_t id = 0) const {
		return (engineApi_ && engineApi_->animGetNormalizedTime) ? engineApi_->animGetNormalizedTime(engineApi_->hostContext, ResolveId(id)) : 0.0f;
	}
	bool IsAnimInTransition(uint32_t id = 0) const {
		return engineApi_ && engineApi_->animIsInTransition && engineApi_->animIsInTransition(engineApi_->hostContext, ResolveId(id));
	}

	// ---- Game feel ----
	/// Shakes the game camera by up to `strength` world units, fading out over `seconds` (a weaker shake never cuts a stronger one)
	void ShakeCamera(float strength, float seconds) const {
		if (engineApi_ && engineApi_->shakeCamera) engineApi_->shakeCamera(engineApi_->hostContext, strength, seconds);
	}
	/// Freezes game time for `seconds` (impact pause). Update still runs with deltaTime 0; SetTimeScale is not changed.
	void Hitstop(float seconds) const {
		if (engineApi_ && engineApi_->hitstop) engineApi_->hitstop(engineApi_->hostContext, seconds);
	}

	// ---- Input actions (keyboard, mouse and gamepad at once; edit in Options > Input Actions) ----
	// Default actions: MoveX, MoveY, Jump, Attack, Submit, Cancel, Pause
	bool IsActionDown(const char* action) const {
		return engineApi_ && engineApi_->isActionDown && engineApi_->isActionDown(engineApi_->hostContext, action);
	}
	bool IsActionTriggered(const char* action) const {
		return engineApi_ && engineApi_->isActionTriggered && engineApi_->isActionTriggered(engineApi_->hostContext, action);
	}
	bool IsActionReleased(const char* action) const {
		return engineApi_ && engineApi_->isActionReleased && engineApi_->isActionReleased(engineApi_->hostContext, action);
	}
	/// -1..1 (keys give -1 / +1, sticks their analog value)
	float GetAxis(const char* action) const {
		return (engineApi_ && engineApi_->getAxis) ? engineApi_->getAxis(engineApi_->hostContext, action) : 0.0f;
	}
	/// Two axes as a vector, length clamped to 1 (diagonals are not faster)
	Vector2 GetAxis2D(const char* actionX = "MoveX", const char* actionY = "MoveY") const {
		Vector2 v{ GetAxis(actionX), GetAxis(actionY) };
		const float len = std::sqrt(v.x * v.x + v.y * v.y);
		if (len > 1.0f) { v.x /= len; v.y /= len; }
		return v;
	}

	// ---- Character controller ----
	/// Moves this object by `motion` (world units, e.g. velocity * deltaTime) without passing through colliders:
	/// slides along walls, climbs ledges up to `stepHeight` while grounded, lands on floors. Triggers and layers
	/// outside "Collides With" are ignored. Uses the Box Collider (or Sphere Collider) of this object.
	/// Do not add a non-kinematic Rigidbody as well (gravity is yours: add it to motion.y while !grounded).
	ScriptCharacterMove MoveCharacter(const Vector3& motion, float stepHeight = 0.3f) const {
		ScriptCharacterMove result{};
		if (engineApi_ && engineApi_->moveCharacter) engineApi_->moveCharacter(engineApi_->hostContext, ownerId_, motion, stepHeight, &result);
		return result;
	}

	// ---- Scenes ----
	/// LoadScene keeps this object (must be a root; its children come along) and its scripts running, like
	/// Unity's DontDestroyOnLoad. Good for a game manager, music player or the player. Ends when Play stops.
	bool KeepAcrossScenes(uint32_t id = 0) const {
		return engineApi_ && engineApi_->keepAcrossScenes && engineApi_->keepAcrossScenes(engineApi_->hostContext, ResolveId(id));
	}

	// ---- Navigation ----
	/// A* path around Static colliders (walls, crates: anything 0.3 above the floor). Writes up to `maxPoints`
	/// corners (first = from, last = to) and returns how many the full path has; 0 = no way there.
	/// The grid is built on first use in Play; call RebuildNavigation after moving or adding static objects.
	int FindPath(const Vector3& from, const Vector3& to, Vector3* outPoints, int maxPoints) const {
		return (engineApi_ && engineApi_->findPath) ? engineApi_->findPath(engineApi_->hostContext, from, to, outPoints, maxPoints) : 0;
	}
	/// cellSize: grid resolution in world units; agentRadius: how far paths keep away from walls
	void RebuildNavigation(float cellSize = 0.5f, float agentRadius = 0.4f) const {
		if (engineApi_ && engineApi_->rebuildNavigation) engineApi_->rebuildNavigation(engineApi_->hostContext, cellSize, agentRadius);
	}

	// Pointer to owner GameObject view (synchronized by the engine)
	GameObject* gameObject = nullptr;
	GameObject* owner = nullptr;

protected:
	uint32_t ResolveId(uint32_t id) const { return id != 0 ? id : ownerId_; }
	bool AnimSet(const char* name, float value, uint32_t id) const {
		return engineApi_ && engineApi_->animSetParam && engineApi_->animSetParam(engineApi_->hostContext, ResolveId(id), name, value);
	}

	void SaveFloatImpl(const char* key, double value) const {
		if (engineApi_ && engineApi_->saveSetNumber && key) engineApi_->saveSetNumber(engineApi_->hostContext, key, value);
	}
	double LoadFloatImpl(const char* key, double fallback) const {
		return (engineApi_ && engineApi_->saveGetNumber && key) ? engineApi_->saveGetNumber(engineApi_->hostContext, key, fallback) : fallback;
	}

	GameObject* gameObject_ = nullptr;
	uint32_t ownerId_ = 0;
	const EngineAPI* engineApi_ = nullptr;
	IScriptHost* host_ = nullptr;
	EditorSuite* editorSuite_ = nullptr;

private:
	ScriptFieldInfo exposedFields_[kMaxExposedFields]{};
	int exposedFieldCount_ = 0;
};

/// Helper struct used by EXPOSE_FIELD to register a member field at ScriptBase construction time.
struct ScriptFieldRegistrar {
	ScriptFieldRegistrar(ScriptBase* self, const char* name, float* ptr) {
		if (self) self->RegisterExposedField(name, ScriptFieldType::Float, ptr, static_cast<uint32_t>(sizeof(float)));
	}
	ScriptFieldRegistrar(ScriptBase* self, const char* name, int* ptr) {
		if (self) self->RegisterExposedField(name, ScriptFieldType::Int, ptr, static_cast<uint32_t>(sizeof(int)));
	}
	ScriptFieldRegistrar(ScriptBase* self, const char* name, bool* ptr) {
		if (self) self->RegisterExposedField(name, ScriptFieldType::Bool, ptr, static_cast<uint32_t>(sizeof(bool)));
	}
	ScriptFieldRegistrar(ScriptBase* self, const char* name, Vector3* ptr) {
		if (self) self->RegisterExposedField(name, ScriptFieldType::Vector3, ptr, static_cast<uint32_t>(sizeof(Vector3)));
	}
	template <size_t N>
	ScriptFieldRegistrar(ScriptBase* self, const char* name, char (*ptr)[N]) {
		if (self) self->RegisterExposedField(name, ScriptFieldType::CharBuffer, *ptr, static_cast<uint32_t>(N));
	}
};

/// Macro to expose a script field (float, int, bool, Vector3, or char[N]) to the Inspector and preserve it across hot reloads and scene saves.
/// Note: Fields not declared with EXPOSE_FIELD are not preserved across a script hot reload.
#define EXPOSE_FIELD(Type, name, ...) \
	::std::type_identity_t<Type> name = __VA_ARGS__; \
	::ScriptFieldRegistrar _rize_field_reg_##name{ this, #name, &this->name };

/// Exported by GameScripts.dll (GameScripts_GetApiFingerprint) and compared by the engine on load
constexpr uint32_t ScriptApiFingerprint() {
	return (kScriptApiVersion << 24)
		^ (static_cast<uint32_t>(sizeof(EngineAPI)) << 12)
		^ (static_cast<uint32_t>(sizeof(ScriptGameObjectView)) << 4)
		^ static_cast<uint32_t>(sizeof(ScriptBase));
}

#ifdef GAMESCRIPTS_EXPORTS
/// Internal DLL registration entry point implemented in GameScripts/Scripts/dllmain.cpp
void GameScripts_RegisterClass(const char* className, std::unique_ptr<ScriptBase> (*factory)());

#define REGISTER_SCRIPT(ClassName) \
	namespace { \
		std::unique_ptr<ScriptBase> Create_##ClassName##_Instance() { \
			return std::make_unique<ClassName>(); \
		} \
		struct ClassName##_DllRegistrar { \
			ClassName##_DllRegistrar() { \
				GameScripts_RegisterClass(#ClassName, &Create_##ClassName##_Instance); \
			} \
		}; \
		static ClassName##_DllRegistrar s_##ClassName##_dllRegistrar; \
	} \
	extern "C" __declspec(dllexport) const void* ForceLinkScript_##ClassName() { \
		return &s_##ClassName##_dllRegistrar; \
	} \
	__pragma(comment(linker, "/include:ForceLinkScript_" #ClassName))
#endif
