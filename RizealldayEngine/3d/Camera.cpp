#include "3d/Camera.h"
#include "math/MathUtil.h"
#include "base/DirectXCommon.h"
#include <algorithm>

DirectXCommon* Camera::sDxCommon_ = nullptr;
CameraForGPU Camera::sDefaultData_ = { { 0.0f, 0.0f, -10.0f }, 0.0f };
uint64_t Camera::sDefaultUploadFrame_ = UINT64_MAX;
D3D12_GPU_VIRTUAL_ADDRESS Camera::sDefaultUploadAddress_ = 0;

void Camera::StaticInitialize(DirectXCommon* dxCommon) {
    sDxCommon_ = dxCommon;
    sDefaultUploadFrame_ = UINT64_MAX;
    sDefaultUploadAddress_ = 0;
}

D3D12_GPU_VIRTUAL_ADDRESS Camera::GetDefaultGPUVirtualAddress() {
    if (!sDxCommon_ || sDxCommon_->IsDeviceLost()) {
        return 0;
    }
    // The default data never changes, so one upload per frame is enough
    const uint64_t frame = sDxCommon_->GetFrameIndex();
    if (sDefaultUploadFrame_ != frame || sDefaultUploadAddress_ == 0) {
        sDefaultUploadAddress_ = sDxCommon_->UploadConstants(sDefaultData_);
        sDefaultUploadFrame_ = frame;
    }
    return sDefaultUploadAddress_;
}

D3D12_GPU_VIRTUAL_ADDRESS Camera::GetGPUVirtualAddress() const {
    if (!sDxCommon_ || sDxCommon_->IsDeviceLost()) {
        return 0;
    }
    const uint64_t frame = sDxCommon_->GetFrameIndex();
    if (uploadFrame_ != frame || uploadedVersion_ != version_ || uploadedAddress_ == 0) {
        uploadedAddress_ = sDxCommon_->UploadConstants(data_);
        uploadFrame_ = frame;
        uploadedVersion_ = version_;
    }
    return uploadedAddress_;
}

Matrix4x4 Camera::MakeProjection() const {
    if (!orthographic) {
        return Mathematics::MakePerspectiveFovMatrix(fovY, aspectRatio, nearClip, farClip);
    }
    const float halfH = (std::max)(orthoSize, 0.01f);
    const float halfW = halfH * aspectRatio;
    return Mathematics::MakeOrthographicMatrix(-halfW, halfH, halfW, -halfH, nearClip, farClip);
}

void Camera::Initialize() {
    matView_ = Mathematics::MakeIdentity4x4();
    matProjection_ = MakeProjection();
    matViewProjection_ = Mathematics::Multiply(matView_, matProjection_);
    data_.worldPosition = translation;
    data_.pad = 0.0f;
    ++version_;
}

void Camera::Update() {
    Matrix4x4 cameraWorldMatrix = Mathematics::MakeAffineMatrix({1.0f, 1.0f, 1.0f}, rotation, translation);
    matView_ = Mathematics::Inverse(cameraWorldMatrix);
    matProjection_ = MakeProjection();
    matViewProjection_ = Mathematics::Multiply(matView_, matProjection_);
    data_.worldPosition = translation;
    data_.pad = 0.0f;
    ++version_;
}
