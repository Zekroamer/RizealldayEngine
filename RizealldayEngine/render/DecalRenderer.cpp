#include "render/DecalRenderer.h"
#include "3d/Camera.h"
#include "base/DirectXCommon.h"
#include "base/DXCManager.h"
#include "base/Log.h"
#include "base/TextureManager.h"
#include "math/MathUtil.h"
#include <cassert>

namespace {
	struct DecalConstants {
		Matrix4x4 viewProjection;
		Matrix4x4 inverseViewProjection;
		Matrix4x4 world;
		Matrix4x4 inverseWorld;
		Vector4 color;
		float targetWidth, targetHeight, edgeFade, pad;
	};
}

DecalRenderer* DecalRenderer::GetInstance()
{
	static DecalRenderer instance;
	return &instance;
}

void DecalRenderer::Initialize(DirectXCommon* dxCommon, DXCManager* dxc)
{
	dxCommon_ = dxCommon;
	ID3D12Device* device = dxCommon_->GetDevice();

	D3D12_DESCRIPTOR_RANGE ranges[2] = {};
	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[0].NumDescriptors = 1;
	ranges[0].BaseShaderRegister = 0;
	ranges[1] = ranges[0];
	ranges[1].BaseShaderRegister = 1;
	D3D12_ROOT_PARAMETER parameters[3] = {};
	parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	for (int i = 0; i < 2; ++i) {
		parameters[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		parameters[i + 1].DescriptorTable.NumDescriptorRanges = 1;
		parameters[i + 1].DescriptorTable.pDescriptorRanges = &ranges[i];
	}
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC signatureDesc{};
	signatureDesc.pParameters = parameters;
	signatureDesc.NumParameters = _countof(parameters);
	signatureDesc.pStaticSamplers = &sampler;
	signatureDesc.NumStaticSamplers = 1;
	Microsoft::WRL::ComPtr<ID3DBlob> blob;
	Microsoft::WRL::ComPtr<ID3DBlob> error;
	HRESULT hr = D3D12SerializeRootSignature(&signatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
	if (FAILED(hr)) {
		if (error) Log::GetInstance()->logOutput(reinterpret_cast<char*>(error->GetBufferPointer()));
		assert(false);
		return;
	}
	hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&signature_));
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDxcBlob> vs = dxc->CompileShader(L"shaders/Decal.VS.hlsl", L"vs_6_0", dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
	Microsoft::WRL::ComPtr<IDxcBlob> ps = dxc->CompileShader(L"shaders/Decal.PS.hlsl", L"ps_6_0", dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
	assert(vs && ps);
	if (!vs || !ps) return;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = signature_.Get();
	desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; // the box winds counter-clockwise from outside, so this keeps the far faces: still drawn when the camera is inside
	desc.RasterizerState.DepthClipEnable = FALSE; // a box reaching past the far plane still paints
	auto& blend = desc.BlendState.RenderTarget[0];
	blend.BlendEnable = TRUE;
	blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blend.BlendOp = D3D12_BLEND_OP_ADD;
	blend.SrcBlendAlpha = D3D12_BLEND_ZERO; // the editor shows this target with alpha: keep what is there
	blend.DestBlendAlpha = D3D12_BLEND_ONE;
	blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
	desc.SampleDesc.Count = 1;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&alphaPso_));
	assert(SUCCEEDED(hr));
	blend.DestBlend = D3D12_BLEND_ONE;
	hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&additivePso_));
	assert(SUCCEEDED(hr));
}

void DecalRenderer::Finalize()
{
	// Called after the GPU has been flushed (RizeEngine::Finalize)
	alphaPso_.Reset();
	additivePso_.Reset();
	signature_.Reset();
	dxCommon_ = nullptr;
}

void DecalRenderer::Draw(ID3D12GraphicsCommandList* cmdList, const Camera& camera, D3D12_GPU_DESCRIPTOR_HANDLE depth, uint32_t width, uint32_t height, const std::vector<DecalDraw>& decals)
{
	if (decals.empty() || !dxCommon_ || !alphaPso_ || !additivePso_) return;
	TextureManager::GetInstance()->SetDescriptorHeaps(cmdList);
	cmdList->SetGraphicsRootSignature(signature_.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->SetGraphicsRootDescriptorTable(1, depth);
	const Matrix4x4 inverseViewProjection = Mathematics::Inverse(camera.GetViewProjectionMatrix());
	for (const DecalDraw& decal : decals) {
		DecalConstants constants{};
		constants.viewProjection = camera.GetViewProjectionMatrix();
		constants.inverseViewProjection = inverseViewProjection;
		constants.world = decal.world;
		constants.inverseWorld = Mathematics::Inverse(decal.world);
		constants.color = decal.color;
		constants.targetWidth = static_cast<float>(width);
		constants.targetHeight = static_cast<float>(height);
		constants.edgeFade = decal.edgeFade;
		const D3D12_GPU_VIRTUAL_ADDRESS address = dxCommon_->UploadConstants(constants);
		if (address == 0) continue;
		cmdList->SetPipelineState(decal.additive ? additivePso_.Get() : alphaPso_.Get());
		cmdList->SetGraphicsRootConstantBufferView(0, address);
		cmdList->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetGPUHandle(decal.textureHandle));
		cmdList->DrawInstanced(36, 1, 0, 0);
	}
}
