#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
#include <wrl.h>

class DirectXCommon
{
public:
	// Descriptor Heap Capacities:
	// RTV Heap (8): [0..1] SwapChain (sRGB, 3D), [2] Scene RT, [3] Game RT, [4] Asset Preview RT,
	//               [6..7] SwapChain (UNORM, ImGui)
	// DSV Heap (8): [0] Main Depth, [1] Scene Depth, [2] Game Depth, [3] ShadowMap DSV, [4] Asset Preview Depth
	// SRV Heap (1024): [0] ImGui (fixed)
	//                  [128] Scene RT, [129] Game RT, [130] ShadowMap, [131] Cubemap, [132] Asset Preview RT,
	//                  [133..136] Preview Texture Ring (fixed range 128..139)
	//                  everything else: dynamic free-list (textures, ...) via AllocateSrvIndex()
	static constexpr uint32_t kMaxRtvDescriptors = 32;
	// Every 3D pass renders linear HDR color into this format; PostProcess turns it into the 8-bit output
	static constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	// RTV [8..10] / DSV [5..7]: HDR scene targets of the Scene, Game and preview views. RTV [12..] PostProcess.
	static constexpr uint32_t kSceneRtvBaseIndex = 8;
	static constexpr uint32_t kSceneDsvBaseIndex = 5;
	static constexpr uint32_t kPostRtvBaseIndex = 12;
	static constexpr uint32_t kScenePostRtvBaseIndex = 22; // the Scene view's own PostProcess (editor preview)
	static constexpr uint32_t kMaxDsvDescriptors = 8;
	static constexpr uint32_t kMaxSrvDescriptors = 1024;
	static constexpr uint32_t kFixedSrvBegin = 128;
	static constexpr uint32_t kFixedSrvEnd = 140;
	static constexpr uint32_t kUiRtvBaseIndex = 6;

	static DirectXCommon* GetInstance();

	void Initialize(HWND hwnd, uint32_t width = 1280, uint32_t height = 720);
	void ResizeBuffers(uint32_t width, uint32_t height);
	void PreDraw();
	void PostDraw();
	void Finalize(HWND hwnd);
	
	ID3D12Device* GetDevice() const { return device.Get(); }
	ID3D12GraphicsCommandList* GetCommandList() const { return commandList.Get(); }
	ID3D12CommandQueue* GetCommandQueue() const { return commandQueue.Get(); }
	ID3D12DescriptorHeap* GetSrvDescriptorHeap() const { return srvDescriptorHeap.Get(); }
	ID3D12CommandAllocator* GetCommandAllocator() const { return commandAllocators_[frameSlot_].Get(); }

	// ---- Frames in flight ----
	// The CPU records frame N while the GPU may still execute frame N-1. Each in-flight frame has its own
	// command allocator and per-frame upload buffer; PreDraw waits only for the frame that used the same slot.
	static constexpr uint32_t kFramesInFlight = 2;
	uint32_t GetFrameSlot() const { return frameSlot_; }
	uint64_t GetFrameIndex() const { return frameIndex_; }

	// ---- Per-frame linear upload allocator ----
	// Memory is valid for the current frame only (reset when its slot is reused). Use it for every
	// constant/vertex/structured buffer the CPU rewrites each frame; never keep the pointer across frames.
	struct UploadAllocation {
		void* cpu = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
	};
	static constexpr size_t kConstantBufferAlignment = 256;
	UploadAllocation AllocateUpload(size_t sizeInBytes, size_t alignment = kConstantBufferAlignment);
	/// Copies `sizeInBytes` into this frame's upload memory and returns its GPU address (0 on failure)
	D3D12_GPU_VIRTUAL_ADDRESS UploadData(const void* data, size_t sizeInBytes, size_t alignment = kConstantBufferAlignment);
	template <typename T>
	D3D12_GPU_VIRTUAL_ADDRESS UploadConstants(const T& data) { return UploadData(&data, sizeof(T)); }
	/// Bytes already handed out from the current frame slot's upload buffer (for the profiler)
	size_t GetFrameUploadBytesUsed() const { return frameUpload_[frameSlot_].offset; }
	static constexpr size_t GetFrameUploadCapacity() { return kFrameUploadBufferSize; }
	/// Name of the adapter in use ("WARP (software)" for the software rasterizer)
	const std::string& GetAdapterName() const { return adapterName_; }
	uint32_t GetSrvDescriptorSize() const { return device ? device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) : 0; }
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);
	void SignalAndWaitFence();

	// Deferred GPU Resource Release Queue (freed only after the fence of the last frame that used it has completed, >= 2 frames later)
	void DeferRelease(Microsoft::WRL::ComPtr<IUnknown> resource);
	void ProcessDeferredReleases(bool forceAll = false);
	uint64_t GetCurrentFenceValue() const { return fenceValue; }
	uint64_t GetCompletedFenceValue() const { return fence ? fence->GetCompletedValue() : 0; }

	// Dynamic SRV descriptor allocation (free-list). Fixed slots ([0] ImGui, [kFixedSrvBegin..kFixedSrvEnd))
	// are never handed out. A freed index becomes reusable only after the GPU has finished the frames
	// that may still reference it.
	static constexpr uint32_t kInvalidSrvIndex = 0xFFFFFFFFu;
	uint32_t AllocateSrvIndex();
	uint32_t PeekSrvIndex();
	// Textures stop this many slots early, so render targets made later (window resize, a view's post process,
	// ImGui font pages) always get one; without a slot they would write a descriptor outside the heap
	static constexpr uint32_t kSrvSlotsKeptForTargets = 64;
	bool HasFreeSrvIndexForTexture() { (void)PeekSrvIndex() /* reclaims retired indices first */; return freeSrvIndices_.size() > kSrvSlotsKeptForTargets; }
	void FreeSrvIndex(uint32_t index);
	uint32_t GetFreeSrvCount() const { return static_cast<uint32_t>(freeSrvIndices_.size()); }

	// Device Removed / Reset safety & Debug Layer message output
	bool IsDeviceLost() const { return isDeviceLost_; }
	bool CheckDeviceLost(HRESULT hr, const char* context = "");
	void FlushDebugMessages();

	uint32_t GetClientWidth() const { return kClientWidth; }
	uint32_t GetClientHeight() const { return kClientHeight; }

	// Descriptor handle helpers for offscreen RenderTexture (bounds-checked)
	D3D12_CPU_DESCRIPTOR_HANDLE GetRtvCPUDescriptorHandle(uint32_t index) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetDsvCPUDescriptorHandle(uint32_t index) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetSrvCPUDescriptorHandle(uint32_t index) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGPUDescriptorHandle(uint32_t index) const;

	// Reset render target to main BackBuffer (called after rendering Offscreen Viewports)
	void BindMainRenderTarget();

private:
	DirectXCommon() = default;
	~DirectXCommon() = default;
	DirectXCommon(const DirectXCommon&) = delete;
	DirectXCommon& operator=(const DirectXCommon&) = delete;

	void CreateFactory();
	void CreateDevice();
	void CreateAdapter();
	void CreateCommand();
	void CreateSwapChain(HWND hwnd);
	void DescriptorHeap();
	void CreateFence();
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height);

private:
	Microsoft::WRL::ComPtr<ID3D12Device> device = nullptr;

	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = nullptr;
	std::string adapterName_;
	uint32_t gpuFrameToken_ = 0xFFFFFFFFu; // profiler scope that brackets the whole frame
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource = nullptr;

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocators_[kFramesInFlight];
	uint64_t frameFenceValues_[kFramesInFlight] = {};
	uint32_t frameSlot_ = 0;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;

	struct FrameUploadBuffer {
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		uint8_t* cpu = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
		size_t offset = 0;
	};
	static constexpr size_t kFrameUploadBufferSize = 32ull * 1024ull * 1024ull; // per in-flight frame
	FrameUploadBuffer frameUpload_[kFramesInFlight];
	void CreateFrameUploadBuffers();
	void WaitForFenceValue(uint64_t value);

	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	// Non-sRGB views of the back buffers used only for ImGui, whose colors are already sRGB-encoded
	D3D12_CPU_DESCRIPTOR_HANDLE uiRtvHandles[2]{};
	void CreateBackBufferRenderTargetViews();

	// Descriptor Heap
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources[2] = {nullptr};

	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue = nullptr;

	// Fence & Deferred Release
	Microsoft::WRL::ComPtr<ID3D12Fence> fence = nullptr;
	uint64_t fenceValue = 0;
	uint64_t frameIndex_ = 0;
	HANDLE fenceEvent = nullptr;
	bool isDeviceLost_ = false;

	struct DeferredReleaseEntry {
		Microsoft::WRL::ComPtr<IUnknown> resource;
		uint64_t retireFenceValue = 0;
		uint64_t retireFrameIndex = 0;
	};
	std::vector<DeferredReleaseEntry> deferredReleaseQueue_;

	// SRV free-list (stack, lowest index on top) and indices waiting for the GPU before reuse
	struct RetiredSrvIndex {
		uint32_t index;
		uint64_t retireFenceValue;
	};
	std::vector<uint32_t> freeSrvIndices_;
	std::vector<RetiredSrvIndex> retiredSrvIndices_;
	void InitializeSrvFreeList();
	void ReclaimRetiredSrvIndices();

	uint32_t kClientWidth = 1280;
	uint32_t kClientHeight = 720;
	HWND hwnd_ = nullptr;

	D3D12_VIEWPORT viewport{};
	D3D12_RECT scissorRect{};
};
