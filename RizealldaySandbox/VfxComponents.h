#pragma once
#include "render/TrailRenderer.h"
#include <string>

inline constexpr const char* kTrailComponent = "Trail";

/// A ribbon behind a moving object (sword swing, dash, projectile)
struct TrailComponent {
	float lifetime = 0.5f;
	float minDistance = 0.05f;
	float widthStart = 0.3f;
	float widthEnd = 0.0f;
	Vector4 colorStart{ 1.0f, 1.0f, 1.0f, 1.0f };
	Vector4 colorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };
	float intensity = 1.0f; // above 1 glows through the bloom
	bool additive = true;
	bool emitting = true; // turn off to let the trail fade, e.g. at the end of a swing

	TrailRibbon ribbon; // runtime, not saved
};

inline constexpr const char* kGpuParticlesComponent = "GPU Particles";

/// Many particles simulated on the GPU. Runs in Edit too, so effects can be tuned while watching them.
struct GpuParticlesComponent {
	int capacity = 2000;
	float emitRate = 100.0f; // per second
	int burst = 0;           // this many at once on the next frame, then back to 0 (SetComponentInt from a script)
	bool emitting = true;
	float lifeMin = 0.5f;
	float lifeMax = 1.5f;
	float speedMin = 1.0f;
	float speedMax = 3.0f;
	float spreadDegrees = 30.0f;
	Vector3 direction{ 0.0f, 1.0f, 0.0f };
	Vector3 gravity{ 0.0f, 0.0f, 0.0f };
	float drag = 0.0f;
	float sizeStart = 0.2f;
	float sizeEnd = 0.0f;
	Vector4 colorStart{ 1.0f, 1.0f, 1.0f, 1.0f };
	Vector4 colorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };
	float intensity = 1.0f;
	bool additive = true;
	char texture[260] = {}; // empty = soft circle

	// Runtime, not saved
	float emitCarry = 0.0f;
	double lastTime = -1.0;
};

/// Starting points of the Inspector's Preset menu
inline constexpr const char* kGpuParticlePresets[] = { "Fire", "Sparks", "Smoke", "Magic", "Snow" };

/// Changes the look and motion, keeps capacity, texture and the runtime state. False for an unknown name.
inline bool ApplyGpuParticlePreset(GpuParticlesComponent& p, const char* name)
{
	const std::string preset = name ? name : "";
	const auto set = [&](float rate, float lifeMin, float lifeMax, float speedMin, float speedMax, float spread, const Vector3& gravity, float drag,
		float sizeStart, float sizeEnd, const Vector4& colorStart, const Vector4& colorEnd, float intensity, bool additive) {
		p.emitRate = rate; p.lifeMin = lifeMin; p.lifeMax = lifeMax; p.speedMin = speedMin; p.speedMax = speedMax; p.spreadDegrees = spread;
		p.direction = { 0.0f, 1.0f, 0.0f }; p.gravity = gravity; p.drag = drag; p.sizeStart = sizeStart; p.sizeEnd = sizeEnd;
		p.colorStart = colorStart; p.colorEnd = colorEnd; p.intensity = intensity; p.additive = additive;
	};
	if (preset == "Fire") set(200.0f, 0.4f, 1.0f, 1.0f, 2.5f, 18.0f, { 0.0f, 1.0f, 0.0f }, 0.5f, 0.7f, 0.1f, { 1.0f, 0.55f, 0.15f, 1.0f }, { 0.9f, 0.1f, 0.0f, 0.0f }, 3.0f, true);
	else if (preset == "Sparks") set(80.0f, 0.4f, 1.0f, 3.0f, 8.0f, 60.0f, { 0.0f, -9.8f, 0.0f }, 0.2f, 0.08f, 0.02f, { 1.0f, 0.8f, 0.4f, 1.0f }, { 1.0f, 0.3f, 0.0f, 0.0f }, 5.0f, true);
	else if (preset == "Smoke") set(30.0f, 2.0f, 4.0f, 0.3f, 0.8f, 25.0f, { 0.0f, 0.3f, 0.0f }, 0.3f, 0.6f, 2.5f, { 0.35f, 0.35f, 0.38f, 0.5f }, { 0.2f, 0.2f, 0.22f, 0.0f }, 1.0f, false);
	else if (preset == "Magic") set(120.0f, 0.8f, 1.6f, 0.5f, 1.5f, 180.0f, { 0.0f, 0.5f, 0.0f }, 1.0f, 0.15f, 0.0f, { 0.5f, 0.7f, 1.0f, 1.0f }, { 0.8f, 0.3f, 1.0f, 0.0f }, 4.0f, true);
	else if (preset == "Snow") set(60.0f, 4.0f, 7.0f, 0.1f, 0.5f, 180.0f, { 0.0f, -0.4f, 0.0f }, 0.5f, 0.08f, 0.06f, { 1.0f, 1.0f, 1.0f, 0.9f }, { 1.0f, 1.0f, 1.0f, 0.0f }, 1.0f, false);
	else return false;
	return true;
}
