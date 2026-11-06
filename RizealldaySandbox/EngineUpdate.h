#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

/// Engine updates from a release zip (Tools/MakeRelease.ps1). No server: the team gets a zip,
/// the editor extracts it, then a helper process (the NEW editor exe started with --apply-update)
/// replaces the engine folders of the install once the old editor has exited.
namespace EngineUpdate {
	inline constexpr const char* kManifestFile = "engine.json";

	/// Top-level entries of an install that belong to the engine; everything else (user files) is left alone
	const std::vector<std::string>& EngineOwnedEntries();

	struct PackageInfo {
		std::filesystem::path root; // folder that contains engine.json, Editor/, SDK/ ...
		std::string version;
		int scriptApi = 0;
	};

	/// Reads engine.json in `dir` or in one of its sub-folders (zips usually hold one top folder)
	std::optional<PackageInfo> FindPackage(const std::filesystem::path& dir);

	/// <root>/Editor/<exe> with <root>/engine.json, or empty when running from a source workspace
	std::filesystem::path FindInstallRoot(const std::filesystem::path& exeDir);

	/// True when the zip has engine.json at its root or one folder deep (an engine package, not assets)
	bool ZipIsEnginePackage(const std::filesystem::path& zip);

	/// Extracts with the tar.exe that ships with Windows 10/11
	bool ExtractZip(const std::filesystem::path& zip, const std::filesystem::path& dest, std::string* error);

	/// Copies every engine-owned entry to "<name>.new" first, then swaps them in, so a failed copy leaves the install untouched
	bool ApplyPackage(const std::filesystem::path& packageRoot, const std::filesystem::path& installRoot, std::string* error);

	/// Starts <packageRoot>/Editor/<exe> --apply-update --target <installRoot> --wait-pid <this process>
	bool LaunchApplyUpdate(const PackageInfo& package, const std::filesystem::path& installRoot, std::string* error);

	/// Runs a hidden console program (tar, git) and returns its exit code; -1 = it could not be started
	int RunHiddenProcess(std::wstring cmdLine, const std::filesystem::path& workDir, std::string* output);

	/// Reads a child's output pipe until the child exits (then drains what is left). Does not wait for the pipe
	/// to close: helpers a build leaves running (mspdbsrv.exe, vctip.exe) inherit it and keep it open for hours.
	/// `pipe` / `process` are Win32 HANDLEs.
	void ReadOutputUntilExit(void* pipe, void* process, const std::function<void(const char* data, size_t size)>& sink);

	/// Value after `name` on the command line, "" when missing (handles quoted paths)
	std::wstring CommandLineValue(const std::wstring& name);

	/// Handles a --apply-update launch before any window exists. Returns the process exit code.
	bool IsApplyUpdateLaunch();
	int RunApplyUpdateMode();
}
