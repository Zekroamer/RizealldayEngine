#include "EditorApp.h"
#include "EngineUpdate.h"
#include "MergeDriver.h"
#include <Windows.h>
#include <filesystem>
#include <memory>

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	// git runs `<exe> --merge-driver %O %A %B %P` during a merge: no window, no GPU, paths relative to the repository root
	if (MergeDriver::IsMergeDriverLaunch()) {
		return MergeDriver::RunMergeDriverMode();
	}

	// Help > Install Engine Update... starts the new exe in this mode; it never opens a window
	if (EngineUpdate::IsApplyUpdateLaunch()) {
		return EngineUpdate::RunApplyUpdateMode();
	}

	// shaders/ and Resources/ sit next to the exe; a shortcut or Explorer may start it from another folder
	{
		wchar_t exePath[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		const std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
		std::error_code ec;
		if (!std::filesystem::exists("shaders", ec) && std::filesystem::exists(exeDir / "shaders", ec)) {
			std::filesystem::current_path(exeDir, ec);
		}
	}

	auto app = std::make_unique<EditorApp>();
	app->Run();
	return EditorApp::GetExitCode();
}
