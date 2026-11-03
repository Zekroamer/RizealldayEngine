// Animator window (roadmap 5.1): node editor for AnimatorController assets + the skeleton overlay.
// Canvas drawn with ImDrawList only (no node library): nodes are drawn shapes + text, every widget lives in the
// side panel. Node positions are canvas space: screen = canvasOrigin + pan + pos * zoom.
#include "EditorSuiteInternal.h"
#include "3d/Object3d.h"
#include "3d/AnimationPlayer.h"

#ifdef USE_IMGUI
namespace {
	constexpr float kNodeW = 170.0f;
	constexpr float kNodeH = 40.0f;
	constexpr float kLinkOffset = 6.0f;  // A->B and B->A run side by side
	constexpr float kLinkPickPx = 5.0f;

	ImVec2 Add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
	ImVec2 Sub(ImVec2 a, ImVec2 b) { return ImVec2(a.x - b.x, a.y - b.y); }
	ImVec2 Mul(ImVec2 a, float s) { return ImVec2(a.x * s, a.y * s); }
	float Len(ImVec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }

	float DistanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b)
	{
		const ImVec2 ab = Sub(b, a);
		const float len2 = ab.x * ab.x + ab.y * ab.y;
		const float t = len2 > 0.0f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
		return Len(Sub(p, Add(a, Mul(ab, t))));
	}

	/// Point where the ray from the rect centre towards `towards` leaves the rect
	ImVec2 ClipToRect(ImVec2 center, ImVec2 halfSize, ImVec2 towards)
	{
		const ImVec2 d = Sub(towards, center);
		if (std::fabs(d.x) < 1e-4f && std::fabs(d.y) < 1e-4f) return center;
		const float tx = std::fabs(d.x) > 1e-4f ? halfSize.x / std::fabs(d.x) : FLT_MAX;
		const float ty = std::fabs(d.y) > 1e-4f ? halfSize.y / std::fabs(d.y) : FLT_MAX;
		const float t = (std::min)({ tx, ty, 1.0f });
		return Add(center, Mul(d, t));
	}

	bool InputString(const char* label, std::string& value, ImGuiInputTextFlags flags = 0)
	{
		char buf[256];
		strncpy_s(buf, value.c_str(), _TRUNCATE);
		if (ImGui::InputText(label, buf, sizeof(buf), flags)) {
			value = buf;
			return true;
		}
		return false;
	}

	const char* ParamTypeName(AnimParamType t)
	{
		switch (t) {
		case AnimParamType::Int: return "Int";
		case AnimParamType::Bool: return "Bool";
		case AnimParamType::Trigger: return "Trigger";
		default: return "Float";
		}
	}

	const char* OpLabel(AnimConditionOp op)
	{
		switch (op) {
		case AnimConditionOp::Greater: return ">";
		case AnimConditionOp::Less: return "<";
		case AnimConditionOp::Equals: return "==";
		case AnimConditionOp::NotEquals: return "!=";
		case AnimConditionOp::IsTrue: return "is true";
		default: return "is false";
		}
	}

	/// Operators that make sense for a parameter type
	std::vector<AnimConditionOp> OpsFor(AnimParamType t)
	{
		switch (t) {
		case AnimParamType::Float: return { AnimConditionOp::Greater, AnimConditionOp::Less };
		case AnimParamType::Int: return { AnimConditionOp::Greater, AnimConditionOp::Less, AnimConditionOp::Equals, AnimConditionOp::NotEquals };
		case AnimParamType::Bool: return { AnimConditionOp::IsTrue, AnimConditionOp::IsFalse };
		default: return { AnimConditionOp::IsTrue };
		}
	}

	std::string UniqueName(const std::string& base, const std::function<bool(const std::string&)>& taken)
	{
		if (!taken(base)) return base;
		for (int i = 1;; ++i) {
			const std::string name = base + " " + std::to_string(i);
			if (!taken(name)) return name;
		}
	}
}
#endif

// ---------------------------------------------------------------------------
// Working copy, saving and undo
// ---------------------------------------------------------------------------
void EditorSuite::AnimatorLoadWorking(const std::string& assetPath)
{
	animatorWorking_.reset();
	animatorWorkingAsset_ = assetPath;
	animatorDirty_ = false;
	animatorUndo_.clear();
	animatorRedo_.clear();
	animatorSelectedState_ = 0;
	animatorSelectedTransition_ = -1;
	animatorSelectedEntry_ = false;
	animatorLinking_ = false;
	if (const auto controller = GetAnimatorController(assetPath)) {
		animatorWorking_ = *controller;
		animatorCommittedJson_ = controller->ToJson();
	}
}

void EditorSuite::AnimatorCommit()
{
	if (!animatorWorking_ || !animatorDirty_) return;
	const std::string json = animatorWorking_->ToJson();
	animatorDirty_ = false;
	if (json == animatorCommittedJson_) return;
	animatorUndo_.push_back(animatorCommittedJson_);
	if (animatorUndo_.size() > 100) animatorUndo_.erase(animatorUndo_.begin());
	animatorRedo_.clear();
	animatorCommittedJson_ = json;
	SaveAnimatorController(animatorWorkingAsset_, *animatorWorking_);
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------
void EditorSuite::DrawAnimatorWindow()
{
#ifdef USE_IMGUI
	// Follow the selection: the selected object's Animator controller is the one shown
	GameObject* selected = (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size()))
		? &gameObjects_[selectedObjectIndex_] : nullptr;
	AnimatorComponent* selectedAnimator = selected ? GetAnimator(*selected) : nullptr;
	if (selectedAnimator && selectedAnimator->controller[0] != '\0' && animatorWindowAsset_ != selectedAnimator->controller && !animatorDraggingNode_) {
		animatorWindowAsset_ = selectedAnimator->controller;
	}
	if (animatorWorkingAsset_ != animatorWindowAsset_ || (!animatorWorking_ && !animatorWindowAsset_.empty())) {
		AnimatorCommit();
		AnimatorLoadWorking(animatorWindowAsset_);
	} else if (animatorWorking_ && !animatorDirty_) {
		// Picks up edits made outside the window (another editor, a text editor)
		if (const auto controller = GetAnimatorController(animatorWorkingAsset_); controller) {
			const std::string json = controller->ToJson();
			if (json != animatorCommittedJson_) {
				animatorWorking_ = *controller;
				animatorCommittedJson_ = json;
			}
		}
	}

	ImGui::SetNextWindowSize(ImVec2(1100.0f, 620.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TrId("Animator"), &showAnimatorWindow_)) {
		ImGui::End();
		return;
	}

	// Toolbar
	char pathBuf[260];
	strncpy_s(pathBuf, animatorWindowAsset_.c_str(), _TRUNCATE);
	ImGui::SetNextItemWidth(360.0f);
	if (ImGui::InputTextWithHint("##AnimatorAsset", Tr("Assets/Animators/<Name>.animator.json"), pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
		animatorWindowAsset_ = pathBuf;
	}
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RIZE_ASSET_PATH")) animatorWindowAsset_ = static_cast<const char*>(payload->Data);
		ImGui::EndDragDropTarget();
	}
	ImGui::SetItemTooltip("%s", Tr("Controller asset (drop one from the Project panel). Follows the selected object's Animator."));
	ImGui::SameLine();
	if (ImGui::Button(TrId("New Controller"))) {
		const std::string path = CreateAnimatorControllerAsset(selected ? selected->name : "New");
		if (!path.empty()) {
			animatorWindowAsset_ = path;
			if (selectedAnimator && selectedAnimator->controller[0] == '\0') {
				strncpy_s(selectedAnimator->controller, path.c_str(), _TRUNCATE);
				isSceneDirty_ = true;
				PushUndoSnapshot();
			}
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(animatorUndo_.empty());
	if (ImGui::Button(TrId("Undo##Animator")) && animatorWorking_) {
		if (auto previous = AnimatorController::FromJson(animatorUndo_.back())) {
			animatorRedo_.push_back(animatorCommittedJson_);
			animatorCommittedJson_ = animatorUndo_.back();
			animatorUndo_.pop_back();
			animatorWorking_ = std::move(*previous);
			SaveAnimatorController(animatorWorkingAsset_, *animatorWorking_);
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(animatorRedo_.empty());
	if (ImGui::Button(TrId("Redo##Animator")) && animatorWorking_) {
		if (auto next = AnimatorController::FromJson(animatorRedo_.back())) {
			animatorUndo_.push_back(animatorCommittedJson_);
			animatorCommittedJson_ = animatorRedo_.back();
			animatorRedo_.pop_back();
			animatorWorking_ = std::move(*next);
			SaveAnimatorController(animatorWorkingAsset_, *animatorWorking_);
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button(TrId("Frame All"))) {
		animatorPan_ = { 0.0f, 0.0f };
		animatorZoom_ = 1.0f;
		if (animatorWorking_ && !animatorWorking_->states.empty()) {
			Vector2 mn = animatorWorking_->states[0].position, mx = mn;
			for (const AnimState& s : animatorWorking_->states) {
				mn = { (std::min)(mn.x, s.position.x), (std::min)(mn.y, s.position.y) };
				mx = { (std::max)(mx.x, s.position.x), (std::max)(mx.y, s.position.y) };
			}
			animatorPan_ = { 220.0f - mn.x, 120.0f - mn.y };
		}
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%s", Tr("Right-click: create / connect. Middle drag or Alt+drag: pan. Wheel: zoom. Del: delete."));

	if (!animatorWorking_) {
		ImGui::Spacing();
		ImGui::TextWrapped("%s", Tr(animatorWindowAsset_.empty()
			? "Select an object with an Animator component, or create a controller with \"New Controller\"."
			: "Controller file not found or invalid."));
		ImGui::End();
		return;
	}

	// Live instance: the selected object when its Animator runs this controller
	AnimatorComponent* liveAnimator = (selectedAnimator && animatorWorkingAsset_ == selectedAnimator->controller) ? selectedAnimator : nullptr;
	const AnimatorRuntime* live = (liveAnimator && liveAnimator->runtime.started && sceneState_ != EditorSceneState::Edit) ? &liveAnimator->runtime : nullptr;

	const float sideW = 330.0f;
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	ImGui::BeginChild("##AnimatorCanvas", ImVec2((std::max)(200.0f, avail.x - sideW - 8.0f), avail.y), true,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
	DrawAnimatorCanvas(*animatorWorking_, live, ImGui::GetContentRegionAvail());
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild("##AnimatorSide", ImVec2(0.0f, avail.y), true);
	DrawAnimatorSidePanel(*animatorWorking_, liveAnimator, liveAnimator ? selected : nullptr);
	ImGui::EndChild();

	// Edit mode: the selected state previews on the selected object
	if (sceneState_ == EditorSceneState::Edit) {
		animatorPreviewObject_ = liveAnimator ? selected->id : 0;
		animatorPreviewState_ = animatorSelectedState_;
	}

	// Drags commit when the mouse is released; widget edits commit right away
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		animatorDraggingNode_ = false;
		AnimatorCommit();
	}
	ImGui::End();
#endif
}

void EditorSuite::DrawAnimatorCanvas(AnimatorController& c, const AnimatorRuntime* live, const ImVec2& size)
{
#ifdef USE_IMGUI
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 canvasSize((std::max)(size.x, 50.0f), (std::max)(size.y, 50.0f));
	const ImVec2 canvasMax = Add(origin, canvasSize);
	ImGui::InvisibleButton("##canvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool hovered = ImGui::IsItemHovered();
	const ImGuiIO& io = ImGui::GetIO();
	const ImVec2 mouse = io.MousePos;
	const float zoom = animatorZoom_;
	const ImVec2 pan(animatorPan_.x, animatorPan_.y);

	auto toScreen = [&](Vector2 p) { return ImVec2(origin.x + pan.x + p.x * zoom, origin.y + pan.y + p.y * zoom); };
	auto toCanvas = [&](ImVec2 s) { return Vector2((s.x - origin.x - pan.x) / zoom, (s.y - origin.y - pan.y) / zoom); };

	// Pan (middle drag, Alt + left drag) and zoom around the mouse
	if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)))) {
		animatorPan_.x += io.MouseDelta.x;
		animatorPan_.y += io.MouseDelta.y;
	}
	if (hovered && io.MouseWheel != 0.0f) {
		const Vector2 before = toCanvas(mouse);
		animatorZoom_ = std::clamp(animatorZoom_ * (io.MouseWheel > 0.0f ? 1.1f : 1.0f / 1.1f), 0.35f, 2.5f);
		animatorPan_.x = mouse.x - origin.x - before.x * animatorZoom_;
		animatorPan_.y = mouse.y - origin.y - before.y * animatorZoom_;
	}

	dl->PushClipRect(origin, canvasMax, true);
	dl->AddRectFilled(origin, canvasMax, IM_COL32(32, 32, 36, 255));
	const float grid = 32.0f * zoom;
	for (float x = std::fmod(pan.x, grid); x < canvasSize.x; x += grid) dl->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, canvasMax.y), IM_COL32(48, 48, 54, 255));
	for (float y = std::fmod(pan.y, grid); y < canvasSize.y; y += grid) dl->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(canvasMax.x, origin.y + y), IM_COL32(48, 48, 54, 255));

	ImFont* font = ImGui::GetFont();
	const float fontSize = ImGui::GetFontSize() * zoom; // ImGui 1.92 bakes glyphs per size: text stays sharp
	const ImVec2 half(kNodeW * 0.5f * zoom, kNodeH * 0.5f * zoom);
	auto stateCenter = [&](uint32_t id) -> ImVec2 {
		if (id == AnimTransition::kAnyState) return Add(toScreen(c.anyStatePosition), half);
		const AnimState* s = c.FindState(id);
		return s ? Add(toScreen(s->position), half) : ImVec2(0, 0);
	};

	// Link geometry: centre to centre, clipped at the node borders, shifted sideways
	struct Link { ImVec2 a, b; };
	auto linkGeometry = [&](ImVec2 ca, ImVec2 cb) {
		const ImVec2 dir = Sub(cb, ca);
		const float len = Len(dir);
		const ImVec2 n = len > 0.0f ? ImVec2(-dir.y / len, dir.x / len) : ImVec2(0, 0);
		const ImVec2 offset = Mul(n, kLinkOffset * zoom);
		const ImVec2 a = ClipToRect(ca, half, cb);
		const ImVec2 b = ClipToRect(cb, half, ca);
		return Link{ Add(a, offset), Add(b, offset) };
	};
	auto drawArrow = [&](const Link& l, ImU32 col, float thickness) {
		dl->AddLine(l.a, l.b, col, thickness);
		const ImVec2 dir = Sub(l.b, l.a);
		const float len = Len(dir);
		if (len < 1.0f) return;
		const ImVec2 d = Mul(dir, 1.0f / len);
		const ImVec2 n(-d.y, d.x);
		const ImVec2 mid = Add(l.a, Mul(dir, 0.5f));
		const float s = 7.0f * zoom;
		dl->AddTriangleFilled(Add(mid, Mul(d, s)), Add(Sub(mid, Mul(d, s)), Mul(n, s)), Sub(Sub(mid, Mul(d, s)), Mul(n, s)), col);
	};

	// Entry -> entry state
	const ImVec2 entryCenter = Add(toScreen(c.entryPosition), half);
	if (c.FindState(c.EntryStateId())) drawArrow(linkGeometry(entryCenter, stateCenter(c.EntryStateId())), IM_COL32(230, 150, 60, 255), 2.0f);

	// Transitions
	int hoveredLink = -1;
	for (int i = 0; i < static_cast<int>(c.transitions.size()); ++i) {
		const AnimTransition& t = c.transitions[i];
		const Link l = linkGeometry(stateCenter(t.from), stateCenter(t.to));
		const bool selectedLink = (animatorSelectedTransition_ == i);
		const bool usable = AnimatorController::IsUsable(t);
		ImU32 col = selectedLink ? IM_COL32(90, 170, 255, 255) : (usable ? IM_COL32(200, 200, 205, 255) : IM_COL32(200, 90, 80, 255));
		drawArrow(l, col, selectedLink ? 3.0f : 2.0f);
		if (live && live->activeTransition == i && live->InTransition()) {
			// Crossfade progress: a dot travelling along the arrow
			const float k = live->transitionTime / (std::max)(live->transitionDuration, 1e-4f);
			dl->AddCircleFilled(Add(l.a, Mul(Sub(l.b, l.a), k)), 5.0f * zoom, IM_COL32(120, 255, 140, 255));
		}
		if (hovered && hoveredLink < 0 && DistanceToSegment(mouse, l.a, l.b) < kLinkPickPx) hoveredLink = i;
	}

	// Nodes (drawn after links so they cover the line ends); hit-test nodes before links
	uint32_t hoveredState = 0;
	bool hoveredEntry = false, hoveredAny = false;
	auto drawNode = [&](Vector2 pos, const char* label, ImU32 fill, ImU32 border, float borderThickness, float progress) {
		const ImVec2 p0 = toScreen(pos);
		const ImVec2 p1 = Add(p0, Mul(half, 2.0f));
		dl->AddRectFilled(p0, p1, fill, 6.0f * zoom);
		dl->AddRect(p0, p1, border, 6.0f * zoom, 0, borderThickness);
		if (progress >= 0.0f) {
			dl->AddRectFilled(ImVec2(p0.x + 4.0f * zoom, p1.y - 5.0f * zoom), ImVec2(p0.x + 4.0f * zoom + (p1.x - p0.x - 8.0f * zoom) * progress, p1.y - 3.0f * zoom), IM_COL32(120, 255, 140, 255));
		}
		const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
		dl->AddText(font, fontSize, ImVec2(p0.x + (p1.x - p0.x - ts.x) * 0.5f, p0.y + (p1.y - p0.y - ts.y) * 0.5f), IM_COL32(235, 235, 240, 255), label);
		return hovered && mouse.x >= p0.x && mouse.x <= p1.x && mouse.y >= p0.y && mouse.y <= p1.y;
	};
	if (drawNode(c.entryPosition, Tr("Entry"), IM_COL32(150, 90, 30, 255), animatorSelectedEntry_ ? IM_COL32(255, 200, 120, 255) : IM_COL32(230, 150, 60, 255), 1.5f, -1.0f)) hoveredEntry = true;
	if (drawNode(c.anyStatePosition, Tr("Any State"), IM_COL32(30, 110, 110, 255), animatorSelectedState_ == AnimTransition::kAnyState ? IM_COL32(150, 240, 240, 255) : IM_COL32(60, 180, 180, 255), 1.5f, -1.0f)) hoveredAny = true;
	for (const AnimState& s : c.states) {
		const bool isLive = live && live->currentState == s.id;
		const bool isSel = animatorSelectedState_ == s.id;
		const bool isEntry = s.id == c.EntryStateId();
		const ImU32 fill = isLive ? IM_COL32(40, 120, 60, 255) : IM_COL32(62, 62, 70, 255);
		const ImU32 border = isSel ? IM_COL32(90, 170, 255, 255) : (isEntry ? IM_COL32(230, 150, 60, 255) : IM_COL32(110, 110, 120, 255));
		float progress = -1.0f;
		if (isLive && selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) {
			const GameObject& so = gameObjects_[selectedObjectIndex_];
			if (so.editObject3d && so.editObject3d->GetModel()) {
				const float n = so.editObject3d->GetAnimationPlayer().NormalizedTime(so.editObject3d->GetModel());
				progress = s.loop ? n - std::floor(n) : std::clamp(n, 0.0f, 1.0f);
			}
		}
		if (drawNode(s.position, s.name.c_str(), fill, border, isSel ? 2.5f : 1.5f, progress)) hoveredState = s.id;
	}

	// New transition being dragged
	if (animatorLinking_) {
		const ImVec2 from = stateCenter(animatorLinkFrom_);
		dl->AddLine(from, mouse, IM_COL32(90, 170, 255, 255), 2.0f);
		if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) animatorLinking_ = false;
	}
	dl->PopClipRect();

	// ---- Interaction ----
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) {
		if (animatorLinking_) {
			if (hoveredState != 0) {
				AnimTransition t;
				t.from = animatorLinkFrom_;
				t.to = hoveredState;
				t.hasExitTime = (t.from != AnimTransition::kAnyState); // a usable default: play the clip, then move on
				c.transitions.push_back(t);
				animatorSelectedTransition_ = static_cast<int>(c.transitions.size()) - 1;
				animatorSelectedState_ = 0;
				animatorSelectedEntry_ = false;
				animatorDirty_ = true;
			}
			animatorLinking_ = false;
		} else {
			animatorSelectedEntry_ = hoveredEntry;
			animatorSelectedState_ = hoveredState != 0 ? hoveredState : (hoveredAny ? AnimTransition::kAnyState : 0);
			animatorSelectedTransition_ = (hoveredState == 0 && !hoveredEntry && !hoveredAny) ? hoveredLink : -1;
		}
	}
	// Dragging the selected node
	if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && !io.KeyAlt && !animatorLinking_ &&
		(animatorSelectedState_ != 0 || animatorSelectedEntry_) && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		const Vector2 delta(io.MouseDelta.x / zoom, io.MouseDelta.y / zoom);
		Vector2* target = nullptr;
		if (animatorSelectedEntry_) target = &c.entryPosition;
		else if (animatorSelectedState_ == AnimTransition::kAnyState) target = &c.anyStatePosition;
		else if (AnimState* s = c.FindState(animatorSelectedState_)) target = &s->position;
		if (target && (delta.x != 0.0f || delta.y != 0.0f)) {
			target->x += delta.x;
			target->y += delta.y;
			animatorDirty_ = true;
			animatorDraggingNode_ = true;
		}
	}

	// Context menus
	static Vector2 sMenuCanvasPos;
	if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !animatorLinking_ && !ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
		sMenuCanvasPos = toCanvas(mouse);
		if (hoveredState != 0 || hoveredAny) {
			animatorSelectedState_ = hoveredState != 0 ? hoveredState : AnimTransition::kAnyState;
			animatorSelectedTransition_ = -1;
			ImGui::OpenPopup("##AnimatorNodeMenu");
		} else if (hoveredLink >= 0) {
			animatorSelectedTransition_ = hoveredLink;
			animatorSelectedState_ = 0;
			ImGui::OpenPopup("##AnimatorLinkMenu");
		} else if (!hoveredEntry) {
			ImGui::OpenPopup("##AnimatorCanvasMenu");
		}
	}
	if (ImGui::BeginPopup("##AnimatorCanvasMenu")) {
		if (ImGui::MenuItem(TrId("Create State"))) {
			const std::string name = UniqueName("New State", [&](const std::string& n) { return c.FindStateByName(n) != nullptr; });
			AnimState& s = c.AddState(name, { sMenuCanvasPos.x - kNodeW * 0.5f, sMenuCanvasPos.y - kNodeH * 0.5f });
			animatorSelectedState_ = s.id;
			animatorSelectedTransition_ = -1;
			animatorDirty_ = true;
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("##AnimatorNodeMenu")) {
		if (ImGui::MenuItem(TrId("Make Transition"))) {
			animatorLinking_ = true;
			animatorLinkFrom_ = animatorSelectedState_;
		}
		if (animatorSelectedState_ != AnimTransition::kAnyState) {
			if (ImGui::MenuItem(TrId("Set as Entry State"))) {
				c.entryState = animatorSelectedState_;
				animatorDirty_ = true;
			}
			if (ImGui::MenuItem(TrId("Delete State"))) {
				c.RemoveState(animatorSelectedState_);
				animatorSelectedState_ = 0;
				animatorDirty_ = true;
			}
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("##AnimatorLinkMenu")) {
		if (ImGui::MenuItem(TrId("Delete Transition")) && animatorSelectedTransition_ >= 0 && animatorSelectedTransition_ < static_cast<int>(c.transitions.size())) {
			c.transitions.erase(c.transitions.begin() + animatorSelectedTransition_);
			animatorSelectedTransition_ = -1;
			animatorDirty_ = true;
		}
		ImGui::EndPopup();
	}

	// Delete key (only while the canvas has focus, so text fields keep their Delete key)
	if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
		if (animatorSelectedTransition_ >= 0 && animatorSelectedTransition_ < static_cast<int>(c.transitions.size())) {
			c.transitions.erase(c.transitions.begin() + animatorSelectedTransition_);
			animatorSelectedTransition_ = -1;
			animatorDirty_ = true;
		} else if (animatorSelectedState_ != 0 && animatorSelectedState_ != AnimTransition::kAnyState) {
			c.RemoveState(animatorSelectedState_);
			animatorSelectedState_ = 0;
			animatorDirty_ = true;
		}
	}
#else
	(void)c; (void)live; (void)size;
#endif
}

void EditorSuite::DrawAnimatorSidePanel(AnimatorController& c, AnimatorComponent* liveAnimator, GameObject* liveObject)
{
#ifdef USE_IMGUI
	auto edited = [&](bool changed) { if (changed) animatorDirty_ = true; };

	// ---- Selection ----
	if (AnimState* s = (animatorSelectedState_ != AnimTransition::kAnyState) ? c.FindState(animatorSelectedState_) : nullptr) {
		ImGui::SeparatorText(Tr("State"));
		std::string name = s->name;
		if (InputString(TrId("Name##State"), name) && !name.empty() && !c.FindStateByName(name)) {
			s->name = name;
			animatorDirty_ = true;
		}
		std::string clipModel = s->clipModel;
		if (InputString(TrId("Clip Model"), clipModel)) { s->clipModel = clipModel; animatorDirty_ = true; }
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RIZE_ASSET_PATH")) {
				s->clipModel = static_cast<const char*>(payload->Data);
				s->clipName.clear();
				s->clipIndex = 0;
				animatorDirty_ = true;
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::SetItemTooltip("%s", Tr("Model (FBX / glTF) that owns the clip. Empty = the object's own model. Drop a file from the Project panel."));

		// Clip list of the clip model (or the live object's model)
		const Model* owner = ResolveClipModel(*s);
		if (!owner && liveObject && liveObject->editObject3d) owner = liveObject->editObject3d->GetModel();
		if (owner && owner->GetAnimationCount() > 0) {
			int current = s->clipName.empty() ? s->clipIndex : owner->FindAnimationIndex(s->clipName);
			const std::string preview = (current >= 0 && current < static_cast<int>(owner->GetAnimationCount()))
				? std::format("{}: {}", current, owner->GetAnimationName(current)) : std::string("-");
			if (ImGui::BeginCombo(TrId("Clip"), preview.c_str())) {
				for (int i = 0; i < static_cast<int>(owner->GetAnimationCount()); ++i) {
					const std::string label = std::format("{}: {} ({:.2f} s)", i, owner->GetAnimationName(i), owner->GetAnimationDuration(i));
					if (ImGui::Selectable(label.c_str(), i == current)) {
						s->clipIndex = i;
						s->clipName = owner->GetAnimationName(i);
						animatorDirty_ = true;
					}
				}
				ImGui::EndCombo();
			}
		} else {
			ImGui::TextDisabled("%s", Tr("No clips: set a Clip Model or select an object with an animated model."));
		}
		edited(ImGui::DragFloat(TrId("Speed##State"), &s->speed, 0.01f, 0.0f, 10.0f, "%.2f"));
		edited(ImGui::Checkbox(TrId("Loop##State"), &s->loop));

		if (ImGui::TreeNode(TrId("Blend Tree"))) {
			ImGui::TextDisabled("%s", Tr("Mixes clips by a float parameter, e.g. speed: idle at 0, walk at 2, run at 5."));
			std::string parameter = s->blendParameter;
			if (InputString(TrId("Parameter##Blend"), parameter)) { s->blendParameter = parameter; animatorDirty_ = true; }
			int removeClip = -1;
			for (int i = 0; i < static_cast<int>(s->blendClips.size()); ++i) {
				AnimBlendClip& clip = s->blendClips[i];
				ImGui::PushID(i);
				std::string model = clip.clipModel;
				if (InputString(TrId("Clip Model##Blend"), model)) { clip.clipModel = model; animatorDirty_ = true; }
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RIZE_ASSET_PATH")) {
						clip.clipModel = static_cast<const char*>(payload->Data);
						animatorDirty_ = true;
					}
					ImGui::EndDragDropTarget();
				}
				std::string name = clip.clipName;
				if (InputString(TrId("Clip Name##Blend"), name)) { clip.clipName = name; animatorDirty_ = true; }
				edited(ImGui::DragFloat(TrId("Threshold"), &clip.threshold, 0.05f));
				if (ImGui::SmallButton(TrId("Remove Clip"))) removeClip = i;
				ImGui::Separator();
				ImGui::PopID();
			}
			if (removeClip >= 0) {
				s->blendClips.erase(s->blendClips.begin() + removeClip);
				animatorDirty_ = true;
			}
			if (ImGui::SmallButton(TrId("Add Clip"))) {
				s->blendClips.push_back({ s->clipModel, s->clipName, s->clipIndex, s->blendClips.empty() ? 0.0f : s->blendClips.back().threshold + 1.0f });
				animatorDirty_ = true;
			}
			ImGui::TreePop();
		}
		if (s->id != c.EntryStateId() && ImGui::Button(TrId("Set as Entry State"))) {
			c.entryState = s->id;
			animatorDirty_ = true;
		}

		ImGui::SeparatorText(Tr("Animation Events"));
		ImGui::TextDisabled("%s", Tr("Scripts receive OnAnimationEvent(name) when the clip passes the time."));
		for (size_t i = 0; i < s->events.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			AnimEvent& e = s->events[i];
			ImGui::SetNextItemWidth(120.0f);
			std::string eventName = e.name;
			if (InputString("##EventName", eventName) && !eventName.empty()) { e.name = eventName; animatorDirty_ = true; }
			ImGui::SameLine();
			ImGui::SetNextItemWidth(110.0f);
			edited(ImGui::SliderFloat("##EventTime", &e.time, 0.0f, 1.0f, "%.2f"));
			ImGui::SameLine();
			if (ImGui::SmallButton("x")) {
				s->events.erase(s->events.begin() + static_cast<long long>(i));
				animatorDirty_ = true;
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
		if (ImGui::SmallButton(TrId("+ Event"))) {
			s->events.push_back({ "event", 0.5f });
			animatorDirty_ = true;
		}
	} else if (animatorSelectedTransition_ >= 0 && animatorSelectedTransition_ < static_cast<int>(c.transitions.size())) {
		AnimTransition& t = c.transitions[animatorSelectedTransition_];
		const AnimState* to = c.FindState(t.to);
		const AnimState* from = c.FindState(t.from);
		ImGui::SeparatorText(Tr("Transition"));
		ImGui::Text(Tr("%s  ->  %s"), t.from == AnimTransition::kAnyState ? Tr("Any State") : (from ? from->name.c_str() : "?"), to ? to->name.c_str() : "?");
		if (!AnimatorController::IsUsable(t)) {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", Tr("Never fires: add a condition or enable Has Exit Time."));
		}
		edited(ImGui::DragFloat(TrId("Fade Duration"), &t.duration, 0.01f, 0.0f, 5.0f, "%.2f s"));
		edited(ImGui::Checkbox(TrId("Has Exit Time"), &t.hasExitTime));
		ImGui::SetItemTooltip("%s", Tr("Wait until the clip reaches Exit Time (normalized; below 1 = that point of every loop)."));
		if (t.hasExitTime) edited(ImGui::DragFloat(TrId("Exit Time"), &t.exitTime, 0.01f, 0.0f, 10.0f, "%.2f"));
		if (t.from == AnimTransition::kAnyState) {
			edited(ImGui::Checkbox(TrId("Can Transition To Self"), &t.canTransitionToSelf));
			ImGui::SetItemTooltip("%s", Tr("Off: an Any State transition is ignored while already in its target state."));
		}
		ImGui::Text(Tr("%s: %d"), Tr("Priority"), animatorSelectedTransition_ + 1);
		ImGui::SameLine();
		if (ImGui::SmallButton("^") && animatorSelectedTransition_ > 0) {
			std::swap(c.transitions[animatorSelectedTransition_], c.transitions[animatorSelectedTransition_ - 1]);
			--animatorSelectedTransition_;
			animatorDirty_ = true;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("v") && animatorSelectedTransition_ + 1 < static_cast<int>(c.transitions.size())) {
			std::swap(c.transitions[animatorSelectedTransition_], c.transitions[animatorSelectedTransition_ + 1]);
			++animatorSelectedTransition_;
			animatorDirty_ = true;
		}
		AnimTransition& tr = c.transitions[animatorSelectedTransition_];

		ImGui::SeparatorText(Tr("Conditions (all must be true)"));
		for (size_t i = 0; i < tr.conditions.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			AnimCondition& cond = tr.conditions[i];
			ImGui::SetNextItemWidth(110.0f);
			if (ImGui::BeginCombo("##Param", cond.parameter.empty() ? "-" : cond.parameter.c_str())) {
				for (const AnimParameter& p : c.parameters) {
					if (ImGui::Selectable(p.name.c_str(), p.name == cond.parameter)) {
						cond.parameter = p.name;
						cond.op = OpsFor(p.type).front();
						animatorDirty_ = true;
					}
				}
				ImGui::EndCombo();
			}
			const AnimParameter* param = c.FindParameter(cond.parameter);
			if (param) {
				ImGui::SameLine();
				ImGui::SetNextItemWidth(70.0f);
				if (ImGui::BeginCombo("##Op", OpLabel(cond.op))) {
					for (AnimConditionOp op : OpsFor(param->type)) {
						if (ImGui::Selectable(OpLabel(op), op == cond.op)) { cond.op = op; animatorDirty_ = true; }
					}
					ImGui::EndCombo();
				}
				if (param->type == AnimParamType::Float || param->type == AnimParamType::Int) {
					ImGui::SameLine();
					ImGui::SetNextItemWidth(70.0f);
					if (param->type == AnimParamType::Int) {
						int v = static_cast<int>(std::lround(cond.threshold));
						if (ImGui::DragInt("##Threshold", &v, 0.2f)) { cond.threshold = static_cast<float>(v); animatorDirty_ = true; }
					} else {
						edited(ImGui::DragFloat("##Threshold", &cond.threshold, 0.01f, 0.0f, 0.0f, "%.2f"));
					}
				}
			} else {
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", Tr("(missing)"));
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("x")) {
				tr.conditions.erase(tr.conditions.begin() + static_cast<long long>(i));
				animatorDirty_ = true;
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
		if (ImGui::SmallButton(TrId("+ Condition"))) {
			AnimCondition cond;
			if (!c.parameters.empty()) {
				cond.parameter = c.parameters.front().name;
				cond.op = OpsFor(c.parameters.front().type).front();
			}
			tr.conditions.push_back(cond);
			animatorDirty_ = true;
		}
		if (ImGui::Button(TrId("Delete Transition"))) {
			c.transitions.erase(c.transitions.begin() + animatorSelectedTransition_);
			animatorSelectedTransition_ = -1;
			animatorDirty_ = true;
		}
	} else {
		ImGui::TextDisabled("%s", Tr("Select a state or a transition."));
	}

	// ---- Parameters ----
	ImGui::SeparatorText(Tr("Parameters"));
	const bool showLive = liveAnimator && liveAnimator->runtime.started && sceneState_ != EditorSceneState::Edit;
	if (showLive) ImGui::TextDisabled("%s", Tr("Live values of the selected object (scripts change them too)."));
	for (size_t i = 0; i < c.parameters.size(); ++i) {
		ImGui::PushID(static_cast<int>(i) + 1000);
		AnimParameter& p = c.parameters[i];
		ImGui::SetNextItemWidth(110.0f);
		std::string name = p.name;
		if (InputString("##ParamName", name, ImGuiInputTextFlags_EnterReturnsTrue) && !name.empty() && !c.FindParameter(name)) {
			// Conditions follow the rename
			for (AnimTransition& t : c.transitions)
				for (AnimCondition& cond : t.conditions)
					if (cond.parameter == p.name) cond.parameter = name;
			p.name = name;
			animatorDirty_ = true;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s", ParamTypeName(p.type));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		if (showLive) {
			float v = liveAnimator->runtime.GetValue(p.name);
			bool changed = false;
			if (p.type == AnimParamType::Float) changed = ImGui::DragFloat("##Value", &v, 0.01f);
			else if (p.type == AnimParamType::Int) { int iv = static_cast<int>(v); changed = ImGui::DragInt("##Value", &iv); v = static_cast<float>(iv); }
			else if (p.type == AnimParamType::Bool) { bool b = v != 0.0f; changed = ImGui::Checkbox("##Value", &b); v = b ? 1.0f : 0.0f; }
			else if (ImGui::SmallButton(v != 0.0f ? Tr("(set)") : Tr("Fire"))) { v = 1.0f; changed = true; }
			if (changed) liveAnimator->runtime.SetValue(c, p.name, v);
		} else {
			if (p.type == AnimParamType::Float) edited(ImGui::DragFloat("##Default", &p.defaultValue, 0.01f));
			else if (p.type == AnimParamType::Int) { int iv = static_cast<int>(p.defaultValue); if (ImGui::DragInt("##Default", &iv)) { p.defaultValue = static_cast<float>(iv); animatorDirty_ = true; } }
			else if (p.type == AnimParamType::Bool) { bool b = p.defaultValue != 0.0f; if (ImGui::Checkbox("##Default", &b)) { p.defaultValue = b ? 1.0f : 0.0f; animatorDirty_ = true; } }
			else ImGui::TextDisabled("-");
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("x")) {
			c.parameters.erase(c.parameters.begin() + static_cast<long long>(i));
			animatorDirty_ = true;
			ImGui::PopID();
			break;
		}
		ImGui::PopID();
	}
	auto addParam = [&](const char* label, AnimParamType type, const char* base) {
		if (ImGui::SmallButton(label)) {
			const std::string name = UniqueName(base, [&](const std::string& n) { return c.FindParameter(n) != nullptr; });
			c.parameters.push_back({ name, type, 0.0f });
			animatorDirty_ = true;
		}
	};
	addParam("+ Float", AnimParamType::Float, "speed");
	ImGui::SameLine();
	addParam("+ Int", AnimParamType::Int, "number");
	ImGui::SameLine();
	addParam("+ Bool", AnimParamType::Bool, "flag");
	ImGui::SameLine();
	addParam("+ Trigger", AnimParamType::Trigger, "trigger");
#else
	(void)c; (void)liveAnimator; (void)liveObject;
#endif
}

// ---------------------------------------------------------------------------
// Skeleton overlay (Scene view, "Bones" toggle): joints of the selected skinned object
// ---------------------------------------------------------------------------
void EditorSuite::DrawSkeletonOverlay(const ImVec2& vpPos, const ImVec2& vpSize)
{
#ifdef USE_IMGUI
	if (selectedObjectIndex_ < 0 || selectedObjectIndex_ >= static_cast<int>(gameObjects_.size())) return;
	const GameObject& obj = gameObjects_[selectedObjectIndex_];
	if (!obj.editObject3d) return;
	const Model* model = obj.editObject3d->GetModel();
	if (!model || !model->HasBones()) return;

	LocalPose pose;
	obj.editObject3d->GetAnimationPlayer().EvaluatePose(*model, pose);
	std::vector<Vector3> joints;
	std::vector<int> parents;
	std::vector<bool> isBone;
	model->GetJointPositions(pose, joints, parents, isBone);
	const Matrix4x4& world = obj.editObject3d->worldTransform.matWorld;

	std::vector<ImVec2> screen(joints.size());
	std::vector<bool> visible(joints.size(), false);
	for (size_t i = 0; i < joints.size(); ++i) {
		float depth = 0.0f;
		visible[i] = WorldToScreen(TransformUtil::TransformPoint(joints[i], world), editorCamera_, vpPos, vpSize, screen[i], depth);
	}
	ImDrawList* dl = ImGui::GetWindowDrawList();
	for (size_t i = 0; i < joints.size(); ++i) {
		if (!isBone[i] || !visible[i]) continue;
		const int p = parents[i];
		if (p >= 0 && isBone[p] && visible[p]) dl->AddLine(screen[p], screen[i], IM_COL32(255, 210, 80, 220), 2.0f);
		dl->AddCircleFilled(screen[i], 3.0f, IM_COL32(255, 240, 160, 255));
	}
#else
	(void)vpPos; (void)vpSize;
#endif
}
