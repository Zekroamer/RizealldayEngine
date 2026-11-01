#pragma once
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <string>
#include <vector>
#include "base/BlendMode.h"
#include "base/GeometryShaderMode.h"

class DXCManager;

class GraphicPipeline
{
public:
    void Initialize(ID3D12Device* pDevice, DXCManager* dxc);
    void Bind(ID3D12GraphicsCommandList* commandList, BlendMode mode = BlendMode::Opaque, GeometryShaderMode gsMode = GeometryShaderMode::None);
    void Bind(ID3D12GraphicsCommandList* commandList, BlendMode mode, bool useGeometryShader);
    /// Same shaders for the 8-bit back buffer; Bind() targets the HDR scene format
    void BindForBackBuffer(ID3D12GraphicsCommandList* commandList, BlendMode mode = BlendMode::Opaque);
    ID3D12RootSignature* GetRootSignature() const { return rootSignature.Get(); }
    /// Switches to the outline PSO after a mesh was drawn with Bind() (root arguments stay bound)
    void BindOutline(ID3D12GraphicsCommandList* commandList);
    void BindInstanced(ID3D12GraphicsCommandList* commandList);
    bool HasOutline() const { return psos_.outline != nullptr; }

    /// Shader hot reload: recompiles the Object3d shaders from `shaderDir` and swaps in new PSOs only when every
    /// shader and PSO succeeds (otherwise the old ones stay and `error` says why). Replaced PSOs go to `retired`
    /// for the caller to DeferRelease, because frames in flight may still use them.
    bool Reload(DXCManager* dxc, const std::wstring& shaderDir, std::vector<Microsoft::WRL::ComPtr<ID3D12PipelineState>>& retired, std::string* error);

private:
    struct ShaderBlobs {
        Microsoft::WRL::ComPtr<IDxcBlob> vs, ps, gs, explode, outlineVs, outlinePs, instancedVs;
    };
    struct PsoSet {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> standard[static_cast<int>(BlendMode::Count)];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> backBuffer[static_cast<int>(BlendMode::Count)];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> gs[static_cast<int>(BlendMode::Count)];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> explode[static_cast<int>(BlendMode::Count)];
        Microsoft::WRL::ComPtr<ID3D12PipelineState> outline;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> instanced; // opaque only
    };

    void CreateRootSignature();
    bool CreatePso(BlendMode mode, const ShaderBlobs& blobs, PsoSet& out);
    D3D12_BLEND_DESC MakeBlendDesc(BlendMode mode);

    ID3D12Device* device = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    PsoSet psos_;
};
