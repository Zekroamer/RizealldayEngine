#include "EditorSuiteInternal.h"
#include "EngineUpdate.h"
#include "base/EngineVersion.h"
#include "Localization.h"

// Engine updates without a server: the team gets a new release zip, picks it in Help > Install Engine Update...
// (or drops it on the window); the new exe from the zip replaces the engine folders and restarts the editor.

namespace fs = std::filesystem;

namespace {
	fs::path OpenZipFileDialog()
	{
		fs::path result;
		const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		{
			Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
			if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog))) && dialog) {
				const COMDLG_FILTERSPEC filters[] = { { L"Engine package (*.zip)", L"*.zip" } };
				dialog->SetFileTypes(1, filters);
				dialog->SetTitle(L"Install Engine Update");
				Microsoft::WRL::ComPtr<IShellItem> item;
				PWSTR path = nullptr;
				if (SUCCEEDED(dialog->Show(nullptr)) && SUCCEEDED(dialog->GetResult(&item)) && item &&
					SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
					result = path;
					CoTaskMemFree(path);
				}
			}
		}
		if (SUCCEEDED(hrInit)) CoUninitialize();
		return result;
	}
}

bool EditorSuite::BeginEngineUpdate(const fs::path& zip)
{
	engineUpdateError_.clear();
	const fs::path installRoot = EngineUpdate::FindInstallRoot(GetExecutableDirectory());
	if (installRoot.empty()) {
		LogMessage("[Update] This editor runs from the engine source workspace; updates install only into a packaged engine (Editor/ next to engine.json).", 2);
		return false;
	}

	std::error_code ec;
	const fs::path updatesDir = fs::temp_directory_path(ec) / "RizealldayUpdate";
	fs::remove_all(updatesDir, ec); // packages of earlier updates
	const fs::path extractDir = updatesDir / std::to_string(GetTickCount64());
	std::string error;
	if (!EngineUpdate::ExtractZip(zip, extractDir, &error)) {
		LogMessage("[Update] " + error, 2);
		return false;
	}
	const auto package = EngineUpdate::FindPackage(extractDir);
	if (!package) {
		LogMessage("[Update] " + zip.filename().string() + " is not an engine package (no engine.json).", 2);
		return false;
	}
	pendingUpdateRoot_ = package->root;
	pendingUpdateVersion_ = package->version;
	pendingUpdateScriptApi_ = package->scriptApi;
	showEngineUpdateModal_ = true;
	LogMessage(std::format("[Update] Engine package {} ready to install (installed: {}).", package->version, kEngineVersion), 0);
	return true;
}

void EditorSuite::DrawEngineUpdateModal()
{
#ifdef USE_IMGUI
	const char* popupId = TrId("Install Engine Update");
	ImGui::OpenPopup(popupId);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;

	ImGui::Text(Tr("%s: %s  ->  %s"), Tr("Version"), kEngineVersion, pendingUpdateVersion_.c_str());
	if (CompareEngineVersions(pendingUpdateVersion_, kEngineVersion) <= 0) {
		ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "%s", Tr("This package is not newer than the installed engine."));
	}
	if (pendingUpdateScriptApi_ != static_cast<int>(kScriptApiVersion)) {
		ImGui::TextWrapped("%s", Tr("The script API changes: each project's scripts are rebuilt automatically when it is opened."));
	}
	ImGui::TextDisabled("%s", Tr("The editor closes, installs the update and starts again. Game projects are not changed."));
	if (isSceneDirty_) ImGui::TextDisabled("%s", Tr("Unsaved scene changes are saved first."));
	if (!engineUpdateError_.empty()) ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", engineUpdateError_.c_str());
	ImGui::Spacing();

	if (ImGui::Button(TrId("Install and Restart"))) {
		engineUpdateError_.clear();
		if (sceneState_ != EditorSceneState::Edit) StopPlayMode();
		if (isSceneDirty_ && (currentScenePath_.empty() || !SaveScene(currentScenePath_))) {
			engineUpdateError_ = Tr("Could not save the scene. Save it with File > Save Scene As..., then try again.");
		} else {
			EngineUpdate::PackageInfo package{ pendingUpdateRoot_, pendingUpdateVersion_, pendingUpdateScriptApi_ };
			std::string error;
			if (EngineUpdate::LaunchApplyUpdate(package, EngineUpdate::FindInstallRoot(GetExecutableDirectory()), &error)) {
				LogMessage("[Update] Installing engine " + pendingUpdateVersion_ + "; the editor restarts.", 0);
				showEngineUpdateModal_ = false;
				ImGui::CloseCurrentPopup();
				PostQuitMessage(0); // the updater waits for this process to exit
			} else {
				engineUpdateError_ = error;
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(TrId("Cancel"))) {
		showEngineUpdateModal_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
#endif
}

void EditorSuite::DrawAboutModal()
{
#ifdef USE_IMGUI
	const char* popupId = TrId("About Rizeallday Engine");
	ImGui::OpenPopup(popupId);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal(popupId, &showAboutModal_, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
	ImGui::Text(Tr("Rizeallday Engine %s"), kEngineVersion);
	ImGui::TextDisabled(Tr("%s: %u"), Tr("Script API"), kScriptApiVersion);
	const fs::path installRoot = EngineUpdate::FindInstallRoot(GetExecutableDirectory());
	ImGui::TextDisabled(Tr("%s: %s"), Tr("Installed at"), installRoot.empty() ? Tr("source workspace") : installRoot.string().c_str());
	ImGui::TextDisabled(Tr("%s: %s"), Tr("Engine SDK"), ResolveEngineSdkDir().string().c_str());
	ImGui::Spacing();
	if (ImGui::Button(TrId("Close"))) {
		showAboutModal_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
#endif
}

// Help menu entry (kept here with the rest of the update flow)
void EditorSuite::DrawHelpMenu()
{
#ifdef USE_IMGUI
	if (ImGui::MenuItem(TrId("Install Engine Update..."))) {
		const fs::path zip = OpenZipFileDialog();
		if (!zip.empty()) BeginEngineUpdate(zip);
	}
	ImGui::SetItemTooltip("%s", Tr("Pick the engine release zip. You can also drop the zip on the editor window."));
	ImGui::Separator();
	if (ImGui::MenuItem(TrId("About Rizeallday Engine"))) showAboutModal_ = true;
#endif
}
