#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include "physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <utility>

namespace Physics {

namespace {

constexpr JPH::ObjectLayer kLayerNonMoving = 0;
constexpr JPH::ObjectLayer kLayerMoving = 1;
constexpr float kMinShapeSize = 0.001f;
constexpr float kMaxSweepPerStep = 10.0f; // a longer kinematic move is a teleport (e.g. pooled object reuse), not a push

JPH::Vec3 ToJolt(const Vector3& v) { return JPH::Vec3(v.x, v.y, v.z); }
Vector3 FromJolt(JPH::Vec3Arg v) { return { v.GetX(), v.GetY(), v.GetZ() }; }

JPH::Quat ToJolt(const Quaternion& q)
{
	const JPH::Quat r(q.x, q.y, q.z, q.w);
	return r.LengthSq() > 1.0e-8f ? r.Normalized() : JPH::Quat::sIdentity();
}

bool SameVector(const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool SameRotation(const Quaternion& a, const Quaternion& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
	JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
	JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer)); }
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
	bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override {
		return layer == kLayerMoving || broadPhase == JPH::BroadPhaseLayer(kLayerMoving);
	}
};

class ObjectVsObject final : public JPH::ObjectLayerPairFilter {
public:
	bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a == kLayerMoving || b == kLayerMoving; }
};

// Group id = the body's layer bit, sub group id = its collision mask. Both bodies must accept each other.
class LayerMaskFilter final : public JPH::GroupFilter {
public:
	bool CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const override {
		return (a.GetGroupID() & b.GetSubGroupID()) != 0 && (b.GetGroupID() & a.GetSubGroupID()) != 0;
	}
};

struct PairHash {
	size_t operator()(const JPH::SubShapeIDPair& p) const { return static_cast<size_t>(p.GetHash()); }
};

struct LiveContact {
	uint32_t a = 0;
	uint32_t b = 0;
	bool sensor = false;
	Vector3 normal = { 0.0f, 1.0f, 0.0f }; // pushes b away from a
	uint64_t lastSeenStep = 0;
};

// Callbacks run on physics worker threads. OnContactRemoved is not used: Jolt also fires it when a pair falls asleep,
// and a resting box must stay grounded and a trigger must not report a false exit. See EndStep.
class ContactStore final : public JPH::ContactListener {
public:
	void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m, JPH::ContactSettings&) override { Store(a, b, m); }
	void OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m, JPH::ContactSettings&) override { Store(a, b, m); }

	void BeginStep() { ++step_; }
	// A pair not reported this step ended, unless both bodies are asleep (active bodies always report their contacts)
	void EndStep(const JPH::BodyInterface& bodies) {
		std::erase_if(live_, [&](const auto& entry) {
			return entry.second.lastSeenStep != step_ &&
				(bodies.IsActive(entry.first.GetBody1ID()) || bodies.IsActive(entry.first.GetBody2ID()));
		});
	}

	void RemoveBody(JPH::BodyID body) {
		std::erase_if(live_, [body](const auto& entry) { return entry.first.GetBody1ID() == body || entry.first.GetBody2ID() == body; });
	}
	void Clear() { live_.clear(); }
	const std::unordered_map<JPH::SubShapeIDPair, LiveContact, PairHash>& Live() const { return live_; }

private:
	void Store(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m) {
		const JPH::SubShapeIDPair key(a.GetID(), m.mSubShapeID1, b.GetID(), m.mSubShapeID2);
		LiveContact contact;
		contact.a = static_cast<uint32_t>(a.GetUserData());
		contact.b = static_cast<uint32_t>(b.GetUserData());
		contact.sensor = a.IsSensor() || b.IsSensor();
		contact.normal = FromJolt(m.mWorldSpaceNormal);
		contact.lastSeenStep = step_;
		std::lock_guard lock(mutex_);
		live_[key] = contact;
	}

	uint64_t step_ = 0; // written only between Jolt updates
	std::mutex mutex_;
	std::unordered_map<JPH::SubShapeIDPair, LiveContact, PairHash> live_;
};

JPH::RefConst<JPH::Shape> BuildSingleShape(const ShapeDesc& d)
{
	JPH::RefConst<JPH::Shape> shape;
	switch (d.type) {
	case ShapeType::Box: {
		const JPH::Vec3 half(
			(std::max)(std::abs(d.halfExtents.x), kMinShapeSize),
			(std::max)(std::abs(d.halfExtents.y), kMinShapeSize),
			(std::max)(std::abs(d.halfExtents.z), kMinShapeSize));
		shape = new JPH::BoxShape(half, (std::min)(JPH::cDefaultConvexRadius, half.ReduceMin()));
		break;
	}
	case ShapeType::Sphere:
		shape = new JPH::SphereShape((std::max)(std::abs(d.radius), kMinShapeSize));
		break;
	case ShapeType::Capsule:
		shape = new JPH::CapsuleShape((std::max)(d.halfHeight, kMinShapeSize), (std::max)(std::abs(d.radius), kMinShapeSize));
		break;
	case ShapeType::Mesh: {
		if (!d.mesh || d.mesh->indices.size() < 3) return nullptr;
		JPH::VertexList vertices;
		vertices.reserve(d.mesh->vertices.size());
		for (const Vector3& v : d.mesh->vertices) vertices.push_back(JPH::Float3(v.x, v.y, v.z));
		JPH::IndexedTriangleList triangles;
		triangles.reserve(d.mesh->indices.size() / 3);
		for (size_t i = 0; i + 2 < d.mesh->indices.size(); i += 3) {
			triangles.push_back(JPH::IndexedTriangle(d.mesh->indices[i], d.mesh->indices[i + 1], d.mesh->indices[i + 2], 0));
		}
		auto result = JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)).Create();
		if (result.HasError()) return nullptr;
		shape = result.Get();
		break;
	}
	}
	if (d.center.x != 0.0f || d.center.y != 0.0f || d.center.z != 0.0f) {
		shape = new JPH::RotatedTranslatedShape(ToJolt(d.center), JPH::Quat::sIdentity(), shape);
	}
	return shape;
}

JPH::RefConst<JPH::Shape> BuildShape(const std::vector<ShapeDesc>& shapes)
{
	if (shapes.empty()) return new JPH::SphereShape(0.05f); // body without collider; its group mask is 0
	if (shapes.size() == 1) return BuildSingleShape(shapes[0]);
	JPH::StaticCompoundShapeSettings compound;
	for (const ShapeDesc& d : shapes) {
		JPH::RefConst<JPH::Shape> part = BuildSingleShape(d);
		if (part) compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), part);
	}
	auto result = compound.Create();
	return result.HasError() ? nullptr : result.Get();
}

bool SameConfig(const BodyDesc& a, const BodyDesc& b)
{
	return a.motion == b.motion && a.mass == b.mass && a.friction == b.friction && a.restitution == b.restitution &&
		a.gravityFactor == b.gravityFactor && a.linearDamping == b.linearDamping && a.freezeRotation == b.freezeRotation &&
		a.isSensor == b.isSensor && a.continuous == b.continuous && a.layerBit == b.layerBit && a.collisionMask == b.collisionMask &&
		a.shapes.empty() == b.shapes.empty();
}

void EnsureJoltRegistered()
{
	static std::once_flag once;
	std::call_once(once, [] {
		JPH::RegisterDefaultAllocator();
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();
	});
}

} // namespace

bool ShapeDesc::operator==(const ShapeDesc& o) const
{
	return type == o.type && SameVector(center, o.center) && SameVector(halfExtents, o.halfExtents) &&
		radius == o.radius && halfHeight == o.halfHeight && mesh == o.mesh;
}

struct PhysicsWorld::Impl {
	struct Record {
		JPH::BodyID body;
		BodyDesc desc;         // as last submitted; pose and velocity updated from the simulation after each Step
		Vector3 stepPosition = { 0.0f, 0.0f, 0.0f }; // pose at the last Step: where a kinematic sweep starts
		Quaternion stepRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
		bool seen = true;
		bool sweeping = false; // kinematic moved last step, so its velocity must be cleared
		bool grounded = false;
	};

	struct JointRecord {
		JPH::Ref<JPH::Constraint> constraint;
		uint32_t other = 0;
		uint64_t configKey = 0;
		bool seen = true;
	};

	BroadPhaseLayers broadPhaseLayers;
	ObjectVsBroadPhase objectVsBroadPhase;
	ObjectVsObject objectVsObject;
	ContactStore contactStore;
	std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
	std::unique_ptr<JPH::JobSystemThreadPool> jobs;
	std::unique_ptr<JPH::PhysicsSystem> system;
	JPH::Ref<LayerMaskFilter> layerFilter;
	std::unordered_map<uint32_t, Record> records;
	std::unordered_map<uint32_t, JointRecord> joints;
	int addedSinceOptimize = 0;
	float lastDeltaTime = 1.0f / 60.0f;

	Impl() {
		EnsureJoltRegistered(); // before anything allocates through Jolt
		layerFilter = new LayerMaskFilter();
		tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(32 * 1024 * 1024);
		const int workers = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 4);
		jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workers);
		system = std::make_unique<JPH::PhysicsSystem>();
		system->Init(65536, 0, 65536, 20480, broadPhaseLayers, objectVsBroadPhase, objectVsObject);
		system->SetContactListener(&contactStore);
		system->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
	}

	JPH::BodyInterface& Bodies() const { return system->GetBodyInterfaceNoLock(); }

	static MotionType EffectiveMotion(const BodyDesc& d) {
		if (d.motion != MotionType::Dynamic) return d.motion;
		for (const ShapeDesc& s : d.shapes) {
			if (s.type == ShapeType::Mesh) return MotionType::Kinematic; // Jolt cannot simulate a dynamic triangle mesh
		}
		return MotionType::Dynamic;
	}

	bool Create(const BodyDesc& d, Record& record) {
		JPH::RefConst<JPH::Shape> shape = BuildShape(d.shapes);
		if (!shape) return false;
		const MotionType motion = EffectiveMotion(d);
		const JPH::EMotionType joltMotion = motion == MotionType::Dynamic ? JPH::EMotionType::Dynamic
			: motion == MotionType::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static;
		JPH::BodyCreationSettings settings(shape, ToJolt(d.position), ToJolt(d.rotation), joltMotion,
			motion == MotionType::Static ? kLayerNonMoving : kLayerMoving);
		settings.mUserData = d.id;
		settings.mFriction = d.friction;
		settings.mRestitution = d.restitution;
		settings.mGravityFactor = d.gravityFactor;
		settings.mLinearDamping = d.linearDamping;
		settings.mIsSensor = d.isSensor;
		settings.mCollideKinematicVsNonDynamic = motion == MotionType::Kinematic; // collider-only objects moved by scripts still report contacts
		settings.mMotionQuality = d.continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
		if (d.freezeRotation) settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
		settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
		settings.mMassPropertiesOverride.mMass = (std::max)(d.mass, 0.0001f);
		settings.mCollisionGroup = JPH::CollisionGroup(layerFilter, d.layerBit, d.shapes.empty() ? 0u : d.collisionMask);
		if (motion == MotionType::Dynamic) settings.mLinearVelocity = ToJolt(d.velocity);

		JPH::Body* body = Bodies().CreateBody(settings);
		if (!body) return false; // body limit reached
		record.body = body->GetID();
		record.stepPosition = d.position;
		record.stepRotation = d.rotation;
		Bodies().AddBody(record.body, JPH::EActivation::Activate);
		++addedSinceOptimize;
		return true;
	}

	void RemoveJoint(std::unordered_map<uint32_t, JointRecord>::iterator it) {
		system->RemoveConstraint(it->second.constraint);
		joints.erase(it);
	}

	void Destroy(uint32_t id, const Record& record) {
		// A constraint must never outlive its bodies; the next SubmitJoint rebuilds it if the body came back
		for (auto it = joints.begin(); it != joints.end();) {
			auto next = std::next(it);
			if (it->first == id || it->second.other == id) RemoveJoint(it);
			it = next;
		}
		contactStore.RemoveBody(record.body);
		Bodies().RemoveBody(record.body);
		Bodies().DestroyBody(record.body);
	}

	void ApplyMove(const BodyDesc& d, Record& record, bool forQuery) {
		const bool poseChanged = !SameVector(d.position, record.desc.position) || !SameRotation(d.rotation, record.desc.rotation);
		const MotionType motion = EffectiveMotion(d);
		if (motion == MotionType::Kinematic && !forQuery) {
			const bool moved = !SameVector(d.position, record.stepPosition) || !SameRotation(d.rotation, record.stepRotation);
			const float dx = d.position.x - record.stepPosition.x;
			const float dy = d.position.y - record.stepPosition.y;
			const float dz = d.position.z - record.stepPosition.z;
			if (moved && dx * dx + dy * dy + dz * dz <= kMaxSweepPerStep * kMaxSweepPerStep) {
				const bool movedForQuery = !SameVector(record.desc.position, record.stepPosition) || !SameRotation(record.desc.rotation, record.stepRotation);
				if (movedForQuery) Bodies().SetPositionAndRotation(record.body, ToJolt(record.stepPosition), ToJolt(record.stepRotation), JPH::EActivation::Activate);
				Bodies().MoveKinematic(record.body, ToJolt(d.position), ToJolt(d.rotation), lastDeltaTime);
				record.sweeping = true;
			} else {
				if (poseChanged) Bodies().SetPositionAndRotation(record.body, ToJolt(d.position), ToJolt(d.rotation), JPH::EActivation::Activate);
				if (record.sweeping) Bodies().SetLinearAndAngularVelocity(record.body, JPH::Vec3::sZero(), JPH::Vec3::sZero());
				record.sweeping = false;
			}
			return;
		}
		if (poseChanged) Bodies().SetPositionAndRotation(record.body, ToJolt(d.position), ToJolt(d.rotation), JPH::EActivation::Activate);
		if (motion == MotionType::Dynamic && !SameVector(d.velocity, record.desc.velocity)) {
			Bodies().SetLinearVelocity(record.body, ToJolt(d.velocity)); // also wakes the body
		}
	}
};

PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {}

PhysicsWorld::~PhysicsWorld()
{
	Clear();
}

void PhysicsWorld::Clear()
{
	for (const auto& [id, record] : impl_->records) impl_->Destroy(id, record);
	impl_->records.clear();
	impl_->contactStore.Clear();
	contacts_.clear();
}

size_t PhysicsWorld::BodyCount() const
{
	return impl_->records.size();
}

void PhysicsWorld::Submit(const BodyDesc& desc)
{
	SubmitImpl(desc, false);
}

void PhysicsWorld::SubmitForQuery(const BodyDesc& desc)
{
	SubmitImpl(desc, true);
}

void PhysicsWorld::SubmitJoint(const JointDesc& desc)
{
	auto existing = impl_->joints.find(desc.id);
	if (existing != impl_->joints.end()) {
		if (existing->second.configKey == desc.configKey && existing->second.other == desc.other) {
			existing->second.seen = true;
			return;
		}
		impl_->RemoveJoint(existing);
	}

	const auto own = impl_->records.find(desc.id);
	const auto other = impl_->records.find(desc.other);
	if (own == impl_->records.end() || (desc.other != 0 && other == impl_->records.end())) return;
	const JPH::BodyLockInterfaceNoLock& locks = impl_->system->GetBodyLockInterfaceNoLock();
	JPH::Body* body1 = locks.TryGetBody(own->second.body);
	JPH::Body* body2 = desc.other != 0 ? locks.TryGetBody(other->second.body) : &JPH::Body::sFixedToWorld;
	if (!body1 || !body2) return;

	JPH::Ref<JPH::Constraint> constraint;
	const JPH::RVec3 anchor = ToJolt(desc.anchor);
	switch (desc.type) {
	case JointType::Fixed: {
		JPH::FixedConstraintSettings s;
		s.mAutoDetectPoint = true;
		constraint = s.Create(*body1, *body2);
		break;
	}
	case JointType::Point: {
		JPH::PointConstraintSettings s;
		s.mPoint1 = s.mPoint2 = anchor;
		constraint = s.Create(*body1, *body2);
		break;
	}
	case JointType::Hinge: {
		JPH::HingeConstraintSettings s;
		s.mPoint1 = s.mPoint2 = anchor;
		const JPH::Vec3 axis = ToJolt(desc.axis).NormalizedOr(JPH::Vec3::sAxisZ());
		s.mHingeAxis1 = s.mHingeAxis2 = axis;
		s.mNormalAxis1 = s.mNormalAxis2 = axis.GetNormalizedPerpendicular();
		constraint = s.Create(*body1, *body2);
		break;
	}
	case JointType::Distance: {
		JPH::DistanceConstraintSettings s;
		s.mPoint1 = anchor;
		s.mPoint2 = ToJolt(desc.otherAnchor);
		constraint = s.Create(*body1, *body2);
		break;
	}
	}
	impl_->system->AddConstraint(constraint);
	impl_->Bodies().ActivateBody(own->second.body);
	Impl::JointRecord record;
	record.constraint = constraint;
	record.other = desc.other;
	record.configKey = desc.configKey;
	impl_->joints.emplace(desc.id, std::move(record));
}

void PhysicsWorld::SubmitImpl(const BodyDesc& desc, bool forQuery)
{
	auto it = impl_->records.find(desc.id);
	if (it != impl_->records.end() && !SameConfig(desc, it->second.desc)) {
		impl_->Destroy(desc.id, it->second); // settings that Jolt fixes at creation; rare, so one path for all of them
		impl_->records.erase(it);
		it = impl_->records.end();
	}
	if (it == impl_->records.end()) {
		Impl::Record record;
		if (!impl_->Create(desc, record)) return;
		record.desc = desc;
		impl_->records.emplace(desc.id, std::move(record));
		return;
	}

	Impl::Record& record = it->second;
	if (!forQuery) record.seen = true;
	impl_->ApplyMove(desc, record, forQuery);
	if (desc.shapes != record.desc.shapes) {
		if (JPH::RefConst<JPH::Shape> shape = BuildShape(desc.shapes)) {
			impl_->Bodies().SetShape(record.body, shape, true, JPH::EActivation::Activate);
		}
	}
	record.desc = desc;
}

void PhysicsWorld::Step(float deltaTime)
{
	for (auto it = impl_->joints.begin(); it != impl_->joints.end();) {
		auto next = std::next(it);
		if (!it->second.seen) impl_->RemoveJoint(it);
		else it->second.seen = false;
		it = next;
	}
	for (auto it = impl_->records.begin(); it != impl_->records.end();) {
		if (!it->second.seen) {
			impl_->Destroy(it->first, it->second);
			it = impl_->records.erase(it);
		} else {
			it->second.seen = false;
			it->second.grounded = false;
			++it;
		}
	}
	if (impl_->addedSinceOptimize > 64) {
		impl_->system->OptimizeBroadPhase();
		impl_->addedSinceOptimize = 0;
	}

	impl_->lastDeltaTime = deltaTime;
	impl_->contactStore.BeginStep();
	impl_->system->Update(deltaTime, 1, impl_->tempAllocator.get(), impl_->jobs.get());

	JPH::BodyInterface& bodies = impl_->Bodies();
	impl_->contactStore.EndStep(bodies);
	for (auto& [id, record] : impl_->records) {
		if (Impl::EffectiveMotion(record.desc) == MotionType::Dynamic) {
			JPH::RVec3 position;
			JPH::Quat rotation;
			bodies.GetPositionAndRotation(record.body, position, rotation);
			record.desc.position = FromJolt(position);
			record.desc.rotation = { rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW() };
			record.desc.velocity = FromJolt(bodies.GetLinearVelocity(record.body));
		}
		record.stepPosition = record.desc.position;
		record.stepRotation = record.desc.rotation;
	}

	// One entry per object pair; a box + sphere on one object produce several sub shape contacts
	contacts_.clear();
	std::set<std::pair<uint32_t, uint32_t>> pairs;
	for (const auto& [key, live] : impl_->contactStore.Live()) {
		if (!live.sensor) {
			constexpr float kGroundNormalY = 0.5f;
			if (live.normal.y > kGroundNormalY) {
				if (auto found = impl_->records.find(live.b); found != impl_->records.end()) found->second.grounded = true;
			} else if (live.normal.y < -kGroundNormalY) {
				if (auto found = impl_->records.find(live.a); found != impl_->records.end()) found->second.grounded = true;
			}
		}
		if (pairs.insert({ (std::min)(live.a, live.b), (std::max)(live.a, live.b) }).second) {
			contacts_.push_back({ live.a, live.b, live.sensor });
		}
	}
}

bool PhysicsWorld::Raycast(const Vector3& origin, const Vector3& direction, float maxDistance, const QueryFilter& accept, RayHit& out) const
{
	class Collector final : public JPH::CastRayCollector {
	public:
		Collector(const JPH::BodyInterface& bodies, const QueryFilter& accept) : bodies_(bodies), accept_(accept) {}
		void AddHit(const JPH::RayCastResult& hit) override {
			if (hit.mFraction <= 0.0f) return; // solid convex: the ray started inside
			if (accept_(static_cast<uint32_t>(bodies_.GetUserData(hit.mBodyID)))) {
				best = hit;
				found = true;
				UpdateEarlyOutFraction(hit.mFraction);
			}
		}
		JPH::RayCastResult best;
		bool found = false;
	private:
		const JPH::BodyInterface& bodies_;
		const QueryFilter& accept_;
	};

	const JPH::Vec3 start = ToJolt(origin);
	const JPH::Vec3 ray = ToJolt(direction) * maxDistance;
	JPH::RayCastSettings settings;
	settings.mTreatConvexAsSolid = true;
	settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces; // model winding differs per asset
	Collector collector(impl_->Bodies(), accept);
	impl_->system->GetNarrowPhaseQueryNoLock().CastRay(JPH::RRayCast(start, ray), settings, collector);
	if (!collector.found) return false;

	const JPH::Vec3 point = start + ray * collector.best.mFraction;
	out.id = static_cast<uint32_t>(impl_->Bodies().GetUserData(collector.best.mBodyID));
	out.distance = maxDistance * collector.best.mFraction;
	out.point = FromJolt(point);
	JPH::BodyLockRead lock(impl_->system->GetBodyLockInterfaceNoLock(), collector.best.mBodyID);
	if (lock.Succeeded()) out.normal = FromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(collector.best.mSubShapeID2, point));
	return true;
}

void PhysicsWorld::OverlapSphere(const Vector3& center, float radius, const QueryFilter& accept, std::vector<uint32_t>& out) const
{
	const JPH::SphereShape sphere((std::max)(radius, kMinShapeSize));
	JPH::CollideShapeSettings settings;
	JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
	impl_->system->GetNarrowPhaseQueryNoLock().CollideShape(&sphere, JPH::Vec3::sReplicate(1.0f),
		JPH::RMat44::sTranslation(ToJolt(center)), settings, JPH::RVec3::sZero(), collector);
	for (const auto& hit : collector.mHits) {
		const uint32_t id = static_cast<uint32_t>(impl_->Bodies().GetUserData(hit.mBodyID2));
		if (std::find(out.begin(), out.end(), id) == out.end() && accept(id)) out.push_back(id);
	}
}

bool PhysicsWorld::GetState(uint32_t id, BodyState& out) const
{
	const auto it = impl_->records.find(id);
	if (it == impl_->records.end()) return false;
	out.position = it->second.desc.position;
	out.rotation = it->second.desc.rotation;
	out.velocity = it->second.desc.velocity;
	out.grounded = it->second.grounded;
	return true;
}

} // namespace Physics
