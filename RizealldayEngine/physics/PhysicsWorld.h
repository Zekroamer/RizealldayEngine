#pragma once
#include "math/Quaternion.h"
#include "math/Vector3.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// Rigid body simulation behind a small interface. Knows object ids only, never GameObject; Jolt stays inside the .cpp.
namespace Physics {

enum class MotionType : uint8_t { Static, Kinematic, Dynamic };
enum class ShapeType : uint8_t { Box, Sphere, Capsule, Mesh };

struct TriangleMesh {
	std::vector<Vector3> vertices;
	std::vector<uint32_t> indices;
};

struct ShapeDesc {
	ShapeType type = ShapeType::Box;
	Vector3 center = { 0.0f, 0.0f, 0.0f };   // offset from the body position, in body space
	Vector3 halfExtents = { 0.5f, 0.5f, 0.5f }; // Box
	float radius = 0.5f;                     // Sphere, Capsule
	float halfHeight = 0.5f;                 // Capsule: half of the straight part, along Y
	std::shared_ptr<const TriangleMesh> mesh; // Mesh: vertices already scaled. Never dynamic (becomes kinematic).

	bool operator==(const ShapeDesc& o) const;
};

struct BodyDesc {
	uint32_t id = 0;
	MotionType motion = MotionType::Static;
	Vector3 position = { 0.0f, 0.0f, 0.0f };
	Quaternion rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
	Vector3 velocity = { 0.0f, 0.0f, 0.0f };
	std::vector<ShapeDesc> shapes; // empty: a body that only moves and touches nothing
	float mass = 1.0f;
	float friction = 0.5f;
	float restitution = 0.0f;
	float gravityFactor = 1.0f;
	float linearDamping = 0.0f;
	bool freezeRotation = false;
	bool isSensor = false;
	bool continuous = false;
	uint32_t layerBit = 1;
	uint32_t collisionMask = 0xFFFFFFFFu;
};

struct BodyState {
	Vector3 position = { 0.0f, 0.0f, 0.0f };
	Quaternion rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
	Vector3 velocity = { 0.0f, 0.0f, 0.0f };
	bool grounded = false;
};

struct Contact {
	uint32_t a = 0;
	uint32_t b = 0;
	bool sensor = false;
};

enum class JointType : uint8_t { Fixed, Point, Hinge, Distance };

struct JointDesc {
	uint32_t id = 0;     // one joint per owner body
	uint32_t other = 0;  // 0 = fixed to the world
	JointType type = JointType::Fixed;
	Vector3 anchor = { 0.0f, 0.0f, 0.0f }; // world space, read when the joint is created
	Vector3 axis = { 0.0f, 0.0f, 1.0f };   // Hinge, world space
	Vector3 otherAnchor = { 0.0f, 0.0f, 0.0f }; // Distance: second end, world space
	uint64_t configKey = 0; // the joint is rebuilt when this changes
};

struct RayHit {
	uint32_t id = 0;
	float distance = 0.0f;
	Vector3 point = { 0.0f, 0.0f, 0.0f };
	Vector3 normal = { 0.0f, 1.0f, 0.0f };
};

using QueryFilter = std::function<bool(uint32_t id)>;

class PhysicsWorld {
public:
	PhysicsWorld();
	~PhysicsWorld();
	PhysicsWorld(const PhysicsWorld&) = delete;
	PhysicsWorld& operator=(const PhysicsWorld&) = delete;

	// Call for every body before each Step. A changed pose or velocity counts as a move by the caller:
	// kinematic bodies sweep to it (and push what they hit), the others teleport.
	void Submit(const BodyDesc& desc);
	// Updates poses for queries between steps. The next Submit still sweeps kinematic bodies from their pose at the last Step.
	void SubmitForQuery(const BodyDesc& desc);
	// Call for every joint after its bodies were submitted, before each Step
	void SubmitJoint(const JointDesc& desc);
	// Removes the bodies and joints not submitted since the last Step, then simulates.
	void Step(float deltaTime);
	// Closest hit whose id passes the filter. A ray starting inside a convex shape ignores that shape.
	bool Raycast(const Vector3& origin, const Vector3& direction, float maxDistance, const QueryFilter& accept, RayHit& out) const;
	// Ids of the bodies touching the sphere that pass the filter, each once
	void OverlapSphere(const Vector3& center, float radius, const QueryFilter& accept, std::vector<uint32_t>& out) const;
	// Pose after the last Step. False when the id has no body.
	bool GetState(uint32_t id, BodyState& out) const;
	// Touching and overlapping pairs after the last Step (once per object pair), including sleeping ones.
	const std::vector<Contact>& Contacts() const { return contacts_; }
	void Clear();
	size_t BodyCount() const;

private:
	struct Impl;
	void SubmitImpl(const BodyDesc& desc, bool forQuery);
	std::unique_ptr<Impl> impl_;
	std::vector<Contact> contacts_;
};

} // namespace Physics
