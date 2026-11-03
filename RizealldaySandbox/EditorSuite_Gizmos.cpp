#include "EditorSuiteInternal.h"

bool EditorSuite::WorldToScreen(const Vector3& worldPos, const Camera& cam, const ImVec2& vpPos, const ImVec2& vpSize, ImVec2& outScreenPos, float& outDepth)
{
	const Matrix4x4& vp = cam.GetViewProjectionMatrix();
	Vector4 clip = {
		worldPos.x * vp.m[0][0] + worldPos.y * vp.m[1][0] + worldPos.z * vp.m[2][0] + vp.m[3][0],
		worldPos.x * vp.m[0][1] + worldPos.y * vp.m[1][1] + worldPos.z * vp.m[2][1] + vp.m[3][1],
		worldPos.x * vp.m[0][2] + worldPos.y * vp.m[1][2] + worldPos.z * vp.m[2][2] + vp.m[3][2],
		worldPos.x * vp.m[0][3] + worldPos.y * vp.m[1][3] + worldPos.z * vp.m[2][3] + vp.m[3][3]
	};

	if (clip.w <= 0.01f) {
		return false;
	}

	float invW = 1.0f / clip.w;
	float ndcX = clip.x * invW;
	float ndcY = clip.y * invW;

	outScreenPos.x = vpPos.x + (ndcX * 0.5f + 0.5f) * vpSize.x;
	outScreenPos.y = vpPos.y + (-ndcY * 0.5f + 0.5f) * vpSize.y;
	outDepth = clip.w;
	return true;
}

void EditorSuite::DrawSceneViewOverlays(const ImVec2& vpPos, const ImVec2& vpSize)
{
#ifdef USE_IMGUI
	// The ground grid is rendered in 3D with depth testing (see RenderViewports), not as an overlay
	if (showWireframe_ || isEditingColliderBounds_) DrawColliderWireframes(vpPos, vpSize);
	if (showSkeletonOverlay_) DrawSkeletonOverlay(vpPos, vpSize);

	if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) {
		const auto& sel = gameObjects_[selectedObjectIndex_];
		if (sel.type == GameObjectType::Camera || sel.hasCamera) {
			DrawCameraFrustum(vpPos, vpSize, sel);
		} else if (sel.type == GameObjectType::DirectionalLight || sel.type == GameObjectType::PointLight || sel.type == GameObjectType::SpotLight || sel.hasLight) {
			DrawLightGizmo(vpPos, vpSize, sel);
		}
	}

	if (showGizmos_ && !terrainSculpt_.active && selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) {
		DrawTransformGizmo(vpPos, vpSize);
	}

	DrawFloatingToolStrip(vpPos, vpSize);
	DrawOrientationCompass(vpPos, vpSize);
	DrawFloatingBottomBar(vpPos, vpSize);
#endif
}

void EditorSuite::DrawFloatingToolStrip(const ImVec2& vpPos, const ImVec2& vpSize)
{
#ifdef USE_IMGUI
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 stripPos = ImVec2(vpPos.x + 12.0f, vpPos.y + 12.0f);
	float btnW = 28.0f;
	float btnH = 24.0f;

	drawList->AddRectFilled(stripPos, ImVec2(stripPos.x + btnW * 6.0f + 20.0f, stripPos.y + btnH + 8.0f), IM_COL32(28, 28, 30, 220), 4.0f);

	const char* toolLabels[] = { "Q", "W", "E", "R", "T", "C" };
	const char* toolTips[] = {
		"Hand / Pan Tool (Q)",
		"Move / Translate Gizmo (W)",
		"Rotate Gizmo (E)",
		"Scale Gizmo (R)",
		"Rect / Bounds Transform Tool (T)",
		"Edit Box Collider Bounds (C)"
	};
	GizmoType toolTypes[] = { GizmoType::Hand, GizmoType::Move, GizmoType::Rotate, GizmoType::Scale, GizmoType::Bounds, GizmoType::Collider };

	for (int t = 0; t < 6; ++t) {
		ImVec2 bMin = ImVec2(stripPos.x + 4.0f + t * (btnW + 2.0f), stripPos.y + 4.0f);
		ImVec2 bMax = ImVec2(bMin.x + btnW, bMin.y + btnH);

		bool active = (currentGizmo_ == toolTypes[t]);
		if (t == 5 && isEditingColliderBounds_) active = true;

		ImU32 col = active ? ImGui::GetColorU32(ImGuiCol_CheckMark) : IM_COL32(45, 45, 48, 255);
		drawList->AddRectFilled(bMin, bMax, col, 3.0f);
		const ImVec2 textSize = ImGui::CalcTextSize(toolLabels[t]);
		drawList->AddText(ImVec2(bMin.x + (btnW - textSize.x) * 0.5f, bMin.y + (btnH - textSize.y) * 0.5f), IM_COL32(240, 240, 240, 255), toolLabels[t]);

		ImGui::SetCursorScreenPos(bMin);
		if (ImGui::InvisibleButton(std::format("##ToolStripBtn{}", t).c_str(), ImVec2(btnW, btnH))) {
			if (t == 5) isEditingColliderBounds_ = !isEditingColliderBounds_;
			else currentGizmo_ = toolTypes[t];
		}
		ImGui::SetItemTooltip(toolTips[t]);
	}
#endif
}

void EditorSuite::DrawOrientationCompass(const ImVec2& vpPos, const ImVec2& vpSize)
{
#ifdef USE_IMGUI
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 center = ImVec2(vpPos.x + vpSize.x - 38.0f, vpPos.y + 38.0f);
	float radius = 24.0f;

	drawList->AddCircleFilled(center, radius, IM_COL32(28, 28, 30, 200), 24);

	Vector3 right = { std::cos(editorCamera_.rotation.y), 0.0f, -std::sin(editorCamera_.rotation.y) };
	Vector3 forward = { std::sin(editorCamera_.rotation.y), 0.0f, std::cos(editorCamera_.rotation.y) };

	ImVec2 xTip = ImVec2(center.x + right.x * 18.0f, center.y - right.z * 18.0f);
	ImVec2 zTip = ImVec2(center.x + forward.x * 18.0f, center.y - forward.z * 18.0f);

	drawList->AddLine(center, xTip, IM_COL32(220, 60, 60, 255), 2.0f);
	drawList->AddText(ImVec2(xTip.x - 3.0f, xTip.y - 7.0f), IM_COL32(255, 100, 100, 255), "X");

	drawList->AddLine(center, zTip, IM_COL32(60, 120, 240, 255), 2.0f);
	drawList->AddText(ImVec2(zTip.x - 3.0f, zTip.y - 7.0f), IM_COL32(100, 160, 255, 255), "Z");
#endif
}

void EditorSuite::DrawFloatingBottomBar(const ImVec2& vpPos, const ImVec2& vpSize)
{
#ifdef USE_IMGUI
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const float barH = ImGui::GetFontSize() + 10.0f;
	ImVec2 barPos = ImVec2(vpPos.x + 12.0f, vpPos.y + vpSize.y - barH - 10.0f);

	// Draw the widgets first (channel 1), then size the background (channel 0) to fit them
	drawList->ChannelsSplit(2);
	drawList->ChannelsSetCurrent(1);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 4.0f));
	ImGui::SetCursorScreenPos(ImVec2(barPos.x + 8.0f, barPos.y + 3.0f));
	ImGui::BeginGroup();
	ImGui::Checkbox(TrId("Grid"), &showGrid_);
	ImGui::SetItemTooltip(Tr("Toggle 3D ground plane grid"));
	ImGui::SameLine();
	ImGui::Checkbox(TrId("Gizmos"), &showGizmos_);
	ImGui::SetItemTooltip(Tr("Toggle 3D transform gizmos"));
	ImGui::SameLine();
	ImGui::Checkbox(TrId("Wire"), &showWireframe_);
	ImGui::SetItemTooltip(Tr("Toggle collider wireframe rendering"));
	ImGui::SameLine();
	ImGui::Checkbox(TrId("Bones"), &showSkeletonOverlay_);
	ImGui::SetItemTooltip("%s", Tr("Show the skeleton of the selected animated object"));
	ImGui::SameLine();
	ImGui::Checkbox(TrId("Light"), &enableLighting_);
	ImGui::SetItemTooltip(Tr("Toggle viewport scene lighting"));
	ImGui::SameLine();
	ImGui::Checkbox(TrId("Cull"), &enableFrustumCulling_);
	ImGui::SetItemTooltip(Tr("Toggle view frustum AABB culling"));
	ImGui::SameLine();
	ImGui::TextDisabled(Tr("Visible %d  Culled %d"), lastVisibleObjects_, lastCulledObjects_);
	ImGui::EndGroup();
	ImGui::PopStyleVar(2);

	drawList->ChannelsSetCurrent(0);
	const float barRight = ImGui::GetItemRectMax().x + 8.0f;
	drawList->AddRectFilled(barPos, ImVec2(barRight, barPos.y + barH), IM_COL32(20, 20, 22, 215), 5.0f);
	drawList->ChannelsMerge();
#endif
}

void EditorSuite::DrawTransformGizmo(const ImVec2& vpPos, const ImVec2& vpSize)
{
#ifdef USE_IMGUI
	if (selectedObjectIndex_ < 0 || selectedObjectIndex_ >= static_cast<int>(gameObjects_.size())) return;
	auto& sel = gameObjects_[selectedObjectIndex_];
	// Whatever this function does to the primary, the rest of the selection follows when it returns
	struct FollowPrimary {
		EditorSuite& suite;
		uint32_t id;
		Vector3 before;
		Vector3 rotationBefore;
		Vector3 scaleBefore;
		~FollowPrimary() {
			const GameObject* primary = suite.FindGameObjectById(id, false);
			if (!primary) return;
			const auto ratio = [](float now, float was) { return was != 0.0f ? now / was : 1.0f; };
			suite.RotateScaleExtraSelection(primary->rotation - rotationBefore,
				{ ratio(primary->scale.x, scaleBefore.x), ratio(primary->scale.y, scaleBefore.y), ratio(primary->scale.z, scaleBefore.z) });
			const Vector3 local = primary->translation - before;
			const GameObject* parent = primary->parentId >= 0 ? suite.FindGameObjectById(static_cast<uint32_t>(primary->parentId), false) : nullptr;
			suite.MoveExtraSelection(parent ? TransformUtil::TransformDirection(local, parent->worldMatrix) : local);
		}
	} follow{ *this, sel.id, sel.translation, sel.rotation, sel.scale };

	ImVec2 originScreen; float originDepth;
	Vector3 selWorldPos{};
	{
		Quaternion selWorldRot = Quaternion::Identity();
		Vector3 selWorldScale{};
		GetWorldTRS(sel, selWorldPos, selWorldRot, selWorldScale);
	}
	if (!WorldToScreen(selWorldPos, editorCamera_, vpPos, vpSize, originScreen, originDepth)) return;

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	float axisLength = 55.0f;

	Vector3 xTarget = selWorldPos + Vector3{ 1.0f, 0.0f, 0.0f };
	Vector3 yTarget = selWorldPos + Vector3{ 0.0f, 1.0f, 0.0f };
	Vector3 zTarget = selWorldPos + Vector3{ 0.0f, 0.0f, 1.0f };

	ImVec2 xScreen, yScreen, zScreen; float xd, yd, zd;
	if (!WorldToScreen(xTarget, editorCamera_, vpPos, vpSize, xScreen, xd)) return;
	if (!WorldToScreen(yTarget, editorCamera_, vpPos, vpSize, yScreen, yd)) return;
	if (!WorldToScreen(zTarget, editorCamera_, vpPos, vpSize, zScreen, zd)) return;

	ImVec2 dirX = ImVec2(xScreen.x - originScreen.x, xScreen.y - originScreen.y);
	ImVec2 dirY = ImVec2(yScreen.x - originScreen.x, yScreen.y - originScreen.y);
	ImVec2 dirZ = ImVec2(zScreen.x - originScreen.x, zScreen.y - originScreen.y);

	float lenX = std::sqrt(dirX.x * dirX.x + dirX.y * dirX.y);
	float lenY = std::sqrt(dirY.x * dirY.x + dirY.y * dirY.y);
	float lenZ = std::sqrt(dirZ.x * dirZ.x + dirZ.y * dirZ.y);

	if (lenX > 0.01f) { dirX.x = dirX.x / lenX * axisLength; dirX.y = dirX.y / lenX * axisLength; }
	if (lenY > 0.01f) { dirY.x = dirY.x / lenY * axisLength; dirY.y = dirY.y / lenY * axisLength; }
	if (lenZ > 0.01f) { dirZ.x = dirZ.x / lenZ * axisLength; dirZ.y = dirZ.y / lenZ * axisLength; }

	ImVec2 tipX = ImVec2(originScreen.x + dirX.x, originScreen.y + dirX.y);
	ImVec2 tipY = ImVec2(originScreen.x + dirY.x, originScreen.y + dirY.y);
	ImVec2 tipZ = ImVec2(originScreen.x + dirZ.x, originScreen.y + dirZ.y);

	ImGuiIO& io = ImGui::GetIO();
	ImVec2 mousePos = io.MousePos;

	auto DistToSegment = [](const ImVec2& p, const ImVec2& a, const ImVec2& b) -> float {
		float l2 = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
		if (l2 < 0.001f) return std::sqrt((p.x - a.x) * (p.x - a.x) + (p.y - a.y) * (p.y - a.y));
		float t = std::clamp(((p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y)) / l2, 0.0f, 1.0f);
		ImVec2 proj = ImVec2(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y));
		return std::sqrt((p.x - proj.x) * (p.x - proj.x) + (p.y - proj.y) * (p.y - proj.y));
	};

	if (!isDraggingGizmo_) {
		activeGizmoAxis_ = GizmoAxis::None;
		if (DistToSegment(mousePos, originScreen, tipX) < 8.0f) activeGizmoAxis_ = GizmoAxis::X;
		else if (DistToSegment(mousePos, originScreen, tipY) < 8.0f) activeGizmoAxis_ = GizmoAxis::Y;
		else if (DistToSegment(mousePos, originScreen, tipZ) < 8.0f) activeGizmoAxis_ = GizmoAxis::Z;

		if (activeGizmoAxis_ != GizmoAxis::None && io.MouseClicked[0]) {
			isDraggingGizmo_ = true;
			gizmoDragStartMouse_ = mousePos;
			gizmoStartTranslation_ = sel.translation;
			gizmoStartRotation_ = sel.rotation;
			gizmoStartScale_ = sel.scale;
			gizmoStartLocalOffset_ = sel.localOffset;
		}
	} else {
		if (io.MouseDown[0]) {
			float dx = (mousePos.x - gizmoDragStartMouse_.x) * 0.03f;
			float dy = -(mousePos.y - gizmoDragStartMouse_.y) * 0.03f;

			if (currentGizmo_ == GizmoType::Move) {
				if (sel.hasParticleSystem && sel.attachedParentId != -1) {
					int pIdx = -1;
					for (size_t p = 0; p < gameObjects_.size(); ++p) {
						if (gameObjects_[p].id == static_cast<uint32_t>(sel.attachedParentId)) {
							pIdx = static_cast<int>(p);
							break;
						}
					}
					if (pIdx >= 0) {
						if (activeGizmoAxis_ == GizmoAxis::X) sel.localOffset.x = gizmoStartLocalOffset_.x + dx;
						else if (activeGizmoAxis_ == GizmoAxis::Y) sel.localOffset.y = gizmoStartLocalOffset_.y + dy;
						else if (activeGizmoAxis_ == GizmoAxis::Z) sel.localOffset.z = gizmoStartLocalOffset_.z + dx;
						sel.translation = gameObjects_[pIdx].translation + sel.localOffset;
					} else {
						sel.attachedParentId = -1;
						sel.localOffset = { 0.0f, 0.0f, 0.0f };
						if (activeGizmoAxis_ == GizmoAxis::X) sel.translation.x = gizmoStartTranslation_.x + dx;
						else if (activeGizmoAxis_ == GizmoAxis::Y) sel.translation.y = gizmoStartTranslation_.y + dy;
						else if (activeGizmoAxis_ == GizmoAxis::Z) sel.translation.z = gizmoStartTranslation_.z + dx;
					}
				} else if (sel.parentId == -1) {
					if (activeGizmoAxis_ == GizmoAxis::X) sel.translation.x = gizmoStartTranslation_.x + dx;
					else if (activeGizmoAxis_ == GizmoAxis::Y) sel.translation.y = gizmoStartTranslation_.y + dy;
					else if (activeGizmoAxis_ == GizmoAxis::Z) sel.translation.z = gizmoStartTranslation_.z + dx;
				} else {
					// The drag is a world-space delta; convert it to the parent's local space before applying
					Vector3 worldDelta = { 0.0f, 0.0f, 0.0f };
					if (activeGizmoAxis_ == GizmoAxis::X) worldDelta.x = dx;
					else if (activeGizmoAxis_ == GizmoAxis::Y) worldDelta.y = dy;
					else if (activeGizmoAxis_ == GizmoAxis::Z) worldDelta.z = dx;

					Matrix4x4 parentWorld = Mathematics::MakeIdentity4x4();
					if (const GameObject* parentObj = FindGameObjectById(static_cast<uint32_t>(sel.parentId), false)) {
						parentWorld = parentObj->worldMatrix;
					}
					const Vector3 localDelta = TransformUtil::TransformDirection(worldDelta, Mathematics::Inverse(parentWorld));
					sel.translation = gizmoStartTranslation_ + localDelta;
				}
			} else if (currentGizmo_ == GizmoType::Rotate) {
				if (activeGizmoAxis_ == GizmoAxis::X) sel.rotation.x = gizmoStartRotation_.x + dy;
				else if (activeGizmoAxis_ == GizmoAxis::Y) sel.rotation.y = gizmoStartRotation_.y + dx;
				else if (activeGizmoAxis_ == GizmoAxis::Z) sel.rotation.z = gizmoStartRotation_.z + dx;
				sel.rotationQuat = Quaternion::FromEuler(sel.rotation);
			} else if (currentGizmo_ == GizmoType::Scale || currentGizmo_ == GizmoType::Bounds) {
				float delta = dx + dy;
				if (activeGizmoAxis_ == GizmoAxis::X) sel.scale.x = (std::max)(0.01f, gizmoStartScale_.x + delta);
				else if (activeGizmoAxis_ == GizmoAxis::Y) sel.scale.y = (std::max)(0.01f, gizmoStartScale_.y + delta);
				else if (activeGizmoAxis_ == GizmoAxis::Z) sel.scale.z = (std::max)(0.01f, gizmoStartScale_.z + delta);
			}
			isSceneDirty_ = true;
		} else {
			// Gizmo drag ended: push snapshot and mark scene dirty
			isDraggingGizmo_ = false;
			activeGizmoAxis_ = GizmoAxis::None;
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
	}

	ImU32 colX = (activeGizmoAxis_ == GizmoAxis::X) ? IM_COL32(255, 230, 50, 255) : IM_COL32(230, 50, 50, 255);
	drawList->AddLine(originScreen, tipX, colX, (activeGizmoAxis_ == GizmoAxis::X) ? 3.0f : 2.0f);
	drawList->AddCircleFilled(tipX, 4.0f, colX);

	ImU32 colY = (activeGizmoAxis_ == GizmoAxis::Y) ? IM_COL32(255, 230, 50, 255) : IM_COL32(50, 220, 60, 255);
	drawList->AddLine(originScreen, tipY, colY, (activeGizmoAxis_ == GizmoAxis::Y) ? 3.0f : 2.0f);
	drawList->AddCircleFilled(tipY, 4.0f, colY);

	ImU32 colZ = (activeGizmoAxis_ == GizmoAxis::Z) ? IM_COL32(255, 230, 50, 255) : IM_COL32(50, 110, 240, 255);
	drawList->AddLine(originScreen, tipZ, colZ, (activeGizmoAxis_ == GizmoAxis::Z) ? 3.0f : 2.0f);
	drawList->AddCircleFilled(tipZ, 4.0f, colZ);
#endif
}

void EditorSuite::DrawColliderWireframes(const ImVec2& vpPos, const ImVec2& vpSize)
{
#ifdef USE_IMGUI
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	for (const auto& obj : gameObjects_) {
		if (!obj.isActive || !obj.hasBoxCollider || !obj.boxColliderEnabled) continue;

		Vector3 wPos{};
		Quaternion wRot = Quaternion::Identity();
		Vector3 wScale{};
		GetWorldTRS(obj, wPos, wRot, wScale);
		// Same world-space AABB as the physics pass (roots: translation + center; children: through the world matrix)
		const Vector3 boxWorldCenter = (obj.parentId == -1) ? (wPos + obj.boxCenter) : TransformUtil::TransformPoint(obj.boxCenter, obj.worldMatrix);
		Vector3 halfSize = { obj.boxSize.x * wScale.x * 0.5f, obj.boxSize.y * wScale.y * 0.5f, obj.boxSize.z * wScale.z * 0.5f };
		Vector3 minP = boxWorldCenter - halfSize;
		Vector3 maxP = boxWorldCenter + halfSize;

		Vector3 corners[8] = {
			{ minP.x, minP.y, minP.z }, { maxP.x, minP.y, minP.z },
			{ maxP.x, maxP.y, minP.z }, { minP.x, maxP.y, minP.z },
			{ minP.x, minP.y, maxP.z }, { maxP.x, minP.y, maxP.z },
			{ maxP.x, maxP.y, maxP.z }, { minP.x, maxP.y, maxP.z }
		};

		ImVec2 sc[8]; float sd[8]; bool allOk = true;
		for (int i = 0; i < 8; ++i) {
			if (!WorldToScreen(corners[i], editorCamera_, vpPos, vpSize, sc[i], sd[i])) { allOk = false; break; }
		}

		if (allOk) {
			ImU32 col = obj.isColliding ? IM_COL32(255, 50, 50, 220) : IM_COL32(50, 255, 100, 180);
			int edges[12][2] = {
				{0,1},{1,2},{2,3},{3,0},
				{4,5},{5,6},{6,7},{7,4},
				{0,4},{1,5},{2,6},{3,7}
			};
			for (int e = 0; e < 12; ++e) {
				drawList->AddLine(sc[edges[e][0]], sc[edges[e][1]], col, 1.2f);
			}
		}
	}
#endif
}

void EditorSuite::DrawCameraFrustum(const ImVec2& vpPos, const ImVec2& vpSize, const GameObject& camObj)
{
#ifdef USE_IMGUI
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 camPos; float camDepth;
	Vector3 camWorldPos{};
	Quaternion camWorldRot = Quaternion::Identity();
	Vector3 camWorldScale{};
	GetWorldTRS(camObj, camWorldPos, camWorldRot, camWorldScale);
	// Roots keep their exact Euler angles; children derive Euler from the world rotation
	const Vector3 camEuler = (camObj.parentId == -1) ? camObj.rotation : camWorldRot.ToEuler();
	if (!WorldToScreen(camWorldPos, editorCamera_, vpPos, vpSize, camPos, camDepth)) return;

	Vector3 forward = { std::sin(camEuler.y), -std::sin(camEuler.x), std::cos(camEuler.y) };
	Vector3 right = { std::cos(camEuler.y), 0.0f, -std::sin(camEuler.y) };
	Vector3 up = { 0.0f, 1.0f, 0.0f };

	float dist = 4.0f;
	float halfH = dist * std::tan(camObj.fov * 0.5f * 3.14159265f / 180.0f);
	float halfW = halfH * (16.0f / 9.0f);

	Vector3 center = camWorldPos + (forward * dist);
	Vector3 fCorners[4] = {
		center + (right * -halfW) + (up *  halfH),
		center + (right *  halfW) + (up *  halfH),
		center + (right *  halfW) + (up * -halfH),
		center + (right * -halfW) + (up * -halfH)
	};

	ImVec2 sc[4]; float sd[4];
	for (int i = 0; i < 4; ++i) {
		if (WorldToScreen(fCorners[i], editorCamera_, vpPos, vpSize, sc[i], sd[i])) {
			drawList->AddLine(camPos, sc[i], IM_COL32(240, 240, 240, 160), 1.0f);
		}
	}
	for (int i = 0; i < 4; ++i) {
		drawList->AddLine(sc[i], sc[(i + 1) % 4], IM_COL32(255, 255, 255, 200), 1.2f);
	}
#endif
}

void EditorSuite::DrawLightGizmo(const ImVec2& vpPos, const ImVec2& vpSize, const GameObject& lightObj)
{
#ifdef USE_IMGUI
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 lPos; float lDepth;
	Vector3 lightWorldPos{};
	{
		Quaternion lightWorldRot = Quaternion::Identity();
		Vector3 lightWorldScale{};
		GetWorldTRS(lightObj, lightWorldPos, lightWorldRot, lightWorldScale);
	}
	if (!WorldToScreen(lightWorldPos, editorCamera_, vpPos, vpSize, lPos, lDepth)) return;

	ImU32 lightCol = IM_COL32(255, 220, 80, 240);
	drawList->AddCircleFilled(lPos, 6.0f, lightCol);
	drawList->AddCircle(lPos, 7.5f, IM_COL32(40, 40, 40, 200), 16, 1.5f);

	if (lightObj.lightType == GameObject::LightType::Directional) {
		// 1. 2D radiating sun rays around center icon
		for (int k = 0; k < 8; ++k) {
			float angle = k * (3.14159265f / 4.0f);
			ImVec2 rStart = ImVec2(lPos.x + std::cos(angle) * 10.0f, lPos.y + std::sin(angle) * 10.0f);
			ImVec2 rEnd = ImVec2(lPos.x + std::cos(angle) * 17.0f, lPos.y + std::sin(angle) * 17.0f);
			drawList->AddLine(rStart, rEnd, IM_COL32(255, 230, 90, 220), 1.5f);
		}

		// 2. 3D directional sun rays (central ray + 4 surrounding parallel rays)
		Vector3 dir = lightObj.lightDirection;
		float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		if (len > 0.001f) { dir.x /= len; dir.y /= len; dir.z /= len; }
		else { dir = { 0.0f, -1.0f, 0.0f }; }

		Vector3 up = (std::abs(dir.y) < 0.99f) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
		Vector3 u = { dir.y * up.z - dir.z * up.y, dir.z * up.x - dir.x * up.z, dir.x * up.y - dir.y * up.x };
		float ulen = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
		if (ulen > 0.0001f) { u.x /= ulen; u.y /= ulen; u.z /= ulen; }
		Vector3 v = { dir.y * u.z - dir.z * u.y, dir.z * u.x - dir.x * u.z, dir.x * u.y - dir.y * u.x };
		float vlen = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		if (vlen > 0.0001f) { v.x /= vlen; v.y /= vlen; v.z /= vlen; }

		Vector3 offsets[5] = {
			{ 0.0f, 0.0f, 0.0f },
			u * 1.0f,
			u * -1.0f,
			v * 1.0f,
			v * -1.0f
		};

		for (int i = 0; i < 5; ++i) {
			Vector3 rStart = lightWorldPos + offsets[i];
			Vector3 rEnd = rStart + (dir * 3.5f);
			ImVec2 s0, s1; float d0, d1;
			if (WorldToScreen(rStart, editorCamera_, vpPos, vpSize, s0, d0) &&
				WorldToScreen(rEnd, editorCamera_, vpPos, vpSize, s1, d1)) {
				drawList->AddLine(s0, s1, (i == 0) ? IM_COL32(255, 240, 100, 255) : IM_COL32(255, 220, 80, 180), (i == 0) ? 2.0f : 1.2f);
			}
		}
	} else if (lightObj.lightType == GameObject::LightType::Point) {
		// Wireframe radius sphere (XZ, XY, YZ orthogonal circles)
		const int segs = 32;
		float r = (std::max)(0.1f, lightObj.lightRange);
		Vector3 center = lightWorldPos;
		ImU32 sphereCol = IM_COL32(255, 220, 80, 180);

		// 1. Equator circle (XZ plane)
		ImVec2 prevP;
		for (int i = 0; i <= segs; ++i) {
			float theta = i * 2.0f * 3.14159265f / segs;
			Vector3 pt = center + Vector3{ std::cos(theta) * r, 0.0f, std::sin(theta) * r };
			ImVec2 sp; float sdep;
			if (WorldToScreen(pt, editorCamera_, vpPos, vpSize, sp, sdep)) {
				if (i > 0) drawList->AddLine(prevP, sp, sphereCol, 1.2f);
				prevP = sp;
			}
		}

		// 2. Vertical front circle (XY plane)
		for (int i = 0; i <= segs; ++i) {
			float theta = i * 2.0f * 3.14159265f / segs;
			Vector3 pt = center + Vector3{ std::cos(theta) * r, std::sin(theta) * r, 0.0f };
			ImVec2 sp; float sdep;
			if (WorldToScreen(pt, editorCamera_, vpPos, vpSize, sp, sdep)) {
				if (i > 0) drawList->AddLine(prevP, sp, sphereCol, 1.2f);
				prevP = sp;
			}
		}

		// 3. Vertical side circle (YZ plane)
		for (int i = 0; i <= segs; ++i) {
			float theta = i * 2.0f * 3.14159265f / segs;
			Vector3 pt = center + Vector3{ 0.0f, std::sin(theta) * r, std::cos(theta) * r };
			ImVec2 sp; float sdep;
			if (WorldToScreen(pt, editorCamera_, vpPos, vpSize, sp, sdep)) {
				if (i > 0) drawList->AddLine(prevP, sp, sphereCol, 1.2f);
				prevP = sp;
			}
		}
	} else if (lightObj.lightType == GameObject::LightType::Spot) {
		// Wireframe cone matching Distance and Spot Angle
		float dist = (std::max)(0.1f, lightObj.lightRange);
		float angleRad = lightObj.lightSpotAngle * 3.14159265f / 180.0f;
		float baseRadius = dist * std::tan(angleRad);

		Vector3 dir = lightObj.lightDirection;
		float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		if (len > 0.001f) { dir.x /= len; dir.y /= len; dir.z /= len; }
		else { dir = { 0.0f, -1.0f, 0.0f }; }

		Vector3 up = (std::abs(dir.y) < 0.99f) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
		Vector3 u = { dir.y * up.z - dir.z * up.y, dir.z * up.x - dir.x * up.z, dir.x * up.y - dir.y * up.x };
		float ulen = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
		if (ulen > 0.0001f) { u.x /= ulen; u.y /= ulen; u.z /= ulen; }
		Vector3 v = { dir.y * u.z - dir.z * u.y, dir.z * u.x - dir.x * u.z, dir.x * u.y - dir.y * u.x };
		float vlen = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		if (vlen > 0.0001f) { v.x /= vlen; v.y /= vlen; v.z /= vlen; }

		Vector3 apex = lightWorldPos;
		Vector3 baseCenter = apex + (dir * dist);
		const int segs = 32;
		ImU32 coneCol = IM_COL32(255, 220, 80, 200);

		// Outer base circle
		ImVec2 prevP;
		for (int i = 0; i <= segs; ++i) {
			float theta = i * 2.0f * 3.14159265f / segs;
			Vector3 pt = baseCenter + (u * (std::cos(theta) * baseRadius)) + (v * (std::sin(theta) * baseRadius));
			ImVec2 sp; float sdep;
			if (WorldToScreen(pt, editorCamera_, vpPos, vpSize, sp, sdep)) {
				if (i > 0) drawList->AddLine(prevP, sp, coneCol, 1.2f);
				prevP = sp;
			}
		}

		// Inner falloff circle (if falloff start angle < spot angle)
		if (lightObj.lightSpotFalloffStart > 0.0f && lightObj.lightSpotFalloffStart < lightObj.lightSpotAngle) {
			float innerRadius = dist * std::tan(lightObj.lightSpotFalloffStart * 3.14159265f / 180.0f);
			ImU32 innerCol = IM_COL32(255, 240, 140, 130);
			for (int i = 0; i <= segs; ++i) {
				float theta = i * 2.0f * 3.14159265f / segs;
				Vector3 pt = baseCenter + (u * (std::cos(theta) * innerRadius)) + (v * (std::sin(theta) * innerRadius));
				ImVec2 sp; float sdep;
				if (WorldToScreen(pt, editorCamera_, vpPos, vpSize, sp, sdep)) {
					if (i > 0) drawList->AddLine(prevP, sp, innerCol, 1.0f);
					prevP = sp;
				}
			}
		}

		// 4 Side generator lines connecting apex to base cardinal points
		Vector3 cardinalPts[4] = {
			baseCenter + (u * baseRadius),
			baseCenter - (u * baseRadius),
			baseCenter + (v * baseRadius),
			baseCenter - (v * baseRadius)
		};
		for (int i = 0; i < 4; ++i) {
			ImVec2 s0, s1; float d0, d1;
			if (WorldToScreen(apex, editorCamera_, vpPos, vpSize, s0, d0) &&
				WorldToScreen(cardinalPts[i], editorCamera_, vpPos, vpSize, s1, d1)) {
				drawList->AddLine(s0, s1, coneCol, 1.2f);
			}
		}

		// Central axis line
		ImVec2 a0, a1; float da0, da1;
		if (WorldToScreen(apex, editorCamera_, vpPos, vpSize, a0, da0) &&
			WorldToScreen(baseCenter, editorCamera_, vpPos, vpSize, a1, da1)) {
			drawList->AddLine(a0, a1, IM_COL32(255, 240, 100, 140), 1.0f);
		}
	}
#endif
}

