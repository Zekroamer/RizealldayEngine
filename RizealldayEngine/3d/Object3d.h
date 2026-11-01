#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include "3d/WorldTransform.h"
#include "math/VertexData.h"
#include "base/BlendMode.h"
#include "base/GeometryShaderMode.h"
#include "3d/AnimationPlayer.h"

class Camera;
class GraphicPipeline;
class DirectXCommon;
class LightGroup;
class ShadowMap;

class Object3d {
public:
	Object3d() = default;
	~Object3d();
	static void StaticInitialize(DirectXCommon* dxCommon);
	static void StaticSetPipeline(GraphicPipeline* p);
	static void StaticSetLightGroup(LightGroup* lights);
	static void StaticSetShadowMap(ShadowMap* shadowMap);
	static void StaticSetEnvironment(D3D12_GPU_DESCRIPTOR_HANDLE cube);
	// Lights, camera, shadow map and environment: what every object of a view shares
	static void BindSceneData(ID3D12GraphicsCommandList* cmdList, Camera* camera);

	void Initialize();
	void Update(float deltaTime = 1.0f / 60.0f);
	void Draw(ID3D12GraphicsCommandList* cmdList);

	void SetModel(Model* model, bool resetAnim = true);
	Model* GetModel() const { return model_; }
	void SetCamera(Camera* camera) { camera_ = camera; }

	// Animation controls (one clip of this model; see AnimationPlayer for crossfades and clips of other models)
	void PlayAnimation(int animIndex, bool loop = true, float speed = 1.0f);
	void SetCurrentAnimationIndex(int animIndex);
	int GetCurrentAnimationIndex() const { return player_.Current().clip; }
	void SetAnimationTime(float time);
	void ResetAnimationTime() { SetAnimationTime(0.0f); }
	float GetAnimationTime() const { return player_.Current().time; }
	float GetAnimationDuration() const;
	void SetAnimationSpeed(float speed) { player_.Current().speed = speed; }
	float GetAnimationSpeed() const { return player_.Current().speed; }
	void SetAnimationLoop(bool loop) { player_.Current().loop = loop; }
	void SetLoopAnimation(bool loop) { player_.Current().loop = loop; }
	bool IsAnimationLoop() const { return player_.Current().loop; }
	/// Crossfades to `clip` of `source` (nullptr = this object's model) over `seconds`
	void CrossFade(const Model* source, int clip, float seconds, bool loop = true, float speed = 1.0f);
	AnimationPlayer& GetAnimationPlayer() { return player_; }
	const AnimationPlayer& GetAnimationPlayer() const { return player_; }
	/// Advances animation time only (game update). Update() then evaluates the pose without moving time again.
	void AdvanceAnimation(float deltaTime);
	void CopyBonePaletteFrom(const Object3d& other);
	/// Uploads this frame's bone palette (b1 VS, 128 matrices) and returns its GPU address.
	/// Meshes without bones share one identity palette per frame instead of uploading 8 KB each.
	D3D12_GPU_VIRTUAL_ADDRESS UploadBonePalette() const;

	/// Blend mode for rendering (default: Opaque)
	BlendMode blendMode = BlendMode::Opaque;

	/// Setter / Getter blend mode
	void SetBlendMode(BlendMode mode) { blendMode = mode; }
	BlendMode GetBlendMode() const { return blendMode; }

	bool useGeometryShader = false;
	void SetUseGeometryShader(bool use) { useGeometryShader = use; }
	bool GetUseGeometryShader() const { return useGeometryShader; }

	GeometryShaderMode geometryShaderMode = GeometryShaderMode::None;
	void SetGeometryShaderMode(GeometryShaderMode mode) { geometryShaderMode = mode; }
	GeometryShaderMode GetGeometryShaderMode() const { return geometryShaderMode; }

	void SetTextureHandle(uint32_t handle) { textureHandle_ = handle; }
	void SetMaterialMaps(const MaterialMaps& maps) { materialMaps_ = maps; }
	const MaterialMaps& GetMaterialMaps() const { return materialMaps_; }
	uint32_t GetTextureHandle() const { return textureHandle_; }

	/// Access material to modify color/alpha
	/// (CPU copy; uploaded for each draw, so changes apply to the next Draw call)
	Material* GetMaterial() { return &material_; }

	WorldTransform worldTransform;

private:
	static DirectXCommon* sDxCommon_;
	static GraphicPipeline* sPipeline_;
	static LightGroup* sLightGroup_;
	static D3D12_GPU_DESCRIPTOR_HANDLE sEnvironment_;
	MaterialMaps materialMaps_;
	static ShadowMap* sShadowMap_;

	Model* model_ = nullptr;
	Camera* camera_ = nullptr;
	uint32_t textureHandle_ = 0;
	// Per-draw constants are kept on the CPU and uploaded through DirectXCommon's per-frame allocator
	Material material_{};

	// Bone palette (128 matrices * 64 bytes = 8192 bytes); only uploaded for meshes that have bones
	static constexpr size_t kMaxBones = 128;
	std::vector<Matrix4x4> bonePalette_;
	std::vector<Matrix4x4> boneTransforms_;
	void ResetBonePalette();

	AnimationPlayer player_;
};
