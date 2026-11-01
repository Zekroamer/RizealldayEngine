#include "3d/Object3d.h"
#include "3d/Camera.h"
#include "3d/LightGroup.h"
#include "3d/ShadowMap.h"
#include "base/DirectXCommon.h"
#include "base/GraphicPipeline.h"
#include "base/TextureManager.h"
#include "3d/Model.h"
#include <algorithm>
#include <cmath>
#include <cstring>

DirectXCommon* Object3d::sDxCommon_ = nullptr;
GraphicPipeline* Object3d::sPipeline_ = nullptr;
LightGroup* Object3d::sLightGroup_ = nullptr;
ShadowMap* Object3d::sShadowMap_ = nullptr;
D3D12_GPU_DESCRIPTOR_HANDLE Object3d::sEnvironment_{};

void Object3d::StaticInitialize(DirectXCommon* dxCommon) {
    sDxCommon_ = dxCommon;
}

void Object3d::StaticSetPipeline(GraphicPipeline* p) {
    sPipeline_ = p;
}

void Object3d::StaticSetLightGroup(LightGroup* lights) {
    sLightGroup_ = lights;
}

void Object3d::StaticSetShadowMap(ShadowMap* shadowMap) {
    sShadowMap_ = shadowMap;
}

void Object3d::StaticSetEnvironment(D3D12_GPU_DESCRIPTOR_HANDLE cube) {
    sEnvironment_ = cube;
}

void Object3d::BindSceneData(ID3D12GraphicsCommandList* cmdList, Camera* camera) {
    if (sLightGroup_) {
        sLightGroup_->Draw(cmdList, 3);
    }
    D3D12_GPU_VIRTUAL_ADDRESS camAddr = camera ? camera->GetGPUVirtualAddress() : Camera::GetDefaultGPUVirtualAddress();
    if (camAddr != 0) {
        cmdList->SetGraphicsRootConstantBufferView(5, camAddr);
    }
    if (sShadowMap_) {
        cmdList->SetGraphicsRootDescriptorTable(6, sShadowMap_->GetSrvGPUHandle());
    }
    TextureManager* textures = TextureManager::GetInstance();
    const D3D12_GPU_DESCRIPTOR_HANDLE environment = sEnvironment_.ptr != 0 ? sEnvironment_ : textures->GetGPUHandle(textures->GetDefaultCubemapTexture());
    cmdList->SetGraphicsRootDescriptorTable(10, environment);
}

Object3d::~Object3d() = default;

void Object3d::Initialize() {
    // 1. Material (CPU copy, uploaded per draw)
    material_ = Material{};
    material_.color = {1.0f, 1.0f, 1.0f, 1.0f};
    material_.enableLighting = 1;
    material_.lightingType = 4; // Blinn-Phong default
    material_.shininess = 32.0f;
    material_.specularColor = { 1.0f, 1.0f, 1.0f };

    // 2. Bone palette (identity until an animated model updates it)
    ResetBonePalette();

    // 3. World Transform
    worldTransform.Initialize();

    // 4. Animation: the first clip of the model (as before the AnimationPlayer)
    player_.Play(nullptr, 0);
}

void Object3d::ResetBonePalette() {
    bonePalette_.assign(kMaxBones, Mathematics::MakeIdentity4x4());
}

void Object3d::SetModel(Model* model, bool resetAnim) {
    if (model_ != model) {
        model_ = model;
        if (resetAnim) {
            SetAnimationTime(0.0f);
        }
        ResetBonePalette();
    }
}

void Object3d::PlayAnimation(int animIndex, bool loop, float speed) {
    player_.Play(nullptr, animIndex, loop, speed);
}

void Object3d::SetCurrentAnimationIndex(int animIndex) {
    const AnimationPlayer::Track& current = player_.Current();
    if (current.clip != animIndex || current.source != nullptr) {
        player_.Play(nullptr, animIndex, current.loop, current.speed);
    }
}

void Object3d::CrossFade(const Model* source, int clip, float seconds, bool loop, float speed) {
    player_.Play(source == model_ ? nullptr : source, clip, loop, speed, seconds);
}

void Object3d::SetAnimationTime(float time) {
    player_.Current().time = time;
    player_.Current().elapsed = time;
}

float Object3d::GetAnimationDuration() const {
    return AnimationPlayer::Duration(player_.Current(), model_);
}

void Object3d::AdvanceAnimation(float deltaTime) {
    if (model_ && model_->HasBones()) {
        player_.Advance(deltaTime, model_);
    }
}

void Object3d::CopyBonePaletteFrom(const Object3d& other) {
    player_ = other.player_;
    boneTransforms_ = other.boneTransforms_;
    bonePalette_ = other.bonePalette_;
}

void Object3d::Update(float deltaTime) {
    if (camera_) {
        worldTransform.UpdateMatrix(camera_->GetViewProjectionMatrix());
    } else {
        worldTransform.UpdateMatrix(Mathematics::MakeIdentity4x4());
    }

    if (model_ && model_->HasBones()) {
        if (deltaTime != 0.0f) player_.Advance(deltaTime, model_);
        player_.Evaluate(*model_, boneTransforms_);

        if (bonePalette_.size() != kMaxBones) {
            ResetBonePalette();
        }
        size_t copyCount = (std::min)(boneTransforms_.size(), kMaxBones);
        std::memcpy(bonePalette_.data(), boneTransforms_.data(), sizeof(Matrix4x4) * copyCount);
    }
}

D3D12_GPU_VIRTUAL_ADDRESS Object3d::UploadBonePalette() const {
    if (!sDxCommon_ || sDxCommon_->IsDeviceLost()) {
        return 0;
    }
    const size_t paletteBytes = sizeof(Matrix4x4) * kMaxBones;
    if (model_ && model_->HasBones() && bonePalette_.size() == kMaxBones) {
        return sDxCommon_->UploadData(bonePalette_.data(), paletteBytes);
    }

    // Shared identity palette, uploaded once per frame for all static meshes
    static uint64_t sIdentityFrame = ~0ull;
    static D3D12_GPU_VIRTUAL_ADDRESS sIdentityAddress = 0;
    if (sIdentityFrame != sDxCommon_->GetFrameIndex() || sIdentityAddress == 0) {
        DirectXCommon::UploadAllocation alloc = sDxCommon_->AllocateUpload(paletteBytes);
        if (!alloc.cpu) return 0;
        Matrix4x4* dst = static_cast<Matrix4x4*>(alloc.cpu);
        const Matrix4x4 identity = Mathematics::MakeIdentity4x4();
        for (size_t i = 0; i < kMaxBones; ++i) {
            dst[i] = identity;
        }
        sIdentityAddress = alloc.gpu;
        sIdentityFrame = sDxCommon_->GetFrameIndex();
    }
    return sIdentityAddress;
}

void Object3d::Draw(ID3D12GraphicsCommandList* cmdList) {
    if (!cmdList || !sDxCommon_ || sDxCommon_->IsDeviceLost()) {
        return;
    }

    const D3D12_GPU_VIRTUAL_ADDRESS materialAddr = sDxCommon_->UploadConstants(material_);
    const D3D12_GPU_VIRTUAL_ADDRESS transformAddr = worldTransform.Upload();
    const D3D12_GPU_VIRTUAL_ADDRESS boneAddr = UploadBonePalette();
    if (materialAddr == 0 || transformAddr == 0 || boneAddr == 0) {
        return;
    }

    // Bind pipeline with matching blend mode for this object
    if (sPipeline_) {
        GeometryShaderMode gsMode = geometryShaderMode;
        if (gsMode == GeometryShaderMode::None && useGeometryShader) {
            gsMode = GeometryShaderMode::PassThrough;
        }
        sPipeline_->Bind(cmdList, blendMode, gsMode);
    }

    cmdList->SetGraphicsRootConstantBufferView(0, materialAddr);
    cmdList->SetGraphicsRootConstantBufferView(1, transformAddr);

    cmdList->SetGraphicsRootConstantBufferView(4, boneAddr);
    BindSceneData(cmdList, camera_);

    if (model_) {
        model_->Draw(cmdList, textureHandle_, &materialMaps_);
        // Toon outline: the same mesh again, pushed out along its normals, back faces only
        if (material_.outlineWidth > 0.0f && sPipeline_ && sPipeline_->HasOutline()) {
            sPipeline_->BindOutline(cmdList);
            model_->Draw(cmdList, textureHandle_, &materialMaps_);
        }
    }
}
