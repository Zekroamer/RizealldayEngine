#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

class DirectXCommon;
class DXCManager;
class Skybox;
struct IDxcBlob;

/// The sky as a prefiltered cube for image-based light: mip 0 is sharp, the last mip stands in for irradiance.
/// Baked again only when the sky changes.
class EnvironmentProbe {
public:
	static constexpr uint32_t kSize = 128;
	static constexpr uint32_t kMips = 6; // 128 .. 4

	~EnvironmentProbe();
	void Initialize(DirectXCommon* dxCommon, DXCManager* dxc);
	/// Records the bake when `skybox` differs from the last one baked. Render targets and viewports are left changed.
	void Update(ID3D12GraphicsCommandList* cmdList, Skybox& skybox);
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrv() const { return srv_; }
	float GetMaxMip() const { return static_cast<float>(kMips - 1); }
	uint64_t GetBakeCount() const { return bakeCount_; }

private:
	struct Constants {
		float right[4];
		float up[4];
		float forward[3];
		float roughness; // packed right after the float3, like the shader
	};

	DirectXCommon* dxCommon_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> prefilter_;
	Microsoft::WRL::ComPtr<ID3D12Resource> capture_; // the sky, one mip
	Microsoft::WRL::ComPtr<ID3D12Resource> cube_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_; // capture faces, then cube faces per mip
	UINT rtvStep_ = 0;
	uint32_t captureSrvIndex_ = 0xFFFFFFFFu;
	uint32_t cubeSrvIndex_ = 0xFFFFFFFFu;
	D3D12_GPU_DESCRIPTOR_HANDLE captureSrv_{};
	D3D12_GPU_DESCRIPTOR_HANDLE srv_{};
	unsigned char lastSky_[128] = {};
	bool baked_ = false;
	uint64_t bakeCount_ = 0;
};
