#include "EditorSuiteInternal.h"

// Transform hierarchy: parentId (saved) is the source of truth, parentIndex / childrenIds / worldMatrix are derived.

void EditorSuite::RebuildHierarchyLinks()
{
	std::unordered_map<uint32_t, size_t> idToIndex;
	idToIndex.reserve(gameObjects_.size());
	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		idToIndex[gameObjects_[i].id] = i;
	}

	for (auto& obj : gameObjects_) {
		obj.childrenIds.clear();
		obj.parentIndex = -1;
	}

	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		GameObject& obj = gameObjects_[i];
		if (obj.parentId == -1) continue;

		auto it = idToIndex.find(static_cast<uint32_t>(obj.parentId));
		if (it == idToIndex.end() || it->second == i) {
			// Missing parent or self-parenting: treat as a root
			obj.parentId = -1;
			continue;
		}
		obj.parentIndex = static_cast<int>(it->second);
		gameObjects_[it->second].childrenIds.push_back(obj.id);
	}

	hierarchyCycleWarned_ = false;
}

void EditorSuite::UpdateWorldMatrices()
{
	const size_t count = gameObjects_.size();
	if (count == 0) return;

	std::unordered_map<uint32_t, size_t> idToIndex;
	idToIndex.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		idToIndex[gameObjects_[i].id] = i;
	}

	enum class State : uint8_t { Unvisited, InProgress, Done };
	std::vector<State> state(count, State::Unvisited);
	bool cycleFound = false;

	// Resolves one object's world matrix (parents first); a cycle is broken by treating the object as a root
	auto resolve = [&](auto&& self, size_t index) -> void {
		GameObject& obj = gameObjects_[index];
		state[index] = State::InProgress;

		Matrix4x4 parentWorld = Mathematics::MakeIdentity4x4();
		bool hasParent = false;
		if (obj.parentId != -1) {
			auto it = idToIndex.find(static_cast<uint32_t>(obj.parentId));
			if (it != idToIndex.end() && it->second != index) {
				const size_t p = it->second;
				if (state[p] == State::InProgress) {
					cycleFound = true;
				} else {
					if (state[p] == State::Unvisited) self(self, p);
					parentWorld = gameObjects_[p].worldMatrix;
					hasParent = true;
					Matrix4x4 boneMatrix;
					if (!obj.components.Empty() && ResolveBoneAttachment(obj, gameObjects_[p], boneMatrix)) {
						parentWorld = Mathematics::Multiply(boneMatrix, parentWorld);
					}
				}
			}
		}

		const Matrix4x4 local = TransformUtil::ComposeTRS(obj.translation, obj.rotationQuat, obj.scale);
		obj.worldMatrix = hasParent ? Mathematics::Multiply(local, parentWorld) : local;
		state[index] = State::Done;
	};

	for (size_t i = 0; i < count; ++i) {
		if (state[i] == State::Unvisited) resolve(resolve, i);
	}

	if (cycleFound && !hierarchyCycleWarned_) {
		hierarchyCycleWarned_ = true;
		LogMessage("[Hierarchy] Parent cycle detected; the affected objects are treated as roots.", 1);
	}
}

bool EditorSuite::IsDescendantOf(uint32_t candidateId, uint32_t ancestorId) const
{
	if (candidateId == ancestorId) return false;

	std::unordered_map<uint32_t, int> parentOf;
	parentOf.reserve(gameObjects_.size());
	for (const auto& obj : gameObjects_) {
		parentOf[obj.id] = obj.parentId;
	}

	uint32_t current = candidateId;
	for (size_t step = 0; step <= gameObjects_.size(); ++step) {
		auto it = parentOf.find(current);
		if (it == parentOf.end() || it->second == -1) return false;
		current = static_cast<uint32_t>(it->second);
		if (current == ancestorId) return true;
	}
	return false; // Cycle guard
}

bool EditorSuite::ReparentGameObject(uint32_t childId, int newParentId, bool keepWorldTransform)
{
	GameObject* child = FindGameObjectById(childId, false);
	if (!child) {
		LogMessage("[Hierarchy] Reparent failed: object not found.", 1);
		return false;
	}

	if (newParentId != -1) {
		const uint32_t parentUid = static_cast<uint32_t>(newParentId);
		if (parentUid == childId) {
			LogMessage("[Hierarchy] Cannot parent an object to itself.", 1);
			return false;
		}
		if (!FindGameObjectById(parentUid, false)) {
			LogMessage("[Hierarchy] Reparent failed: parent not found.", 1);
			return false;
		}
		if (IsDescendantOf(parentUid, childId)) {
			LogMessage("[Hierarchy] Cannot parent an object to one of its own descendants.", 1);
			return false;
		}
	}

	if (child->parentId == newParentId) return true;

	if (keepWorldTransform) {
		UpdateWorldMatrices();
		Matrix4x4 newParentWorld = Mathematics::MakeIdentity4x4();
		if (newParentId != -1) {
			if (const GameObject* parent = FindGameObjectById(static_cast<uint32_t>(newParentId), false)) {
				newParentWorld = parent->worldMatrix;
			}
		}
		const Matrix4x4 local = Mathematics::Multiply(child->worldMatrix, Mathematics::Inverse(newParentWorld));
		Vector3 t{};
		Quaternion q = Quaternion::Identity();
		Vector3 s{};
		TransformUtil::Decompose(local, t, q, s);
		child->translation = t;
		child->rotationQuat = q;
		child->scale = s;
		child->rotation = q.ToEuler();
	}

	child->parentId = newParentId;
	RebuildHierarchyLinks();
	UpdateWorldMatrices();
	isSceneDirty_ = true;
	PushUndoSnapshot();
	return true;
}

void EditorSuite::GetWorldTRS(const GameObject& obj, Vector3& t, Quaternion& q, Vector3& s) const
{
	if (obj.parentId == -1) {
		t = obj.translation;
		q = obj.rotationQuat;
		s = obj.scale;
		return;
	}
	TransformUtil::Decompose(obj.worldMatrix, t, q, s);
}
