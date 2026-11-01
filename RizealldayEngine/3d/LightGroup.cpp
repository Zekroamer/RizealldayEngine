#include "3d/LightGroup.h"
#include "3d/Camera.h"
#include "base/DirectXCommon.h"
#include "render/LightClusters.h"
#include <cassert>
#include <cmath>
#include <cstring>

DirectXCommon* LightGroup::sDxCommon_ = nullptr;

void LightGroup::StaticInitialize(DirectXCommon* dxCommon) {
    sDxCommon_ = dxCommon;
}

std::unique_ptr<LightGroup> LightGroup::Create() {
    auto instance = std::make_unique<LightGroup>(PrivateTag{});
    instance->Initialize();
    return instance;
}

void LightGroup::Initialize() {
    assert(sDxCommon_);

    SetDefaultLighting();
    Update();
}

void LightGroup::SetDefaultLighting() {
    ambientColor_ = {1.0f, 1.0f, 1.0f};
    ambientIntensity_ = 0.1f;

    directionalLight_.active = 1;
    directionalLight_.color = {1.0f, 1.0f, 1.0f, 1.0f};
    directionalLight_.direction = {0.0f, -1.0f, 0.0f};
    directionalLight_.intensity = 1.0f;

    lights_.clear();
    ClearCascades();
}

void LightGroup::ClearLights() {
    lights_.clear();
}

void LightGroup::AddPointLight(const PointLightData& light) {
    if (lights_.size() >= kMaxLights) return;
    LightGPU gpu{};
    gpu.position = light.position;
    gpu.radius = light.radius;
    gpu.color = { light.color.x, light.color.y, light.color.z };
    gpu.intensity = light.intensity;
    gpu.decay = light.decay;
    lights_.push_back(gpu);
}

void LightGroup::AddSpotLight(const SpotLightData& light) {
    if (lights_.size() >= kMaxLights) return;
    LightGPU gpu{};
    gpu.position = light.position;
    gpu.radius = light.distance;
    gpu.color = { light.color.x, light.color.y, light.color.z };
    gpu.intensity = light.intensity;
    const float len = std::sqrt(light.direction.x * light.direction.x + light.direction.y * light.direction.y + light.direction.z * light.direction.z);
    gpu.direction = len > 0.0001f ? Vector3{ light.direction.x / len, light.direction.y / len, light.direction.z / len } : Vector3{ 0.0f, -1.0f, 0.0f };
    gpu.decay = light.decay;
    gpu.cosAngle = light.cosAngle;
    gpu.cosFalloffStart = light.cosFalloffStart;
    gpu.isSpot = 1;
    lights_.push_back(gpu);
}

void LightGroup::SetCascade(int index, const Matrix4x4& lightViewProjection, float end, float depthScale, float normalScale) {
    if (index < 0 || index >= kMaxCascades) return;
    cascadeMatrix_[index] = lightViewProjection;
    cascadeEnd_[index] = end;
    cascadeDepthScale_[index] = depthScale;
    cascadeNormalScale_[index] = normalScale;
}

void LightGroup::ClearCascades() {
    for (int i = 0; i < kMaxCascades; ++i) {
        cascadeMatrix_[i] = Mathematics::MakeIdentity4x4();
        cascadeEnd_[i] = 0.0f;
        cascadeDepthScale_[i] = 1.0f;
        cascadeNormalScale_[i] = 1.0f;
    }
}

void LightGroup::SetView(const Camera& camera, uint32_t width, uint32_t height) {
    const Matrix4x4& view = camera.GetViewMatrix();
    const Matrix4x4& proj = camera.GetProjectionMatrix();
    for (int i = 0; i < 4; ++i) viewDepthRow_[i] = view.m[i][2];
    screenSize_[0] = static_cast<float>((std::max)(width, 1u));
    screenSize_[1] = static_cast<float>((std::max)(height, 1u));
    clusterNear_ = (std::max)(camera.nearClip, 0.001f);
    clusterFar_ = (std::max)(camera.farClip, clusterNear_ * 2.0f);

    LightClusters::Projection projection;
    projection.orthographic = proj.m[3][3] != 0.0f;
    projection.tanHalfFovX = proj.m[0][0] != 0.0f ? 1.0f / proj.m[0][0] : 1.0f;
    projection.tanHalfFovY = proj.m[1][1] != 0.0f ? 1.0f / proj.m[1][1] : 1.0f;
    projection.nearZ = clusterNear_;
    projection.farZ = clusterFar_;

    std::vector<LightClusters::Sphere> spheres;
    spheres.reserve(lights_.size());
    for (const LightGPU& light : lights_) {
        const Vector3& p = light.position;
        LightClusters::Sphere sphere;
        sphere.viewPosition = {
            p.x * view.m[0][0] + p.y * view.m[1][0] + p.z * view.m[2][0] + view.m[3][0],
            p.x * view.m[0][1] + p.y * view.m[1][1] + p.z * view.m[2][1] + view.m[3][1],
            p.x * view.m[0][2] + p.y * view.m[1][2] + p.z * view.m[2][2] + view.m[3][2] };
        sphere.radius = light.radius;
        spheres.push_back(sphere);
    }
    LightClusters::Build(spheres, projection, clusters_, clusterIndices_);
    Update();
}

void LightGroup::Update() {
    data_.ambientColor = ambientColor_;
    data_.ambientIntensity = ambientIntensity_;
    data_.directionalLight = directionalLight_;

    for (int i = 0; i < kMaxCascades; ++i) {
        data_.cascadeMatrix[i] = cascadeMatrix_[i];
        data_.cascadeEnd[i] = cascadeEnd_[i];
        data_.cascadeDepthScale[i] = cascadeDepthScale_[i];
        data_.cascadeNormalScale[i] = cascadeNormalScale_[i];
    }
    data_.shadowActive = shadowActive_;
    data_.shadowBias = shadowBias_;
    data_.shadowStrength = shadowStrength_;
    data_.shadowNormalBias = shadowNormalBias_;
    std::memcpy(data_.viewDepthRow, viewDepthRow_, sizeof(viewDepthRow_));
    data_.screenSize[0] = screenSize_[0];
    data_.screenSize[1] = screenSize_[1];
    data_.clusterNear = clusterNear_;
    data_.clusterFar = clusterFar_;
    data_.environmentIntensity = environmentIntensity_;
    data_.environmentMaxMip = environmentMaxMip_;
    ++version_;
}

void LightGroup::Draw(ID3D12GraphicsCommandList* cmdList, uint32_t rootParameterIndex) {
    if (!sDxCommon_ || sDxCommon_->IsDeviceLost()) {
        return;
    }
    const uint64_t frame = sDxCommon_->GetFrameIndex();
    if (uploadFrame_ != frame || uploadedVersion_ != version_ || uploadedAddress_ == 0) {
        uploadedAddress_ = sDxCommon_->UploadConstants(data_);
        // Root SRVs must point at something even with no lights
        static const uint32_t kEmptyClusters[LightClusters::kCount] = {};
        static const LightGPU kNoLight{};
        static const uint32_t kNoIndex = 0;
        const bool hasClusters = clusters_.size() == LightClusters::kCount;
        uploadedClusters_ = hasClusters ? sDxCommon_->UploadData(clusters_.data(), clusters_.size() * sizeof(uint32_t))
                                        : sDxCommon_->UploadData(kEmptyClusters, sizeof(kEmptyClusters));
        uploadedLights_ = lights_.empty() ? sDxCommon_->UploadData(&kNoLight, sizeof(kNoLight))
                                          : sDxCommon_->UploadData(lights_.data(), lights_.size() * sizeof(LightGPU));
        uploadedIndices_ = clusterIndices_.empty() ? sDxCommon_->UploadData(&kNoIndex, sizeof(kNoIndex))
                                                   : sDxCommon_->UploadData(clusterIndices_.data(), clusterIndices_.size() * sizeof(uint32_t));
        uploadFrame_ = frame;
        uploadedVersion_ = version_;
    }
    if (uploadedAddress_ != 0) {
        cmdList->SetGraphicsRootConstantBufferView(rootParameterIndex, uploadedAddress_);
    }
    if (uploadedLights_ && uploadedClusters_ && uploadedIndices_) {
        cmdList->SetGraphicsRootShaderResourceView(11, uploadedLights_);
        cmdList->SetGraphicsRootShaderResourceView(12, uploadedClusters_);
        cmdList->SetGraphicsRootShaderResourceView(13, uploadedIndices_);
    }
}
