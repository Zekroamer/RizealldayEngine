#include "EditorSuiteInternal.h"
#include "PhysicsComponents.h"

// Multi-select (Fase 8). selectedObjectIndex_ stays the primary object (Inspector, gizmo); extraSelection_ holds the
// other selected objects by id, because indices move when objects are deleted.

void EditorSuite::SelectObject(int index, bool additive)
{
	const bool valid = index >= 0 && index < static_cast<int>(gameObjects_.size());
	if (!valid || !additive) {
		extraSelection_.clear();
		selectedObjectIndex_ = valid ? index : -1;
		return;
	}
	const uint32_t id = gameObjects_[index].id;
	const bool hasPrimary = selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size());
	if (!hasPrimary) {
		selectedObjectIndex_ = index;
		return;
	}
	if (selectedObjectIndex_ == index) {
		// The primary leaves: the next selected object takes its place
		selectedObjectIndex_ = -1;
		while (!extraSelection_.empty() && selectedObjectIndex_ < 0) {
			selectedObjectIndex_ = FindGameObjectIndexById(extraSelection_.front());
			extraSelection_.erase(extraSelection_.begin());
		}
		return;
	}
	if (const auto it = std::find(extraSelection_.begin(), extraSelection_.end(), id); it != extraSelection_.end()) {
		extraSelection_.erase(it);
	} else {
		extraSelection_.push_back(id);
	}
}

void EditorSuite::SelectObjectRange(int toIndex)
{
	const int count = static_cast<int>(gameObjects_.size());
	if (toIndex < 0 || toIndex >= count) return;
	if (selectedObjectIndex_ < 0 || selectedObjectIndex_ >= count) {
		SelectObject(toIndex, false);
		return;
	}
	const int lo = (std::min)(selectedObjectIndex_, toIndex);
	const int hi = (std::max)(selectedObjectIndex_, toIndex);
	extraSelection_.clear();
	for (int i = lo; i <= hi; ++i) {
		if (i == selectedObjectIndex_) continue;
		extraSelection_.push_back(gameObjects_[i].id);
	}
}

std::vector<uint32_t> EditorSuite::SelectedIds() const
{
	std::vector<uint32_t> ids;
	if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) ids.push_back(gameObjects_[selectedObjectIndex_].id);
	for (uint32_t id : extraSelection_) {
		const bool exists = std::any_of(gameObjects_.begin(), gameObjects_.end(), [&](const GameObject& o) { return o.id == id; });
		if (exists && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
	}
	return ids;
}

bool EditorSuite::IsSelected(uint32_t id) const
{
	const std::vector<uint32_t> ids = SelectedIds();
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void EditorSuite::RemapJointTarget(GameObject& obj, const std::unordered_map<uint32_t, uint32_t>& idMap)
{
	auto* joint = obj.components.Get<JointComponent>("Joint");
	if (!joint || joint->connectedObjectId <= 0) return;
	const auto it = idMap.find(static_cast<uint32_t>(joint->connectedObjectId));
	if (it != idMap.end()) joint->connectedObjectId = static_cast<int>(it->second);
}

std::vector<uint32_t> EditorSuite::DuplicateObjects(const std::vector<uint32_t>& ids)
{
	const auto inList = [&](int id) { return id >= 0 && std::find(ids.begin(), ids.end(), static_cast<uint32_t>(id)) != ids.end(); };
	const auto hasListedAncestor = [&](const GameObject& obj) {
		for (int parent = obj.parentId; parent >= 0;) {
			if (inList(parent)) return true;
			const GameObject* p = FindGameObjectById(static_cast<uint32_t>(parent), false);
			parent = p ? p->parentId : -1;
		}
		return false;
	};

	std::vector<uint32_t> newRoots;
	for (uint32_t rootId : ids) {
		const GameObject* root = FindGameObjectById(rootId, false);
		if (!root || hasListedAncestor(*root)) continue; // copied with its ancestor
		// The subtree, parents before children
		std::vector<uint32_t> subtree{ rootId };
		for (size_t i = 0; i < subtree.size(); ++i) {
			for (const GameObject& o : gameObjects_) {
				if (o.parentId == static_cast<int>(subtree[i])) subtree.push_back(o.id);
			}
		}
		std::unordered_map<uint32_t, uint32_t> idMap;
		for (uint32_t id : subtree) idMap[id] = nextObjectId_++;
		std::vector<GameObject> copies;
		copies.reserve(subtree.size());
		for (uint32_t id : subtree) {
			GameObject* original = FindGameObjectById(id, false);
			for (auto& sc : original->scripts) CaptureScriptFieldsToSaved(sc);
			GameObject copy = *original;
			copy.id = idMap[id];
			copy.childrenIds.clear();
			copy.parentIndex = -1;
			if (id == rootId) {
				copy.name += " (Copy)"; // keeps the original's parent
				copy.translation.x += 1.0f;
			} else {
				copy.parentId = static_cast<int>(idMap[static_cast<uint32_t>(copy.parentId)]);
			}
			RemapJointTarget(copy, idMap);
			copies.push_back(std::move(copy));
		}
		// Right after the original's subtree, like Unity, not at the end of the Hierarchy
		int insertAt = 0;
		for (uint32_t id : subtree) insertAt = (std::max)(insertAt, FindGameObjectIndexById(id) + 1);
		const int count = static_cast<int>(copies.size());
		for (int& pending : pendingDeletes_) {
			if (pending >= insertAt) pending += count; // queued deletes are indices: keep them on their objects
		}
		if (selectedObjectIndex_ >= insertAt) selectedObjectIndex_ += count;
		gameObjects_.insert(gameObjects_.begin() + insertAt, std::make_move_iterator(copies.begin()), std::make_move_iterator(copies.end()));
		newRoots.push_back(idMap[rootId]);
	}
	if (newRoots.empty()) return newRoots;

	RebuildHierarchyLinks();
	selectedObjectIndex_ = FindGameObjectIndexById(newRoots.front());
	extraSelection_.assign(newRoots.begin() + 1, newRoots.end());
	RefreshAllScriptObjectViews();
	EnsureEditModeScriptInstances();
	isSceneDirty_ = true;
	PushUndoSnapshot();
	return newRoots;
}

void EditorSuite::MoveExtraSelection(const Vector3& delta)
{
	if ((delta.x == 0.0f && delta.y == 0.0f && delta.z == 0.0f) || extraSelection_.empty()) return;
	const std::vector<uint32_t> selected = SelectedIds();
	const auto hasSelectedAncestor = [&](const GameObject& obj) {
		for (int parent = obj.parentId; parent >= 0;) {
			if (std::find(selected.begin(), selected.end(), static_cast<uint32_t>(parent)) != selected.end()) return true;
			const GameObject* p = FindGameObjectById(static_cast<uint32_t>(parent), false);
			parent = p ? p->parentId : -1;
		}
		return false;
	};
	for (uint32_t id : extraSelection_) {
		GameObject* obj = FindGameObjectById(id, false);
		if (!obj || hasSelectedAncestor(*obj)) continue; // it moves with its parent
		// delta is in world units: into the space of this object's parent (turned or scaled parents move it the same way)
		const GameObject* parent = obj->parentId >= 0 ? FindGameObjectById(static_cast<uint32_t>(obj->parentId), false) : nullptr;
		const Vector3 local = parent ? TransformUtil::TransformDirection(delta, Mathematics::Inverse(parent->worldMatrix)) : delta;
		obj->translation = obj->translation + local;
	}
	isSceneDirty_ = true;
}

void EditorSuite::RotateScaleExtraSelection(const Vector3& eulerDelta, const Vector3& scaleRatio)
{
	const bool turned = eulerDelta.x != 0.0f || eulerDelta.y != 0.0f || eulerDelta.z != 0.0f;
	const bool scaled = scaleRatio.x != 1.0f || scaleRatio.y != 1.0f || scaleRatio.z != 1.0f;
	if ((!turned && !scaled) || extraSelection_.empty()) return;
	for (uint32_t id : extraSelection_) {
		GameObject* obj = FindGameObjectById(id, false);
		if (!obj) continue;
		// Each one around its own pivot, like the primary
		if (turned) {
			obj->rotation = obj->rotation + eulerDelta;
			obj->rotationQuat = Quaternion::FromEuler(obj->rotation);
		}
		if (scaled) obj->scale = { obj->scale.x * scaleRatio.x, obj->scale.y * scaleRatio.y, obj->scale.z * scaleRatio.z };
	}
	isSceneDirty_ = true;
}
