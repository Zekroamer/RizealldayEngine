#include "EditorSuiteInternal.h"
#include "render/GpuParticles.h"
#include "base/TextureManager.h"
#include <chrono>

// Visual effect components (Fase 7). The effects themselves live in RizealldayEngine/render.

namespace {
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
		RIZE_FIELD(TrailComponent, emitting, ComponentFieldType::Bool, 0.0f, 0.0f) });

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
		RIZE_FIELD(GpuParticlesComponent, intensity, ComponentFieldType::Float, 0.0f, 100.0f),
		RIZE_FIELD(GpuParticlesComponent, additive, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(GpuParticlesComponent, texture, ComponentFieldType::Asset, 0.0f, 0.0f) });
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
		settings.additive = p->additive;
		const std::string texture = p->texture[0] != '\0' ? ResolveAssetPath(p->texture) : std::string("Resources/circle.png");
		settings.textureHandle = TextureManager::GetInstance()->LoadTexture(texture);

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

void EditorSuite::DrawEffects(ID3D12GraphicsCommandList* cmdList, const Camera& camera)
{
	std::vector<TrailVertex> additive;
	std::vector<TrailVertex> blended;
	for (GameObject& obj : gameObjects_) {
		const TrailComponent* trail = obj.components.Get<TrailComponent>(kTrailComponent);
		if (!trail || trail->ribbon.PointCount() < 2) continue;
		trail->ribbon.BuildMesh(camera.translation, gameTime_, StyleOf(*trail), trail->additive ? additive : blended);
	}
	TrailRenderer::GetInstance()->Draw(cmdList, camera, blended, false);
	TrailRenderer::GetInstance()->Draw(cmdList, camera, additive, true);
	GpuParticles::GetInstance()->Draw(cmdList, camera);
}
