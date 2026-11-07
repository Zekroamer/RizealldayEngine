#pragma once
#include "math/Vector3.h"
#include "math/Vector4.h"
#include <d3d12.h>
#include <wrl.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

class DirectXCommon;
class DXCManager;
struct IDxcBlob;
class Camera;

struct GpuParticleSettings {
	uint32_t capacity = 2000;
	float lifeMin = 0.5f;
	float lifeMax = 1.5f;
	float speedMin = 1.0f;
	float speedMax = 3.0f;
	float spreadDegrees = 30.0f; // cone around direction; 180 = every way
	Vector3 direction{ 0.0f, 1.0f, 0.0f };
	Vector3 gravity{ 0.0f, 0.0f, 0.0f };
	float drag = 0.0f;
	float sizeStart = 0.2f;
	float sizeEnd = 0.0f;
	Vector4 colorStart{ 1.0f, 1.0f, 1.0f, 1.0f };
	Vector4 colorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };
	bool useMiddleKey = false; // size and colour pass through the middle values at middleTime
	float middleTime = 0.5f;
	float sizeMiddle = 0.2f;
	Vector4 colorMiddle{ 1.0f, 1.0f, 1.0f, 1.0f };
	bool collideWithFloor = false; // a flat floor at floorHeight (world Y)
	float floorHeight = 0.0f;
	float bounce = 0.3f; // 0 = stops, 1 = keeps its speed
	bool additive = true;
	uint32_t textureHandle = 0;
	// Where new particles appear and which way they fly (world space, built from the object's transform)
	uint32_t shape = 0;        // GpuParticleShape
	uint32_t velocityMode = 0; // GpuParticleVelocity
	float shapeRadius = 0.5f;  // Sphere / Cone base / Ring
	Vector3 shapeAxisX{ 0.5f, 0.0f, 0.0f }; // Box: half-axes, rotated and scaled with the object
	Vector3 shapeAxisY{ 0.0f, 0.5f, 0.0f };
	Vector3 shapeAxisZ{ 0.0f, 0.0f, 0.5f };
	float stretch = 0.0f; // 0 = round billboards; > 0 = streaks along the velocity, length x (1 + speed * stretch)
	bool upright = false; // quads stand on world Y and only turn about it (grass, flames seen from above stay thin)
	uint64_t material = 0;              // a material built with BuildMaterial; 0 = texture x colour
	uint32_t materialTextures[4] = {};  // texture handles of the material's slots; 0 = textureHandle
	// A particle graph built with BuildParticleGraph: it alone decides how particles start and move (life, speed,
	// shape, gravity, size and colour above are then unused); 0 = the settings above
	uint64_t graph = 0;
	Vector4 graphParams[8] = {}; // the graph's parameters, in its order
};

enum class GpuParticleShape : uint32_t { Point = 0, Sphere = 1, Box = 2, Cone = 3, Ring = 4, Count };
enum class GpuParticleVelocity : uint32_t { Direction = 0, Radial = 1, Count };

/// Two unit vectors across `direction` (normalized); GpuParticle.CS.hlsl builds the same pair
inline void GpuParticleTangents(const Vector3& direction, Vector3& tangent, Vector3& bitangent)
{
	const Vector3 helper = std::abs(direction.y) < 0.99f ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
	tangent = { helper.y * direction.z - helper.z * direction.y, helper.z * direction.x - helper.x * direction.z, helper.x * direction.y - helper.y * direction.x };
	const float length = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z);
	tangent = { tangent.x / length, tangent.y / length, tangent.z / length };
	bitangent = { direction.y * tangent.z - direction.z * tangent.y, direction.z * tangent.x - direction.x * tangent.z, direction.x * tangent.y - direction.y * tangent.x };
}

/// Spawn position relative to the emitter for random numbers u1..u3 in [0, 1]. Same formulas as SpawnOffset in GpuParticle.CS.hlsl.
inline Vector3 GpuParticleSpawnOffset(const GpuParticleSettings& s, const Vector3& direction, float u1, float u2, float u3)
{
	constexpr float kTwoPi = 6.2831853f;
	switch (static_cast<GpuParticleShape>(s.shape)) {
	case GpuParticleShape::Sphere: {
		// Uniform in the ball: uniform direction, distance cbrt(u) x radius
		const float z = 1.0f - 2.0f * u1;
		const float ring = std::sqrt((std::max)(1.0f - z * z, 0.0f));
		const float angle = u2 * kTwoPi;
		const float distance = s.shapeRadius * std::cbrt(u3);
		return { std::cos(angle) * ring * distance, std::sin(angle) * ring * distance, z * distance };
	}
	case GpuParticleShape::Box: {
		const float a = u1 * 2.0f - 1.0f, b = u2 * 2.0f - 1.0f, c = u3 * 2.0f - 1.0f;
		return { s.shapeAxisX.x * a + s.shapeAxisY.x * b + s.shapeAxisZ.x * c,
			s.shapeAxisX.y * a + s.shapeAxisY.y * b + s.shapeAxisZ.y * c,
			s.shapeAxisX.z * a + s.shapeAxisY.z * b + s.shapeAxisZ.z * c };
	}
	case GpuParticleShape::Cone:
	case GpuParticleShape::Ring: {
		// Cone: the whole base disc; Ring: only its edge. Both lie across the direction.
		Vector3 t, b;
		GpuParticleTangents(direction, t, b);
		const float angle = u1 * kTwoPi;
		const float r = s.shapeRadius * (s.shape == static_cast<uint32_t>(GpuParticleShape::Cone) ? std::sqrt(u2) : 1.0f);
		const float x = std::cos(angle) * r, y = std::sin(angle) * r;
		return { t.x * x + b.x * y, t.y * x + b.y * y, t.z * x + b.z * y };
	}
	default:
		return { 0.0f, 0.0f, 0.0f };
	}
}

/// Radial mode: away from the emitter's centre; from the centre itself any direction (uniform on the sphere)
inline Vector3 GpuParticleRadialDirection(const Vector3& offset, float u1, float u2)
{
	const float length = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
	if (length > 1.0e-4f) return { offset.x / length, offset.y / length, offset.z / length };
	const float z = 1.0f - 2.0f * u1;
	const float ring = std::sqrt((std::max)(1.0f - z * z, 0.0f));
	const float angle = u2 * 6.2831853f;
	return { std::cos(angle) * ring, std::sin(angle) * ring, z };
}

/// Value at life fraction t (0..1). Same formula as Key() in GpuParticle.VS.hlsl.
inline float GpuParticleKey(float t, float start, float middle, float end, float middleTime, bool useMiddleKey)
{
	const auto mix = [](float a, float b, float f) { return a + (b - a) * f; };
	t = std::clamp(t, 0.0f, 1.0f);
	if (!useMiddleKey) return mix(start, end, t);
	const float m = std::clamp(middleTime, 0.001f, 0.999f);
	return t < m ? mix(start, middle, t / m) : mix(middle, end, (t - m) / (1.0f - m));
}

/// Indices of the emitters, farthest from the eye first (alpha blending needs back to front)
inline std::vector<size_t> GpuParticleDrawOrder(const std::vector<Vector3>& origins, const Vector3& eye)
{
	std::vector<size_t> order(origins.size());
	std::iota(order.begin(), order.end(), size_t{ 0 });
	const auto distance = [&](size_t i) {
		const Vector3 d{ origins[i].x - eye.x, origins[i].y - eye.y, origins[i].z - eye.z };
		return d.x * d.x + d.y * d.y + d.z * d.z;
	};
	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return distance(a) > distance(b); });
	return order;
}

/// How many particles a continuous rate gives this step, keeping the fraction for the next one
inline uint32_t GpuParticleEmitCount(float rate, float deltaTime, float& carry)
{
	if (!(rate > 0.0f) || !(deltaTime > 0.0f)) return 0;
	carry += rate * deltaTime;
	const uint32_t count = static_cast<uint32_t>(carry);
	carry -= static_cast<float>(count);
	return count;
}

/// Particles simulated in a compute shader. One buffer per emitter, reused as a ring (a full ring overwrites its oldest).
/// Emitters are keyed by the caller's id; one not simulated in a frame's EndFrame is released.
class GpuParticles {
public:
	static GpuParticles* GetInstance();
	void Initialize(DirectXCommon* dxCommon, DXCManager* dxc);
	void Finalize();

	/// One step of one emitter. A new capacity starts it over.
	void Simulate(ID3D12GraphicsCommandList* cmdList, uint64_t id, const Vector3& origin, float deltaTime, uint32_t emitCount, const GpuParticleSettings& settings);
	/// Releases emitters that were not simulated since the last call
	void EndFrame();
	void Draw(ID3D12GraphicsCommandList* cmdList, const Camera& camera);
	/// The editor's material preview: simulated like any emitter, drawn only by DrawEmitter (never by Draw)
	static constexpr uint64_t kPreviewEmitterId = 0xFFFFFFFFFFFFFF01ull;
	void DrawEmitter(ID3D12GraphicsCommandList* cmdList, const Camera& camera, uint64_t id);
	/// Only these emitters, back to front (the Effect Editor's preview of one effect)
	void DrawEmitters(ID3D12GraphicsCommandList* cmdList, const Camera& camera, const std::vector<uint64_t>& ids);
	/// Time materials see (gTime): game time in Play, the editor clock in Edit
	void SetTime(float seconds) { time_ = seconds; }

	// Materials (node graphs): `body` is the HLSL a material graph generated; it runs inside MaterialShaderSource.
	// Built once per key; false with the compiler output when it does not compile (the emitter then draws as before).
	bool BuildMaterial(uint64_t key, const std::string& body, std::string* error);
	bool HasMaterial(uint64_t key) const { return materials_.count(key) != 0; }
	/// The whole pixel shader of a material graph: inputs uv, vertexColor, particleAge, particleRandom, gTime, gTexture0..3
	static std::string MaterialShaderSource(const std::string& body);
	static constexpr uint32_t kMaterialTextures = 4;

	// Particle graphs: the compute shader of an emitter from two generated bodies. Spawn runs once per new particle
	// (sets position, velocity, life, color, size, rotation), Update every frame per living one (velocity, color,
	// size, rotation, kill; the position then moves by velocity). An empty body keeps the defaults.
	static std::string ParticleGraphShaderSource(const std::string& spawnBody, const std::string& updateBody);
	static constexpr uint32_t kGraphParameters = 8;
	/// Builds the compute shader of a particle graph once per key; false with the compiler output when it does not compile
	bool BuildParticleGraph(uint64_t key, const std::string& spawnBody, const std::string& updateBody, std::string* error);
	bool HasParticleGraph(uint64_t key) const { return graphs_.count(key) != 0; }
	void Clear();
	size_t EmitterCount() const { return emitters_.size(); }
	/// GPU buffers created so far (a pooled object that comes back reuses its buffer)
	uint64_t BufferCreations() const { return bufferCreations_; }
	/// An emitter not simulated for this many EndFrames is released; until then it is hidden and kept for reuse
	static constexpr uint32_t kIdleFramesBeforeRelease = 120;

private:
	struct Emitter {
		Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
		GpuParticleSettings settings;
		Vector3 origin{ 0.0f, 0.0f, 0.0f };
		uint32_t ringHead = 0;
		uint32_t seed = 1;
		bool simulated = false;
		bool isShaderResource = false;
		uint32_t idleFrames = 0; // EndFrames without a Simulate; > 0 = hidden, its particles are stale
		bool usesGraph = false;  // 64-byte graph particles (drawn by the graph vertex shader)
	};

	struct MaterialPsos {
		Microsoft::WRL::ComPtr<ID3D12PipelineState> alpha;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> additive;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> graphAlpha; // the same pixel shader on graph particles
		Microsoft::WRL::ComPtr<ID3D12PipelineState> graphAdditive;
	};
	Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateDrawPso(IDxcBlob* ps, bool additive, IDxcBlob* vs) const;
	/// only == nullptr: every emitter except the preview one
	void DrawList(ID3D12GraphicsCommandList* cmdList, const Camera& camera, const std::vector<uint64_t>* only);

	DirectXCommon* dxCommon_ = nullptr;
	DXCManager* dxc_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShader_;
	Microsoft::WRL::ComPtr<IDxcBlob> graphVertexShader_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphAlphaPso_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphAdditivePso_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> graphComputeSignature_;
	std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID3D12PipelineState>> graphs_;
	std::unordered_map<uint64_t, MaterialPsos> materials_;
	float time_ = 0.0f;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> computeSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> computePso_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> drawSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> alphaPso_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> additivePso_;
	std::unordered_map<uint64_t, Emitter> emitters_;
	uint64_t bufferCreations_ = 0;
};
