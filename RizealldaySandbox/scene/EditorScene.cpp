#include "scene/EditorScene.h"
#include "scene/TitleScene.h"
#include "scene/SceneManager.h"
#include "base/TextureManager.h"

EditorScene::~EditorScene() {
	Finalize();
}

void EditorScene::Initialize() {
	// 1. Setup Game Camera
	gameCamera_.Initialize();
	gameCamera_.translation = { 0.0f, 1.5f, -8.0f };
	gameCamera_.rotation = { 0.15f, 0.0f, 0.0f };

	// 2. Lighting Setup
	lights_ = LightGroup::Create();
	lights_->SetDefaultLighting();
	lights_->SetAmbientColor({ 0.25f, 0.25f, 0.3f });
	lights_->SetAmbientIntensity(0.6f);
	lights_->GetDirectionalLight().direction = { 0.5f, -1.0f, 0.5f };
	lights_->GetDirectionalLight().color = { 1.0f, 0.95f, 0.9f, 1.0f };

	// 3. Initialize Editor Suite
	editorSuite_ = std::make_unique<EditorSuite>();
	editorSuite_->Initialize(RizeEngine::GetDirectXCommon(), &gameCamera_, lights_.get());

	// 4. Hook up transition callback back to TitleScene
	editorSuite_->SetOnRequestTitleScene([this]() {
		if (sceneManager_) {
			sceneManager_->ChangeScene(std::make_unique<TitleScene>());
		}
	});
}

void EditorScene::Update(float deltaTime) {
	lastDeltaTime_ = deltaTime;
	if (!editorSuite_) return;

	Input* input = RizeEngine::GetInput();

	// Update Editor & Cameras
	editorSuite_->Update(deltaTime, input);
	if (lights_) {
		lights_->Update();
	}
}

void EditorScene::Draw() {
	if (!editorSuite_) return;

	// Render Offscreen Viewports (Edit View & Game View) then restore to Main BackBuffer
	auto* cmdList = RizeEngine::GetDirectXCommon()->GetCommandList();
	editorSuite_->RenderViewports(cmdList);

	// Draw ImGui Editor Workspace
	editorSuite_->DrawEditorUI(lastDeltaTime_);
}

void EditorScene::Finalize() {
	// Reset Object3d static references before tearing down EditorSuite and lights
	Object3d::StaticSetShadowMap(nullptr);
	Object3d::StaticSetLightGroup(nullptr);

	// Tear down EditorSuite first (joins build thread, destroys script instances, unloads DLL, releases RTs)
	editorSuite_.reset();

	// Clean up scene-local lighting
	lights_.reset();
}
