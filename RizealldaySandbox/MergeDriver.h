#pragma once
#include <filesystem>
#include <string>
#include <vector>

/// git merge driver for scenes and prefabs: `<exe> --merge-driver %O %A %B %P` runs SceneMerge before any window
/// or GPU exists. Installed per project by EditorSuite::InstallMergeDriver.
namespace MergeDriver {
	inline constexpr const char* kDriverName = "rizescene";
	inline constexpr const char* kReportSuffix = ".merge.json";

	bool IsMergeDriverLaunch();
	/// Reads the four paths after --merge-driver and runs MergeFiles. Returns the process exit code.
	int RunMergeDriverMode();

	/// Merges the three files and writes the result over `ours`; when there are conflicts or notes, also writes the
	/// report to `reportFile`. 0 = valid merged JSON (conflicts included), non-zero = not merged and `ours` untouched.
	int MergeFiles(const std::filesystem::path& base, const std::filesystem::path& ours, const std::filesystem::path& theirs,
		const std::filesystem::path& reportFile, std::string* message);

	/// Value of `git config merge.rizescene.driver` for this exe (quoted, forward slashes for git's sh)
	std::string DriverCommand(const std::filesystem::path& exe);

	/// Appends every line of `lines` that `text` does not hold as a whole line; true when something was added
	bool AppendMissingLines(std::string& text, const std::vector<std::string>& lines);
}
