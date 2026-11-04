#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "math/VertexData.h"
#include "math/MathUtil.h"
#include "math/Frustum.h"

class DirectXCommon;
class TextureManager;

struct MeshData {
	std::vector<VertexData> vertices;
	std::vector<uint32_t> indices;
	uint32_t textureHandle = 0;
	MaterialMaps maps; // PBR maps found in the model file
	Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // material colour from the file (untextured meshes only), times Material::color
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
	D3D12_VERTEX_BUFFER_VIEW vbView{};
	D3D12_INDEX_BUFFER_VIEW ibView{};
};

struct BoneInfo {
	std::string name;
	uint32_t id = 0;
	Matrix4x4 offsetMatrix;
};

struct KeyVector3 {
	float time = 0.0f;
	Vector3 value = { 0.0f, 0.0f, 0.0f };
};

struct KeyQuat {
	float time = 0.0f;
	float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

struct NodeAnimTrack {
	std::vector<KeyVector3> positions;
	std::vector<KeyQuat> rotations;
	std::vector<KeyVector3> scalings;
};

struct AnimationClip {
	std::string name;
	float duration = 0.0f; // in ticks
	float ticksPerSecond = 25.0f;
	std::unordered_map<std::string, NodeAnimTrack> channels;
};

/// Local transform of one node in an animation pose (roadmap 5.1). `bind` = not animated: use NodeData::transform.
struct NodePose {
	Vector3 translation = { 0.0f, 0.0f, 0.0f };
	KeyQuat rotation{};
	Vector3 scaling = { 1.0f, 1.0f, 1.0f };
	bool bind = true;
};
/// One NodePose per node, in Model's flattened (pre-order) node order
using LocalPose = std::vector<NodePose>;

struct NodeData {
	std::string name;
	Matrix4x4 transform = Mathematics::MakeIdentity4x4(); // (C26495: was left uninitialised)
	Vector3 defaultPosition = { 0.0f, 0.0f, 0.0f };
	KeyQuat defaultRotation = { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
	Vector3 defaultScaling = { 1.0f, 1.0f, 1.0f };
	std::vector<NodeData> children;
};

class Model {
private:
	struct PrivateTag {};

public:
	explicit Model(PrivateTag) {}
	~Model();
	static void StaticInitialize(DirectXCommon* dxCommon);
	static Model* CreateFromFile(const std::string& directoryPath, const std::string& filename);
	static std::unique_ptr<Model> CreatePreviewFromFile(const std::string& filePath);
	static Model* CreateCube();
	static Model* CreateSphere();
	static Model* CreatePlane();
	static Model* CreateQuad();
	
	/// `material`: the bound one (root param 0). Given and the model has mesh colours, each mesh rebinds a copy tinted by MeshData::baseColor.
	void Draw(ID3D12GraphicsCommandList* cmdList, uint32_t overrideTextureHandle = 0, const MaterialMaps* maps = nullptr, UINT instanceCount = 1, const Material* material = nullptr);
	/// True when some mesh has a non-white MeshData::baseColor
	bool HasMeshColors() const { return hasMeshColors_; }
	// Vertices only, nothing bound: depth passes
	void DrawGeometry(ID3D12GraphicsCommandList* cmdList, UINT instanceCount = 1);
	const std::vector<MeshData>& GetMeshes() const { return meshes_; }
	uint32_t GetTotalVertexCount() const;
	uint32_t GetTotalTriangleCount() const;
	void GetBounds(Vector3& outCenter, float& outMaxExtent) const;
	void GetLocalAABB(Vector3& outMin, Vector3& outMax) const;
	AABB GetLocalAABB() const { return { localMin_, localMax_ }; }

	bool HasBones() const { return !boneInfoMap_.empty(); }
	size_t GetBoneCount() const { return boneInfoMap_.size(); }
	const std::vector<BoneInfo>& GetBoneList() const { return boneList_; }
	size_t GetAnimationCount() const { return animations_.size(); }
	float GetAnimationDuration(int animIndex = 0) const;
	const std::string& GetAnimationName(int animIndex = 0) const;

	const AnimationClip* GetAnimationClip(int animIndex) const;
	/// Index of a clip by name (-1 when none)
	int FindAnimationIndex(const std::string& name) const;

	/// Skin matrices of one clip at `timeInSeconds` (= SampleLocalPose + BuildSkinMatrices)
	void CalculateBoneTransforms(float timeInSeconds, int animIndex, std::vector<Matrix4x4>& outMatrices) const;

	// Pose pipeline (roadmap 5.1): sample local poses, blend them (crossfade), then build the skin matrices.
	// The clip may come from another model with the same bone names (e.g. Mixamo Idle.fbx + Walk.fbx).
	size_t GetNodeCount() const;
	void SampleLocalPose(const AnimationClip* clip, float timeInSeconds, bool loop, LocalPose& outPose) const;
	/// weight 0 = a, 1 = b. Translation / scale lerp, rotation slerp. a and b must come from this model.
	/// `mask` (one byte per node, from MakeNodeMask): nodes with 0 keep a
	void BlendPoses(const LocalPose& a, const LocalPose& b, float weight, LocalPose& outPose, const std::vector<uint8_t>* mask = nullptr) const;
	/// `overrides`: node index -> model-space matrix replacing what the pose says (ragdoll)
	void BuildSkinMatrices(const LocalPose& pose, std::vector<Matrix4x4>& outMatrices, const std::vector<std::pair<int, Matrix4x4>>* overrides = nullptr) const;
	int FindNode(const std::string& name) const; // -1 when none
	/// 1 for the named node and everything below it. Empty name or unknown bone: every node.
	std::vector<uint8_t> MakeNodeMask(const std::string& rootName) const;
	/// First bone of the skeleton in node order, -1 without bones
	int RootBoneNode() const;
	bool GetNodeModelMatrix(const LocalPose& pose, int node, Matrix4x4& out, const std::vector<std::pair<int, Matrix4x4>>* overrides = nullptr) const;
	/// Turns the two bones above `endNode` so the end reaches `target` (model space), bending towards `pole`
	bool SolveTwoBoneIK(LocalPose& pose, int endNode, const Vector3& target, const Vector3& pole, float weight) const;
	struct RagdollBone {
		int node = -1;
		Vector3 start{};     // model space
		Vector3 end{};
		int parentPart = -1; // index in the returned list
	};
	/// The long bones of a pose (at most 16), parents first: hips, spine, limbs. Short ones such as fingers follow their parent.
	std::vector<RagdollBone> BuildRagdollBones(const LocalPose& pose) const;
	/// Model-space joint positions and parent node indices (-1 = root) of a pose, for the skeleton overlay
	void GetJointPositions(const LocalPose& pose, std::vector<Vector3>& outPositions, std::vector<int>& outParents, std::vector<bool>& outIsBone) const;

	/// Previous single-pass implementation, kept as the reference for the regression test of the pose pipeline
	void CalculateBoneTransformsReference(float timeInSeconds, int animIndex, std::vector<Matrix4x4>& outMatrices) const;

	void SetLockRootMotion(bool lock) { lockRootMotion_ = lock; }
	bool GetLockRootMotion() const { return lockRootMotion_; }

private:
	Model() = default;
	void LoadAssimpModel(const std::string& filePath);
	void LoadOBJ(const std::string& directoryPath, const std::string& filename);
	void CreateGPUBuffers();
	void ComputeCachedLocalAABB();

	NodeData BuildNodeHierarchy(void* pAiNode);
	void ReadNodeHierarchy(float animTimeTicks, const AnimationClip* pAnim, const NodeData& node, const Matrix4x4& parentTransform, std::vector<Matrix4x4>& outMatrices) const;
	
	static Vector3 InterpolatePosition(float animTimeTicks, const NodeAnimTrack& track, const Vector3& defaultPos);
	static KeyQuat InterpolateRotation(float animTimeTicks, const NodeAnimTrack& track, const KeyQuat& defaultRot);
	static Vector3 InterpolateScaling(float animTimeTicks, const NodeAnimTrack& track, const Vector3& defaultScale);

	std::vector<MeshData> meshes_;
	std::unordered_map<std::string, BoneInfo> boneInfoMap_;
	std::vector<BoneInfo> boneList_;
	NodeData rootNode_;
	Matrix4x4 globalInverseTransform_ = Mathematics::MakeIdentity4x4();
	std::vector<AnimationClip> animations_;
	// Flattened node tree (pre-order: parents before children), built on first use
	struct FlatNode {
		const NodeData* node = nullptr;
		int parent = -1;
		int boneId = -1;
	};
	const std::vector<FlatNode>& FlatNodes() const;
	void ComputeGlobalTransforms(const LocalPose& pose, std::vector<Matrix4x4>& outGlobals, const std::vector<std::pair<int, Matrix4x4>>* overrides = nullptr) const;
	mutable std::vector<FlatNode> flatNodes_;
	bool lockRootMotion_ = false;
	bool hasMeshColors_ = false;
	Vector3 localMin_ = { -0.5f, -0.5f, -0.5f };
	Vector3 localMax_ = {  0.5f,  0.5f,  0.5f };
	bool hasValidBounds_ = false;
	
	static DirectXCommon* sDxCommon_;

	static std::unordered_map<std::string, std::unique_ptr<Model>> sCache_;
};
