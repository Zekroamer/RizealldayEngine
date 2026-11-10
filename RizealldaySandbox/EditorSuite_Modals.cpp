#include "EditorSuiteInternal.h"

using EditorSuiteDetail::SceneNameFromPath;
using EditorSuiteDetail::IsSceneFile;

void EditorSuite::DrawSaveSceneAsModal()
{
#ifdef USE_IMGUI
	ImGui::OpenPopup(TrId("Save Scene As..."));
	if (ImGui::BeginPopupModal(TrId("Save Scene As..."), &showSaveSceneAsModal_, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text(Tr("Scene name or file path:"));
		ImGui::SetNextItemWidth(420.0f);
		ImGui::InputText("##SaveScenePath", saveSceneAsPathBuf_, sizeof(saveSceneAsPathBuf_));

		// Show where the file will actually be written (bare names go to the project's Assets/Scenes)
		const std::string targetPath = NormalizeScenePath(saveSceneAsPathBuf_);
		ImGui::TextDisabled(Tr("Saves to: %s"), targetPath.empty() ? "-" : targetPath.c_str());

		ImGui::Separator();
		ImGui::BeginDisabled(targetPath.empty());
		if (ImGui::Button(TrId("Save"), ImVec2(100.0f, 0.0f))) {
			if (SaveScene(targetPath)) {
				strncpy_s(saveSceneAsPathBuf_, targetPath.c_str(), sizeof(saveSceneAsPathBuf_) - 1);
				strncpy_s(openScenePathBuf_, targetPath.c_str(), sizeof(openScenePathBuf_) - 1);
			}
			showSaveSceneAsModal_ = false;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button(TrId("Cancel"), ImVec2(90.0f, 0.0f))) {
			showSaveSceneAsModal_ = false;
		}
		ImGui::EndPopup();
	}
#endif
}

void EditorSuite::DrawOpenSceneModal()
{
#ifdef USE_IMGUI
	ImGui::OpenPopup(TrId("Open Scene..."));
	if (ImGui::BeginPopupModal(TrId("Open Scene..."), &showOpenSceneModal_, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text(Tr("Select a scene (double-click to open):"));

		// Only scenes that belong to the open project (its Assets/ folder, recursively) are listed
		std::vector<std::filesystem::path> sceneFiles;
		if (!currentProjectRoot_.empty()) {
			const std::filesystem::path assetsRoot = currentProjectRoot_ / "Assets";
			std::error_code ec;
			if (std::filesystem::is_directory(assetsRoot, ec)) {
				for (auto it = std::filesystem::recursive_directory_iterator(assetsRoot, std::filesystem::directory_options::skip_permission_denied, ec);
					it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
					if (ec) break;
					if (it->is_regular_file(ec) && IsSceneFile(it->path())) sceneFiles.push_back(it->path());
				}
			}
			std::sort(sceneFiles.begin(), sceneFiles.end());
		}

		ImGui::TextDisabled(Tr("Project: %s"), currentProjectRoot_.empty() ? "(none)" : currentProjectRoot_.filename().string().c_str());
		ImGui::BeginChild("SceneListArea", ImVec2(460.0f, 200.0f), ImGuiChildFlags_Borders);
		if (currentProjectRoot_.empty()) {
			ImGui::TextDisabled(Tr("No project is open. Create or open a project first."));
		} else if (sceneFiles.empty()) {
			ImGui::TextDisabled(Tr("No scenes found in this project's Assets/."));
		}
		for (const auto& scenePath : sceneFiles) {
			const std::string pathStr = scenePath.string();
			const bool isSel = (pathStr == openScenePathBuf_);
			const bool isCurrent = (pathStr == currentScenePath_);
			std::string label = SceneNameFromPath(scenePath) + (isCurrent ? "  (open)" : "");
			if (ImGui::Selectable((label + "##" + pathStr).c_str(), isSel, ImGuiSelectableFlags_AllowDoubleClick)) {
				strncpy_s(openScenePathBuf_, pathStr.c_str(), sizeof(openScenePathBuf_) - 1);
				if (ImGui::IsMouseDoubleClicked(0)) {
					LoadScene(pathStr);
					showOpenSceneModal_ = false;
				}
			}
			ImGui::SetItemTooltip("%s", pathStr.c_str());
			ImGui::SameLine(300.0f);
			ImGui::TextDisabled("%s", scenePath.parent_path().filename().string().c_str());
		}
		ImGui::EndChild();

		ImGui::SetNextItemWidth(460.0f);
		ImGui::InputText("##OpenScenePath", openScenePathBuf_, sizeof(openScenePathBuf_));

		ImGui::Separator();
		const bool pathExists = openScenePathBuf_[0] != '\0' && std::filesystem::exists(openScenePathBuf_);
		const bool insideProject = pathExists && IsPathInsideProject(openScenePathBuf_);
		const bool canOpen = pathExists && insideProject;
		if (pathExists && !insideProject) {
			ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), Tr("This scene is outside the current project."));
		}
		ImGui::BeginDisabled(!canOpen);
		if (ImGui::Button(TrId("Open"), ImVec2(100.0f, 0.0f))) {
			LoadScene(openScenePathBuf_);
			showOpenSceneModal_ = false;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button(TrId("Cancel"), ImVec2(90.0f, 0.0f))) {
			showOpenSceneModal_ = false;
		}
		ImGui::EndPopup();
	}
#endif
}

void EditorSuite::DrawNewProjectModal()
{
#ifdef USE_IMGUI
	ImGui::OpenPopup(TrId("New Project"));
	if (ImGui::BeginPopupModal(TrId("New Project"), &showNewProjectModal_, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text(Tr("Create a new, empty game project."));
		ImGui::Separator();

		ImGui::SetNextItemWidth(320.0f);
		ImGui::InputText(TrId("Project Name"), projectNameBuf_, sizeof(projectNameBuf_));
		ImGui::SetNextItemWidth(320.0f);
		ImGui::InputText(TrId("Location"), projectFolderBuf_, sizeof(projectFolderBuf_));
		ImGui::SameLine();
		if (ImGui::Button(TrId("Browse..."))) {
			std::string chosen = OpenNativeFolderDialog();
			if (!chosen.empty()) strncpy_s(projectFolderBuf_, chosen.c_str(), sizeof(projectFolderBuf_) - 1);
		}

		const std::filesystem::path previewPath = std::filesystem::path(projectFolderBuf_) / projectNameBuf_;
		ImGui::TextDisabled(Tr("Project folder: %s"), previewPath.string().c_str());
		ImGui::TextDisabled(Tr("Creates Assets/, GameScripts/ (C++ scripts), ProjectSettings/project.json"));
		ImGui::TextDisabled(Tr("and a default scene, then switches the editor to the new project."));
		ImGui::Checkbox(TrId("Initialize Git repository"), &newProjectInitGit_);

		if (!projectModalError_.empty()) {
			ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", projectModalError_.c_str());
		}

		ImGui::Separator();
		if (ImGui::Button(TrId("Create Project"), ImVec2(130.0f, 0.0f))) {
			if (CreateNewProject(projectFolderBuf_, projectNameBuf_)) {
				showNewProjectModal_ = false;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(TrId("Cancel"), ImVec2(90.0f, 0.0f))) {
			projectModalError_.clear();
			showNewProjectModal_ = false;
		}
		ImGui::EndPopup();
	}
#endif
}

void EditorSuite::DrawOpenProjectModal()
{
#ifdef USE_IMGUI
	ImGui::OpenPopup(TrId("Open Project"));
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(640.0f, 0.0f), ImGuiCond_Always);
	if (ImGui::BeginPopupModal(TrId("Open Project"), &showOpenProjectModal_, ImGuiWindowFlags_NoResize)) {
		// Recent projects: double-click (or select + Open Selected) to open
		ImGui::SeparatorText(Tr("Recent Projects"));
		if (DrawRecentProjectsList(200.0f)) {
			showOpenProjectModal_ = false;
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}
		const bool hasSelection = selectedRecentProject_ >= 0 && selectedRecentProject_ < static_cast<int>(recentProjects_.size());
		ImGui::BeginDisabled(!hasSelection);
		if (ImGui::Button(TrId("Open Selected"), ImVec2(130.0f, 26.0f)) && hasSelection) {
			if (OpenProject(recentProjects_[selectedRecentProject_].string())) {
				showOpenProjectModal_ = false;
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}
		}
		ImGui::SetItemTooltip("%s", Tr("Opens the project highlighted in the list (click one first, or double-click it)"));
		ImGui::EndDisabled();

		ImGui::SeparatorText(Tr("Other Folder"));
		ImGui::Text(Tr("Select a Project Folder (containing ProjectSettings/project.json):"));

		ImGui::InputText(TrId("Project Folder"), openProjectFolderBuf_, sizeof(openProjectFolderBuf_));
		ImGui::SameLine();
		if (ImGui::Button(TrId("[ Browse Folder... ]"))) {
			std::string chosen = OpenNativeFolderDialog();
			if (!chosen.empty()) strncpy_s(openProjectFolderBuf_, chosen.c_str(), sizeof(openProjectFolderBuf_) - 1);
		}

		ImGui::Separator();
		if (ImGui::Button(TrId("Open Project"), ImVec2(130.0f, 26.0f))) {
			if (OpenProject(openProjectFolderBuf_)) {
				showOpenProjectModal_ = false;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(TrId("Cancel"), ImVec2(90.0f, 26.0f))) {
			showOpenProjectModal_ = false;
		}
		ImGui::EndPopup();
	}
#endif
}

void EditorSuite::DrawBuildSettingsModal()
{
#ifdef USE_IMGUI
	ImGui::OpenPopup(TrId("Build Settings & Standalone Game Exporter"));
	if (ImGui::BeginPopupModal(TrId("Build Settings & Standalone Game Exporter"), &showBuildSettingsModal_, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text(Tr("Export Standalone DirectX 12 Game Build"));
		ImGui::Separator();

		ImGui::InputText(TrId("Target Output Folder"), buildOutputDirBuf_, sizeof(buildOutputDirBuf_));
		ImGui::SameLine();
		if (ImGui::Button(TrId("[ Browse Folder... ]"))) {
			std::string chosen = OpenNativeFolderDialog();
			if (!chosen.empty()) strncpy_s(buildOutputDirBuf_, chosen.c_str(), sizeof(buildOutputDirBuf_) - 1);
		}

		ImGui::InputText(TrId("Game Title"), buildGameTitleBuf_, sizeof(buildGameTitleBuf_));

		const char* resolutions[] = { "1920 x 1080 (FHD)", "1280 x 720 (HD)", "2560 x 1440 (2K QHD)" };
		ImGui::Combo(TrId("Window Resolution"), &buildResolutionIndex_, resolutions, 3);
		ImGui::Checkbox(TrId("Start in Fullscreen"), &buildFullscreen_);

		ImGui::InputText(TrId("Startup Scene"), buildStartupSceneBuf_, sizeof(buildStartupSceneBuf_));

		ImGui::Separator();
		if (ImGui::Button(TrId("[ Export Standalone Game ]"), ImVec2(200.0f, 28.0f))) {
			if (ExportStandaloneGame()) {
				showBuildSettingsModal_ = false;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(TrId("[ Open Output Folder ]"), ImVec2(160.0f, 28.0f))) {
			std::filesystem::path outDir(buildOutputDirBuf_);
			std::error_code ec;
			std::filesystem::create_directories(outDir, ec);
			std::filesystem::path absOut = std::filesystem::absolute(outDir, ec);
			ShellExecuteW(NULL, L"open", absOut.wstring().c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
		ImGui::SameLine();
		if (ImGui::Button(TrId("Cancel"), ImVec2(90.0f, 28.0f))) {
			showBuildSettingsModal_ = false;
		}
		ImGui::EndPopup();
	}
#endif
}


bool EditorSuite::OnCloseRequested()
{
	// Exported games and the Project Hub (no project open yet) have nothing to save
	if (IsStandaloneMode() || showProjectHub_ || !isSceneDirty_) {
		return true;
	}
	unsavedCloseError_.clear();
	closeSessionFiles_.clear();
	for (std::string& path : AssetsCreatedThisSession()) closeSessionFiles_.push_back({ std::move(path), false });
	showUnsavedCloseModal_ = true;
	return false;
}

void EditorSuite::NoteSessionStartAssets()
{
	sessionAssetsRoot_ = currentProjectRoot_;
	sessionStartGuids_.clear();
	if (currentProjectRoot_.empty()) return;
	AssetDatabase& db = AssetDb();
	db.Refresh();
	for (std::string& guid : db.AllGuids()) sessionStartGuids_.insert(std::move(guid));
}

std::vector<std::string> EditorSuite::AssetsCreatedThisSession()
{
	std::vector<std::string> made;
	if (currentProjectRoot_.empty() || currentProjectRoot_ != sessionAssetsRoot_) return made;
	AssetDatabase& db = AssetDb();
	db.Refresh(); // files written since the last scan (graphs, prefabs, terrain bakes) get their GUID now
	for (const std::string& guid : db.AllGuids()) {
		if (!sessionStartGuids_.contains(guid)) made.push_back(db.PathForGuid(guid));
	}
	std::sort(made.begin(), made.end());
	return made;
}

void EditorSuite::DiscardForClose()
{
	if (sceneState_ != EditorSceneState::Edit) StopPlayMode(); // assets cannot be deleted during Play
	for (const SessionFile& file : closeSessionFiles_) {
		if (file.remove) DeleteAssetToRecycleBin(file.path);
	}
	closeSessionFiles_.clear();
	DiscardAutoSave(currentScenePath_); // the next start must not offer the changes the user just threw away
}

void EditorSuite::DrawUnsavedChangesModal()
{
#ifdef USE_IMGUI
	const char* popupId = "Unsaved Changes##CloseConfirm";
	ImGui::OpenPopup(popupId);

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}

	ImGui::Text(Tr("The scene \"%s\" has unsaved changes."), currentSceneName_.c_str());
	ImGui::TextDisabled(Tr("Do you want to save them before closing?"));
	if (sceneState_ != EditorSceneState::Edit) {
		ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), Tr("Play mode is running: it will be stopped first (Play-mode changes are not saved)."));
	}
	if (!unsavedCloseError_.empty()) {
		ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", unsavedCloseError_.c_str());
	}
	int checkedFiles = 0;
	if (!closeSessionFiles_.empty()) {
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextUnformatted(Tr("Files made in this session are already saved on disk:"));
		ImGui::TextDisabled("%s", Tr("Check the ones \"Don't Save\" should move to the Recycle Bin."));
		if (ImGui::SmallButton(TrId("Check All"))) for (SessionFile& f : closeSessionFiles_) f.remove = true;
		ImGui::SameLine();
		if (ImGui::SmallButton(TrId("Check None"))) for (SessionFile& f : closeSessionFiles_) f.remove = false;
		const float rowHeight = ImGui::GetFrameHeightWithSpacing();
		const float listHeight = (std::min)(static_cast<float>(closeSessionFiles_.size()), 8.0f) * rowHeight + ImGui::GetStyle().WindowPadding.y * 2.0f;
		if (ImGui::BeginChild("##SessionFiles", ImVec2(460.0f, listHeight), ImGuiChildFlags_Borders)) {
			// The theme's frame colour equals the popup background: an unchecked box would be invisible
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_Button));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
			for (SessionFile& f : closeSessionFiles_) ImGui::Checkbox(f.path.c_str(), &f.remove);
			ImGui::PopStyleColor(2);
		}
		ImGui::EndChild();
		for (const SessionFile& f : closeSessionFiles_) checkedFiles += f.remove ? 1 : 0;
	}
	ImGui::Spacing();

	auto closeApplication = [&]() {
		showUnsavedCloseModal_ = false;
		ImGui::CloseCurrentPopup();
		PostQuitMessage(0); // the main loop ends on the next frame
	};

	const ImVec2 buttonSize(110.0f, 0.0f);
	if (ImGui::Button(TrId("Save"), buttonSize)) {
		if (sceneState_ != EditorSceneState::Edit) {
			StopPlayMode();
		}
		if (!currentScenePath_.empty() && SaveScene(currentScenePath_)) {
			closeApplication();
		} else {
			unsavedCloseError_ = "Could not save the scene. Use File > Save Scene As..., then close again.";
		}
	}
	ImGui::SetItemTooltip(Tr("Save to %s and close"), currentScenePath_.c_str());

	ImGui::SameLine();
	if (ImGui::Button(TrId("Don't Save"), buttonSize)) {
		DiscardForClose();
		closeApplication();
	}
	if (checkedFiles > 0) ImGui::SetItemTooltip(Tr("Close without saving the scene and move %d checked file(s) to the Recycle Bin"), checkedFiles);
	else ImGui::SetItemTooltip("%s", Tr("Close without saving the scene"));

	ImGui::SameLine();
	if (ImGui::Button(TrId("Cancel"), buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		showUnsavedCloseModal_ = false;
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
#endif
}
