#include "EditorSuiteInternal.h"

GameObject& EditorSuite::CreateGameObject(const std::string& name, GameObjectType type)
{
	GameObject obj{};
	obj.id = nextObjectId_++;
	obj.name = name;
	obj.type = type;
	obj.isActive = true;
	obj.isStatic = false;
	obj.tag = "Untagged";
	obj.layer = 0;
	obj.collisionMask = 0xFFFFFFFF;
	obj.translation = { 0.0f, 0.0f, 0.0f };
	obj.rotation = { 0.0f, 0.0f, 0.0f };
	obj.scale = { 1.0f, 1.0f, 1.0f };
	obj.rotationQuat = Quaternion::Identity();

	gameObjects_.push_back(std::move(obj));
	if (!isSpawningFromScript_) {
		selectedObjectIndex_ = static_cast<int>(gameObjects_.size()) - 1;
		isSceneDirty_ = true;
		// Recorded at the start of the next frame: callers (CreateCube, menus, ...) still fill in
		// mesh/collider/light fields after this returns, and the undo command must contain the final object
		undoPushPending_ = true;
	}
	return gameObjects_.back();
}

GameObject& EditorSuite::CreateCube(const std::string& name, const Vector3& pos)
{
	auto& obj = CreateGameObject(name, GameObjectType::Cube);
	obj.translation = pos;
	obj.hasMesh = true;
	obj.meshType = GameObject::MeshType::Cube;
	obj.meshEnabled = true;
	obj.isLit = true;
	obj.lightingType = 4; // Blinn-Phong
	obj.shininess = 32.0f;
	obj.specularColor = { 1.0f, 1.0f, 1.0f };
	obj.materialColor = { 0.75f, 0.75f, 0.78f, 1.0f };

	// Automatically attach active Box Collider
	obj.hasBoxCollider = true;
	obj.boxColliderEnabled = true;
	obj.boxCenter = { 0.0f, 0.0f, 0.0f };
	obj.boxSize = { 1.0f, 1.0f, 1.0f };
	obj.isTrigger = false;
	obj.providesContacts = true;

	if (!isSpawningFromScript_) {
		LogMessage(std::format("[Hierarchy] Created 3D Cube with Box Collider: {}", name), 0);
	}
	return obj;
}

GameObject& EditorSuite::CreateSphere(const std::string& name, const Vector3& pos)
{
	auto& obj = CreateGameObject(name, GameObjectType::Sphere);
	obj.translation = pos;
	obj.hasMesh = true;
	obj.meshType = GameObject::MeshType::Sphere;
	obj.meshEnabled = true;
	obj.isLit = true;
	obj.lightingType = 4; // Blinn-Phong
	obj.shininess = 48.0f;
	obj.specularColor = { 1.0f, 1.0f, 1.0f };
	obj.materialColor = { 0.35f, 0.65f, 0.85f, 1.0f };

	// Automatically attach active Sphere Collider
	obj.hasSphereCollider = true;
	obj.sphereColliderEnabled = true;
	obj.sphereCenter = { 0.0f, 0.0f, 0.0f };
	obj.sphereRadius = 0.5f;

	if (!isSpawningFromScript_) {
		LogMessage(std::format("[Hierarchy] Created 3D Sphere with Sphere Collider: {}", name), 0);
	}
	return obj;
}

GameObject& EditorSuite::CreatePlane(const std::string& name, const Vector3& pos)
{
	auto& obj = CreateGameObject(name, GameObjectType::Plane);
	obj.translation = pos;
	obj.scale = { 10.0f, 1.0f, 10.0f };
	obj.hasMesh = true;
	obj.meshType = GameObject::MeshType::Plane;
	obj.meshEnabled = true;
	obj.isLit = true;
	obj.lightingType = 4;
	obj.shininess = 16.0f;
	obj.specularColor = { 0.5f, 0.5f, 0.5f };
	obj.materialColor = { 0.30f, 0.32f, 0.35f, 1.0f };
	obj.castShadow = false;
	obj.receiveShadow = true;

	// Automatically attach active Box Collider for floor bounds
	obj.hasBoxCollider = true;
	obj.boxColliderEnabled = true;
	obj.boxCenter = { 0.0f, -0.05f, 0.0f };
	obj.boxSize = { 10.0f, 0.1f, 10.0f };

	if (!isSpawningFromScript_) {
		LogMessage(std::format("[Hierarchy] Created 3D Plane with Box Collider: {}", name), 0);
	}
	return obj;
}

GameObject& EditorSuite::CreateQuad(const std::string& name, const Vector3& pos)
{
	auto& obj = CreateGameObject(name, GameObjectType::Quad);
	obj.translation = pos;
	obj.scale = { 1.0f, 1.0f, 1.0f };
	obj.hasMesh = true;
	obj.meshType = GameObject::MeshType::Quad;
	obj.meshEnabled = true;
	obj.isLit = true;
	obj.lightingType = 0; // Unlit by default for textured quads/sprites
	obj.materialColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	obj.texturePath = "Resources/circle.png";
	obj.textureHandle = TextureManager::GetInstance()->LoadTexture(ResolveAssetPath(obj.texturePath));

	if (!isSpawningFromScript_) {
		LogMessage(std::format("[Hierarchy] Created Textured Quad: {}", name), 0);
	}
	return obj;
}

void EditorSuite::DeleteSelectedGameObject()
{
	if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) {
		QueueDeleteGameObject(selectedObjectIndex_);
	}
}

void EditorSuite::QueueDeleteGameObject(int index)
{
	if (index >= 0 && index < static_cast<int>(gameObjects_.size())) {
		if (std::find(pendingDeletes_.begin(), pendingDeletes_.end(), index) == pendingDeletes_.end()) {
			pendingDeletes_.push_back(index);
		}
	}
}

std::string EditorSuite::GetCollisionLayerName(uint32_t layer)
{
	static constexpr const char* kNames[LAYER_NAMED_COUNT] = {
		"Default", "Player", "Enemy", "PlayerBullet", "Wall", "Trigger", "EnemyBullet", "Pickup"
	};
	layer &= 31u;
	return (layer < LAYER_NAMED_COUNT) ? kNames[layer] : std::format("Layer {}", layer);
}

int EditorSuite::FindGameObjectIndexById(uint32_t id)
{
	auto it = idIndexCache_.find(id);
	if (it != idIndexCache_.end() && it->second < gameObjects_.size() && gameObjects_[it->second].id == id) {
		return static_cast<int>(it->second);
	}
	// Stale or missing entry: the vector changed since the last rebuild
	idIndexCache_.clear();
	idIndexCache_.reserve(gameObjects_.size());
	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		idIndexCache_[gameObjects_[i].id] = i;
	}
	it = idIndexCache_.find(id);
	return (it != idIndexCache_.end()) ? static_cast<int>(it->second) : -1;
}

GameObject* EditorSuite::FindGameObjectById(uint32_t id, bool warnIfMissing)
{
	const int index = FindGameObjectIndexById(id);
	if (index >= 0) {
		return &gameObjects_[static_cast<size_t>(index)];
	}
	if (warnIfMissing) {
		LogMessage(std::format("[Script] Warning: GameObject with ID {} was not found (may have been deleted).", id), 1);
	}
	return nullptr;
}

GameObject* EditorSuite::FindGameObjectByName(const char* name, bool warnIfMissing)
{
	if (!name || name[0] == '\0') {
		return nullptr;
	}
	for (auto& obj : gameObjects_) {
		if (obj.name == name) {
			return &obj;
		}
	}
	if (warnIfMissing) {
		LogMessage(std::format("[Script] Warning: GameObject '{}' was not found (may have been deleted).", name), 1);
	}
	return nullptr;
}

void EditorSuite::QueueDeleteGameObjectById(uint32_t id)
{
	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		if (gameObjects_[i].id == id) {
			QueueDeleteGameObject(static_cast<int>(i));
			return;
		}
	}
	LogMessage(std::format("[Script] Warning: Cannot delete GameObject with ID {} (already deleted or not found).", id), 1);
}

void EditorSuite::ProcessPendingDeletes()
{
	// 1. Process deferred duplication if requested
	if (pendingDuplicateIndex_ >= 0 && pendingDuplicateIndex_ < static_cast<int>(gameObjects_.size())) {
		for (auto& sc : gameObjects_[pendingDuplicateIndex_].scripts) {
			CaptureScriptFieldsToSaved(sc);
		}
		GameObject clone = gameObjects_[pendingDuplicateIndex_];
		clone.id = nextObjectId_++;
		clone.name += " (Copy)";
		clone.translation.x += 1.0f;
		clone.childrenIds.clear();
		clone.parentIndex = -1;
		clone.parentId = -1;
		gameObjects_.push_back(std::move(clone));
		selectedObjectIndex_ = static_cast<int>(gameObjects_.size()) - 1;
		RefreshAllScriptObjectViews();
		EnsureEditModeScriptInstances();
		isSceneDirty_ = true;
		PushUndoSnapshot();
		pendingDuplicateIndex_ = -1;
	}

	if (pendingDeletes_.empty()) {
		return;
	}

	// 2. Recursive Children Deletion (Unity-style):
	// Transitively collect all children and descendants of deleted objects
	std::unordered_set<int> deleteIndices;
	for (int idx : pendingDeletes_) {
		if (idx >= 0 && idx < static_cast<int>(gameObjects_.size())) {
			deleteIndices.insert(idx);
		}
	}

	bool addedChild = true;
	while (addedChild) {
		addedChild = false;
		for (size_t i = 0; i < gameObjects_.size(); ++i) {
			if (deleteIndices.count(static_cast<int>(i)) > 0) continue;
			const auto& cand = gameObjects_[i];
			for (int delIdx : deleteIndices) {
				const auto& parent = gameObjects_[delIdx];
				bool isDescendant = false;
				if (cand.parentIndex == delIdx) {
					isDescendant = true;
				} else if (cand.parentId != -1 && cand.parentId == static_cast<int>(parent.id)) {
					isDescendant = true;
				} else {
					for (uint32_t cid : parent.childrenIds) {
						if (cand.id == cid) {
							isDescendant = true;
							break;
						}
					}
				}
				if (isDescendant) {
					deleteIndices.insert(static_cast<int>(i));
					addedChild = true;
					break;
				}
			}
		}
	}

	if (deleteIndices.empty()) {
		pendingDeletes_.clear();
		return;
	}

	// 3. Record all deleted IDs and log
	std::unordered_set<uint32_t> deletedIds;
	for (int delIdx : deleteIndices) {
		deletedIds.insert(gameObjects_[delIdx].id);
		LogMessage(std::format("[Hierarchy] Deleted GameObject: {}", gameObjects_[delIdx].name), 0);
	}

	// 4. Call OnDestroy() on script instances (in Play mode), release GPU resources, and clear collision references
	RefreshAllScriptObjectViews();
	const bool inPlay = (sceneState_ == EditorSceneState::Play || sceneState_ == EditorSceneState::Pause || IsStandaloneMode());
	for (int delIdx : deleteIndices) {
		auto& obj = gameObjects_[delIdx];
		for (auto& sc : obj.scripts) {
			if (sc.instance) {
				if (inPlay) {
					ScriptGameObjectView* view = GetOrUpdateScriptViewById(obj.id);
					sc.instance->SetOwner(view, obj.id);
					sc.instance->SetEngineAPI(&engineApi_);
					sc.instance->SetHost(this, this);
					ScriptBase* instance = sc.instance.get();
					CallScript(instance, obj.id, "OnDestroy", [&] { instance->OnDestroy(); });
					if (view && view->dirtyFromScript) {
						SyncViewToGameObject(*view, obj);
					}
				}
				sc.instance.reset();
			}
		}
		if (obj.editObject3d) {
			obj.editObject3d.reset();
		}
		if (obj.gameObject3d) {
			obj.gameObject3d.reset();
		}
		if (obj.shadowTransform) {
			obj.shadowTransform.reset();
		}
		if (obj.runtimeVoiceHandle != 0) {
			Audio::GetInstance()->StopAudio(obj.runtimeVoiceHandle);
			obj.runtimeVoiceHandle = 0;
		}
		if (obj.particleQuad) {
			obj.particleQuad.reset();
		}
	}

	// 5. Erase from highest index to lowest index so remaining indices remain valid during erasure
	std::vector<int> sortedDescending(deleteIndices.begin(), deleteIndices.end());
	std::sort(sortedDescending.rbegin(), sortedDescending.rend());
	for (int delIdx : sortedDescending) {
		gameObjects_.erase(gameObjects_.begin() + delIdx);
	}

	// 6. Sanitize selectedObjectIndex_ and Gizmo state
	std::vector<int> sortedAscending(deleteIndices.begin(), deleteIndices.end());
	std::sort(sortedAscending.begin(), sortedAscending.end());

	if (deleteIndices.count(selectedObjectIndex_) > 0) {
		selectedObjectIndex_ = -1;
		isDraggingGizmo_ = false;
		activeGizmoAxis_ = GizmoAxis::None;
		isEditingColliderBounds_ = false;
	} else if (selectedObjectIndex_ >= 0) {
		int countBelow = 0;
		for (int d : sortedAscending) {
			if (d < selectedObjectIndex_) countBelow++;
		}
		selectedObjectIndex_ -= countBelow;
		if (selectedObjectIndex_ >= static_cast<int>(gameObjects_.size())) {
			selectedObjectIndex_ = static_cast<int>(gameObjects_.size()) - 1;
		}
	}

	// 7. Sanitize all remaining GameObjects and refresh script owner pointers
	RefreshAllScriptObjectViews();
	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		auto& obj = gameObjects_[i];

		for (auto& sc : obj.scripts) {
			if (sc.instance) {
				sc.instance->SetOwner(GetOrUpdateScriptViewById(obj.id), obj.id);
				sc.instance->SetEngineAPI(&engineApi_);
				sc.instance->SetHost(this, this);
			}
		}

		// Parent reference sanitization
		if (obj.parentId != -1 && deletedIds.count(static_cast<uint32_t>(obj.parentId)) > 0) {
			obj.parentIndex = -1;
			obj.parentId = -1;
		} else if (obj.parentIndex >= 0) {
			if (deleteIndices.count(obj.parentIndex) > 0) {
				obj.parentIndex = -1;
				obj.parentId = -1;
			} else {
				int countBelow = 0;
				for (int d : sortedAscending) {
					if (d < obj.parentIndex) countBelow++;
				}
				obj.parentIndex -= countBelow;
				if (obj.parentIndex >= 0 && obj.parentIndex < static_cast<int>(gameObjects_.size())) {
					obj.parentId = static_cast<int>(gameObjects_[obj.parentIndex].id);
				} else {
					obj.parentIndex = -1;
					obj.parentId = -1;
				}
			}
		} else if (obj.parentId != -1) {
			// Resync parentIndex from parentId
			obj.parentIndex = -1;
			for (size_t p = 0; p < gameObjects_.size(); ++p) {
				if (gameObjects_[p].id == static_cast<uint32_t>(obj.parentId)) {
					obj.parentIndex = static_cast<int>(p);
					break;
				}
			}
			if (obj.parentIndex == -1) {
				obj.parentId = -1;
			}
		}

		// Particle parent attachment sanitization
		if (obj.attachedParentId != -1 && deletedIds.count(static_cast<uint32_t>(obj.attachedParentId)) > 0) {
			obj.attachToIndex = -1;
			obj.attachedParentId = -1;
			obj.localOffset = { 0.0f, 0.0f, 0.0f };
		} else if (obj.attachToIndex >= 0) {
			if (deleteIndices.count(obj.attachToIndex) > 0) {
				obj.attachToIndex = -1;
				obj.attachedParentId = -1;
				obj.localOffset = { 0.0f, 0.0f, 0.0f };
			} else {
				int countBelow = 0;
				for (int d : sortedAscending) {
					if (d < obj.attachToIndex) countBelow++;
				}
				obj.attachToIndex -= countBelow;
				if (obj.attachToIndex >= 0 && obj.attachToIndex < static_cast<int>(gameObjects_.size())) {
					obj.attachedParentId = static_cast<int>(gameObjects_[obj.attachToIndex].id);
				} else {
					obj.attachToIndex = -1;
					obj.attachedParentId = -1;
					obj.localOffset = { 0.0f, 0.0f, 0.0f };
				}
			}
		} else if (obj.attachedParentId != -1) {
			// Resync attachToIndex from attachedParentId
			obj.attachToIndex = -1;
			for (size_t p = 0; p < gameObjects_.size(); ++p) {
				if (gameObjects_[p].id == static_cast<uint32_t>(obj.attachedParentId)) {
					obj.attachToIndex = static_cast<int>(p);
					break;
				}
			}
			if (obj.attachToIndex == -1) {
				obj.attachedParentId = -1;
				obj.localOffset = { 0.0f, 0.0f, 0.0f };
			}
		}

		// Filter childrenIds
		obj.childrenIds.erase(
			std::remove_if(obj.childrenIds.begin(), obj.childrenIds.end(),
				[&](uint32_t cid) { return deletedIds.count(cid) > 0; }),
			obj.childrenIds.end()
		);
	}

	// 8. Sanitize tetris grid and piece cubes
	for (int y = 0; y < 20; ++y) {
		for (int x = 0; x < 10; ++x) {
			if (deletedIds.count(tetrisBlockIds_[y][x]) > 0) {
				tetrisBlockIds_[y][x] = 0;
				tetrisBoard_[y][x] = 0;
			}
		}
	}
	currentPieceCubeIds_.erase(
		std::remove_if(currentPieceCubeIds_.begin(), currentPieceCubeIds_.end(),
			[&](uint32_t cid) { return deletedIds.count(cid) > 0; }),
		currentPieceCubeIds_.end()
	);

	pendingDeletes_.clear();
	if (sceneState_ == EditorSceneState::Edit) {
		playModeSnapshots_.erase(
			std::remove_if(playModeSnapshots_.begin(), playModeSnapshots_.end(),
				[&](const TransformSnapshot& snap) { return deletedIds.count(snap.id) > 0; }),
			playModeSnapshots_.end()
		);
		isSceneDirty_ = true;
		PushUndoSnapshot();
	}
}

