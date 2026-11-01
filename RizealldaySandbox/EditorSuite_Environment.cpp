#include "EditorSuiteInternal.h"

void EditorSuite::RefreshAvailableCubemaps()
{
	availableCubemaps_.clear();
	std::vector<std::string> searchDirs = { "Resources", "Resources/Skybox", "Assets" };
	for (const auto& dir : searchDirs) {
		if (std::filesystem::exists(dir)) {
			for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
				if (entry.is_regular_file() && entry.path().extension() == ".dds") {
					availableCubemaps_.push_back(entry.path().string());
				}
			}
		}
	}
	if (availableCubemaps_.empty()) {
		availableCubemaps_.push_back("Resources/skybox.dds");
	}
}

void EditorSuite::DrawPostProcessSettingsUI()
{
#ifdef USE_IMGUI
	ImGui::Spacing();
	ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%s", Tr("Post Processing (Game view)"));
	ImGui::Separator();

	PostProcessSettings& post = postSettings_;
	bool changed = ImGui::Checkbox(TrId("Enable Post Processing"), &post.enabled);
	ImGui::BeginDisabled(!post.enabled);
	changed |= ImGui::Checkbox(TrId("Tone Mapping (ACES)"), &post.toneMapping);
	changed |= ImGui::DragFloat(TrId("Exposure"), &post.exposure, 0.01f, 0.0f, 10.0f);
	changed |= ImGui::Checkbox(TrId("Bloom"), &post.bloom);
	changed |= ImGui::DragFloat(TrId("Bloom Threshold"), &post.bloomThreshold, 0.01f, 0.0f, 10.0f);
	changed |= ImGui::DragFloat(TrId("Bloom Intensity"), &post.bloomIntensity, 0.01f, 0.0f, 5.0f);
	changed |= ImGui::DragFloat(TrId("Contrast"), &post.contrast, 0.01f, 0.0f, 3.0f);
	changed |= ImGui::DragFloat(TrId("Saturation"), &post.saturation, 0.01f, 0.0f, 3.0f);
	changed |= ImGui::ColorEdit3(TrId("Tint"), &post.tint.x);
	changed |= ImGui::DragFloat(TrId("Vignette"), &post.vignette, 0.01f, 0.0f, 1.0f);
	changed |= ImGui::DragFloat(TrId("Letterbox"), &post.letterbox, 0.002f, 0.0f, 0.45f);
	changed |= ImGui::Checkbox(TrId("FXAA"), &post.fxaa);
	changed |= ImGui::Checkbox(TrId("Ambient Occlusion"), &post.ssao);
	changed |= ImGui::DragFloat(TrId("Occlusion Radius"), &post.ssaoRadius, 0.01f, 0.05f, 5.0f);
	changed |= ImGui::DragFloat(TrId("Occlusion Intensity"), &post.ssaoIntensity, 0.01f, 0.0f, 3.0f);
	ImGui::EndDisabled();

	ImGui::Spacing();
	ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%s", Tr("Lighting Quality"));
	ImGui::Separator();
	changed |= ImGui::SliderInt(TrId("Shadow Cascades"), &post.shadowCascades, 0, 4);
	ImGui::SetItemTooltip("%s", Tr("0 = one shadow map over the whole scene. More = sharper shadows near the camera."));
	changed |= ImGui::DragFloat(TrId("Shadow Distance"), &post.shadowDistance, 1.0f, 5.0f, 1000.0f);
	changed |= ImGui::DragFloat(TrId("Environment Intensity"), &post.environmentIntensity, 0.01f, 0.0f, 10.0f);
	ImGui::SetItemTooltip("%s", Tr("How much the sky lights PBR materials"));
	if (changed) {
		isSceneDirty_ = true;
		PushUndoSnapshot();
	}
#endif
}

void EditorSuite::DrawSkyboxSettingsUI()
{
#ifdef USE_IMGUI
	ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "Skybox / Environment Settings");
	ImGui::Separator();

	const char* modeNames[] = { "Gradient", "Solid Color", "Cubemap" };
	int currentMode = static_cast<int>(skybox_.GetMode());
	if (ImGui::Combo("Background Mode", &currentMode, modeNames, 3)) {
		skybox_.SetMode(static_cast<SkyboxMode>(currentMode));
		if (skybox_.GetMode() == SkyboxMode::Cubemap) {
			if (!skybox_.LoadCubemap(skybox_.GetCubemapPath())) {
				LogMessage("[Skybox] Failed to load cubemap: " + skybox_.GetCubemapPath() + ". Falling back to Gradient mode.", 1);
				skybox_.SetMode(SkyboxMode::Gradient);
			}
		}
		isSceneDirty_ = true;
		PushUndoSnapshot();
	}
	ImGui::SetItemTooltip("Select sky background rendering mode: Gradient, Solid Color, or Cubemap");

	ImGui::Spacing();

	if (skybox_.GetMode() == SkyboxMode::Gradient) {
		Vector4 topCol = skybox_.GetTopColor();
		float topArr[3] = { topCol.x, topCol.y, topCol.z };
		if (ImGui::ColorEdit3("Top Color", topArr)) {
			skybox_.SetTopColor({ topArr[0], topArr[1], topArr[2], topCol.w });
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip("Sky apex / zenith gradient color");

		Vector4 horizCol = skybox_.GetHorizonColor();
		float horizArr[3] = { horizCol.x, horizCol.y, horizCol.z };
		if (ImGui::ColorEdit3("Horizon Color", horizArr)) {
			skybox_.SetHorizonColor({ horizArr[0], horizArr[1], horizArr[2], horizCol.w });
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip("Sky horizon midpoint gradient color");

		Vector4 botCol = skybox_.GetBottomColor();
		float botArr[3] = { botCol.x, botCol.y, botCol.z };
		if (ImGui::ColorEdit3("Ground Color", botArr)) {
			skybox_.SetBottomColor({ botArr[0], botArr[1], botArr[2], botCol.w });
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip("Nadir / ground hemisphere gradient color");
	} else if (skybox_.GetMode() == SkyboxMode::SolidColor) {
		Vector4 solidCol = skybox_.GetSolidColor();
		float solidArr[3] = { solidCol.x, solidCol.y, solidCol.z };
		if (ImGui::ColorEdit3("Solid Color", solidArr)) {
			skybox_.SetSolidColor({ solidArr[0], solidArr[1], solidArr[2], solidCol.w });
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip("Uniform solid clear color for the viewport background");
	} else if (skybox_.GetMode() == SkyboxMode::Cubemap) {
		if (availableCubemaps_.empty()) {
			RefreshAvailableCubemaps();
		}

		int currentIdx = -1;
		std::string currentPath = skybox_.GetCubemapPath();
		for (int i = 0; i < static_cast<int>(availableCubemaps_.size()); ++i) {
			if (availableCubemaps_[i] == currentPath) {
				currentIdx = i;
				break;
			}
		}

		const char* previewText = (currentIdx >= 0) ? availableCubemaps_[currentIdx].c_str() : "(Select Cubemap...)";
		if (ImGui::BeginCombo("Cubemap Asset", previewText)) {
			for (int i = 0; i < static_cast<int>(availableCubemaps_.size()); ++i) {
				bool isSelected = (currentIdx == i);
				if (ImGui::Selectable(availableCubemaps_[i].c_str(), isSelected)) {
					currentPath = availableCubemaps_[i];
					skybox_.SetCubemapPath(currentPath);
					if (!skybox_.LoadCubemap(currentPath)) {
						LogMessage("[Skybox] Failed to load cubemap: " + currentPath + ". Falling back to Gradient mode.", 1);
						skybox_.SetMode(SkyboxMode::Gradient);
					}
					isSceneDirty_ = true;
					PushUndoSnapshot();
				}
				if (isSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("Choose a cubemap DDS file detected in Resources or Assets");

		ImGui::SameLine();
		if (ImGui::Button("Refresh")) {
			RefreshAvailableCubemaps();
		}
		ImGui::SetItemTooltip("Rescan folders for .dds cubemap textures");

		char pathBuf[256];
		strncpy_s(pathBuf, currentPath.c_str(), sizeof(pathBuf) - 1);
		if (ImGui::InputText("File Path", pathBuf, sizeof(pathBuf))) {
			skybox_.SetCubemapPath(pathBuf);
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			if (!skybox_.LoadCubemap(pathBuf)) {
				LogMessage("[Skybox] Failed to load cubemap: " + std::string(pathBuf) + ". Falling back to Gradient mode.", 1);
				skybox_.SetMode(SkyboxMode::Gradient);
			}
			PushUndoSnapshot();
		}
		ImGui::SetItemTooltip("Manual file path to DDS cubemap (press Enter to apply)");

		float rot = skybox_.GetRotation();
		if (ImGui::SliderFloat("Rotation (Y)", &rot, 0.0f, 360.0f, "%.1f deg")) {
			skybox_.SetRotation(rot);
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip("Rotate cubemap around Y axis (in degrees)");

		float exp = skybox_.GetExposure();
		if (ImGui::SliderFloat("Exposure", &exp, 0.1f, 5.0f, "%.2f")) {
			skybox_.SetExposure(exp);
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip("Cubemap brightness and exposure multiplier");

		ImGui::Spacing();
		if (skybox_.HasValidCubemap()) {
			ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.3f, 1.0f), "Status: Cubemap Active & Loaded");
		} else {
			ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.1f, 1.0f), "Status: Fallback Gradient Active");
		}
	}
#endif
}

