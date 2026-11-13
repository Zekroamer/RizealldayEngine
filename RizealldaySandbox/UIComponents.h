#pragma once
#include "UILayout.h"
#include "math/Vector4.h"

// Game UI data components (EditorSuite_GameUI.cpp). A UI element is an object with UI Rect under an object with Canvas.

inline constexpr const char* kCanvasComponent = "Canvas";
inline constexpr const char* kUIRectComponent = "UI Rect";
inline constexpr const char* kUIImageComponent = "UI Image";
inline constexpr const char* kUITextComponent = "UI Text";
inline constexpr const char* kUIProgressBarComponent = "UI Progress Bar";
inline constexpr const char* kUIButtonComponent = "UI Button";

/// Root of a game screen: a 1280x720 reference area scaled to the view's height (UILayout::CanvasScale)
struct CanvasComponent {};

using UIRectComponent = UILayout::Placement;

/// A filled rectangle (Panel) or a texture, multiplied by the colour
struct UIImageComponent {
	char texture[260] = {}; // empty = plain colour
	Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
};

/// Text inside the element's rectangle, centred vertically. UTF-8 (Japanese works), '\n' starts a new line.
struct UITextComponent {
	char text[256] = {};
	float size = 32.0f;     // line height in reference pixels
	Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
	int alignment = 1;      // UIRenderer::Align: 0 left, 1 centre, 2 right
};

/// A bar filled from the left by `value` (0..1) over a background
struct UIProgressBarComponent {
	float value = 1.0f;
	Vector4 fillColor{ 0.2f, 0.8f, 0.3f, 1.0f };
	Vector4 backgroundColor{ 0.1f, 0.1f, 0.1f, 0.8f };
};

/// A clickable rectangle with a centred label. Scripts get ScriptBase::OnUIButton(name) in Play.
struct UIButtonComponent {
	char label[128] = {};
	float labelSize = 28.0f; // line height in reference pixels
	Vector4 normalColor{ 0.1f, 0.1f, 0.12f, 0.85f };
	Vector4 hoverColor{ 0.25f, 0.25f, 0.3f, 0.95f }; // also used while selected by a script
	Vector4 pressedColor{ 0.95f, 0.75f, 0.2f, 1.0f };
};

/// What GameObject > UI creates
enum class UIObjectKind { Canvas, Panel, Image, Text, ProgressBar, Button };

/// One frame of pointer input for the game UI, in view pixels (origin top-left)
struct GameUIPointer {
	float x = 0.0f;
	float y = 0.0f;
	float viewWidth = 0.0f;
	float viewHeight = 0.0f;
	bool inside = false;  // the mouse is over the view
	bool down = false;    // left button held
	bool confirm = false; // Enter / Space / pad A pressed this frame (clicks the button selected by script)
};
