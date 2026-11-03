#pragma once
#include "math/Vector3.h"
#include "math/Vector4.h"
#include <d3d12.h>
#include <wrl.h>
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <vector>

class DirectXCommon;
class DXCManager;
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
};

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
	void Clear();
	size_t EmitterCount() const { return emitters_.size(); }

private:
	struct Emitter {
		Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
		GpuParticleSettings settings;
		Vector3 origin{ 0.0f, 0.0f, 0.0f };
		uint32_t ringHead = 0;
		uint32_t seed = 1;
		bool simulated = false;
		bool isShaderResource = false;
	};

	DirectXCommon* dxCommon_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> computeSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> computePso_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> drawSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> alphaPso_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> additivePso_;
	std::unordered_map<uint64_t, Emitter> emitters_;
};
