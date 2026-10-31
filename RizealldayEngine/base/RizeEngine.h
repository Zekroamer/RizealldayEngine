#pragma once

// === Rizeallday Engine v2 ===
// Include this single header to access the entire engine.

// Base
#include "base/DirectXCommon.h"
#include "base/WinApp.h"
#include "base/TextureManager.h"
#include "base/DXCManager.h"
#include "base/GraphicPipeline.h"
#include "base/Log.h"
#include "base/ScriptAPI.h"
#include "base/ScriptBase.h"
#include "base/ScriptModule.h"

// 3D
#include "3d/Model.h"
#include "3d/Object3d.h"
#include "3d/Camera.h"
#include "3d/DebugCamera.h"
#include "3d/WorldTransform.h"
#include "3d/LightGroup.h"
#include "3d/DirectionalLight.h"
#include "3d/PointLight.h"
#include "3d/SpotLight.h"
#include "3d/PrimitiveDrawer.h"
#include "3d/CircleShadow.h"

// 2D
#include "2d/Sprite.h"

// Input
#include "input/Input.h"

// Audio
#include "audio/Audio.h"

// Math
#include "math/Vector2.h"
#include "math/Vector3.h"
#include "math/Vector4.h"
#include "math/Matrix4x4.h"
#include "math/MathUtil.h"
#include "math/VertexData.h"
#include "math/Frustum.h"

// Framework
#include "framework/Framework.h"

// Scene (Application States)
#include "scene/BaseScene.h"
#include "scene/SceneManager.h"

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace RizeEngine {

/// Engine initialization
void Initialize(const char* title = "Rizeallday Engine", int width = 1280, int height = 720);

/// Window message processing (returns false when exiting loop)
bool ProcessMessage();

/// Returns true if the D3D12 device is currently in a lost state
bool IsDeviceLost();

/// Begin frame (PreDraw + ImGui NewFrame)
void BeginFrame();

/// End frame (ImGui Render + PostDraw + Present)
void EndFrame();

/// Engine finalization
void Finalize();

/// Subsystem accessors
DirectXCommon* GetDirectXCommon();
DXCManager* GetDXCManager();
GraphicPipeline* GetGraphicPipeline();
Input* GetInput();
Audio* GetAudio();

} // namespace RizeEngine
