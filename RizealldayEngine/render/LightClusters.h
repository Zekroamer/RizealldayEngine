#pragma once
#include "math/Vector3.h"
#include <cstdint>
#include <vector>

// CPU light culling for forward shading: every pixel only loops over the lights of its cluster.
// A cluster is a tile of the screen cut into depth slices (log spaced). No GPU types here.
namespace LightClusters {

constexpr uint32_t kX = 16;
constexpr uint32_t kY = 9;
constexpr uint32_t kZ = 16; // same numbers as Object3d.PS.hlsl
constexpr uint32_t kCount = kX * kY * kZ;
constexpr uint32_t kMaxLightsPerCluster = 64;

struct Sphere {
	Vector3 viewPosition; // view space: +Z forward, +Y up
	float radius = 0.0f;
};

struct Projection {
	float tanHalfFovX = 1.0f;
	float tanHalfFovY = 1.0f;
	float nearZ = 0.1f;
	float farZ = 1000.0f;
	bool orthographic = false; // tiles mean nothing to the math below: every cluster lists every light
};

/// outClusters[(slice * kY + y) * kX + x] = first index in outIndices | count << 24. Row y = 0 is the top of the screen.
void Build(const std::vector<Sphere>& lights, const Projection& projection,
	std::vector<uint32_t>& outClusters, std::vector<uint32_t>& outIndices);

} // namespace LightClusters
