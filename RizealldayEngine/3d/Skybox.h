#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include "math/Vector3.h"
#include "math/Vector4.h"
#include "math/Matrix4x4.h"

enum class SkyboxMode {
	Gradient = 0,
	SolidColor = 1,
	Cubemap = 2
};

struct SkyboxConstants {
	Matrix4x4 invViewProj;        // 64 bytes
	Vector4 topColor;             // 16 bytes
	Vector4 horizonColor;         // 16 bytes
	Vector4 bottomColor;          // 16 bytes
	Vector4 solidColor;           // 16 bytes
	float rotation = 0.0f;        // 4 bytes
	float exposure = 1.0f;        // 4 bytes
	int32_t mode = 0;             // 4 bytes (0: Gradient, 1: SolidColor, 2: Cubemap)
	float pad = 0.0f;             // 4 bytes
};

class DirectXCommon;
class DXCManager;
class Camera;

class Skybox {
public:
	Skybox() = default;
	~Skybox();

	void Initialize(DirectXCommon* dxCommon, DXCManager* dxc);
	bool LoadCubemap(const std::string& filePath);
	void Draw(ID3D12GraphicsCommandList* cmdList, const Camera& camera);
	// The sky into the bound HDR target without a depth buffer (environment capture)
	void DrawToTarget(ID3D12GraphicsCommandList* cmdList, const Matrix4x4& invViewProj);
	SkyboxConstants BuildConstants(const Matrix4x4& invViewProj) const;

	SkyboxMode GetMode() const { return mode_; }
	void SetMode(SkyboxMode mode) { mode_ = mode; }

	const Vector4& GetTopColor() const { return topColor_; }
	void SetTopColor(const Vector4& col) { topColor_ = col; }

	const Vector4& GetHorizonColor() const { return horizonColor_; }
	void SetHorizonColor(const Vector4& col) { horizonColor_ = col; }

	const Vector4& GetBottomColor() const { return bottomColor_; }
	void SetBottomColor(const Vector4& col) { bottomColor_ = col; }

	const Vector4& GetSolidColor() const { return solidColor_; }
	void SetSolidColor(const Vector4& col) { solidColor_ = col; }

	const std::string& GetCubemapPath() const { return cubemapPath_; }
	void SetCubemapPath(const std::string& path) { cubemapPath_ = path; }

	float GetRotation() const { return rotation_; }
	void SetRotation(float r) { rotation_ = r; }

	float GetExposure() const { return exposure_; }
	void SetExposure(float e) { exposure_ = e; }

	bool HasValidCubemap() const { return hasValidCubemap_; }
	uint32_t GetCubemapTextureHandle() const { return cubemapTextureHandle_; }

private:
	void CreateRootSignature();
	void CreatePSO(DXCManager* dxc);

	DirectXCommon* dxCommon_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoNoDepth_;
	void DrawWith(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* pso, const Matrix4x4& invViewProj);

	SkyboxMode mode_ = SkyboxMode::Gradient;
	Vector4 topColor_ = { 0.22f, 0.45f, 0.78f, 1.0f };
	Vector4 horizonColor_ = { 0.72f, 0.82f, 0.92f, 1.0f };
	Vector4 bottomColor_ = { 0.35f, 0.37f, 0.40f, 1.0f };
	Vector4 solidColor_ = { 0.157f, 0.157f, 0.157f, 1.0f };
	std::string cubemapPath_ = "Resources/skybox.dds";
	float rotation_ = 0.0f;
	float exposure_ = 1.0f;

	uint32_t cubemapTextureHandle_ = 0;
	bool hasValidCubemap_ = false;
};
