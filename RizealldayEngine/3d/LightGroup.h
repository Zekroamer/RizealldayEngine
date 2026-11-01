#pragma once
#include "3d/DirectionalLight.h"
#include "3d/PointLight.h"
#include "3d/SpotLight.h"
#include "math/Vector3.h"
#include "math/Matrix4x4.h"
#include "math/MathUtil.h"
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <memory>
#include <vector>

class DirectXCommon;
class Camera;

struct LightGroupGPU {
    Vector3 ambientColor;
    float ambientIntensity;
    DirectionalLightData directionalLight;
    Matrix4x4 cascadeMatrix[4];
    float cascadeEnd[4];
    float cascadeDepthScale[4];
    float cascadeNormalScale[4];
    int32_t shadowActive;
    float shadowBias;
    float shadowStrength;
    float shadowNormalBias;
    float viewDepthRow[4];
    float screenSize[2];
    float clusterNear;
    float clusterFar;
    float environmentIntensity;
    float environmentMaxMip;
    float pad[2];
};

// One point or spot light in the structured buffer of Object3d.PS.hlsl
struct LightGPU {
    Vector3 position;
    float radius;
    Vector3 color;
    float intensity;
    Vector3 direction;
    float decay;
    float cosAngle;
    float cosFalloffStart;
    int32_t isSpot;
    float pad;
};

class LightGroup {
private:
    struct PrivateTag {};

public:
    static constexpr size_t kMaxLights = 1024;
    static constexpr int kMaxCascades = 4;

    static void StaticInitialize(DirectXCommon* dxCommon);
    static std::unique_ptr<LightGroup> Create();

    explicit LightGroup(PrivateTag) {}
    ~LightGroup() = default;

    void Initialize();
    void Update();
    // Binds root parameter `rootParameterIndex` (constants) and 11, 12, 13 (lights, clusters, cluster light lists)
    void Draw(ID3D12GraphicsCommandList* cmdList, uint32_t rootParameterIndex);
    void SetDefaultLighting();

    void SetAmbientColor(const Vector3& color) { ambientColor_ = color; }
    void SetAmbientIntensity(float intensity) { ambientIntensity_ = intensity; }
    void SetCameraPosition(const Vector3&) {}
    Vector3 GetCameraPosition() const { return Vector3{0.0f, 0.0f, 0.0f}; }

    void SetShadowActive(int32_t active) { shadowActive_ = active; }
    void SetShadowBias(float bias) { shadowBias_ = bias; }
    void SetShadowStrength(float strength) { shadowStrength_ = strength; }
    void SetShadowNormalBias(float normalBias) { shadowNormalBias_ = normalBias; }
    // View depth `end` is where this cascade stops; unused cascades end at 0
    void SetCascade(int index, const Matrix4x4& lightViewProjection, float end, float depthScale, float normalScale);
    void ClearCascades();

    void SetEnvironment(float intensity, float maxMip) { environmentIntensity_ = intensity; environmentMaxMip_ = maxMip; }

    DirectionalLightData& GetDirectionalLight() { return directionalLight_; }
    void ClearLights();
    void AddPointLight(const PointLightData& light);
    void AddSpotLight(const SpotLightData& light);
    size_t GetLightCount() const { return lights_.size(); }

    // Culls the lights for one view. Call before drawing into that view (screen size = its render target).
    void SetView(const Camera& camera, uint32_t width, uint32_t height);

private:
    LightGroup() = default;
    static DirectXCommon* sDxCommon_;

    // CPU copy of the light CB; uploaded to the per-frame upload allocator at bind time
    LightGroupGPU data_ = {};
    uint64_t uploadFrame_ = UINT64_MAX;
    uint64_t version_ = 0;
    uint64_t uploadedVersion_ = UINT64_MAX;
    D3D12_GPU_VIRTUAL_ADDRESS uploadedAddress_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS uploadedLights_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS uploadedClusters_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS uploadedIndices_ = 0;

    Vector3 ambientColor_ = {1.0f, 1.0f, 1.0f};
    float ambientIntensity_ = 0.1f;

    DirectionalLightData directionalLight_;
    std::vector<LightGPU> lights_;
    std::vector<uint32_t> clusters_;
    std::vector<uint32_t> clusterIndices_;

    Matrix4x4 cascadeMatrix_[kMaxCascades];
    float cascadeEnd_[kMaxCascades] = {};
    float cascadeDepthScale_[kMaxCascades] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float cascadeNormalScale_[kMaxCascades] = { 1.0f, 1.0f, 1.0f, 1.0f };
    int32_t shadowActive_ = 1;
    float shadowBias_ = 0.002f;
    float shadowStrength_ = 1.0f;
    float shadowNormalBias_ = 0.02f;
    float environmentIntensity_ = 1.0f;
    float environmentMaxMip_ = 0.0f;
    float viewDepthRow_[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
    float screenSize_[2] = { 1.0f, 1.0f };
    float clusterNear_ = 0.1f;
    float clusterFar_ = 1000.0f;
};
