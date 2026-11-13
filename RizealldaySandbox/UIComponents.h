#pragma once
#include "UILayout.h"
#include "math/Vector4.h"

// Game UI data components (EditorSuite_GameUI.cpp). A UI element is an object with UI Rect under an object with Canvas.

inline constexpr const char* kCanvasComponent = "Canvas";
inline constexpr const char* kUIRectComponent = "UI Rect";
inline constexpr const char* kUIImageComponent = "UI Image";

/// Root of a game screen: a 1280x720 reference area scaled to the view's height (UILayout::CanvasScale)
struct CanvasComponent {};

using UIRectComponent = UILayout::Placement;

/// A filled rectangle (Panel) or a texture, multiplied by the colour
struct UIImageComponent {
	char texture[260] = {}; // empty = plain colour
	Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
};

/// What GameObject > UI creates
enum class UIObjectKind { Canvas, Panel, Image };
