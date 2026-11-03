#include "EditorSuiteInternal.h"
#include "AutoSave.h"
#include <fstream>
#include <sstream>

// Editor auto-save: a copy of the open scene with unsaved changes, so a crash or a power cut loses at most a few minutes.

void EditorSuite::UpdateAutoSave(double now)
{
	if (!autoSaveEnabled_ || currentProjectRoot_.empty() || prefabMode_.active || isRunningTests_ && !autoSaveInTests_) return;
	if (!AutoSave::IsDue(now, lastAutoSaveTime_, autoSaveIntervalSeconds_, isSceneDirty_, sceneState_ == EditorSceneState::Edit)) return;
	lastAutoSaveTime_ = now;
	const std::filesystem::path file = AutoSave::PathFor(currentProjectRoot_, currentScenePath_);
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	if (!out) {
		LogMessage(std::format("[AutoSave] Could not write {}", file.string()), 1);
		return;
	}
	out << SerializeSceneToString();
	LogMessage(std::format("[AutoSave] Saved a copy to {}", file.string()), 0);
}

void EditorSuite::DiscardAutoSave(const std::string& scenePath)
{
	if (currentProjectRoot_.empty()) return;
	std::error_code ec;
	std::filesystem::remove(AutoSave::PathFor(currentProjectRoot_, scenePath), ec);
}

void EditorSuite::CheckForAutoSave(const std::string& scenePath)
{
	pendingAutoSaveRestore_.clear();
	if (currentProjectRoot_.empty() || IsStandaloneMode()) return;
	const std::filesystem::path copy = AutoSave::PathFor(currentProjectRoot_, scenePath);
	if (AutoSave::IsNewerThanScene(copy, scenePath)) pendingAutoSaveRestore_ = copy.string();
}

bool EditorSuite::RestoreAutoSave()
{
	if (pendingAutoSaveRestore_.empty()) return false;
	std::ifstream in(pendingAutoSaveRestore_, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	const std::string scenePath = currentScenePath_;
	const std::string sceneName = currentSceneName_;
	pendingAutoSaveRestore_.clear();
	if (!in || !DeserializeSceneFromString(text.str())) {
		LogMessage("[AutoSave] The auto-saved copy could not be read.", 2);
		return false;
	}
	currentScenePath_ = scenePath;
	currentSceneName_ = sceneName;
	isSceneDirty_ = true; // restored, not saved yet: Save writes it to the scene file
	ResetUndoHistory();
	LogMessage("[AutoSave] Restored the auto-saved copy. Save the scene to keep it.", 0);
	return true;
}

void EditorSuite::DrawAutoSaveRestoreModal()
{
#ifdef USE_IMGUI
	if (pendingAutoSaveRestore_.empty()) return;
	ImGui::OpenPopup(TrId("Restore Auto-Save?"));
	if (ImGui::BeginPopupModal(TrId("Restore Auto-Save?"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted(Tr("This scene has an auto-saved copy that is newer than the saved file."));
		ImGui::TextUnformatted(Tr("The editor may have closed without saving."));
		if (ImGui::Button(TrId("Restore"))) {
			RestoreAutoSave();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(TrId("Discard"))) {
			std::error_code ec;
			std::filesystem::remove(pendingAutoSaveRestore_, ec);
			pendingAutoSaveRestore_.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
#endif
}
