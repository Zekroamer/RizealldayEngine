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
	constexpr uint32_t kGraphParticleBytes = 64; // GraphParticle in ParticleGraphShaderSource and GpuParticleGraph.VS.hlsl

	struct GraphConstants {
		Vector3 origin; float deltaTime;
		Vector3 direction; float time;
		uint32_t emitStart, emitCount, capacity, seed;
		Vector4 params[GpuParticles::kGraphParameters];
	};
	static_assert(sizeof(GraphConstants) == 44 * 4, "keep in sync with GraphConstants in ParticleGraphShaderSource");
	constexpr uint32_t kMaxCapacity = 1000000;
	constexpr uint32_t kThreadGroupSize = 64;

	struct ComputeConstants {
		Vector3 origin; float deltaTime;
		Vector3 direction; float cosSpread;
		Vector3 gravity; float drag;
		float lifeMin, lifeMax, speedMin, speedMax;
		uint32_t emitStart, emitCount, capacity, seed;
		float floorHeight, bounce;
		uint32_t collideWithFloor, shape;
		Vector3 shapeAxisX; float shapeRadius; // float3 + scalar per 16 bytes, as the HLSL cbuffer packs them
		Vector3 shapeAxisY; uint32_t velocityMode;
		Vector3 shapeAxisZ; float pad;
	};
	static_assert(sizeof(ComputeConstants) == 36 * 4, "keep in sync with the Constants cbuffer in GpuParticle.CS.hlsl");

	struct DrawConstants {
		Matrix4x4 viewProjection;
		Vector3 cameraRight; float sizeStart;
		Vector3 cameraUp; float sizeEnd;
		Vector4 colorStart;
		Vector4 colorEnd;
		Vector4 colorMiddle;
		float sizeMiddle;
		float middleTime; // < 0: two keys
		float stretch;    // 0 = camera-facing squares
		float upright;    // 1 = stands on world Y
		float time;       // materials
		float drawPad[3];
	};
	static_assert(sizeof(DrawConstants) == 176, "keep in sync with the DrawConstants cbuffer in GpuParticle.hlsli");

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
	dxc_ = dxc;
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

	// Draw: constants, particles (vertex shader), four textures t1..t4 (one table each: any texture handles)
	D3D12_DESCRIPTOR_RANGE ranges[kMaterialTextures] = {};
	D3D12_ROOT_PARAMETER drawParameters[2 + kMaterialTextures] = {};
	drawParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	drawParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	drawParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	for (uint32_t i = 0; i < kMaterialTextures; ++i) {
		ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[i].NumDescriptors = 1;
		ranges[i].BaseShaderRegister = 1 + i;
		drawParameters[2 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		drawParameters[2 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		drawParameters[2 + i].DescriptorTable.NumDescriptorRanges = 1;
		drawParameters[2 + i].DescriptorTable.pDescriptorRanges = &ranges[i];
	}
	D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
	samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	samplers[1] = samplers[0]; // s1: repeating, for scrolling textures
	samplers[1].ShaderRegister = 1;
	samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	D3D12_ROOT_SIGNATURE_DESC drawDesc{};
	drawDesc.pParameters = drawParameters;
	drawDesc.NumParameters = _countof(drawParameters);
	drawDesc.pStaticSamplers = samplers;
	drawDesc.NumStaticSamplers = _countof(samplers);
	drawSignature_ = MakeSignature(device, drawDesc);

	vertexShader_ = compile(L"shaders/GpuParticle.VS.hlsl", L"vs_6_0");
	graphVertexShader_ = compile(L"shaders/GpuParticleGraph.VS.hlsl", L"vs_6_0");
	Microsoft::WRL::ComPtr<IDxcBlob> ps = compile(L"shaders/GpuParticle.PS.hlsl", L"ps_6_0");
	assert(vertexShader_ && graphVertexShader_ && ps);
	if (!vertexShader_ || !graphVertexShader_ || !ps) return;
	alphaPso_ = CreateDrawPso(ps.Get(), false, vertexShader_.Get());
	additivePso_ = CreateDrawPso(ps.Get(), true, vertexShader_.Get());
	graphAlphaPso_ = CreateDrawPso(ps.Get(), false, graphVertexShader_.Get());
	graphAdditivePso_ = CreateDrawPso(ps.Get(), true, graphVertexShader_.Get());
	assert(alphaPso_ && additivePso_ && graphAlphaPso_ && graphAdditivePso_);

	// Particle graphs: their constants (with the parameters) + the particle buffer
	D3D12_ROOT_PARAMETER graphParameters[2] = {};
	graphParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	graphParameters[0].Constants.Num32BitValues = sizeof(GraphConstants) / 4;
	graphParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
	D3D12_ROOT_SIGNATURE_DESC graphDesc{};
	graphDesc.pParameters = graphParameters;
	graphDesc.NumParameters = _countof(graphParameters);
	graphComputeSignature_ = MakeSignature(device, graphDesc);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> GpuParticles::CreateDrawPso(IDxcBlob* ps, bool additive, IDxcBlob* vs) const
{
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
	if (additive) blend.DestBlend = D3D12_BLEND_ONE;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
	if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)))) return nullptr;
	return pso;
}

std::string GpuParticles::MaterialShaderSource(const std::string& body)
{
	return "#include \"GpuParticle.hlsli\"\n"
		"#include \"GraphCommon.hlsli\"\n"
		"Texture2D<float4> gTexture0 : register(t1);\n"
		"Texture2D<float4> gTexture1 : register(t2);\n"
		"Texture2D<float4> gTexture2 : register(t3);\n"
		"Texture2D<float4> gTexture3 : register(t4);\n"
		"SamplerState gSampler : register(s0);\n"
		"SamplerState gWrapSampler : register(s1);\n"
		"float4 main(GpuParticleVSOutput input) : SV_TARGET0\n{\n"
		"const float2 uv = input.uv;\n"
		"const float4 vertexColor = input.color;\n"
		"const float particleAge = input.data.x;\n"
		"const float particleRandom = input.data.y;\n"
		"const float4 baseColor = gTexture0.Sample(gSampler, uv) * vertexColor; // the look without a material\n"
		"float3 emissive = vertexColor.rgb;\n"
		"float opacity = vertexColor.a;\n"
		+ body +
		"\nreturn float4(emissive, opacity);\n}\n";
}

std::string GpuParticles::ParticleGraphShaderSource(const std::string& spawnBody, const std::string& updateBody)
{
	// GraphParticle is 64 bytes; keep it in sync with GpuParticleGraph.VS.hlsl and kGraphParticleBytes
	return "#include \"GraphCommon.hlsli\"\n"
		"struct GraphParticle { float3 position; float age; float3 velocity; float life; float4 color; float size; float rotation; uint seed; float custom; };\n"
		"RWStructuredBuffer<GraphParticle> gParticles : register(u0);\n"
		"cbuffer GraphConstants : register(b0)\n{\n"
		"float3 gOrigin; float gDeltaTime;\n"
		"float3 gDirection; float gTime;\n"
		"uint gEmitStart; uint gEmitCount; uint gCapacity; uint gSeed;\n"
		"float4 gParams[8];\n};\n"
		"[numthreads(64, 1, 1)]\nvoid main(uint3 id : SV_DispatchThreadID)\n{\n"
		"const uint index = id.x;\n"
		"if (index >= gCapacity) return;\n"
		"GraphParticle p = gParticles[index];\n"
		"if ((index + gCapacity - gEmitStart) % gCapacity < gEmitCount)\n{\n"
		"const uint seed = GraphPcg(index * 2654435761u + gSeed);\n"
		"const float age = 0.0f;\nconst float particleAge = 0.0f;\nconst float particleRandom = GraphRandom(seed, 0u);\n"
		"float3 position = gOrigin;\nfloat3 velocity = gDirection;\nfloat life = 1.0f;\nfloat4 color = float4(1, 1, 1, 1);\nfloat size = 0.2f;\nfloat rotation = 0.0f;\n"
		"{\n" + spawnBody + "\n}\n"
		"p.position = position;\np.velocity = velocity;\np.life = max(life, 0.001f);\np.age = 0.0f;\n"
		"p.color = color;\np.size = size;\np.rotation = rotation;\np.seed = seed;\np.custom = 0.0f;\n"
		"}\nelse if (p.age < p.life)\n{\n"
		"const uint seed = p.seed;\nconst float age = p.age;\nconst float life = p.life;\n"
		"const float particleAge = saturate(age / life);\nconst float particleRandom = GraphRandom(seed, 0u);\n"
		"const float3 position = p.position;\nfloat3 velocity = p.velocity;\nfloat4 color = p.color;\nfloat size = p.size;\nfloat rotation = p.rotation;\n"
		"float kill = 0.0f;\nfloat floorClamp = -3.0e38f;\n"
		"{\n" + updateBody + "\n}\n"
		"p.velocity = velocity;\np.position = position + velocity * gDeltaTime;\np.position.y = max(p.position.y, floorClamp);\n"
		"p.color = color;\np.size = size;\np.rotation = rotation;\n"
		"p.age = kill > 0.5f ? p.life : age + gDeltaTime;\n"
		"}\n"
		"gParticles[index] = p;\n}\n";
}

bool GpuParticles::BuildMaterial(uint64_t key, const std::string& body, std::string* error)
{
	if (materials_.count(key)) return true;
	if (!dxc_ || !dxCommon_ || !vertexShader_ || !graphVertexShader_) {
		if (error) *error = "GPU particles are not initialized";
		return false;
	}
	Microsoft::WRL::ComPtr<IDxcBlob> ps = dxc_->TryCompileShaderSource(MaterialShaderSource(body), L"ParticleMaterial.PS.hlsl", L"ps_6_0", error);
	if (!ps) return false;
	MaterialPsos psos{ CreateDrawPso(ps.Get(), false, vertexShader_.Get()), CreateDrawPso(ps.Get(), true, vertexShader_.Get()),
		CreateDrawPso(ps.Get(), false, graphVertexShader_.Get()), CreateDrawPso(ps.Get(), true, graphVertexShader_.Get()) };
	if (!psos.alpha || !psos.additive || !psos.graphAlpha || !psos.graphAdditive) {
		if (error) *error = "The material's pipeline could not be created";
		return false;
	}
	materials_[key] = std::move(psos);
	return true;
}

bool GpuParticles::BuildParticleGraph(uint64_t key, const std::string& spawnBody, const std::string& updateBody, std::string* error)
{
	if (graphs_.count(key)) return true;
	if (!dxc_ || !dxCommon_ || !graphComputeSignature_) {
		if (error) *error = "GPU particles are not initialized";
		return false;
	}
	Microsoft::WRL::ComPtr<IDxcBlob> cs = dxc_->TryCompileShaderSource(ParticleGraphShaderSource(spawnBody, updateBody), L"ParticleGraph.CS.hlsl", L"cs_6_0", error);
	if (!cs) return false;
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = graphComputeSignature_.Get();
	desc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
	if (FAILED(dxCommon_->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso)))) {
		if (error) *error = "The particle graph's pipeline could not be created";
		return false;
	}
	graphs_[key] = std::move(pso);
	return true;
}

void GpuParticles::ReleaseMaterial(uint64_t key)
{
	const auto it = materials_.find(key);
	if (it == materials_.end()) return;
	const MaterialPsos& psos = it->second;
	for (ID3D12PipelineState* pso : { psos.alpha.Get(), psos.additive.Get(), psos.graphAlpha.Get(), psos.graphAdditive.Get() }) {
		if (pso) dxCommon_->DeferRelease(pso);
	}
	materials_.erase(it);
}

void GpuParticles::ReleaseParticleGraph(uint64_t key)
{
	const auto it = graphs_.find(key);
	if (it == graphs_.end()) return;
	if (it->second) dxCommon_->DeferRelease(it->second);
	graphs_.erase(it);
}

void GpuParticles::Finalize()
{
	// Called after the GPU has been flushed (RizeEngine::Finalize)
	emitters_.clear();
	materials_.clear();
	graphs_.clear();
	vertexShader_.Reset();
	graphVertexShader_.Reset();
	graphAlphaPso_.Reset();
	graphAdditivePso_.Reset();
	graphComputeSignature_.Reset();
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
	// A graph that does not compile (yet) falls back to the settings, so the emitter never just vanishes
	const auto graph = settings.graph != 0 ? graphs_.find(settings.graph) : graphs_.end();
	const bool usesGraph = graph != graphs_.end();
	Emitter& emitter = emitters_[id];
	if (!emitter.buffer || emitter.settings.capacity != capacity || emitter.usesGraph != usesGraph) {
		if (emitter.buffer) dxCommon_->DeferRelease(emitter.buffer);
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC resource{};
		resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resource.Width = static_cast<UINT64>(capacity) * (usesGraph ? kGraphParticleBytes : kParticleBytes);
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
		++bufferCreations_;
		emitter.ringHead = 0;
		emitter.isShaderResource = false;
		emitter.idleFrames = 0;
		emitter.usesGraph = usesGraph;
	}
	// Back from the pool: the particles of its last life must not show up again
	const bool clearStale = emitter.idleFrames > 0;
	emitter.idleFrames = 0;
	emitter.settings = settings;
	emitter.settings.capacity = capacity;
	emitter.origin = origin;
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
	constants.floorHeight = std::isfinite(settings.floorHeight) ? settings.floorHeight : 0.0f;
	constants.bounce = std::clamp(settings.bounce, 0.0f, 1.0f);
	constants.collideWithFloor = settings.collideWithFloor ? 1u : 0u;
	constants.shape = settings.shape < static_cast<uint32_t>(GpuParticleShape::Count) ? settings.shape : 0u;
	constants.velocityMode = settings.velocityMode < static_cast<uint32_t>(GpuParticleVelocity::Count) ? settings.velocityMode : 0u;
	constants.shapeRadius = std::isfinite(settings.shapeRadius) ? (std::max)(settings.shapeRadius, 0.0f) : 0.0f;
	constants.shapeAxisX = settings.shapeAxisX;
	constants.shapeAxisY = settings.shapeAxisY;
	constants.shapeAxisZ = settings.shapeAxisZ;
	emitter.seed = emitter.seed * 1664525u + 1013904223u;
	emitter.ringHead = (emitter.ringHead + count) % capacity;

	if (usesGraph) {
		GraphConstants g{};
		g.origin = origin;
		g.deltaTime = constants.deltaTime;
		g.direction = constants.direction;
		g.time = time_;
		g.emitStart = constants.emitStart;
		g.emitCount = count;
		g.capacity = capacity;
		g.seed = constants.seed;
		for (uint32_t i = 0; i < kGraphParameters; ++i) {
			const Vector4& v = settings.graphParams[i];
			g.params[i] = (std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w)) ? v : Vector4{ 0.0f, 0.0f, 0.0f, 0.0f };
		}
		cmdList->SetComputeRootSignature(graphComputeSignature_.Get());
		cmdList->SetPipelineState(graph->second.Get());
		cmdList->SetComputeRootUnorderedAccessView(1, emitter.buffer->GetGPUVirtualAddress());
		if (clearStale) {
			// The same as below: an hour-long step without emitting ends every old particle
			GraphConstants expire = g;
			expire.deltaTime = 3600.0f;
			expire.emitCount = 0;
			cmdList->SetComputeRoot32BitConstants(0, sizeof(GraphConstants) / 4, &expire, 0);
			cmdList->Dispatch((capacity + kThreadGroupSize - 1) / kThreadGroupSize, 1, 1);
			D3D12_RESOURCE_BARRIER uav{};
			uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			uav.UAV.pResource = emitter.buffer.Get();
			cmdList->ResourceBarrier(1, &uav);
		}
		cmdList->SetComputeRoot32BitConstants(0, sizeof(GraphConstants) / 4, &g, 0);
		cmdList->Dispatch((capacity + kThreadGroupSize - 1) / kThreadGroupSize, 1, 1);
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		cmdList->ResourceBarrier(1, &barrier);
		emitter.isShaderResource = true;
		return;
	}
	cmdList->SetComputeRootSignature(computeSignature_.Get());
	cmdList->SetPipelineState(computePso_.Get());
	cmdList->SetComputeRootUnorderedAccessView(1, emitter.buffer->GetGPUVirtualAddress());
	if (clearStale) {
		// One step of an hour without emitting ages every old particle past its life (lives are at most a minute)
		ComputeConstants expire = constants;
		expire.deltaTime = 3600.0f;
		expire.emitCount = 0;
		expire.collideWithFloor = 0u;
		cmdList->SetComputeRoot32BitConstants(0, sizeof(ComputeConstants) / 4, &expire, 0);
		cmdList->Dispatch((capacity + kThreadGroupSize - 1) / kThreadGroupSize, 1, 1);
		D3D12_RESOURCE_BARRIER uav{};
		uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uav.UAV.pResource = emitter.buffer.Get();
		cmdList->ResourceBarrier(1, &uav);
	}
	cmdList->SetComputeRoot32BitConstants(0, sizeof(ComputeConstants) / 4, &constants, 0);
	cmdList->Dispatch((capacity + kThreadGroupSize - 1) / kThreadGroupSize, 1, 1);

	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	cmdList->ResourceBarrier(1, &barrier);
	emitter.isShaderResource = true;
}

void GpuParticles::EndFrame()
{
	for (auto it = emitters_.begin(); it != emitters_.end();) {
		Emitter& emitter = it->second;
		if (emitter.simulated) {
			emitter.simulated = false;
			++it;
		} else if (++emitter.idleFrames <= kIdleFramesBeforeRelease) {
			++it; // kept: a pooled object usually comes back soon
		} else {
			dxCommon_->DeferRelease(emitter.buffer);
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
	DrawList(cmdList, camera, nullptr);
}

void GpuParticles::DrawEmitter(ID3D12GraphicsCommandList* cmdList, const Camera& camera, uint64_t id)
{
	const std::vector<uint64_t> ids{ id };
	DrawList(cmdList, camera, &ids);
}

void GpuParticles::DrawEmitters(ID3D12GraphicsCommandList* cmdList, const Camera& camera, const std::vector<uint64_t>& ids)
{
	if (!ids.empty()) DrawList(cmdList, camera, &ids);
}

void GpuParticles::DrawList(ID3D12GraphicsCommandList* cmdList, const Camera& camera, const std::vector<uint64_t>* only)
{
	if (emitters_.empty() || !dxCommon_ || !alphaPso_ || !additivePso_) return;
	const Matrix4x4& view = camera.GetViewMatrix();
	TextureManager::GetInstance()->SetDescriptorHeaps(cmdList);
	cmdList->SetGraphicsRootSignature(drawSignature_.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// Back to front, so a near cloud of smoke covers a far one
	std::vector<const Emitter*> drawn;
	std::vector<Vector3> origins;
	for (const auto& [id, emitter] : emitters_) {
		if (!emitter.isShaderResource || emitter.idleFrames > 0) continue; // not simulated last frame: hidden
		if (only ? std::find(only->begin(), only->end(), id) == only->end() : id == kPreviewEmitterId) continue;
		drawn.push_back(&emitter);
		origins.push_back(emitter.origin);
	}
	const Vector3 eye = camera.translation;
	for (const size_t index : GpuParticleDrawOrder(origins, eye)) {
		const Emitter& emitter = *drawn[index];
		DrawConstants constants{};
		constants.viewProjection = camera.GetViewProjectionMatrix();
		constants.cameraRight = { view.m[0][0], view.m[1][0], view.m[2][0] };
		constants.cameraUp = { view.m[0][1], view.m[1][1], view.m[2][1] };
		constants.sizeStart = emitter.settings.sizeStart;
		constants.sizeEnd = emitter.settings.sizeEnd;
		constants.colorStart = emitter.settings.colorStart;
		constants.colorEnd = emitter.settings.colorEnd;
		constants.colorMiddle = emitter.settings.colorMiddle;
		constants.sizeMiddle = emitter.settings.sizeMiddle;
		constants.middleTime = emitter.settings.useMiddleKey ? std::clamp(emitter.settings.middleTime, 0.001f, 0.999f) : -1.0f;
		constants.stretch = std::isfinite(emitter.settings.stretch) ? (std::max)(emitter.settings.stretch, 0.0f) : 0.0f;
		constants.upright = emitter.settings.upright ? 1.0f : 0.0f;
		constants.time = time_;
		const D3D12_GPU_VIRTUAL_ADDRESS address = dxCommon_->UploadConstants(constants);
		if (address == 0) continue;
		const bool additive = emitter.settings.additive;
		ID3D12PipelineState* pso = emitter.usesGraph ? (additive ? graphAdditivePso_.Get() : graphAlphaPso_.Get()) : (additive ? additivePso_.Get() : alphaPso_.Get());
		if (auto material = materials_.find(emitter.settings.material); emitter.settings.material != 0 && material != materials_.end()) {
			const MaterialPsos& m = material->second;
			pso = emitter.usesGraph ? (additive ? m.graphAdditive.Get() : m.graphAlpha.Get()) : (additive ? m.additive.Get() : m.alpha.Get());
		}
		cmdList->SetPipelineState(pso);
		cmdList->SetGraphicsRootConstantBufferView(0, address);
		cmdList->SetGraphicsRootShaderResourceView(1, emitter.buffer->GetGPUVirtualAddress());
		for (uint32_t slot = 0; slot < kMaterialTextures; ++slot) {
			const uint32_t handle = emitter.settings.materialTextures[slot] != 0 ? emitter.settings.materialTextures[slot] : emitter.settings.textureHandle;
			cmdList->SetGraphicsRootDescriptorTable(2 + slot, TextureManager::GetInstance()->GetGPUHandle(handle));
		}
		cmdList->DrawInstanced(6, emitter.settings.capacity, 0, 0);
	}
}
