#include "EngineUpdate.h"
#include "SceneJson.h"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace {
	fs::path TarExe()
	{
		wchar_t systemDir[MAX_PATH] = {};
		GetSystemDirectoryW(systemDir, MAX_PATH);
		return fs::path(systemDir) / L"tar.exe";
	}

	fs::path CurrentExePath()
	{
		wchar_t buf[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, buf, MAX_PATH);
		return fs::path(buf);
	}

	/// Value after `name` on the command line ("" when missing)
	std::wstring ArgValue(const std::wstring& name)
	{
		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
		std::wstring value;
		for (int i = 0; argv && i + 1 < argc; ++i) {
			if (name == argv[i]) {
				value = argv[i + 1];
				break;
			}
		}
		if (argv) LocalFree(argv);
		return value;
	}

	// Antivirus scanners and the search indexer hold a file for a moment: retry for about 5 s
	bool RenameWithRetry(const fs::path& from, const fs::path& to, std::error_code& ec)
	{
		for (int attempt = 0; attempt < 20; ++attempt) {
			if (attempt > 0) std::this_thread::sleep_for(std::chrono::milliseconds(250));
			ec.clear();
			fs::rename(from, to, ec);
			if (!ec) return true;
		}
		return false;
	}
}

namespace EngineUpdate {

std::wstring CommandLineValue(const std::wstring& name)
{
	return ArgValue(name);
}

void ReadOutputUntilExit(void* pipe, void* process, const std::function<void(const char* data, size_t size)>& sink)
{
	char buf[4096];
	bool exited = false;
	for (;;) {
		DWORD available = 0;
		if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) break; // every writer closed it
		if (available > 0) {
			DWORD read = 0;
			if (!ReadFile(pipe, buf, (std::min)(available, static_cast<DWORD>(sizeof(buf))), &read, nullptr) || read == 0) break;
			sink(buf, read);
			continue;
		}
		if (exited) break; // the child is gone and its output is drained
		exited = WaitForSingleObject(process, 20) == WAIT_OBJECT_0;
	}
	WaitForSingleObject(process, INFINITE);
}

int RunHiddenProcess(std::wstring cmdLine, const fs::path& workDir, std::string* output)
{
	SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
	HANDLE readPipe = nullptr, writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return -1;
	SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	si.hStdOutput = writePipe;
	si.hStdError = writePipe;
	PROCESS_INFORMATION pi{};
	const std::wstring dir = workDir.wstring();
	const BOOL started = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
		dir.empty() ? nullptr : dir.c_str(), &si, &pi);
	CloseHandle(writePipe);
	if (!started) {
		CloseHandle(readPipe);
		return -1;
	}
	ReadOutputUntilExit(readPipe, pi.hProcess, [output](const char* data, size_t size) {
		if (output) output->append(data, size);
	});
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	CloseHandle(readPipe);
	return static_cast<int>(code);
}

const std::vector<std::string>& EngineOwnedEntries()
{
	static const std::vector<std::string> entries = {
		"Editor", "Runtime", "SDK", "Templates", kManifestFile, "README_TEAM.md", "GUIDE_JA.md",
		"CHANGELOG.md", "SCRIPTING_GUIDE.md"
	};
	return entries;
}

std::optional<PackageInfo> FindPackage(const fs::path& dir)
{
	auto readManifest = [](const fs::path& root) -> std::optional<PackageInfo> {
		std::ifstream in(root / kManifestFile);
		if (!in.is_open()) return std::nullopt;
		std::stringstream text;
		text << in.rdbuf();
		const auto json = SceneJson::Parse(text.str());
		if (!json) return std::nullopt;
		PackageInfo info;
		info.root = root;
		SceneJson::Read(*json, "version", info.version);
		SceneJson::Read(*json, "scriptApi", info.scriptApi);
		if (info.version.empty()) return std::nullopt;
		return info;
	};
	if (auto info = readManifest(dir)) return info;
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(dir, ec)) {
		if (entry.is_directory(ec)) {
			if (auto info = readManifest(entry.path())) return info;
		}
	}
	return std::nullopt;
}

fs::path FindInstallRoot(const fs::path& exeDir)
{
	std::error_code ec;
	const fs::path root = exeDir.parent_path();
	if (_wcsicmp(exeDir.filename().c_str(), L"Editor") == 0 && fs::is_regular_file(root / kManifestFile, ec)) return root;
	return {};
}

bool ZipIsEnginePackage(const fs::path& zip)
{
	std::string listing;
	if (RunHiddenProcess(L"\"" + TarExe().wstring() + L"\" -tf \"" + zip.wstring() + L"\"", {}, &listing) != 0) return false;
	std::istringstream lines(listing);
	std::string line;
	while (std::getline(lines, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		std::replace(line.begin(), line.end(), '\\', '/');
		if (line.starts_with("./")) line.erase(0, 2);
		if (line == kManifestFile) return true;
		const size_t slash = line.find('/');
		if (slash != std::string::npos && line.substr(slash + 1) == kManifestFile) return true;
	}
	return false;
}

bool ExtractZip(const fs::path& zip, const fs::path& dest, std::string* error)
{
	std::error_code ec;
	fs::create_directories(dest, ec);
	std::string output;
	const int code = RunHiddenProcess(L"\"" + TarExe().wstring() + L"\" -xf \"" + zip.wstring() + L"\" -C \"" + dest.wstring() + L"\"", {}, &output);
	if (code != 0) {
		if (error) *error = code < 0 ? "tar.exe could not be started (Windows 10 1803 or newer is required)." : "Extracting the zip failed: " + output;
		return false;
	}
	return true;
}

bool ApplyPackage(const fs::path& packageRoot, const fs::path& installRoot, std::string* error)
{
	std::error_code ec;
	std::vector<std::string> staged;
	auto fail = [&](const std::string& message) {
		for (const auto& name : staged) {
			std::error_code rec;
			fs::remove_all(installRoot / (name + ".new"), rec);
		}
		if (error) *error = message;
		return false;
	};

	// 1. Stage: copy the new files next to the old ones (nothing of the install changes yet)
	for (const auto& name : EngineOwnedEntries()) {
		const fs::path src = packageRoot / name;
		if (!fs::exists(src, ec)) continue;
		const fs::path staging = installRoot / (name + ".new");
		fs::remove_all(staging, ec);
		ec.clear();
		if (fs::is_directory(src, ec)) {
			fs::copy(src, staging, fs::copy_options::recursive, ec);
		} else {
			fs::copy_file(src, staging, ec);
		}
		staged.push_back(name);
		if (ec) return fail("Could not copy " + name + ": " + ec.message());
	}
	// The docking layout belongs to the user
	const fs::path layout = installRoot / "Editor" / "imgui.ini";
	if (fs::exists(layout, ec) && fs::exists(installRoot / "Editor.new", ec)) {
		fs::copy_file(layout, installRoot / "Editor.new" / "imgui.ini", fs::copy_options::overwrite_existing, ec);
	}

	// 2. Swap: old -> "<name>.old", new -> "<name>", then drop the old copy
	for (size_t i = 0; i < staged.size(); ++i) {
		const std::string& name = staged[i];
		const fs::path target = installRoot / name;
		const fs::path old = installRoot / (name + ".old");
		fs::remove_all(old, ec);
		ec.clear();
		const auto partly = [&](const std::string& what) {
			staged.erase(staged.begin(), staged.begin() + static_cast<std::ptrdiff_t>(i)); // already installed ones stay
			return fail(what + ": " + ec.message() + (i > 0 ? "\nThe entries before it are already updated; run the update again." : ""));
		};
		if (fs::exists(target, ec) && !RenameWithRetry(target, old, ec)) {
			// A reader without delete sharing blocks the rename of a single file but not writing it
			std::error_code copyEc;
			if (fs::is_regular_file(target, copyEc) && fs::copy_file(installRoot / (name + ".new"), target, fs::copy_options::overwrite_existing, copyEc)) {
				fs::remove(installRoot / (name + ".new"), copyEc);
				ec.clear();
				continue;
			}
			return partly("Could not replace " + name + " (is a file in it still open?)");
		}
		RenameWithRetry(installRoot / (name + ".new"), target, ec);
		if (ec) {
			std::error_code rec;
			fs::rename(old, target, rec); // put the old one back
			return partly("Could not install " + name);
		}
		fs::remove_all(old, ec);
		ec.clear();
	}
	return true;
}

bool LaunchApplyUpdate(const PackageInfo& package, const fs::path& installRoot, std::string* error)
{
	const fs::path exe = package.root / "Editor" / CurrentExePath().filename();
	std::error_code ec;
	if (!fs::exists(exe, ec)) {
		if (error) *error = "The package has no " + exe.filename().string() + " in Editor/.";
		return false;
	}
	std::wstring cmd = L"\"" + exe.wstring() + L"\" --apply-update --target \"" + installRoot.wstring() +
		L"\" --wait-pid " + std::to_wstring(GetCurrentProcessId());
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	const std::wstring workDir = exe.parent_path().wstring();
	if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, workDir.c_str(), &si, &pi)) {
		if (error) *error = "Could not start the updater (Win32 error " + std::to_string(GetLastError()) + ").";
		return false;
	}
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return true;
}

bool IsApplyUpdateLaunch()
{
	return wcsstr(GetCommandLineW(), L"--apply-update") != nullptr;
}

int RunApplyUpdateMode()
{
	const fs::path installRoot = ArgValue(L"--target");
	const DWORD waitPid = static_cast<DWORD>(_wtoi(ArgValue(L"--wait-pid").c_str()));
	if (installRoot.empty()) return 2;

	// Wait for the old editor to exit so its exe and DLLs are no longer locked
	if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, waitPid)) {
		WaitForSingleObject(process, 60000);
		CloseHandle(process);
	}

	const fs::path packageRoot = CurrentExePath().parent_path().parent_path();
	std::string error;
	bool ok = false;
	for (int attempt = 0; attempt < 10 && !ok; ++attempt) { // antivirus scans may hold files for a moment
		if (attempt > 0) std::this_thread::sleep_for(std::chrono::milliseconds(500));
		ok = ApplyPackage(packageRoot, installRoot, &error);
	}
	if (!ok) {
		const std::wstring message = L"The engine update could not be installed.\n\n" + fs::path(error).wstring() +
			L"\n\nClose every program that uses the engine folder and install the update again.";
		MessageBoxW(nullptr, message.c_str(), L"Rizeallday Engine Update", MB_OK | MB_ICONERROR);
		return 1;
	}

	const fs::path editorExe = installRoot / "Editor" / CurrentExePath().filename();
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	std::wstring cmd = L"\"" + editorExe.wstring() + L"\"";
	const std::wstring workDir = editorExe.parent_path().wstring();
	if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, workDir.c_str(), &si, &pi)) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
	return 0;
}

}
