#include "render/GpuParticles.h"
#include "3d/Camera.h"
#include "base/DirectXCommon.h"
#include "base/DXCManager.h"
#include "base/Log.h"
#include "base/TextureManager.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
	constexpr uint32_t kParticleBytes = 32; // Particle in GpuParticle.CS.hlsl
	constexpr uint32_t kMaxCapacity = 1000000;
	constexpr uint32_t kThreadGroupSize = 64;

	struct ComputeConstants {
		Vector3 origin; float deltaTime;
		Vector3 direction; float cosSpread;
		Vector3 gravity; float drag;
		float lifeMin, lifeMax, speedMin, speedMax;
		uint32_t emitStart, emitCount, capacity, seed;
	};

	struct DrawConstants {
		Matrix4x4 viewProjection;
		Vector3 cameraRight; float sizeStart;
		Vector3 cameraUp; float sizeEnd;
		Vector4 colorStart;
		Vector4 colorEnd;
	};

	Microsoft::WRL::ComPtr<ID3D12RootSignature> MakeSignature(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& desc)
	{
		Microsoft::WRL::ComPtr<ID3DBlob> blob;
		Microsoft::WRL::ComPtr<ID3DBlob> error;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> signature;
		if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error))) {
			if (error) Log::GetInstance()->logOutput(reinterpret_cast<char*>(error->GetBufferPointer()));
			assert(false);
			return signature;
		}
		const HRESULT hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&signature));
		assert(SUCCEEDED(hr));
		(void)hr;
		return signature;
	}

	Vector3 NormalizedOrUp(const Vector3& v)
	{
		const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		if (!(length > 1.0e-5f) || !std::isfinite(length)) return { 0.0f, 1.0f, 0.0f };
		return { v.x / length, v.y / length, v.z / length };
	}
}

GpuParticles* GpuParticles::GetInstance()
{
	static GpuParticles instance;
	return &instance;
}

void GpuParticles::Initialize(DirectXCommon* dxCommon, DXCManager* dxc)
{
	dxCommon_ = dxCommon;
	ID3D12Device* device = dxCommon_->GetDevice();
	const auto compile = [&](const wchar_t* file, const wchar_t* profile) {
		return dxc->CompileShader(file, profile, dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
	};

	// Compute: constants + the particle buffer
	D3D12_ROOT_PARAMETER computeParameters[2] = {};
	computeParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	computeParameters[0].Constants.Num32BitValues = sizeof(ComputeConstants) / 4;
	computeParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
	D3D12_ROOT_SIGNATURE_DESC computeDesc{};
	computeDesc.pParameters = computeParameters;
	computeDesc.NumParameters = _countof(computeParameters);
	computeSignature_ = MakeSignature(device, computeDesc);
	Microsoft::WRL::ComPtr<IDxcBlob> cs = compile(L"shaders/GpuParticle.CS.hlsl", L"cs_6_0");
	assert(cs);
	if (!cs) return;
	D3D12_COMPUTE_PIPELINE_STATE_DESC computePso{};
	computePso.pRootSignature = computeSignature_.Get();
	computePso.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
	HRESULT hr = device->CreateComputePipelineState(&computePso, IID_PPV_ARGS(&computePso_));
	assert(SUCCEEDED(hr));

	// Draw: constants, particles (vertex shader), texture
	D3D12_DESCRIPTOR_RANGE range{};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.NumDescriptors = 1;
	range.BaseShaderRegister = 1;
	D3D12_ROOT_PARAMETER drawParameters[3] = {};
	drawParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	drawParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	drawParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	drawParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	drawParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	drawParameters[2].DescriptorTable.NumDescriptorRanges = 1;
	drawParameters[2].DescriptorTable.pDescriptorRanges = &range;
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC drawDesc{};
	drawDesc.pParameters = drawParameters;
	drawDesc.NumParameters = _countof(drawParameters);
	drawDesc.pStaticSamplers = &sampler;
	drawDesc.NumStaticSamplers = 1;
	drawSignature_ = MakeSignature(device, drawDesc);

	Microsoft::WRL::ComPtr<IDxcBlob> vs = compile(L"shaders/GpuParticle.VS.hlsl", L"vs_6_0");
	Microsoft::WRL::ComPtr<IDxcBlob> ps = compile(L"shaders/GpuParticle.PS.hlsl", L"ps_6_0");
	assert(vs && ps);
	if (!vs || !ps) return;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = drawSignature_.Get();
	desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.RasterizerState.DepthClipEnable = TRUE;
	desc.DepthStencilState.DepthEnable = TRUE;
	desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
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
	desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.SampleDesc.Count = 1;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&alphaPso_));
	assert(SUCCEEDED(hr));
	blend.DestBlend = D3D12_BLEND_ONE;
	hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&additivePso_));
	assert(SUCCEEDED(hr));
}

void GpuParticles::Finalize()
{
	// Called after the GPU has been flushed (RizeEngine::Finalize)
	emitters_.clear();
	computePso_.Reset();
	computeSignature_.Reset();
	alphaPso_.Reset();
	additivePso_.Reset();
	drawSignature_.Reset();
	dxCommon_ = nullptr;
}

void GpuParticles::Simulate(ID3D12GraphicsCommandList* cmdList, uint64_t id, const Vector3& origin, float deltaTime, uint32_t emitCount, const GpuParticleSettings& settings)
{
	if (!dxCommon_ || !computePso_) return;
	const uint32_t capacity = std::clamp(settings.capacity, 1u, kMaxCapacity);
	Emitter& emitter = emitters_[id];
	if (!emitter.buffer || emitter.settings.capacity != capacity) {
		if (emitter.buffer) dxCommon_->DeferRelease(emitter.buffer);
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC resource{};
		resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resource.Width = static_cast<UINT64>(capacity) * kParticleBytes;
		resource.Height = 1;
		resource.DepthOrArraySize = 1;
		resource.MipLevels = 1;
		resource.SampleDesc.Count = 1;
		resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		emitter.buffer.Reset();
		// Committed resources start zeroed: age 0, life 0 = dead
		if (FAILED(dxCommon_->GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&emitter.buffer)))) {
			emitters_.erase(id);
			return;
		}
		emitter.ringHead = 0;
		emitter.isShaderResource = false;
	}
	emitter.settings = settings;
	emitter.settings.capacity = capacity;
	emitter.simulated = true;

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = emitter.buffer.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = emitter.isShaderResource ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COMMON;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	cmdList->ResourceBarrier(1, &barrier);

	const uint32_t count = (std::min)(emitCount, capacity);
	ComputeConstants constants{};
	constants.origin = origin;
	constants.deltaTime = (std::max)(deltaTime, 0.0f);
	constants.direction = NormalizedOrUp(settings.direction);
	constants.cosSpread = std::cos(std::clamp(settings.spreadDegrees, 0.0f, 180.0f) * 3.14159265f / 180.0f);
	constants.gravity = settings.gravity;
	constants.drag = (std::max)(settings.drag, 0.0f);
	constants.lifeMin = (std::max)(settings.lifeMin, 0.001f);
	constants.lifeMax = (std::max)(settings.lifeMax, constants.lifeMin);
	constants.speedMin = settings.speedMin;
	constants.speedMax = settings.speedMax;
	constants.emitStart = emitter.ringHead;
	constants.emitCount = count;
	constants.capacity = capacity;
	constants.seed = emitter.seed;
	emitter.seed = emitter.seed * 1664525u + 1013904223u;
	emitter.ringHead = (emitter.ringHead + count) % capacity;

	cmdList->SetComputeRootSignature(computeSignature_.Get());
	cmdList->SetPipelineState(computePso_.Get());
	cmdList->SetComputeRoot32BitConstants(0, sizeof(ComputeConstants) / 4, &constants, 0);
	cmdList->SetComputeRootUnorderedAccessView(1, emitter.buffer->GetGPUVirtualAddress());
	cmdList->Dispatch((capacity + kThreadGroupSize - 1) / kThreadGroupSize, 1, 1);

	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	cmdList->ResourceBarrier(1, &barrier);
	emitter.isShaderResource = true;
}

void GpuParticles::EndFrame()
{
	for (auto it = emitters_.begin(); it != emitters_.end();) {
		if (it->second.simulated) {
			it->second.simulated = false;
			++it;
		} else {
			dxCommon_->DeferRelease(it->second.buffer);
			it = emitters_.erase(it);
		}
	}
}

void GpuParticles::Clear()
{
	if (!dxCommon_) return;
	for (auto& [id, emitter] : emitters_) dxCommon_->DeferRelease(emitter.buffer);
	emitters_.clear();
}

void GpuParticles::Draw(ID3D12GraphicsCommandList* cmdList, const Camera& camera)
{
	if (emitters_.empty() || !dxCommon_ || !alphaPso_ || !additivePso_) return;
	const Matrix4x4& view = camera.GetViewMatrix();
	TextureManager::GetInstance()->SetDescriptorHeaps(cmdList);
	cmdList->SetGraphicsRootSignature(drawSignature_.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	for (const auto& [id, emitter] : emitters_) {
		if (!emitter.isShaderResource) continue;
		DrawConstants constants{};
		constants.viewProjection = camera.GetViewProjectionMatrix();
		constants.cameraRight = { view.m[0][0], view.m[1][0], view.m[2][0] };
		constants.cameraUp = { view.m[0][1], view.m[1][1], view.m[2][1] };
		constants.sizeStart = emitter.settings.sizeStart;
		constants.sizeEnd = emitter.settings.sizeEnd;
		constants.colorStart = emitter.settings.colorStart;
		constants.colorEnd = emitter.settings.colorEnd;
		const D3D12_GPU_VIRTUAL_ADDRESS address = dxCommon_->UploadConstants(constants);
		if (address == 0) continue;
		cmdList->SetPipelineState(emitter.settings.additive ? additivePso_.Get() : alphaPso_.Get());
		cmdList->SetGraphicsRootConstantBufferView(0, address);
		cmdList->SetGraphicsRootShaderResourceView(1, emitter.buffer->GetGPUVirtualAddress());
		cmdList->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetGPUHandle(emitter.settings.textureHandle));
		cmdList->DrawInstanced(6, emitter.settings.capacity, 0, 0);
	}
}
