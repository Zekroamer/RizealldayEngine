// Animator runtime glue (roadmap 5.1): the "Animator" data component, the controller asset cache,
// per-frame state machine updates, animation events for scripts and the skeleton overlay.
#include "EditorSuiteInternal.h"
#include "3d/Object3d.h"
#include "3d/AnimationPlayer.h"

namespace {
	constexpr const char* kAnimatorComponent = "Animator";
	constexpr auto kControllerRecheck = std::chrono::milliseconds(500);

	// Controllers always live in the project; ResolveAssetPath caches misses, which breaks right after creating one
	std::filesystem::path ControllerFile(const std::filesystem::path& projectRoot, const std::string& assetPath)
	{
		const std::filesystem::path p(assetPath);
		if (p.is_absolute() || projectRoot.empty()) return p;
		return projectRoot / p;
	}
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------
void EditorSuite::RegisterAnimatorComponent()
{
	if (ComponentRegistry::Get().Find(kAnimatorComponent)) return;
	ComponentInfo& info = RegisterDataComponent<AnimatorComponent>(kAnimatorComponent, "Rendering", {
		ComponentField{ "controller", ComponentFieldType::Asset, offsetof(AnimatorComponent, controller), sizeof(AnimatorComponent::controller), 0.0f, 0.0f,
			"Animator Controller asset (.animator.json). Drop one from the Project panel." },
		ComponentField{ "speed", ComponentFieldType::Float, offsetof(AnimatorComponent, speed), sizeof(float), 0.0f, 10.0f,
			"Multiplies the speed of every state" },
	});
	info.drawExtra = [](EditorSuite& s, GameObject& obj, ComponentData& data) {
#ifdef USE_IMGUI
		auto& a = *static_cast<AnimatorComponent*>(data.Data());
		if (a.controller[0] == '\0') {
			if (ImGui::Button(TrId("Create Animator Controller"))) {
				const std::string path = s.CreateAnimatorControllerAsset(obj.name.empty() ? "New" : obj.name);
				if (!path.empty()) {
					strncpy_s(a.controller, path.c_str(), _TRUNCATE);
					s.isSceneDirty_ = true;
					s.PushUndoSnapshot();
				}
			}
			return;
		}
		const std::shared_ptr<AnimatorController> controller = s.GetAnimatorController(a.controller);
		if (!controller) {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", Tr("Controller file not found or invalid."));
			return;
		}
		if (ImGui::Button(TrId("Open Animator Window"))) {
			s.animatorWindowAsset_ = a.controller;
			s.showAnimatorWindow_ = true;
		}
		if (a.runtime.started) {
			const AnimState* state = controller->FindState(a.runtime.currentState);
			ImGui::TextDisabled("%s: %s%s", Tr("State"), state ? state->name.c_str() : "-",
				a.runtime.InTransition() ? " (fading)" : "");
		} else if (!obj.editObject3d || !obj.editObject3d->GetModel() || !obj.editObject3d->GetModel()->HasBones()) {
			ImGui::TextDisabled("%s", Tr("Needs a skinned (animated) model on this object."));
		}
#else
		(void)s; (void)obj; (void)data;
#endif
	};
}

AnimatorComponent* EditorSuite::GetAnimator(GameObject& obj)
{
	return obj.components.Get<AnimatorComponent>(kAnimatorComponent);
}

// ---------------------------------------------------------------------------
// Controller assets
// ---------------------------------------------------------------------------
std::shared_ptr<AnimatorController> EditorSuite::GetAnimatorController(const std::string& assetPath)
{
	if (assetPath.empty()) return nullptr;
	const auto now = std::chrono::steady_clock::now();
	auto it = animatorControllers_.find(assetPath);
	// In Play the cache is trusted; in Edit the file is re-checked twice a second (edited outside or by the window)
	if (it != animatorControllers_.end() && (sceneState_ != EditorSceneState::Edit || now - it->second.checkedAt < kControllerRecheck)) {
		return it->second.controller;
	}

	std::error_code ec;
	const std::filesystem::path file = ControllerFile(currentProjectRoot_, assetPath);
	const auto stamp = std::filesystem::last_write_time(file, ec);
	if (ec) {
		animatorControllers_[assetPath] = { nullptr, {}, now };
		return nullptr;
	}
	if (it != animatorControllers_.end() && it->second.controller && it->second.stamp == stamp) {
		it->second.checkedAt = now;
		return it->second.controller;
	}

	std::ifstream in(file, std::ios::binary);
	std::stringstream buffer;
	buffer << in.rdbuf();
	std::string error;
	std::optional<AnimatorController> parsed = AnimatorController::FromJson(buffer.str(), &error);
	if (!parsed) {
		LogMessage(std::format("[Animator] '{}' is invalid: {}", assetPath, error), 2);
		animatorControllers_[assetPath] = { nullptr, stamp, now };
		return nullptr;
	}
	auto controller = std::make_shared<AnimatorController>(std::move(*parsed));
	animatorControllers_[assetPath] = { controller, stamp, now };
	return controller;
}

bool EditorSuite::SaveAnimatorController(const std::string& assetPath, const AnimatorController& controller)
{
	std::error_code ec;
	const std::filesystem::path file = ControllerFile(currentProjectRoot_, assetPath);
	std::filesystem::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	if (!out) {
		LogMessage(std::format("[Animator] Cannot write '{}'.", assetPath), 2);
		return false;
	}
	out << controller.ToJson() << '\n';
	out.close();
	// The cache holds the saved version, so running Animators pick the change up immediately
	auto shared = std::make_shared<AnimatorController>(controller);
	animatorControllers_[assetPath] = { shared, std::filesystem::last_write_time(file, ec), std::chrono::steady_clock::now() };
	AssetDb().GuidForPath(assetPath);
	return true;
}

std::string EditorSuite::CreateAnimatorControllerAsset(const std::string& name)
{
	std::string base;
	for (char c : name) base += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
	if (base.empty()) base = "New";
	std::error_code ec;
	std::string assetPath;
	for (int n = 0; n < 1000; ++n) {
		assetPath = std::format("Assets/Animators/{}{}.animator.json", base, n == 0 ? std::string() : std::to_string(n));
		if (!std::filesystem::exists(ControllerFile(currentProjectRoot_, assetPath), ec)) break;
	}
	AnimatorController controller;
	controller.AddState("Idle", { 0.0f, 0.0f });
	if (!SaveAnimatorController(assetPath, controller)) return {};
	LogMessage(std::format("[Animator] Created {}", assetPath), 0);
	return assetPath;
}

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------
Model* EditorSuite::ResolveClipModel(const AnimState& state)
{
	if (state.clipModel.empty()) return nullptr;
	std::error_code ec;
	const std::filesystem::path path(ResolveAssetPath(state.clipModel));
	if (!std::filesystem::exists(path, ec)) return nullptr;
	return Model::CreateFromFile(path.parent_path().string(), path.filename().string());
}

void EditorSuite::ApplyAnimatorState(GameObject& obj, const AnimatorComponent& animator, const AnimState& state, float fadeSeconds)
{
	if (!obj.editObject3d) return;
	Model* source = ResolveClipModel(state);
	const Model* owner = source ? source : obj.editObject3d->GetModel();
	int clip = (owner && !state.clipName.empty()) ? owner->FindAnimationIndex(state.clipName) : -1;
	if (clip < 0) clip = state.clipIndex;
	obj.editObject3d->CrossFade(source, clip, fadeSeconds, state.loop, state.speed * animator.speed);
}

void EditorSuite::UpdateAnimations(float deltaTime, bool runStateMachines)
{
	RIZE_PROFILE_CPU("Animation");
	std::vector<std::pair<uint32_t, std::string>> events;
	for (GameObject& obj : gameObjects_) {
		if (!obj.isActive || !obj.editObject3d) continue;
		Object3d& o3d = *obj.editObject3d;
		Model* model = o3d.GetModel();
		if (!model || !model->HasBones()) continue;

		AnimatorComponent* animator = GetAnimator(obj);
		const std::shared_ptr<AnimatorController> controller =
			(animator && animator->controller[0] != '\0') ? GetAnimatorController(animator->controller) : nullptr;

		if (animator && controller && !controller->states.empty()) {
			AnimationPlayer& player = o3d.GetAnimationPlayer();
			if (runStateMachines) {
				AnimatorRuntime& rt = animator->runtime;
				AnimatorRuntime::Change change;
				if (!rt.started || animator->runtimeController != animator->controller) {
					rt = AnimatorRuntime{};
					animator->runtimeController = animator->controller;
					change = rt.Start(*controller);
				} else {
					change = rt.Step(*controller, deltaTime, player.NormalizedTime(model));
				}
				if (change.changed) {
					if (const AnimState* s = controller->FindState(change.state)) ApplyAnimatorState(obj, *animator, *s, change.fadeSeconds);
				}
				o3d.AdvanceAnimation(deltaTime);

				// Events of the current state crossed by this frame's time window
				if (const AnimState* s = controller->FindState(rt.currentState); s && !s->events.empty()) {
					const float duration = AnimationPlayer::Duration(player.Current(), model);
					if (duration > 0.0f) {
						std::vector<std::string> fired;
						AnimatorRuntime::CollectEvents(*s, player.LastAdvanceStart() / duration, player.LastAdvanceEnd() / duration, player.Current().loop, fired);
						for (std::string& e : fired) events.emplace_back(obj.id, std::move(e));
					}
				}
			} else {
				// Edit mode: loop the entry state (or the state selected in the Animator window)
				uint32_t stateId = controller->EntryStateId();
				if (animatorPreviewObject_ == obj.id && controller->FindState(animatorPreviewState_)) stateId = animatorPreviewState_;
				if (const AnimState* s = controller->FindState(stateId)) {
					Model* source = ResolveClipModel(*s);
					const Model* owner = source ? source : model;
					int clip = !s->clipName.empty() ? owner->FindAnimationIndex(s->clipName) : -1;
					if (clip < 0) clip = s->clipIndex;
					const AnimationPlayer::Track& cur = player.Current();
					if (cur.clip != clip || cur.source != (source == model ? nullptr : source)) ApplyAnimatorState(obj, *animator, *s, 0.0f);
					player.Current().speed = s->speed * animator->speed;
					player.Current().loop = s->loop;
				}
				o3d.AdvanceAnimation(obj.animationPlaying ? deltaTime : 0.0f);
			}
			continue;
		}

		// No Animator: the clip chosen in the Mesh Inspector / by PlayAnimation() from scripts
		o3d.SetCurrentAnimationIndex((std::max)(0, obj.animationIndex));
		o3d.SetAnimationSpeed(obj.animationSpeed);
		o3d.SetLoopAnimation(obj.animationLoop);
		o3d.AdvanceAnimation(obj.animationPlaying ? deltaTime : 0.0f);
	}

	for (const auto& [id, name] : events) {
		DispatchScriptAnimationEvent(id, name.c_str());
	}
}

void EditorSuite::DispatchScriptAnimationEvent(uint32_t objectId, const char* eventName)
{
	if (sceneState_ == EditorSceneState::Edit && !IsStandaloneMode()) return;
	GameObject* obj = FindGameObjectById(objectId, false);
	if (!obj || !obj->isActive || obj->scripts.empty()) return;
	ScriptGameObjectView* view = GetOrUpdateScriptViewById(objectId);
	if (!view) return;
	for (size_t i = 0; i < obj->scripts.size(); ++i) {
		auto& sc = obj->scripts[i];
		if (!sc.instance) continue;
		sc.instance->SetOwner(view, obj->id);
		sc.instance->SetEngineAPI(&engineApi_);
		sc.instance->SetHost(this, this);
		ScriptBase* instance = sc.instance.get();
		CallScript(instance, objectId, "OnAnimationEvent", [&] { instance->OnAnimationEvent(eventName); });
		obj = FindGameObjectById(objectId, false); // callbacks never resize gameObjects_, but stay safe
		if (!obj) return;
		SyncViewToGameObject(*view, *obj);
	}
}
