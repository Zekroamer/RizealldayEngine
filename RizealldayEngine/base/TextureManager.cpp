#include "base/TextureManager.h"
#include "base/DirectXCommon.h"
#include "base/ConvertString.h"
#include "base/Log.h"
#include "d3dx12.h"
#include <cassert>
#include <vector>
#include <filesystem>

TextureManager* TextureManager::GetInstance()
{
	static TextureManager instance;
	return &instance;
}

void TextureManager::Initialize(DirectXCommon* dxCommon)
{
	dxCommon_ = dxCommon;
	textureResources_.assign(DirectXCommon::kMaxSrvDescriptors, nullptr);
	textureMetadatas_.assign(DirectXCommon::kMaxSrvDescriptors, DirectX::TexMetadata{});
	textureCache_.clear();

	fallbackMetadata_ = {};
	fallbackMetadata_.width = 1;
	fallbackMetadata_.height = 1;
	fallbackMetadata_.depth = 1;
	fallbackMetadata_.arraySize = 1;
	fallbackMetadata_.mipLevels = 1;
	fallbackMetadata_.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	fallbackMetadata_.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

	HRESULT hrAlloc = dxCommon_->GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAllocator_));
	assert(SUCCEEDED(hrAlloc));
	HRESULT hrList = dxCommon_->GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAllocator_.Get(), nullptr, IID_PPV_ARGS(&uploadCommandList_));
	assert(SUCCEEDED(hrList));
	uploadCommandList_->Close();

	// 1x1 white fallback texture
	DirectX::ScratchImage whiteImg;
	HRESULT hr = whiteImg.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, 1);
	if (SUCCEEDED(hr)) {
		const DirectX::Image* img = whiteImg.GetImage(0, 0, 0);
		if (img && img->pixels) {
			uint32_t whitePixel = 0xFFFFFFFF;
			memcpy(img->pixels, &whitePixel, sizeof(whitePixel));

			uint32_t handle = dxCommon_->AllocateSrvIndex();
			defaultWhiteHandle_ = handle;

			Microsoft::WRL::ComPtr<ID3D12Resource> resource = CreateTextureResource(whiteImg.GetMetadata());

			uploadAllocator_->Reset();
			uploadCommandList_->Reset(uploadAllocator_.Get(), nullptr);

			Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(resource.Get(), whiteImg);

			uploadCommandList_->Close();
			ID3D12CommandList* commandLists[] = { uploadCommandList_.Get() };
			dxCommon_->GetCommandQueue()->ExecuteCommandLists(1, commandLists);
			dxCommon_->SignalAndWaitFence();

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;

			D3D12_CPU_DESCRIPTOR_HANDLE srvCPUHandle = dxCommon_->GetSrvDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
			srvCPUHandle.ptr += dxCommon_->GetSrvDescriptorSize() * handle;
			dxCommon_->GetDevice()->CreateShaderResourceView(resource.Get(), &srvDesc, srvCPUHandle);

			textureResources_[handle] = resource;
			textureMetadatas_[handle] = whiteImg.GetMetadata();
			fallbackMetadata_ = whiteImg.GetMetadata();
		}

		CreateDefaultCubemapTexture();
	}
}

std::string TextureManager::NormalizePath(const std::string& filePath)
{
	std::string norm;
	norm.reserve(filePath.size());
	char prev = 0;
	for (char ch : filePath) {
		char c = (ch == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		if (c == '/' && prev == '/') {
			continue;
		}
		norm.push_back(c);
		prev = c;
	}
	if (norm.rfind("./", 0) == 0) {
		norm.erase(0, 2);
	}
	return norm;
}

uint32_t TextureManager::LoadTexture(const std::string& filePath, bool linear)
{
	if (!dxCommon_ || dxCommon_->IsDeviceLost()) {
		return defaultWhiteHandle_;
	}

	std::string normKey = NormalizePath(filePath) + (linear ? "|linear" : "");

	// Check cache
	auto it = textureCache_.find(normKey);
	if (it != textureCache_.end()) {
		return it->second;
	}

	if (!std::filesystem::exists(filePath)) {
		Log::GetInstance()->logOutput(std::format("Texture not found: {}, using default white.\n", filePath));
		return defaultWhiteHandle_;
	}

	if (!dxCommon_->HasFreeSrvIndexForTexture()) {
		static bool sLoggedLimit = false;
		if (!sLoggedLimit) {
			sLoggedLimit = true;
			Log::GetInstance()->logOutput("[TextureManager] SRV descriptor heap is full; falling back to transient preview slot.\n");
		}
		return LoadPreviewTexture(filePath);
	}

	ConvertStrings convertString;
	DirectX::ScratchImage image{};
	std::wstring filePathW = convertString.ConvertString(filePath);

	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), linear ? DirectX::WIC_FLAGS_IGNORE_SRGB : DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	if (FAILED(hr)) {
		hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
	}
	if (FAILED(hr)) {
		Log::GetInstance()->logOutput(std::format("Failed to decode texture: {}\n", filePath));
		return defaultWhiteHandle_;
	}

	// If cubemap DDS is loaded as a 2D texture, extract first face for 2D SRV compatibility
	if (image.GetMetadata().IsCubemap()) {
		const DirectX::Image* face0 = image.GetImage(0, 0, 0);
		if (face0) {
			DirectX::ScratchImage faceImg{};
			if (SUCCEEDED(faceImg.InitializeFromImage(*face0))) {
				image = std::move(faceImg);
			}
		}
	}

	// Ensure standard uncompressed 32-bit RGBA format for non-compressed images
	if (!DirectX::IsCompressed(image.GetMetadata().format) &&
		image.GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM &&
		image.GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
		image.GetMetadata().format != DXGI_FORMAT_B8G8R8A8_UNORM &&
		image.GetMetadata().format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
		DirectX::ScratchImage converted{};
		if (SUCCEEDED(DirectX::Convert(
			image.GetImages(), image.GetImageCount(), image.GetMetadata(),
			linear ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted))) {
			image = std::move(converted);
		}
	}

	uint32_t candidateHandle = dxCommon_->PeekSrvIndex();
	D3D12_CPU_DESCRIPTOR_HANDLE srvCPUHandle = dxCommon_->GetSrvCPUDescriptorHandle(candidateHandle);
	if (srvCPUHandle.ptr == 0) {
		return defaultWhiteHandle_;
	}

	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), linear ? DirectX::TEX_FILTER_LINEAR : DirectX::TEX_FILTER_SRGB, 0, mipImages);
	const DirectX::ScratchImage& finalImg = SUCCEEDED(hr) ? mipImages : image;
	const DirectX::TexMetadata& metadata = finalImg.GetMetadata();

	Microsoft::WRL::ComPtr<ID3D12Resource> resource = CreateTextureResource(metadata);
	if (!resource) {
		return defaultWhiteHandle_;
	}

	uploadAllocator_->Reset();
	uploadCommandList_->Reset(uploadAllocator_.Get(), nullptr);

	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(resource.Get(), finalImg);
	if (!intermediateResource) {
		uploadCommandList_->Close();
		return defaultWhiteHandle_;
	}

	uploadCommandList_->Close();
	ID3D12CommandList* commandLists[] = { uploadCommandList_.Get() };
	dxCommon_->GetCommandQueue()->ExecuteCommandLists(1, commandLists);
	dxCommon_->SignalAndWaitFence();

	if (dxCommon_->IsDeviceLost()) {
		return defaultWhiteHandle_;
	}

	uint32_t handle = dxCommon_->AllocateSrvIndex();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	dxCommon_->GetDevice()->CreateShaderResourceView(resource.Get(), &srvDesc, srvCPUHandle);

	textureResources_[handle] = resource;
	textureMetadatas_[handle] = metadata;
	textureCache_[normKey] = handle;

	return handle;
}

uint32_t TextureManager::GetDefaultNormalTexture()
{
	if (defaultNormalHandle_ == 0) {
		const uint8_t flat[4] = { 128, 128, 255, 255 };
		defaultNormalHandle_ = CreateTextureFromRGBA("defaultNormal", flat, 1, 1);
	}
	return defaultNormalHandle_;
}

uint32_t TextureManager::CreateTextureFromRGBA(const std::string& cacheKey, const uint8_t* rgba, uint32_t width, uint32_t height)
{
	if (!dxCommon_ || dxCommon_->IsDeviceLost() || !rgba || width == 0 || height == 0) {
		return defaultWhiteHandle_;
	}
	const std::string key = "memory:" + cacheKey;
	if (auto it = textureCache_.find(key); it != textureCache_.end()) {
		return it->second;
	}
	if (!dxCommon_->HasFreeSrvIndexForTexture()) {
		static bool sLoggedLimit = false;
		if (!sLoggedLimit) {
			sLoggedLimit = true;
			Log::GetInstance()->logOutput("[TextureManager] SRV descriptor heap is full; generated textures use the default white texture.\n");
		}
		return defaultWhiteHandle_;
	}

	DirectX::ScratchImage image;
	if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1))) {
		return defaultWhiteHandle_;
	}
	const DirectX::Image* img = image.GetImage(0, 0, 0);
	if (!img || !img->pixels) {
		return defaultWhiteHandle_;
	}
	for (uint32_t y = 0; y < height; ++y) {
		memcpy(img->pixels + y * img->rowPitch, rgba + static_cast<size_t>(y) * width * 4, static_cast<size_t>(width) * 4);
	}

	Microsoft::WRL::ComPtr<ID3D12Resource> resource = CreateTextureResource(image.GetMetadata());
	if (!resource) {
		return defaultWhiteHandle_;
	}
	uploadAllocator_->Reset();
	uploadCommandList_->Reset(uploadAllocator_.Get(), nullptr);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(resource.Get(), image);
	uploadCommandList_->Close();
	if (!intermediateResource) {
		return defaultWhiteHandle_;
	}
	ID3D12CommandList* commandLists[] = { uploadCommandList_.Get() };
	dxCommon_->GetCommandQueue()->ExecuteCommandLists(1, commandLists);
	dxCommon_->SignalAndWaitFence();
	if (dxCommon_->IsDeviceLost()) {
		return defaultWhiteHandle_;
	}

	const uint32_t handle = dxCommon_->AllocateSrvIndex();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	D3D12_CPU_DESCRIPTOR_HANDLE srvCPUHandle = dxCommon_->GetSrvDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
	srvCPUHandle.ptr += dxCommon_->GetSrvDescriptorSize() * handle;
	dxCommon_->GetDevice()->CreateShaderResourceView(resource.Get(), &srvDesc, srvCPUHandle);

	textureResources_[handle] = resource;
	textureMetadatas_[handle] = image.GetMetadata();
	textureCache_[key] = handle;
	return handle;
}

bool TextureManager::UpdateTextureFromRGBA(uint32_t textureHandle, const uint8_t* rgba, uint32_t width, uint32_t height)
{
	if (!dxCommon_ || dxCommon_->IsDeviceLost() || !rgba || width == 0 || height == 0 || textureHandle == defaultWhiteHandle_) {
		return false;
	}
	if (textureHandle >= textureResources_.size() || !textureResources_[textureHandle]) {
		return false;
	}

	DirectX::ScratchImage image;
	if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1))) {
		return false;
	}
	const DirectX::Image* img = image.GetImage(0, 0, 0);
	if (!img || !img->pixels) {
		return false;
	}
	for (uint32_t y = 0; y < height; ++y) {
		memcpy(img->pixels + y * img->rowPitch, rgba + static_cast<size_t>(y) * width * 4, static_cast<size_t>(width) * 4);
	}

	// A fresh resource instead of writing the old one: earlier frames may still sample it
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = CreateTextureResource(image.GetMetadata());
	if (!resource) {
		return false;
	}
	uploadAllocator_->Reset();
	uploadCommandList_->Reset(uploadAllocator_.Get(), nullptr);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(resource.Get(), image);
	uploadCommandList_->Close();
	if (!intermediateResource) {
		return false;
	}
	ID3D12CommandList* commandLists[] = { uploadCommandList_.Get() };
	dxCommon_->GetCommandQueue()->ExecuteCommandLists(1, commandLists);
	// Also drains the frames in flight, so rewriting the descriptor below cannot race the GPU
	dxCommon_->SignalAndWaitFence();
	if (dxCommon_->IsDeviceLost()) {
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	D3D12_CPU_DESCRIPTOR_HANDLE srvCPUHandle = dxCommon_->GetSrvDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
	srvCPUHandle.ptr += dxCommon_->GetSrvDescriptorSize() * textureHandle;
	dxCommon_->GetDevice()->CreateShaderResourceView(resource.Get(), &srvDesc, srvCPUHandle);

	dxCommon_->DeferRelease(textureResources_[textureHandle]);
	textureResources_[textureHandle] = resource;
	textureMetadatas_[textureHandle] = image.GetMetadata();
	return true;
}

uint32_t TextureManager::LoadPreviewTexture(const std::string& filePath)
{
	if (!dxCommon_ || dxCommon_->IsDeviceLost()) {
		return defaultWhiteHandle_;
	}

	std::string normKey = NormalizePath(filePath);

	auto it = textureCache_.find(normKey);
	if (it != textureCache_.end()) {
		return it->second;
	}

	if (normKey == currentPreviewTexturePath_ && previewTextureResource_) {
		return currentPreviewSrvIndex_;
	}

	if (!std::filesystem::exists(filePath)) {
		return defaultWhiteHandle_;
	}

	ConvertStrings convertString;
	DirectX::ScratchImage image{};
	std::wstring filePathW = convertString.ConvertString(filePath);

	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	if (FAILED(hr)) {
		hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
	}
	if (FAILED(hr)) {
		return defaultWhiteHandle_;
	}

	if (image.GetMetadata().IsCubemap()) {
		const DirectX::Image* face0 = image.GetImage(0, 0, 0);
		if (face0) {
			DirectX::ScratchImage faceImg{};
			if (SUCCEEDED(faceImg.InitializeFromImage(*face0))) {
				image = std::move(faceImg);
			}
		}
	}

	if (!DirectX::IsCompressed(image.GetMetadata().format) &&
		image.GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM &&
		image.GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
		image.GetMetadata().format != DXGI_FORMAT_B8G8R8A8_UNORM &&
		image.GetMetadata().format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
		DirectX::ScratchImage converted{};
		if (SUCCEEDED(DirectX::Convert(
			image.GetImages(), image.GetImageCount(), image.GetMetadata(),
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted))) {
			image = std::move(converted);
		}
	}

	ReleasePreviewTexture();

	uint32_t targetSrvIndex = kPreviewSrvBaseIndex + (previewSrvRingOffset_ % kPreviewSrvSlotCount);
	previewSrvRingOffset_ = (previewSrvRingOffset_ + 1) % kPreviewSrvSlotCount;

	D3D12_CPU_DESCRIPTOR_HANDLE srvCPUHandle = dxCommon_->GetSrvCPUDescriptorHandle(targetSrvIndex);
	if (srvCPUHandle.ptr == 0) {
		return defaultWhiteHandle_;
	}

	const DirectX::TexMetadata& metadata = image.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = CreateTextureResource(metadata);
	if (!resource) {
		return defaultWhiteHandle_;
	}

	uploadAllocator_->Reset();
	uploadCommandList_->Reset(uploadAllocator_.Get(), nullptr);

	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(resource.Get(), image);
	if (!intermediateResource) {
		uploadCommandList_->Close();
		return defaultWhiteHandle_;
	}

	uploadCommandList_->Close();
	ID3D12CommandList* commandLists[] = { uploadCommandList_.Get() };
	dxCommon_->GetCommandQueue()->ExecuteCommandLists(1, commandLists);
	dxCommon_->SignalAndWaitFence();

	if (dxCommon_->IsDeviceLost()) {
		return defaultWhiteHandle_;
	}

	// Preview textures are only displayed by ImGui, which renders to a non-sRGB target:
	// read the raw sRGB-encoded bytes (no decode) so the preview matches the source image.
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DirectX::MakeLinear(metadata.format);
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	dxCommon_->GetDevice()->CreateShaderResourceView(resource.Get(), &srvDesc, srvCPUHandle);

	previewTextureResource_ = resource;
	previewTextureMetadata_ = metadata;
	currentPreviewTexturePath_ = normKey;
	currentPreviewSrvIndex_ = targetSrvIndex;
	return currentPreviewSrvIndex_;
}

void TextureManager::ReleasePreviewTexture()
{
	if (previewTextureResource_) {
		if (dxCommon_) {
			dxCommon_->DeferRelease(previewTextureResource_);
		}
		previewTextureResource_.Reset();
		currentPreviewTexturePath_.clear();
	}
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetGPUHandle(uint32_t textureHandle)
{
	if (!dxCommon_) {
		return D3D12_GPU_DESCRIPTOR_HANDLE{ 0 };
	}
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = dxCommon_->GetSrvGPUDescriptorHandle(textureHandle);
	if (gpuHandle.ptr == 0 && defaultWhiteHandle_ != 0 && textureHandle != defaultWhiteHandle_) {
		return dxCommon_->GetSrvGPUDescriptorHandle(defaultWhiteHandle_);
	}
	return gpuHandle;
}

const DirectX::TexMetadata& TextureManager::GetMetaData(uint32_t textureHandle) const
{
	if (textureHandle < textureResources_.size() && textureResources_[textureHandle] != nullptr) {
		return textureMetadatas_[textureHandle];
	}
	if (textureHandle >= kPreviewSrvBaseIndex &&
		textureHandle < (kPreviewSrvBaseIndex + kPreviewSrvSlotCount) &&
		previewTextureResource_ != nullptr) {
		return previewTextureMetadata_;
	}
	if (defaultWhiteHandle_ < textureResources_.size() && textureResources_[defaultWhiteHandle_] != nullptr) {
		return textureMetadatas_[defaultWhiteHandle_];
	}
	return fallbackMetadata_;
}

const DirectX::TexMetadata& TextureManager::GetMetaData(const std::string& filePath)
{
	uint32_t handle = LoadTexture(filePath);
	return GetMetaData(handle);
}

Vector2 TextureManager::GetTextureSize(uint32_t textureHandle) const
{
	const DirectX::TexMetadata& metadata = GetMetaData(textureHandle);
	return Vector2{
		static_cast<float>(metadata.width),
		static_cast<float>(metadata.height)
	};
}

uint32_t TextureManager::GetTextureHandleFromGPUDescriptor(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) const
{
	if (!dxCommon_ || gpuHandle.ptr == 0 || !dxCommon_->GetSrvDescriptorHeap()) {
		return 0;
	}
	D3D12_GPU_DESCRIPTOR_HANDLE baseHandle = dxCommon_->GetSrvDescriptorHeap()->GetGPUDescriptorHandleForHeapStart();
	uint32_t descriptorSize = dxCommon_->GetSrvDescriptorSize();
	if (descriptorSize == 0 || gpuHandle.ptr < baseHandle.ptr) {
		return 0;
	}
	uint64_t diff = gpuHandle.ptr - baseHandle.ptr;
	if (diff % descriptorSize != 0) {
		return 0;
	}
	uint32_t index = static_cast<uint32_t>(diff / descriptorSize);
	if (index < textureResources_.size() && textureResources_[index] != nullptr) {
		return index;
	}
	if (index >= kPreviewSrvBaseIndex &&
		index < (kPreviewSrvBaseIndex + kPreviewSrvSlotCount) &&
		previewTextureResource_ != nullptr) {
		return index;
	}
	return 0;
}

void TextureManager::SetDescriptorHeaps(ID3D12GraphicsCommandList* cmdList)
{
	if (!dxCommon_ || dxCommon_->IsDeviceLost()) return;
	ID3D12DescriptorHeap* descriptorHeaps[] = { dxCommon_->GetSrvDescriptorHeap() };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
}

Microsoft::WRL::ComPtr<ID3D12Resource> TextureManager::CreateTextureResource(const DirectX::TexMetadata& metadata)
{
	if (!dxCommon_ || dxCommon_->IsDeviceLost()) {
		return nullptr;
	}

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width);
	resourceDesc.Height = UINT(metadata.height);
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
	resourceDesc.Format = metadata.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&resource)
	);
	if (FAILED(hr)) {
		if (!dxCommon_->CheckDeviceLost(hr, "TextureManager::CreateTextureResource")) {
			Log::GetInstance()->logOutput(std::format("[TextureManager] CreateCommittedResource failed (HRESULT=0x{:08X}).\n", static_cast<uint32_t>(hr)));
		}
		return nullptr;
	}
	return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> TextureManager::UploadTextureData(ID3D12Resource* textureResource, const DirectX::ScratchImage& mipImages)
{
	if (!textureResource || !dxCommon_ || dxCommon_->IsDeviceLost()) {
		return nullptr;
	}
	ID3D12Device* device = dxCommon_->GetDevice();
	ID3D12GraphicsCommandList* commandList = uploadCommandList_.Get();
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	HRESULT hr = DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
	if (FAILED(hr) || subresources.empty()) {
		dxCommon_->CheckDeviceLost(hr, "TextureManager::PrepareUpload");
		return nullptr;
	}
	uint64_t intermediateSize = GetRequiredIntermediateSize(textureResource, 0, UINT(subresources.size()));
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = dxCommon_->CreateBufferResource(intermediateSize);
	if (!intermediateResource) {
		return nullptr;
	}
	UpdateSubresources(commandList, textureResource, intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data());
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = textureResource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList->ResourceBarrier(1, &barrier);
	return intermediateResource;
}

uint32_t TextureManager::CreateDefaultCubemapTexture()
{
	if (defaultCubemapHandle_ != 0) return defaultCubemapHandle_;

	DirectX::ScratchImage cubeImg{};
	HRESULT hr = cubeImg.InitializeCube(DXGI_FORMAT_R8G8B8A8_UNORM, 16, 16, 1, 1);
	if (FAILED(hr)) return 0;

	// Fill faces with pleasant sky colors
	for (size_t f = 0; f < 6; ++f) {
		const DirectX::Image* img = cubeImg.GetImage(0, f, 0);
		if (!img) continue;
		for (size_t y = 0; y < img->height; ++y) {
			float v = static_cast<float>(y) / static_cast<float>(img->height - 1);
			for (size_t x = 0; x < img->width; ++x) {
				size_t idx = (y * img->rowPitch) + (x * 4);
				if (f == 2) { // Top (+Y)
					img->pixels[idx + 0] = 55;
					img->pixels[idx + 1] = 115;
					img->pixels[idx + 2] = 200;
					img->pixels[idx + 3] = 255;
				} else if (f == 3) { // Bottom (-Y)
					img->pixels[idx + 0] = 90;
					img->pixels[idx + 1] = 95;
					img->pixels[idx + 2] = 100;
					img->pixels[idx + 3] = 255;
				} else { // Sides (+X, -X, +Z, -Z)
					uint8_t r = static_cast<uint8_t>(55.0f * (1.0f - v) + 185.0f * v);
					uint8_t g = static_cast<uint8_t>(115.0f * (1.0f - v) + 210.0f * v);
					uint8_t b = static_cast<uint8_t>(200.0f * (1.0f - v) + 245.0f * v);
					img->pixels[idx + 0] = r;
					img->pixels[idx + 1] = g;
					img->pixels[idx + 2] = b;
					img->pixels[idx + 3] = 255;
				}
			}
		}
	}

	uint32_t handle = dxCommon_->AllocateSrvIndex();
	defaultCubemapHandle_ = handle;

	Microsoft::WRL::ComPtr<ID3D12Resource> resource = CreateTextureResource(cubeImg.GetMetadata());

	uploadAllocator_->Reset();
	uploadCommandList_->Reset(uploadAllocator_.Get(), nullptr);

	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(resource.Get(), cubeImg);

	uploadCommandList_->Close();
	ID3D12CommandList* commandLists[] = { uploadCommandList_.Get() };
	dxCommon_->GetCommandQueue()->ExecuteCommandLists(1, commandLists);
	dxCommon_->SignalAndWaitFence();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MipLevels = 1;

	D3D12_CPU_DESCRIPTOR_HANDLE srvCPUHandle = dxCommon_->GetSrvDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
	srvCPUHandle.ptr += dxCommon_->GetSrvDescriptorSize() * handle;
	dxCommon_->GetDevice()->CreateShaderResourceView(resource.Get(), &srvDesc, srvCPUHandle);

	textureResources_[handle] = resource;
	textureMetadatas_[handle] = cubeImg.GetMetadata();
	return handle;
}

uint32_t TextureManager::LoadCubemapTexture(const std::string& filePath)
{
	auto it = textureCache_.find(filePath);
	if (it != textureCache_.end()) {
		return it->second;
	}

	std::string actualPath = filePath;
	if (!std::filesystem::exists(actualPath)) {
		wchar_t exePath[MAX_PATH];
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		std::filesystem::path pExe(exePath);
		std::filesystem::path candidate = pExe.parent_path() / filePath;
		if (std::filesystem::exists(candidate)) {
			actualPath = candidate.string();
		} else {
			candidate = pExe.parent_path().parent_path() / "RizealldaySandbox" / filePath;
			if (std::filesystem::exists(candidate)) {
				actualPath = candidate.string();
			} else {
				candidate = pExe.parent_path() / "Resources" / filePath;
				if (std::filesystem::exists(candidate)) {
					actualPath = candidate.string();
				} else {
					Log::GetInstance()->logOutput(std::format("Cubemap file not found: {}\n", filePath));
					return 0;
				}
			}
		}
	}

	if (!dxCommon_->HasFreeSrvIndexForTexture()) {
		Log::GetInstance()->logOutput("Texture limit exceeded when loading cubemap\n");
		return 0;
	}

	ConvertStrings convertString;
	DirectX::ScratchImage image{};
	DirectX::TexMetadata metadata{};
	std::wstring filePathW = convertString.ConvertString(actualPath);

	HRESULT hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
	if (FAILED(hr)) {
		Log::GetInstance()->logOutput(std::format("Failed to load DDS cubemap: {}\n", actualPath));
		return 0;
	}

	if (!metadata.IsCubemap()) {
		Log::GetInstance()->logOutput(std::format("DDS file is not a cubemap: {}\n", actualPath));
		return 0;
	}

	uint32_t handle = dxCommon_->AllocateSrvIndex();
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = CreateTextureResource(metadata);

	uploadAllocator_->Reset();
	uploadCommandList_->Reset(uploadAllocator_.Get(), nullptr);

	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(resource.Get(), image);

	uploadCommandList_->Close();
	ID3D12CommandList* commandLists[] = { uploadCommandList_.Get() };
	dxCommon_->GetCommandQueue()->ExecuteCommandLists(1, commandLists);
	dxCommon_->SignalAndWaitFence();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MipLevels = UINT(metadata.mipLevels);
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

	D3D12_CPU_DESCRIPTOR_HANDLE srvCPUHandle = dxCommon_->GetSrvDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
	srvCPUHandle.ptr += dxCommon_->GetSrvDescriptorSize() * handle;
	dxCommon_->GetDevice()->CreateShaderResourceView(resource.Get(), &srvDesc, srvCPUHandle);

	textureResources_[handle] = resource;
	textureMetadatas_[handle] = metadata;
	textureCache_[filePath] = handle;
	return handle;
}

void TextureManager::CreateProceduralSkyboxDDS(const std::string& outPath)
{
	if (std::filesystem::exists(outPath)) return;

	std::filesystem::path p(outPath);
	if (p.has_parent_path() && !std::filesystem::exists(p.parent_path())) {
		std::filesystem::create_directories(p.parent_path());
	}

	DirectX::ScratchImage cubeImg{};
	if (SUCCEEDED(cubeImg.InitializeCube(DXGI_FORMAT_R8G8B8A8_UNORM, 64, 64, 1, 1))) {
		for (size_t f = 0; f < 6; ++f) {
			const DirectX::Image* img = cubeImg.GetImage(0, f, 0);
			if (!img) continue;
			for (size_t y = 0; y < img->height; ++y) {
				float v = static_cast<float>(y) / static_cast<float>(img->height - 1);
				for (size_t x = 0; x < img->width; ++x) {
					size_t idx = (y * img->rowPitch) + (x * 4);
					if (f == 2) { // Top (+Y)
						img->pixels[idx + 0] = 55;
						img->pixels[idx + 1] = 115;
						img->pixels[idx + 2] = 200;
						img->pixels[idx + 3] = 255;
					} else if (f == 3) { // Bottom (-Y)
						img->pixels[idx + 0] = 90;
						img->pixels[idx + 1] = 95;
						img->pixels[idx + 2] = 100;
						img->pixels[idx + 3] = 255;
					} else { // Sides (+X, -X, +Z, -Z)
						uint8_t r = static_cast<uint8_t>(55.0f * (1.0f - v) + 185.0f * v);
						uint8_t g = static_cast<uint8_t>(115.0f * (1.0f - v) + 210.0f * v);
						uint8_t b = static_cast<uint8_t>(200.0f * (1.0f - v) + 245.0f * v);
						img->pixels[idx + 0] = r;
						img->pixels[idx + 1] = g;
						img->pixels[idx + 2] = b;
						img->pixels[idx + 3] = 255;
					}
				}
			}
		}
		ConvertStrings cvt;
		std::wstring wOut = cvt.ConvertString(outPath);
		DirectX::SaveToDDSFile(cubeImg.GetImages(), cubeImg.GetImageCount(), cubeImg.GetMetadata(), DirectX::DDS_FLAGS_NONE, wOut.c_str());
	}
}
