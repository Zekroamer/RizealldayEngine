#pragma once

// Game UI layout (pure): where a UI element sits on the screen. Shared by the renderer, the UI Editor and hit testing.
// Screen pixels, origin top-left, y down. Anchors and pivot are fractions of the parent (0,0 = top-left, 1,1 = bottom-right).
namespace UILayout {

inline constexpr float kReferenceWidth = 1280.0f;
inline constexpr float kReferenceHeight = 720.0f;

struct Rect {
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

/// What a UI element saves (the "UI Rect" component). Offsets and size are in reference pixels.
struct Placement {
	float anchorMinX = 0.5f;
	float anchorMinY = 0.5f;
	float anchorMaxX = 0.5f;
	float anchorMaxY = 0.5f;
	float pivotX = 0.5f;
	float pivotY = 0.5f;
	float offsetX = 0.0f;   // the pivot's distance from its anchor point
	float offsetY = 0.0f;
	float width = 100.0f;   // added to the anchors' span: with stretched anchors a negative value is an inset
	float height = 100.0f;
};

/// Reference pixels -> screen pixels: the canvas matches the view's height, so wider views do not stretch the UI
inline float CanvasScale(float viewHeight) { return viewHeight / kReferenceHeight; }

/// The element's screen rectangle inside its parent's rectangle
Rect Place(const Rect& parent, const Placement& placement, float scale);

} // namespace UILayout
