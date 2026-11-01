#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <DirectXTex.h>
#include "math/Vector2.h"

class DirectXCommon;

class TextureManager
{
public:
	static TextureManager* GetInstance();

	void Initialize(DirectXCommon* dxCommon);

	/// Load texture (cached)
	/// @return Texture handle ID
	/// linear = data, not color (normal, roughness, ...): no sRGB decoding when sampled
	uint32_t LoadTexture(const std::string& filePath, bool linear = false);

	/// Create (or return the cached) RGBA8 texture from memory, e.g. a generated font atlas.
	/// `cacheKey` must be unique and is never a file path. rgba holds width * height * 4 bytes, top row first.
	uint32_t CreateTextureFromRGBA(const std::string& cacheKey, const uint8_t* rgba, uint32_t width, uint32_t height);
	/// Replaces the pixels of a texture made by CreateTextureFromRGBA; the handle stays valid.
	/// Waits for the GPU like the create call, so use it for occasional changes (e.g. new font glyphs).
	bool UpdateTextureFromRGBA(uint32_t textureHandle, const uint8_t* rgba, uint32_t width, uint32_t height);

	/// Get GPU descriptor handle
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t textureHandle);

	/// Get texture metadata (width, height, format, mipLevels, etc.) by texture handle
	const DirectX::TexMetadata& GetMetaData(uint32_t textureHandle) const;

	/// Get texture metadata by file path (loads texture if not yet cached)
	const DirectX::TexMetadata& GetMetaData(const std::string& filePath);

	/// Get original texture size (width, height) in pixels
	Vector2 GetTextureSize(uint32_t textureHandle) const;

	/// Resolve texture handle ID from a GPU descriptor handle (returns 0 if unknown)
	uint32_t GetTextureHandleFromGPUDescriptor(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) const;

	/// Bind SRV Descriptor Heap to command list
	void SetDescriptorHeaps(ID3D12GraphicsCommandList* cmdList);

	/// Default white texture handle
	uint32_t GetDefaultWhiteTexture() const { return defaultWhiteHandle_; }
	uint32_t GetDefaultNormalTexture();

	/// Load cubemap texture (cached)
	/// @return Texture handle ID (0 on failure)
	uint32_t LoadCubemapTexture(const std::string& filePath);

	/// Default cubemap texture handle
	uint32_t GetDefaultCubemapTexture() const { return defaultCubemapHandle_; }

	/// Load or update transient preview texture in fixed SRV slot without growing the descriptor heap
	uint32_t LoadPreviewTexture(const std::string& filePath);

	/// Release transient preview texture GPU resource when selection changes
	void ReleasePreviewTexture();

	/// Generate procedural skybox DDS cubemap
	static void CreateProceduralSkyboxDDS(const std::string& outPath);

private:
	TextureManager() = default;
	~TextureManager() = default;
	TextureManager(const TextureManager&) = delete;
	TextureManager& operator=(const TextureManager&) = delete;

	static std::string NormalizePath(const std::string& filePath);
	uint32_t CreateDefaultCubemapTexture();
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);
	[[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages);

	// Texture handle == SRV descriptor index, allocated from DirectXCommon's free-list
	static const uint32_t kPreviewSrvBaseIndex = 133;
	static const uint32_t kPreviewSrvSlotCount = 4;
	DirectXCommon* dxCommon_ = nullptr;
	uint32_t defaultWhiteHandle_ = 0;
	uint32_t defaultNormalHandle_ = 0;
	uint32_t defaultCubemapHandle_ = 0;
	uint32_t previewSrvRingOffset_ = 0;
	uint32_t currentPreviewSrvIndex_ = kPreviewSrvBaseIndex;
	std::unordered_map<std::string, uint32_t> textureCache_;
	std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> textureResources_;
	std::vector<DirectX::TexMetadata> textureMetadatas_;
	Microsoft::WRL::ComPtr<ID3D12Resource> previewTextureResource_;
	DirectX::TexMetadata previewTextureMetadata_{};
	DirectX::TexMetadata fallbackMetadata_{};
	std::string currentPreviewTexturePath_;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> uploadAllocator_;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> uploadCommandList_;
};
