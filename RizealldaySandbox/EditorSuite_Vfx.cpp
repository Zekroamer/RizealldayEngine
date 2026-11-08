#include "EditorSuiteInternal.h"
#include "render/GpuParticles.h"
#include "render/DecalRenderer.h"
#include "base/TextureManager.h"
#include <chrono>
#include <map>

// Visual effect components (Fase 7). The effects themselves live in RizealldayEngine/render.

namespace {
	/// White with a round alpha falloff: what particles and decals use without a texture of their own
	uint32_t SoftCircleTexture()
	{
		static uint32_t handle = 0; // asked for every frame: build the pixels once
		if (handle != 0) return handle;
		constexpr uint32_t kSize = 64;
		std::vector<uint8_t> rgba(kSize * kSize * 4, 255);
		for (uint32_t y = 0; y < kSize; ++y) {
			for (uint32_t x = 0; x < kSize; ++x) {
				const float dx = (x + 0.5f) / kSize * 2.0f - 1.0f;
				const float dy = (y + 0.5f) / kSize * 2.0f - 1.0f;
				const float t = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
				rgba[(static_cast<size_t>(y) * kSize + x) * 4 + 3] = static_cast<uint8_t>(t * t * (3.0f - 2.0f * t) * 255.0f);
			}
		}
		handle = TextureManager::GetInstance()->CreateTextureFromRGBA("__SoftCircle", rgba.data(), kSize, kSize);
		return handle;
	}

	TrailStyle StyleOf(const TrailComponent& trail)
	{
		TrailStyle style;
		style.lifetime = trail.lifetime;
		style.minDistance = trail.minDistance;
		style.widthStart = trail.widthStart;
		style.widthEnd = trail.widthEnd;
		const auto bright = [&](const Vector4& c) { return Vector4{ c.x * trail.intensity, c.y * trail.intensity, c.z * trail.intensity, c.w }; };
		style.colorStart = bright(trail.colorStart);
		style.colorEnd = bright(trail.colorEnd);
		style.softEdges = trail.texture[0] == '\0';
		return style;
	}
}

void EditorSuite::RegisterVfxComponents()
{
	ComponentInfo& trail = RegisterDataComponent<TrailComponent>(kTrailComponent, "Rendering", {
		RIZE_FIELD(TrailComponent, lifetime, ComponentFieldType::Float, 0.01f, 30.0f),
		RIZE_FIELD(TrailComponent, minDistance, ComponentFieldType::Float, 0.001f, 10.0f),
		RIZE_FIELD(TrailComponent, widthStart, ComponentFieldType::Float, 0.0f, 100.0f),
		RIZE_FIELD(TrailComponent, widthEnd, ComponentFieldType::Float, 0.0f, 100.0f),
		RIZE_FIELD(TrailComponent, colorStart, ComponentFieldType::Color, 0.0f, 0.0f),
		RIZE_FIELD(TrailComponent, colorEnd, ComponentFieldType::Color, 0.0f, 0.0f),
		RIZE_FIELD(TrailComponent, intensity, ComponentFieldType::Float, 0.0f, 100.0f),
		RIZE_FIELD(TrailComponent, additive, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(TrailComponent, emitting, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(TrailComponent, texture, ComponentFieldType::Asset, 0.0f, 0.0f),
		RIZE_FIELD(TrailComponent, material, ComponentFieldType::Asset, 0.0f, 0.0f) });
#ifdef USE_IMGUI
	trail.drawExtra = [](EditorSuite& suite, GameObject& obj, ComponentData& data) {
		auto* t = static_cast<TrailComponent*>(data.Data());
		suite.DrawMaterialRow(obj, t->material, sizeof(t->material));
	};
#else
	(void)trail;
#endif

	RegisterDataComponent<DecalComponent>(kDecalComponent, "Rendering", {
		RIZE_FIELD(DecalComponent, texture, ComponentFieldType::Asset, 0.0f, 0.0f),
		RIZE_FIELD(DecalComponent, color, ComponentFieldType::Color, 0.0f, 0.0f),
		RIZE_FIELD(DecalComponent, intensity, ComponentFieldType::Float, 0.0f, 100.0f),
		RIZE_FIELD(DecalComponent, edgeFade, ComponentFieldType::Float, 0.0f, 1.0f),
		RIZE_FIELD(DecalComponent, additive, ComponentFieldType::Bool, 0.0f, 0.0f) });

	ComponentInfo& particles = RegisterDataComponent<GpuParticlesComponent>(kGpuParticlesComponent, "Rendering", {
		RIZE_FIELD(GpuParticlesComponent, capacity, ComponentFieldType::Int, 1.0f, 1000000.0f),
		RIZE_FIELD(GpuParticlesComponent, emitRate, ComponentFieldType::Float, 0.0f, 100000.0f),
		RIZE_FIELD(GpuParticlesComponent, burst, ComponentFieldType::Int, 0.0f, 1000000.0f),
		RIZE_FIELD(GpuParticlesComponent, emitting, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, lifeMin, ComponentFieldType::Float, 0.001f, 60.0f),
		RIZE_FIELD(GpuParticlesComponent, lifeMax, ComponentFieldType::Float, 0.001f, 60.0f),
		RIZE_FIELD(GpuParticlesComponent, speedMin, ComponentFieldType::Float, 0.0f, 1000.0f),
		RIZE_FIELD(GpuParticlesComponent, speedMax, ComponentFieldType::Float, 0.0f, 1000.0f),
		RIZE_FIELD(GpuParticlesComponent, spreadDegrees, ComponentFieldType::Float, 0.0f, 180.0f),
		RIZE_FIELD(GpuParticlesComponent, direction, ComponentFieldType::Vector3, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, gravity, ComponentFieldType::Vector3, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, drag, ComponentFieldType::Float, 0.0f, 100.0f),
		RIZE_FIELD(GpuParticlesComponent, sizeStart, ComponentFieldType::Float, 0.0f, 1000.0f),
		RIZE_FIELD(GpuParticlesComponent, sizeEnd, ComponentFieldType::Float, 0.0f, 1000.0f),
		RIZE_FIELD(GpuParticlesComponent, colorStart, ComponentFieldType::Color, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, colorEnd, ComponentFieldType::Color, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, useMiddleKey, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, middleTime, ComponentFieldType::Float, 0.001f, 0.999f),
		RIZE_FIELD(GpuParticlesComponent, sizeMiddle, ComponentFieldType::Float, 0.0f, 1000.0f),
		RIZE_FIELD(GpuParticlesComponent, colorMiddle, ComponentFieldType::Color, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, collideWithFloor, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, floorHeight, ComponentFieldType::Float, -100000.0f, 100000.0f),
		RIZE_FIELD(GpuParticlesComponent, bounce, ComponentFieldType::Float, 0.0f, 1.0f),
		RIZE_FIELD(GpuParticlesComponent, intensity, ComponentFieldType::Float, 0.0f, 100.0f),
		RIZE_FIELD(GpuParticlesComponent, additive, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, texture, ComponentFieldType::Asset, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, shape, ComponentFieldType::Int, 0.0f, 4.0f),
		RIZE_FIELD(GpuParticlesComponent, velocityMode, ComponentFieldType::Int, 0.0f, 1.0f),
		RIZE_FIELD(GpuParticlesComponent, shapeRadius, ComponentFieldType::Float, 0.0f, 1000.0f),
		RIZE_FIELD(GpuParticlesComponent, boxExtents, ComponentFieldType::Vector3, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, stretchFactor, ComponentFieldType::Float, 0.0f, 10.0f),
		RIZE_FIELD(GpuParticlesComponent, uprightBillboard, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, spawnBurst, ComponentFieldType::Int, 0.0f, 1000000.0f),
		RIZE_FIELD(GpuParticlesComponent, material, ComponentFieldType::Asset, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, graph, ComponentFieldType::Asset, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, parameters, ComponentFieldType::Text, 0.0f, 0.0f) });
	for (ComponentField& field : particles.fields) {
		const std::string_view name = field.name;
		if (name == "shape") field.tooltip = "0 Point, 1 Sphere, 2 Box, 3 Cone (base disc), 4 Ring. Turns and scales with the object.";
		else if (name == "velocityMode") field.tooltip = "0 Direction: a cone around Direction. 1 Radial: away from the centre of the shape.";
		else if (name == "stretchFactor") field.tooltip = "0 = round. Above 0 particles stretch along their velocity (sparks, rain).";
		else if (name == "uprightBillboard") field.tooltip = "Quads stand on the world Y axis and only turn around it (flames, grass).";
		else if (name == "material") field.tooltip = "A material graph (.vfxmat.json) that decides the colour of every pixel. Empty = Texture x Color.";
		else if (name == "graph") field.tooltip = "A particle graph (.vfxparticle.json) that decides how particles start and move.\nWhile it is set, the settings the graph replaces are hidden.";
		else if (name == "intensity") field.tooltip = "Above 1 the particles glow through the bloom";
		else if (name == "parameters") field.hidden = true; // drawn as one row per parameter below
		else if (name == "spawnBurst") field.tooltip = "Prefab instances fire this many particles when spawned or reused.\nScripts: EmitParticles() / SetParticlesEmitting().";
	}
	particles.fieldShown = [](const void* data, const ComponentField& field) {
		const auto* p = static_cast<const GpuParticlesComponent*>(data);
		const std::string_view name = field.name;
		const auto any = [&](std::initializer_list<std::string_view> names) { return std::find(names.begin(), names.end(), name) != names.end(); };
		// Size and colour keys are edited as a curve and a gradient below the fields
		if (any({ "sizeStart", "sizeMiddle", "sizeEnd", "colorStart", "colorMiddle", "colorEnd", "middleTime", "useMiddleKey" })) return false;
		if (p->graph[0] == '\0') return true;
		// A graph decides start, motion, size and colour: only emission and the look stay
		return any({ "capacity", "emitRate", "burst", "emitting", "spawnBurst", "additive", "texture", "material", "stretchFactor",
			"uprightBillboard", "graph", "direction" });
	};
#ifdef USE_IMGUI
	particles.drawExtra = [](EditorSuite& suite, GameObject& obj, ComponentData& data) {
		auto* p = static_cast<GpuParticlesComponent*>(data.Data());
		if (p->graph[0] == '\0') {
			if (suite.DrawParticleLifetimeCurves(*p)) {
				suite.isSceneDirty_ = true;
				suite.PushUndoSnapshot(); // edits of the same object within 0.6 s merge into one step
			}
		}
		suite.DrawMaterialRow(obj, p->material, sizeof(p->material));
		suite.DrawParticleGraphRows(obj, *p);
		if (ImGui::BeginCombo(TrId("Preset"), Tr("Choose..."))) {
			for (const char* preset : kGpuParticlePresets) {
				if (ImGui::Selectable(Tr(preset))) {
					ApplyGpuParticlePreset(*p, preset);
					suite.isSceneDirty_ = true;
					suite.PushUndoSnapshot();
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::Button(TrId("Burst 100"))) p->burst += 100; // preview only: consumed on the next frame
	};
#else
	(void)particles;
#endif
}

bool EditorSuite::DrawParticleLifetimeCurves(GpuParticlesComponent& p)
{
#ifdef USE_IMGUI
	bool changed = false;
	ImDrawList* draw = ImGui::GetWindowDrawList();
	// Size over lifetime: drag the keys up and down (the middle one also left and right)
	ImGui::TextDisabled("%s", Tr("Size over Lifetime"));
	const ImVec2 box = ImGui::GetCursorScreenPos();
	const ImVec2 size(ImGui::GetContentRegionAvail().x, 90.0f);
	const float top = (std::max)({ p.sizeStart, p.sizeEnd, p.useMiddleKey ? p.sizeMiddle : 0.0f, 0.1f }) * 1.25f;
	draw->AddRectFilled(box, ImVec2(box.x + size.x, box.y + size.y), IM_COL32(30, 30, 36, 255));
	const auto point = [&](float t, float value) { return ImVec2(box.x + size.x * t, box.y + size.y * (1.0f - value / top)); };
	const float middleTime = std::clamp(p.middleTime, 0.01f, 0.99f);
	if (p.useMiddleKey) {
		draw->AddLine(point(0.0f, p.sizeStart), point(middleTime, p.sizeMiddle), IM_COL32(255, 170, 60, 255), 2.0f);
		draw->AddLine(point(middleTime, p.sizeMiddle), point(1.0f, p.sizeEnd), IM_COL32(255, 170, 60, 255), 2.0f);
	} else {
		draw->AddLine(point(0.0f, p.sizeStart), point(1.0f, p.sizeEnd), IM_COL32(255, 170, 60, 255), 2.0f);
	}
	const auto key = [&](const char* id, float t, float& value, float* time) {
		const ImVec2 at = point(t, value);
		ImGui::SetCursorScreenPos(ImVec2(at.x - 6.0f, at.y - 6.0f));
		ImGui::InvisibleButton(id, ImVec2(12.0f, 12.0f));
		const bool active = ImGui::IsItemActive();
		draw->AddCircleFilled(at, 5.0f, active || ImGui::IsItemHovered() ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 200, 120, 255));
		if (active) {
			const ImVec2 delta = ImGui::GetIO().MouseDelta;
			value = (std::max)(0.0f, value - delta.y / size.y * top);
			if (time) *time = std::clamp(*time + delta.x / size.x, 0.01f, 0.99f);
			return true;
		}
		return false;
	};
	changed |= key("##SizeStart", 0.0f, p.sizeStart, nullptr);
	changed |= key("##SizeEnd", 1.0f, p.sizeEnd, nullptr);
	if (p.useMiddleKey) changed |= key("##SizeMiddle", middleTime, p.sizeMiddle, &p.middleTime);
	ImGui::SetCursorScreenPos(ImVec2(box.x, box.y + size.y + 4.0f));
	changed |= ImGui::Checkbox(TrId("Use Middle Key"), &p.useMiddleKey);

	// Colour over lifetime: the gradient the particles go through, alpha drawn as darkness
	ImGui::TextDisabled("%s", Tr("Color over Lifetime"));
	const ImVec2 bar = ImGui::GetCursorScreenPos();
	const float barWidth = ImGui::GetContentRegionAvail().x;
	const auto color = [](const Vector4& c) {
		const auto u8 = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
		return IM_COL32(u8(c.x * c.w), u8(c.y * c.w), u8(c.z * c.w), 255);
	};
	const float split = p.useMiddleKey ? middleTime : 0.5f;
	const ImU32 mid = color(p.useMiddleKey ? p.colorMiddle : Vector4{ (p.colorStart.x + p.colorEnd.x) * 0.5f, (p.colorStart.y + p.colorEnd.y) * 0.5f,
		(p.colorStart.z + p.colorEnd.z) * 0.5f, (p.colorStart.w + p.colorEnd.w) * 0.5f });
	draw->AddRectFilledMultiColor(bar, ImVec2(bar.x + barWidth * split, bar.y + 18.0f), color(p.colorStart), mid, mid, color(p.colorStart));
	draw->AddRectFilledMultiColor(ImVec2(bar.x + barWidth * split, bar.y), ImVec2(bar.x + barWidth, bar.y + 18.0f), mid, color(p.colorEnd), color(p.colorEnd), mid);
	ImGui::Dummy(ImVec2(barWidth, 20.0f));
	changed |= ImGui::ColorEdit4(TrId("Start##FxColor"), &p.colorStart.x, ImGuiColorEditFlags_NoInputs);
	ImGui::SameLine();
	if (p.useMiddleKey) {
		changed |= ImGui::ColorEdit4(TrId("Middle##FxColor"), &p.colorMiddle.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
	}
	changed |= ImGui::ColorEdit4(TrId("End##FxColor"), &p.colorEnd.x, ImGuiColorEditFlags_NoInputs);
	return changed;
#else
	(void)p;
	return false;
#endif
}

void EditorSuite::UpdateGpuParticles(ID3D12GraphicsCommandList* cmdList)
{
	const bool inPlay = sceneState_ != EditorSceneState::Edit || IsStandaloneMode();
	const double wallClock = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	const double now = inPlay ? gameTime_ : wallClock;
	GpuParticles::GetInstance()->SetTime(static_cast<float>(std::fmod(now, 3600.0))); // materials that move (an hour keeps float precision)
	TrailRenderer::GetInstance()->SetTime(static_cast<float>(std::fmod(now, 3600.0)));
	for (GameObject& obj : gameObjects_) {
		auto* p = obj.isActive ? obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent) : nullptr;
		if (!p) continue;
		// Going back in time (a new Play) or a long gap (loading) must not produce a jump
		float deltaTime = (p->lastTime < 0.0) ? 0.0f : std::clamp(static_cast<float>(now - p->lastTime), 0.0f, 0.1f);
		p->lastTime = now;
		// In Edit an effect plays only in the Effect Editor's preview (slower, faster or paused); the others stay quiet
		const uint32_t effectRoot = inPlay ? 0 : EffectRootOf(obj.id);
		if (effectRoot != 0 && effectRoot == effectPreviewRoot_) {
			deltaTime *= effectPreviewPlaying_ ? std::clamp(effectPreviewSpeed_, 0.05f, 4.0f) : 0.0f;
		}
		uint32_t emit = static_cast<uint32_t>((std::max)(p->burst, 0));
		p->burst = 0;
		bool emitting = EffectiveEmitting(p->effectGate, p->emitting);
		if (effectRoot != 0 && effectRoot != effectPreviewRoot_) emitting = false;
		if (emitting) emit += GpuParticleEmitCount(p->emitRate, deltaTime, p->emitCarry);

		GpuParticleSettings settings;
		settings.capacity = static_cast<uint32_t>((std::max)(p->capacity, 1));
		settings.lifeMin = p->lifeMin;
		settings.lifeMax = p->lifeMax;
		settings.speedMin = p->speedMin;
		settings.speedMax = p->speedMax;
		settings.spreadDegrees = p->spreadDegrees;
		settings.direction = TransformUtil::TransformDirection(p->direction, obj.worldMatrix); // turns with the object
		settings.gravity = p->gravity;
		settings.drag = p->drag;
		settings.sizeStart = p->sizeStart;
		settings.sizeEnd = p->sizeEnd;
		const auto bright = [&](const Vector4& c) { return Vector4{ c.x * p->intensity, c.y * p->intensity, c.z * p->intensity, c.w }; };
		settings.colorStart = bright(p->colorStart);
		settings.colorEnd = bright(p->colorEnd);
		settings.useMiddleKey = p->useMiddleKey;
		settings.middleTime = p->middleTime;
		settings.sizeMiddle = p->sizeMiddle;
		settings.colorMiddle = bright(p->colorMiddle);
		settings.collideWithFloor = p->collideWithFloor;
		settings.floorHeight = p->floorHeight;
		settings.bounce = p->bounce;
		settings.additive = p->additive;
		ApplyGpuParticleShape(*p, obj.worldMatrix, settings);
		settings.textureHandle = p->texture[0] != '\0' ? TextureManager::GetInstance()->LoadTexture(ResolveAssetPath(p->texture)) : SoftCircleTexture();
		if (p->material[0] != '\0') {
			if (const EffectMaterialEntry* material = GetEffectMaterial(p->material); material && material->ok) {
				settings.material = material->key;
				for (size_t slot = 0; slot < material->textureHandles.size() && slot < GpuParticles::kMaterialTextures; ++slot) {
					settings.materialTextures[slot] = material->textureHandles[slot];
				}
			}
		}
		if (p->graph[0] != '\0') {
			if (const EffectMaterialEntry* graph = GetParticleGraph(p->graph); graph && graph->ok) {
				settings.graph = graph->key;
				// The graph's default, then this object's value, then what a script set
				const std::map<std::string, Vector4> values = ParseEffectParameterValues(p->parameters);
				const auto overrides = effectParameterOverrides_.find(obj.id);
				for (size_t i = 0; i < graph->parameters.size() && i < GpuParticles::kGraphParameters; ++i) {
					const std::string& name = graph->parameters[i].name;
					Vector4 value = graph->parameters[i].value;
					if (const auto it = values.find(name); it != values.end()) value = it->second;
					if (overrides != effectParameterOverrides_.end()) {
						if (const auto it = overrides->second.find(name); it != overrides->second.end()) value = it->second;
					}
					settings.graphParams[i] = value;
				}
			}
		}

		const Vector3 origin{ obj.worldMatrix.m[3][0], obj.worldMatrix.m[3][1], obj.worldMatrix.m[3][2] };
		GpuParticles::GetInstance()->Simulate(cmdList, obj.id, origin, deltaTime, emit, settings);
	}
	if (MaterialPreviewActive()) SimulateMaterialPreview(cmdList, wallClock);
	GpuParticles::GetInstance()->EndFrame();
}

namespace {
	// Far below the scene: the preview emitter is never drawn in the views, but it should not even be near them
	const Vector3 kMaterialPreviewOrigin{ 0.0f, -10000.0f, 0.0f };
}

bool EditorSuite::MaterialPreviewActive() const
{
#ifdef USE_IMGUI
	// The asset preview of a 3D model uses the same target: that one wins while it is open
	return showMaterialGraph_ && !materialEditorPath_.empty() && !(previewModel_ && previewObject3d_) && modelPreviewRT_.IsInitialized();
#else
	return false;
#endif
}

void EditorSuite::SimulateMaterialPreview(ID3D12GraphicsCommandList* cmdList, double wallClock)
{
	GpuParticleSettings settings;
	settings.capacity = 160;
	settings.lifeMin = 1.2f;
	settings.lifeMax = 2.0f;
	settings.speedMin = 0.2f;
	settings.speedMax = 0.6f;
	settings.spreadDegrees = 180.0f;
	settings.sizeStart = 0.9f;
	settings.sizeEnd = 0.6f;
	settings.colorStart = { 1.0f, 1.0f, 1.0f, 1.0f };
	settings.colorEnd = { 1.0f, 1.0f, 1.0f, 0.0f };
	settings.additive = false;
	settings.shape = static_cast<uint32_t>(GpuParticleShape::Sphere);
	settings.shapeRadius = 0.6f;
	settings.textureHandle = SoftCircleTexture();
	uint64_t key = 0;
	if (materialEditor_.graph.kind == GraphKind::Particle) {
		// A particle graph decides everything itself; its parameters at their defaults
		if (const EffectMaterialEntry* graph = GetParticleGraph(materialEditorPath_); graph && graph->ok) {
			key = graph->key;
			settings.graph = key;
			for (size_t i = 0; i < graph->parameters.size() && i < GpuParticles::kGraphParameters; ++i) settings.graphParams[i] = graph->parameters[i].value;
		}
	} else if (const EffectMaterialEntry* material = GetEffectMaterial(materialEditorPath_); material && material->ok) {
		key = material->key;
		settings.material = key;
		for (size_t slot = 0; slot < material->textureHandles.size() && slot < GpuParticles::kMaterialTextures; ++slot) {
			settings.materialTextures[slot] = material->textureHandles[slot];
		}
	}
	// A new material starts a fresh cloud with a burst: a different capacity makes GpuParticles drop the old particles
	uint32_t emit = 0;
	if (!materialPreviewStarted_ || key != materialPreviewKey_) {
		materialPreviewKey_ = key;
		materialPreviewStarted_ = true;
		materialPreviewLast_ = wallClock;
		materialPreviewCapacity_ = materialPreviewCapacity_ == 160u ? 161u : 160u;
		emit = 60;
	}
	settings.capacity = materialPreviewCapacity_;
	const float dt = std::clamp(static_cast<float>(wallClock - materialPreviewLast_), 0.0f, 0.1f);
	materialPreviewLast_ = wallClock;
	emit += GpuParticleEmitCount(40.0f, dt, materialPreviewCarry_);
	GpuParticles::GetInstance()->Simulate(cmdList, GpuParticles::kPreviewEmitterId, kMaterialPreviewOrigin, dt, emit, settings);
}

void EditorSuite::UpdateTrails()
{
	const bool inPlay = sceneState_ != EditorSceneState::Edit || IsStandaloneMode();
	for (GameObject& obj : gameObjects_) {
		TrailComponent* trail = obj.components.Get<TrailComponent>(kTrailComponent);
		if (!trail) continue;
		// In Edit only the effect the Effect Editor previews draws its trails, on the preview clock
		const bool previewed = !inPlay && effectPreviewRoot_ != 0 && EffectRootOf(obj.id) == effectPreviewRoot_;
		if (!inPlay && !previewed) {
			trail->ribbon.Clear();
			continue;
		}
		const Vector3 head{ obj.worldMatrix.m[3][0], obj.worldMatrix.m[3][1], obj.worldMatrix.m[3][2] };
		const bool emitting = EffectiveEmitting(trail->effectGate, trail->emitting);
		trail->ribbon.Update(head, previewed ? effectPreviewClock_ : gameTime_, StyleOf(*trail), emitting && obj.isActive);
	}
}

void EditorSuite::DrawDecals(ID3D12GraphicsCommandList* cmdList, const Camera& camera, RenderTexture& target)
{
	std::vector<DecalDraw> decals;
	for (GameObject& obj : gameObjects_) {
		const DecalComponent* decal = obj.isActive ? obj.components.Get<DecalComponent>(kDecalComponent) : nullptr;
		if (!decal) continue;
		DecalDraw draw;
		draw.world = obj.worldMatrix;
		draw.color = { decal->color.x * decal->intensity, decal->color.y * decal->intensity, decal->color.z * decal->intensity, decal->color.w };
		draw.edgeFade = decal->edgeFade;
		draw.additive = decal->additive;
		draw.textureHandle = decal->texture[0] != '\0' ? TextureManager::GetInstance()->LoadTexture(ResolveAssetPath(decal->texture)) : SoftCircleTexture();
		decals.push_back(draw);
	}
	if (!decals.empty() && target.IsDepthReadable()) {
		target.BeginColorOnlyWithDepthRead(cmdList);
		DecalRenderer::GetInstance()->Draw(cmdList, camera, target.GetDepthSrvGPUHandle(), target.GetWidth(), target.GetHeight(), decals);
		target.EndColorOnlyWithDepthRead(cmdList);
	}
	// Not in the depth the decals read: drawn over the marks like anything else in front of the floor
	for (Object3d* held : decalExcluded_) held->Draw(cmdList);
	decalExcluded_.clear();
}

bool EditorSuite::EffectPanelPreviewActive() const
{
#ifdef USE_IMGUI
	return showEffectEditor_ && effectPreviewRoot_ != 0 && sceneState_ == EditorSceneState::Edit && !MaterialPreviewActive() &&
		!(previewModel_ && previewObject3d_) && modelPreviewRT_.IsInitialized();
#else
	return false;
#endif
}

void EditorSuite::RenderPreviewPanel(ID3D12GraphicsCommandList* cmdList)
{
#ifdef USE_IMGUI
	// Graph windows first: their preview emitter on a fixed camera. Else the Effect Editor's effect on its orbit camera.
	const bool graph = MaterialPreviewActive();
	const GameObject* root = graph ? nullptr : FindGameObjectById(effectPreviewRoot_, false);
	if (!graph && !root) return;
	Camera& camera = graph ? materialPreviewCamera_ : effectPanelCamera_;
	camera.aspectRatio = 1.0f;
	camera.nearClip = 0.05f;
	camera.orthographic = false;
	if (graph) {
		camera.fovY = 0.6f;
		camera.farClip = 100.0f;
		camera.translation = { kMaterialPreviewOrigin.x, kMaterialPreviewOrigin.y, kMaterialPreviewOrigin.z - 4.0f };
		camera.rotation = { 0.0f, 0.0f, 0.0f };
	} else {
		// Orbit around the root (its preview motion included): the panel keeps the effect in view wherever it is
		const Vector3 target{ root->worldMatrix.m[3][0], root->worldMatrix.m[3][1], root->worldMatrix.m[3][2] };
		const float pitch = std::clamp(effectPanelPitch_, -1.4f, 1.4f);
		const float distance = std::clamp(effectPanelDistance_, 0.5f, 200.0f);
		camera.fovY = 0.7f;
		camera.farClip = 1000.0f;
		// Rotation (pitch, yaw) looks along +Z turned by yaw and tilted down by pitch: step back along that line
		const Vector3 forward{ std::sin(effectPanelYaw_) * std::cos(pitch), -std::sin(pitch), std::cos(effectPanelYaw_) * std::cos(pitch) };
		camera.translation = { target.x - forward.x * distance, target.y - forward.y * distance, target.z - forward.z * distance };
		camera.rotation = { pitch, effectPanelYaw_, 0.0f };
	}
	camera.Update();
	previewSceneRT_.BeginRender();
	if (graph) GpuParticles::GetInstance()->DrawEmitter(cmdList, camera, GpuParticles::kPreviewEmitterId);
	else DrawEffects(cmdList, camera, previewSceneRT_, effectPreviewRoot_);
	previewSceneRT_.EndRender();
	modelPreviewRT_.BeginRender(false);
	postProcess_.Resolve(cmdList, previewSceneRT_);
	modelPreviewRT_.EndRender();
#else
	(void)cmdList;
#endif
}

void EditorSuite::DrawEffects(ID3D12GraphicsCommandList* cmdList, const Camera& camera, RenderTexture& target, uint32_t onlyEffectRoot)
{
	(void)target;

	// One draw per blend mode, texture and material; blended ones first
	struct TrailBatch { std::vector<TrailVertex> vertices; std::vector<uint32_t> textures; };
	std::map<std::tuple<bool, uint32_t, uint64_t>, TrailBatch> trails;
	// Ribbons store the clock they were made on: the Effect Editor's preview has its own in Edit
	const bool editPreview = sceneState_ == EditorSceneState::Edit && !IsStandaloneMode() && effectPreviewRoot_ != 0;
	const double now = editPreview ? effectPreviewClock_ : gameTime_;
	std::vector<uint64_t> effectEmitters;
	for (GameObject& obj : gameObjects_) {
		if (onlyEffectRoot != 0) {
			if (EffectRootOf(obj.id) != onlyEffectRoot) continue;
			if (obj.components.Has(kGpuParticlesComponent)) effectEmitters.push_back(obj.id);
		}
		const TrailComponent* trail = obj.components.Get<TrailComponent>(kTrailComponent);
		if (!trail || trail->ribbon.PointCount() < 2) continue;
		const uint32_t texture = trail->texture[0] != '\0' ? TextureManager::GetInstance()->LoadTexture(ResolveAssetPath(trail->texture)) : 0u;
		uint64_t material = 0;
		std::vector<uint32_t> materialTextures;
		if (trail->material[0] != '\0') {
			const EffectMaterialEntry* entry = GetEffectMaterial(trail->material);
			if (entry && entry->ok && TrailRenderer::GetInstance()->BuildMaterial(entry->key, entry->body, nullptr)) {
				material = entry->key;
				materialTextures = entry->textureHandles;
			}
		}
		TrailBatch& batch = trails[{ trail->additive, texture, material }];
		batch.textures = materialTextures;
		trail->ribbon.BuildMesh(camera.translation, now, StyleOf(*trail), batch.vertices);
	}
	for (auto& [key, batch] : trails) {
		batch.textures.resize(TrailRenderer::kMaterialTextures, 0u);
		TrailRenderer::GetInstance()->Draw(cmdList, camera, batch.vertices, std::get<0>(key), std::get<1>(key), std::get<2>(key), batch.textures.data());
	}
	if (onlyEffectRoot != 0) GpuParticles::GetInstance()->DrawEmitters(cmdList, camera, effectEmitters);
	else GpuParticles::GetInstance()->Draw(cmdList, camera);
}
