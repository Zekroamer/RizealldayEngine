#include "base/DirectXCommon.h"
#include "base/DXCManager.h"
#include "render/PostProcess.h"
#include "base/RenderTexture.h"
#include <algorithm>
#include <cassert>
#include <cstring>

PostProcess::~PostProcess()
{
	if (!dxCommon_) return;
	for (Target& t : effectTargets_) ReleaseTarget(t);
	ReleaseTarget(fxaaSource_);
	for (Target& t : bloom_) ReleaseTarget(t);
	for (Target& t : ssao_) ReleaseTarget(t);
	if (ssaoPass_) dxCommon_->DeferRelease(ssaoPass_);
	if (ssaoBlur_) dxCommon_->DeferRelease(ssaoBlur_);
	for (auto& [file, pso] : effectPsos_) {
		if (pso) dxCommon_->DeferRelease(pso);
	}
	if (compositeFinal_) dxCommon_->DeferRelease(compositeFinal_);
	if (compositeTemp_) dxCommon_->DeferRelease(compositeTemp_);
	if (fxaaFinal_) dxCommon_->DeferRelease(fxaaFinal_);
	if (bloomDown_) dxCommon_->DeferRelease(bloomDown_);
	if (bloomUp_) dxCommon_->DeferRelease(bloomUp_);
	if (rootSignature_) dxCommon_->DeferRelease(rootSignature_);
}

void PostProcess::Initialize(DirectXCommon* dxCommon, DXCManager* dxc)
{
	dxCommon_ = dxCommon;
	dxc_ = dxc;
	CreateRootSignature();

	auto compile = [dxc](const wchar_t* file, const wchar_t* profile) {
		auto blob = dxc->CompileShader(file, profile, dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
		assert(blob != nullptr);
		return blob;
	};
	vertexShader_ = compile(L"shaders/Post.VS.hlsl", L"vs_6_0");
	auto composite = compile(L"shaders/Post.Composite.PS.hlsl", L"ps_6_0");
	auto fxaa = compile(L"shaders/Post.Fxaa.PS.hlsl", L"ps_6_0");
	auto down = compile(L"shaders/Post.BloomDown.PS.hlsl", L"ps_6_0");
	auto up = compile(L"shaders/Post.BloomUp.PS.hlsl", L"ps_6_0");

	compositeFinal_ = CreatePso(composite.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, true, false);
	compositeTemp_ = CreatePso(composite.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, false, false);
	fxaaFinal_ = CreatePso(fxaa.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, true, false);
	bloomDown_ = CreatePso(down.Get(), DirectXCommon::kSceneColorFormat, false, false);
	bloomUp_ = CreatePso(up.Get(), DirectXCommon::kSceneColorFormat, false, true);
	ssaoPass_ = CreatePso(compile(L"shaders/Post.Ssao.PS.hlsl", L"ps_6_0").Get(), DXGI_FORMAT_R8_UNORM, false, false);
	ssaoBlur_ = CreatePso(compile(L"shaders/Post.SsaoBlur.PS.hlsl", L"ps_6_0").Get(), DXGI_FORMAT_R8_UNORM, false, false);
}

void PostProcess::CreateRootSignature()
{
	D3D12_DESCRIPTOR_RANGE ranges[4] = {};
	D3D12_ROOT_PARAMETER params[5] = {};
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	for (uint32_t i = 0; i < 4; ++i) {
		ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[i].NumDescriptors = 1;
		ranges[i].BaseShaderRegister = i;
		params[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		params[i + 1].DescriptorTable.NumDescriptorRanges = 1;
		params[i + 1].DescriptorTable.pDescriptorRanges = &ranges[i];
	}

	D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
	samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	samplers[1] = samplers[0];
	samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	samplers[1].ShaderRegister = 1;

	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.pParameters = params;
	desc.NumParameters = _countof(params);
	desc.pStaticSamplers = samplers;
	desc.NumStaticSamplers = _countof(samplers);

	Microsoft::WRL::ComPtr<ID3DBlob> blob, errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errorBlob);
	assert(SUCCEEDED(hr));
	hr = dxCommon_->GetDevice()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));
	(void)hr;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PostProcess::CreatePso(IDxcBlob* ps, DXGI_FORMAT format, bool withDepthFormat, bool additive)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();
	desc.VS = { vertexShader_->GetBufferPointer(), vertexShader_->GetBufferSize() };
	desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.RasterizerState.DepthClipEnable = TRUE;
	D3D12_RENDER_TARGET_BLEND_DESC& blend = desc.BlendState.RenderTarget[0];
	blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	if (additive) {
		blend.BlendEnable = TRUE;
		blend.SrcBlend = D3D12_BLEND_ONE;
		blend.DestBlend = D3D12_BLEND_ONE;
		blend.BlendOp = D3D12_BLEND_OP_ADD;
		blend.SrcBlendAlpha = D3D12_BLEND_ONE;
		blend.DestBlendAlpha = D3D12_BLEND_ZERO;
		blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	}
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = format;
	// The final output keeps its depth buffer bound (UI uses the same target), depth stays off
	desc.DSVFormat = withDepthFormat ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
	if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)))) return nullptr;
	return pso;
}

void PostProcess::CreateTarget(Target& target, uint32_t rtvIndex, uint32_t width, uint32_t height, DXGI_FORMAT format)
{
	ReleaseTarget(target);
	ID3D12Device* device = dxCommon_->GetDevice();

	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_CLEAR_VALUE clear{};
	clear.Format = format;
	HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&target.resource));
	if (dxCommon_->CheckDeviceLost(hr, "PostProcess::CreateTarget") || !target.resource) return;

	target.rtvIndex = rtvIndex;
	target.rtv = dxCommon_->GetRtvCPUDescriptorHandle(rtvIndex);
	device->CreateRenderTargetView(target.resource.Get(), nullptr, target.rtv);
	target.srvIndex = dxCommon_->AllocateSrvIndex();
	target.srv = dxCommon_->GetSrvGPUDescriptorHandle(target.srvIndex);
	device->CreateShaderResourceView(target.resource.Get(), nullptr, dxCommon_->GetSrvCPUDescriptorHandle(target.srvIndex));
	target.width = width;
	target.height = height;
	target.format = format;
}

void PostProcess::ReleaseTarget(Target& target)
{
	if (target.resource) dxCommon_->DeferRelease(target.resource);
	target.resource.Reset();
	if (target.srvIndex != DirectXCommon::kInvalidSrvIndex) dxCommon_->FreeSrvIndex(target.srvIndex);
	target.srvIndex = DirectXCommon::kInvalidSrvIndex;
	target.width = target.height = 0;
}

void PostProcess::Resize(uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0 || (fxaaSource_.width == width && fxaaSource_.height == height)) return;
	uint32_t rtv = DirectXCommon::kPostRtvBaseIndex;
	CreateTarget(effectTargets_[0], rtv++, width, height, DirectXCommon::kSceneColorFormat);
	CreateTarget(effectTargets_[1], rtv++, width, height, DirectXCommon::kSceneColorFormat);
	CreateTarget(fxaaSource_, rtv++, width, height, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
	uint32_t w = width;
	uint32_t h = height;
	for (Target& level : bloom_) {
		w = (std::max)(w / 2u, 1u);
		h = (std::max)(h / 2u, 1u);
		CreateTarget(level, rtv++, w, h, DirectXCommon::kSceneColorFormat);
	}
	const uint32_t halfWidth = (std::max)(width / 2u, 1u);
	const uint32_t halfHeight = (std::max)(height / 2u, 1u);
	CreateTarget(ssao_[0], rtv++, halfWidth, halfHeight, DXGI_FORMAT_R8_UNORM);
	CreateTarget(ssao_[1], rtv++, halfWidth, halfHeight, DXGI_FORMAT_R8_UNORM);
}

void PostProcess::BeginPass(ID3D12GraphicsCommandList* cmdList, Target& target, bool clear)
{
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = target.resource.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	cmdList->ResourceBarrier(1, &barrier);

	const D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(target.width), static_cast<float>(target.height), 0.0f, 1.0f };
	const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(target.width), static_cast<LONG>(target.height) };
	cmdList->RSSetViewports(1, &viewport);
	cmdList->RSSetScissorRects(1, &scissor);
	cmdList->OMSetRenderTargets(1, &target.rtv, FALSE, nullptr);
	if (clear) {
		const float black[4] = {};
		cmdList->ClearRenderTargetView(target.rtv, black, 0, nullptr);
	}
}

void PostProcess::EndPass(ID3D12GraphicsCommandList* cmdList, Target& target)
{
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = target.resource.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	cmdList->ResourceBarrier(1, &barrier);
}

void PostProcess::DrawFullscreen(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* pso, const Constants& constants,
	D3D12_GPU_DESCRIPTOR_HANDLE source, D3D12_GPU_DESCRIPTOR_HANDLE bloom, D3D12_GPU_DESCRIPTOR_HANDLE depth)
{
	const D3D12_GPU_VIRTUAL_ADDRESS address = dxCommon_->UploadConstants(constants);
	if (address == 0 || !pso) return;
	ID3D12DescriptorHeap* heaps[] = { dxCommon_->GetSrvDescriptorHeap() };
	cmdList->SetDescriptorHeaps(1, heaps);
	cmdList->SetGraphicsRootSignature(rootSignature_.Get());
	cmdList->SetPipelineState(pso);
	cmdList->SetGraphicsRootConstantBufferView(0, address);
	cmdList->SetGraphicsRootDescriptorTable(1, source);
	cmdList->SetGraphicsRootDescriptorTable(2, bloom);
	cmdList->SetGraphicsRootDescriptorTable(3, depth);
	cmdList->SetGraphicsRootDescriptorTable(4, occlusion_.ptr != 0 ? occlusion_ : source); // read only with flag 8
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(3, 1, 0, 0);
}

void PostProcess::SetCamera(float tanHalfFovX, float tanHalfFovY, float nearZ, float farZ, bool perspective)
{
	cameraInfo_[0] = tanHalfFovX;
	cameraInfo_[1] = tanHalfFovY;
	cameraInfo_[2] = nearZ;
	cameraInfo_[3] = farZ;
	cameraPerspective_ = perspective;
}

void PostProcess::Prepare(ID3D12GraphicsCommandList* cmdList, RenderTexture& scene, const PostProcessSettings& settings, float gameTime)
{
	Resize(scene.GetWidth(), scene.GetHeight());
	scene.BeginDepthRead(cmdList);

	const D3D12_GPU_DESCRIPTOR_HANDLE depth = scene.GetDepthSrvGPUHandle();
	D3D12_GPU_DESCRIPTOR_HANDLE source = scene.GetSrvGPUHandle();

	Constants constants{};
	constants.exposure = settings.exposure;
	constants.contrast = settings.contrast;
	constants.saturation = settings.saturation;
	constants.vignette = settings.vignette;
	constants.tint = settings.tint;
	constants.letterbox = settings.letterbox;
	constants.bloomIntensity = settings.bloomIntensity;
	constants.bloomThreshold = settings.bloomThreshold;
	constants.flags = (settings.enabled ? 1u : 0u) | (settings.toneMapping ? 2u : 0u) | (settings.bloom ? 4u : 0u);
	constants.time = gameTime;
	constants.texelSize[0] = 1.0f / static_cast<float>(scene.GetWidth());
	constants.texelSize[1] = 1.0f / static_cast<float>(scene.GetHeight());

	occlusion_ = {};
	if (settings.enabled && settings.ssao && settings.ssaoIntensity > 0.0f && cameraPerspective_) {
		Constants pass = constants;
		std::memcpy(pass.params, cameraInfo_, sizeof(cameraInfo_));
		pass.params[4] = settings.ssaoRadius;
		pass.params[5] = settings.ssaoIntensity;
		BeginPass(cmdList, ssao_[0], false);
		DrawFullscreen(cmdList, ssaoPass_.Get(), pass, source, source, depth);
		EndPass(cmdList, ssao_[0]);
		pass.texelSize[0] = 1.0f / static_cast<float>(ssao_[0].width);
		pass.texelSize[1] = 1.0f / static_cast<float>(ssao_[0].height);
		BeginPass(cmdList, ssao_[1], false);
		DrawFullscreen(cmdList, ssaoBlur_.Get(), pass, ssao_[0].srv, ssao_[0].srv, depth);
		EndPass(cmdList, ssao_[1]);
		occlusion_ = ssao_[1].srv;
		constants.flags |= 8u;
	}

	// Screen effects run even with the look settings off: they are gameplay, not a look
	int nextTarget = 0;
	for (const ScreenEffect& effect : effects_) {
		if (!effect.pso) continue;
		Target& target = effectTargets_[nextTarget];
		nextTarget ^= 1;
		BeginPass(cmdList, target, false);
		Constants effectConstants = constants;
		std::memcpy(effectConstants.params, effect.params, sizeof(effect.params));
		DrawFullscreen(cmdList, effect.pso, effectConstants, source, source, depth);
		EndPass(cmdList, target);
		source = target.srv;
	}

	const bool bloom = settings.enabled && settings.bloom && settings.bloomIntensity > 0.0f;
	if (bloom) {
		D3D12_GPU_DESCRIPTOR_HANDLE input = source;
		float inputTexel[2] = { constants.texelSize[0], constants.texelSize[1] };
		for (int i = 0; i < kBloomLevels; ++i) {
			Constants pass = constants;
			pass.texelSize[0] = inputTexel[0];
			pass.texelSize[1] = inputTexel[1];
			pass.params[0] = (i == 0) ? 1.0f : 0.0f;
			BeginPass(cmdList, bloom_[i], false);
			DrawFullscreen(cmdList, bloomDown_.Get(), pass, input, input, depth);
			EndPass(cmdList, bloom_[i]);
			input = bloom_[i].srv;
			inputTexel[0] = 1.0f / static_cast<float>(bloom_[i].width);
			inputTexel[1] = 1.0f / static_cast<float>(bloom_[i].height);
		}
		for (int i = kBloomLevels - 1; i > 0; --i) {
			Constants pass = constants;
			pass.texelSize[0] = 1.0f / static_cast<float>(bloom_[i].width);
			pass.texelSize[1] = 1.0f / static_cast<float>(bloom_[i].height);
			BeginPass(cmdList, bloom_[i - 1], false);
			DrawFullscreen(cmdList, bloomUp_.Get(), pass, bloom_[i].srv, bloom_[i].srv, depth);
			EndPass(cmdList, bloom_[i - 1]);
		}
	}

	composite_ = constants;
	if (!bloom) composite_.flags &= ~4u;
	compositeBloom_ = bloom ? bloom_[0].srv : source;
	compositeDepth_ = depth;
	compositeSource_ = source;

	useFxaa_ = settings.enabled && settings.fxaa;
	if (useFxaa_) {
		BeginPass(cmdList, fxaaSource_, false);
		DrawFullscreen(cmdList, compositeTemp_.Get(), composite_, compositeSource_, compositeBloom_, compositeDepth_);
		EndPass(cmdList, fxaaSource_);
		compositeSource_ = fxaaSource_.srv;
	}
}

void PostProcess::Composite(ID3D12GraphicsCommandList* cmdList)
{
	DrawFullscreen(cmdList, useFxaa_ ? fxaaFinal_.Get() : compositeFinal_.Get(), composite_, compositeSource_, compositeBloom_, compositeDepth_);
}

void PostProcess::Resolve(ID3D12GraphicsCommandList* cmdList, RenderTexture& scene)
{
	Constants constants{}; // flags 0 = pass-through
	occlusion_ = {};
	const D3D12_GPU_DESCRIPTOR_HANDLE source = scene.GetSrvGPUHandle();
	DrawFullscreen(cmdList, compositeFinal_.Get(), constants, source, source, source);
}

bool PostProcess::SetScreenEffect(int slot, const std::wstring& shaderFile, const float params[kScreenEffectParams], std::string* error)
{
	if (slot < 0 || slot >= kMaxScreenEffects) {
		if (error) *error = "Screen effect slot out of range";
		return false;
	}
	auto it = effectPsos_.find(shaderFile);
	if (it == effectPsos_.end()) {
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
		std::string compileError;
		auto blob = dxc_->TryCompileShader(shaderFile, L"ps_6_0", &compileError);
		if (blob) pso = CreatePso(blob.Get(), DirectXCommon::kSceneColorFormat, false, false);
		if (!pso && error) *error = blob ? "Could not create the pipeline" : compileError;
		// A broken file is remembered too: a script calling this every frame must not recompile every frame
		it = effectPsos_.emplace(shaderFile, pso).first;
		if (!pso) {
			effects_[slot] = {};
			return false;
		}
	}
	if (!it->second) {
		effects_[slot] = {};
		return false;
	}
	effects_[slot].pso = it->second.Get();
	if (params) std::memcpy(effects_[slot].params, params, sizeof(effects_[slot].params));
	return true;
}

void PostProcess::ClearScreenEffect(int slot)
{
	if (slot >= 0 && slot < kMaxScreenEffects) effects_[slot] = {};
}

void PostProcess::ClearScreenEffects()
{
	for (ScreenEffect& effect : effects_) effect = {};
	std::erase_if(effectPsos_, [](const auto& entry) { return entry.second == nullptr; }); // retry broken files in the next session
}

bool PostProcess::HasScreenEffect(int slot) const
{
	return slot >= 0 && slot < kMaxScreenEffects && effects_[slot].pso != nullptr;
}
