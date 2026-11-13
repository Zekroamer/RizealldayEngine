#include "MergeDriver.h"
#include "SceneMerge.h"
#include "EditorSuiteInternal.h" // EditorSuiteDetail::WriteFileAtomic
#include <windows.h>
#include <shellapi.h>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>

namespace fs = std::filesystem;

namespace {
	std::optional<std::string> ReadAll(const fs::path& file)
	{
		std::ifstream in(file, std::ios::binary);
		if (!in.is_open()) return std::nullopt;
		std::stringstream text;
		text << in.rdbuf();
		return text.str();
	}

	// Through a temporary file, so a failed write never leaves half a scene behind
	bool WriteAll(const fs::path& file, const std::string& text) { return EditorSuiteDetail::WriteFileAtomic(file, text); }

	// git shows the driver's stderr; a GUI-subsystem exe still gets the inherited handle
	void PrintToGit(const std::string& line)
	{
		HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
		if (err == nullptr || err == INVALID_HANDLE_VALUE) return;
		const std::string text = line + "\n";
		DWORD written = 0;
		WriteFile(err, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
	}
}

namespace MergeDriver {

bool IsMergeDriverLaunch()
{
	return wcsstr(GetCommandLineW(), L"--merge-driver") != nullptr;
}

int RunMergeDriverMode()
{
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	std::vector<fs::path> args;
	for (int i = 0; argv && i < argc; ++i) {
		if (!args.empty() || std::wstring(argv[i]) == L"--merge-driver") args.emplace_back(argv[i]);
	}
	if (argv) LocalFree(argv);
	if (args.size() < 4) {
		PrintToGit("Rizeallday merge driver: expected --merge-driver %O %A %B %P");
		return 2;
	}
	// %P is the file's path in the working tree (git runs the driver from the repository root); %A is a temporary file
	const fs::path report = (args.size() >= 5 ? args[4] : args[2]).string() + kReportSuffix;
	std::string message;
	const int code = MergeFiles(args[1], args[2], args[3], report, &message);
	PrintToGit("Rizeallday merge: " + message);
	return code;
}

int MergeFiles(const fs::path& base, const fs::path& ours, const fs::path& theirs, const fs::path& reportFile, std::string* message)
{
	auto fail = [&](const std::string& why) {
		if (message) *message = why + " Left to git's normal conflict handling.";
		return 1;
	};
	const std::optional<std::string> baseText = ReadAll(base); // git passes an empty file when there is no common ancestor
	const std::optional<std::string> oursText = ReadAll(ours);
	const std::optional<std::string> theirsText = ReadAll(theirs);
	if (!baseText || !oursText || !theirsText) return fail("Could not read the files to merge.");

	const SceneMerge::Result result = SceneMerge::Merge(*baseText, *oursText, *theirsText);
	if (!result.ok) return fail(result.error);
	if (!WriteAll(ours, result.mergedText)) return fail("Could not write the merged file.");

	if (result.conflicts.empty() && result.notes.empty()) {
		if (message) *message = "merged cleanly.";
		return 0;
	}
	const bool reportWritten = WriteReport(reportFile, result);
	if (message) {
		*message = std::format("merged; {} conflict(s) kept your value, {} note(s). {} Decide them in the editor: Window > Merge Conflicts.",
			result.conflicts.size(), result.notes.size(),
			reportWritten ? "Report: " + reportFile.string() + "." : "The report could not be written (" + reportFile.string() + ").");
	}
	return 0;
}

bool WriteReport(const fs::path& reportFile, const SceneMerge::Result& result)
{
	SceneMerge::Result all;
	if (const std::optional<std::string> old = ReadAll(reportFile)) SceneMerge::ReportFromJson(*old, all.conflicts, all.notes); // unreadable: replaced
	std::erase_if(all.conflicts, [&](const SceneMerge::Conflict& c) {
		return std::any_of(result.conflicts.begin(), result.conflicts.end(),
			[&](const SceneMerge::Conflict& n) { return n.objectId == c.objectId && n.field == c.field; });
	});
	all.conflicts.insert(all.conflicts.end(), result.conflicts.begin(), result.conflicts.end());
	all.notes.insert(all.notes.end(), result.notes.begin(), result.notes.end());
	return WriteAll(reportFile, SceneMerge::ReportToJson(all));
}

std::string DriverCommand(const fs::path& exe)
{
	return "\"" + exe.generic_string() + "\" --merge-driver %O %A %B %P";
}

bool AppendMissingLines(std::string& text, const std::vector<std::string>& lines)
{
	const std::string newline = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
	bool added = false;
	for (const std::string& line : lines) {
		bool present = false;
		std::istringstream in(text);
		for (std::string existing; std::getline(in, existing);) {
			if (!existing.empty() && existing.back() == '\r') existing.pop_back();
			if (existing == line) {
				present = true;
				break;
			}
		}
		if (present) continue;
		if (!text.empty() && text.back() != '\n') text += newline;
		text += line + newline;
		added = true;
	}
	return added;
}

} // namespace MergeDriver
