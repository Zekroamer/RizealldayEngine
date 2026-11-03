#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <map>
#include <string>
#include "math/Vector3.h"

class DirectXCommon;
class DXCManager;
class RenderTexture;
struct IDxcBlob;

/// What a scene saves. `enabled == false` shows the scene as it was rendered.
struct PostProcessSettings {
	bool enabled = false;
	bool toneMapping = true;
	bool bloom = true;
	bool fxaa = true;
	float exposure = 1.0f;
	float bloomThreshold = 1.0f;
	float bloomIntensity = 0.4f;
	float contrast = 1.0f;
	float saturation = 1.0f;
	Vector3 tint = { 1.0f, 1.0f, 1.0f };
	float vignette = 0.0f;
	float letterbox = 0.0f; // fraction of the height covered at the top and at the bottom
	bool ssao = false;
	float ssaoRadius = 0.6f; // world units
	float ssaoIntensity = 1.0f;
	// Lighting quality of the scene; saved here with the rest of its look, read by the renderer
	int shadowCascades = 0; // 0 = one shadow map over all casters (scenes from before cascades)
	float shadowDistance = 80.0f;
	float environmentIntensity = 1.0f;
	bool fogEnabled = false;
	Vector3 fogColor = { 0.7f, 0.75f, 0.8f };
	float fogDensity = 0.02f;
	float fogHeight = 0.0f;        // world height where it is as dense as fogDensity
	float fogHeightFalloff = 0.1f; // how fast it thins out above that; 0 = the same everywhere
};

/// HDR scene -> 8-bit output. Knows nothing about the editor: it takes textures and settings.
/// Usage per view: Prepare (off-screen passes), bind the output target, Composite. UI is drawn after that.
class PostProcess {
public:
	static constexpr int kMaxScreenEffects = 4;
	static constexpr int kBloomLevels = 5;
	static constexpr int kScreenEffectParams = 16;

	/// The camera of the next Prepare (ambient occlusion rebuilds positions from depth)
	void SetCamera(float tanHalfFovX, float tanHalfFovY, float nearZ, float farZ, bool perspective);
	/// Height of the camera, and the height component of its right, up and forward axes (height fog)
	void SetCameraHeight(float worldY, float rightY, float upY, float forwardY);

	~PostProcess();
	void Initialize(DirectXCommon* dxCommon, DXCManager* dxc);
	/// Recreates the size-dependent targets when the size changed
	void Resize(uint32_t width, uint32_t height);

	/// Runs screen effects, bloom and (when needed) the first half of FXAA. `scene` must have finished rendering.
	void Prepare(ID3D12GraphicsCommandList* cmdList, RenderTexture& scene, const PostProcessSettings& settings, float gameTime);
	/// Draws the result of the last Prepare into the bound target (sRGB 8-bit with a D24S8 depth buffer)
	void Composite(ID3D12GraphicsCommandList* cmdList);
	/// Shows `scene` unchanged in the bound target (Scene view, previews): no Prepare needed
	void Resolve(ID3D12GraphicsCommandList* cmdList, RenderTexture& scene);

	/// Applies a pixel shader (see shaders/Post.hlsli) to the HDR scene, in slot order. Compiled once per file;
	/// a shader that does not compile returns false with the reason in `error` and leaves the slot empty.
	bool SetScreenEffect(int slot, const std::wstring& shaderFile, const float params[kScreenEffectParams], std::string* error);
	void ClearScreenEffect(int slot);
	void ClearScreenEffects();
	bool HasScreenEffect(int slot) const;

private:
	struct Target {
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
		D3D12_GPU_DESCRIPTOR_HANDLE srv{};
		uint32_t rtvIndex = 0;
		uint32_t srvIndex = 0xFFFFFFFFu;
		uint32_t width = 0;
		uint32_t height = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	};
	struct Constants {
		float exposure, contrast, saturation, vignette;
		Vector3 tint;
		float letterbox;
		float bloomIntensity, bloomThreshold;
		uint32_t flags;
		float time;
		float texelSize[2];
		float pad[2];
		float params[kScreenEffectParams];
	};
	struct ScreenEffect {
		ID3D12PipelineState* pso = nullptr;
		float params[kScreenEffectParams] = {};
	};

	void CreateRootSignature();
	Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePso(IDxcBlob* ps, DXGI_FORMAT format, bool withDepthFormat, bool additive);
	void CreateTarget(Target& target, uint32_t rtvIndex, uint32_t width, uint32_t height, DXGI_FORMAT format);
	void ReleaseTarget(Target& target);
	void BeginPass(ID3D12GraphicsCommandList* cmdList, Target& target, bool clear);
	void EndPass(ID3D12GraphicsCommandList* cmdList, Target& target);
	void DrawFullscreen(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* pso, const Constants& constants,
		D3D12_GPU_DESCRIPTOR_HANDLE source, D3D12_GPU_DESCRIPTOR_HANDLE bloom, D3D12_GPU_DESCRIPTOR_HANDLE depth);

	DirectXCommon* dxCommon_ = nullptr;
	DXCManager* dxc_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShader_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> compositeFinal_, compositeTemp_, fxaaFinal_, bloomDown_, bloomUp_, ssaoPass_, ssaoBlur_;
	std::map<std::wstring, Microsoft::WRL::ComPtr<ID3D12PipelineState>> effectPsos_;
	ScreenEffect effects_[kMaxScreenEffects];

	Target effectTargets_[2];
	Target fxaaSource_;
	Target bloom_[kBloomLevels];
	Target ssao_[2];
	float cameraInfo_[4] = { 1.0f, 1.0f, 0.1f, 1000.0f };
	bool cameraPerspective_ = true;
	float cameraHeight_[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
	D3D12_GPU_DESCRIPTOR_HANDLE occlusion_{};

	// Result of the last Prepare, consumed by Composite
	Constants composite_{};
	D3D12_GPU_DESCRIPTOR_HANDLE compositeSource_{};
	D3D12_GPU_DESCRIPTOR_HANDLE compositeBloom_{};
	D3D12_GPU_DESCRIPTOR_HANDLE compositeDepth_{};
	bool useFxaa_ = false;
};
