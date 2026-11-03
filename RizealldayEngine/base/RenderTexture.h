#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

class DirectXCommon;

class RenderTexture
{
public:
	~RenderTexture();

	void Initialize(
		DirectXCommon* dxCommon,
		uint32_t width,
		uint32_t height,
		uint32_t rtvIndex,
		uint32_t dsvIndex,
		uint32_t srvIndex,
		const float clearColor[4] = nullptr,
		bool hdrScene = false, // linear DirectXCommon::kSceneColorFormat instead of sRGB 8-bit
		uint32_t depthSrvIndex = 0xFFFFFFFFu // lets shaders read the depth buffer (BeginDepthRead)
	);

	void BeginRender(bool clearColor = true);
	void EndRender();
	/// Depth becomes readable through GetDepthSrvGPUHandle until the next BeginRender
	void BeginDepthRead(ID3D12GraphicsCommandList* cmdList);
	D3D12_GPU_DESCRIPTOR_HANDLE GetDepthSrvGPUHandle() const { return depthSrvGpuHandle_; }
	bool IsDepthReadable() const { return depthReadable_; }
	/// Inside a pass: read the depth while drawing to color only (decals), then go back to color + depth
	void BeginColorOnlyWithDepthRead(ID3D12GraphicsCommandList* cmdList);
	void EndColorOnlyWithDepthRead(ID3D12GraphicsCommandList* cmdList);

	/// Records a copy of the color target into a new readback buffer (call after EndRender, map it after the frame's fence).
	/// The buffer holds tightly addressed RGBA8 rows of outRowPitch bytes.
	bool RecordReadback(ID3D12GraphicsCommandList* cmdList, Microsoft::WRL::ComPtr<ID3D12Resource>& outBuffer, uint32_t& outRowPitch);

	bool IsInitialized() const { return isInitialized_; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGPUHandle() const { return srvGpuHandle_; }
	uint32_t GetWidth() const { return width_; }
	uint32_t GetHeight() const { return height_; }

private:
	DirectXCommon* dxCommon_ = nullptr;
	uint32_t width_ = 1280;
	uint32_t height_ = 720;
	float clearColor_[4] = { 0.12f, 0.12f, 0.15f, 1.0f };
	bool isInitialized_ = false;
	D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	bool depthReadable_ = false;
	bool depthInReadState_ = false;
	D3D12_GPU_DESCRIPTOR_HANDLE depthSrvGpuHandle_{};

	Microsoft::WRL::ComPtr<ID3D12Resource> colorResource_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> depthResource_ = nullptr;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_{};
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};
	D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle_{};
	D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle_{};

	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissorRect_{};
};
