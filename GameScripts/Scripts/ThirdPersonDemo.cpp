#include "ThirdPersonDemo.h"
#include <algorithm>
#include <cmath>

namespace {
	float gYaw = 0.0f;
	Vector3 gPlayerPos = { 0.0f, 0.0f, 0.0f };
	constexpr float kGravity = 20.0f;
}

void ThirdPersonPlayer::Initialize() {
	gYaw = 0.0f;
	fallSpeed_ = 0.0f;
	SetCursorLocked(true);
	PrewarmPrefab("ThirdPersonShot", 16);
}

void ThirdPersonPlayer::Update(float deltaTime) {
	GameObject* self = GetOwner();
	if (!self) return;

	// Forward and right follow the camera's yaw
	const Vector2 move = GetMoveAxis();
	const float sinYaw = std::sin(gYaw);
	const float cosYaw = std::cos(gYaw);
	Vector3 motion = {
		(move.x * cosYaw + move.y * sinYaw) * moveSpeed * deltaTime,
		0.0f,
		(move.y * cosYaw - move.x * sinYaw) * moveSpeed * deltaTime
	};
	fallSpeed_ += kGravity * deltaTime;
	motion.y = -fallSpeed_ * deltaTime;
	if (MoveCharacter(motion).grounded) fallSpeed_ = 0.0f;
	if (move.x != 0.0f || move.y != 0.0f) {
		self->rotation.y = std::atan2(motion.x, motion.z);
		self->rotationQuat = Quaternion::FromEuler(self->rotation);
	}
	gPlayerPos = self->translation;

	// Esc releases the cursor in the editor; a click takes it back instead of shooting
	if (IsMouseTriggered(0)) {
		if (!IsCursorLocked()) {
			SetCursorLocked(true);
		} else {
			Vector3 origin{};
			Vector3 direction{};
			ScriptRaycastHit hit{};
			if (ScreenPointToRay({ kUICanvasWidth * 0.5f, kUICanvasHeight * 0.5f }, origin, direction) &&
				Raycast(origin, direction, 200.0f, hit, kCollideWithAll, self->id)) {
				if (GameObject* shot = InstantiatePrefab("ThirdPersonShot", hit.point)) DespawnAfter(1.0f, shot->id);
			}
		}
	}

	if (IsMouseTriggered(1)) slashTime_ = 0.7f;
	if (slashTime_ > 0.0f) {
		slashTime_ -= deltaTime;
		const float opening = std::sin(std::clamp(slashTime_ / 0.7f, 0.0f, 1.0f) * 3.1415927f);
		const float params[16] = { 0.5f, 0.5f, -0.5f, opening, /* HDR red glows through bloom */ 6.0f, 0.3f, 0.2f, 1.0f };
		SetScreenEffect(0, "shaders/ScreenEffect.Slash.PS.hlsl", params);
		if (slashTime_ <= 0.0f) ClearScreenEffect(0);
	}

	UIText("+", kUICanvasWidth * 0.5f, kUICanvasHeight * 0.5f - 20.0f, 40.0f, { 1.0f, 1.0f, 1.0f, 0.9f }, 1);
	Vector2 label{};
	if (WorldToCanvas({ gPlayerPos.x, gPlayerPos.y + 1.4f, gPlayerPos.z }, label)) {
		UIText("Player", label.x, label.y, 24.0f, { 1.0f, 0.9f, 0.4f, 1.0f }, 1);
	}
}

void ThirdPersonCamera::Initialize() {
	pitch_ = 0.35f;
}

void ThirdPersonCamera::Update(float) {
	GameObject* self = GetOwner();
	if (!self) return;

	const Vector2 delta = GetMouseDelta();
	gYaw += delta.x * sensitivity;
	pitch_ = std::clamp(pitch_ + delta.y * sensitivity, -0.3f, 1.2f);
	distance = std::clamp(distance - GetMouseWheel(), 2.0f, 20.0f);

	const float horizontal = std::cos(pitch_) * distance;
	self->translation = {
		gPlayerPos.x - std::sin(gYaw) * horizontal,
		gPlayerPos.y + 1.0f + std::sin(pitch_) * distance,
		gPlayerPos.z - std::cos(gYaw) * horizontal
	};
	self->rotation = { pitch_, gYaw, 0.0f };
	self->rotationQuat = Quaternion::FromEuler(self->rotation);
}

REGISTER_SCRIPT(ThirdPersonPlayer)
REGISTER_SCRIPT(ThirdPersonCamera)
