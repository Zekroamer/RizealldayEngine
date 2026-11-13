#include "UILayout.h"

namespace UILayout {

Rect Place(const Rect& parent, const Placement& p, float scale)
{
	const float left = parent.x + p.anchorMinX * parent.width;
	const float right = parent.x + p.anchorMaxX * parent.width;
	const float top = parent.y + p.anchorMinY * parent.height;
	const float bottom = parent.y + p.anchorMaxY * parent.height;
	Rect r;
	r.width = (right - left) + p.width * scale;
	r.height = (bottom - top) + p.height * scale;
	// The pivot sits at its anchor point (between the min and max anchors) plus the offset
	r.x = left + (right - left) * p.pivotX + p.offsetX * scale - p.pivotX * r.width;
	r.y = top + (bottom - top) * p.pivotY + p.offsetY * scale - p.pivotY * r.height;
	return r;
}

} // namespace UILayout
