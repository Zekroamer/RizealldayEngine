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
	RegisterDataComponent<TrailComponent>(kTrailComponent, "Rendering", {
		RIZE_FIELD(TrailComponent, lifetime, ComponentFieldType::Float, 0.01f, 30.0f),
		RIZE_FIELD(TrailComponent, minDistance, ComponentFieldType::Float, 0.001f, 10.0f),
		RIZE_FIELD(TrailComponent, widthStart, ComponentFieldType::Float, 0.0f, 100.0f),
		RIZE_FIELD(TrailComponent, widthEnd, ComponentFieldType::Float, 0.0f, 100.0f),
		RIZE_FIELD(TrailComponent, colorStart, ComponentFieldType::Color, 0.0f, 0.0f),
		RIZE_FIELD(TrailComponent, colorEnd, ComponentFieldType::Color, 0.0f, 0.0f),
		RIZE_FIELD(TrailComponent, intensity, ComponentFieldType::Float, 0.0f, 100.0f),
		RIZE_FIELD(TrailComponent, additive, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(TrailComponent, emitting, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(TrailComponent, texture, ComponentFieldType::Asset, 0.0f, 0.0f) });

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
		RIZE_FIELD(GpuParticlesComponent, spawnBurst, ComponentFieldType::Int, 0.0f, 1000000.0f) });
	for (ComponentField& field : particles.fields) {
		const std::string_view name = field.name;
		if (name == "shape") field.tooltip = "0 Point, 1 Sphere, 2 Box, 3 Cone (base disc), 4 Ring. Turns and scales with the object.";
		else if (name == "velocityMode") field.tooltip = "0 Direction: a cone around Direction. 1 Radial: away from the centre of the shape.";
		else if (name == "stretchFactor") field.tooltip = "0 = round. Above 0 particles stretch along their velocity (sparks, rain).";
		else if (name == "uprightBillboard") field.tooltip = "Quads stand on the world Y axis and only turn around it (flames, grass).";
		else if (name == "spawnBurst") field.tooltip = "Prefab instances fire this many particles when spawned or reused.\nScripts: EmitParticles() / SetParticlesEmitting().";
	}
#ifdef USE_IMGUI
	particles.drawExtra = [](EditorSuite& suite, GameObject&, ComponentData& data) {
		auto* p = static_cast<GpuParticlesComponent*>(data.Data());
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

void EditorSuite::UpdateGpuParticles(ID3D12GraphicsCommandList* cmdList)
{
	const bool inPlay = sceneState_ != EditorSceneState::Edit || IsStandaloneMode();
	const double wallClock = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	const double now = inPlay ? gameTime_ : wallClock;
	for (GameObject& obj : gameObjects_) {
		auto* p = obj.isActive ? obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent) : nullptr;
		if (!p) continue;
		// Going back in time (a new Play) or a long gap (loading) must not produce a jump
		const float deltaTime = (p->lastTime < 0.0) ? 0.0f : std::clamp(static_cast<float>(now - p->lastTime), 0.0f, 0.1f);
		p->lastTime = now;
		uint32_t emit = static_cast<uint32_t>((std::max)(p->burst, 0));
		p->burst = 0;
		if (p->emitting) emit += GpuParticleEmitCount(p->emitRate, deltaTime, p->emitCarry);

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

		const Vector3 origin{ obj.worldMatrix.m[3][0], obj.worldMatrix.m[3][1], obj.worldMatrix.m[3][2] };
		GpuParticles::GetInstance()->Simulate(cmdList, obj.id, origin, deltaTime, emit, settings);
	}
	GpuParticles::GetInstance()->EndFrame();
}

void EditorSuite::UpdateTrails()
{
	const bool inPlay = sceneState_ != EditorSceneState::Edit || IsStandaloneMode();
	for (GameObject& obj : gameObjects_) {
		TrailComponent* trail = obj.components.Get<TrailComponent>(kTrailComponent);
		if (!trail) continue;
		if (!inPlay) {
			trail->ribbon.Clear();
			continue;
		}
		const Vector3 head{ obj.worldMatrix.m[3][0], obj.worldMatrix.m[3][1], obj.worldMatrix.m[3][2] };
		trail->ribbon.Update(head, gameTime_, StyleOf(*trail), trail->emitting && obj.isActive);
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

void EditorSuite::DrawEffects(ID3D12GraphicsCommandList* cmdList, const Camera& camera, RenderTexture& target)
{
	(void)target;

	// One draw per texture and blend mode; blended ones first
	std::map<std::pair<bool, uint32_t>, std::vector<TrailVertex>> trails;
	for (GameObject& obj : gameObjects_) {
		const TrailComponent* trail = obj.components.Get<TrailComponent>(kTrailComponent);
		if (!trail || trail->ribbon.PointCount() < 2) continue;
		const uint32_t texture = trail->texture[0] != '\0' ? TextureManager::GetInstance()->LoadTexture(ResolveAssetPath(trail->texture)) : 0u;
		trail->ribbon.BuildMesh(camera.translation, gameTime_, StyleOf(*trail), trails[{ trail->additive, texture }]);
	}
	for (const auto& [key, vertices] : trails) TrailRenderer::GetInstance()->Draw(cmdList, camera, vertices, key.first, key.second);
	GpuParticles::GetInstance()->Draw(cmdList, camera);
}
