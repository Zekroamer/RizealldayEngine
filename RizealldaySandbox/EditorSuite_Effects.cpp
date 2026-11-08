#include "EditorSuiteInternal.h"

// Layered effects (Effect Editor plan, Fase C): an effect is a prefab whose root has an "Effect Timing" component.
// Every object of the root's hierarchy with GPU Particles or a Trail is a layer. A layer uses its own Effect Timing
// when it has one (start delay, duration, pulse), else the root's. The pure timing rules are in EffectTiming.h.

void EditorSuite::RegisterEffectComponents()
{
	ComponentInfo& timing = RegisterDataComponent<EffectTimingComponent>(kEffectTimingComponent, "Rendering", {
		RIZE_FIELD(EffectTimingComponent, startDelay, ComponentFieldType::Float, 0.0f, 60.0f),
		RIZE_FIELD(EffectTimingComponent, duration, ComponentFieldType::Float, 0.0f, 600.0f),
		RIZE_FIELD(EffectTimingComponent, loop, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(EffectTimingComponent, despawnWhenDone, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(EffectTimingComponent, pulseBloom, ComponentFieldType::Float, 0.0f, 10.0f),
		RIZE_FIELD(EffectTimingComponent, pulseBlur, ComponentFieldType::Float, 0.0f, 1.0f),
		RIZE_FIELD(EffectTimingComponent, pulseShake, ComponentFieldType::Float, 0.0f, 2.0f),
		RIZE_FIELD(EffectTimingComponent, pulseSeconds, ComponentFieldType::Float, 0.01f, 5.0f) });
	for (ComponentField& field : timing.fields) {
		const std::string_view name = field.name;
		if (name == "duration") field.tooltip = "How long this layer emits. 0 = until the effect is removed.";
		else if (name == "loop") field.tooltip = "On the effect's root: start over when every layer stopped emitting.";
		else if (name == "despawnWhenDone") field.tooltip = "On the effect's root, in Play: remove the instance when its last particles died.";
		else if (name == "pulseBloom") field.tooltip = "Impact pulse fired when this layer starts: extra bloom, radial blur and camera shake.";
	}
#ifdef USE_IMGUI
	timing.drawExtra = [](EditorSuite& suite, GameObject&, ComponentData&) {
		if (ImGui::Button(TrId("Open Effect Editor"))) suite.showEffectEditor_ = true;
	};
#endif
}

uint32_t EditorSuite::EffectRootOf(uint32_t id)
{
	uint32_t root = 0;
	// A broken file can make a parent cycle: never take more steps than there are objects
	size_t steps = gameObjects_.size();
	for (const GameObject* obj = FindGameObjectById(id, false); obj && steps-- > 0; ) {
		if (obj->components.Has(kEffectTimingComponent)) root = obj->id;
		obj = obj->parentId >= 0 ? FindGameObjectById(static_cast<uint32_t>(obj->parentId), false) : nullptr;
	}
	return root;
}

namespace {
	struct EffectLayer {
		uint32_t id = 0;
		EffectLayerWindow window;
		const EffectTimingComponent* timing = nullptr; // the layer's own, or the root's
		GpuParticlesComponent* particles = nullptr;
		TrailComponent* trail = nullptr;
	};

	/// The layers of an effect: the root and its descendants that emit something or carry their own timing.
	/// The one place that decides what a layer is and how long it lasts (runtime and the Effect Editor lanes).
	std::vector<EffectLayer> CollectEffectLayers(EditorSuite& suite, uint32_t rootId)
	{
		std::vector<EffectLayer> layers;
		GameObject* root = suite.FindGameObjectById(rootId, false);
		const auto* rootTiming = root ? root->components.Get<EffectTimingComponent>(kEffectTimingComponent) : nullptr;
		if (!rootTiming) return layers;
		std::vector<uint32_t> ids = { rootId };
		std::unordered_set<uint32_t> seen = { rootId }; // a parent cycle must not grow the list forever
		for (size_t i = 0; i < ids.size(); ++i) {
			GameObject* obj = suite.FindGameObjectById(ids[i], false);
			if (!obj) continue;
			for (uint32_t child : obj->childrenIds) {
				if (seen.insert(child).second) ids.push_back(child);
			}
			EffectLayer layer{ obj->id };
			layer.particles = obj->components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
			layer.trail = obj->components.Get<TrailComponent>(kTrailComponent);
			const auto* own = obj->components.Get<EffectTimingComponent>(kEffectTimingComponent);
			if (!own && !layer.particles && !layer.trail) continue;
			layer.timing = own ? own : rootTiming;
			layer.window.startDelay = (std::max)(layer.timing->startDelay, 0.0f);
			layer.window.duration = (std::max)(layer.timing->duration, 0.0f);
			if (layer.particles) layer.window.tail = (std::max)(layer.particles->lifeMax, 0.0f);
			if (layer.trail) layer.window.tail = (std::max)(layer.window.tail, layer.trail->lifetime);
			layers.push_back(layer);
		}
		return layers;
	}
}

void EditorSuite::AdvanceEffectRoot(GameObject& root, float deltaTime, bool inPlay)
{
	auto* rootTiming = root.components.Get<EffectTimingComponent>(kEffectTimingComponent);
	if (!rootTiming || rootTiming->finished) return;
	const uint32_t rootId = root.id;

	const std::vector<EffectLayer> layers = CollectEffectLayers(*this, rootId);
	std::vector<EffectLayerWindow> windows;
	for (const EffectLayer& layer : layers) windows.push_back(layer.window);
	const float period = EffectLength(windows, !rootTiming->loop);

	const EffectClock clock = AdvanceEffect(rootTiming->time, deltaTime, period, rootTiming->loop);
	rootTiming->time = clock.time;
	if (clock.restarted) rootTiming->started.clear();

	for (const EffectLayer& layer : layers) {
		const EffectPhase phase = clock.finished ? EffectPhase::Done
			: EffectLayerPhase(layer.window.startDelay, layer.window.duration, rootTiming->time);
		const int gate = (phase == EffectPhase::Emitting) ? kEffectGateOn : kEffectGateOff;
		if (layer.particles) layer.particles->effectGate = gate;
		if (layer.trail) layer.trail->effectGate = gate;
		const bool alreadyStarted = std::find(rootTiming->started.begin(), rootTiming->started.end(), layer.id) != rootTiming->started.end();
		if (phase == EffectPhase::Waiting || alreadyStarted || clock.finished) continue;
		rootTiming->started.push_back(layer.id);
		if (layer.particles && layer.particles->spawnBurst > 0) {
			layer.particles->burst = std::clamp(layer.particles->burst + layer.particles->spawnBurst, 0, (std::max)(layer.particles->capacity, 1));
		}
		const EffectTimingComponent& t = *layer.timing;
		const bool ownTiming = layer.timing != rootTiming || layer.id == rootId; // a layer without its own timing does not repeat the root's pulse
		if (ownTiming && (t.pulseBloom > 0.0f || t.pulseBlur > 0.0f || t.pulseShake > 0.0f)) {
			ImpactPulse pulse;
			pulse.bloomBoost = t.pulseBloom;
			pulse.radialBlur = t.pulseBlur;
			pulse.shake = t.pulseShake;
			pulse.seconds = t.pulseSeconds;
			TriggerImpactPulse(pulse);
		}
	}

	if (clock.finished) {
		rootTiming->finished = true;
		if (inPlay && rootTiming->despawnWhenDone) QueueDespawn(rootId);
	}
}

void EditorSuite::UpdateEffects(float deltaTime)
{
	const bool inPlay = sceneState_ != EditorSceneState::Edit || IsStandaloneMode();
	// Ids first: QueueDespawn and the timing never add objects, but a root found later may sit inside an earlier one
	std::vector<uint32_t> roots;
	for (const GameObject& obj : gameObjects_) {
		if (obj.isActive && obj.components.Has(kEffectTimingComponent) && EffectRootOf(obj.id) == obj.id) roots.push_back(obj.id);
	}
	for (uint32_t id : roots) {
		if (GameObject* root = FindGameObjectById(id, false)) AdvanceEffectRoot(*root, deltaTime, inPlay);
	}
}

void EditorSuite::ResetEffects()
{
	effectPreviewRoot_ = 0;
	effectPreviewPlaying_ = false;
	effectPreviewClock_ = 0.0;
	for (GameObject& obj : gameObjects_) {
		if (auto* t = obj.components.Get<EffectTimingComponent>(kEffectTimingComponent)) {
			t->time = 0.0f;
			t->finished = false;
			t->started.clear();
		}
		if (auto* p = obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent)) p->effectGate = kEffectGateNone;
		if (auto* trail = obj.components.Get<TrailComponent>(kTrailComponent)) trail->effectGate = kEffectGateNone;
	}
}

void EditorSuite::StartEffectPreview(uint32_t rootId)
{
	ResetEffects();
	if (!FindGameObjectById(rootId, false) || EffectRootOf(rootId) != rootId) return;
	effectPreviewRoot_ = rootId;
	effectPreviewPlaying_ = true;
}

void EditorSuite::StopEffectPreview()
{
	ResetEffects();
}

void EditorSuite::UpdateEffectPreview(float realDeltaTime)
{
	if (effectPreviewRoot_ == 0) return;
	GameObject* root = FindGameObjectById(effectPreviewRoot_, false);
	if (!root || sceneState_ != EditorSceneState::Edit) {
		StopEffectPreview();
		return;
	}
	if (!effectPreviewPlaying_ || !std::isfinite(realDeltaTime) || realDeltaTime <= 0.0f) return;
	const float dt = realDeltaTime * std::clamp(effectPreviewSpeed_, 0.05f, 4.0f);
	effectPreviewClock_ += dt;
	AdvanceEffectRoot(*root, dt, false);
}

void EditorSuite::SetEffectPreviewMotion(EffectPreviewMotion motion, float radius, float cyclesPerSecond)
{
	effectPreviewMotion_ = (static_cast<int>(motion) >= 0 && motion < EffectPreviewMotion::Count) ? motion : EffectPreviewMotion::None;
	effectPreviewRadius_ = std::isfinite(radius) ? std::clamp(radius, 0.0f, 100.0f) : 0.0f;
	effectPreviewCycles_ = std::isfinite(cyclesPerSecond) ? std::clamp(cyclesPerSecond, 0.0f, 10.0f) : 0.0f;
}

uint32_t EditorSuite::CreateEffectFromTemplate(const std::string& name, const Vector3& position)
{
	if (std::find_if(std::begin(kEffectTemplates), std::end(kEffectTemplates), [&](const char* t) { return name == t; }) == std::end(kEffectTemplates)) return 0;

	const auto make = [&](const std::string& objectName, uint32_t parent) {
		GameObject& obj = CreateGameObject(objectName, GameObjectType::Empty);
		const uint32_t id = obj.id;
		if (parent != 0) ReparentGameObject(id, static_cast<int>(parent), false);
		if (GameObject* o = FindGameObjectById(id, false)) o->translation = parent != 0 ? Vector3{ 0.0f, 0.0f, 0.0f } : position;
		return id;
	};
	const auto timing = [&](uint32_t id) -> EffectTimingComponent* {
		GameObject* obj = FindGameObjectById(id, false);
		if (!obj || !AddComponentByName(*obj, kEffectTimingComponent)) return nullptr;
		return FindGameObjectById(id, false)->components.Get<EffectTimingComponent>(kEffectTimingComponent);
	};
	const auto particles = [&](uint32_t id, const char* preset) -> GpuParticlesComponent* {
		GameObject* obj = FindGameObjectById(id, false);
		if (!obj || !AddComponentByName(*obj, kGpuParticlesComponent)) return nullptr;
		auto* p = FindGameObjectById(id, false)->components.Get<GpuParticlesComponent>(kGpuParticlesComponent);
		if (p) ApplyGpuParticlePreset(*p, preset);
		return p;
	};
	const auto burstOnly = [](GpuParticlesComponent* p, int count, int capacity) {
		if (!p) return;
		p->emitRate = 0.0f;
		p->spawnBurst = count;
		p->capacity = capacity;
	};

	const uint32_t root = make(name, 0);
	EffectTimingComponent* t = timing(root);
	if (!t) return 0;
	if (name == "Hit Spark") {
		t->duration = 0.1f;
		t->pulseBloom = 0.4f;
		t->pulseShake = 0.03f;
		t->pulseSeconds = 0.15f;
		burstOnly(particles(make("Sparks", root), "Sparks"), 40, 200);
		if (GpuParticlesComponent* flash = particles(make("Flash", root), "Explosion")) {
			burstOnly(flash, 3, 16);
			flash->speedMin = flash->speedMax = 0.0f;
			flash->lifeMin = 0.08f;
			flash->lifeMax = 0.15f;
			flash->sizeStart = 1.2f;
			flash->sizeEnd = 0.0f;
		}
	} else if (name == "Explosion") {
		t->duration = 0.2f;
		t->pulseBloom = 1.5f;
		t->pulseBlur = 0.25f;
		t->pulseShake = 0.3f;
		t->pulseSeconds = 0.35f;
		burstOnly(particles(make("Fireball", root), "Explosion"), 150, 400);
		const uint32_t smoke = make("Smoke", root);
		particles(smoke, "Smoke");
		if (EffectTimingComponent* st = timing(smoke)) {
			st->startDelay = 0.1f;
			st->duration = 0.6f;
		}
		if (auto* p = FindGameObjectById(smoke, false)->components.Get<GpuParticlesComponent>(kGpuParticlesComponent)) p->emitRate = 40.0f;
		burstOnly(particles(make("Shockwave", root), "Shockwave Ring"), 200, 400);
	} else if (name == "Magic Aura") {
		t->loop = true;
		t->despawnWhenDone = false;
		t->duration = 1.5f;
		particles(make("Aura", root), "Magic");
		burstOnly(particles(make("Pulse Ring", root), "Shockwave Ring"), 80, 200);
	} else if (name == "Slash Trail") {
		t->loop = true;
		t->despawnWhenDone = false;
		t->duration = 0.6f;
		GameObject* rootObj = FindGameObjectById(root, false);
		if (rootObj && AddComponentByName(*rootObj, kTrailComponent)) {
			if (auto* trail = FindGameObjectById(root, false)->components.Get<TrailComponent>(kTrailComponent)) {
				trail->lifetime = 0.35f;
				trail->widthStart = 0.4f;
				trail->widthEnd = 0.0f;
				trail->colorStart = { 0.5f, 0.9f, 1.0f, 1.0f };
				trail->colorEnd = { 0.2f, 0.4f, 1.0f, 0.0f };
				trail->intensity = 3.0f;
			}
		}
		if (GpuParticlesComponent* sparks = particles(make("Sparks", root), "Sparks")) sparks->emitRate = 60.0f;
	}

	RebuildHierarchyLinks();
	UpdateWorldMatrices();
	selectedObjectIndex_ = FindGameObjectIndexById(root);
	extraSelection_.clear();
	isSceneDirty_ = true;
	PushUndoSnapshot();
	return root;
}

void EditorSuite::DrawEffectEditorWindow()
{
#ifdef USE_IMGUI
	ImGui::SetNextWindowSize(ImVec2(460.0f, 560.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TrId("Effect Editor"), &showEffectEditor_)) {
		ImGui::End();
		return;
	}

	// New effect from a template (at the Scene camera's focus point would need a pick: the origin is predictable)
	if (ImGui::BeginCombo(TrId("New Effect"), Tr("Choose..."))) {
		for (const char* name : kEffectTemplates) {
			if (ImGui::Selectable(Tr(name))) {
				if (const uint32_t id = CreateEffectFromTemplate(name, { 0.0f, 1.0f, 0.0f }); id != 0) StartEffectPreview(id);
			}
		}
		ImGui::EndCombo();
	}
	// After the combo: a new effect is the selection now
	const GameObject* selected = (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) ? &gameObjects_[selectedObjectIndex_] : nullptr;
	const uint32_t rootId = selected ? EffectRootOf(selected->id) : 0;
	if (rootId == 0) {
		ImGui::TextWrapped("%s", Tr("Select an effect (an object with Effect Timing, or one of its layers), or create one from a template."));
		if (selected && ImGui::Button(TrId("Make Selected an Effect"))) {
			if (GameObject* obj = FindGameObjectById(selected->id, false)) AddComponentByName(*obj, kEffectTimingComponent);
		}
		if (effectPreviewRoot_ != 0) StopEffectPreview();
		ImGui::End();
		return;
	}
	GameObject* root = FindGameObjectById(rootId, false);
	auto* rootTiming = root ? root->components.Get<EffectTimingComponent>(kEffectTimingComponent) : nullptr;
	if (!rootTiming) {
		ImGui::End();
		return;
	}
	if (effectPreviewRoot_ != 0 && effectPreviewRoot_ != rootId) StopEffectPreview(); // another effect got selected
	const bool inEdit = sceneState_ == EditorSceneState::Edit;
	ImGui::TextUnformatted(std::format("{}: {}", Tr("Effect"), root->name).c_str());

	// Transport
	ImGui::BeginDisabled(!inEdit);
	const bool previewing = effectPreviewRoot_ == rootId;
	if (ImGui::Button(TrId(previewing && effectPreviewPlaying_ ? "Pause##FxPlay" : "Play##FxPlay"))) {
		if (!previewing) StartEffectPreview(rootId);
		else effectPreviewPlaying_ = !effectPreviewPlaying_;
	}
	ImGui::SameLine();
	if (ImGui::Button(TrId("Restart"))) StartEffectPreview(rootId);
	ImGui::SetItemTooltip("%s", Tr("From 0 s again: bursts and the impact pulse fire again"));
	ImGui::SameLine();
	if (ImGui::Button(TrId("Stop"))) StopEffectPreview();
	ImGui::EndDisabled();
	if (!inEdit) ImGui::TextDisabled("%s", Tr("The preview runs in Edit mode. In Play the effect runs on game time."));

	// Preview panel: only this effect, on a camera that orbits it
	if (EffectPanelPreviewActive()) {
		const float side = (std::min)(ImGui::GetContentRegionAvail().x, 260.0f);
		// A button under the picture owns the mouse: dragging turns the camera instead of moving the window
		const ImVec2 at = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##EffectPreviewPanel", ImVec2(side, side));
		ImGui::GetWindowDrawList()->AddImage(ImTextureRef((ImTextureID)modelPreviewRT_.GetSrvGPUHandle().ptr), at, ImVec2(at.x + side, at.y + side));
		if (ImGui::IsItemActive()) {
			const ImVec2 delta = ImGui::GetIO().MouseDelta;
			effectPanelYaw_ += delta.x * 0.01f;
			effectPanelPitch_ = std::clamp(effectPanelPitch_ + delta.y * 0.01f, -1.4f, 1.4f);
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); // the wheel zooms instead of scrolling the window
			const float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f) effectPanelDistance_ = std::clamp(effectPanelDistance_ * std::pow(0.85f, wheel), 0.5f, 200.0f);
		}
		ImGui::SetItemTooltip("%s", Tr("Only this effect. Drag to turn around it, mouse wheel to move closer."));
	} else if (inEdit && effectPreviewRoot_ == rootId && previewModel_ && previewObject3d_) {
		ImGui::TextDisabled("%s", Tr("Preview is paused while a 3D model preview is open in the Project panel."));
	} else if (inEdit && effectPreviewRoot_ == rootId && MaterialPreviewActive()) {
		ImGui::TextDisabled("%s", Tr("The Material Graph shows its preview now. Close it to see the effect here."));
	} else if (inEdit && effectPreviewRoot_ != rootId) {
		ImGui::TextDisabled("%s", Tr("Press Play to see the effect here."));
	}
	ImGui::SliderFloat(TrId("Speed"), &effectPreviewSpeed_, 0.1f, 2.0f, "%.2fx");
	bool changed = ImGui::Checkbox(TrId("Loop"), &rootTiming->loop);

	// A layer picked in the Hierarchy (or the Scene view) becomes the edited layer
	if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) {
		const uint32_t picked = gameObjects_[selectedObjectIndex_].id;
		if (picked != effectEditorLayer_ && EffectRootOf(picked) == rootId) effectEditorLayer_ = picked;
	}

	// Layers on a time line
	struct Lane { uint32_t id; std::string name; float start, end, tail; bool emitting; };
	std::vector<Lane> lanes;
	float timeline = 0.0f;
	for (const EffectLayer& layer : CollectEffectLayers(*this, rootId)) {
		const GameObject* obj = FindGameObjectById(layer.id, false);
		const float duration = layer.window.duration > 0.0f ? layer.window.duration : 5.0f; // open-ended layers draw 5 s
		const int gate = layer.particles ? layer.particles->effectGate : (layer.trail ? layer.trail->effectGate : kEffectGateNone);
		const float start = layer.window.startDelay;
		lanes.push_back({ layer.id, obj ? obj->name : std::string(), start, start + duration, layer.window.tail, gate == kEffectGateOn });
		timeline = (std::max)(timeline, start + duration + layer.window.tail);
	}
	timeline = (std::max)(timeline, 0.1f);
	ImGui::Spacing();
	ImGui::TextDisabled("%s", std::format("{}  {:.2f} / {:.2f} s", Tr("Layers"), rootTiming->time, timeline).c_str());
	const float laneHeight = 18.0f;
	const float nameWidth = 110.0f;
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const float width = (std::max)(ImGui::GetContentRegionAvail().x - nameWidth, 50.0f);
	for (const Lane& lane : lanes) {
		const ImVec2 at = ImGui::GetCursorScreenPos();
		const bool isSelected = effectEditorLayer_ == lane.id;
		ImGui::PushID(static_cast<int>(lane.id));
		if (ImGui::Selectable(lane.name.c_str(), isSelected, 0, ImVec2(nameWidth - 6.0f, laneHeight))) {
			effectEditorLayer_ = lane.id;
			selectedObjectIndex_ = FindGameObjectIndexById(lane.id);
			extraSelection_.clear();
		}
		ImGui::PopID();
		const float x0 = at.x + nameWidth;
		const auto x = [&](float seconds) { return x0 + width * std::clamp(seconds / timeline, 0.0f, 1.0f); };
		draw->AddRectFilled(ImVec2(x0, at.y + 2.0f), ImVec2(x0 + width, at.y + laneHeight - 2.0f), IM_COL32(40, 40, 46, 255));
		draw->AddRectFilled(ImVec2(x(lane.end), at.y + 6.0f), ImVec2(x(lane.end + lane.tail), at.y + laneHeight - 6.0f), IM_COL32(255, 150, 40, 70));
		draw->AddRectFilled(ImVec2(x(lane.start), at.y + 3.0f), ImVec2((std::max)(x(lane.end), x(lane.start) + 2.0f), at.y + laneHeight - 3.0f),
			lane.emitting ? IM_COL32(255, 170, 60, 255) : IM_COL32(200, 120, 40, 170));
		const float cursor = x(rootTiming->time);
		draw->AddLine(ImVec2(cursor, at.y), ImVec2(cursor, at.y + laneHeight), IM_COL32(255, 255, 255, 220), 1.5f);
	}

	// Preview motion: moves the picture only, so trails can be judged without Play
	ImGui::Spacing();
	int motion = static_cast<int>(effectPreviewMotion_);
	float radius = effectPreviewRadius_;
	float cycles = effectPreviewCycles_;
	bool motionChanged = false;
	if (ImGui::BeginCombo(TrId("Preview Motion"), Tr(kEffectPreviewMotionNames[motion]))) {
		for (int i = 0; i < static_cast<int>(EffectPreviewMotion::Count); ++i) {
			if (ImGui::Selectable(Tr(kEffectPreviewMotionNames[i]), i == motion)) {
				motion = i;
				motionChanged = true;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("%s", Tr("Moves the effect in the Scene view while previewing. The saved position does not change."));
	motionChanged |= ImGui::DragFloat(TrId("Motion Radius"), &radius, 0.05f, 0.0f, 20.0f);
	motionChanged |= ImGui::DragFloat(TrId("Rounds per Second"), &cycles, 0.01f, 0.0f, 5.0f);
	if (motionChanged) SetEffectPreviewMotion(static_cast<EffectPreviewMotion>(motion), radius, cycles);

	// The chosen layer: its timing (the particle look is edited in the Inspector)
	GameObject* layer = FindGameObjectById(effectEditorLayer_, false);
	if (!layer || EffectRootOf(layer->id) != rootId) layer = root;
	ImGui::SeparatorText(layer->name.c_str());
	if (auto* own = layer->components.Get<EffectTimingComponent>(kEffectTimingComponent)) {
		changed |= ImGui::DragFloat(TrId("Start Delay"), &own->startDelay, 0.01f, 0.0f, 60.0f, "%.2f s");
		changed |= ImGui::DragFloat(TrId("Duration"), &own->duration, 0.01f, 0.0f, 600.0f, "%.2f s");
		changed |= ImGui::DragFloat(TrId("Pulse Bloom"), &own->pulseBloom, 0.05f, 0.0f, 10.0f);
		changed |= ImGui::SliderFloat(TrId("Pulse Blur"), &own->pulseBlur, 0.0f, 1.0f);
		changed |= ImGui::DragFloat(TrId("Pulse Shake"), &own->pulseShake, 0.01f, 0.0f, 2.0f);
		changed |= ImGui::DragFloat(TrId("Pulse Seconds"), &own->pulseSeconds, 0.01f, 0.01f, 5.0f);
	} else if (ImGui::Button(TrId("Own Timing"))) {
		AddComponentByName(*layer, kEffectTimingComponent); // `layer` stays valid: adding a component moves no object
	}
	if (layer) {
		if (auto* p = layer->components.Get<GpuParticlesComponent>(kGpuParticlesComponent)) {
			if (p->graph[0] != '\0') {
				ImGui::TextWrapped("%s", Tr("This layer uses a particle graph: its size and colour come from the graph. Its parameters are in the Inspector."));
				if (ImGui::Button(TrId("Edit Graph"))) OpenMaterialGraph(p->graph);
			} else {
				ImGui::TextWrapped("%s", Tr("Emission, size and colour of this layer are in the Inspector (GPU Particles)."));
			}
		} else if (layer->components.Has(kTrailComponent)) {
			ImGui::TextDisabled("%s", Tr("Trail layer: edit it in the Inspector. Pick a Preview Motion to see it in Edit mode."));
		}
	}

	ImGui::Separator();
	if (ImGui::Button(TrId("Save as Prefab"))) {
		if (SavePrefab(rootId, root->name)) LogMessage("[Effect] Saved prefab " + root->name, 0);
	}
	ImGui::SetItemTooltip("%s", Tr("Scripts spawn it with InstantiatePrefab; every spawn plays the effect from the start."));
	if (changed) {
		isSceneDirty_ = true;
		PushUndoSnapshot();
	}
	ImGui::End();
#endif
}
