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
	if (ImGui::BeginCombo(TrId("Look Preset"), Tr("Choose..."))) {
		for (const char* preset : kPostLookPresets) {
			if (ImGui::Selectable(Tr(preset))) changed |= ApplyPostLookPreset(post, preset);
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("%s", Tr("Sets grading, bloom, vignette and letterbox. Shadows, ambient occlusion and fog stay."));
	if (ImGui::Checkbox(TrId("Preview in Scene View"), &scenePostPreview_)) Localization::SetEditorSettingBool("scenePostPreview", scenePostPreview_);
	ImGui::SetItemTooltip("%s", Tr("Preview the game's post-processing (bloom, grading, screen effects) in the Scene view"));
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

	ImGui::Spacing();
	ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%s", Tr("Fog"));
	ImGui::Separator();
	changed |= ImGui::Checkbox(TrId("Enable Fog"), &post.fogEnabled);
	ImGui::BeginDisabled(!post.fogEnabled);
	changed |= ImGui::ColorEdit3(TrId("Fog Color"), &post.fogColor.x);
	changed |= ImGui::DragFloat(TrId("Fog Density"), &post.fogDensity, 0.001f, 0.0f, 10.0f, "%.3f");
	changed |= ImGui::DragFloat(TrId("Fog Height"), &post.fogHeight, 0.1f);
	changed |= ImGui::DragFloat(TrId("Height Falloff"), &post.fogHeightFalloff, 0.005f, 0.0f, 10.0f, "%.3f");
	ImGui::SetItemTooltip("%s", Tr("How fast the fog thins out above Fog Height. 0 = the same at every height."));
	ImGui::EndDisabled();
	if (changed) {
		isSceneDirty_ = true;
		PushUndoSnapshot();
	}

	// Screen Effect studio: try a full-screen shader and the impact pulse without writing a script
	ImGui::Spacing();
	ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%s", Tr("Screen Effects"));
	ImGui::Separator();
	if (screenEffectStudioFiles_.empty()) screenEffectStudioFiles_ = ListScreenEffectShaders("shaders");
	if (screenEffectStudioFiles_.empty()) {
		ImGui::TextDisabled("%s", Tr("No ScreenEffect.*.PS.hlsl shaders in the shaders folder"));
	} else {
		screenEffectStudioShader_ = std::clamp(screenEffectStudioShader_, 0, static_cast<int>(screenEffectStudioFiles_.size()) - 1);
		const std::string& current = screenEffectStudioFiles_[screenEffectStudioShader_];
		if (ImGui::BeginCombo(TrId("Shader"), current.c_str())) {
			screenEffectStudioFiles_ = ListScreenEffectShaders("shaders"); // new files show up when the list opens
			for (int i = 0; i < static_cast<int>(screenEffectStudioFiles_.size()); ++i) {
				if (ImGui::Selectable(screenEffectStudioFiles_[i].c_str(), i == screenEffectStudioShader_)) {
					screenEffectStudioShader_ = i;
					screenEffectStudioHint_.clear();
				}
			}
			ImGui::EndCombo();
		}
		const std::string file = "shaders/" + screenEffectStudioFiles_[(std::min)(screenEffectStudioShader_, static_cast<int>(screenEffectStudioFiles_.size()) - 1)];
		if (screenEffectStudioHint_.empty()) screenEffectStudioHint_ = ScreenEffectShaderHint(file);
		if (!screenEffectStudioHint_.empty()) {
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("%s", screenEffectStudioHint_.c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::DragFloat4(TrId("gParams[0]"), &screenEffectStudioParams_[0], 0.01f);
		ImGui::DragFloat4(TrId("gParams[1]"), &screenEffectStudioParams_[4], 0.01f);
		ImGui::SliderInt(TrId("Slot"), &screenEffectStudioSlot_, 0, kImpactEffectSlot - 1);
		ImGui::SetItemTooltip("%s", Tr("Scripts use the same slots: Play clears them"));
		if (ImGui::Button(TrId("Apply"))) {
			float params[PostProcess::kScreenEffectParams] = {};
			std::copy(std::begin(screenEffectStudioParams_), std::end(screenEffectStudioParams_), params);
			std::string error;
			const std::filesystem::path path(file);
			if (!SetScreenEffectInViews(screenEffectStudioSlot_, path.wstring(), params, &error)) LogMessage("[ScreenEffect] " + file + ": " + error, 2);
		}
		ImGui::SameLine();
		if (ImGui::Button(TrId("Clear Screen Effects"))) ClearAllScreenEffects();
	}

	ImGui::Spacing();
	ImGui::TextDisabled("%s", Tr("Impact Pulse"));
	ImGui::DragFloat(TrId("Bloom Boost"), &impactPulseTest_.bloomBoost, 0.05f, 0.0f, 10.0f);
	ImGui::SliderFloat(TrId("Radial Blur"), &impactPulseTest_.radialBlur, 0.0f, 1.0f);
	ImGui::DragFloat(TrId("Shake"), &impactPulseTest_.shake, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat(TrId("Seconds"), &impactPulseTest_.seconds, 0.01f, 0.05f, 3.0f);
	if (ImGui::Button(TrId("Test Impact Pulse"))) TriggerImpactPulse(impactPulseTest_);
	ImGui::SetItemTooltip("%s", Tr("Flash, blur and shake as a hit would cause them. Turn on Post FX to see it in the Scene view."));
#endif
}

void EditorSuite::DrawSkyboxSettingsUI()
{
#ifdef USE_IMGUI
	ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), Tr("Skybox / Environment Settings"));
	ImGui::Separator();

	const char* modeNames[] = { "Gradient", "Solid Color", "Cubemap" };
	int currentMode = static_cast<int>(skybox_.GetMode());
	if (ImGui::Combo(TrId("Background Mode"), &currentMode, modeNames, 3)) {
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
	ImGui::SetItemTooltip(Tr("Select sky background rendering mode: Gradient, Solid Color, or Cubemap"));

	ImGui::Spacing();

	if (skybox_.GetMode() == SkyboxMode::Gradient) {
		Vector4 topCol = skybox_.GetTopColor();
		float topArr[3] = { topCol.x, topCol.y, topCol.z };
		if (ImGui::ColorEdit3(TrId("Top Color"), topArr)) {
			skybox_.SetTopColor({ topArr[0], topArr[1], topArr[2], topCol.w });
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("Sky apex / zenith gradient color"));

		Vector4 horizCol = skybox_.GetHorizonColor();
		float horizArr[3] = { horizCol.x, horizCol.y, horizCol.z };
		if (ImGui::ColorEdit3(TrId("Horizon Color"), horizArr)) {
			skybox_.SetHorizonColor({ horizArr[0], horizArr[1], horizArr[2], horizCol.w });
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("Sky horizon midpoint gradient color"));

		Vector4 botCol = skybox_.GetBottomColor();
		float botArr[3] = { botCol.x, botCol.y, botCol.z };
		if (ImGui::ColorEdit3(TrId("Ground Color"), botArr)) {
			skybox_.SetBottomColor({ botArr[0], botArr[1], botArr[2], botCol.w });
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("Nadir / ground hemisphere gradient color"));
	} else if (skybox_.GetMode() == SkyboxMode::SolidColor) {
		Vector4 solidCol = skybox_.GetSolidColor();
		float solidArr[3] = { solidCol.x, solidCol.y, solidCol.z };
		if (ImGui::ColorEdit3(TrId("Solid Color"), solidArr)) {
			skybox_.SetSolidColor({ solidArr[0], solidArr[1], solidArr[2], solidCol.w });
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("Uniform solid clear color for the viewport background"));
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
		if (ImGui::BeginCombo(TrId("Cubemap Asset"), previewText)) {
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
		ImGui::SetItemTooltip(Tr("Choose a cubemap DDS file detected in Resources or Assets"));

		ImGui::SameLine();
		if (ImGui::Button(TrId("Refresh"))) {
			RefreshAvailableCubemaps();
		}
		ImGui::SetItemTooltip(Tr("Rescan folders for .dds cubemap textures"));

		char pathBuf[256];
		strncpy_s(pathBuf, currentPath.c_str(), sizeof(pathBuf) - 1);
		if (ImGui::InputText(TrId("File Path"), pathBuf, sizeof(pathBuf))) {
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
		ImGui::SetItemTooltip(Tr("Manual file path to DDS cubemap (press Enter to apply)"));

		float rot = skybox_.GetRotation();
		if (ImGui::SliderFloat(TrId("Rotation (Y)"), &rot, 0.0f, 360.0f, "%.1f deg")) {
			skybox_.SetRotation(rot);
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("Rotate cubemap around Y axis (in degrees)"));

		float exp = skybox_.GetExposure();
		if (ImGui::SliderFloat(TrId("Exposure"), &exp, 0.1f, 5.0f, "%.2f")) {
			skybox_.SetExposure(exp);
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("Cubemap brightness and exposure multiplier"));

		ImGui::Spacing();
		if (skybox_.HasValidCubemap()) {
			ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.3f, 1.0f), Tr("Status: Cubemap Active & Loaded"));
		} else {
			ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.1f, 1.0f), Tr("Status: Fallback Gradient Active"));
		}
	}
#endif
}

