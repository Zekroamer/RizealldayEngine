#pragma once
#include "render/TrailRenderer.h"
#include "render/GpuParticles.h"
#include "math/Matrix4x4.h"
#include <cmath>
#include <string>

inline constexpr const char* kTrailComponent = "Trail";

// Runtime switch set by an Effect Timing root (EditorSuite_Effects.cpp): None = the component's own "emitting"
inline constexpr int kEffectGateNone = 0;
inline constexpr int kEffectGateOn = 1;
inline constexpr int kEffectGateOff = 2;

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
	char texture[260] = {}; // empty = soft edges; u runs along the trail (head 0, tail 1), v across it

	TrailRibbon ribbon; // runtime, not saved
	int effectGate = kEffectGateNone; // runtime
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
	bool useMiddleKey = false; // size and colour go start -> middle (at middleTime) -> end
	float middleTime = 0.5f;
	float sizeMiddle = 0.2f;
	Vector4 colorMiddle{ 1.0f, 1.0f, 1.0f, 1.0f };
	bool collideWithFloor = false;
	float floorHeight = 0.0f; // world Y
	float bounce = 0.3f;
	float intensity = 1.0f;
	bool additive = true;
	char texture[260] = {}; // empty = soft circle
	int shape = 0;           // GpuParticleShape: 0 Point, 1 Sphere, 2 Box, 3 Cone, 4 Ring (rotates and scales with the object)
	int velocityMode = 0;    // GpuParticleVelocity: 0 Direction (cone around direction), 1 Radial (away from the centre)
	float shapeRadius = 0.5f;
	Vector3 boxExtents{ 0.5f, 0.5f, 0.5f };
	float stretchFactor = 0.0f; // > 0: streaks along the velocity (sparks, rain)
	bool uprightBillboard = false; // quads stand on world Y (flames, grass) instead of facing the camera
	int spawnBurst = 0;            // prefab instances fire this many when spawned or reused from the pool

	// Runtime, not saved
	float emitCarry = 0.0f;
	double lastTime = -1.0;
	int effectGate = kEffectGateNone;
};

/// Shape, velocity mode and stretch of `p` in world space: box half-axes follow the object's rotation and
/// per-axis scale, radii its largest scale. Unknown values (old files, scripts) fall back to a point emitter.
inline void ApplyGpuParticleShape(const GpuParticlesComponent& p, const Matrix4x4& world, GpuParticleSettings& s)
{
	const auto row = [&](int i) { return Vector3{ world.m[i][0], world.m[i][1], world.m[i][2] }; };
	const auto length = [](const Vector3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
	const auto scaled = [](const Vector3& v, float f) { return Vector3{ v.x * f, v.y * f, v.z * f }; };
	const bool knownShape = p.shape >= 0 && p.shape < static_cast<int>(GpuParticleShape::Count);
	const bool knownMode = p.velocityMode >= 0 && p.velocityMode < static_cast<int>(GpuParticleVelocity::Count);
	s.shape = knownShape ? static_cast<uint32_t>(p.shape) : 0u;
	s.velocityMode = knownMode ? static_cast<uint32_t>(p.velocityMode) : 0u;
	const float largestScale = (std::max)({ length(row(0)), length(row(1)), length(row(2)) });
	s.shapeRadius = (std::max)(p.shapeRadius, 0.0f) * largestScale;
	// Row-vector matrices: rows 0..2 are the object's X/Y/Z axes, already multiplied by its scale
	s.shapeAxisX = scaled(row(0), (std::max)(p.boxExtents.x, 0.0f));
	s.shapeAxisY = scaled(row(1), (std::max)(p.boxExtents.y, 0.0f));
	s.shapeAxisZ = scaled(row(2), (std::max)(p.boxExtents.z, 0.0f));
	s.stretch = std::isfinite(p.stretchFactor) ? (std::max)(p.stretchFactor, 0.0f) : 0.0f;
	s.upright = p.uprightBillboard;
}

/// Starting points of the Inspector's Preset menu
inline constexpr const char* kGpuParticlePresets[] = { "Fire", "Sparks", "Smoke", "Magic", "Snow", "Shockwave Ring", "Explosion", "Portal" };

/// Changes the look and motion, keeps capacity, texture and the runtime state. False for an unknown name.
inline bool ApplyGpuParticlePreset(GpuParticlesComponent& p, const char* name)
{
	const std::string preset = name ? name : "";
	const auto set = [&](float rate, float lifeMin, float lifeMax, float speedMin, float speedMax, float spread, const Vector3& gravity, float drag,
		float sizeStart, float sizeEnd, const Vector4& colorStart, const Vector4& colorEnd, float intensity, bool additive) {
		p.emitRate = rate; p.lifeMin = lifeMin; p.lifeMax = lifeMax; p.speedMin = speedMin; p.speedMax = speedMax; p.spreadDegrees = spread;
		p.direction = { 0.0f, 1.0f, 0.0f }; p.gravity = gravity; p.drag = drag; p.sizeStart = sizeStart; p.sizeEnd = sizeEnd;
		p.colorStart = colorStart; p.colorEnd = colorEnd; p.intensity = intensity; p.additive = additive;
		// Ticking Use Middle Key later starts from the halfway look, not from white
		p.sizeMiddle = (sizeStart + sizeEnd) * 0.5f;
		p.colorMiddle = { (colorStart.x + colorEnd.x) * 0.5f, (colorStart.y + colorEnd.y) * 0.5f, (colorStart.z + colorEnd.z) * 0.5f, (colorStart.w + colorEnd.w) * 0.5f };
		// A point emitter with round particles unless the preset says otherwise
		p.shape = static_cast<int>(GpuParticleShape::Point);
		p.velocityMode = static_cast<int>(GpuParticleVelocity::Direction);
		p.shapeRadius = 0.5f;
		p.boxExtents = { 0.5f, 0.5f, 0.5f };
		p.stretchFactor = 0.0f;
		p.uprightBillboard = false;
	};
	const auto shaped = [&](GpuParticleShape shape, GpuParticleVelocity mode, float radius, float stretch) {
		p.shape = static_cast<int>(shape);
		p.velocityMode = static_cast<int>(mode);
		p.shapeRadius = radius;
		p.stretchFactor = stretch;
	};
	if (preset == "Sparks") {
		set(80.0f, 0.4f, 1.0f, 3.0f, 8.0f, 60.0f, { 0.0f, -9.8f, 0.0f }, 0.2f, 0.08f, 0.02f, { 1.0f, 0.8f, 0.4f, 1.0f }, { 1.0f, 0.3f, 0.0f, 0.0f }, 5.0f, true);
		p.stretchFactor = 0.1f; // streaks
	} else if (preset == "Shockwave Ring") {
		set(300.0f, 0.5f, 0.5f, 6.0f, 6.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 2.0f, 0.25f, 0.05f, { 0.6f, 0.9f, 1.0f, 1.0f }, { 0.2f, 0.4f, 1.0f, 0.0f }, 3.0f, true);
		shaped(GpuParticleShape::Ring, GpuParticleVelocity::Radial, 0.3f, 0.05f);
	} else if (preset == "Explosion") {
		set(300.0f, 0.4f, 0.9f, 4.0f, 10.0f, 0.0f, { 0.0f, -2.0f, 0.0f }, 3.0f, 0.6f, 0.0f, { 1.0f, 0.6f, 0.15f, 1.0f }, { 0.6f, 0.05f, 0.0f, 0.0f }, 4.0f, true);
		shaped(GpuParticleShape::Sphere, GpuParticleVelocity::Radial, 0.3f, 0.03f);
	} else if (preset == "Portal") {
		set(200.0f, 1.0f, 2.0f, 0.1f, 0.3f, 10.0f, { 0.0f, 0.0f, 0.0f }, 0.5f, 0.12f, 0.0f, { 0.7f, 0.4f, 1.0f, 1.0f }, { 0.3f, 0.1f, 0.8f, 0.0f }, 3.0f, true);
		shaped(GpuParticleShape::Ring, GpuParticleVelocity::Direction, 1.5f, 0.0f);
		p.direction = { 0.0f, 0.0f, 1.0f }; // a standing ring that faces forward
	} else if (preset == "Fire") set(200.0f, 0.4f, 1.0f, 1.0f, 2.5f, 18.0f, { 0.0f, 1.0f, 0.0f }, 0.5f, 0.7f, 0.1f, { 1.0f, 0.45f, 0.1f, 0.35f }, { 0.8f, 0.08f, 0.0f, 0.0f }, 1.5f, true);
	else if (preset == "Smoke") set(30.0f, 2.0f, 4.0f, 0.3f, 0.8f, 25.0f, { 0.0f, 0.3f, 0.0f }, 0.3f, 0.6f, 2.5f, { 0.35f, 0.35f, 0.38f, 0.5f }, { 0.2f, 0.2f, 0.22f, 0.0f }, 1.0f, false);
	else if (preset == "Magic") set(120.0f, 0.8f, 1.6f, 0.5f, 1.5f, 180.0f, { 0.0f, 0.5f, 0.0f }, 1.0f, 0.15f, 0.0f, { 0.5f, 0.7f, 1.0f, 1.0f }, { 0.8f, 0.3f, 1.0f, 0.0f }, 4.0f, true);
	else if (preset == "Snow") set(60.0f, 4.0f, 7.0f, 0.1f, 0.5f, 180.0f, { 0.0f, -0.4f, 0.0f }, 0.5f, 0.08f, 0.06f, { 1.0f, 1.0f, 1.0f, 0.9f }, { 1.0f, 1.0f, 1.0f, 0.0f }, 1.0f, false);
	else return false;
	return true;
}

inline constexpr const char* kDecalComponent = "Decal";

/// Paints a texture on whatever is inside the object's unit box (scale = size), projected along its Y axis
struct DecalComponent {
	char texture[260] = {}; // empty = soft circle
	Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
	float intensity = 1.0f;
	float edgeFade = 0.3f;
	bool additive = false;
};
