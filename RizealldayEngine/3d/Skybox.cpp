#include "3d/Skybox.h"
#include "base/DirectXCommon.h"
#include "base/Profiler.h"
#include "base/DXCManager.h"
#include "base/TextureManager.h"
#include "base/Log.h"
#include "3d/Camera.h"
#include "math/MathUtil.h"
#include <cassert>

Skybox::~Skybox()
{
	if (dxCommon_) {
		if (pso_) dxCommon_->DeferRelease(pso_);
		if (rootSignature_) dxCommon_->DeferRelease(rootSignature_);
	}
	pso_.Reset();
	rootSignature_.Reset();
}

void Skybox::Initialize(DirectXCommon* dxCommon, DXCManager* dxc)
{
	dxCommon_ = dxCommon;
	CreateRootSignature();
	CreatePSO(dxc);

	// Attempt to load default cubemap if available
	if (!cubemapPath_.empty()) {
		LoadCubemap(cubemapPath_);
	}
}

void Skybox::CreateRootSignature()
{
	ID3D12Device* device = dxCommon_->GetDevice();

	D3D12_DESCRIPTOR_RANGE srvRange[1] = {};
	srvRange[0].BaseShaderRegister = 0; // t0
	srvRange[0].NumDescriptors = 1;
	srvRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// Root parameters:
	// [0] ConstantBuffer<SkyboxConstants> b0 (ALL)
	// [1] TextureCube SRV Table t0 (PIXEL)
	D3D12_ROOT_PARAMETER rootParameters[2] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[0].Descriptor.ShaderRegister = 0;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[1].DescriptorTable.pDescriptorRanges = srvRange;
	rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(srvRange);

	// Linear-Clamp Static Sampler (s0)
	D3D12_STATIC_SAMPLER_DESC staticSampler{};
	staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
	staticSampler.ShaderRegister = 0;
	staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC sigDesc{};
	sigDesc.pParameters = rootParameters;
	sigDesc.NumParameters = _countof(rootParameters);
	sigDesc.pStaticSamplers = &staticSampler;
	sigDesc.NumStaticSamplers = 1;
	sigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&sigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		if (errorBlob) {
			Log::GetInstance()->logOutput(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		}
		assert(false);
	}

	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));
}

void Skybox::CreatePSO(DXCManager* dxc)
{
	ID3D12Device* device = dxCommon_->GetDevice();

	Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxc->CompileShader(L"shaders/Skybox.VS.hlsl", L"vs_6_0", dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
	assert(vsBlob != nullptr);
	Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxc->CompileShader(L"shaders/Skybox.PS.hlsl", L"ps_6_0", dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
	assert(psBlob != nullptr);

	D3D12_RASTERIZER_DESC rasterDesc{};
	rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterDesc.DepthClipEnable = TRUE;

	D3D12_DEPTH_STENCIL_DESC depthDesc{};
	depthDesc.DepthEnable = TRUE;
	depthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // Depth write disabled
	depthDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;  // LESS_EQUAL so far-plane passes

	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = FALSE;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = rootSignature_.Get();
	psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
	psoDesc.RasterizerState = rasterDesc;
	psoDesc.DepthStencilState = depthDesc;
	psoDesc.BlendState = blendDesc;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso_));
	assert(SUCCEEDED(hr));
}

bool Skybox::LoadCubemap(const std::string& filePath)
{
	cubemapPath_ = filePath;
	uint32_t handle = TextureManager::GetInstance()->LoadCubemapTexture(filePath);
	if (handle != 0) {
		cubemapTextureHandle_ = handle;
		hasValidCubemap_ = true;
		return true;
	} else {
		hasValidCubemap_ = false;
		return false;
	}
}

void Skybox::Draw(ID3D12GraphicsCommandList* cmdList, const Camera& camera)
{
	if (!pso_ || !rootSignature_ || !dxCommon_ || dxCommon_->IsDeviceLost()) return;

	// Remove camera translation for skybox view matrix
	Matrix4x4 cameraWorldRotOnly = Mathematics::MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, camera.rotation, { 0.0f, 0.0f, 0.0f });
	Matrix4x4 viewRotOnly = Mathematics::Inverse(cameraWorldRotOnly);
	Matrix4x4 viewProjNoTranslation = Mathematics::Multiply(viewRotOnly, camera.GetProjectionMatrix());
	Matrix4x4 invViewProj = Mathematics::Inverse(viewProjNoTranslation);

	SkyboxConstants constantsData{};
	SkyboxConstants* constants = &constantsData;

	constants->invViewProj = invViewProj;
	constants->topColor = topColor_;
	constants->horizonColor = horizonColor_;
	constants->bottomColor = bottomColor_;
	constants->solidColor = solidColor_;
	constants->rotation = rotation_;
	constants->exposure = exposure_;

	SkyboxMode activeMode = mode_;
	if (activeMode == SkyboxMode::Cubemap && !hasValidCubemap_) {
		activeMode = SkyboxMode::Gradient;
	}
	constants->mode = static_cast<int32_t>(activeMode);
	constants->pad = 0.0f;

	// Bind root signature and PSO
	cmdList->SetGraphicsRootSignature(rootSignature_.Get());
	cmdList->SetPipelineState(pso_.Get());

	// Ensure SRV descriptor heap is set
	TextureManager::GetInstance()->SetDescriptorHeaps(cmdList);

	// Bind root parameters
	D3D12_GPU_VIRTUAL_ADDRESS cbvAddr = dxCommon_->UploadConstants(constantsData);
	if (cbvAddr == 0) return;
	cmdList->SetGraphicsRootConstantBufferView(0, cbvAddr);

	uint32_t texHandle = (hasValidCubemap_ && cubemapTextureHandle_ != 0)
		? cubemapTextureHandle_
		: TextureManager::GetInstance()->GetDefaultCubemapTexture();
	cmdList->SetGraphicsRootDescriptorTable(1, TextureManager::GetInstance()->GetGPUHandle(texHandle));

	// Draw fullscreen triangle (SV_VertexID generates vertices at far plane)
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(3, 1, 0, 0);
	Profiler::GetInstance()->CountDrawCall();
}
