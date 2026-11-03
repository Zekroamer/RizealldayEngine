#pragma once
#include "math/Vector3.h"
#include <cstdint>
#include <string>
#include <vector>

// Terrain = a height grid turned into an ordinary mesh file. Pure math, no GPU and no files here:
// rendering, shadows, the Mesh Collider, saving and export then work as for any model.

struct TerrainHeights {
	int resolution = 0; // vertices per side
	std::vector<float> values; // 0..1, row by row (z, then x)

	float At(int x, int z) const { return values[static_cast<size_t>(z) * resolution + x]; }
};

/// Rolling hills from layered noise; the same settings always give the same result
TerrainHeights GenerateTerrainNoise(int resolution, int seed, float frequency, int octaves);

/// Resamples a grayscale image (width x height bytes, row 0 = far edge, +z)
TerrainHeights TerrainFromImage(const std::vector<uint8_t>& pixels, int width, int height, int resolution);

/// Wavefront OBJ text, centered on the origin, y = height * value. uvRepeat = how often a texture tiles.
std::string BuildTerrainObj(const TerrainHeights& heights, float size, float height, float uvRepeat);

enum class TerrainBrush : int { Raise, Lower, Smooth, Flatten };

/// One dab at (u, v) in 0..1 (u along x, v along z). radius = part of the terrain's width, soft towards its edge.
/// Raise / Lower add strength per dab; Smooth and Flatten (towards flattenTo) blend by strength (0..1). Values stay in 0..1.
void SculptTerrain(TerrainHeights& heights, float u, float v, float radius, float strength, TerrainBrush brush, float flattenTo = 0.5f);

/// Height (0..1) at (u, v), between the grid points
float TerrainHeightAt(const TerrainHeights& heights, float u, float v);

/// A ray in the terrain's own space (the mesh BuildTerrainObj writes). False when it misses.
bool RaycastTerrain(const TerrainHeights& heights, float size, float height, const Vector3& origin, const Vector3& direction, float& u, float& v);

/// Sculpted heights as a file: "RZTH", resolution, then the values
std::vector<uint8_t> EncodeTerrainHeights(const TerrainHeights& heights);
bool DecodeTerrainHeights(const std::vector<uint8_t>& bytes, TerrainHeights& out);

inline constexpr const char* kTerrainComponent = "Terrain";

/// Settings of the Inspector's Generate button; the result is the object's model and Mesh Collider
struct TerrainComponent {
	float size = 100.0f;
	float height = 10.0f;
	int resolution = 129;
	char heightmap[260] = {}; // empty = noise
	char heightData[260] = {}; // sculpted heights (.terrainheights); when set it wins over the heightmap and the noise
	int seed = 1;
	float frequency = 4.0f; // hills across the terrain
	int octaves = 4;
	float uvRepeat = 20.0f;
};
