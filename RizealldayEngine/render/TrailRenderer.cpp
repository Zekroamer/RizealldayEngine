#include "render/TrailRenderer.h"
#include "3d/Camera.h"
#include "base/DirectXCommon.h"
#include "base/DXCManager.h"
#include "base/Log.h"
#include "base/TextureManager.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
	float Distance(const Vector3& a, const Vector3& b)
	{
		const float dx = a.x - b.x;
		const float dy = a.y - b.y;
		const float dz = a.z - b.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	Vector4 Lerp(const Vector4& a, const Vector4& b, float t)
	{
		return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
	}
}

void TrailRibbon::Update(const Vector3& head, double now, const TrailStyle& style, bool emitting)
{
	const double lifetime = (std::max)(style.lifetime, 0.001f);
	size_t expired = 0;
	const size_t movingHead = (emitting && wasEmitting_ && !points_.empty()) ? 1 : 0; // the head is never old
	while (expired + movingHead < points_.size() && now - points_[expired].born > lifetime) ++expired;
	points_.erase(points_.begin(), points_.begin() + static_cast<std::ptrdiff_t>(expired));

	const bool valid = std::isfinite(head.x) && std::isfinite(head.y) && std::isfinite(head.z);
	if (!emitting) {
		wasEmitting_ = false;
		return;
	}
	if (!valid) return;
	if (!wasEmitting_ || points_.empty()) {
		// Two points: one stays here, the other follows the head. A separate piece: no line back to where it stopped.
		points_.push_back({ head, now, true });
		points_.push_back({ head, now, false });
		wasEmitting_ = true;
	} else {
		Point& last = points_.back();
		last.position = head;
		last.born = now;
		if (Distance(head, points_[points_.size() - 2].position) >= style.minDistance) {
			points_.push_back({ head, now, false });
		}
	}
	if (points_.size() > kMaxPoints) {
		points_.erase(points_.begin(), points_.end() - static_cast<std::ptrdiff_t>(kMaxPoints));
	}
}

void TrailRibbon::BuildMesh(const Vector3& cameraPosition, double now, const TrailStyle& style, std::vector<TrailVertex>& out) const
{
	if (points_.size() < 2) return;
	const float lifetime = (std::max)(style.lifetime, 0.001f);
	struct Ring { Vector3 left; Vector3 centre; Vector3 right; Vector4 color; float age; };
	std::vector<Ring> rings(points_.size());
	Vector3 side{ 0.0f, 1.0f, 0.0f }; // the last good one is kept where the ribbon points at the camera
	for (size_t i = 0; i < points_.size(); ++i) {
		const Vector3& p = points_[i].position;
		const Vector3& before = (i > 0 && !points_[i].startsPiece) ? points_[i - 1].position : p;
		const Vector3& after = (i + 1 < points_.size() && !points_[i + 1].startsPiece) ? points_[i + 1].position : p;
		const Vector3 tangent{ after.x - before.x, after.y - before.y, after.z - before.z };
		const Vector3 toCamera{ cameraPosition.x - p.x, cameraPosition.y - p.y, cameraPosition.z - p.z };
		const Vector3 cross{ tangent.y * toCamera.z - tangent.z * toCamera.y, tangent.z * toCamera.x - tangent.x * toCamera.z, tangent.x * toCamera.y - tangent.y * toCamera.x };
		const float length = std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
		if (length > 1.0e-6f && std::isfinite(length)) side = { cross.x / length, cross.y / length, cross.z / length };

		const float t = std::clamp(static_cast<float>(now - points_[i].born) / lifetime, 0.0f, 1.0f);
		const float halfWidth = (std::max)(style.widthStart + (style.widthEnd - style.widthStart) * t, 0.0f) * 0.5f;
		const Vector3 offset{ side.x * halfWidth, side.y * halfWidth, side.z * halfWidth };
		rings[i] = { { p.x + offset.x, p.y + offset.y, p.z + offset.z }, p, { p.x - offset.x, p.y - offset.y, p.z - offset.z }, Lerp(style.colorStart, style.colorEnd, t), t };
	}

	const float edge = style.softEdges ? 0.0f : 1.0f;
	const auto vertex = [&](const Vector3& position, const Ring& ring, float side) {
		const float alpha = side == 0.5f ? 1.0f : edge;
		return TrailVertex{ { position.x, position.y, position.z, 1.0f }, { ring.color.x, ring.color.y, ring.color.z, ring.color.w * alpha }, { ring.age, side } };
	};
	// Two strips per segment, left half and right half, so the soft edge has a full-alpha centre line
	const auto quad = [&](const Ring& a, const Ring& b, const Vector3& a0, const Vector3& a1, const Vector3& b0, const Vector3& b1, float side0, float side1) {
		out.push_back(vertex(a0, a, side0));
		out.push_back(vertex(a1, a, side1));
		out.push_back(vertex(b0, b, side0));
		out.push_back(vertex(b0, b, side0));
		out.push_back(vertex(a1, a, side1));
		out.push_back(vertex(b1, b, side1));
	};
	for (size_t i = 0; i + 1 < rings.size(); ++i) {
		if (points_[i + 1].startsPiece) continue;
		const Ring& a = rings[i];
		const Ring& b = rings[i + 1];
		quad(a, b, a.left, a.centre, b.left, b.centre, 0.0f, 0.5f);
		quad(a, b, a.centre, a.right, b.centre, b.right, 0.5f, 1.0f);
	}
}

TrailRenderer* TrailRenderer::GetInstance()
{
	static TrailRenderer instance;
	return &instance;
}

void TrailRenderer::Initialize(DirectXCommon* dxCommon, DXCManager* dxc)
{
	dxCommon_ = dxCommon;

	// View-projection as root constants, the texture as a table
	D3D12_DESCRIPTOR_RANGE range{};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.NumDescriptors = 1;
	D3D12_ROOT_PARAMETER parameters[2] = {};
	parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	parameters[0].Constants.Num32BitValues = 16;
	parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	parameters[1].DescriptorTable.NumDescriptorRanges = 1;
	parameters[1].DescriptorTable.pDescriptorRanges = &range;
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC sigDesc{};
	sigDesc.pParameters = parameters;
	sigDesc.NumParameters = _countof(parameters);
	sigDesc.pStaticSamplers = &sampler;
	sigDesc.NumStaticSamplers = 1;
	sigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&sigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		if (errorBlob) Log::GetInstance()->logOutput(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
		return;
	}
	hr = dxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDxcBlob> vs = dxc->CompileShader(L"shaders/Trail.VS.hlsl", L"vs_6_0", dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
	Microsoft::WRL::ComPtr<IDxcBlob> ps = dxc->CompileShader(L"shaders/Trail.PS.hlsl", L"ps_6_0", dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
	assert(vs && ps);
	if (!vs || !ps) return;

	D3D12_INPUT_ELEMENT_DESC inputElements[3] = {};
	inputElements[0].SemanticName = "POSITION";
	inputElements[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElements[1].SemanticName = "COLOR";
	inputElements[1].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElements[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElements[2].SemanticName = "TEXCOORD";
	inputElements[2].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElements[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();
	desc.InputLayout = { inputElements, _countof(inputElements) };
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
	hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&alphaPso_));
	assert(SUCCEEDED(hr));
	blend.DestBlend = D3D12_BLEND_ONE;
	hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&additivePso_));
	assert(SUCCEEDED(hr));
}

void TrailRenderer::Finalize()
{
	// Called after the GPU has been flushed (RizeEngine::Finalize)
	alphaPso_.Reset();
	additivePso_.Reset();
	rootSignature_.Reset();
	dxCommon_ = nullptr;
}

void TrailRenderer::Draw(ID3D12GraphicsCommandList* cmdList, const Camera& camera, const std::vector<TrailVertex>& vertices, bool additive, uint32_t textureHandle)
{
	if (vertices.empty() || !dxCommon_ || !alphaPso_ || !additivePso_) return;
	const size_t bytes = vertices.size() * sizeof(TrailVertex);
	const D3D12_GPU_VIRTUAL_ADDRESS address = dxCommon_->UploadData(vertices.data(), bytes, sizeof(float));
	if (address == 0) return;

	TextureManager* textures = TextureManager::GetInstance();
	textures->SetDescriptorHeaps(cmdList);
	cmdList->SetGraphicsRootSignature(rootSignature_.Get());
	cmdList->SetPipelineState(additive ? additivePso_.Get() : alphaPso_.Get());
	cmdList->SetGraphicsRoot32BitConstants(0, 16, &camera.GetViewProjectionMatrix(), 0);
	cmdList->SetGraphicsRootDescriptorTable(1, textures->GetGPUHandle(textureHandle != 0 ? textureHandle : textures->GetDefaultWhiteTexture()));
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW view{ address, static_cast<UINT>(bytes), sizeof(TrailVertex) };
	cmdList->IASetVertexBuffers(0, 1, &view);
	cmdList->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
}
