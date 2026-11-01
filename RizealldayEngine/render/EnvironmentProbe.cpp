#include "base/DirectXCommon.h"
#include "base/DXCManager.h"
#include "render/EnvironmentProbe.h"
#include "3d/Skybox.h"
#include "base/Profiler.h"
#include <cassert>
#include <cstring>

namespace {
// D3D cube face order: +X -X +Y -Y +Z -Z. Clip +X = right, clip +Y = up, the face looks along forward.
const float kFaces[6][3][3] = {
	{ {  0, 0, -1 }, { 0, 1,  0 }, {  1,  0,  0 } },
	{ {  0, 0,  1 }, { 0, 1,  0 }, { -1,  0,  0 } },
	{ {  1, 0,  0 }, { 0, 0, -1 }, {  0,  1,  0 } },
	{ {  1, 0,  0 }, { 0, 0,  1 }, {  0, -1,  0 } },
	{ {  1, 0,  0 }, { 0, 1,  0 }, {  0,  0,  1 } },
	{ { -1, 0,  0 }, { 0, 1,  0 }, {  0,  0, -1 } },
};

Microsoft::WRL::ComPtr<ID3D12Resource> CreateCube(ID3D12Device* device, uint32_t size, uint32_t mips)
{
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = size;
	desc.Height = size;
	desc.DepthOrArraySize = 6;
	desc.MipLevels = static_cast<UINT16>(mips);
	desc.Format = DirectXCommon::kSceneColorFormat;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_CLEAR_VALUE clear{};
	clear.Format = desc.Format;
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&resource));
	return resource;
}

void Transition(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	cmdList->ResourceBarrier(1, &barrier);
}
}

EnvironmentProbe::~EnvironmentProbe()
{
	if (!dxCommon_) return;
	if (capture_) dxCommon_->DeferRelease(capture_);
	if (cube_) dxCommon_->DeferRelease(cube_);
	if (prefilter_) dxCommon_->DeferRelease(prefilter_);
	if (rootSignature_) dxCommon_->DeferRelease(rootSignature_);
	if (captureSrvIndex_ != DirectXCommon::kInvalidSrvIndex) dxCommon_->FreeSrvIndex(captureSrvIndex_);
	if (cubeSrvIndex_ != DirectXCommon::kInvalidSrvIndex) dxCommon_->FreeSrvIndex(cubeSrvIndex_);
}

void EnvironmentProbe::Initialize(DirectXCommon* dxCommon, DXCManager* dxc)
{
	dxCommon_ = dxCommon;
	ID3D12Device* device = dxCommon->GetDevice();
	capture_ = CreateCube(device, kSize, 1);
	cube_ = CreateCube(device, kSize, kMips);
	assert(capture_ && cube_);

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heapDesc.NumDescriptors = 6 + 6 * kMips;
	HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap_));
	assert(SUCCEEDED(hr));
	rtvStep_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
	const auto addRtv = [&](ID3D12Resource* resource, UINT mip, UINT face) {
		D3D12_RENDER_TARGET_VIEW_DESC desc{};
		desc.Format = DirectXCommon::kSceneColorFormat;
		desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
		desc.Texture2DArray.MipSlice = mip;
		desc.Texture2DArray.FirstArraySlice = face;
		desc.Texture2DArray.ArraySize = 1;
		device->CreateRenderTargetView(resource, &desc, rtv);
		rtv.ptr += rtvStep_;
	};
	for (UINT face = 0; face < 6; ++face) addRtv(capture_.Get(), 0, face);
	for (UINT mip = 0; mip < kMips; ++mip) {
		for (UINT face = 0; face < 6; ++face) addRtv(cube_.Get(), mip, face);
	}

	const auto addSrv = [&](ID3D12Resource* resource, UINT mips, uint32_t& index, D3D12_GPU_DESCRIPTOR_HANDLE& gpu) {
		index = dxCommon->AllocateSrvIndex();
		D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
		desc.Format = DirectXCommon::kSceneColorFormat;
		desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		desc.TextureCube.MipLevels = mips;
		device->CreateShaderResourceView(resource, &desc, dxCommon->GetSrvCPUDescriptorHandle(index));
		gpu = dxCommon->GetSrvGPUDescriptorHandle(index);
	};
	addSrv(capture_.Get(), 1, captureSrvIndex_, captureSrv_);
	addSrv(cube_.Get(), kMips, cubeSrvIndex_, srv_);

	// Root signature: b0 face + roughness, t0 captured sky
	D3D12_DESCRIPTOR_RANGE range{};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.NumDescriptors = 1;
	D3D12_ROOT_PARAMETER params[2] = {};
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[1].DescriptorTable.NumDescriptorRanges = 1;
	params[1].DescriptorTable.pDescriptorRanges = &range;
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC rootDesc{};
	rootDesc.pParameters = params;
	rootDesc.NumParameters = _countof(params);
	rootDesc.pStaticSamplers = &sampler;
	rootDesc.NumStaticSamplers = 1;
	Microsoft::WRL::ComPtr<ID3DBlob> blob, errorBlob;
	hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errorBlob);
	assert(SUCCEEDED(hr));
	hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));

	auto vs = dxc->CompileShader(L"shaders/Post.VS.hlsl", L"vs_6_0", dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
	auto ps = dxc->CompileShader(L"shaders/Environment.Prefilter.PS.hlsl", L"ps_6_0", dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
	assert(vs && ps);
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = rootSignature_.Get();
	psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState.DepthClipEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&prefilter_));
	assert(SUCCEEDED(hr));
	(void)hr;
}

void EnvironmentProbe::Update(ID3D12GraphicsCommandList* cmdList, Skybox& skybox)
{
	if (!dxCommon_ || !prefilter_) return;
	const SkyboxConstants sky = skybox.BuildConstants(Matrix4x4{});
	// Everything but the matrix decides what the sky looks like; the cube itself is in the handle
	struct Key { unsigned char constants[sizeof(SkyboxConstants) - sizeof(Matrix4x4)]; uint32_t cubemap; } key{};
	static_assert(sizeof(Key) <= sizeof(lastSky_));
	std::memcpy(key.constants, reinterpret_cast<const unsigned char*>(&sky) + sizeof(Matrix4x4), sizeof(key.constants));
	key.cubemap = skybox.GetCubemapTextureHandle();
	if (baked_ && std::memcmp(lastSky_, &key, sizeof(key)) == 0) return;
	std::memcpy(lastSky_, &key, sizeof(key));
	baked_ = true;
	++bakeCount_;

	RIZE_PROFILE_GPU(cmdList, "Environment Bake");
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

	// 1. The sky into the capture cube, with the skybox's own shader
	Transition(cmdList, capture_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	const D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, 1.0f };
	const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(kSize), static_cast<LONG>(kSize) };
	cmdList->RSSetViewports(1, &viewport);
	cmdList->RSSetScissorRects(1, &scissor);
	for (UINT face = 0; face < 6; ++face) {
		D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvStart;
		rtv.ptr += rtvStep_ * face;
		cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		// The vertex shader computes mul(float4(x, y, 1, 1), M): rows right, up, forward, w
		Matrix4x4 faceMatrix{};
		for (int row = 0; row < 3; ++row) {
			for (int column = 0; column < 3; ++column) faceMatrix.m[row][column] = kFaces[face][row][column];
		}
		faceMatrix.m[3][3] = 1.0f;
		skybox.DrawToTarget(cmdList, faceMatrix);
	}
	Transition(cmdList, capture_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	// 2. Each mip is the captured sky blurred for one roughness
	Transition(cmdList, cube_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	ID3D12DescriptorHeap* heaps[] = { dxCommon_->GetSrvDescriptorHeap() };
	cmdList->SetDescriptorHeaps(1, heaps);
	cmdList->SetGraphicsRootSignature(rootSignature_.Get());
	cmdList->SetPipelineState(prefilter_.Get());
	cmdList->SetGraphicsRootDescriptorTable(1, captureSrv_);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	for (UINT mip = 0; mip < kMips; ++mip) {
		const float mipSize = static_cast<float>(kSize >> mip);
		const D3D12_VIEWPORT mipViewport{ 0.0f, 0.0f, mipSize, mipSize, 0.0f, 1.0f };
		const D3D12_RECT mipScissor{ 0, 0, static_cast<LONG>(kSize >> mip), static_cast<LONG>(kSize >> mip) };
		cmdList->RSSetViewports(1, &mipViewport);
		cmdList->RSSetScissorRects(1, &mipScissor);
		for (UINT face = 0; face < 6; ++face) {
			Constants constants{};
			for (int i = 0; i < 3; ++i) {
				constants.right[i] = kFaces[face][0][i];
				constants.up[i] = kFaces[face][1][i];
				constants.forward[i] = kFaces[face][2][i];
			}
			constants.roughness = static_cast<float>(mip) / static_cast<float>(kMips - 1);
			const D3D12_GPU_VIRTUAL_ADDRESS address = dxCommon_->UploadConstants(constants);
			if (address == 0) continue;
			D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvStart;
			rtv.ptr += rtvStep_ * (6 + mip * 6 + face);
			cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
			cmdList->SetGraphicsRootConstantBufferView(0, address);
			cmdList->DrawInstanced(3, 1, 0, 0);
		}
	}
	Transition(cmdList, cube_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}
