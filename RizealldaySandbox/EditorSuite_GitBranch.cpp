#include "EditorSuiteInternal.h"
#include "EngineUpdate.h"
#include "SceneJson.h"
#include "Localization.h"

// Branch menu: a team member sees and changes the project's git branch without leaving the editor. Commit, pull and
// push stay in Visual Studio. git runs hidden (EngineUpdate::RunHiddenProcess); its own message is shown when it refuses.

namespace {
	std::string Trimmed(std::string text)
	{
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
		return text;
	}

	/// A name that is safe as one quoted argument (git itself checks the rest)
	bool SafeArgument(const std::string& name)
	{
		return !name.empty() && name[0] != '-' && name.find('"') == std::string::npos;
	}

	std::wstring Wide(const std::string& utf8) { return std::filesystem::path(utf8).wstring(); } // the process code page is UTF-8

	constexpr const char* kDetachedHead = "(detached)";
}

void EditorSuite::RefreshGitBranch()
{
	gitHeadRoot_ = currentProjectRoot_;
	std::error_code ec;
	gitHeadTime_ = std::filesystem::last_write_time(currentProjectRoot_ / ".git" / "HEAD", ec);
	gitBranch_.clear();
	if (currentProjectRoot_.empty()) return;
	std::string output;
	// -1 = git is not installed; non-zero = not a working tree. Both hide the button.
	if (EngineUpdate::RunHiddenProcess(L"git branch --show-current", currentProjectRoot_, &output) != 0) return;
	gitBranch_ = Trimmed(output);
	if (gitBranch_.empty()) gitBranch_ = kDetachedHead;
}

void EditorSuite::PollGitHead()
{
	if (IsStandaloneMode()) return; // an exported game has no branch menu
	std::error_code ec;
	const std::filesystem::file_time_type time = std::filesystem::last_write_time(currentProjectRoot_ / ".git" / "HEAD", ec);
	if (gitHeadRoot_ == currentProjectRoot_ && time == gitHeadTime_) return;
	RefreshGitBranch(); // another project, or a switch made in Visual Studio
}

bool EditorSuite::SwitchGitBranch(const std::string& branch, UnsavedChoice unsaved)
{
	gitBranchError_.clear();
	auto refuse = [&](std::string message) {
		gitBranchError_ = std::move(message);
		LogMessage("[Git] " + gitBranchError_, 2);
		return false;
	};
	if (sceneState_ != EditorSceneState::Edit) return refuse("Stop Play mode before switching the branch.");
	if (currentProjectRoot_.empty()) return refuse("No project is open.");
	if (AssetDatabase::GitIsBusy(currentProjectRoot_)) return refuse("Git is busy (another git command is running). Try again in a moment.");
	if (!SafeArgument(branch)) return refuse("Not a branch name: " + branch);

	if (unsaved == UnsavedChoice::Save) {
		if (prefabMode_.active && isSceneDirty_ && !SavePrefabMode()) return refuse("Could not save the prefab; the branch was not switched.");
		if (prefabMode_.active && prefabMode_.sceneDirty) ClosePrefabMode(); // the scene's own edits are saved next
		if (!prefabMode_.active && isSceneDirty_ && !currentScenePath_.empty() && !SaveScene(currentScenePath_)) {
			return refuse("Could not save the scene; the branch was not switched.");
		}
	}

	// A remote-only "origin/x" becomes `git switch x`, which creates the local branch that tracks it
	const bool remote = std::find(gitBranchList_.remoteOnly.begin(), gitBranchList_.remoteOnly.end(), branch) != gitBranchList_.remoteOnly.end();
	const std::string target = remote ? GitBranches::LocalNameOf(branch) : branch;
	std::string output;
	const int code = EngineUpdate::RunHiddenProcess(L"git switch \"" + Wide(target) + L"\"", currentProjectRoot_, &output);
	if (code != 0) {
		showBranchErrorModal_ = true;
		return refuse(code == -1 ? std::string("Git is not installed (not on PATH).") : Trimmed(output));
	}
	RefreshGitBranch();
	LogMessage(std::format("[Git] Switched to branch {}.", gitBranch_), 0);

	// The files are the other branch's now: reload what the editor holds of them
	if (prefabMode_.active) ClosePrefabMode(); // unsaved prefab edits were the user's "without saving" choice
	const std::string scenePath = currentScenePath_;
	std::error_code ec;
	if (!scenePath.empty() && std::filesystem::exists(scenePath, ec)) {
		LoadScene(scenePath); // also loads the merge report, which stays hidden when it belongs to another branch
	} else if (!scenePath.empty()) {
		std::string startup;
		std::ifstream in(currentProjectRoot_ / "ProjectSettings" / "project.json", std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (const auto settings = SceneJson::Parse(text)) SceneJson::Read(*settings, "startupScene", startup);
		const std::filesystem::path startupPath = currentProjectRoot_ / startup;
		LogMessage(std::format("[Git] {} does not exist on this branch.", scenePath), 1);
		if (!startup.empty() && std::filesystem::exists(startupPath, ec)) LoadScene(startupPath.string());
		else NewScene();
	}
	// An untitled scene has no file on any branch: it stays as it is
	ResetEditorHistory();
	AssetDb().Refresh();
	SyncScriptModuleWithProject(); // builds when the branch has other scripts than the DLL
	return true;
}

bool EditorSuite::CreateGitBranch(const std::string& name)
{
	gitBranchError_.clear();
	auto refuse = [&](std::string message) {
		gitBranchError_ = std::move(message);
		LogMessage("[Git] " + gitBranchError_, 2);
		return false;
	};
	if (currentProjectRoot_.empty()) return refuse("No project is open.");
	if (AssetDatabase::GitIsBusy(currentProjectRoot_)) return refuse("Git is busy (another git command is running). Try again in a moment.");
	std::string output;
	if (!SafeArgument(name) ||
		EngineUpdate::RunHiddenProcess(L"git check-ref-format --branch \"" + Wide(name) + L"\"", currentProjectRoot_, &output) != 0) {
		return refuse(std::format("\"{}\" cannot be a branch name (no spaces, and none of ~ ^ : ? * [ \\ or ..).", name));
	}
	output.clear();
	if (EngineUpdate::RunHiddenProcess(L"git switch -c \"" + Wide(name) + L"\"", currentProjectRoot_, &output) != 0) return refuse(Trimmed(output));
	RefreshGitBranch();
	LogMessage(std::format("[Git] Created branch {} and switched to it.", gitBranch_), 0);
	return true;
}

void EditorSuite::DrawGitBranchMenu()
{
#ifdef USE_IMGUI
	if (gitBranch_.empty()) return;
	const std::string shown = std::format("{} {}", Tr("Branch:"), gitBranch_ == kDetachedHead ? Tr("(detached)") : gitBranch_.c_str());
	const std::string label = shown + "###GitBranchMenu";
	// At the right end of the bar (never over the menus or notices before it)
	const float width = ImGui::CalcTextSize(shown.c_str()).x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, ImGui::GetContentRegionAvail().x - width));
	const bool inPlay = sceneState_ != EditorSceneState::Edit;
	const bool open = ImGui::BeginMenu(label.c_str(), !inPlay);
	if (inPlay && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", Tr("Stop Play mode to switch the branch."));
	if (!open) {
		branchMenuOpen_ = false;
		return;
	}
	const bool busy = AssetDatabase::GitIsBusy(currentProjectRoot_);
	if (!branchMenuOpen_ && !busy) {
		// Read once per opening: git is never asked every frame
		std::string output;
		gitBranchList_ = EngineUpdate::RunHiddenProcess(GitBranches::kListCommand, currentProjectRoot_, &output) == 0 ? GitBranches::Parse(output) : GitBranches::List{};
	}
	branchMenuOpen_ = true;
	if (busy) {
		ImGui::TextDisabled("%s", Tr("Git is busy"));
	} else {
		auto request = [&](const std::string& branch) {
			if (isSceneDirty_ || (prefabMode_.active && prefabMode_.sceneDirty)) {
				pendingBranchSwitch_ = branch;
				showBranchSaveModal_ = true;
			} else {
				SwitchGitBranch(branch, UnsavedChoice::Discard);
			}
		};
		for (const std::string& branch : gitBranchList_.local) {
			const bool current = branch == gitBranchList_.current;
			if (ImGui::MenuItem((branch + "##localBranch").c_str(), nullptr, current) && !current) request(branch);
		}
		if (!gitBranchList_.remoteOnly.empty()) {
			ImGui::Separator();
			ImGui::TextDisabled("%s", Tr("On the server only"));
			for (const std::string& branch : gitBranchList_.remoteOnly) {
				if (ImGui::MenuItem((branch + "##remoteBranch").c_str())) request(branch);
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem(TrId("New Branch..."))) {
			newBranchNameBuf_[0] = '\0';
			gitBranchError_.clear();
			showNewBranchModal_ = true;
		}
	}
	if (!gitBranchError_.empty()) {
		ImGui::Separator();
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 360.0f);
		ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", gitBranchError_.c_str());
		ImGui::PopTextWrapPos();
	}
	ImGui::EndMenu();
#endif
}

void EditorSuite::DrawGitBranchModals()
{
#ifdef USE_IMGUI
	if (showBranchSaveModal_) {
		const char* popupId = TrId("Switch Branch");
		ImGui::OpenPopup(popupId);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::TextUnformatted(std::format("{} {}", Tr("Switch to branch:"), pendingBranchSwitch_).c_str());
			ImGui::TextUnformatted(Tr("The scene has unsaved changes."));
			ImGui::Spacing();
			int choice = -1;
			if (ImGui::Button(TrId("Save and switch"))) choice = 0;
			ImGui::SameLine();
			if (ImGui::Button(TrId("Switch without saving"))) choice = 1;
			ImGui::SameLine();
			if (ImGui::Button(TrId("Cancel"))) choice = 2;
			if (choice >= 0) {
				showBranchSaveModal_ = false;
				ImGui::CloseCurrentPopup();
				if (choice < 2) SwitchGitBranch(pendingBranchSwitch_, choice == 0 ? UnsavedChoice::Save : UnsavedChoice::Discard);
			}
			ImGui::EndPopup();
		}
	}
	if (showBranchErrorModal_ && !showBranchSaveModal_) {
		const char* popupId = TrId("Git did not switch the branch");
		ImGui::OpenPopup(popupId);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::TextUnformatted(Tr("Nothing was changed. Git says:"));
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
			ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", gitBranchError_.c_str());
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
			if (ImGui::Button(TrId("OK"))) {
				showBranchErrorModal_ = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
	if (showNewBranchModal_) {
		const char* popupId = TrId("New Branch");
		ImGui::OpenPopup(popupId);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::TextUnformatted(std::format("{} {}", Tr("Made from:"), gitBranch_).c_str());
			if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
			const bool enter = ImGui::InputText(TrId("Name"), newBranchNameBuf_, sizeof(newBranchNameBuf_), ImGuiInputTextFlags_EnterReturnsTrue);
			if (!gitBranchError_.empty()) ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", gitBranchError_.c_str());
			ImGui::Spacing();
			if ((ImGui::Button(TrId("Create")) || enter) && CreateGitBranch(newBranchNameBuf_)) {
				showNewBranchModal_ = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button(TrId("Cancel"))) {
				showNewBranchModal_ = false;
				gitBranchError_.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
#endif
}
