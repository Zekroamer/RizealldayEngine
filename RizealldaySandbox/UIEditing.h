#pragma once
#include "UILayout.h"

// UI Editor editing rules (pure): drag, resize from a handle, anchor presets. Pixel deltas are screen pixels of the
// shown canvas; `scale` is its UILayout::CanvasScale. The element's rectangle comes from UILayout::Place.
namespace UIEditing {

/// The aspect ratios the UI Editor can show (the canvas keeps the reference height)
enum class Aspect { Wide16x9, Wide16x10, Standard4x3, Count };

/// The canvas in reference pixels for an aspect: 1280x720, 1152x720 or 960x720
UILayout::Rect CanvasRect(Aspect aspect);

/// Drag: the pivot follows the mouse
void Move(UILayout::Placement& p, float dx, float dy, float scale);

/// Edges a resize handle moves (a corner moves two)
enum Edge : int { kLeft = 1, kRight = 2, kTop = 4, kBottom = 8 };

/// Moves the given edges by the delta; the opposite edges stay where they are. The size never goes below 0.
void Resize(UILayout::Placement& p, const UILayout::Rect& parent, float scale, int edges, float dx, float dy);

/// Anchor preset column / row: 0 = min (left / top), 1 = centre, 2 = max (right / bottom), 3 = stretched
inline constexpr int kStretch = 3;

/// New anchors from the preset grid; offsets and size change so the element stays where it is on the screen
void ApplyAnchorPreset(UILayout::Placement& p, const UILayout::Rect& parent, float scale, int column, int row);

} // namespace UIEditing
