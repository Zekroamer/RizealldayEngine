#pragma once
#include "math/Matrix4x4.h"
#include "math/Vector4.h"
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <vector>

class DirectXCommon;
class DXCManager;
class Camera;

struct DecalDraw {
	Matrix4x4 world;  // a unit box in this space is painted; the picture lies on its XZ plane
	Vector4 color;
	uint32_t textureHandle = 0;
	float edgeFade = 0.3f;
	bool additive = false;
};

/// Projects textures onto the scene using its depth buffer. The caller binds the color target without depth
/// and makes the depth readable; everything already drawn gets painted, moving objects too.
class DecalRenderer {
public:
	static DecalRenderer* GetInstance();
	void Initialize(DirectXCommon* dxCommon, DXCManager* dxc);
	void Finalize();
	void Draw(ID3D12GraphicsCommandList* cmdList, const Camera& camera, D3D12_GPU_DESCRIPTOR_HANDLE depth, uint32_t width, uint32_t height, const std::vector<DecalDraw>& decals);

private:
	DirectXCommon* dxCommon_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> signature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> alphaPso_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> additivePso_;
};
