#pragma once
#include <d3d12.h>
#include <wrl.h>
#include "math/Vector3.h"
#include "math/Matrix4x4.h"
#include "math/Quaternion.h"
#include "math/MathUtil.h"

class DirectXCommon;

/// World transform of one draw. The matrices live on the CPU; Upload() copies them into this frame's
/// upload memory at bind time, so several cameras/passes can draw the same object in one frame and
/// the CPU can record the next frame while the GPU still reads the previous one.
class WorldTransform {
public:
	WorldTransform() = default;
	~WorldTransform() = default;

	static void StaticInitialize(DirectXCommon* dxCommon);

	void Initialize();
	void UpdateMatrix(const Matrix4x4& viewProjectionMatrix);

	/// Uploads the current matrices for this frame and returns the GPU address to bind (0 on failure)
	D3D12_GPU_VIRTUAL_ADDRESS Upload() const;

	Vector3 scale = {1.0f, 1.0f, 1.0f};
	Vector3 rotation = {0.0f, 0.0f, 0.0f};
	Quaternion rotationQuat = Quaternion::Identity();
	Vector3 translation = {0.0f, 0.0f, 0.0f};

	Matrix4x4 matWorld = {};
	const TransformationMatrix& GetMatrices() const { return data_; }

private:
	static DirectXCommon* sDxCommon_;
	TransformationMatrix data_{};
};
