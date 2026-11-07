#include "EditorSuiteInternal.h"

void EditorSuite::StartPlayMode()
{
	if (prefabMode_.active) {
		LogMessage("[Prefab] Go back to the scene to Play.", 1);
		return;
	}
	if (sceneState_ != EditorSceneState::Edit) {
		sceneState_ = EditorSceneState::Play;
		return;
	}
	persistentIds_.clear();

	// Capture any Edit-mode Inspector tweaks into savedFields before taking the Play snapshot
	for (auto& obj : gameObjects_) {
		for (auto& sc : obj.scripts) {
			CaptureScriptFieldsToSaved(sc);
		}
	}

	SavePlayModeSnapshot();
	playModePostSettings_ = postSettings_;
	DestroyAllScriptInstances();
	sceneState_ = EditorSceneState::Play;
	timeScale_ = 1.0f;
	ClearPrefabCache(); // pick up prefab edits made since the last run
	ResetPrefabPools();
	ResetScriptRuntimeState();
	UIRenderer::GetInstance()->Clear();
	gameTime_ = 0.0;
	stepSingleFrame_ = false;

	RefreshAllScriptObjectViews();

	// 1. Instantiate all Script components for all GameObjects in the scene via GameScripts DLL
	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		auto& obj = gameObjects_[i];
		for (auto& sc : obj.scripts) {
			sc.instance.reset();
			if (sc.className.empty()) continue;

			if (scriptModule_.IsLoaded() && scriptModule_.IsRegistered(sc.className)) {
				sc.instance = ScriptInstancePtr(scriptModule_.CreateScript(sc.className), ScriptInstanceDeleter{ &scriptModule_ });
				sc.isMissingScript = false;
				sc.isCompiling = false;
				if (sc.instance) {
					sc.instance->SetOwner(GetOrUpdateScriptViewById(obj.id), obj.id);
					sc.instance->SetEngineAPI(&engineApi_);
					sc.instance->SetHost(this, this);
					ApplySavedFieldsToScript(sc);
				}
			} else {
				sc.isMissingScript = true;
				LogMessage(std::format("[Scripts] Warning: Script '{}' on GameObject '{}' is missing from GameScripts.dll and cannot run.", sc.className, obj.name), 1);
			}
		}
	}

	// 2. Call Initialize() on all created script instances
	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		if (!gameObjects_[i].isActive) continue;

		for (size_t s = 0; s < gameObjects_[i].scripts.size(); ++s) {
			auto& sc = gameObjects_[i].scripts[s];
			if (sc.instance) {
				ScriptGameObjectView* view = GetOrUpdateScriptViewById(gameObjects_[i].id);
				Vector3 prevEuler = view ? view->rotation : gameObjects_[i].rotation;
				Quaternion prevQuat = view ? view->rotationQuat : gameObjects_[i].rotationQuat;

				sc.instance->SetOwner(view, gameObjects_[i].id);
				sc.instance->SetEngineAPI(&engineApi_);
				sc.instance->SetHost(this, this);
				ScriptBase* instance = sc.instance.get();
				CallScript(instance, gameObjects_[i].id, "Initialize", [&] { instance->Initialize(); });

				if (view) {
					bool eulerChanged = (view->rotation.x != prevEuler.x || view->rotation.y != prevEuler.y || view->rotation.z != prevEuler.z);
					bool quatChanged = (view->rotationQuat.x != prevQuat.x || view->rotationQuat.y != prevQuat.y || view->rotationQuat.z != prevQuat.z || view->rotationQuat.w != prevQuat.w);
					if (eulerChanged && !quatChanged) {
						view->rotationQuat = Quaternion::FromEuler(view->rotation);
					} else if (quatChanged && !eulerChanged) {
						view->rotation = view->rotationQuat.ToEuler();
					}
					SyncViewToGameObject(*view, gameObjects_[i]);
				}
			}
		}
	}

	// Create objects instantiated from Initialize()
	ProcessPendingSpawns();

	// 3. Start Audio Sources configured with Play On Awake (using cached LoadAudio / PlayAudio separation)
	Audio::GetInstance()->StopAllAudio();
	for (auto& obj : gameObjects_) {
		obj.runtimeVoiceHandle = 0;
		if (obj.isActive && (obj.hasAudioSource || obj.type == GameObjectType::AudioSource)) {
			if (obj.audioPlayOnAwake) StartAudioSource(obj);
		}
	}

	if (!pendingDeletes_.empty()) {
		ProcessPendingDeletes();
	}
}

void EditorSuite::StopPlayMode()
{
	persistentIds_.clear();
	// Stop all active runtime audio voices when exiting Play Mode
	Audio::GetInstance()->StopAllAudio();
	Audio::GetInstance()->ResetBusVolumes(); // a script's mute must not outlive the run
	audioFollowers_.clear();
	for (auto& obj : gameObjects_) {
		obj.runtimeVoiceHandle = 0;
	}

	RefreshAllScriptObjectViews();

	// 1. Call OnDestroy() and destroy all active script instances via GameScripts DLL
	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		auto& obj = gameObjects_[i];
		for (auto& sc : obj.scripts) {
			if (sc.instance) {
				sc.instance->SetOwner(GetOrUpdateScriptViewById(obj.id), obj.id);
				sc.instance->SetEngineAPI(&engineApi_);
				sc.instance->SetHost(this, this);
				ScriptBase* instance = sc.instance.get();
				CallScript(instance, obj.id, "OnDestroy", [&] { instance->OnDestroy(); });
				sc.instance.reset();
			}
		}
	}

	pendingDeletes_.clear();
	pendingSpawns_.clear();
	ResetPrefabPools();
	ResetScriptRuntimeState();
	UIRenderer::GetInstance()->Clear();
	sceneState_ = EditorSceneState::Edit;
	timeScale_ = 1.0f;
	gameTime_ = 0.0;
	stepSingleFrame_ = false;

	// 2. Restore scene snapshot (including any objects deleted during Play mode)
	pendingSceneLoad_.clear();
	RestorePlayModeSnapshot();
	RestoreRuntimeSceneOrigin(); // scene path/name/settings if a script switched scenes
	postSettings_ = playModePostSettings_; // scripts may have changed the look

	// 3. Re-create Edit-mode script instances so Inspector can display exposed fields
	EnsureEditModeScriptInstances();

	// No Play-mode script instance is alive anymore, so views of deleted objects can be released
	RefreshAllScriptObjectViews();
	ClearRetiredScriptViews();

	// The restored scene is the undo baseline again (play-mode changes are not part of the history)
	undoBaseline_ = CaptureUndoState();
	undoMergeAllowed_ = false;
}

void EditorSuite::SavePlayModeSnapshot()
{
	playModeSnapshots_.clear();
	playModeObjectSnapshots_.clear();
	playModeObjectSnapshots_.reserve(gameObjects_.size());

	for (auto& obj : gameObjects_) {
		for (auto& sc : obj.scripts) {
			CaptureScriptFieldsToSaved(sc);
		}

		TransformSnapshot s;
		s.id = obj.id;
		s.translation = obj.translation;
		s.rotation = obj.rotation;
		s.rotationQuat = obj.rotationQuat;
		s.scale = obj.scale;
		s.velocity = obj.velocity;
		s.isGrounded = obj.isGrounded;
		playModeSnapshots_.push_back(s);

		GameObject snapCopy = obj;
		for (auto& sc : snapCopy.scripts) {
			sc.instance.reset();
		}
		playModeObjectSnapshots_.push_back(std::move(snapCopy));
	}
	LogMessage(std::format("[Physics] Saved Play Mode snapshot for {} GameObjects.", playModeSnapshots_.size()), 0);
}

void EditorSuite::RestorePlayModeSnapshot()
{
	if (!playModeObjectSnapshots_.empty()) {
		bool structureChanged = (gameObjects_.size() != playModeObjectSnapshots_.size());
		if (!structureChanged) {
			for (size_t i = 0; i < gameObjects_.size(); ++i) {
				if (gameObjects_[i].id != playModeObjectSnapshots_[i].id) {
					structureChanged = true;
					break;
				}
			}
		}

		if (structureChanged) {
			if (dxCommon_ && !dxCommon_->IsDeviceLost()) {
				dxCommon_->SignalAndWaitFence();
			}

			std::vector<GameObject> restored;
			restored.reserve(playModeObjectSnapshots_.size());
			for (auto& snapObj : playModeObjectSnapshots_) {
				GameObject copyObj = snapObj;
				for (auto& sc : copyObj.scripts) {
					sc.instance.reset();
				}
				for (auto& existing : gameObjects_) {
					if (existing.id == copyObj.id) {
						copyObj.editObject3d = std::move(existing.editObject3d);
						copyObj.gameObject3d = std::move(existing.gameObject3d);
						break;
					}
				}
				restored.push_back(std::move(copyObj));
			}
			gameObjects_ = std::move(restored);
		} else {
			// Same objects: restore every component value (scripts, and materials / lights / particles that
			// scripts changed during Play) while keeping the GPU objects. Nothing is destroyed, so no fence wait.
			for (size_t i = 0; i < gameObjects_.size(); ++i) {
				GameObject copyObj = playModeObjectSnapshots_[i];
				for (auto& sc : copyObj.scripts) {
					sc.instance.reset();
				}
				copyObj.editObject3d = std::move(gameObjects_[i].editObject3d);
				copyObj.gameObject3d = std::move(gameObjects_[i].gameObject3d);
				copyObj.shadowTransform = std::move(gameObjects_[i].shadowTransform);
				gameObjects_[i] = std::move(copyObj);
			}
		}
	}

	for (const auto& s : playModeSnapshots_) {
		for (auto& obj : gameObjects_) {
			if (obj.id == s.id) {
				obj.translation = s.translation;
				obj.rotation = s.rotation;
				obj.rotationQuat = s.rotationQuat;
				obj.scale = s.scale;
				obj.velocity = { 0.0f, 0.0f, 0.0f };
				obj.isGrounded = s.isGrounded;
				obj.isColliding = false;
				break;
			}
		}
	}

	// Rebuild hierarchy parent/child indices cleanly
	RebuildHierarchyLinks();
	UpdateWorldMatrices();

	if (selectedObjectIndex_ >= static_cast<int>(gameObjects_.size())) {
		selectedObjectIndex_ = gameObjects_.empty() ? -1 : static_cast<int>(gameObjects_.size()) - 1;
	}

	LogMessage(std::format("[Physics] Restored initial transforms for {} GameObjects.", playModeSnapshots_.size()), 0);
}

