#include "EditorSuiteInternal.h"
#include "SceneJson.h"

void EditorSuite::UpdateScripts(float deltaTime)
{
	RestoreSimulatedPoses(); // scripts see the simulated pose, not the blended one
	physicsQueryDirty_ = true;
	if (sceneState_ == EditorSceneState::Edit && !IsStandaloneMode()) {
		return;
	}

	// Scripts rebuild the game UI every frame they run (the last frame stays visible while paused)
	UIRenderer::GetInstance()->Clear();
	// DespawnAfter() timers run on game time (paused with timeScale 0)
	TickScriptTimers(deltaTime);
	RefreshAllScriptObjectViews();

	// Instantiate/Destroy from scripts are deferred, so gameObjects_ does not change size inside this loop.
	// Objects are still re-fetched by index after each callback instead of holding references across it.
	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		if (!gameObjects_[i].isActive || gameObjects_[i].scripts.empty()) continue;

		// Skip objects already queued for deferred deletion (pendingDeletes_ stores indices)
		if (std::find(pendingDeletes_.begin(), pendingDeletes_.end(), static_cast<int>(i)) != pendingDeletes_.end()) {
			continue;
		}

		const uint32_t objId = gameObjects_[i].id;
		for (size_t s = 0; s < gameObjects_[i].scripts.size(); ++s) {
			ScriptBase* instance = gameObjects_[i].scripts[s].instance.get();
			if (instance) {
				ScriptGameObjectView* view = GetOrUpdateScriptViewById(objId);
				Vector3 prevEuler = view ? view->rotation : gameObjects_[i].rotation;
				Quaternion prevQuat = view ? view->rotationQuat : gameObjects_[i].rotationQuat;

				instance->SetOwner(view, objId);
				instance->SetEngineAPI(&engineApi_);
				instance->SetHost(this, this);
				CallScript(instance, objId, "Update", [&] { instance->Update(deltaTime); });

				if (view) {
					bool eulerChanged = (view->rotation.x != prevEuler.x || view->rotation.y != prevEuler.y || view->rotation.z != prevEuler.z);
					bool quatChanged = (view->rotationQuat.x != prevQuat.x || view->rotationQuat.y != prevQuat.y || view->rotationQuat.z != prevQuat.z || view->rotationQuat.w != prevQuat.w);
					if (eulerChanged && !quatChanged) {
						view->rotationQuat = Quaternion::FromEuler(view->rotation);
					} else if (quatChanged && !eulerChanged) {
						view->rotation = view->rotationQuat.ToEuler();
					}
					SyncViewToGameObject(*view, gameObjects_[i]);
				}
			}
		}
	}

	// Create objects instantiated during Update() now that no script callback is running
	ProcessPendingSpawns();
}

void EditorSuite::DispatchScriptCollision(uint32_t selfId, uint32_t otherId, CollisionEvent kind)
{
	if (sceneState_ == EditorSceneState::Edit && !IsStandaloneMode()) {
		return;
	}

	GameObject* selfObj = FindGameObjectById(selfId, false);
	GameObject* otherObj = FindGameObjectById(otherId, false);
	if (!selfObj || !selfObj->isActive || selfObj->scripts.empty()) {
		return;
	}
	// Exit is also reported when the other object was deactivated, despawned or destroyed
	if (kind != CollisionEvent::Exit && (!otherObj || !otherObj->isActive)) {
		return;
	}

	ScriptGameObjectView* selfView = GetOrUpdateScriptViewById(selfId);
	ScriptGameObjectView* otherView = otherObj ? GetOrUpdateScriptViewById(otherId) : nullptr;
	ScriptGameObjectView goneView{};
	if (!otherView) {
		goneView.id = otherId;
		goneView.isActive = false;
		otherView = &goneView;
	}
	if (!selfView) {
		return;
	}

	for (size_t s = 0; s < selfObj->scripts.size(); ++s) {
		auto& sc = selfObj->scripts[s];
		if (sc.instance) {
			Vector3 prevEuler = selfView->rotation;
			Quaternion prevQuat = selfView->rotationQuat;

			sc.instance->SetOwner(selfView, selfObj->id);
			sc.instance->SetEngineAPI(&engineApi_);
			sc.instance->SetHost(this, this);
			// Inside the DLL GameObject is an alias of ScriptGameObjectView, so the DLL receives the view.
			// Never pass an editor GameObject here.
			GameObject& otherRef = *reinterpret_cast<GameObject*>(otherView);
			ScriptBase* instance = sc.instance.get();
			CallScript(instance, selfId, "OnCollision", [&] {
				switch (kind) {
				case CollisionEvent::Enter: instance->OnCollisionEnter(otherRef); break;
				case CollisionEvent::Exit: instance->OnCollisionExit(otherRef); break;
				default: instance->OnCollision(otherRef); break;
				}
			});

			// Callbacks never resize gameObjects_ (spawns/deletes are deferred), but re-fetch anyway
			selfObj = FindGameObjectById(selfId, false);
			otherObj = FindGameObjectById(otherId, false);
			if (!selfObj) return;

			bool eulerChanged = (selfView->rotation.x != prevEuler.x || selfView->rotation.y != prevEuler.y || selfView->rotation.z != prevEuler.z);
			bool quatChanged = (selfView->rotationQuat.x != prevQuat.x || selfView->rotationQuat.y != prevQuat.y || selfView->rotationQuat.z != prevQuat.z || selfView->rotationQuat.w != prevQuat.w);
			if (eulerChanged && !quatChanged) {
				selfView->rotationQuat = Quaternion::FromEuler(selfView->rotation);
			} else if (quatChanged && !eulerChanged) {
				selfView->rotation = selfView->rotationQuat.ToEuler();
			}
			SyncViewToGameObject(*selfView, *selfObj);
			if (otherObj && otherView != &goneView) {
				SyncViewToGameObject(*otherView, *otherObj);
			}
		}
	}
}

void EditorSuite::SetupEngineAPI()
{
	engineApi_.hostContext = this;
	engineApi_.log = [](void* ctx, const char* msg, int logType) {
		if (ctx && msg) {
			static_cast<EditorSuite*>(ctx)->LogMessage(msg, logType);
		}
	};
	engineApi_.findObjectByName = [](void* ctx, const char* name, bool warnIfMissing) -> ScriptGameObjectView* {
		if (!ctx || !name) return nullptr;
		auto* self = static_cast<EditorSuite*>(ctx);
		if (GameObject* found = self->FindGameObjectByName(name, false)) {
			return self->GetOrUpdateScriptViewById(found->id);
		}
		for (const auto& spawn : self->pendingSpawns_) {
			if (spawn.name == name) {
				return self->FindPendingSpawnView(spawn.id);
			}
		}
		if (warnIfMissing) {
			self->FindGameObjectByName(name, true); // logs the missing-object warning
		}
		return nullptr;
	};
	engineApi_.findObjectById = [](void* ctx, uint32_t id, bool warnIfMissing) -> ScriptGameObjectView* {
		if (!ctx || id == 0) return nullptr;
		auto* self = static_cast<EditorSuite*>(ctx);
		if (ScriptGameObjectView* view = self->GetOrUpdateScriptViewById(id)) {
			return view;
		}
		if (warnIfMissing) {
			self->FindGameObjectById(id, true); // logs the missing-object warning
		}
		return nullptr;
	};
	engineApi_.getTransform = [](void* ctx, uint32_t id, ScriptTransformData* outTransform) -> bool {
		if (!ctx || id == 0 || !outTransform) return false;
		auto* self = static_cast<EditorSuite*>(ctx);
		GameObject* found = self->FindGameObjectById(id, false);
		if (!found) {
			const ScriptGameObjectView* pending = self->FindPendingSpawnView(id);
			if (!pending) return false;
			outTransform->translation = pending->translation;
			outTransform->rotation = pending->rotation;
			outTransform->rotationQuat = pending->rotationQuat;
			outTransform->scale = pending->scale;
			return true;
		}
		outTransform->translation = found->translation;
		outTransform->rotation = found->rotation;
		outTransform->rotationQuat = found->rotationQuat;
		outTransform->scale = found->scale;
		return true;
	};
	engineApi_.setTransform = [](void* ctx, uint32_t id, const ScriptTransformData* inTransform) -> bool {
		if (!ctx || id == 0 || !inTransform) return false;
		auto* self = static_cast<EditorSuite*>(ctx);
		GameObject* found = self->FindGameObjectById(id, false);
		if (!found) {
			// Not created yet: ProcessPendingSpawns() applies the view transform on creation
			ScriptGameObjectView* pending = self->FindPendingSpawnView(id);
			if (!pending) return false;
			pending->translation = inTransform->translation;
			pending->rotation = inTransform->rotation;
			pending->rotationQuat = inTransform->rotationQuat;
			pending->scale = inTransform->scale;
			return true;
		}
		found->translation = inTransform->translation;
		found->rotation = inTransform->rotation;
		found->rotationQuat = inTransform->rotationQuat;
		found->scale = inTransform->scale;
		if (ScriptGameObjectView* view = self->GetOrUpdateScriptViewById(id)) {
			view->translation = inTransform->translation;
			view->rotation = inTransform->rotation;
			view->rotationQuat = inTransform->rotationQuat;
			view->scale = inTransform->scale;
		}
		return true;
	};
	engineApi_.deleteObject = [](void* ctx, uint32_t id) {
		if (ctx && id != 0) {
			auto* self = static_cast<EditorSuite*>(ctx);
			self->CancelDelayedDespawn(id);
			// An object instantiated this frame has not been created yet: just drop the request
			if (!self->CancelPendingSpawn(id)) {
				self->QueueDeleteGameObjectById(id);
			}
		}
	};
	engineApi_.isKeyDown = [](void* ctx, int keyCode) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		return (self && self->currentInput_) ? self->currentInput_->PushKey(static_cast<BYTE>(keyCode & 0xFF)) : false;
	};
	engineApi_.isKeyTriggered = [](void* ctx, int keyCode) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		return (self && self->currentInput_) ? self->currentInput_->TriggerKey(static_cast<BYTE>(keyCode & 0xFF)) : false;
	};
	engineApi_.getDeltaTime = [](void* ctx) -> float {
		auto* self = static_cast<EditorSuite*>(ctx);
		return self ? self->currentDeltaTime_ : 0.016f;
	};
	// Deferred: the GameObject is created by ProcessPendingSpawns() after the current script callback returns.
	// The returned view is valid immediately and keeps its address after the object is created.
	engineApi_.instantiate = [](void* ctx, const char* name, int primitiveType, Vector3 position) -> ScriptGameObjectView* {
		if (!ctx) return nullptr;
		return static_cast<EditorSuite*>(ctx)->QueueScriptSpawn(name, primitiveType, position);
	};

	// Mouse buttons only count while the cursor is over the Game view (clicks on editor panels are ignored)
	engineApi_.isMouseDown = [](void* ctx, int button) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
		return self && self->currentInput_ && self->GetGameViewMousePosition(x, y, w, h) && self->currentInput_->IsPressMouse(button);
	};
	engineApi_.isMouseTriggered = [](void* ctx, int button) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
		return self && self->currentInput_ && self->GetGameViewMousePosition(x, y, w, h) && self->currentInput_->IsTriggerMouse(button);
	};
	engineApi_.getMousePosition = [](void* ctx, Vector2* outPixels, Vector2* outViewSize) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
		const bool inside = self && self->GetGameViewMousePosition(x, y, w, h);
		if (outPixels) *outPixels = { x, y };
		if (outViewSize) *outViewSize = { w, h };
		return inside;
	};
	engineApi_.getMouseWorldOnPlane = [](void* ctx, float planeY, Vector3* outPoint) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		Vector3 p{};
		if (!self || !outPoint || !self->GetMouseWorldOnPlane(planeY, p)) return false;
		*outPoint = p;
		return true;
	};

	engineApi_.isPadConnected = [](void* ctx, int pad) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		return self && self->currentInput_ && self->currentInput_->IsGamepadConnected(pad);
	};
	engineApi_.isPadButtonDown = [](void* ctx, int button, int pad) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		return self && self->currentInput_ && self->currentInput_->PushGamepadButton(static_cast<WORD>(button & 0xFFFF), pad);
	};
	engineApi_.isPadButtonTriggered = [](void* ctx, int button, int pad) -> bool {
		auto* self = static_cast<EditorSuite*>(ctx);
		return self && self->currentInput_ && self->currentInput_->TriggerGamepadButton(static_cast<WORD>(button & 0xFFFF), pad);
	};
	engineApi_.getPadStick = [](void* ctx, int stick, int pad) -> Vector2 {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || !self->currentInput_) return {};
		Input* in = self->currentInput_;
		return (stick == 0) ? Vector2{ in->GetLeftStickX(pad), in->GetLeftStickY(pad) }
			: Vector2{ in->GetRightStickX(pad), in->GetRightStickY(pad) };
	};
	engineApi_.getPadTrigger = [](void* ctx, int side, int pad) -> float {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || !self->currentInput_) return 0.0f;
		return (side == 0) ? self->currentInput_->GetLeftTrigger(pad) : self->currentInput_->GetRightTrigger(pad);
	};
	engineApi_.setPadVibration = [](void* ctx, int pad, float left, float right) {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (self && self->currentInput_) self->currentInput_->SetVibration(pad, left, right);
	};

	engineApi_.getUnscaledDeltaTime = [](void* ctx) -> float {
		auto* self = static_cast<EditorSuite*>(ctx);
		return self ? self->unscaledDeltaTime_ : 0.016f;
	};
	engineApi_.getTime = [](void* ctx) -> float {
		auto* self = static_cast<EditorSuite*>(ctx);
		return self ? static_cast<float>(self->gameTime_) : 0.0f;
	};
	engineApi_.getTimeScale = [](void* ctx) -> float {
		auto* self = static_cast<EditorSuite*>(ctx);
		return self ? self->timeScale_ : 1.0f;
	};
	engineApi_.setTimeScale = [](void* ctx, float scale) {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (self) self->timeScale_ = std::isfinite(scale) ? std::clamp(scale, 0.0f, 10.0f) : 1.0f;
	};

	engineApi_.instantiatePrefab = [](void* ctx, const char* prefabName, Vector3 position) -> ScriptGameObjectView* {
		return ctx ? static_cast<EditorSuite*>(ctx)->QueuePrefabSpawn(prefabName, position) : nullptr;
	};
	engineApi_.despawn = [](void* ctx, uint32_t id) {
		if (ctx) static_cast<EditorSuite*>(ctx)->QueueDespawn(id);
	};

	engineApi_.uiRect = [](void*, float x, float y, float w, float h, Vector4 color) {
		UIRenderer::GetInstance()->DrawRect(x, y, w, h, color);
	};
	engineApi_.uiText = [](void*, const char* text, float x, float y, float size, Vector4 color, int align) {
		UIRenderer::GetInstance()->DrawString(text, x, y, size, color, static_cast<UIRenderer::Align>(std::clamp(align, 0, 2)));
	};
	engineApi_.uiImage = [](void* ctx, const char* texturePath, float x, float y, float w, float h, Vector4 color) {
		auto* self = static_cast<EditorSuite*>(ctx);
		if (!self || !texturePath || texturePath[0] == '\0') return;
		// LoadTexture caches by path, so only the first call per texture touches the disk
		const uint32_t texture = TextureManager::GetInstance()->LoadTexture(self->ResolveAssetPath(texturePath));
		UIRenderer::GetInstance()->DrawImage(texture, x, y, w, h, color);
	};
	engineApi_.uiMeasureText = [](void*, const char* text, float size) -> float {
		return UIRenderer::GetInstance()->MeasureText(text, size);
	};

	engineApi_.loadScene = [](void* ctx, const char* sceneNameOrPath) {
		if (ctx && sceneNameOrPath) static_cast<EditorSuite*>(ctx)->RequestSceneLoad(sceneNameOrPath);
	};
	engineApi_.keepAcrossScenes = [](void* ctx, uint32_t id) -> bool {
		return ctx && static_cast<EditorSuite*>(ctx)->KeepAcrossScenes(id);
	};
	engineApi_.getSceneName = [](void* ctx, char* out, int outSize) -> int {
		if (!ctx || !out || outSize <= 0) return 0;
		const std::string& name = static_cast<EditorSuite*>(ctx)->currentSceneName_;
		strncpy_s(out, static_cast<size_t>(outSize), name.c_str(), _TRUNCATE);
		return static_cast<int>(strlen(out));
	};
	engineApi_.saveSetNumber = [](void* ctx, const char* key, double value) {
		if (ctx && key) static_cast<EditorSuite*>(ctx)->SetSaveNumber(key, value);
	};
	engineApi_.saveGetNumber = [](void* ctx, const char* key, double fallback) -> double {
		return (ctx && key) ? static_cast<EditorSuite*>(ctx)->GetSaveNumber(key, fallback) : fallback;
	};
	engineApi_.saveSetString = [](void* ctx, const char* key, const char* value) {
		if (ctx && key && value) static_cast<EditorSuite*>(ctx)->SetSaveString(key, value);
	};
	engineApi_.saveGetString = [](void* ctx, const char* key, char* out, int outSize) -> bool {
		if (!ctx || !key || !out || outSize <= 0) return false;
		std::string value;
		if (!static_cast<EditorSuite*>(ctx)->GetSaveString(key, value)) return false;
		strncpy_s(out, static_cast<size_t>(outSize), value.c_str(), _TRUNCATE);
		return true;
	};
	engineApi_.saveHasKey = [](void* ctx, const char* key) -> bool {
		return ctx && key && static_cast<EditorSuite*>(ctx)->HasSaveKey(key);
	};
	engineApi_.saveDeleteAll = [](void* ctx) {
		if (ctx) static_cast<EditorSuite*>(ctx)->DeleteAllSaveData();
	};

	// Audio, particles, material, queries, hierarchy and component access (EditorSuite_ScriptQueries.cpp)
	SetupEngineAPIExtensions();
}

bool EditorSuite::GetGameViewMousePosition(float& outX, float& outY, float& outW, float& outH) const
{
	float rect[4] = { gameViewRect_[0], gameViewRect_[1], gameViewRect_[2], gameViewRect_[3] };
	float mouseX = 0.0f;
	float mouseY = 0.0f;
#ifdef USE_IMGUI
	if (ImGui::GetCurrentContext() == nullptr) return false;
	const ImVec2 mp = ImGui::GetIO().MousePos;
	mouseX = mp.x;
	mouseY = mp.y;
#else
	// Release has no ImGui: the game fills the client area
	HWND hwnd = WinApp::GetInstance()->GetHWND();
	POINT pt{};
	RECT client{};
	if (!hwnd || !GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt) || !GetClientRect(hwnd, &client)) return false;
	rect[0] = 0.0f;
	rect[1] = 0.0f;
	rect[2] = static_cast<float>(client.right - client.left);
	rect[3] = static_cast<float>(client.bottom - client.top);
	mouseX = static_cast<float>(pt.x);
	mouseY = static_cast<float>(pt.y);
#endif
	if (rect[2] <= 0.0f || rect[3] <= 0.0f) return false;
	outX = mouseX - rect[0];
	outY = mouseY - rect[1];
	outW = rect[2];
	outH = rect[3];
	return outX >= 0.0f && outY >= 0.0f && outX < outW && outY < outH;
}

// Row-vector convention, so p * inverse(VP)
static Vector3 UnprojectNdc(const Matrix4x4& inv, float x, float y, float z)
{
	const float px = x * inv.m[0][0] + y * inv.m[1][0] + z * inv.m[2][0] + inv.m[3][0];
	const float py = x * inv.m[0][1] + y * inv.m[1][1] + z * inv.m[2][1] + inv.m[3][1];
	const float pz = x * inv.m[0][2] + y * inv.m[1][2] + z * inv.m[2][2] + inv.m[3][2];
	const float pw = x * inv.m[0][3] + y * inv.m[1][3] + z * inv.m[2][3] + inv.m[3][3];
	const float invW = (std::abs(pw) > 1e-12f) ? 1.0f / pw : 0.0f;
	return Vector3{ px * invW, py * invW, pz * invW };
}

bool EditorSuite::ScreenPointToRay(const Vector2& canvasPos, Vector3& outOrigin, Vector3& outDirection) const
{
	if (!gameCamera_) return false;
	const float ndcX = canvasPos.x / kUICanvasWidth * 2.0f - 1.0f;
	const float ndcY = 1.0f - canvasPos.y / kUICanvasHeight * 2.0f;
	const Matrix4x4 inv = Mathematics::Inverse(gameCamera_->GetViewProjectionMatrix());
	const Vector3 nearP = UnprojectNdc(inv, ndcX, ndcY, 0.0f);
	const Vector3 farP = UnprojectNdc(inv, ndcX, ndcY, 1.0f);
	const Vector3 d = { farP.x - nearP.x, farP.y - nearP.y, farP.z - nearP.z };
	const float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
	if (length < 1e-8f) return false;
	outOrigin = nearP;
	outDirection = { d.x / length, d.y / length, d.z / length };
	return true;
}

bool EditorSuite::WorldToCanvas(const Vector3& worldPos, Vector2& outCanvasPos) const
{
	if (!gameCamera_) return false;
	const Matrix4x4 vp = gameCamera_->GetViewProjectionMatrix();
	const float cx = worldPos.x * vp.m[0][0] + worldPos.y * vp.m[1][0] + worldPos.z * vp.m[2][0] + vp.m[3][0];
	const float cy = worldPos.x * vp.m[0][1] + worldPos.y * vp.m[1][1] + worldPos.z * vp.m[2][1] + vp.m[3][1];
	const float cw = worldPos.x * vp.m[0][3] + worldPos.y * vp.m[1][3] + worldPos.z * vp.m[2][3] + vp.m[3][3];
	if (cw <= 1e-6f) return false; // behind the camera
	outCanvasPos = { (cx / cw + 1.0f) * 0.5f * kUICanvasWidth, (1.0f - cy / cw) * 0.5f * kUICanvasHeight };
	return true;
}

bool EditorSuite::ScreenRayToPlaneY(const Matrix4x4& viewProjection, float ndcX, float ndcY, float planeY, Vector3& outPoint)
{
	// Unproject the near (z = 0) and far (z = 1) points
	const Matrix4x4 inv = Mathematics::Inverse(viewProjection);
	const Vector3 nearP = UnprojectNdc(inv, ndcX, ndcY, 0.0f);
	const Vector3 farP = UnprojectNdc(inv, ndcX, ndcY, 1.0f);
	const float dirY = farP.y - nearP.y;
	if (std::abs(dirY) < 1e-8f) return false; // ray parallel to the plane
	const float t = (planeY - nearP.y) / dirY;
	if (t < 0.0f || !std::isfinite(t)) return false; // plane is behind the camera
	outPoint = { nearP.x + (farP.x - nearP.x) * t, planeY, nearP.z + (farP.z - nearP.z) * t };
	return true;
}

bool EditorSuite::GetMouseWorldOnPlane(float planeY, Vector3& outPoint) const
{
	float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
	if (!gameCamera_ || !GetGameViewMousePosition(x, y, w, h)) return false;
	const float ndcX = (x / w) * 2.0f - 1.0f;
	const float ndcY = 1.0f - (y / h) * 2.0f;
	return ScreenRayToPlaneY(gameCamera_->GetViewProjectionMatrix(), ndcX, ndcY, planeY, outPoint);
}

void EditorSuite::SyncGameObjectToView(const GameObject& src, ScriptGameObjectView& dst)
{
	dst.id = src.id;
	strncpy_s(dst.name, src.name.c_str(), sizeof(dst.name) - 1);
	strncpy_s(dst.tag, src.tag.c_str(), sizeof(dst.tag) - 1);
	dst.isActive = src.isActive;
	dst.isStatic = src.isStatic;
	dst.layer = src.layer;
	dst.collisionMask = src.collisionMask;
	dst.translation = src.translation;
	dst.rotation = src.rotation;
	dst.scale = src.scale;
	dst.rotationQuat = src.rotationQuat;
	dst.velocity = src.velocity;
	dst.isGrounded = src.isGrounded;
	dst.isColliding = src.isColliding;
	dst.dirtyFromScript = false;
}

void EditorSuite::SyncViewToGameObject(const ScriptGameObjectView& src, GameObject& dst)
{
	dst.isActive = src.isActive;
	dst.isStatic = src.isStatic;
	dst.layer = src.layer & 31u;
	dst.collisionMask = src.collisionMask;
	dst.translation = src.translation;
	dst.rotation = src.rotation;
	dst.scale = src.scale;
	dst.rotationQuat = src.rotationQuat;
	dst.velocity = src.velocity;
	dst.isGrounded = src.isGrounded;
}

void EditorSuite::RefreshAllScriptObjectViews()
{
	std::unordered_set<uint32_t> liveIds;
	liveIds.reserve(gameObjects_.size() + pendingSpawns_.size());
	for (const auto& obj : gameObjects_) {
		auto& view = scriptObjectViews_[obj.id];
		if (!view) {
			view = std::make_unique<ScriptGameObjectView>();
		}
		SyncGameObjectToView(obj, *view);
		liveIds.insert(obj.id);
	}
	for (const auto& spawn : pendingSpawns_) {
		liveIds.insert(spawn.id);
	}

	// Retire views whose GameObject no longer exists (scripts may still hold the pointer)
	std::vector<uint32_t> staleIds;
	for (const auto& [id, view] : scriptObjectViews_) {
		if (liveIds.count(id) == 0) {
			staleIds.push_back(id);
		}
	}
	for (uint32_t id : staleIds) {
		RetireScriptView(id);
	}
}

ScriptGameObjectView* EditorSuite::GetOrUpdateScriptViewById(uint32_t id)
{
	if (const GameObject* obj = FindGameObjectById(id, false)) {
		auto& view = scriptObjectViews_[id];
		if (!view) {
			view = std::make_unique<ScriptGameObjectView>();
		}
		SyncGameObjectToView(*obj, *view);
		return view.get();
	}
	return FindPendingSpawnView(id);
}

ScriptGameObjectView* EditorSuite::FindPendingSpawnView(uint32_t id)
{
	for (const auto& spawn : pendingSpawns_) {
		if (spawn.id == id) {
			auto it = scriptObjectViews_.find(id);
			return (it != scriptObjectViews_.end()) ? it->second.get() : nullptr;
		}
	}
	return nullptr;
}

ScriptGameObjectView* EditorSuite::QueueScriptSpawn(const char* name, int primitiveType, const Vector3& position)
{
	PendingSpawn spawn{};
	spawn.id = nextObjectId_++;
	spawn.name = (name && name[0] != '\0') ? name : "InstantiatedObject";
	spawn.primitiveType = primitiveType;

	// The view exists immediately so the script can move/scale the object before it is created
	auto view = std::make_unique<ScriptGameObjectView>();
	view->id = spawn.id;
	strncpy_s(view->name, spawn.name.c_str(), sizeof(view->name) - 1);
	view->translation = position;
	view->rotationQuat = Quaternion::Identity();
	view->scale = (primitiveType == 2) ? Vector3{ 10.0f, 1.0f, 10.0f } : Vector3{ 1.0f, 1.0f, 1.0f };

	ScriptGameObjectView* rawView = view.get();
	scriptObjectViews_[spawn.id] = std::move(view);
	pendingSpawns_.push_back(std::move(spawn));
	return rawView;
}

bool EditorSuite::CancelPendingSpawn(uint32_t id)
{
	auto it = std::find_if(pendingSpawns_.begin(), pendingSpawns_.end(),
		[id](const PendingSpawn& spawn) { return spawn.id == id; });
	if (it == pendingSpawns_.end()) {
		return false;
	}
	if (it->reusePooled) {
		// The object exists (hidden in its pool): put it back instead of retiring its view
		prefabPools_[it->prefabName].push_back(id);
		pendingSpawns_.erase(it);
		if (auto viewIt = scriptObjectViews_.find(id); viewIt != scriptObjectViews_.end() && viewIt->second) {
			viewIt->second->isActive = false;
		}
		return true;
	}
	pendingSpawns_.erase(it);
	RetireScriptView(id);
	return true;
}

void EditorSuite::ProcessPendingSpawns()
{
	physicsQueryDirty_ = true;
	// Despawns first, so a prefab instance returned to its pool this frame is hidden before anything else runs
	ProcessPendingDespawns();
	ProcessPendingPrewarms();

	if (pendingSpawns_.empty()) {
		ProcessDeferredScriptRequests();
		return;
	}

	std::vector<PendingSpawn> spawns;
	spawns.swap(pendingSpawns_);

	isSpawningFromScript_ = true;
	gameObjects_.reserve(gameObjects_.size() + spawns.size());
	for (const auto& spawn : spawns) {
		auto viewIt = scriptObjectViews_.find(spawn.id);
		ScriptGameObjectView* view = (viewIt != scriptObjectViews_.end()) ? viewIt->second.get() : nullptr;
		const Vector3 position = view ? view->translation : Vector3{ 0.0f, 0.0f, 0.0f };

		if (!spawn.prefabName.empty()) {
			if (spawn.reusePooled) {
				ResetPooledPrefabInstance(spawn.id, view);
			} else if (InstantiatePrefabNow(spawn.prefabName, position, spawn.id, view) == 0) {
				RetireScriptView(spawn.id); // prefab vanished from disk since the spawn was queued
			}
			continue;
		}

		GameObject* created = nullptr;
		switch (spawn.primitiveType) {
		case 0: created = &CreateCube(spawn.name, position); break;
		case 1: created = &CreateSphere(spawn.name, position); break;
		case 2: created = &CreatePlane(spawn.name, position); break;
		case 3: created = &CreateQuad(spawn.name, position); break;
		default:
			created = &CreateGameObject(spawn.name, GameObjectType::Empty);
			created->translation = position;
			break;
		}

		// Keep the id that was already handed to the script
		created->id = spawn.id;
		if (view) {
			SyncViewToGameObject(*view, *created);
		}
	}
	isSpawningFromScript_ = false;

	RefreshAllScriptObjectViews();
	// Reparent / particle requests may target objects created just above
	ProcessDeferredScriptRequests();
}

void EditorSuite::RetireScriptView(uint32_t id)
{
	auto it = scriptObjectViews_.find(id);
	if (it == scriptObjectViews_.end()) {
		return;
	}
	if (it->second) {
		it->second->isActive = false;
		retiredScriptViews_.push_back(std::move(it->second));
	}
	scriptObjectViews_.erase(it);

	// Bound memory in long-running standalone games: drop the oldest half once the cap is reached
	if (retiredScriptViews_.size() > kMaxRetiredScriptViews) {
		const size_t dropCount = retiredScriptViews_.size() / 2;
		retiredScriptViews_.erase(retiredScriptViews_.begin(), retiredScriptViews_.begin() + static_cast<std::ptrdiff_t>(dropCount));
	}
}

void EditorSuite::ClearRetiredScriptViews()
{
	retiredScriptViews_.clear();
}

void EditorSuite::CaptureScriptFieldsToSaved(GameObject::ScriptComponent& sc)
{
	if (!sc.instance || !scriptModule_.IsLoaded()) return;

	int fieldCount = scriptModule_.GetFieldCount(sc.instance.get());
	for (int f = 0; f < fieldCount; ++f) {
		ScriptFieldInfo info{};
		if (!scriptModule_.GetFieldInfo(sc.instance.get(), f, info) || !info.dataPtr || !info.name || info.name[0] == '\0') continue;

		GameObject::ScriptSavedFieldValue saved{};
		saved.type = info.type;
		switch (info.type) {
		case ScriptFieldType::Float:
			saved.floatVal = *static_cast<const float*>(info.dataPtr);
			break;
		case ScriptFieldType::Int:
			saved.intVal = *static_cast<const int*>(info.dataPtr);
			break;
		case ScriptFieldType::Bool:
			saved.boolVal = *static_cast<const bool*>(info.dataPtr);
			break;
		case ScriptFieldType::Vec3:
			saved.vec3Val = *static_cast<const Vector3*>(info.dataPtr);
			break;
		case ScriptFieldType::CharBuffer:
			saved.strVal = static_cast<const char*>(info.dataPtr);
			break;
		}
		sc.savedFields[info.name] = saved;
	}
}

void EditorSuite::ApplySavedFieldsToScript(GameObject::ScriptComponent& sc)
{
	if (!sc.instance || !scriptModule_.IsLoaded() || sc.savedFields.empty()) return;

	int fieldCount = scriptModule_.GetFieldCount(sc.instance.get());
	for (int f = 0; f < fieldCount; ++f) {
		ScriptFieldInfo info{};
		if (!scriptModule_.GetFieldInfo(sc.instance.get(), f, info) || !info.dataPtr || !info.name || info.name[0] == '\0') continue;

		auto it = sc.savedFields.find(info.name);
		if (it != sc.savedFields.end() && it->second.type == info.type) {
			switch (info.type) {
			case ScriptFieldType::Float:
				*static_cast<float*>(info.dataPtr) = it->second.floatVal;
				break;
			case ScriptFieldType::Int:
				*static_cast<int*>(info.dataPtr) = it->second.intVal;
				break;
			case ScriptFieldType::Bool:
				*static_cast<bool*>(info.dataPtr) = it->second.boolVal;
				break;
			case ScriptFieldType::Vec3:
				*static_cast<Vector3*>(info.dataPtr) = it->second.vec3Val;
				break;
			case ScriptFieldType::CharBuffer:
				if (info.sizeInBytes > 0) {
					strncpy_s(static_cast<char*>(info.dataPtr), info.sizeInBytes, it->second.strVal.c_str(), info.sizeInBytes - 1);
				}
				break;
			}
		}
	}
}

std::string EditorSuite::SerializeAllScriptFieldsToJson()
{
	using SceneJson::Json;
	using SceneJson::Num;

	Json components = Json::array();
	for (auto& obj : gameObjects_) {
		for (size_t s = 0; s < obj.scripts.size(); ++s) {
			auto& sc = obj.scripts[s];
			CaptureScriptFieldsToSaved(sc);

			Json fields = Json::array();
			for (const auto& [fName, fVal] : sc.savedFields) {
				Json f = Json::object();
				f["name"] = fName;
				f["type"] = static_cast<int>(fVal.type);
				switch (fVal.type) {
				case ScriptFieldType::Float:
					f["v0"] = Num(fVal.floatVal);
					break;
				case ScriptFieldType::Int:
					f["v0"] = fVal.intVal;
					break;
				case ScriptFieldType::Bool:
					f["v0"] = fVal.boolVal ? 1 : 0;
					break;
				case ScriptFieldType::Vec3:
					f["v0"] = Num(fVal.vec3Val.x);
					f["v1"] = Num(fVal.vec3Val.y);
					f["v2"] = Num(fVal.vec3Val.z);
					break;
				case ScriptFieldType::CharBuffer:
					f["str"] = fVal.strVal;
					break;
				}
				fields.push_back(std::move(f));
			}

			Json entry = Json::object();
			entry["objectId"] = obj.id;
			entry["componentIndex"] = s;
			entry["className"] = sc.className;
			entry["fields"] = std::move(fields);
			components.push_back(std::move(entry));
		}
	}

	Json root = Json::object();
	root["scriptComponents"] = std::move(components);
	return SceneJson::DumpCompact(root);
}

void EditorSuite::DeserializeAllScriptFieldsFromJson(const std::string& json)
{
	(void)json;
	for (auto& obj : gameObjects_) {
		for (auto& sc : obj.scripts) {
			ApplySavedFieldsToScript(sc);
		}
	}
}

void EditorSuite::DestroyAllScriptInstances()
{
	for (auto& obj : gameObjects_) {
		for (auto& sc : obj.scripts) {
			if (sc.instance) {
				CaptureScriptFieldsToSaved(sc);
				sc.instance.reset();
			}
		}
	}
}

void EditorSuite::EnsureEditModeScriptInstances()
{
	if (sceneState_ != EditorSceneState::Edit) return;

	RefreshAllScriptObjectViews();
	for (auto& obj : gameObjects_) {
		for (auto& sc : obj.scripts) {
			if (sc.className.empty()) continue;

			if (scriptModule_.IsLoaded() && scriptModule_.IsRegistered(sc.className)) {
				sc.isMissingScript = false;
				sc.isCompiling = false;
				if (!sc.instance) {
					sc.instance = ScriptInstancePtr(scriptModule_.CreateScript(sc.className), ScriptInstanceDeleter{ &scriptModule_ });
					if (sc.instance) {
						sc.instance->SetOwner(GetOrUpdateScriptViewById(obj.id), obj.id);
						sc.instance->SetEngineAPI(&engineApi_);
						sc.instance->SetHost(this, this);
						ApplySavedFieldsToScript(sc);
						CaptureScriptFieldsToSaved(sc);
					}
				}
			} else if (!sc.isCompiling) {
				sc.instance.reset();
				sc.isMissingScript = true;
			}
		}
	}
}

void EditorSuite::HotReloadScripts()
{
	// 1. Serialize exposed fields of every script component in the scene (tagged by GameObject ID, component index, and class name)
	std::string savedJson = SerializeAllScriptFieldsToJson();

	// 2. Destroy all ScriptBase instances before unloading the old shadow DLL.
	//    Note: OnDestroy() is intentionally NOT called during hot reload because the GameObject is not being destroyed.
	DestroyAllScriptInstances();

	// 3. Unload old shadow DLL, copy new GameScripts.dll to a new shadow file, LoadLibrary, and resolve exports
	std::string reloadError;
	if (!scriptModule_.LoadShadowCopy(&reloadError)) {
		LogMessage(std::format("[Scripts] Hot reload failed: {}", reloadError), 2);
		// Typical after an engine update: the SDK headers keep their old dates, so an incremental build reuses
		// object files from the previous ScriptAPI.h. Rebuilding everything once fixes it.
		if (scriptModule_.LastLoadWasApiMismatch() && !apiRebuildTried_ && !isRunningTests_) {
			apiRebuildTried_ = true;
			LogMessage("[Scripts] Rebuilding all scripts against this engine version...", 1);
			TriggerScriptBuild(true);
		}
		return;
	}
	apiRebuildTried_ = false;

	RefreshAllScriptObjectViews();

	// 4. Recreate each script component instance by class name via the new DLL's CreateScript
	size_t reloadedInstanceCount = 0;
	for (auto& obj : gameObjects_) {
		for (auto& sc : obj.scripts) {
			sc.instance.reset();
			sc.isCompiling = false;
			if (sc.className.empty()) continue;

			if (scriptModule_.IsRegistered(sc.className)) {
				sc.isMissingScript = false;
				sc.instance = ScriptInstancePtr(scriptModule_.CreateScript(sc.className), ScriptInstanceDeleter{ &scriptModule_ });
				if (sc.instance) {
					sc.instance->SetOwner(GetOrUpdateScriptViewById(obj.id), obj.id);
					sc.instance->SetEngineAPI(&engineApi_);
					sc.instance->SetHost(this, this);
					reloadedInstanceCount++;
				}
			} else {
				sc.isMissingScript = true;
				LogMessage(std::format("[Scripts] Warning: Class '{}' on '{}' is no longer in GameScripts.dll (marked Missing Script).", sc.className, obj.name), 1);
			}
		}
	}

	// 5. Restore serialized field values by matching field name and type
	DeserializeAllScriptFieldsFromJson(savedJson);

	// 6. In Play mode, call OnAfterReload() instead of Initialize() so one-shot setup does not repeat
	if (sceneState_ != EditorSceneState::Edit) {
		for (auto& obj : gameObjects_) {
			if (!obj.isActive) continue;
			for (auto& sc : obj.scripts) {
				if (sc.instance) {
					ScriptGameObjectView* view = GetOrUpdateScriptViewById(obj.id);
					sc.instance->SetOwner(view, obj.id);
					sc.instance->SetEngineAPI(&engineApi_);
					sc.instance->SetHost(this, this);
					ScriptBase* instance = sc.instance.get();
					CallScript(instance, obj.id, "OnAfterReload", [&] { instance->OnAfterReload(); });
					if (view) {
						SyncViewToGameObject(*view, obj);
					}
				}
			}
		}
	}

	LogMessage(std::format("[Scripts] Reloaded {} ({} registered classes, {} active instances).",
		scriptModule_.GetActiveShadowPath().filename().string(),
		scriptModule_.GetRegisteredNames().size(),
		reloadedInstanceCount), 0);
}

