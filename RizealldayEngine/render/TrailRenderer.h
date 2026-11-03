#pragma once
#include "math/Vector2.h"
#include "math/Vector3.h"
#include "math/Vector4.h"
#include <d3d12.h>
#include <wrl.h>
#include <vector>

class DirectXCommon;
class DXCManager;
class Camera;

struct TrailVertex {
	Vector4 position;
	Vector4 color;
	Vector2 uv; // u: head 0 -> tail 1 (by age), v: one side 0 -> other side 1
};

struct TrailStyle {
	float lifetime = 0.5f;
	float minDistance = 0.05f; // a new point once the head has moved this far
	float widthStart = 0.3f;   // at the head
	float widthEnd = 0.0f;     // at the tail
	Vector4 colorStart{ 1.0f, 1.0f, 1.0f, 1.0f };
	Vector4 colorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };
	bool softEdges = true; // false with a texture: its alpha gives the shape across the ribbon
};

/// History of one moving point, and the ribbon that shows it. No GPU work: the renderer draws what BuildMesh returns.
class TrailRibbon {
public:
	static constexpr size_t kMaxPoints = 256;

	/// Call once per frame with game time. emitting = false: the head stops and the rest ages away.
	void Update(const Vector3& head, double now, const TrailStyle& style, bool emitting);
	/// Appends triangles facing cameraPosition; with softEdges the centre has full alpha and the edges 0
	void BuildMesh(const Vector3& cameraPosition, double now, const TrailStyle& style, std::vector<TrailVertex>& out) const;
	void Clear() { points_.clear(); wasEmitting_ = false; }
	size_t PointCount() const { return points_.size(); }

private:
	struct Point {
		Vector3 position;
		double born = 0.0;
		bool startsPiece = false; // not joined to the point before
	};
	std::vector<Point> points_; // oldest first; the last one follows the head while emitting
	bool wasEmitting_ = false;
};

/// Draws trail meshes into the HDR scene target: depth-tested, no depth write. Colors above 1 feed the bloom.
class TrailRenderer {
public:
	static TrailRenderer* GetInstance();
	void Initialize(DirectXCommon* dxCommon, DXCManager* dxc);
	void Finalize();
	/// textureHandle 0 = plain white
	void Draw(ID3D12GraphicsCommandList* cmdList, const Camera& camera, const std::vector<TrailVertex>& vertices, bool additive, uint32_t textureHandle = 0);

private:
	DirectXCommon* dxCommon_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> alphaPso_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> additivePso_;
};
