#include "UIEditing.h"
#include <algorithm>

namespace UIEditing {

UILayout::Rect CanvasRect(Aspect aspect)
{
	const float h = UILayout::kReferenceHeight;
	switch (aspect) {
	case Aspect::Wide16x10: return { 0.0f, 0.0f, h * 16.0f / 10.0f, h };
	case Aspect::Standard4x3: return { 0.0f, 0.0f, h * 4.0f / 3.0f, h };
	default: return { 0.0f, 0.0f, UILayout::kReferenceWidth, h };
	}
}

void Move(UILayout::Placement& p, float dx, float dy, float scale)
{
	if (scale <= 0.0f) return;
	p.offsetX += dx / scale;
	p.offsetY += dy / scale;
}

namespace {
	// One axis: rect size `size`, pivot fraction `pivot`. Moving the min edge by d keeps the max edge, and the other way round.
	void ResizeAxis(float& offset, float& sizeField, float size, float pivot, float scale, bool minEdge, bool maxEdge, float d)
	{
		if (minEdge) {
			d = (std::min)(d, size); // the size never goes below 0
			sizeField -= d / scale;
			offset += (1.0f - pivot) * d / scale;
			size -= d;
		}
		if (maxEdge) {
			d = (std::max)(d, -size);
			sizeField += d / scale;
			offset += pivot * d / scale;
		}
	}
}

void Resize(UILayout::Placement& p, const UILayout::Rect& parent, float scale, int edges, float dx, float dy)
{
	if (scale <= 0.0f) return;
	const UILayout::Rect r = UILayout::Place(parent, p, scale);
	ResizeAxis(p.offsetX, p.width, r.width, p.pivotX, scale, (edges & kLeft) != 0, (edges & kRight) != 0, dx);
	ResizeAxis(p.offsetY, p.height, r.height, p.pivotY, scale, (edges & kTop) != 0, (edges & kBottom) != 0, dy);
}

void ApplyAnchorPreset(UILayout::Placement& p, const UILayout::Rect& parent, float scale, int column, int row)
{
	if (scale <= 0.0f) return;
	const UILayout::Rect r = UILayout::Place(parent, p, scale);
	const auto anchors = [](int index, float& lo, float& hi) {
		if (index == kStretch) { lo = 0.0f; hi = 1.0f; }
		else { lo = hi = 0.5f * static_cast<float>(std::clamp(index, 0, 2)); }
	};
	anchors(column, p.anchorMinX, p.anchorMaxX);
	anchors(row, p.anchorMinY, p.anchorMaxY);
	// Solve UILayout::Place backwards for the same rectangle
	const auto solve = [scale](float parentPos, float parentSize, float lo, float hi, float pivot, float rectPos, float rectSize, float& offset, float& size) {
		const float span = (hi - lo) * parentSize;
		size = (rectSize - span) / scale;
		offset = (rectPos + pivot * rectSize - (parentPos + lo * parentSize + span * pivot)) / scale;
	};
	solve(parent.x, parent.width, p.anchorMinX, p.anchorMaxX, p.pivotX, r.x, r.width, p.offsetX, p.width);
	solve(parent.y, parent.height, p.anchorMinY, p.anchorMaxY, p.pivotY, r.y, r.height, p.offsetY, p.height);
}

} // namespace UIEditing
