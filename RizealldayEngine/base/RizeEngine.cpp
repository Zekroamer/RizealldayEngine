#include "base/RizeEngine.h"
#include "base/DirectXCommon.h"
#include "base/Profiler.h"
#include "base/WinApp.h"
#include "base/DXCManager.h"
#include "base/GraphicPipeline.h"
#include "base/TextureManager.h"
#include "base/Log.h"
#include "base/CrashHandler.h"
#include "input/Input.h"
#include "audio/Audio.h"
#include "3d/Model.h"
#include "3d/Object3d.h"
#include "3d/WorldTransform.h"
#include "3d/Camera.h"
#include "3d/LightGroup.h"
#include "3d/PrimitiveDrawer.h"
#include "2d/UIRenderer.h"
#include "3d/CircleShadow.h"
#include <memory>

namespace {
	WinApp* sWinApp = nullptr;
	DirectXCommon* sDxCommon = nullptr;
	std::unique_ptr<DXCManager> sDxcManager;
	std::unique_ptr<GraphicPipeline> sGraphicPipeline;
	Input* sInput = nullptr;
	Audio* sAudio = nullptr;
}

namespace RizeEngine {

void Initialize(const char* title, int width, int height) {
	// Logger
	Log::GetInstance()->saveLogs();

	// Crash Handler
	CrashHandler::GetInstance()->Initialize();

	// Window
	sWinApp = WinApp::GetInstance();
	sWinApp->CreateGameWindow(title, width, height);

	int clientW = sWinApp->GetClientWidth();
	int clientH = sWinApp->GetClientHeight();

	// DirectX
	sDxCommon = DirectXCommon::GetInstance();
	sDxCommon->Initialize(sWinApp->GetHWND(), clientW, clientH);

	// Profiler (needs the device + queue)
	Profiler::GetInstance()->Initialize(sDxCommon);

	// DXC Shader Compiler
	sDxcManager = std::make_unique<DXCManager>();
	sDxcManager->Initialize();

	// Graphics Pipeline
	sGraphicPipeline = std::make_unique<GraphicPipeline>();
	sGraphicPipeline->Initialize(sDxCommon->GetDevice(), sDxcManager.get());

	// Texture Manager
	TextureManager::GetInstance()->Initialize(sDxCommon);

	// Input
	sInput = Input::GetInstance();
	sInput->Initialize(sWinApp->GetHInstance(), sWinApp->GetHWND());

	// Audio
	sAudio = Audio::GetInstance();
	sAudio->Initialize();

	// Static Initialization for 3D systems
	Model::StaticInitialize(sDxCommon);
	Object3d::StaticInitialize(sDxCommon);
	WorldTransform::StaticInitialize(sDxCommon);
	Camera::StaticInitialize(sDxCommon);
	LightGroup::StaticInitialize(sDxCommon);
	PrimitiveDrawer::GetInstance()->Initialize(sDxCommon, sDxcManager.get());
	UIRenderer::GetInstance()->Initialize(sDxCommon, sDxcManager.get());

	// Register pipeline with Object3d so Draw() can bind blend modes
	Object3d::StaticSetPipeline(sGraphicPipeline.get());

	// Initialize Circle Shadow system
	CircleShadow::StaticInitialize(sDxCommon, sDxcManager.get());

	Log::GetInstance()->logOutput("Rizeallday Engine v2 initialized.\n");
}

bool ProcessMessage() {
	return !sWinApp->ProcessMessage();
}

bool IsDeviceLost() {
	return sDxCommon && sDxCommon->IsDeviceLost();
}

static bool sFrameActive = false;

void BeginFrame() {
	// Input & Audio update
	sInput->Update();
	if (sAudio) {
		sAudio->Update();
	}

	if (!sDxCommon || sDxCommon->IsDeviceLost()) {
		sFrameActive = false;
		return;
	}

	sFrameActive = true;

	// PreDraw (clears screen, sets render targets, starts ImGui frame)
	sDxCommon->PreDraw();

	// Bind the graphics pipeline
	sGraphicPipeline->BindForBackBuffer(sDxCommon->GetCommandList());

	// Set Descriptor Heaps for textures
	TextureManager::GetInstance()->SetDescriptorHeaps(sDxCommon->GetCommandList());
}

void EndFrame() {
	if (!sDxCommon || !sFrameActive) {
		return;
	}
	sFrameActive = false;
	// PostDraw (renders ImGui, presents)
	sDxCommon->PostDraw();
}

void Finalize() {
	// Wait for GPU to finish
	if (sDxCommon && !sDxCommon->IsDeviceLost()) {
		sDxCommon->SignalAndWaitFence();
	}

	// Cleanup
	sAudio->Finalize();

	PrimitiveDrawer::GetInstance()->Finalize();
	UIRenderer::GetInstance()->Finalize();

	// Release the query heaps before DirectXCommon flushes its deferred-release queue
	Profiler::GetInstance()->Finalize();

	sGraphicPipeline.reset();
	sDxcManager.reset();

	sDxCommon->Finalize(sWinApp->GetHWND());
	sWinApp->TerminateGameWindow();

	Log::GetInstance()->logOutput("Rizeallday Engine v2 finalized.\n");
}

DirectXCommon* GetDirectXCommon() {
	return sDxCommon;
}

DXCManager* GetDXCManager() {
	return sDxcManager.get();
}

GraphicPipeline* GetGraphicPipeline() {
	return sGraphicPipeline.get();
}

Input* GetInput() {
	return sInput;
}

Audio* GetAudio() {
	return sAudio;
}

} // namespace RizeEngine
