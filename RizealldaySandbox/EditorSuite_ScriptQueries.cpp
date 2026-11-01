#include "EditorSuiteInternal.h"
#include "audio/Audio.h"

// Script API part 2 (Tahap H): collision enter/exit, scene queries, hierarchy, audio, particles,
// material and component access. EngineAPI entries are filled in by SetupEngineAPIExtensions().

namespace {
Vector3 WorldPosition(const GameObject& obj)
{
	return { obj.worldMatrix.m[3][0], obj.worldMatrix.m[3][1], obj.worldMatrix.m[3][2] };
}

/// Address of one field of a data component the object has, nullptr when any part is missing
void* FindDataComponentField(GameObject& obj, const char* component, const char* field, const ComponentField*& outField)
{
	const ComponentInfo* info = ComponentRegistry::Get().Find(component);
	ComponentData* data = (info && info->IsDataComponent()) ? obj.components.Find(component) : nullptr;
	if (!data) return nullptr;
	for (const ComponentField& candidate : info->fields) {
		if (std::strcmp(candidate.name, field) == 0) {
			outField = &candidate;
			return static_cast<char*>(data->Data()) + candidate.offset;
		}
	}
	return nullptr;
}

/// Animator of a script target, started at its entry state if the state machine has not run yet
/// (Initialize runs before the first animation update, and parameters set there must not be lost)
AnimatorComponent* ScriptAnimator(void* ctx, uint32_t id, std::shared_ptr<AnimatorController>& outController)
{
	if (!ctx) return nullptr;
	EditorSuite& self = *static_cast<EditorSuite*>(ctx);
	GameObject* obj = self.FindGameObjectById(id, false);
	AnimatorComponent* a = obj ? self.GetAnimator(*obj) : nullptr;
	if (!a || a->controller[0] == '\0') return nullptr;
	outController = self.GetAnimatorController(a->controller);
	if (!outController || outController->states.empty()) return nullptr;
	if (!a->runtime.started || a->runtimeController != a->controller) {
		a->runtime = AnimatorRuntime{};
		a->runtimeController = a->controller;
		const AnimatorRuntime::Change change = a->runtime.Start(*outController);
		if (const AnimState* s = change.changed ? outController->FindState(change.state) : nullptr) self.ApplyAnimatorState(*obj, *a, *s, 0.0f);
	}
	return a;
}

constexpr uint64_t ContactKey(uint32_t selfId, uint32_t otherId)
{
	return (static_cast<uint64_t>(selfId) << 32) | otherId;
}

float Dot3(const Vector3& a, const Vector3& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Objects about to be deleted or despawned at the end of this frame are invisible to queries,
// so two bullets fired in the same frame cannot both "hit" an enemy that the first one destroyed.
std::unordered_set<uint32_t> CollectQueryExclusions(const std::vector<GameObject>& objects,
	const std::vector<int>& pendingDeleteIndices, const std::vector<uint32_t>& pendingDespawnIds)
{
	std::unordered_set<uint32_t> excluded(pendingDespawnIds.begin(), pendingDespawnIds.end());
	for (int index : pendingDeleteIndices) {
		if (index >= 0 && static_cast<size_t>(index) < objects.size()) {
			excluded.insert(objects[static_cast<size_t>(index)].id);
		}
	}
	return excluded;
}

bool HasCollider(const GameObject& obj)
{
	return (obj.hasBoxCollider && obj.boxColliderEnabled) || (obj.hasSphereCollider && obj.sphereColliderEnabled);
}

bool TagMatches(const GameObject& obj, const char* tag)
{
	return !tag || tag[0] == '\0' || obj.tag == tag;
}
}

// ---------------------------------------------------------------------------
// Collision events
// ---------------------------------------------------------------------------
void EditorSuite::RecordCollisionContact(uint32_t selfId, uint32_t otherId)
{
	if (selfId == otherId) return;
	const uint64_t key = ContactKey(selfId, otherId);
	if (frameContactSet_.insert(key).second) {
		frameContacts_.push_back(key);
	}
}

void EditorSuite::DispatchCollisionEvents()
{
	std::vector<uint64_t> current;
	current.swap(frameContacts_);
	std::unordered_set<uint64_t> currentSet;
	currentSet.swap(frameContactSet_);
	lastCollisionEvents_ = CollisionEventCounts{};

	for (uint64_t key : current) {
		const uint32_t selfId = static_cast<uint32_t>(key >> 32);
		const uint32_t otherId = static_cast<uint32_t>(key & 0xFFFFFFFFu);
		if (previousContactSet_.count(key) == 0) {
			++lastCollisionEvents_.enter;
			DispatchScriptCollision(selfId, otherId, CollisionEvent::Enter);
		}
		++lastCollisionEvents_.stay;
		DispatchScriptCollision(selfId, otherId, CollisionEvent::Stay);
	}
	for (uint64_t key : previousContacts_) {
		if (currentSet.count(key) == 0) {
			++lastCollisionEvents_.exit;
			DispatchScriptCollision(static_cast<uint32_t>(key >> 32), static_cast<uint32_t>(key & 0xFFFFFFFFu), CollisionEvent::Exit);
		}
	}

	previousContacts_ = std::move(current);
	previousContactSet_ = std::move(currentSet);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
Vector3 EditorSuite::GetWorldPositionOf(const GameObject& obj) const
{
	// Roots: translation is fresh even mid-frame. Children: the world matrix (refreshed every frame).
	if (obj.parentId == -1) return obj.translation;
	return { obj.worldMatrix.m[3][0], obj.worldMatrix.m[3][1], obj.worldMatrix.m[3][2] };
}

bool EditorSuite::ScriptRaycast(const Vector3& origin, const Vector3& direction, float maxDistance,
	uint32_t layerMask, uint32_t ignoreId, ScriptRaycastHit& outHit)
{
	outHit = ScriptRaycastHit{};
	const float len = std::sqrt(Dot3(direction, direction));
	if (!(len > 1.0e-6f) || !(maxDistance > 0.0f) || !std::isfinite(len) || !std::isfinite(maxDistance)) return false;

	SyncPhysicsForQueries();
	const auto excluded = CollectQueryExclusions(gameObjects_, pendingDeletes_, pendingDespawns_);
	Physics::RayHit hit;
	if (!physicsWorld_->Raycast(origin, direction / len, maxDistance, [&](uint32_t id) { return AcceptQueryHit(id, layerMask, ignoreId, excluded); }, hit)) {
		return false;
	}
	outHit.objectId = hit.id;
	outHit.distance = hit.distance;
	outHit.point = hit.point;
	outHit.normal = hit.normal;
	return true;
}

void EditorSuite::BuildNavGrid(const Vector3& agentPosition)
{
	// Level geometry = Static, non-trigger colliders. The floor is the highest static top under the agent (at or
	// below its position); anything more than 0.3 above that floor blocks.
	// shortcut: one floor per grid; agents on different storeys rebuild it in turn (fine for one or two floors)
	struct Bounds { Vector3 min, max; };
	std::vector<Bounds> statics;
	for (const auto& o : gameObjects_) {
		if (!o.isActive || !o.isStatic || o.isTrigger) continue;
		Bounds b{};
		if (o.hasBoxCollider && o.boxColliderEnabled) {
			ComputeBoxColliderBounds(o, true, b.min, b.max);
			statics.push_back(b);
		}
		if (o.hasSphereCollider && o.sphereColliderEnabled) {
			float r = 0.0f;
			ComputeSphereColliderWorld(o, b.min, r);
			b.max = { b.min.x + r, b.min.y + r, b.min.z + r };
			b.min = { b.min.x - r, b.min.y - r, b.min.z - r };
			statics.push_back(b);
		}
	}
	navGrid_ = NavGrid{};
	navGridValid_ = true;
	if (statics.empty()) return;

	float lowestTop = 1e30f, floorTop = -1e30f;
	Vector3 lo{ 1e30f, 0.0f, 1e30f }, hi{ -1e30f, 0.0f, -1e30f };
	for (const auto& b : statics) {
		lowestTop = (std::min)(lowestTop, b.max.y);
		const bool under = agentPosition.x >= b.min.x && agentPosition.x <= b.max.x && agentPosition.z >= b.min.z && agentPosition.z <= b.max.z;
		if (under && b.max.y <= agentPosition.y + 0.01f) floorTop = (std::max)(floorTop, b.max.y);
		lo = { (std::min)(lo.x, b.min.x), 0.0f, (std::min)(lo.z, b.min.z) };
		hi = { (std::max)(hi.x, b.max.x), 0.0f, (std::max)(hi.z, b.max.z) };
	}
	if (floorTop < -1e29f) floorTop = lowestTop; // nothing under the agent
	navFloorY_ = floorTop;
	navGrid_.Build(lo.x - 1.0f, lo.z - 1.0f, hi.x + 1.0f, hi.z + 1.0f, navCellSize_);
	const float r = navAgentRadius_;
	for (const auto& b : statics) {
		if (b.max.y > floorTop + 0.3f) navGrid_.BlockRect(b.min.x - r, b.min.z - r, b.max.x + r, b.max.z + r);
	}
}

int EditorSuite::ScriptFindPath(const Vector3& from, const Vector3& to, Vector3* outPoints, int maxPoints)
{
	// Rebuild for another storey: the agent stands well above or below the floor the grid was made for
	if (!navGridValid_ || (!navGrid_.IsEmpty() && (from.y < navFloorY_ - 0.3f || from.y > navFloorY_ + 2.5f))) BuildNavGrid(from);
	// No level geometry at all: the way is open
	const std::vector<Vector3> path = navGrid_.IsEmpty() ? std::vector<Vector3>{ from, to } : navGrid_.FindPath(from, to);
	const int count = static_cast<int>(path.size());
	if (outPoints) {
		for (int i = 0; i < (std::min)(count, maxPoints); ++i) outPoints[i] = path[i];
	}
	return count;
}

bool EditorSuite::ScriptMoveCharacter(uint32_t id, const Vector3& motion, float stepHeight, ScriptCharacterMove& out)
{
	out = {};
	GameObject* obj = FindGameObjectById(id, false);
	// Root objects only: their collider follows `translation` directly
	if (!obj || !obj->isActive || obj->parentId != -1) return false;

	// The script may have moved its view earlier in this Update
	auto viewIt = scriptObjectViews_.find(id);
	ScriptGameObjectView* view = viewIt != scriptObjectViews_.end() ? viewIt->second.get() : nullptr;
	if (view) obj->translation = view->translation;

	Physics::BodyDesc desc;
	if (!BuildPhysicsBody(*obj, desc) || desc.shapes.empty()) return false;

	SyncPhysicsForQueries();
	const auto excluded = CollectQueryExclusions(gameObjects_, pendingDeletes_, pendingDespawns_);
	const uint32_t selfBit = 1u << (obj->layer & 31u);
	const uint32_t selfMask = obj->collisionMask;
	Physics::CharacterMoveResult r;
	const bool moved = physicsWorld_->MoveCharacter(desc, motion, stepHeight, [&](uint32_t other) {
		if (excluded.count(other) != 0) return false;
		const GameObject* o = FindGameObjectById(other, false);
		return o && o->isActive && (selfMask & (1u << (o->layer & 31u))) != 0 && (o->collisionMask & selfBit) != 0;
	}, r);
	if (!moved) return false;

	obj = FindGameObjectById(id, false);
	obj->translation = { obj->translation.x + r.moved.x, obj->translation.y + r.moved.y, obj->translation.z + r.moved.z };
	obj->isGrounded = r.grounded;
	if (view) {
		view->translation = obj->translation;
		view->isGrounded = r.grounded;
	}
	out.moved = r.moved;
	out.grounded = r.grounded;
	out.hitWall = r.hitWall;
	out.hitCeiling = r.hitCeiling;
	return true;
}

int EditorSuite::ScriptOverlapSphere(const Vector3& center, float radius, uint32_t layerMask, uint32_t* outIds, int maxCount)
{
	if (!outIds || maxCount <= 0 || !(radius >= 0.0f) || !std::isfinite(radius)) return 0;
	SyncPhysicsForQueries();
	const auto excluded = CollectQueryExclusions(gameObjects_, pendingDeletes_, pendingDespawns_);
	std::vector<uint32_t> ids;
	physicsWorld_->OverlapSphere(center, radius, [&](uint32_t id) { return AcceptQueryHit(id, layerMask, 0, excluded); }, ids);
	const int count = (std::min)(static_cast<int>(ids.size()), maxCount);
	std::copy_n(ids.begin(), count, outIds);
	return count;
}

bool EditorSuite::AcceptQueryHit(uint32_t id, uint32_t layerMask, uint32_t ignoreId, const std::unordered_set<uint32_t>& excluded) const
{
	if (id == ignoreId || excluded.count(id) != 0) return false;
	const GameObject* obj = const_cast<EditorSuite*>(this)->FindGameObjectById(id, false);
	if (!obj || !obj->isActive) return false; // removed since the last step: its body is still in the world
	if ((layerMask & LayerBit(obj->layer)) == 0) return false;
	// A body that exists only to move (Rigidbody without collider) has no surface to hit
	return HasCollider(*obj) || !obj->components.Empty();
}

const GameObject* EditorSuite::FindNearestObject(const Vector3& position, const char* tag, float maxDistance, uint32_t ignoreId) const
{
	const auto excluded = CollectQueryExclusions(gameObjects_, pendingDeletes_, pendingDespawns_);
	const GameObject* nearest = nullptr;
	float bestSq = (maxDistance > 0.0f && std::isfinite(maxDistance)) ? maxDistance * maxDistance : 3.0e38f;
	for (const auto& obj : gameObjects_) {
		if (!obj.isActive || obj.id == ignoreId || !TagMatches(obj, tag) || excluded.count(obj.id) != 0) continue;
		const Vector3 delta = GetWorldPositionOf(obj) - position;
		const float distSq = Dot3(delta, delta);
		if (distSq <= bestSq) {
			bestSq = distSq;
			nearest = &obj;
		}
	}
	return nearest;
}

// ---------------------------------------------------------------------------
// Deferred requests & runtime state
// ---------------------------------------------------------------------------
bool EditorSuite::MouseInputAllowed() const
{
	if (cursorLocked_) return true;
	float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
	return GetGameViewMousePosition(x, y, w, h);
}

void EditorSuite::UpdateCursorLock()
{
	const bool inPlay = sceneState_ != EditorSceneState::Edit || IsStandaloneMode();
	if (!inPlay) cursorLocked_ = false;
	HWND hwnd = WinApp::GetInstance()->GetHWND();
#ifdef USE_IMGUI
	// A locked cursor must never trap the user inside the editor
	if (cursorLocked_ && !IsStandaloneMode() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) cursorLocked_ = false;
#endif
	RECT client{};
	const bool active = cursorLocked_ && hwnd && GetForegroundWindow() == hwnd && GetClientRect(hwnd, &client);
	if (active) {
		POINT center{ (client.right - client.left) / 2, (client.bottom - client.top) / 2 };
#ifdef USE_IMGUI
		if (gameViewRect_[2] > 0.0f && gameViewRect_[3] > 0.0f) {
			center.x = static_cast<LONG>(gameViewRect_[0] + gameViewRect_[2] * 0.5f);
			center.y = static_cast<LONG>(gameViewRect_[1] + gameViewRect_[3] * 0.5f);
		}
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
#endif
		ClientToScreen(hwnd, &center);
		const RECT clip{ center.x, center.y, center.x + 1, center.y + 1 };
		ClipCursor(&clip);
		if (!cursorHidden_) {
			ShowCursor(FALSE);
			cursorHidden_ = true;
		}
	} else if (cursorHidden_) {
		ClipCursor(nullptr);
		ShowCursor(TRUE);
		cursorHidden_ = false;
#ifdef USE_IMGUI
		ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
#endif
	}
}

#ifdef USE_IMGUI
// No C++ objects in here: __try cannot share a function with destructors
bool EditorSuite::RunScriptGuarded(void (*fn)(void*), void* ctx, unsigned long& outCode)
{
	__try {
		fn(ctx);
		return true;
	} __except (outCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
		if (outCode == EXCEPTION_STACK_OVERFLOW) _resetstkoflw();
		return false;
	}
}

void EditorSuite::OnScriptFault(ScriptBase* instance, uint32_t objectId, const char* callback, unsigned long code)
{
	std::string className = "?";
	std::string objectName = "?";
	if (GameObject* obj = FindGameObjectById(objectId, false)) {
		objectName = obj->name;
		for (auto& sc : obj->scripts) {
			if (sc.instance.get() != instance) continue;
			className = sc.className;
			// shortcut: the broken instance is leaked, its destructor could crash again; freed when the DLL unloads
			(void)sc.instance.release();
		}
	}
	scriptFaultPending_ = true;
	++scriptFaultCount_;
	scriptFaultMessage_ = std::format("[Scripts] Crash (0x{:08X}) in {}::{} on '{}'. Play was stopped and the scene restored.", code, className, callback, objectName);
}

void EditorSuite::HandleScriptFault()
{
	LogMessage(scriptFaultMessage_, 2);
	if (sceneState_ != EditorSceneState::Edit) StopPlayMode(); // the fault flag keeps OnDestroy of the other scripts from running
	scriptFaultPending_ = false;
	// Static data of the DLL may be damaged: start from a fresh copy
	HotReloadScripts();
}
#endif

bool EditorSuite::StartAudioSource(GameObject& obj, bool preview)
{
	if (obj.audioClipName.empty() || obj.audioClipName == "None") return false;
	Audio* audio = Audio::GetInstance();
	if (obj.runtimeVoiceHandle != 0) audio->StopAudio(obj.runtimeVoiceHandle);
	obj.runtimeSoundHandle = audio->LoadAudio(ResolveAssetPath(obj.audioClipName));
	Audio::PlayParams params;
	params.loop = obj.audioLoop;
	params.volume = obj.audioVolume;
	params.pitch = obj.audioPitch;
	params.bus = static_cast<AudioBus>(obj.audioBus);
	params.spatial = obj.audioSpatial && !preview;
	params.position = WorldPosition(obj);
	params.minDistance = obj.audioMinDistance;
	params.maxDistance = obj.audioMaxDistance;
	obj.runtimeVoiceHandle = audio->PlayAudio(obj.runtimeSoundHandle, params);
	return obj.runtimeVoiceHandle != 0;
}

void EditorSuite::UpdateAudio()
{
	Audio* audio = Audio::GetInstance();
	if (!audio->IsAvailable()) return;
	if (gameCamera_) {
		const Matrix4x4& view = gameCamera_->GetViewMatrix();
		audio->SetListener(gameCamera_->translation, { view.m[0][0], view.m[1][0], view.m[2][0] }); // first column of a view matrix = camera right
	}
	for (const GameObject& obj : gameObjects_) {
		if (obj.runtimeVoiceHandle != 0 && obj.audioSpatial) audio->SetVoicePosition(obj.runtimeVoiceHandle, WorldPosition(obj));
	}
	std::erase_if(audioFollowers_, [&](const AudioFollower& follower) {
		const GameObject* obj = FindGameObjectById(follower.objectId, false);
		if (!obj || !audio->IsPlaying(follower.voice)) return true;
		audio->SetVoicePosition(follower.voice, WorldPosition(*obj));
		return false;
	});
	audio->ApplySpatial();
}

void EditorSuite::ResetScriptRuntimeState()
{
	audioFollowers_.clear();
	cursorLocked_ = false;
	postProcess_.ClearScreenEffects();
	navGridValid_ = false; // the next FindPath sees the current level
	frameContacts_.clear();
	frameContactSet_.clear();
	previousContacts_.clear();
	previousContactSet_.clear();
	delayedDespawns_.clear();
	pendingReparents_.clear();
	pendingParticleBursts_.clear();
	if (musicVoice_ != 0) {
		Audio::GetInstance()->StopAudio(musicVoice_);
	}
	musicVoice_ = 0;
	shakeLeft_ = 0.0f;
	hitstopLeft_ = 0.0f;
	physicsAccumulator_ = 0.0;
	if (physicsWorld_) physicsWorld_->Clear();
	for (const auto& [id, ragdoll] : ragdolls_) {
		if (GameObject* obj = FindGameObjectById(id, false); obj && obj->editObject3d) obj->editObject3d->GetAnimationPlayer().Overrides().clear();
	}
	ragdolls_.clear();
	physicsQueryDirty_ = true;
	renderPoses_.clear();
	renderPosesApplied_ = false;
	musicPath_.clear();
}

void EditorSuite::CancelDelayedDespawn(uint32_t id)
{
	std::erase_if(delayedDespawns_, [id](const DelayedDespawn& d) { return d.id == id; });
}

void EditorSuite::TickScriptTimers(float deltaTime)
{
	if (delayedDespawns_.empty()) return;
	std::vector<uint32_t> due;
	for (auto it = delayedDespawns_.begin(); it != delayedDespawns_.end();) {
		it->remaining -= deltaTime;
		if (it->remaining <= 0.0f) {
			due.push_back(it->id);
			it = delayedDespawns_.erase(it);
		} else {
			++it;
		}
	}
	for (uint32_t id : due) QueueDespawn(id);
}

void EditorSuite::BurstOneShotParticles(uint32_t id)
{
	const GameObject* obj = FindGameObjectById(id, false);
	if (!obj || !obj->isActive || !obj->hasParticleSystem || obj->particleEmitContinuously) return;
	pendingParticleBursts_.push_back({ id, obj->particleBurstCount, true });
}

void EditorSuite::ProcessDeferredScriptRequests()
{
	if (!pendingReparents_.empty()) {
		std::vector<PendingReparent> reparents;
		reparents.swap(pendingReparents_);
		// Play-mode hierarchy changes are runtime state: they must not mark the scene as edited
		const bool wasDirty = isSceneDirty_;
		for (const auto& r : reparents) {
			if (FindGameObjectById(r.childId, false)) {
				ReparentGameObject(r.childId, r.parentId, r.keepWorldTransform);
			}
		}
		isSceneDirty_ = wasDirty;
	}

	if (!pendingParticleBursts_.empty()) {
		std::vector<PendingParticleBurst> bursts;
		bursts.swap(pendingParticleBursts_);
		UpdateWorldMatrices(); // emit from the current world position (children of spawned prefabs)
		for (const auto& b : bursts) {
			GameObject* obj = FindGameObjectById(b.id, false);
			if (!obj || !obj->isActive || !obj->hasParticleSystem) continue;
			EnsureParticleQuadInitialized(*obj);
			if (!obj->particleQuad) continue;
			if (b.restart) obj->particleQuad->ClearParticles();
			obj->particleQuad->emitter.translate = GetWorldPositionOf(*obj);
			const int count = std::clamp(b.count, 1, (std::max)(1, obj->particleMaxParticles));
			obj->particleQuad->EmitBurst(static_cast<uint32_t>(count));
		}
	}
}

// ---------------------------------------------------------------------------
// EngineAPI entries
// ---------------------------------------------------------------------------
void EditorSuite::SetupEngineAPIExtensions()
{
	// ---- Camera input and projection ----
	engineApi_.getMouseDelta = [](void* ctx, Vector2* outPixels) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || !outPixels || !self->currentInput_ || !self->MouseInputAllowed()) return false;
		*outPixels = { static_cast<float>(self->currentInput_->GetMouseMoveX()), static_cast<float>(self->currentInput_->GetMouseMoveY()) };
		return true;
	};
	engineApi_.getMouseWheel = [](void* ctx) -> float {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || !self->currentInput_ || !self->MouseInputAllowed()) return 0.0f;
		return static_cast<float>(self->currentInput_->GetMouseWheel()) / WHEEL_DELTA;
	};
	engineApi_.setCursorLocked = [](void* ctx, bool locked) {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (self && (self->sceneState_ != EditorSceneState::Edit || self->IsStandaloneMode())) self->cursorLocked_ = locked;
	};
	engineApi_.isCursorLocked = [](void* ctx) -> bool {
		return ctx && static_cast<EditorSuite*>(ctx)->cursorLocked_;
	};
	engineApi_.screenPointToRay = [](void* ctx, Vector2 canvasPos, Vector3* outOrigin, Vector3* outDirection) -> bool {
		return ctx && outOrigin && outDirection && static_cast<EditorSuite*>(ctx)->ScreenPointToRay(canvasPos, *outOrigin, *outDirection);
	};
	engineApi_.worldToCanvas = [](void* ctx, Vector3 worldPos, Vector2* outCanvasPos) -> bool {
		return ctx && outCanvasPos && static_cast<EditorSuite*>(ctx)->WorldToCanvas(worldPos, *outCanvasPos);
	};
	engineApi_.prewarmPrefab = [](void* ctx, const char* prefabName, int count) -> bool {
		return ctx && static_cast<EditorSuite*>(ctx)->QueuePrefabPrewarm(prefabName, count);
	};

	// ---- Post-processing ----
	engineApi_.getPostProcess = [](void* ctx, ScriptPostProcess* out) -> bool {
		if (!ctx || !out) return false;
		const PostProcessSettings& p = static_cast<EditorSuite*>(ctx)->postSettings_;
		*out = { p.enabled, p.toneMapping, p.bloom, p.fxaa, p.exposure, p.bloomThreshold, p.bloomIntensity, p.contrast, p.saturation, p.tint, p.vignette, p.letterbox };
		return true;
	};
	engineApi_.setPostProcess = [](void* ctx, const ScriptPostProcess* value) -> bool {
		if (!ctx || !value) return false;
		PostProcessSettings& p = static_cast<EditorSuite*>(ctx)->postSettings_;
		p = { value->enabled, value->toneMapping, value->bloom, value->fxaa, (std::max)(value->exposure, 0.0f), value->bloomThreshold, (std::max)(value->bloomIntensity, 0.0f),
			(std::max)(value->contrast, 0.0f), (std::max)(value->saturation, 0.0f), value->tint, std::clamp(value->vignette, 0.0f, 1.0f), std::clamp(value->letterbox, 0.0f, 0.5f) };
		return true;
	};
	engineApi_.setScreenEffect = [](void* ctx, int slot, const char* shaderFile, const float* params16) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self) return false;
		if (!shaderFile) {
			self->postProcess_.ClearScreenEffect(slot);
			return true;
		}
		std::error_code ec;
		const std::filesystem::path file = std::filesystem::exists(shaderFile, ec) ? std::filesystem::path(shaderFile) : std::filesystem::path(self->ResolveAssetPath(shaderFile));
		std::string error;
		const bool ok = self->postProcess_.SetScreenEffect(slot, file.wstring(), params16, &error);
		if (!error.empty()) self->LogMessage(std::format("[ScreenEffect] {}: {}", shaderFile, error), 2);
		return ok;
	};

	// ---- Audio ----
	engineApi_.playSound = [](void* ctx, const char* path, float volume, bool loop) -> uint32_t {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || !path || path[0] == '\0') return 0u;
		Audio* audio = Audio::GetInstance();
		const uint32_t sound = audio->LoadAudio(self->ResolveAssetPath(path));
		if (sound == 0) return 0u;
		const float v = std::isfinite(volume) ? std::clamp(volume, 0.0f, 4.0f) : 1.0f;
		return audio->PlayAudio(sound, loop, v);
	};
	engineApi_.stopSound = [](void*, uint32_t voice) {
		Audio::GetInstance()->StopAudio(voice);
	};
	engineApi_.setSoundVolume = [](void*, uint32_t voice, float volume) {
		Audio::GetInstance()->SetVolume(voice, std::isfinite(volume) ? volume : 1.0f);
	};
	engineApi_.isSoundPlaying = [](void*, uint32_t voice) -> bool {
		return Audio::GetInstance()->IsPlaying(voice);
	};
	engineApi_.preloadSound = [](void* ctx, const char* path) {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (self && path && path[0] != '\0') Audio::GetInstance()->LoadAudio(self->ResolveAssetPath(path));
	};
	engineApi_.playMusic = [](void* ctx, const char* path, float volume) {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || !path || path[0] == '\0') return;
		Audio* audio = Audio::GetInstance();
		const std::string resolved = self->ResolveAssetPath(path);
		const float v = std::isfinite(volume) ? std::clamp(volume, 0.0f, 4.0f) : 1.0f;
		if (self->musicVoice_ != 0 && self->musicPath_ == resolved && audio->IsPlaying(self->musicVoice_)) {
			audio->SetVolume(self->musicVoice_, v);
			return;
		}
		if (self->musicVoice_ != 0) audio->StopAudio(self->musicVoice_);
		const uint32_t sound = audio->LoadAudio(resolved);
		Audio::PlayParams params;
		params.loop = true;
		params.volume = v;
		params.bus = AudioBus::Music;
		self->musicVoice_ = (sound != 0) ? audio->PlayAudio(sound, params) : 0u;
		self->musicPath_ = (self->musicVoice_ != 0) ? resolved : std::string();
	};
	engineApi_.stopMusic = [](void* ctx) {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self) return;
		if (self->musicVoice_ != 0) Audio::GetInstance()->StopAudio(self->musicVoice_);
		self->musicVoice_ = 0;
		self->musicPath_.clear();
	};
	engineApi_.playSoundEx = [](void* ctx, const char* path, const ScriptAudioParams* in) -> uint32_t {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || !path || path[0] == '\0' || !in) return 0u;
		Audio* audio = Audio::GetInstance();
		const uint32_t sound = audio->LoadAudio(self->ResolveAssetPath(path));
		if (sound == 0) return 0u;
		Audio::PlayParams params;
		params.loop = in->loop;
		params.volume = in->volume;
		params.pitch = in->pitch;
		params.bus = static_cast<AudioBus>(in->bus);
		params.spatial = in->spatial || in->followObject != 0;
		params.position = in->position;
		params.minDistance = in->minDistance;
		params.maxDistance = in->maxDistance;
		const GameObject* followed = in->followObject != 0 ? self->FindGameObjectById(in->followObject, false) : nullptr;
		if (followed) params.position = WorldPosition(*followed);
		const uint32_t voice = audio->PlayAudio(sound, params);
		if (voice != 0 && followed) self->audioFollowers_.push_back({ voice, in->followObject });
		return voice;
	};
	engineApi_.setSoundPosition = [](void*, uint32_t voice, Vector3 position) {
		Audio::GetInstance()->SetVoicePosition(voice, position);
	};
	engineApi_.setSoundPitch = [](void*, uint32_t voice, float pitch) {
		Audio::GetInstance()->SetVoicePitch(voice, pitch);
	};
	engineApi_.setBusVolume = [](void*, int bus, float volume) {
		Audio::GetInstance()->SetBusVolume(static_cast<AudioBus>(bus), volume);
	};
	engineApi_.getBusVolume = [](void*, int bus) -> float {
		return Audio::GetInstance()->GetBusVolume(static_cast<AudioBus>(bus));
	};
	engineApi_.playAudioSource = [](void* ctx, uint32_t id) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		GameObject* obj = self ? self->FindGameObjectById(id, false) : nullptr;
		return obj && (obj->hasAudioSource || obj->type == GameObjectType::AudioSource) && self->StartAudioSource(*obj);
	};
	engineApi_.stopAudioSource = [](void* ctx, uint32_t id) {
		auto* self = static_cast<EditorSuite*>(ctx);
		GameObject* obj = self ? self->FindGameObjectById(id, false) : nullptr;
		if (!obj || obj->runtimeVoiceHandle == 0) return;
		Audio::GetInstance()->StopAudio(obj->runtimeVoiceHandle);
		obj->runtimeVoiceHandle = 0;
	};

	// ---- Components by name ----
	engineApi_.hasComponent = [](void* ctx, uint32_t id, const char* component) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		const GameObject* obj = (self && component) ? self->FindGameObjectById(id, false) : nullptr;
		return obj && self->HasComponentByName(*obj, component);
	};
	engineApi_.addComponent = [](void* ctx, uint32_t id, const char* component) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		GameObject* obj = (self && component) ? self->FindGameObjectById(id, false) : nullptr;
		return obj && self->AddComponentByName(*obj, component);
	};
	engineApi_.removeComponent = [](void* ctx, uint32_t id, const char* component) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		GameObject* obj = (self && component) ? self->FindGameObjectById(id, false) : nullptr;
		return obj && self->RemoveComponentByName(*obj, component);
	};
	engineApi_.getComponentField = [](void* ctx, uint32_t id, const char* component, const char* field, ScriptComponentValue* out) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || !component || !field || !out) return false;
		GameObject* obj = self->FindGameObjectById(id, false);
		const ComponentField* info = nullptr;
		void* ptr = obj ? FindDataComponentField(*obj, component, field, info) : nullptr;
		if (!ptr) return false;
		switch (info->type) {
		case ComponentFieldType::Float:
			out->kind = ScriptComponentValue::Float;
			out->number[0] = *static_cast<const float*>(ptr);
			break;
		case ComponentFieldType::Int:
			out->kind = ScriptComponentValue::Int;
			out->integer = *static_cast<const int*>(ptr);
			break;
		case ComponentFieldType::Bool:
			out->kind = ScriptComponentValue::Bool;
			out->integer = *static_cast<const bool*>(ptr) ? 1 : 0;
			break;
		case ComponentFieldType::Vector3:
			out->kind = ScriptComponentValue::Vector;
			std::memcpy(out->number, ptr, sizeof(float) * 3);
			break;
		case ComponentFieldType::Color:
			out->kind = ScriptComponentValue::Color;
			out->number[3] = 1.0f;
			std::memcpy(out->number, ptr, (std::min)(info->size, sizeof(out->number)));
			break;
		case ComponentFieldType::Text:
		case ComponentFieldType::Asset:
			out->kind = ScriptComponentValue::Text;
			strncpy_s(out->text, static_cast<const char*>(ptr), _TRUNCATE);
			break;
		}
		return true;
	};
	engineApi_.setComponentField = [](void* ctx, uint32_t id, const char* component, const char* field, const ScriptComponentValue* value) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || !component || !field || !value) return false;
		GameObject* obj = self->FindGameObjectById(id, false);
		const ComponentField* info = nullptr;
		void* ptr = obj ? FindDataComponentField(*obj, component, field, info) : nullptr;
		if (!ptr) return false;
		const auto allFinite = [&](int count) {
			for (int i = 0; i < count; ++i) {
				if (!std::isfinite(value->number[i])) return false;
			}
			return true;
		};
		const bool limited = info->minValue != info->maxValue;
		switch (info->type) {
		case ComponentFieldType::Float:
			if (value->kind != ScriptComponentValue::Float || !allFinite(1)) return false;
			*static_cast<float*>(ptr) = limited ? std::clamp(value->number[0], info->minValue, info->maxValue) : value->number[0];
			break;
		case ComponentFieldType::Int:
			if (value->kind != ScriptComponentValue::Int) return false;
			*static_cast<int*>(ptr) = limited ? std::clamp(value->integer, static_cast<int>(info->minValue), static_cast<int>(info->maxValue)) : value->integer;
			break;
		case ComponentFieldType::Bool:
			if (value->kind != ScriptComponentValue::Bool) return false;
			*static_cast<bool*>(ptr) = value->integer != 0;
			break;
		case ComponentFieldType::Vector3:
			if (value->kind != ScriptComponentValue::Vector || !allFinite(3)) return false;
			std::memcpy(ptr, value->number, sizeof(float) * 3);
			break;
		case ComponentFieldType::Color:
			if (value->kind != ScriptComponentValue::Color || !allFinite(4)) return false;
			std::memcpy(ptr, value->number, (std::min)(info->size, sizeof(value->number)));
			break;
		case ComponentFieldType::Text:
		case ComponentFieldType::Asset:
			if (value->kind != ScriptComponentValue::Text) return false;
			strncpy_s(static_cast<char*>(ptr), info->size, value->text, _TRUNCATE);
			break;
		}
		return true;
	};

	// ---- Particles & timers ----
	engineApi_.emitParticles = [](void* ctx, uint32_t id, int count) {
		if (ctx && id != 0 && count > 0) static_cast<EditorSuite*>(ctx)->pendingParticleBursts_.push_back({ id, count, false });
	};
	engineApi_.setParticlesEmitting = [](void* ctx, uint32_t id, bool emitting) {
		if (!ctx) return;
		if (GameObject* obj = static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false)) {
			obj->particleEmitContinuously = emitting;
		}
	};
	engineApi_.despawnAfter = [](void* ctx, uint32_t id, float seconds) {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || id == 0 || std::isnan(seconds)) return;
		self->CancelDelayedDespawn(id);
		if (seconds <= 0.0f) {
			self->QueueDespawn(id);
			return;
		}
		self->delayedDespawns_.push_back({ id, seconds });
	};

	// ---- Material ----
	engineApi_.getMaterial = [](void* ctx, uint32_t id, ScriptMaterialData* out) -> bool {
		const GameObject* obj = ctx ? static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false) : nullptr;
		if (!obj || !obj->hasMesh || !out) return false;
		out->color = obj->materialColor;
		out->visible = obj->meshEnabled;
		out->lit = obj->isLit;
		out->lightingType = obj->lightingType;
		out->shininess = obj->shininess;
		out->uvOffset = obj->uvOffset;
		out->uvTiling = obj->uvTiling;
		out->outlineWidth = obj->outlineWidth;
		out->outlineColor = obj->outlineColor;
		return true;
	};
	engineApi_.setMaterial = [](void* ctx, uint32_t id, const ScriptMaterialData* in) -> bool {
		GameObject* obj = ctx ? static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false) : nullptr;
		if (!obj || !obj->hasMesh || !in) return false;
		obj->materialColor = in->color;
		obj->meshEnabled = in->visible;
		obj->isLit = in->lit;
		obj->lightingType = std::clamp(in->lightingType, 0, 5);
		obj->shininess = (std::max)(0.0f, in->shininess);
		obj->uvOffset = in->uvOffset;
		obj->uvTiling = in->uvTiling;
		obj->outlineWidth = (std::max)(0.0f, in->outlineWidth);
		obj->outlineColor = in->outlineColor;
		return true;
	};
	engineApi_.setTexture = [](void* ctx, uint32_t id, const char* texturePath) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		GameObject* obj = self ? self->FindGameObjectById(id, false) : nullptr;
		if (!obj || !obj->hasMesh || !texturePath) return false;
		obj->texturePath = (texturePath[0] != '\0') ? texturePath : "Resources/Model/white.png";
		// LoadTexture caches by path, so switching back and forth is cheap
		obj->textureHandle = TextureManager::GetInstance()->LoadTexture(self->ResolveAssetPath(obj->texturePath));
		return true;
	};

	// ---- Queries ----
	engineApi_.raycast = [](void* ctx, Vector3 origin, Vector3 direction, float maxDistance, uint32_t layerMask, uint32_t ignoreId, ScriptRaycastHit* outHit) -> bool {
		if (!ctx || !outHit) return false;
		return static_cast<EditorSuite*>(ctx)->ScriptRaycast(origin, direction, maxDistance, layerMask, ignoreId, *outHit);
	};
	engineApi_.overlapSphere = [](void* ctx, Vector3 center, float radius, uint32_t layerMask, uint32_t* outIds, int maxCount) -> int {
		return ctx ? static_cast<EditorSuite*>(ctx)->ScriptOverlapSphere(center, radius, layerMask, outIds, maxCount) : 0;
	};
	engineApi_.findWithTag = [](void* ctx, const char* tag) -> ScriptGameObjectView* {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || !tag) return nullptr;
		const auto excluded = CollectQueryExclusions(self->gameObjects_, self->pendingDeletes_, self->pendingDespawns_);
		for (const auto& obj : self->gameObjects_) {
			if (obj.isActive && obj.tag == tag && excluded.count(obj.id) == 0) {
				return self->GetOrUpdateScriptViewById(obj.id);
			}
		}
		return nullptr;
	};
	engineApi_.findAllWithTag = [](void* ctx, const char* tag, uint32_t* outIds, int maxCount) -> int {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || !tag || !outIds || maxCount <= 0) return 0;
		const auto excluded = CollectQueryExclusions(self->gameObjects_, self->pendingDeletes_, self->pendingDespawns_);
		int count = 0;
		for (const auto& obj : self->gameObjects_) {
			if (count >= maxCount) break;
			if (obj.isActive && obj.tag == tag && excluded.count(obj.id) == 0) outIds[count++] = obj.id;
		}
		return count;
	};
	engineApi_.findNearest = [](void* ctx, Vector3 position, const char* tag, float maxDistance, uint32_t ignoreId) -> ScriptGameObjectView* {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self) return nullptr;
		const GameObject* nearest = self->FindNearestObject(position, tag, maxDistance, ignoreId);
		return nearest ? self->GetOrUpdateScriptViewById(nearest->id) : nullptr;
	};

	// ---- Hierarchy ----
	engineApi_.setParent = [](void* ctx, uint32_t childId, uint32_t parentId, bool keepWorldTransform) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || childId == 0 || childId == parentId) return false;
		const bool childKnown = self->FindGameObjectById(childId, false) || self->FindPendingSpawnView(childId);
		const bool parentKnown = parentId == 0 || self->FindGameObjectById(parentId, false) || self->FindPendingSpawnView(parentId);
		if (!childKnown || !parentKnown) return false;
		self->pendingReparents_.push_back({ childId, parentId == 0 ? -1 : static_cast<int>(parentId), keepWorldTransform });
		return true;
	};
	engineApi_.getParent = [](void* ctx, uint32_t id) -> uint32_t {
		const GameObject* obj = ctx ? static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false) : nullptr;
		return (obj && obj->parentId >= 0) ? static_cast<uint32_t>(obj->parentId) : 0u;
	};
	engineApi_.getChildren = [](void* ctx, uint32_t id, uint32_t* outIds, int maxCount) -> int {
		const GameObject* obj = ctx ? static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false) : nullptr;
		if (!obj || !outIds || maxCount <= 0) return 0;
		const int count = (std::min)(maxCount, static_cast<int>(obj->childrenIds.size()));
		for (int i = 0; i < count; ++i) outIds[i] = obj->childrenIds[static_cast<size_t>(i)];
		return count;
	};
	engineApi_.getWorldPosition = [](void* ctx, uint32_t id, Vector3* out) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || !out) return false;
		if (const GameObject* obj = self->FindGameObjectById(id, false)) {
			*out = self->GetWorldPositionOf(*obj);
			return true;
		}
		if (const ScriptGameObjectView* pending = self->FindPendingSpawnView(id)) {
			*out = pending->translation; // spawned objects start as roots
			return true;
		}
		return false;
	};

	// ---- Components ----
	engineApi_.getLight = [](void* ctx, uint32_t id, ScriptLightData* out) -> bool {
		const GameObject* obj = ctx ? static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false) : nullptr;
		if (!obj || !obj->hasLight || !out) return false;
		out->enabled = obj->lightEnabled;
		out->type = static_cast<int>(obj->lightType);
		out->color = obj->lightColor;
		out->intensity = obj->lightIntensity;
		out->range = obj->lightRange;
		out->spotAngle = obj->lightSpotAngle;
		return true;
	};
	engineApi_.setLight = [](void* ctx, uint32_t id, const ScriptLightData* in) -> bool {
		GameObject* obj = ctx ? static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false) : nullptr;
		if (!obj || !obj->hasLight || !in) return false;
		obj->lightEnabled = in->enabled;
		obj->lightType = static_cast<GameObject::LightType>(std::clamp(in->type, 0, 2));
		obj->lightColor = in->color;
		obj->lightIntensity = (std::max)(0.0f, in->intensity);
		obj->lightRange = (std::max)(0.0f, in->range);
		obj->lightSpotAngle = std::clamp(in->spotAngle, 1.0f, 179.0f);
		return true;
	};
	engineApi_.getRigidbody = [](void* ctx, uint32_t id, ScriptRigidbodyData* out) -> bool {
		const GameObject* obj = ctx ? static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false) : nullptr;
		if (!obj || !obj->hasRigidbody || !out) return false;
		out->enabled = obj->rbEnabled;
		out->mass = obj->mass;
		out->drag = obj->drag;
		out->gravityScale = obj->gravityScale;
		out->useGravity = obj->useGravity;
		out->isKinematic = obj->isKinematic;
		out->bounciness = obj->bounciness;
		return true;
	};
	engineApi_.setRigidbody = [](void* ctx, uint32_t id, const ScriptRigidbodyData* in) -> bool {
		GameObject* obj = ctx ? static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false) : nullptr;
		if (!obj || !obj->hasRigidbody || !in) return false;
		obj->rbEnabled = in->enabled;
		obj->mass = (std::max)(0.0001f, in->mass);
		obj->drag = (std::max)(0.0f, in->drag);
		obj->gravityScale = in->gravityScale;
		obj->useGravity = in->useGravity;
		obj->isKinematic = in->isKinematic;
		obj->bounciness = std::clamp(in->bounciness, 0.0f, 1.0f);
		return true;
	};
	engineApi_.getAnimation = [](void* ctx, uint32_t id, ScriptAnimationData* out) -> bool {
		const GameObject* obj = ctx ? static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false) : nullptr;
		if (!obj || !obj->hasMesh || !out) return false;
		out->clipIndex = obj->animationIndex;
		out->speed = obj->animationSpeed;
		out->loop = obj->animationLoop;
		out->playing = obj->animationPlaying;
		return true;
	};
	engineApi_.setAnimation = [](void* ctx, uint32_t id, const ScriptAnimationData* in) -> bool {
		GameObject* obj = ctx ? static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false) : nullptr;
		if (!obj || !obj->hasMesh || !in) return false;
		obj->animationIndex = (std::max)(0, in->clipIndex);
		obj->animationSpeed = std::isfinite(in->speed) ? in->speed : 1.0f;
		obj->animationLoop = in->loop;
		obj->animationPlaying = in->playing;
		return true;
	};
	// Animator (API v4)
	engineApi_.animSetParam = [](void* ctx, uint32_t id, const char* name, float value) -> bool {
		std::shared_ptr<AnimatorController> c;
		AnimatorComponent* a = name ? ScriptAnimator(ctx, id, c) : nullptr;
		return a && std::isfinite(value) && a->runtime.SetValue(*c, name, value);
	};
	engineApi_.animGetParam = [](void* ctx, uint32_t id, const char* name) -> float {
		std::shared_ptr<AnimatorController> c;
		AnimatorComponent* a = name ? ScriptAnimator(ctx, id, c) : nullptr;
		return a ? a->runtime.GetValue(name) : 0.0f;
	};
	engineApi_.animResetTrigger = [](void* ctx, uint32_t id, const char* name) -> bool {
		std::shared_ptr<AnimatorController> c;
		AnimatorComponent* a = name ? ScriptAnimator(ctx, id, c) : nullptr;
		if (!a || !c->FindParameter(name)) return false;
		a->runtime.ResetTrigger(name);
		return true;
	};
	engineApi_.animIsState = [](void* ctx, uint32_t id, const char* stateName) -> bool {
		std::shared_ptr<AnimatorController> c;
		AnimatorComponent* a = stateName ? ScriptAnimator(ctx, id, c) : nullptr;
		const AnimState* s = a ? c->FindState(a->runtime.currentState) : nullptr;
		return s && s->name == stateName;
	};
	engineApi_.animGetState = [](void* ctx, uint32_t id, char* outName, int outSize) -> int {
		std::shared_ptr<AnimatorController> c;
		AnimatorComponent* a = ScriptAnimator(ctx, id, c);
		if (!a) return -1;
		const AnimState* s = c->FindState(a->runtime.currentState);
		const std::string name = s ? s->name : std::string();
		if (outName && outSize > 0) strncpy_s(outName, static_cast<size_t>(outSize), name.c_str(), _TRUNCATE);
		return static_cast<int>(name.size());
	};
	engineApi_.animCrossFade = [](void* ctx, uint32_t id, const char* stateName, float seconds) -> bool {
		std::shared_ptr<AnimatorController> c;
		AnimatorComponent* a = stateName ? ScriptAnimator(ctx, id, c) : nullptr;
		if (!a) return false;
		const AnimatorRuntime::Change change = a->runtime.ForceState(*c, stateName, std::isfinite(seconds) ? seconds : 0.0f);
		const AnimState* s = change.changed ? c->FindState(change.state) : nullptr;
		GameObject* obj = static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false);
		if (!s || !obj) return false;
		static_cast<EditorSuite*>(ctx)->ApplyAnimatorState(*obj, *a, *s, change.fadeSeconds);
		return true;
	};
	engineApi_.animGetNormalizedTime = [](void* ctx, uint32_t id) -> float {
		GameObject* obj = ctx ? static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false) : nullptr;
		if (!obj || !obj->editObject3d || !obj->editObject3d->GetModel()) return 0.0f;
		return obj->editObject3d->GetAnimationPlayer().NormalizedTime(obj->editObject3d->GetModel());
	};
	engineApi_.animIsInTransition = [](void* ctx, uint32_t id) -> bool {
		std::shared_ptr<AnimatorController> c;
		AnimatorComponent* a = ScriptAnimator(ctx, id, c);
		return a && a->runtime.InTransition();
	};
	// Input actions (API v5): the project's map (ProjectSettings/input.json or the defaults)
	engineApi_.isActionDown = [](void* ctx, const char* action) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		return self && action && self->GetInputActions().IsDown(action, self->InputSource());
	};
	engineApi_.isActionTriggered = [](void* ctx, const char* action) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		return self && action && self->GetInputActions().IsTriggered(action, self->InputSource());
	};
	engineApi_.isActionReleased = [](void* ctx, const char* action) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		return self && action && self->GetInputActions().IsReleased(action, self->InputSource());
	};
	engineApi_.getAxis = [](void* ctx, const char* action) -> float {
		auto* self = static_cast<EditorSuite*>(ctx);
		return (self && action) ? self->GetInputActions().GetAxis(action, self->InputSource()) : 0.0f;
	};
	// Game feel (API v5)
	engineApi_.shakeCamera = [](void* ctx, float strength, float seconds) {
		if (ctx) static_cast<EditorSuite*>(ctx)->ShakeCamera(strength, seconds);
	};
	engineApi_.hitstop = [](void* ctx, float seconds) {
		if (ctx) static_cast<EditorSuite*>(ctx)->Hitstop(seconds);
	};
	engineApi_.animPlayLayer = [](void* ctx, uint32_t id, int layer, const char* clip, const char* maskBone, bool loop, float speed, float fadeSeconds) -> bool {
		return ctx && static_cast<EditorSuite*>(ctx)->ScriptPlayAnimationLayer(id, layer, clip, maskBone, loop, speed, fadeSeconds);
	};
	engineApi_.animStopLayer = [](void* ctx, uint32_t id, int layer, float fadeSeconds) -> bool {
		return ctx && static_cast<EditorSuite*>(ctx)->ScriptStopAnimationLayer(id, layer, fadeSeconds);
	};
	engineApi_.getBoneWorldPosition = [](void* ctx, uint32_t id, const char* bone, Vector3* out) -> bool {
		return ctx && out && static_cast<EditorSuite*>(ctx)->ScriptGetBoneWorldPosition(id, bone, *out);
	};
	engineApi_.setBoneIK = [](void* ctx, uint32_t id, const char* endBone, Vector3 target, Vector3 pole, float weight) -> bool {
		return ctx && static_cast<EditorSuite*>(ctx)->ScriptSetBoneIK(id, endBone, target, pole, weight);
	};
	engineApi_.attachToBone = [](void* ctx, uint32_t childId, uint32_t parentId, const char* bone) -> bool {
		return ctx && static_cast<EditorSuite*>(ctx)->ScriptAttachToBone(childId, parentId, bone);
	};
	engineApi_.setRagdoll = [](void* ctx, uint32_t id, bool enabled) -> bool {
		return ctx && static_cast<EditorSuite*>(ctx)->ScriptSetRagdoll(id, enabled);
	};
	engineApi_.moveCharacter = [](void* ctx, uint32_t id, Vector3 motion, float stepHeight, ScriptCharacterMove* out) -> bool {
		return ctx && out && static_cast<EditorSuite*>(ctx)->ScriptMoveCharacter(id, motion, stepHeight, *out);
	};
	engineApi_.findPath = [](void* ctx, Vector3 from, Vector3 to, Vector3* outPoints, int maxPoints) -> int {
		return ctx ? static_cast<EditorSuite*>(ctx)->ScriptFindPath(from, to, outPoints, maxPoints) : 0;
	};
	engineApi_.rebuildNavigation = [](void* ctx, float cellSize, float agentRadius) {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self) return;
		self->navCellSize_ = (std::max)(cellSize, 0.05f);
		self->navAgentRadius_ = (std::max)(agentRadius, 0.0f);
		self->navGridValid_ = false; // rebuilt at the next FindPath, for that agent's floor
	};
	engineApi_.getCamera = [](void* ctx, uint32_t id, ScriptCameraData* out) -> bool {
		const GameObject* obj = ctx ? static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false) : nullptr;
		if (!obj || !(obj->hasCamera || obj->type == GameObjectType::Camera) || !out) return false;
		out->fov = obj->fov;
		out->nearClip = obj->nearClip;
		out->farClip = obj->farClip;
		out->orthographic = obj->isOrthographic;
		return true;
	};
	engineApi_.setCamera = [](void* ctx, uint32_t id, const ScriptCameraData* in) -> bool {
		GameObject* obj = ctx ? static_cast<EditorSuite*>(ctx)->FindGameObjectById(id, false) : nullptr;
		if (!obj || !(obj->hasCamera || obj->type == GameObjectType::Camera) || !in) return false;
		obj->fov = std::clamp(in->fov, 1.0f, 179.0f);
		obj->nearClip = (std::max)(0.001f, in->nearClip);
		obj->farClip = (std::max)(obj->nearClip + 0.01f, in->farClip);
		obj->isOrthographic = in->orthographic;
		return true;
	};
}

// ---------------------------------------------------------------------------
// Game feel
// ---------------------------------------------------------------------------
void EditorSuite::ShakeCamera(float strength, float seconds)
{
	if (!std::isfinite(strength) || !std::isfinite(seconds) || strength <= 0.0f || seconds <= 0.0f) return;
	if (strength < CameraShakeStrength()) return; // a weak hit never cuts a strong shake short
	shakeStrength_ = strength;
	shakeDuration_ = seconds;
	shakeLeft_ = seconds;
}

void EditorSuite::Hitstop(float seconds)
{
	if (!std::isfinite(seconds) || seconds <= 0.0f) return;
	hitstopLeft_ = (std::max)(hitstopLeft_, seconds);
}

Vector3 EditorSuite::CameraShakeOffset() const
{
	const float a = CameraShakeStrength();
	if (a <= 0.0f) return { 0.0f, 0.0f, 0.0f };
	// Incommensurate sines: jittery like noise but deterministic (tests, replays)
	const float t = shakeTime_;
	return { a * std::sin(t * 47.0f), a * std::sin(t * 61.0f + 1.3f), 0.5f * a * std::sin(t * 53.0f + 2.1f) };
}
