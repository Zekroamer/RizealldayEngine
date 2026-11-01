#pragma once
#include "math/Vector3.h"
#include "math/Vector4.h"
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <unordered_map>

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
	bool additive = true;
	uint32_t textureHandle = 0;
};

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
