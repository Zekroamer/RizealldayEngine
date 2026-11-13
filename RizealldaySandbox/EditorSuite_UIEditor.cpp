#include "EditorSuiteInternal.h"
#include "UIEditing.h"

// Window > UI Editor: the selected canvas at a chosen aspect, element rectangles with anchors and pivots,
// drag to move, handles to resize. The editing rules are UIEditing (pure) and the functions below (tested);
// the window only turns mouse input into calls to them.

uint32_t EditorSuite::UIEditorCanvasId()
{
	const auto isCanvas = [](const GameObject* o) { return o && o->components.Has(kCanvasComponent); };
	if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) {
		const GameObject* o = &gameObjects_[selectedObjectIndex_];
		for (int depth = 0; o && depth < 64; ++depth) {
			if (isCanvas(o)) return uiEditorCanvasId_ = o->id;
			o = o->parentId >= 0 ? FindGameObjectById(static_cast<uint32_t>(o->parentId), false) : nullptr;
		}
	}
	if (uiEditorCanvasId_ != 0 && isCanvas(FindGameObjectById(uiEditorCanvasId_, false))) return uiEditorCanvasId_;
	for (const GameObject& o : gameObjects_) {
		if (isCanvas(&o)) return uiEditorCanvasId_ = o.id;
	}
	return uiEditorCanvasId_ = 0;
}

bool EditorSuite::UIElementRect(uint32_t id, const UILayout::Rect& canvasRect, UILayout::Rect& rect, UILayout::Rect* parentRect)
{
	// Walk up to the canvas; every step on the way must be a UI element (as in DrawGameUI)
	std::vector<const UIRectComponent*> chain;
	const GameObject* o = FindGameObjectById(id, false);
	while (o && !o->components.Has(kCanvasComponent)) {
		const UIRectComponent* placement = o->components.Get<UIRectComponent>(kUIRectComponent);
		if (!placement || chain.size() > 64) return false;
		chain.push_back(placement);
		o = o->parentId >= 0 ? FindGameObjectById(static_cast<uint32_t>(o->parentId), false) : nullptr;
	}
	if (!o || chain.empty()) return false;
	const float scale = UILayout::CanvasScale(canvasRect.height);
	UILayout::Rect parent = canvasRect;
	for (size_t i = chain.size(); i-- > 1;) parent = UILayout::Place(parent, *chain[i], scale);
	rect = UILayout::Place(parent, *chain[0], scale);
	if (parentRect) *parentRect = parent;
	return true;
}

std::vector<std::pair<uint32_t, UILayout::Rect>> EditorSuite::UIEditorElementRects(uint32_t canvasId, const UILayout::Rect& canvasRect)
{
	std::vector<std::pair<uint32_t, UILayout::Rect>> out;
	const GameObject* canvas = FindGameObjectById(canvasId, false);
	if (!canvas) return out;
	const float scale = UILayout::CanvasScale(canvasRect.height);
	const auto visit = [&](auto& self, const GameObject& parent, const UILayout::Rect& parentRect, int depth) -> void {
		if (depth > 64) return;
		for (const uint32_t childId : parent.childrenIds) {
			const GameObject* child = FindGameObjectById(childId, false);
			if (!child || !child->isActive) continue;
			const UIRectComponent* placement = child->components.Get<UIRectComponent>(kUIRectComponent);
			if (!placement) continue;
			const UILayout::Rect r = UILayout::Place(parentRect, *placement, scale);
			out.emplace_back(childId, r);
			self(self, *child, r, depth + 1);
		}
	};
	visit(visit, *canvas, canvasRect, 0);
	return out;
}

bool EditorSuite::MoveUIElement(uint32_t id, float dx, float dy, const UILayout::Rect& canvasRect)
{
	UILayout::Rect r;
	if (!UIElementRect(id, canvasRect, r)) return false;
	UIEditing::Move(*FindGameObjectById(id, false)->components.Get<UIRectComponent>(kUIRectComponent), dx, dy, UILayout::CanvasScale(canvasRect.height));
	isSceneDirty_ = true;
	return true;
}

bool EditorSuite::ResizeUIElement(uint32_t id, int edges, float dx, float dy, const UILayout::Rect& canvasRect)
{
	UILayout::Rect r, parent;
	if (!UIElementRect(id, canvasRect, r, &parent)) return false;
	UIEditing::Resize(*FindGameObjectById(id, false)->components.Get<UIRectComponent>(kUIRectComponent), parent,
		UILayout::CanvasScale(canvasRect.height), edges, dx, dy);
	isSceneDirty_ = true;
	return true;
}

bool EditorSuite::ApplyUIAnchorPreset(uint32_t id, int column, int row)
{
	const UILayout::Rect canvas = UIEditing::CanvasRect(static_cast<UIEditing::Aspect>(std::clamp(uiEditorAspect_, 0, 2)));
	UILayout::Rect r, parent;
	if (!UIElementRect(id, canvas, r, &parent)) return false;
	UIEditing::ApplyAnchorPreset(*FindGameObjectById(id, false)->components.Get<UIRectComponent>(kUIRectComponent), parent,
		UILayout::CanvasScale(canvas.height), column, row);
	isSceneDirty_ = true;
	PushUndoSnapshot();
	return true;
}

void EditorSuite::DrawUIRectAnchorPresets(EditorSuite& suite, GameObject& obj)
{
#ifdef USE_IMGUI
	const UIRectComponent* p = obj.components.Get<UIRectComponent>(kUIRectComponent);
	if (!p) return;
	static const char* const kColumns[] = { "Left", "Center", "Right", "Stretch" };
	static const char* const kRows[] = { "Top", "Middle", "Bottom", "Stretch" };
	const auto presetOf = [](float lo, float hi) {
		if (lo == 0.0f && hi == 1.0f) return UIEditing::kStretch;
		if (lo != hi) return -1;
		return lo == 0.0f ? 0 : lo == 0.5f ? 1 : lo == 1.0f ? 2 : -1;
	};
	const int currentColumn = presetOf(p->anchorMinX, p->anchorMaxX);
	const int currentRow = presetOf(p->anchorMinY, p->anchorMaxY);
	const uint32_t id = obj.id;

	ImGui::TextUnformatted(Tr("Anchor Presets"));
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float cell = 22.0f;
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			ImGui::PushID(row * 4 + column);
			if (column > 0) ImGui::SameLine(0.0f, 2.0f);
			const ImVec2 a = ImGui::GetCursorScreenPos();
			const bool clicked = ImGui::InvisibleButton("##anchorPreset", ImVec2(cell, cell));
			const bool current = column == currentColumn && row == currentRow;
			const ImU32 frame = current ? IM_COL32(80, 140, 230, 255) : ImGui::IsItemHovered() ? IM_COL32(140, 140, 140, 255) : IM_COL32(90, 90, 90, 255);
			const ImU32 mark = IM_COL32(230, 120, 60, 255);
			const ImVec2 lo(a.x + 4.0f, a.y + 4.0f), hi(a.x + cell - 4.0f, a.y + cell - 4.0f);
			dl->AddRect(lo, hi, frame, 0.0f, 0, current ? 2.0f : 1.0f);
			const float x = lo.x + (hi.x - lo.x) * 0.5f * static_cast<float>((std::min)(column, 2));
			const float y = lo.y + (hi.y - lo.y) * 0.5f * static_cast<float>((std::min)(row, 2));
			if (column == UIEditing::kStretch && row == UIEditing::kStretch) dl->AddRectFilled(ImVec2(lo.x + 3.0f, lo.y + 3.0f), ImVec2(hi.x - 3.0f, hi.y - 3.0f), mark);
			else if (column == UIEditing::kStretch) dl->AddLine(ImVec2(lo.x, y), ImVec2(hi.x, y), mark, 2.0f);
			else if (row == UIEditing::kStretch) dl->AddLine(ImVec2(x, lo.y), ImVec2(x, hi.y), mark, 2.0f);
			else dl->AddCircleFilled(ImVec2(x, y), 2.5f, mark);
			ImGui::SetItemTooltip("%s", (std::string(Tr(kColumns[column])) + " / " + Tr(kRows[row])).c_str());
			if (clicked) suite.ApplyUIAnchorPreset(id, column, row);
			ImGui::PopID();
		}
	}
	ImGui::TextDisabled("%s", Tr("The element keeps its place on the screen."));
#else
	(void)suite;
	(void)obj;
#endif
}

void EditorSuite::DrawUIEditorWindow()
{
#ifdef USE_IMGUI
	ImGui::SetNextWindowSize(ImVec2(720.0f, 480.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TrId("UI Editor"), &showUIEditorWindow_)) {
		ImGui::End();
		return;
	}
	static const char* const kAspects[] = { "16:9", "16:10", "4:3" };
	ImGui::SetNextItemWidth(100.0f);
	ImGui::Combo(TrId("Aspect"), &uiEditorAspect_, kAspects, IM_ARRAYSIZE(kAspects));
	uiEditorAspect_ = std::clamp(uiEditorAspect_, 0, 2);

	const uint32_t canvasId = UIEditorCanvasId();
	const GameObject* canvasObj = canvasId ? FindGameObjectById(canvasId, false) : nullptr;
	if (!canvasObj) {
		ImGui::TextWrapped("%s", Tr("No canvas in the scene. Add one with GameObject > UI > Canvas."));
		ImGui::End();
		return;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%s", canvasObj->name.c_str());

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const UILayout::Rect reference = UIEditing::CanvasRect(static_cast<UIEditing::Aspect>(uiEditorAspect_));
	const float fit = (std::min)(avail.x / reference.width, avail.y / reference.height);
	if (fit <= 0.0f) {
		ImGui::End();
		return;
	}
	const UILayout::Rect canvas{ origin.x + (avail.x - reference.width * fit) * 0.5f, origin.y, reference.width * fit, reference.height * fit };
	// One item over the whole area so a drag never moves the window
	ImGui::InvisibleButton("##uiEditorCanvas", avail);
	const bool pressed = ImGui::IsItemActivated();
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;

	const auto toU32 = [](const Vector4& c) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w)); };
	const auto contains = [](const UILayout::Rect& r, ImVec2 p) { return p.x >= r.x && p.y >= r.y && p.x <= r.x + r.width && p.y <= r.y + r.height; };
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 c0(canvas.x, canvas.y), c1(canvas.x + canvas.width, canvas.y + canvas.height);
	dl->AddRectFilled(c0, c1, IM_COL32(40, 44, 52, 255));
	dl->PushClipRect(c0, c1, true);

	// The elements with their look, roughly as the Game view draws them (textures as their tint)
	const float scale = UILayout::CanvasScale(canvas.height);
	const auto elements = UIEditorElementRects(canvasId, canvas);
	for (const auto& [id, r] : elements) {
		const GameObject* obj = FindGameObjectById(id, false);
		const ImVec2 r0(r.x, r.y), r1(r.x + r.width, r.y + r.height);
		if (const UIImageComponent* image = obj->components.Get<UIImageComponent>(kUIImageComponent)) dl->AddRectFilled(r0, r1, toU32(image->color));
		if (const UIProgressBarComponent* bar = obj->components.Get<UIProgressBarComponent>(kUIProgressBarComponent)) {
			dl->AddRectFilled(r0, r1, toU32(bar->backgroundColor));
			dl->AddRectFilled(r0, ImVec2(r.x + r.width * std::clamp(bar->value, 0.0f, 1.0f), r1.y), toU32(bar->fillColor));
		}
		if (const UIButtonComponent* button = obj->components.Get<UIButtonComponent>(kUIButtonComponent)) dl->AddRectFilled(r0, r1, toU32(button->normalColor));
		EditorSuiteDetail::ForEachUIText(*obj, [&](const char* s, size_t capacity, float refSize, const Vector4& color, int alignment) {
			const float size = refSize * scale;
			const char* end = s + strnlen_s(s, capacity);
			const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, s, end);
			const ImVec2 at(r.x + (r.width - extent.x) * 0.5f * static_cast<float>(std::clamp(alignment, 0, 2)), r.y + (r.height - extent.y) * 0.5f);
			const ImVec4 clip(r0.x, r0.y, r1.x, r1.y);
			dl->AddText(ImGui::GetFont(), size, at, toU32(color), s, end, 0.0f, &clip);
		});
		dl->AddRect(r0, r1, IM_COL32(255, 255, 255, 60));
	}
	dl->PopClipRect();
	dl->AddRect(c0, c1, IM_COL32(200, 200, 200, 255));

	// The selected element: outline, anchors (corners of the anchor box in the parent), pivot and resize handles
	const uint32_t selectedId = (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) ? gameObjects_[selectedObjectIndex_].id : 0;
	struct Handle { ImVec2 at; int edges; };
	std::vector<Handle> handles;
	UILayout::Rect sel, parent;
	if (selectedId != 0 && UIElementRect(selectedId, canvas, sel, &parent)) {
		const UIRectComponent* p = FindGameObjectById(selectedId, false)->components.Get<UIRectComponent>(kUIRectComponent);
		const ImU32 accent = IM_COL32(80, 160, 255, 255);
		dl->AddRect(ImVec2(sel.x, sel.y), ImVec2(sel.x + sel.width, sel.y + sel.height), accent, 0.0f, 0, 2.0f);
		const float ax[2] = { parent.x + p->anchorMinX * parent.width, parent.x + p->anchorMaxX * parent.width };
		const float ay[2] = { parent.y + p->anchorMinY * parent.height, parent.y + p->anchorMaxY * parent.height };
		for (const float x : ax) {
			for (const float y : ay) {
				const ImVec2 a(x, y);
				dl->AddTriangleFilled(a, ImVec2(a.x - 5.0f, a.y - 9.0f), ImVec2(a.x + 5.0f, a.y - 9.0f), IM_COL32(240, 240, 240, 230));
			}
		}
		dl->AddCircle(ImVec2(sel.x + p->pivotX * sel.width, sel.y + p->pivotY * sel.height), 5.0f, IM_COL32(60, 220, 120, 255), 0, 2.0f);
		using namespace UIEditing;
		const float xs[3] = { sel.x, sel.x + sel.width * 0.5f, sel.x + sel.width };
		const float ys[3] = { sel.y, sel.y + sel.height * 0.5f, sel.y + sel.height };
		const int xEdges[3] = { kLeft, 0, kRight };
		const int yEdges[3] = { kTop, 0, kBottom };
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				if (i == 1 && j == 1) continue;
				handles.push_back({ ImVec2(xs[i], ys[j]), xEdges[i] | yEdges[j] });
				dl->AddRectFilled(ImVec2(xs[i] - 4.0f, ys[j] - 4.0f), ImVec2(xs[i] + 4.0f, ys[j] + 4.0f), accent);
			}
		}
	}

	// Mouse: press on a handle = resize, on an element = select (topmost) and move; release = one undo step
	if (pressed) {
		uiEditorDragEdges_ = -1;
		for (const Handle& h : handles) {
			if (std::abs(mouse.x - h.at.x) <= 6.0f && std::abs(mouse.y - h.at.y) <= 6.0f) {
				uiEditorDragId_ = selectedId;
				uiEditorDragEdges_ = h.edges;
				break;
			}
		}
		if (uiEditorDragEdges_ < 0) {
			uint32_t picked = 0;
			for (auto it = elements.rbegin(); it != elements.rend() && picked == 0; ++it) {
				if (contains(it->second, mouse)) picked = it->first;
			}
			SelectObject(FindGameObjectIndexById(picked != 0 ? picked : canvasId), false);
			if (picked != 0) {
				uiEditorDragId_ = picked;
				uiEditorDragEdges_ = 0;
			}
		}
	}
	if (uiEditorDragEdges_ >= 0) {
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			uiEditorDragEdges_ = -1;
			PushUndoSnapshot();
		} else if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
			const bool ok = uiEditorDragEdges_ == 0 ? MoveUIElement(uiEditorDragId_, mouseDelta.x, mouseDelta.y, canvas)
				: ResizeUIElement(uiEditorDragId_, uiEditorDragEdges_, mouseDelta.x, mouseDelta.y, canvas);
			if (!ok) uiEditorDragEdges_ = -1; // the element went away mid-drag
		}
	}
	ImGui::End();
#endif
}
