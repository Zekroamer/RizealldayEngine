#include "BenchScripts.h"
#include <cmath>

namespace {
	constexpr float kArena = 30.0f;
	constexpr float kTau = 6.2831853f;
}

void BenchSpawner::Initialize() {
	// Golden-angle spiral: even, repeatable spread
	for (int i = 0; i < enemies; ++i) {
		const float angle = i * 2.3999632f;
		const float radius = 6.0f + 22.0f * std::sqrt((i + 0.5f) / enemies);
		InstantiatePrefab("BenchEnemy", Vector3{ std::cos(angle) * radius, 0.5f, std::sin(angle) * radius });
	}
	for (int i = 0; i < bullets; ++i) {
		const float angle = i * 2.3999632f;
		const float radius = kArena * std::sqrt((i + 0.5f) / bullets);
		if (GameObject* bullet = InstantiatePrefab("BenchBullet", Vector3{ std::cos(angle) * radius, 0.5f, std::sin(angle) * radius })) {
			bullet->rotation.y = kTau * i / bullets; // heading
		}
	}
}

void BenchEnemy::Update(float deltaTime) {
	GameObject* self = GetOwner();
	if (!self) return;
	const float dx = -self->translation.x;
	const float dz = -self->translation.z;
	const float dist = std::sqrt(dx * dx + dz * dz);
	if (dist > 2.0f) {
		const float step = 3.0f * deltaTime / dist;
		MoveCharacter(Vector3{ dx * step, 0.0f, dz * step });
	}
}

void BenchBullet::Update(float deltaTime) {
	GameObject* self = GetOwner();
	if (!self) return;
	self->translation.x += std::sin(self->rotation.y) * 12.0f * deltaTime;
	self->translation.z += std::cos(self->rotation.y) * 12.0f * deltaTime;
	if (std::abs(self->translation.x) > kArena) self->translation.x = -self->translation.x * 0.99f;
	if (std::abs(self->translation.z) > kArena) self->translation.z = -self->translation.z * 0.99f;
}

REGISTER_SCRIPT(BenchSpawner)
REGISTER_SCRIPT(BenchEnemy)
REGISTER_SCRIPT(BenchBullet)
