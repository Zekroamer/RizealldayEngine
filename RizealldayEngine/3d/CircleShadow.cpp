#include "3d/CircleShadow.h"
#include "3d/Camera.h"
#include "base/DirectXCommon.h"
#include "base/Profiler.h"
#include "base/DXCManager.h"
#include "math/MathUtil.h"
#include <cassert>

// ============================================================
// Static member definitions
// ============================================================
DirectXCommon* CircleShadow::sDxCommon_ = nullptr;
Microsoft::WRL::ComPtr<ID3D12PipelineState> CircleShadow::sPipelineState_;
Microsoft::WRL::ComPtr<ID3D12RootSignature> CircleShadow::sRootSignature_;
bool CircleShadow::sInitialized_ = false;

// ============================================================
// StaticInitialize
// ============================================================
void CircleShadow::StaticInitialize(DirectXCommon* dxCommon, DXCManager* dxcManager)
{
    if (sInitialized_) return;
    sDxCommon_ = dxCommon;
    CreateRootSignature();
    CreatePipelineState(dxcManager);
    sInitialized_ = true;
}

// ============================================================
// CreateRootSignature
// Root parameters:
//   [0] Material CBV  (b0, Pixel)  - warna, radius, softness, center
//   [1] Transform CBV (b0, Vertex) - WVP + World
//   [2] Texture SRV   (t0, Pixel)  - dummy / tidak dipakai
// ============================================================
void CircleShadow::CreateRootSignature()
{
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[3] = {};
    // [0] Material CBV (Pixel)
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].Descriptor.ShaderRegister = 0;
    // [1] Transform CBV (Vertex)
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].Descriptor.ShaderRegister = 0;
    // [2] Texture SRV table (Pixel) - optional
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].DescriptorTable.pDescriptorRanges = &srvRange;
    params[2].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.pParameters = params;
    desc.NumParameters = _countof(params);
    desc.pStaticSamplers = &sampler;
    desc.NumStaticSamplers = 1;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> blob = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> err  = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    assert(SUCCEEDED(hr));
    hr = sDxCommon_->GetDevice()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&sRootSignature_));
    assert(SUCCEEDED(hr));
}

// ============================================================
// CreatePipelineState
// Uses CircleShadow.VS.hlsl and CircleShadow.PS.hlsl
// Input layout: POSITION4 + TEXCOORD2 + NORMAL3 (3 elements, no bone skinning)
// ============================================================
void CircleShadow::CreatePipelineState(DXCManager* dxc)
{
    // Simple input layout: no bone skinning
    D3D12_INPUT_ELEMENT_DESC inputElems[3] = {};
    inputElems[0].SemanticName = "POSITION";
    inputElems[0].SemanticIndex = 0;
    inputElems[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElems[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElems[1].SemanticName = "TEXCOORD";
    inputElems[1].SemanticIndex = 0;
    inputElems[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElems[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElems[2].SemanticName = "NORMAL";
    inputElems[2].SemanticIndex = 0;
    inputElems[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElems[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    // Compile dedicated VS and PS for CircleShadow
    Microsoft::WRL::ComPtr<IDxcBlob> vs = dxc->CompileShader(L"shaders/CircleShadow.VS.hlsl", L"vs_6_0",
        dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
    Microsoft::WRL::ComPtr<IDxcBlob> ps = dxc->CompileShader(L"shaders/CircleShadow.PS.hlsl", L"ps_6_0",
        dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
    assert(vs && ps);

    // Alpha blending for transparent shadow
    D3D12_BLEND_DESC blendDesc{};
    auto& rt = blendDesc.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    rt.BlendEnable    = TRUE;
    rt.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp        = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha  = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt.BlendOpAlpha   = D3D12_BLEND_OP_ADD;

    // Rasterizer: render from both sides
    D3D12_RASTERIZER_DESC rastDesc{};
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode = D3D12_CULL_MODE_NONE;

    // Depth: read but do not write (so shadow does not block depth)
    D3D12_DEPTH_STENCIL_DESC dsDesc{};
    dsDesc.DepthEnable    = TRUE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = sRootSignature_.Get();
    psoDesc.InputLayout    = { inputElems, _countof(inputElems) };
    psoDesc.VS             = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS             = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState     = blendDesc;
    psoDesc.RasterizerState = rastDesc;
    psoDesc.DepthStencilState = dsDesc;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0]  = DirectXCommon::kSceneColorFormat;
    psoDesc.DSVFormat      = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask     = D3D12_DEFAULT_SAMPLE_MASK;

    HRESULT hr = sDxCommon_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&sPipelineState_));
    assert(SUCCEEDED(hr));
}

// ============================================================
// Initialize (per instance)
// ============================================================
void CircleShadow::Initialize()
{
    auto* device = sDxCommon_->GetDevice();
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    // Helper lambda: buat upload buffer dengan alignment 256 bytes
    auto makeBuffer = [&](size_t size) -> Microsoft::WRL::ComPtr<ID3D12Resource> {
        D3D12_RESOURCE_DESC d{};
        d.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width            = (size + 255) & ~255ull; // 256-byte alignment
        d.Height           = 1;
        d.DepthOrArraySize = 1;
        d.MipLevels        = 1;
        d.SampleDesc.Count = 1;
        d.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> res;
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res));
        return res;
    };

    // ---- Vertex Buffer: quad datar 4 titik (POSITION4 + TEXCOORD2 + NORMAL3) ----
    struct ShadowVertex { Vector4 pos; Vector2 uv; Vector3 normal; };
    size_t vbSize = sizeof(ShadowVertex) * 4;
    vertexResource_ = makeBuffer(vbSize);

    ShadowVertex* verts = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&verts));
    if (verts) {
        // Quad datar di sumbu XZ (Y=0 lokal), UV sudut 0-1
        verts[0] = { {-1.0f, 0.0f, -1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f} };
        verts[1] = { { 1.0f, 0.0f, -1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f} };
        verts[2] = { {-1.0f, 0.0f,  1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f} };
        verts[3] = { { 1.0f, 0.0f,  1.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f} };
    }
    vbView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vbView_.SizeInBytes    = static_cast<UINT>(sizeof(ShadowVertex) * 4);
    vbView_.StrideInBytes  = sizeof(ShadowVertex);

    // ---- Index Buffer: 2 segitiga (quad) ----
    size_t ibSize = sizeof(uint32_t) * 6;
    indexResource_ = makeBuffer(ibSize);

    uint32_t* idxData = nullptr;
    indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&idxData));
    if (idxData) {
        idxData[0] = 0; idxData[1] = 1; idxData[2] = 2;
        idxData[3] = 2; idxData[4] = 1; idxData[5] = 3;
    }
    ibView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    ibView_.SizeInBytes    = static_cast<UINT>(ibSize);
    ibView_.Format         = DXGI_FORMAT_R32_UINT;

    // ---- Material / Transform: CPU copies, uploaded per frame at draw time ----
    material_ = CircleShadowMaterialGPU{}; // Default values
    transform_.WVP   = Mathematics::MakeIdentity4x4();
    transform_.World = Mathematics::MakeIdentity4x4();
}

// ============================================================
// Update
// ============================================================
void CircleShadow::Update(const Vector3& characterPos, float floorY, float radius, float opacity)
{
    position_ = { characterPos.x, floorY, characterPos.z };
    radius_   = radius;
    opacity_  = opacity;
    material_.color.w = opacity;
}

// ============================================================
// Draw
// ============================================================
void CircleShadow::Draw(ID3D12GraphicsCommandList* cmdList, Camera* camera)
{
    if (!sInitialized_ || !sDxCommon_ || sDxCommon_->IsDeviceLost()) return;

    // Hitung transform: posisi di lantai + scale sesuai radius
    Matrix4x4 scale     = Mathematics::MakeScaleMatrix({ radius_, 1.0f, radius_ });
    Matrix4x4 translate = Mathematics::MakeTranslateMatrix(position_);
    Matrix4x4 world     = Mathematics::Multiply(scale, translate);

    const Matrix4x4& viewProj = camera->GetViewProjectionMatrix();
    transform_.World = world;
    transform_.WVP   = Mathematics::Multiply(world, viewProj);

    // Bind pipeline khusus circle shadow
    cmdList->SetGraphicsRootSignature(sRootSignature_.Get());
    cmdList->SetPipelineState(sPipelineState_.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &vbView_);
    cmdList->IASetIndexBuffer(&ibView_);

    // Bind constant buffers
    const D3D12_GPU_VIRTUAL_ADDRESS materialAddr  = sDxCommon_->UploadConstants(material_);
    const D3D12_GPU_VIRTUAL_ADDRESS transformAddr = sDxCommon_->UploadConstants(transform_);
    if (materialAddr == 0 || transformAddr == 0) return;
    cmdList->SetGraphicsRootConstantBufferView(0, materialAddr);
    cmdList->SetGraphicsRootConstantBufferView(1, transformAddr);
    // Slot 2 (SRV table) tidak di-set karena PS tidak benar-benar membaca texture

    cmdList->DrawIndexedInstanced(6, 1, 0, 0, 0);
    Profiler::GetInstance()->CountDrawCall();

    // Catatan: Setelah Draw(), pipeline 3D utama perlu di-bind ulang.
    // Object3d::Draw() sudah otomatis melakukan ini dengan memanggil sPipeline_->Bind().
}

// ============================================================
// SetColor
// ============================================================
void CircleShadow::SetColor(const Vector4& color)
{
    material_.color = color;
}
