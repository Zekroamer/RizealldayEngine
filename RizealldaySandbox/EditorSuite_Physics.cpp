#include "EditorSuiteInternal.h"
#include "PhysicsComponents.h"

namespace {
	constexpr const char* kCapsuleColliderComponent = "Capsule Collider";
	constexpr const char* kMeshColliderComponent = "Mesh Collider";
	constexpr const char* kJointComponent = "Joint";
}

// World-space AABB of an object's box collider. Roots keep the original math (translation + center);
// children transform the center through the world matrix and use the world scale.
void EditorSuite::ComputeBoxColliderBounds(const GameObject& o, bool useAbsSize, Vector3& outMin, Vector3& outMax) const
{
	Vector3 wPos{};
	Quaternion wRot = Quaternion::Identity();
	Vector3 wScale{};
	GetWorldTRS(o, wPos, wRot, wScale);
	const Vector3 center = (o.parentId == -1)
		? Vector3{ wPos.x + o.boxCenter.x, wPos.y + o.boxCenter.y, wPos.z + o.boxCenter.z }
		: TransformUtil::TransformPoint(o.boxCenter, o.worldMatrix);
	Vector3 half = { o.boxSize.x * wScale.x * 0.5f, o.boxSize.y * wScale.y * 0.5f, o.boxSize.z * wScale.z * 0.5f };
	if (useAbsSize) {
		half = { std::abs(half.x), std::abs(half.y), std::abs(half.z) };
	}
	outMin = { center.x - half.x, center.y - half.y, center.z - half.z };
	outMax = { center.x + half.x, center.y + half.y, center.z + half.z };
}

void EditorSuite::ComputeSphereColliderWorld(const GameObject& o, Vector3& outCenter, float& outRadius) const
{
	Vector3 wPos{};
	Quaternion wRot = Quaternion::Identity();
	Vector3 wScale{};
	GetWorldTRS(o, wPos, wRot, wScale);
	const float maxScale = (std::max)({ std::abs(wScale.x), std::abs(wScale.y), std::abs(wScale.z) });
	outCenter = (o.parentId == -1)
		? Vector3{ wPos.x + o.sphereCenter.x, wPos.y + o.sphereCenter.y, wPos.z + o.sphereCenter.z }
		: TransformUtil::TransformPoint(o.sphereCenter, o.worldMatrix);
	outRadius = o.sphereRadius * maxScale;
}

bool EditorSuite::BuildPhysicsBody(const GameObject& obj, Physics::BodyDesc& desc)
{
	const bool box = obj.hasBoxCollider && obj.boxColliderEnabled;
	const bool sphere = obj.hasSphereCollider && obj.sphereColliderEnabled;
	const CapsuleColliderComponent* capsule = nullptr;
	bool meshCollider = false;
	if (!obj.components.Empty()) {
		auto& store = const_cast<ComponentStore&>(obj.components); // Get has no const overload
		capsule = store.Get<CapsuleColliderComponent>(kCapsuleColliderComponent);
		if (capsule && !capsule->enabled) capsule = nullptr;
		const MeshColliderComponent* mesh = store.Get<MeshColliderComponent>(kMeshColliderComponent);
		meshCollider = mesh && mesh->enabled;
	}
	const bool rigidbody = obj.hasRigidbody && obj.rbEnabled;
	const bool moves = rigidbody && !obj.isKinematic && !obj.isStatic;
	if (!box && !sphere && !capsule && !meshCollider && !moves) return false;

	// Rigidbody simulation is root-only: a child is treated as kinematic (its collider stays active)
	if (moves && obj.parentId != -1 && childRigidbodyWarned_.insert(obj.id).second) {
		LogMessage(std::format("[Physics] '{}' has a Rigidbody but is a child object; it is treated as kinematic.", obj.name), 1);
	}

	Vector3 worldScale{};
	desc.id = obj.id;
	GetWorldTRS(obj, desc.position, desc.rotation, worldScale);
	desc.motion = obj.isStatic ? Physics::MotionType::Static
		: (moves && obj.parentId == -1) ? Physics::MotionType::Dynamic : Physics::MotionType::Kinematic;
	desc.velocity = obj.velocity;
	desc.mass = obj.mass;
	desc.friction = rigidbody ? obj.friction : 0.5f;
	desc.restitution = rigidbody ? obj.bounciness : 0.0f;
	desc.gravityFactor = obj.useGravity ? obj.gravityScale : 0.0f;
	desc.linearDamping = obj.drag;
	desc.freezeRotation = obj.freezeRotation;
	desc.continuous = obj.continuousCollision;
	desc.isSensor = obj.isTrigger;
	desc.layerBit = 1u << (obj.layer & 31u);
	desc.collisionMask = obj.collisionMask;

	// Offsets of roots are world units, offsets of children scale with the object (see ComputeBoxColliderBounds)
	const auto offset = [&](const Vector3& c) {
		return obj.parentId == -1 ? c : Vector3{ c.x * worldScale.x, c.y * worldScale.y, c.z * worldScale.z };
	};
	desc.shapes.clear();
	if (box) {
		Physics::ShapeDesc shape;
		shape.type = Physics::ShapeType::Box;
		shape.center = offset(obj.boxCenter);
		shape.halfExtents = { std::abs(obj.boxSize.x * worldScale.x) * 0.5f, std::abs(obj.boxSize.y * worldScale.y) * 0.5f, std::abs(obj.boxSize.z * worldScale.z) * 0.5f };
		desc.shapes.push_back(shape);
	}
	if (sphere) {
		Physics::ShapeDesc shape;
		shape.type = Physics::ShapeType::Sphere;
		shape.center = offset(obj.sphereCenter);
		shape.radius = obj.sphereRadius * (std::max)({ std::abs(worldScale.x), std::abs(worldScale.y), std::abs(worldScale.z) });
		desc.shapes.push_back(shape);
	}
	if (capsule) {
		const float radiusScale = (std::max)(std::abs(worldScale.x), std::abs(worldScale.z));
		Physics::ShapeDesc shape;
		shape.type = Physics::ShapeType::Capsule;
		shape.center = offset(capsule->center);
		shape.radius = capsule->radius * radiusScale;
		shape.halfHeight = (std::max)(capsule->height * std::abs(worldScale.y) * 0.5f - shape.radius, 0.0f);
		desc.shapes.push_back(shape);
	}
	if (meshCollider) {
		if (auto mesh = LoadCollisionMesh(obj, worldScale)) {
			Physics::ShapeDesc shape;
			shape.type = Physics::ShapeType::Mesh;
			shape.mesh = std::move(mesh);
			desc.shapes.push_back(std::move(shape));
		}
	}
	return !desc.shapes.empty() || moves;
}

std::shared_ptr<const Physics::TriangleMesh> EditorSuite::LoadCollisionMesh(const GameObject& obj, const Vector3& scale)
{
	if (obj.meshType != GameObject::MeshType::CustomModel || obj.modelPath.empty()) return nullptr;
	const std::string key = std::format("{}|{}|{}|{}", obj.modelPath, scale.x, scale.y, scale.z);
	if (const auto found = collisionMeshes_.find(key); found != collisionMeshes_.end()) return found->second;

	std::shared_ptr<Physics::TriangleMesh> result; // a failed load is remembered too: this runs every step
	const std::filesystem::path path(ResolveAssetPath(obj.modelPath));
	std::error_code ec;
	if (const Model* model = std::filesystem::exists(path, ec) ? Model::CreateFromFile(path.parent_path().string(), path.filename().string()) : nullptr) {
		result = std::make_shared<Physics::TriangleMesh>();
		for (const MeshData& mesh : model->GetMeshes()) {
			const uint32_t base = static_cast<uint32_t>(result->vertices.size());
			for (const VertexData& v : mesh.vertices) {
				result->vertices.push_back({ v.position.x * scale.x, v.position.y * scale.y, v.position.z * scale.z });
			}
			for (uint32_t index : mesh.indices) result->indices.push_back(base + index);
		}
	}
	collisionMeshes_[key] = result;
	return result;
}

void EditorSuite::SubmitPhysicsJoints()
{
	for (const auto& obj : gameObjects_) {
		if (!obj.isActive || obj.components.Empty()) continue;
		auto& store = const_cast<ComponentStore&>(obj.components);
		const JointComponent* joint = store.Get<JointComponent>(kJointComponent);
		if (!joint || !joint->enabled) continue;
		const uint32_t otherId = static_cast<uint32_t>((std::max)(joint->connectedObjectId, 0));
		const GameObject* other = otherId != 0 ? FindGameObjectById(otherId, false) : nullptr;
		if (otherId != 0 && (!other || !other->isActive)) continue;

		Physics::JointDesc desc;
		desc.id = obj.id;
		desc.other = otherId;
		desc.type = static_cast<Physics::JointType>(std::clamp(joint->jointType, 0, 3));
		desc.anchor = TransformUtil::TransformPoint(joint->anchor, obj.worldMatrix);
		desc.axis = joint->axis;
		desc.otherAnchor = other ? GetWorldPositionOf(*other) : desc.anchor;
		// Only what the user set, not the world pose: the joint must not rebuild while its bodies move
		const std::hash<float> hash;
		desc.configKey = (static_cast<uint64_t>(desc.type) + 1) * 0x9E3779B97F4A7C15ull
			^ hash(joint->anchor.x) ^ (hash(joint->anchor.y) << 1) ^ (hash(joint->anchor.z) << 2)
			^ (hash(joint->axis.x) << 3) ^ (hash(joint->axis.y) << 4) ^ (hash(joint->axis.z) << 5);
		physicsWorld_->SubmitJoint(desc);
	}
}

void EditorSuite::RegisterPhysicsComponents()
{
	if (ComponentRegistry::Get().Find(kCapsuleColliderComponent)) return;
	RegisterDataComponent<CapsuleColliderComponent>(kCapsuleColliderComponent, "Physics", {
		RIZE_FIELD(CapsuleColliderComponent, enabled, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(CapsuleColliderComponent, center, ComponentFieldType::Vector3, 0.0f, 0.0f),
		RIZE_FIELD(CapsuleColliderComponent, radius, ComponentFieldType::Float, 0.01f, 100.0f),
		RIZE_FIELD(CapsuleColliderComponent, height, ComponentFieldType::Float, 0.01f, 100.0f) });
	RegisterDataComponent<MeshColliderComponent>(kMeshColliderComponent, "Physics", {
		RIZE_FIELD(MeshColliderComponent, enabled, ComponentFieldType::Bool, 0.0f, 0.0f) });
	RegisterDataComponent<JointComponent>(kJointComponent, "Physics", {
		RIZE_FIELD(JointComponent, enabled, ComponentFieldType::Bool, 0.0f, 0.0f),
		ComponentField{ "jointType", ComponentFieldType::Int, offsetof(JointComponent, jointType), sizeof(int), 0.0f, 3.0f, "0 Fixed, 1 Point, 2 Hinge, 3 Distance" },
		ComponentField{ "connectedObjectId", ComponentFieldType::Int, offsetof(JointComponent, connectedObjectId), sizeof(int), 0.0f, 0.0f, "Id of the other object. 0 = the world." },
		RIZE_FIELD(JointComponent, anchor, ComponentFieldType::Vector3, 0.0f, 0.0f),
		RIZE_FIELD(JointComponent, axis, ComponentFieldType::Vector3, 0.0f, 0.0f) });
}

void EditorSuite::SyncPhysicsForQueries()
{
	if (!physicsWorld_) physicsWorld_ = std::make_unique<Physics::PhysicsWorld>();
	// shortcut: synced once per frame, so a move made earlier in the same frame is not seen by queries until the next one; sync per query if a game needs it
	if (!physicsQueryDirty_ && !isRunningTests_) return;
	physicsQueryDirty_ = false;
	Physics::BodyDesc desc;
	for (const auto& obj : gameObjects_) {
		if (obj.isActive && BuildPhysicsBody(obj, desc)) physicsWorld_->SubmitForQuery(desc);
	}
}

void EditorSuite::RestoreSimulatedPoses()
{
	if (!renderPosesApplied_) return;
	renderPosesApplied_ = false;
	for (const auto& [id, pose] : renderPoses_) {
		GameObject* obj = FindGameObjectById(id, false);
		if (!obj) continue;
		obj->translation = pose.simPosition;
		obj->rotationQuat = pose.simRotation;
		obj->rotation = pose.simRotation.ToEuler();
	}
}

void EditorSuite::ApplyRenderPoses(float alpha)
{
	for (auto it = renderPoses_.begin(); it != renderPoses_.end();) {
		GameObject* obj = FindGameObjectById(it->first, false);
		if (!obj || !obj->isActive) {
			it = renderPoses_.erase(it);
			continue;
		}
		RenderPose& pose = it->second;
		pose.simPosition = obj->translation;
		pose.simRotation = obj->rotationQuat;
		obj->translation = pose.previousPosition + (pose.simPosition - pose.previousPosition) * alpha;
		obj->rotationQuat = Quaternion::Slerp(pose.previousRotation, pose.simRotation, alpha);
		obj->rotation = obj->rotationQuat.ToEuler();
		++it;
	}
	renderPosesApplied_ = true;
	UpdateWorldMatrices();
}

void EditorSuite::UpdatePhysics(float deltaTime)
{
	// Scripts may have moved objects earlier this frame
	UpdateWorldMatrices();

	if (!physicsWorld_) physicsWorld_ = std::make_unique<Physics::PhysicsWorld>();

	Physics::BodyDesc desc;
	for (auto& obj : gameObjects_) {
		obj.isColliding = false;
		if (obj.isActive && !ragdolls_.contains(obj.id) && BuildPhysicsBody(obj, desc)) physicsWorld_->Submit(desc); // a ragdoll replaces the body
	}
	SubmitPhysicsJoints();

	physicsWorld_->Step(deltaTime);
	if (!ragdolls_.empty()) UpdateRagdolls();

	for (auto& obj : gameObjects_) {
		if (!obj.isActive || !obj.hasRigidbody || !obj.rbEnabled || obj.isKinematic || obj.isStatic || obj.parentId != -1) continue;
		Physics::BodyState state;
		if (!physicsWorld_->GetState(obj.id, state)) continue;
		obj.translation = state.position;
		obj.velocity = state.velocity;
		obj.isGrounded = state.grounded;
		if (!obj.freezeRotation) {
			obj.rotationQuat = state.rotation;
			obj.rotation = state.rotation.ToEuler();
		}
	}

	const bool reportEvents = sceneState_ != EditorSceneState::Edit || IsStandaloneMode();
	for (const Physics::Contact& contact : physicsWorld_->Contacts()) {
		if (GameObject* a = FindGameObjectById(contact.a, false)) a->isColliding = true;
		if (GameObject* b = FindGameObjectById(contact.b, false)) b->isColliding = true;
		if (reportEvents) {
			RecordCollisionContact(contact.a, contact.b);
			RecordCollisionContact(contact.b, contact.a);
		}
	}
	// Script callbacks run after the step, once per object pair
	if (reportEvents) DispatchCollisionEvents();

	// The simulation moved root objects; keep world matrices current for whoever runs next
	UpdateWorldMatrices();
}


int EditorSuite::StepPhysics(float deltaTime)
{
	RestoreSimulatedPoses();
	if (std::isfinite(deltaTime) && deltaTime > 0.0f) physicsAccumulator_ += deltaTime;
	int steps = 0;
	while (physicsAccumulator_ >= kFixedPhysicsStep && steps < kMaxPhysicsSteps) {
		// Rendering shows the leftover time as a blend between the last two steps
		for (const auto& obj : gameObjects_) {
			if (!obj.isActive || !obj.hasRigidbody || !obj.rbEnabled || obj.isKinematic || obj.isStatic || obj.parentId != -1) continue;
			RenderPose& pose = renderPoses_[obj.id];
			pose.previousPosition = obj.translation;
			pose.previousRotation = obj.rotationQuat;
		}
		UpdatePhysics(static_cast<float>(kFixedPhysicsStep));
		physicsAccumulator_ -= kFixedPhysicsStep;
		++steps;
	}
	if (steps == kMaxPhysicsSteps) physicsAccumulator_ = 0.0; // too slow to catch up: drop the rest
	if (!renderPoses_.empty()) ApplyRenderPoses(static_cast<float>(physicsAccumulator_ / kFixedPhysicsStep));
	return steps;
}

bool EditorSuite::FitBoxColliderToMesh(GameObject& obj)
{
	if (obj.meshType != GameObject::MeshType::CustomModel || obj.modelPath.empty()) return false;
	const std::filesystem::path path(ResolveAssetPath(obj.modelPath));
	std::error_code ec;
	if (!std::filesystem::exists(path, ec)) return false;
	const Model* model = Model::CreateFromFile(path.parent_path().string(), path.filename().string());
	if (!model) return false;

	// Bind-pose vertices (Model::GetLocalAABB is padded for animation culling, too big for a collider)
	Vector3 mn{ FLT_MAX, FLT_MAX, FLT_MAX }, mx{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
	for (const MeshData& mesh : model->GetMeshes()) {
		for (const VertexData& v : mesh.vertices) {
			mn = { (std::min)(mn.x, v.position.x), (std::min)(mn.y, v.position.y), (std::min)(mn.z, v.position.z) };
			mx = { (std::max)(mx.x, v.position.x), (std::max)(mx.y, v.position.y), (std::max)(mx.z, v.position.z) };
		}
	}
	if (mn.x > mx.x) return false;
	const Vector3 center = (mn + mx) * 0.5f;
	obj.boxSize = mx - mn; // multiplied by the object scale like any box collider
	// Roots offset the box in world units (translation + boxCenter), children through their world matrix (scaled)
	obj.boxCenter = (obj.parentId == -1) ? Vector3{ center.x * obj.scale.x, center.y * obj.scale.y, center.z * obj.scale.z } : center;
	return true;
}
