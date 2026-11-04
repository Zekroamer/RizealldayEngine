#pragma once
#include "math/Vector3.h"
#include "math/Matrix4x4.h"
#include "math/MathUtil.h"
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

class DirectXCommon;

struct CameraForGPU {
	Vector3 worldPosition;
	float pad = 0.0f;
};

class Camera {
public:
	static void StaticInitialize(DirectXCommon* dxCommon);
	static D3D12_GPU_VIRTUAL_ADDRESS GetDefaultGPUVirtualAddress();

	Camera() = default;
	~Camera() = default;

	void Initialize();
	void Update();

	const Matrix4x4& GetViewMatrix() const { return matView_; }
	const Matrix4x4& GetProjectionMatrix() const { return matProjection_; }
	const Matrix4x4& GetViewProjectionMatrix() const { return matViewProjection_; }

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const;

	Vector3 translation = {0.0f, 0.0f, -10.0f};
	Vector3 rotation = {0.0f, 0.0f, 0.0f};

	float fovY = 0.45f;
	float aspectRatio = 1280.0f / 720.0f;
	float nearClip = 0.1f;
	float farClip = 1000.0f;
	bool orthographic = false;
	float orthoSize = 10.0f; // half the visible height in world units (orthographic only)

private:
	Matrix4x4 MakeProjection() const;

	static DirectXCommon* sDxCommon_;
	static CameraForGPU sDefaultData_;
	static uint64_t sDefaultUploadFrame_;
	static D3D12_GPU_VIRTUAL_ADDRESS sDefaultUploadAddress_;

	// CPU copy of the camera CB; uploaded to the per-frame upload allocator on demand
	CameraForGPU data_ = { { 0.0f, 0.0f, -10.0f }, 0.0f };
	uint64_t version_ = 0;
	mutable uint64_t uploadFrame_ = UINT64_MAX;
	mutable uint64_t uploadedVersion_ = UINT64_MAX;
	mutable D3D12_GPU_VIRTUAL_ADDRESS uploadedAddress_ = 0;

	Matrix4x4 matView_ = {};
	Matrix4x4 matProjection_ = {};
	Matrix4x4 matViewProjection_ = {};
};
