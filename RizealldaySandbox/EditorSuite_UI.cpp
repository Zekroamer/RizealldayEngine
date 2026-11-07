#include "EditorSuiteInternal.h"

void EditorSuite::DrawEditorUI(float deltaTime)
{
	RIZE_PROFILE_CPU("Editor UI");
	if (!dxCommon_ || dxCommon_->IsDeviceLost()) {
		return;
	}
#ifdef USE_IMGUI
	gameViewRect_[2] = 0.0f; // set again below if the Game view is visible this frame
	if (IsStandaloneMode()) {
		ImGuiIO& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		if (ImGui::Begin("##StandaloneFullscreenGameView", nullptr, flags)) {
			ImVec2 avail = ImGui::GetContentRegionAvail();
			ImGui::Image(ImTextureRef((ImTextureID)gameViewRT_.GetSrvGPUHandle().ptr), avail);
			RecordGameViewRect(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y, ImGui::GetItemRectSize().x, ImGui::GetItemRectSize().y);

			if (isStandalonePreview_) {
				ImDrawList* dl = ImGui::GetWindowDrawList();
				dl->AddRectFilled(ImVec2(10.0f, 10.0f), ImVec2(360.0f, 38.0f), IM_COL32(0, 0, 0, 180), 4.0f);
				dl->AddText(ImVec2(18.0f, 16.0f), IM_COL32(0, 220, 255, 255), "Standalone Game Preview | Press F11 or Esc to exit");
			}

			if (isTetrisActive_) {
				DrawTetrisHUD(ImVec2(0.0f, 0.0f), avail);
			}
		}
		ImGui::End();
		ImGui::PopStyleVar();
		return;
	}

	DrawMainMenuBar();
	DrawTopPlayBar();
	DrawHierarchyPanel();
	DrawViewportsPanel();
	DrawProjectAndConsolePanel();
	DrawInspectorAndDetailsPanel();

	if (showScriptGenModal_) DrawCppScriptGeneratorModal();
	if (showNewProjectModal_) DrawNewProjectModal();
	if (showOpenProjectModal_) DrawOpenProjectModal();
	if (showBuildSettingsModal_) DrawBuildSettingsModal();
	if (showSaveSceneAsModal_) DrawSaveSceneAsModal();
	if (showOpenSceneModal_) DrawOpenSceneModal();
	if (showProjectHub_) DrawProjectHub();
	if (showUnsavedCloseModal_) DrawUnsavedChangesModal();
	if (showEngineUpdateModal_) DrawEngineUpdateModal();
	if (showAboutModal_) DrawAboutModal();

	if (showLightingSettings_) {
		ImGui::SetNextWindowSize(ImVec2(380.0f, 420.0f), ImGuiCond_FirstUseEver);
		if (ImGui::Begin(TrId("Lighting & Environment"), &showLightingSettings_)) {
			DrawSkyboxSettingsUI();
			DrawPostProcessSettingsUI();
		}
		ImGui::End();
	}

	if (showProfiler_) DrawProfilerWindow();
	if (showAnimatorWindow_) DrawAnimatorWindow();
	if (showTimelineWindow_) DrawTimelineWindow();
	if (showEffectEditor_) DrawEffectEditorWindow();
	else if (effectPreviewRoot_ != 0) StopEffectPreview(); // closed with its X
	DrawAutoSaveRestoreModal();
	if (showInputActionsWindow_) DrawInputActionsWindow();
#endif
}

void EditorSuite::ApplyUnity6DarkTheme()
{
#ifdef USE_IMGUI
	// The UI font is loaded in DirectXCommon::Initialize: the font atlas is locked during a frame,
	// and EditorSuite is created mid-frame by SceneManager.
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 0.0f;
	style.ChildRounding = 4.0f;
	style.FrameRounding = 4.0f;
	style.PopupRounding = 4.0f;
	style.ScrollbarRounding = 6.0f;
	style.GrabRounding = 4.0f;
	style.TabRounding = 4.0f;
	style.WindowBorderSize = 1.0f;
	style.ChildBorderSize = 1.0f;
	style.PopupBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;
	style.WindowPadding = ImVec2(8.0f, 8.0f);
	style.FramePadding = ImVec2(8.0f, 4.0f);
	style.ItemSpacing = ImVec2(8.0f, 5.0f);
	style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
	style.IndentSpacing = 16.0f;
	style.ScrollbarSize = 12.0f;
	style.GrabMinSize = 10.0f;
	style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
	style.SeparatorTextBorderSize = 1.0f;

	// Neutral dark greys with a single blue accent
	const ImVec4 bgDark     = ImVec4(0.098f, 0.098f, 0.106f, 1.0f); // #19191B
	const ImVec4 bgPanel    = ImVec4(0.137f, 0.137f, 0.145f, 1.0f); // #232325
	const ImVec4 bgRaised   = ImVec4(0.180f, 0.180f, 0.192f, 1.0f); // #2E2E31
	const ImVec4 bgHover    = ImVec4(0.235f, 0.235f, 0.251f, 1.0f); // #3C3C40
	const ImVec4 bgActive   = ImVec4(0.290f, 0.290f, 0.310f, 1.0f);
	const ImVec4 accent     = ImVec4(0.231f, 0.510f, 0.965f, 1.0f); // #3B82F6
	const ImVec4 accentSoft = ImVec4(0.231f, 0.510f, 0.965f, 0.35f);
	const ImVec4 border     = ImVec4(0.067f, 0.067f, 0.075f, 1.0f);

	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text]                 = ImVec4(0.902f, 0.902f, 0.918f, 1.0f);
	colors[ImGuiCol_TextDisabled]         = ImVec4(0.502f, 0.502f, 0.533f, 1.0f);
	colors[ImGuiCol_WindowBg]             = bgPanel;
	colors[ImGuiCol_ChildBg]              = bgDark;
	colors[ImGuiCol_PopupBg]              = ImVec4(0.118f, 0.118f, 0.126f, 0.98f);
	colors[ImGuiCol_Border]               = border;
	colors[ImGuiCol_BorderShadow]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	colors[ImGuiCol_FrameBg]              = bgDark;
	colors[ImGuiCol_FrameBgHovered]       = bgHover;
	colors[ImGuiCol_FrameBgActive]        = bgActive;
	colors[ImGuiCol_TitleBg]              = bgDark;
	colors[ImGuiCol_TitleBgActive]        = bgDark;
	colors[ImGuiCol_TitleBgCollapsed]     = bgDark;
	colors[ImGuiCol_MenuBarBg]            = bgDark;
	colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	colors[ImGuiCol_ScrollbarGrab]        = bgHover;
	colors[ImGuiCol_ScrollbarGrabHovered] = bgActive;
	colors[ImGuiCol_ScrollbarGrabActive]  = accent;
	colors[ImGuiCol_CheckMark]            = accent;
	colors[ImGuiCol_SliderGrab]           = ImVec4(0.420f, 0.420f, 0.450f, 1.0f);
	colors[ImGuiCol_SliderGrabActive]     = accent;
	colors[ImGuiCol_Button]               = bgRaised;
	colors[ImGuiCol_ButtonHovered]        = bgHover;
	colors[ImGuiCol_ButtonActive]         = accentSoft;
	colors[ImGuiCol_Header]               = bgRaised;
	colors[ImGuiCol_HeaderHovered]        = bgHover;
	colors[ImGuiCol_HeaderActive]         = accentSoft;
	colors[ImGuiCol_Separator]            = border;
	colors[ImGuiCol_SeparatorHovered]     = accentSoft;
	colors[ImGuiCol_SeparatorActive]      = accent;
	colors[ImGuiCol_ResizeGrip]           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	colors[ImGuiCol_ResizeGripHovered]    = accentSoft;
	colors[ImGuiCol_ResizeGripActive]     = accent;
	colors[ImGuiCol_Tab]                  = bgDark;
	colors[ImGuiCol_TabHovered]           = bgHover;
	colors[ImGuiCol_TabSelected]          = bgRaised;
	colors[ImGuiCol_TabSelectedOverline]  = accent;
	colors[ImGuiCol_TabDimmed]            = bgDark;
	colors[ImGuiCol_TabDimmedSelected]    = bgPanel;
	colors[ImGuiCol_TableHeaderBg]        = bgRaised;
	colors[ImGuiCol_TableBorderStrong]    = border;
	colors[ImGuiCol_TableBorderLight]     = border;
	colors[ImGuiCol_TextSelectedBg]       = accentSoft;
	colors[ImGuiCol_NavCursor]            = accent;
	colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
#endif
}

void EditorSuite::DrawMainMenuBar()
{
#ifdef USE_IMGUI
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu(TrId("File"))) {
			if (ImGui::MenuItem(TrId("New Scene"), "Ctrl+N")) { NewScene(); }
			if (ImGui::MenuItem(TrId("Open Scene..."))) { showOpenSceneModal_ = true; }
			if (ImGui::MenuItem(TrId("Save Scene"), "Ctrl+S")) {
				if (currentScenePath_.empty()) showSaveSceneAsModal_ = true;
				else SaveScene(currentScenePath_);
			}
			if (ImGui::MenuItem(TrId("Save Scene As..."))) { showSaveSceneAsModal_ = true; }
			if (ImGui::MenuItem(TrId("Revert Unsaved Changes"))) { DiscardUnsavedChanges(); }
			ImGui::Separator();
			if (ImGui::MenuItem(TrId("New Project..."))) { showNewProjectModal_ = true; }
			if (ImGui::MenuItem(TrId("Open Project..."))) { LoadRecentProjects(); showOpenProjectModal_ = true; }
			if (ImGui::BeginMenu(TrId("Open Recent"), !recentProjects_.empty())) {
				for (size_t i = 0; i < recentProjects_.size(); ++i) {
					const std::filesystem::path& proj = recentProjects_[i];
					std::error_code ec;
					const bool exists = std::filesystem::exists(proj, ec);
					const bool isCurrent = !currentProjectRoot_.empty() && std::filesystem::equivalent(proj, currentProjectRoot_, ec);
					const std::string label = std::format("{}##recent{}", proj.filename().string(), i);
					if (ImGui::MenuItem(label.c_str(), nullptr, isCurrent, exists && !isCurrent)) OpenProject(proj.string());
					ImGui::SetItemTooltip("%s", proj.string().c_str());
				}
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem(TrId("Load Game Template > Playable 3D Tetris"))) { LoadTetrisTemplate(); }
			ImGui::Separator();
			if (ImGui::MenuItem(TrId("Build & Export Game..."))) { showBuildSettingsModal_ = true; }
			ImGui::Separator();
			if (ImGui::MenuItem(TrId("Exit"), "Alt+F4")) { WinApp::GetInstance()->RequestClose(); }
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(TrId("Edit"))) {
			if (ImGui::MenuItem(TrId("Undo"), "Ctrl+Z")) { Undo(); }
			if (ImGui::MenuItem(TrId("Redo"), "Ctrl+Y")) { Redo(); }
			ImGui::Separator();
			if (ImGui::MenuItem(TrId("Delete Selected"), "Del")) { DeleteSelectedGameObject(); }
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(TrId("Assets"))) {
			if (ImGui::MenuItem(TrId("Import Assets..."), nullptr, false, !currentProjectRoot_.empty())) {
				const std::vector<std::string> files = OpenNativeFilesDialog();
				ImportAssets(std::vector<std::filesystem::path>(files.begin(), files.end()));
			}
			ImGui::SetItemTooltip("%s", Tr("Copy files into the project (you can also drag files from Explorer onto the editor)"));
			if (ImGui::MenuItem(TrId("Import Folder..."), nullptr, false, !currentProjectRoot_.empty())) {
				if (const std::string folder = OpenNativeFolderDialog(); !folder.empty()) ImportAssets({ std::filesystem::path(folder) });
			}
			ImGui::Separator();
			if (ImGui::MenuItem(TrId("Create C++ Script..."))) {
				scriptGenTargetObjectIndex_ = -1;
				scriptGenErrorMsg_.clear();
				showScriptGenModal_ = true;
			}
			if (ImGui::MenuItem(TrId("Build Scripts (GameScripts.dll)"), "Ctrl+Shift+B")) { TriggerScriptBuild(); }
			ImGui::MenuItem(TrId("Auto Build Scripts on Save"), nullptr, &autoBuildScripts_);
			ImGui::Separator();
			if (ImGui::MenuItem(TrId("Refresh Asset Database"), "Ctrl+R")) { RefreshAssetBrowser(); }
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(TrId("GameObject"))) {
			DrawHierarchyCreationMenu();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(TrId("Window"))) {
			if (ImGui::MenuItem(TrId("Preview Standalone Game Mode"), "F11")) { ToggleStandalonePreview(); }
			if (ImGui::MenuItem(TrId("Lighting / Environment"), nullptr, showLightingSettings_)) {
				showLightingSettings_ = !showLightingSettings_;
			}
			if (ImGui::MenuItem(TrId("Timeline"), nullptr, showTimelineWindow_)) {
				showTimelineWindow_ = !showTimelineWindow_;
			}
			if (ImGui::MenuItem(TrId("Effect Editor"), nullptr, showEffectEditor_)) {
				showEffectEditor_ = !showEffectEditor_;
			}
			if (ImGui::MenuItem(TrId("Animator"), nullptr, showAnimatorWindow_)) {
				showAnimatorWindow_ = !showAnimatorWindow_;
			}
			if (ImGui::MenuItem(TrId("Profiler"), "Ctrl+P", showProfiler_)) {
				showProfiler_ = !showProfiler_;
			}
			if (ImGui::BeginMenu(TrId("Application Scene"))) {
				bool canSwitch = (sceneState_ == EditorSceneState::Edit);
				if (ImGui::MenuItem(TrId("Title Scene (Demo)"), nullptr, false, canSwitch)) {
					if (onRequestTitleScene_) {
						onRequestTitleScene_();
					}
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(TrId("Options"))) {
			if (ImGui::MenuItem(TrId("Input Actions..."), nullptr, showInputActionsWindow_)) showInputActionsWindow_ = !showInputActionsWindow_;
			if (ImGui::MenuItem(TrId("Auto Save"), nullptr, &autoSaveEnabled_)) Localization::SetEditorSettingBool("autoSave", autoSaveEnabled_);
			ImGui::SetItemTooltip("%s", Tr("Every 2 minutes, a copy of a scene with unsaved changes goes to Library/AutoSave."));
			if (ImGui::BeginMenu(TrId("Language"))) {
				// Language names come from the files ("_language"), so each one is shown in its own script
				for (const Localization::LanguageInfo& lang : Localization::AvailableLanguages()) {
					const bool current = (lang.code == Localization::CurrentLanguage());
					if (ImGui::MenuItem((lang.displayName + "##lang_" + lang.code).c_str(), nullptr, current) && !current) {
						if (Localization::SetLanguage(lang.code)) {
							if (!isRunningTests_) Localization::SavePreference();
							LogMessage(std::format("[Editor] Language: {} ({} translated strings)", lang.displayName, Localization::TranslationCount()), 0);
						} else {
							LogMessage(std::format("[Editor] Cannot load language file '{}.json'.", lang.code), 2);
						}
					}
				}
				ImGui::Separator();
				ImGui::TextDisabled("%s", Tr("Translations: Resources/Localization/<code>.json"));
				ImGui::TextDisabled("%s", Tr("Missing entries are shown in English."));
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(TrId("Help"))) {
			DrawHelpMenu();
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
#endif
}

void EditorSuite::DrawTopPlayBar()
{
#ifdef USE_IMGUI
	ImGuiIO& io = ImGui::GetIO();
	const float screenW = io.DisplaySize.x;
	const float barH = kTopBarHeight;
	const float menuH = ImGui::GetFrameHeight();
	const ImVec2 buttonPadding = ImGui::GetStyle().FramePadding;
	const float spacing = ImGui::GetStyle().ItemSpacing.x;

	ImGui::SetNextWindowPos(ImVec2(0.0f, menuH));
	ImGui::SetNextWindowSize(ImVec2(screenW, barH));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

	auto buttonWidth = [&](const char* label) {
		return ImGui::CalcTextSize(label, nullptr, true).x + buttonPadding.x * 2.0f;
	};

	const bool isPlayBar = (sceneState_ == EditorSceneState::Play);
	const bool isPauseBar = (sceneState_ == EditorSceneState::Pause);
	if (isPlayBar) ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.24f, 0.14f, 1.0f));
	else if (isPauseBar) ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.26f, 0.19f, 0.09f, 1.0f));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
	if (ImGui::Begin("##TopPlayBar", nullptr, flags)) {
		const float rowH = ImGui::GetFrameHeight();
		ImGui::SetCursorPosY((barH - rowH) * 0.5f);

		// Left: script build status as a small colored dot + label
		const bool building = isBuildingScripts_.load();
		const bool buildFailed = !building && lastScriptBuildStatus_.load() == 2;
		const ImVec4 statusColor = building ? ImVec4(1.0f, 0.78f, 0.25f, 1.0f)
			: buildFailed ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
			: ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
		const char* statusText = Tr(building ? "Building scripts..." : buildFailed ? "Script build failed" : "Scripts ready");
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 p = ImGui::GetCursorScreenPos();
			dl->AddCircleFilled(ImVec2(p.x + 5.0f, p.y + rowH * 0.5f), 4.0f, ImGui::GetColorU32(statusColor));
			ImGui::Dummy(ImVec2(14.0f, rowH));
			ImGui::SameLine(0.0f, 2.0f);
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", statusText);
			if (buildFailed) ImGui::SetItemTooltip("%s", Tr("See the Console tab for MSBuild errors"));
		}

		// Center: Play / Pause / Step / Stop
		const bool isPlay = (sceneState_ == EditorSceneState::Play);
		const bool isPause = (sceneState_ == EditorSceneState::Pause);
		const bool inPlaySession = (isPlay || isPause);
		// Wide enough for the longest label in the current language (stable while toggling Play / Pause)
		const float playW = (std::max)({ 76.0f, buttonWidth(TrId("Play")), buttonWidth(TrId("Playing")) });
		const float smallW = (std::max)({ 64.0f, buttonWidth(TrId("Pause")), buttonWidth(TrId("Resume")),
			buttonWidth(TrId("Step")), buttonWidth(TrId("Stop")) });
		const float groupW = playW + smallW * 3.0f + spacing * 3.0f;
		ImGui::SameLine((std::max)((screenW - groupW) * 0.5f, ImGui::GetCursorPosX() + 16.0f));

		if (inPlaySession) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.50f, 0.25f, 1.0f));
		if (inPlaySession) ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.58f, 0.30f, 1.0f));
		if (ImGui::Button(TrId(inPlaySession ? "Playing" : "Play"), ImVec2(playW, 0.0f))) {
			if (!inPlaySession) {
				StartPlayMode();
			} else {
				StopPlayMode();
			}
		}
		if (inPlaySession) ImGui::PopStyleColor(2);
		ImGui::SetItemTooltip("%s", Tr(inPlaySession ? "Stop play mode" : "Enter play mode"));

		ImGui::SameLine();
		ImGui::BeginDisabled(!inPlaySession);
		if (isPause) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.42f, 0.12f, 1.0f));
		if (ImGui::Button(TrId(isPause ? "Resume" : "Pause"), ImVec2(smallW, 0.0f))) {
			sceneState_ = isPlay ? EditorSceneState::Pause : EditorSceneState::Play;
		}
		if (isPause) ImGui::PopStyleColor();
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", Tr("Pause / resume the running game"));

		ImGui::SameLine();
		if (ImGui::Button(TrId("Step"), ImVec2(smallW, 0.0f))) {
			if (sceneState_ == EditorSceneState::Edit) {
				StartPlayMode();
			}
			sceneState_ = EditorSceneState::Pause;
			stepSingleFrame_ = true;
		}
		ImGui::SetItemTooltip("%s", Tr("Advance a single frame (enters play mode paused)"));

		ImGui::SameLine();
		ImGui::BeginDisabled(!inPlaySession);
		if (ImGui::Button(TrId("Stop"), ImVec2(smallW, 0.0f))) {
			StopPlayMode();
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", Tr("Stop play mode and restore the scene"));

		// Right: Build Scripts + Tools menu
		const char* buildLabel = TrId(building ? "Building..." : "Build Scripts");
		const char* toolsLabel = TrId("Tools");
		const float rightW = buttonWidth(buildLabel) + buttonWidth(toolsLabel) + spacing;
		ImGui::SameLine((std::max)(screenW - rightW - 10.0f, ImGui::GetCursorPosX() + 16.0f));

		ImGui::BeginDisabled(building);
		if (ImGui::Button(buildLabel)) {
			TriggerScriptBuild();
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", Tr("Compile GameScripts.dll in the background (Ctrl+Shift+B)"));

		ImGui::SameLine();
		if (ImGui::Button(toolsLabel)) {
			ImGui::OpenPopup("##TopBarTools");
		}
		if (ImGui::BeginPopup("##TopBarTools")) {
			ImGui::MenuItem(TrId("Auto Build Scripts"), nullptr, &autoBuildScripts_);
			ImGui::SetItemTooltip("%s", Tr("Rebuild and hot-reload GameScripts.dll ~1s after saving a .cpp/.h in GameScripts/Scripts"));
			ImGui::MenuItem(TrId("Simulate Physics in Edit Mode"), nullptr, &simulatePhysicsInEditMode_);
			ImGui::Separator();
			if (ImGui::MenuItem(TrId("New C++ Script..."))) {
				scriptGenTargetObjectIndex_ = selectedObjectIndex_;
				scriptGenErrorMsg_.clear();
				showScriptGenModal_ = true;
			}
			if (ImGui::MenuItem(TrId("Build / Export..."))) {
				showBuildSettingsModal_ = true;
			}
			ImGui::EndPopup();
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
	if (isPlayBar || isPauseBar) ImGui::PopStyleColor();
#endif
}

void EditorSuite::DrawHierarchyPanel()
{
#ifdef USE_IMGUI
	ImGuiIO& io = ImGui::GetIO();
	float screenW = io.DisplaySize.x;
	float screenH = io.DisplaySize.y;
	float topY = ImGui::GetFrameHeight() + kTopBarHeight;
	float leftW = screenW * 0.18f;
	if (leftW < 220.0f) leftW = 220.0f;
	float midH = (screenH - topY) * 0.65f;

	ImGui::SetNextWindowPos(ImVec2(0.0f, topY));
	ImGui::SetNextWindowSize(ImVec2(leftW, midH));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

	if (ImGui::Begin(TrId("Hierarchy"), nullptr, flags)) {
		// Sub-bar: creation button and search filter
		if (ImGui::Button(TrId("+ Create"))) {
			ImGui::OpenPopup("HierarchyCreationPopup");
		}
		ImGui::SetItemTooltip("%s", Tr("Create a new GameObject (or right-click empty space)"));

		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##SearchHierarchy", Tr("Search..."), hierarchySearchBuf_, sizeof(hierarchySearchBuf_));

		ImGui::Separator();

		if (ImGui::BeginPopup("HierarchyCreationPopup")) {
			DrawHierarchyCreationMenu();
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopupContextWindow("HierarchyContextWindow", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
			DrawHierarchyCreationMenu();
			ImGui::EndPopup();
		}

		// Hierarchy Scene Name with dynamic name and dirty flag '*'
		std::string sceneLabel = currentSceneName_ + (isSceneDirty_ ? "*" : "");

		if (isEditingSceneName_) {
			ImGui::SetNextItemWidth(-1.0f);
			if (focusSceneNameEdit_) { ImGui::SetKeyboardFocusHere(); focusSceneNameEdit_ = false; } // typing right away must not trigger tool shortcuts
			const bool entered = ImGui::InputText("##EditSceneName", sceneNameEditBuf_, sizeof(sceneNameEditBuf_), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
			// Renames the scene file too, so Ctrl+S keeps the new name (a refused name keeps the old one and logs why)
			if (entered || ImGui::IsItemDeactivatedAfterEdit()) RenameCurrentScene(sceneNameEditBuf_);
			if (entered || ImGui::IsItemDeactivated()) isEditingSceneName_ = false;
		} else {
			// OpenOnArrow: the double-click that starts a rename must not also collapse the scene
			bool nodeOpen = ImGui::TreeNodeEx("##SceneTreeHeader", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow, "%s", sceneLabel.c_str());

			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
				isEditingSceneName_ = true;
				focusSceneNameEdit_ = true;
				strncpy_s(sceneNameEditBuf_, currentSceneName_.c_str(), sizeof(sceneNameEditBuf_) - 1);
			}

			if (ImGui::BeginPopupContextItem("SceneHeaderContext")) {
				if (ImGui::MenuItem(TrId("Rename Scene"))) {
					isEditingSceneName_ = true;
					focusSceneNameEdit_ = true;
					strncpy_s(sceneNameEditBuf_, currentSceneName_.c_str(), sizeof(sceneNameEditBuf_) - 1);
				}
				if (ImGui::MenuItem(TrId("Save Scene"), "Ctrl+S")) {
					if (currentScenePath_.empty()) showSaveSceneAsModal_ = true;
					else SaveScene(currentScenePath_);
				}
				ImGui::EndPopup();
			}

			if (nodeOpen) {
				const bool searching = (hierarchySearchBuf_[0] != '\0');

				// Reparenting is deferred until after the rows are drawn so the tree is not modified while iterating it
				bool hasPendingReparent = false;
				uint32_t pendingReparentChildId = 0;
				int pendingReparentParentId = -1;

				auto acceptReparentDrop = [&](int targetParentId) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RIZE_GAMEOBJECT")) {
						if (payload->DataSize == static_cast<int>(sizeof(uint32_t))) {
							hasPendingReparent = true;
							pendingReparentChildId = *static_cast<const uint32_t*>(payload->Data);
							pendingReparentParentId = targetParentId;
						}
					}
				};

				// Children lists (in gameObjects_ order) for the tree view; roots are objects without a valid parent
				std::unordered_map<uint32_t, std::vector<size_t>> childIndices;
				std::vector<size_t> rootIndices;
				if (!searching) {
					std::unordered_set<uint32_t> knownIds;
					knownIds.reserve(gameObjects_.size());
					for (const auto& o : gameObjects_) knownIds.insert(o.id);
					for (size_t i = 0; i < gameObjects_.size(); ++i) {
						const GameObject& o = gameObjects_[i];
						if (o.parentId != -1 && knownIds.count(static_cast<uint32_t>(o.parentId)) > 0 && static_cast<uint32_t>(o.parentId) != o.id) {
							childIndices[static_cast<uint32_t>(o.parentId)].push_back(i);
						} else {
							rootIndices.push_back(i);
						}
					}
				}
				std::vector<bool> visited(gameObjects_.size(), false);

				auto drawRow = [&](auto&& self, size_t i) -> void {
					auto& obj = gameObjects_[i];
					visited[i] = true;

					const std::vector<size_t>* kids = nullptr;
					if (!searching) {
						auto it = childIndices.find(obj.id);
						if (it != childIndices.end() && !it->second.empty()) kids = &it->second;
					}

					ImGui::PushID(static_cast<int>(obj.id));

					const char* typeName = Tr("Empty");
					if (obj.type == GameObjectType::Cube) typeName = Tr("Cube");
					else if (obj.type == GameObjectType::Sphere) typeName = Tr("Sphere");
					else if (obj.type == GameObjectType::Plane) typeName = Tr("Plane");
					else if (obj.type == GameObjectType::Quad) typeName = Tr("Quad");
					else if (obj.type == GameObjectType::Camera) typeName = Tr("Camera");
					else if (obj.type == GameObjectType::DirectionalLight) typeName = Tr("Light");
					else if (obj.type == GameObjectType::PointLight) typeName = Tr("Point Light");
					else if (obj.type == GameObjectType::SpotLight) typeName = Tr("Spot Light");
					else if (obj.type == GameObjectType::ParticleSystem) typeName = Tr("Particles");
					else if (obj.type == GameObjectType::MapchipGrid) typeName = Tr("Mapchip");
					else if (obj.type == GameObjectType::Sprite) typeName = Tr("Sprite");
					else if (obj.type == GameObjectType::AudioSource || obj.hasAudioSource) typeName = Tr("Audio");

					// Row layout: [ name ............ type  (x) ] -- active toggle on the right, type tag dimmed
					const float rowStartX = ImGui::GetCursorPosX();
					const float rowW = ImGui::GetContentRegionAvail().x;
					const float toggleSize = ImGui::GetFontSize();
					const float typeW = ImGui::CalcTextSize(typeName).x;

					ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
						ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
					if (!kids) nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
					if (IsSelected(obj.id)) nodeFlags |= ImGuiTreeNodeFlags_Selected;

					const bool isPrefabLinked = !obj.prefabSource.empty();
					if (!obj.isActive) {
						ImGui::PushStyleColor(ImGuiCol_Text, isPrefabLinked ? ImVec4(0.35f, 0.52f, 0.68f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
					} else if (isPrefabLinked) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.52f, 0.80f, 1.0f, 1.0f));
					}
					ImGui::SetNextItemAllowOverlap();
					const bool nodeIsOpen = ImGui::TreeNodeEx("##Row", nodeFlags, "%s", obj.name.c_str());
					if (!obj.isActive || isPrefabLinked) ImGui::PopStyleColor();

					if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
						if (ImGui::GetIO().KeyShift) {
							SelectObjectRange(static_cast<int>(i));
						} else {
							SelectObject(static_cast<int>(i), ImGui::GetIO().KeyCtrl);
						}
					}

					// Drag & drop reparenting: drag a row onto another row to make it a child
					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
						const uint32_t dragId = obj.id;
						ImGui::SetDragDropPayload("RIZE_GAMEOBJECT", &dragId, sizeof(dragId));
						ImGui::TextUnformatted(obj.name.c_str());
						ImGui::EndDragDropSource();
					}
					if (ImGui::BeginDragDropTarget()) {
						acceptReparentDrop(static_cast<int>(obj.id));
						ImGui::EndDragDropTarget();
					}

					if (ImGui::BeginPopupContextItem()) {
						if (!IsSelected(obj.id)) SelectObject(static_cast<int>(i), false);
						if (ImGui::MenuItem(TrId("Focus in Scene"), "F")) {
							FocusSelectedObject();
						}
						if (ImGui::MenuItem(TrId("Delete GameObject"), "Del")) {
							QueueDeleteGameObject(static_cast<int>(i));
						}
						if (ImGui::MenuItem(TrId("Duplicate"), "Ctrl+D")) {
							pendingDuplicateIndex_ = static_cast<int>(i);
						}
						if (ImGui::MenuItem(TrId("Unparent"), nullptr, false, obj.parentId != -1)) {
							hasPendingReparent = true;
							pendingReparentChildId = obj.id;
							pendingReparentParentId = -1;
						}
						ImGui::Separator();
						if (isPrefabLinked && ImGui::MenuItem(TrId("Open Prefab"))) {
							const std::string prefabToOpen = obj.prefabSource;
							ImGui::EndPopup();
							if (kids && nodeIsOpen) ImGui::TreePop();
							ImGui::PopID();
							OpenPrefabMode(prefabToOpen);
							return;
						}
						if (ImGui::MenuItem(TrId("Save as Prefab"))) {
							SavePrefab(obj.id, obj.name);
						}
						ImGui::SetItemTooltip("%s", Tr("Saves this object and its children to Assets/Prefabs/<name>.prefab.json"));
						ImGui::EndPopup();
					}

					const float toggleX = rowStartX + rowW - toggleSize - 2.0f;
					const float typeX = toggleX - typeW - ImGui::GetStyle().ItemSpacing.x;
					if (typeX > rowStartX + ImGui::CalcTextSize(obj.name.c_str()).x + 24.0f) {
						ImGui::SameLine(typeX);
						ImGui::TextDisabled("%s", typeName);
					}
					ImGui::SameLine(toggleX);
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
					if (ImGui::Checkbox("##Active", &obj.isActive)) {
						isSceneDirty_ = true;
						PushUndoSnapshot();
					}
					ImGui::PopStyleVar();
					ImGui::SetItemTooltip(obj.isActive ? "Active (click to disable)" : "Inactive (click to enable)");

					if (kids && nodeIsOpen) {
						for (size_t child : *kids) {
							if (!visited[child]) self(self, child);
						}
						ImGui::TreePop();
					}

					ImGui::PopID();
				};

				if (searching) {
					for (size_t i = 0; i < gameObjects_.size(); ++i) {
						if (gameObjects_[i].name.find(hierarchySearchBuf_) == std::string::npos) continue;
						drawRow(drawRow, i);
					}
				} else {
					for (size_t r : rootIndices) {
						if (!visited[r]) drawRow(drawRow, r);
					}
					// Objects stuck in a parent cycle have no root; show them at the top level so they stay reachable
					for (size_t i = 0; i < gameObjects_.size(); ++i) {
						if (!visited[i]) drawRow(drawRow, i);
					}
				}

				// Empty area below the rows: drop here to unparent; right-click opens the creation menu
				{
					const float dropH = (std::max)(24.0f, ImGui::GetContentRegionAvail().y);
					ImGui::InvisibleButton("##HierarchyDropArea", ImVec2(-1.0f, dropH));
					if (ImGui::BeginDragDropTarget()) {
						acceptReparentDrop(-1);
						ImGui::EndDragDropTarget();
					}
					if (ImGui::BeginPopupContextItem("HierarchyEmptyAreaContext")) {
						DrawHierarchyCreationMenu();
						ImGui::EndPopup();
					}
				}

				if (hasPendingReparent) {
					ReparentGameObject(pendingReparentChildId, pendingReparentParentId, true);
				}
				ImGui::TreePop();
			}
		}
	}
	ImGui::End();
#endif
}

void EditorSuite::DrawHierarchyCreationMenu()
{
#ifdef USE_IMGUI
	if (ImGui::MenuItem(TrId("Create Empty"), "Ctrl+Shift+N")) {
		CreateGameObject("GameObject", GameObjectType::Empty);
	}
	if (ImGui::MenuItem(TrId("Create Empty Child"), "Alt+Shift+N")) {
		int parentIdx = selectedObjectIndex_;
		int parentId = (parentIdx >= 0 && parentIdx < static_cast<int>(gameObjects_.size())) ? static_cast<int>(gameObjects_[parentIdx].id) : -1;
		auto& child = CreateGameObject("ChildGameObject", GameObjectType::Empty);
		child.parentIndex = parentIdx;
		child.parentId = parentId;
		if (parentIdx >= 0 && parentIdx < static_cast<int>(gameObjects_.size()) - 1) {
			gameObjects_[parentIdx].childrenIds.push_back(child.id);
		}
		RebuildHierarchyLinks();
		UpdateWorldMatrices();
	}
	if (ImGui::MenuItem(TrId("Create Empty Parent"), "Ctrl+Shift+G")) {
		int childIdx = selectedObjectIndex_;
		auto& parent = CreateGameObject("ParentGameObject", GameObjectType::Empty);
		if (childIdx >= 0 && childIdx < static_cast<int>(gameObjects_.size()) - 1) {
			// The new parent sits at the child's world position with identity rotation/scale,
			// so the child's local translation becomes zero and its world transform is unchanged
			Vector3 childWorldPos{};
			Quaternion childWorldRot = Quaternion::Identity();
			Vector3 childWorldScale{};
			UpdateWorldMatrices();
			GetWorldTRS(gameObjects_[childIdx], childWorldPos, childWorldRot, childWorldScale);
			parent.translation = childWorldPos;
			gameObjects_[childIdx].translation = { 0.0f, 0.0f, 0.0f };
			if (gameObjects_[childIdx].parentId != -1) {
				// The child leaves its old parent: bake its world rotation/scale into the local values
				gameObjects_[childIdx].rotationQuat = childWorldRot;
				gameObjects_[childIdx].rotation = childWorldRot.ToEuler();
				gameObjects_[childIdx].scale = childWorldScale;
			}
			gameObjects_[childIdx].parentId = static_cast<int>(parent.id);
			gameObjects_[childIdx].parentIndex = static_cast<int>(gameObjects_.size()) - 1;
			parent.childrenIds.push_back(gameObjects_[childIdx].id);
			RebuildHierarchyLinks();
			UpdateWorldMatrices();
		}
	}
	ImGui::Separator();

	if (ImGui::BeginMenu(TrId("2D Object"))) {
		if (ImGui::MenuItem(TrId("Sprite"))) {
			CreateGameObject("Sprite", GameObjectType::Sprite);
		}
		if (ImGui::MenuItem(TrId("Textured Rectangle"))) {
			CreateQuad("Textured Rectangle", { 0.0f, 0.5f, 0.0f });
		}
		if (ImGui::MenuItem(TrId("Mapchip Grid"))) {
			auto& map = CreateGameObject("Level Mapchip Grid", GameObjectType::MapchipGrid);
			map.hasMapchip = true;
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(TrId("3D Object"))) {
		if (ImGui::MenuItem(TrId("Cube (with Box Collider)"))) {
			CreateCube("Cube", { 0.0f, 0.5f, 0.0f });
		}
		if (ImGui::MenuItem(TrId("Sphere (with Sphere Collider)"))) {
			CreateSphere("Sphere", { 0.0f, 0.5f, 0.0f });
		}
		if (ImGui::MenuItem(TrId("Plane (Ground)"))) {
			CreatePlane("Plane", { 0.0f, 0.0f, 0.0f });
		}
		if (ImGui::MenuItem(TrId("Textured Quad"))) {
			CreateQuad("Textured Quad", { 0.0f, 0.5f, 0.0f });
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(TrId("Visual Effects"))) {
		if (ImGui::MenuItem(TrId("Particle System"))) {
			auto& ps = CreateGameObject("Particle System", GameObjectType::ParticleSystem);
			AddComponentByName(ps, kGpuParticlesComponent);
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		if (ImGui::BeginMenu(TrId("Effect"))) {
			for (const char* name : kEffectTemplates) {
				if (ImGui::MenuItem(Tr(name)) && CreateEffectFromTemplate(name, { 0.0f, 1.0f, 0.0f }) != 0) showEffectEditor_ = true;
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(TrId("Light"))) {
		if (ImGui::MenuItem(TrId("Directional Light"))) {
			auto& light = CreateGameObject("Directional Light", GameObjectType::DirectionalLight);
			light.hasLight = true;
			light.lightType = GameObject::LightType::Directional;
			light.lightDirection = { 0.5f, -1.0f, 0.5f };
			Vector3 d = light.lightDirection;
			float dLen = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
			if (dLen > 0.0001f) { d.x /= dLen; d.y /= dLen; d.z /= dLen; }
			light.rotation = { std::asin(std::clamp(-d.y, -1.0f, 1.0f)), std::atan2(d.x, d.z), 0.0f };
			light.rotationQuat = Quaternion::FromEuler(light.rotation);
		}
		if (ImGui::MenuItem(TrId("Point Light"))) {
			auto& light = CreateGameObject("Point Light", GameObjectType::PointLight);
			light.hasLight = true;
			light.lightType = GameObject::LightType::Point;
			light.lightRange = 10.0f;
			light.lightDecay = 2.0f;
		}
		if (ImGui::MenuItem(TrId("Spot Light"))) {
			auto& light = CreateGameObject("Spot Light", GameObjectType::SpotLight);
			light.hasLight = true;
			light.lightType = GameObject::LightType::Spot;
			light.lightRange = 7.0f;
			light.lightDecay = 2.0f;
			light.lightSpotAngle = 30.0f;
			light.lightSpotFalloffStart = 20.0f;
			light.rotation = { 0.5f, 0.0f, 0.0f };
			light.rotationQuat = Quaternion::FromEuler(light.rotation); // world matrix / cone direction use the quaternion
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(TrId("Audio"))) {
		if (ImGui::MenuItem(TrId("Audio Source"))) {
			auto& audio = CreateGameObject("Audio Source", GameObjectType::AudioSource);
			audio.hasAudioSource = true;
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(TrId("UI (Canvas)"))) {
		if (ImGui::MenuItem(TrId("Canvas"))) {
			CreateGameObject("Canvas", GameObjectType::Canvas);
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();
	if (ImGui::BeginMenu(TrId("Samples"))) {
		const bool canCreate = (sceneState_ == EditorSceneState::Edit) && !currentProjectRoot_.empty();
		if (ImGui::MenuItem(TrId("Survivors Demo (new scene)"), nullptr, false, canCreate)) {
			CreateSurvivorsDemo();
		}
		if (ImGui::MenuItem(TrId("Third Person Demo (new scene)"), nullptr, false, canCreate)) {
			CreateThirdPersonDemo();
		}
		ImGui::SetItemTooltip("%s", Tr("Creates Assets/Scenes/SurvivorsDemo.scene.json and the Enemy / Bullet prefabs,\n"
			"using the Survivors* sample scripts in GameScripts. Unsaved changes in the current scene are discarded."));
		ImGui::EndMenu();
	}
#endif
}

void EditorSuite::DrawViewportsPanel()
{
#ifdef USE_IMGUI
	ImGuiIO& io = ImGui::GetIO();
	float screenW = io.DisplaySize.x;
	float screenH = io.DisplaySize.y;
	float topY = ImGui::GetFrameHeight() + kTopBarHeight;
	float leftW = screenW * 0.18f;
	if (leftW < 220.0f) leftW = 220.0f;
	float rightW = screenW * 0.24f;
	if (rightW < 300.0f) rightW = 300.0f;
	float centerW = screenW - leftW - rightW;
	float midH = (screenH - topY) * 0.65f;

	ImGui::SetNextWindowPos(ImVec2(leftW, topY));
	ImGui::SetNextWindowSize(ImVec2(centerW, midH));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

	if (ImGui::Begin(TrId("Viewport"), nullptr, flags)) {
		DrawPrefabModeBar();
		if (ImGui::BeginTabBar("ViewportTabs")) {
			// TAB 1: # SCENE VIEW (Interactive 3D Viewport)
			if (ImGui::BeginTabItem(TrId("# Scene"))) {
				ImVec2 avail = ImGui::GetContentRegionAvail();
				ImVec2 imgPos = ImGui::GetCursorScreenPos();

				ImGui::Image(ImTextureRef((ImTextureID)editViewRT_.GetSrvGPUHandle().ptr), avail);

				bool isHovered = ImGui::IsItemHovered();

				// Flight Camera Navigation (RMB held inside # Scene)
				if (isHovered && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
					isFlightNavigating_ = true;
					ImVec2 mouseDelta = io.MouseDelta;
					float rotSpeed = 0.003f;
					editorCamera_.rotation.y += mouseDelta.x * rotSpeed;
					editorCamera_.rotation.x += mouseDelta.y * rotSpeed;
					editorCamera_.rotation.x = std::clamp(editorCamera_.rotation.x, -1.5f, 1.5f);

					float moveSpeed = 8.0f * io.DeltaTime;
					if (io.KeyShift) moveSpeed *= 2.5f;

					Vector3 forward = { std::sin(editorCamera_.rotation.y), 0.0f, std::cos(editorCamera_.rotation.y) };
					Vector3 right = { std::cos(editorCamera_.rotation.y), 0.0f, -std::sin(editorCamera_.rotation.y) };

					if (ImGui::IsKeyDown(ImGuiKey_W)) editorCamera_.translation = editorCamera_.translation + (forward * moveSpeed);
					if (ImGui::IsKeyDown(ImGuiKey_S)) editorCamera_.translation = editorCamera_.translation - (forward * moveSpeed);
					if (ImGui::IsKeyDown(ImGuiKey_D)) editorCamera_.translation = editorCamera_.translation + (right * moveSpeed);
					if (ImGui::IsKeyDown(ImGuiKey_A)) editorCamera_.translation = editorCamera_.translation - (right * moveSpeed);
					if (ImGui::IsKeyDown(ImGuiKey_E)) editorCamera_.translation.y += moveSpeed;
					if (ImGui::IsKeyDown(ImGuiKey_Q)) editorCamera_.translation.y -= moveSpeed;
				} else {
					isFlightNavigating_ = false;
				}

				// MMB Pan
				if (isHovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
					float panSpeed = 0.015f;
					Vector3 right = { std::cos(editorCamera_.rotation.y), 0.0f, -std::sin(editorCamera_.rotation.y) };
					editorCamera_.translation = editorCamera_.translation - (right * (io.MouseDelta.x * panSpeed));
					editorCamera_.translation.y += io.MouseDelta.y * panSpeed;
				}

				// Mouse Wheel Zoom
				if (isHovered && io.MouseWheel != 0.0f) {
					Vector3 forward = { std::sin(editorCamera_.rotation.y), -std::sin(editorCamera_.rotation.x), std::cos(editorCamera_.rotation.y) };
					editorCamera_.translation = editorCamera_.translation + (forward * (io.MouseWheel * 1.5f));
				}

				UpdateTerrainSculptInput(imgPos, avail, isHovered);
				DrawSceneViewOverlays(imgPos, avail);
				ImGui::EndTabItem();
			}

			// TAB 2: GAME VIEW (Player Camera)
			if (ImGui::BeginTabItem(TrId("Game"))) {
				ImVec2 avail = ImGui::GetContentRegionAvail();
				ImVec2 imgPos = ImGui::GetCursorScreenPos();
				ImGui::Image(ImTextureRef((ImTextureID)gameViewRT_.GetSrvGPUHandle().ptr), avail);
				RecordGameViewRect(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y, ImGui::GetItemRectSize().x, ImGui::GetItemRectSize().y);

				if (isTetrisActive_) {
					DrawTetrisHUD(imgPos, avail);
				}
				ImGui::EndTabItem();
			}

			// TAB 3: DUAL SPLIT VIEW
			if (ImGui::BeginTabItem(TrId("Dual Split"))) {
				ImVec2 avail = ImGui::GetContentRegionAvail();
				float halfW = (avail.x - 8.0f) * 0.5f;
				ImGui::Image(ImTextureRef((ImTextureID)editViewRT_.GetSrvGPUHandle().ptr), ImVec2(halfW, avail.y));
				ImGui::SameLine();
				ImGui::Image(ImTextureRef((ImTextureID)gameViewRT_.GetSrvGPUHandle().ptr), ImVec2(halfW, avail.y));
				RecordGameViewRect(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y, ImGui::GetItemRectSize().x, ImGui::GetItemRectSize().y);
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
		if (sceneState_ == EditorSceneState::Play || sceneState_ == EditorSceneState::Pause) {
			const ImVec2 wPos = ImGui::GetWindowPos();
			const ImVec2 wSize = ImGui::GetWindowSize();
			const ImU32 borderCol = (sceneState_ == EditorSceneState::Play)
				? IM_COL32(55, 205, 95, 210)
				: IM_COL32(235, 175, 50, 210);
			ImGui::GetForegroundDrawList()->AddRect(
				wPos, ImVec2(wPos.x + wSize.x, wPos.y + wSize.y), borderCol, 0.0f, 0, 2.5f);
		}
	}
	ImGui::End();
#endif
}

