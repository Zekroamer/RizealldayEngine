#include "3d/PrimitiveDrawer.h"
#include "3d/Camera.h"
#include "base/DirectXCommon.h"
#include "base/Profiler.h"
#include "base/DXCManager.h"
#include "base/Log.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstring>

PrimitiveDrawer* PrimitiveDrawer::GetInstance()
{
	static PrimitiveDrawer instance;
	return &instance;
}

void PrimitiveDrawer::Initialize(DirectXCommon* dxCommon, DXCManager* dxc)
{
	dxCommon_ = dxCommon;

	lineVertices_.reserve(kMaxVertices);

	CreateRootSignature();
	CreatePipeline(dxc);
}

void PrimitiveDrawer::Finalize()
{
	// Called after the GPU has been flushed (RizeEngine::Finalize), so resources can be released directly
	pso_.Reset();
	rootSignature_.Reset();
	lineVertices_.clear();
	dxCommon_ = nullptr;
}

void PrimitiveDrawer::CreateRootSignature()
{
	// [0] float4x4 viewProjection as 32-bit root constants (b0, VS)
	D3D12_ROOT_PARAMETER rootParameters[1] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[0].Constants.ShaderRegister = 0;
	rootParameters[0].Constants.Num32BitValues = 16;

	D3D12_ROOT_SIGNATURE_DESC sigDesc{};
	sigDesc.pParameters = rootParameters;
	sigDesc.NumParameters = _countof(rootParameters);
	sigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&sigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		if (errorBlob) {
			Log::GetInstance()->logOutput(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		}
		assert(false);
		return;
	}

	hr = dxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));
}

void PrimitiveDrawer::CreatePipeline(DXCManager* dxc)
{
	Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxc->CompileShader(L"shaders/Line.VS.hlsl", L"vs_6_0", dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
	assert(vsBlob != nullptr);
	Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxc->CompileShader(L"shaders/Line.PS.hlsl", L"ps_6_0", dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
	assert(psBlob != nullptr);
	if (!vsBlob || !psBlob) {
		return;
	}

	D3D12_INPUT_ELEMENT_DESC inputElements[2] = {};
	inputElements[0].SemanticName = "POSITION";
	inputElements[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElements[1].SemanticName = "COLOR";
	inputElements[1].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElements[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_RASTERIZER_DESC rasterDesc{};
	rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterDesc.DepthClipEnable = TRUE;
	rasterDesc.AntialiasedLineEnable = TRUE;

	// Depth-tested so geometry hides the lines, but no depth write so lines never hide anything
	D3D12_DEPTH_STENCIL_DESC depthDesc{};
	depthDesc.DepthEnable = TRUE;
	depthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	// Keep the destination alpha: the editor shows this render target through ImGui::Image with alpha
	// blending, so writing the line alpha would make faded lines see-through to the dark panel behind
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = rootSignature_.Get();
	psoDesc.InputLayout = { inputElements, _countof(inputElements) };
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
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;

	HRESULT hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso_));
	assert(SUCCEEDED(hr));
	(void)hr;
}

void PrimitiveDrawer::DrawLine3d(const Vector3& p1, const Vector3& p2, const Vector4& color)
{
	PushLine(p1, color, p2, color);
}

void PrimitiveDrawer::PushLine(const Vector3& p1, const Vector4& c1, const Vector3& p2, const Vector4& c2)
{
	if (lineVertices_.size() + 2 > kMaxVertices) return;

	lineVertices_.push_back({ { p1.x, p1.y, p1.z, 1.0f }, c1 });
	lineVertices_.push_back({ { p2.x, p2.y, p2.z, 1.0f }, c2 });
}

void PrimitiveDrawer::DrawGrid(float size, float subdivisions, const Vector4& color)
{
	float halfSize = size * 0.5f;
	float step = size / subdivisions;

	for (float i = -halfSize; i <= halfSize; i += step) {
		DrawLine3d({ i, 0.0f, -halfSize }, { i, 0.0f, halfSize }, color);
		DrawLine3d({ -halfSize, 0.0f, i }, { halfSize, 0.0f, i }, color);
	}
}

void PrimitiveDrawer::DrawEditorGrid(const Vector3& center, int halfExtent, const Vector4& color, const Vector4& xAxisColor, const Vector4& zAxisColor)
{
	if (halfExtent <= 0) return;

	const float cx = std::round(center.x);
	const float cz = std::round(center.z);
	const float extent = static_cast<float>(halfExtent);
	const float y = center.y;

	auto fadedColor = [&](const Vector4& base, float x, float z) {
		const float dx = x - cx;
		const float dz = z - cz;
		// Full opacity near the center, smooth fade-out over the outer 80% of the extent
		const float t = std::clamp((std::sqrt(dx * dx + dz * dz) / extent - 0.2f) / 0.8f, 0.0f, 1.0f);
		const float fade = 1.0f - t * t * (3.0f - 2.0f * t);
		return Vector4{ base.x, base.y, base.z, base.w * fade };
	};

	// Lines are split into 1-unit segments so the alpha can fade out with distance
	for (int i = -halfExtent; i <= halfExtent; ++i) {
		const float x = cx + static_cast<float>(i);
		const float z = cz + static_cast<float>(i);
		const Vector4& lineAlongZ = (x == 0.0f) ? zAxisColor : color;
		const Vector4& lineAlongX = (z == 0.0f) ? xAxisColor : color;

		for (int s = -halfExtent; s < halfExtent; ++s) {
			const float a = static_cast<float>(s);
			const float b = static_cast<float>(s + 1);
			PushLine({ x, y, cz + a }, fadedColor(lineAlongZ, x, cz + a), { x, y, cz + b }, fadedColor(lineAlongZ, x, cz + b));
			PushLine({ cx + a, y, z }, fadedColor(lineAlongX, cx + a, z), { cx + b, y, z }, fadedColor(lineAlongX, cx + b, z));
		}
	}
}

void PrimitiveDrawer::Render(ID3D12GraphicsCommandList* cmdList, const Camera& camera)
{
	if (lineVertices_.empty() || !pso_ || !rootSignature_ || !dxCommon_ || dxCommon_->IsDeviceLost()) {
		lineVertices_.clear();
		return;
	}

	size_t count = (std::min)(lineVertices_.size(), kMaxVertices);

	// Vertices go into this frame's linear upload memory, so several Render() calls in one frame
	// (e.g. Scene and Game view) never overwrite data the GPU has not consumed yet
	const size_t bytes = sizeof(LineVertex) * count;
	DirectXCommon::UploadAllocation alloc = dxCommon_->AllocateUpload(bytes, 16);
	if (!alloc.cpu) {
		lineVertices_.clear();
		return;
	}
	std::memcpy(alloc.cpu, lineVertices_.data(), bytes);

	D3D12_VERTEX_BUFFER_VIEW vbView{};
	vbView.BufferLocation = alloc.gpu;
	vbView.SizeInBytes = static_cast<UINT>(bytes);
	vbView.StrideInBytes = sizeof(LineVertex);

	const Matrix4x4& viewProjection = camera.GetViewProjectionMatrix();

	cmdList->SetGraphicsRootSignature(rootSignature_.Get());
	cmdList->SetPipelineState(pso_.Get());
	cmdList->SetGraphicsRoot32BitConstants(0, 16, &viewProjection, 0);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	cmdList->IASetVertexBuffers(0, 1, &vbView);
	cmdList->DrawInstanced(static_cast<UINT>(count), 1, 0, 0);
	Profiler::GetInstance()->CountDrawCall();

	lineVertices_.clear();
}

void PrimitiveDrawer::Reset()
{
	lineVertices_.clear();
}
