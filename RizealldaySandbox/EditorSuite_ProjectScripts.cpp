#include "EditorSuiteInternal.h"
#include "EngineUpdate.h"

// Per-project game scripts: every project carries GameScripts/ (vcxproj + Scripts/*.cpp) in its own git repo,
// builds Library/Scripts/GameScripts.dll on each PC, and finds the engine headers through GameScripts/RizeEngine.props.

namespace fs = std::filesystem;

namespace {
	constexpr const char* kUtf8Bom = "\xEF\xBB\xBF";
	// Same GUID as Templates/ScriptsProject/GameScripts.vcxproj
	constexpr const char* kScriptsProjectGuid = "{7D1F3A52-5B8E-4C2A-9F61-0E4B2C7A9D31}";

	std::string ReadTextFile(const fs::path& file)
	{
		std::ifstream in(file, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}

	bool WriteTextFile(const fs::path& file, const std::string& text)
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out << text;
		return out.good();
	}

	/// Inserts `insertion` before the first of `anchors` found, unless `already` is in the file
	void InsertBeforeAnchor(const fs::path& file, const std::string& already, std::initializer_list<const char*> anchors, const std::string& insertion)
	{
		std::string content = ReadTextFile(file);
		if (content.empty() || content.find(already) != std::string::npos) return;
		for (const char* anchor : anchors) {
			if (const size_t pos = content.find(anchor); pos != std::string::npos) {
				content.insert(pos, insertion);
				WriteTextFile(file, content);
				return;
			}
		}
	}

	std::string XmlEscape(const std::string& text)
	{
		std::string out;
		for (char c : text) {
			if (c == '&') out += "&amp;";
			else if (c == '<') out += "&lt;";
			else if (c == '>') out += "&gt;";
			else out += c;
		}
		return out;
	}

	/// GameScripts/RizeEngine.props: this PC's engine SDK folder (the file is in the project's .gitignore)
	std::string ScriptsPropsText(const fs::path& sdkDir)
	{
		std::string sdk = sdkDir.string();
		if (!sdk.empty() && sdk.back() != '\\') sdk += '\\';
		return std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n")
			+ "<!-- Written by the Rizeallday editor for this PC. Not committed (each PC has its own engine folder). -->\r\n"
			+ "<Project xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\r\n"
			+ "  <PropertyGroup>\r\n"
			+ "    <RizeEngineSDK>" + XmlEscape(sdk) + "</RizeEngineSDK>\r\n"
			+ "  </PropertyGroup>\r\n"
			+ "</Project>\r\n";
	}

	std::string SolutionText()
	{
		const std::string guid = kScriptsProjectGuid;
		return std::string(kUtf8Bom) + "\r\n"
			+ "Microsoft Visual Studio Solution File, Format Version 12.00\r\n"
			+ "# Visual Studio Version 17\r\n"
			+ "VisualStudioVersion = 17.0.31903.59\r\n"
			+ "MinimumVisualStudioVersion = 10.0.40219.1\r\n"
			+ "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"GameScripts\", \"GameScripts\\GameScripts.vcxproj\", \"" + guid + "\"\r\n"
			+ "EndProject\r\n"
			+ "Global\r\n"
			+ "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\r\n"
			+ "\t\tDevelopment|x64 = Development|x64\r\n"
			+ "\t\tRelease|x64 = Release|x64\r\n"
			+ "\tEndGlobalSection\r\n"
			+ "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\r\n"
			+ "\t\t" + guid + ".Development|x64.ActiveCfg = Development|x64\r\n"
			+ "\t\t" + guid + ".Development|x64.Build.0 = Development|x64\r\n"
			+ "\t\t" + guid + ".Release|x64.ActiveCfg = Release|x64\r\n"
			+ "\t\t" + guid + ".Release|x64.Build.0 = Release|x64\r\n"
			+ "\tEndGlobalSection\r\n"
			+ "EndGlobal\r\n";
	}

	/// The engine workspace's own scripts folder (source tree only)
	fs::path WorkspaceScriptSources()
	{
		return ScriptModule::ResolveWorkspaceRoot() / ScriptModule::kGameScriptsDirName / "Scripts";
	}
}

fs::path EditorSuite::ProjectScriptsDir() const
{
	if (currentProjectRoot_.empty()) return {};
	std::error_code ec;
	const fs::path dir = currentProjectRoot_ / ScriptModule::kGameScriptsDirName;
	return fs::is_regular_file(dir / "GameScripts.vcxproj", ec) ? dir : fs::path{};
}

fs::path EditorSuite::ResolveEngineSdkDir()
{
	std::error_code ec;
	const fs::path install = EngineUpdate::FindInstallRoot(GetExecutableDirectory());
	if (!install.empty() && fs::exists(install / "SDK" / "include" / "base" / "ScriptAPI.h", ec)) return install / "SDK" / "include";
	const fs::path source = ScriptModule::ResolveWorkspaceRoot() / "RizealldayEngine";
	if (fs::exists(source / "base" / "ScriptAPI.h", ec)) return source;
	return {};
}

fs::path EditorSuite::ResolveEngineTemplatesDir()
{
	std::error_code ec;
	const fs::path install = EngineUpdate::FindInstallRoot(GetExecutableDirectory());
	if (!install.empty() && fs::is_directory(install / "Templates", ec)) return install / "Templates";
	return ScriptModule::ResolveWorkspaceRoot() / "Templates";
}

fs::path EditorSuite::ResolveRuntimeDir() const
{
	if (!exportRuntimeDirOverride_.empty()) return exportRuntimeDirOverride_;
	const fs::path install = EngineUpdate::FindInstallRoot(GetExecutableDirectory());
	if (!install.empty()) return install / "Runtime";
	return ScriptModule::ResolveWorkspaceRoot() / "bin" / "Release";
}

bool EditorSuite::CreateProjectScripts(const fs::path& projectRoot, const std::string& projectName, std::string* error)
{
	std::error_code ec;
	const fs::path templ = ResolveEngineTemplatesDir() / "ScriptsProject";
	fs::path dllmain = templ / "Scripts" / "dllmain.cpp";
	if (!fs::exists(dllmain, ec)) dllmain = WorkspaceScriptSources() / "dllmain.cpp"; // source tree: the engine's own copy
	if (!fs::exists(templ / "GameScripts.vcxproj", ec) || !fs::exists(dllmain, ec)) {
		if (error) *error = "Scripts template not found in " + templ.string();
		return false;
	}
	const fs::path dir = projectRoot / ScriptModule::kGameScriptsDirName;
	fs::create_directories(dir / "Scripts", ec);
	fs::copy_file(templ / "GameScripts.vcxproj", dir / "GameScripts.vcxproj", fs::copy_options::overwrite_existing, ec);
	if (!ec) fs::copy_file(templ / "GameScripts.vcxproj.filters", dir / "GameScripts.vcxproj.filters", fs::copy_options::overwrite_existing, ec);
	if (!ec) fs::copy_file(dllmain, dir / "Scripts" / "dllmain.cpp", fs::copy_options::overwrite_existing, ec);
	if (ec) {
		if (error) *error = "Could not copy the scripts template: " + ec.message();
		return false;
	}
	WriteTextFile(dir / "RizeEngine.props", ScriptsPropsText(ResolveEngineSdkDir()));
	WriteTextFile(projectRoot / (projectName + ".sln"), SolutionText());
	return true;
}

bool EditorSuite::EnsureProjectScriptsProps()
{
	const fs::path dir = ProjectScriptsDir();
	if (dir.empty()) return false;
	const std::string wanted = ScriptsPropsText(ResolveEngineSdkDir());
	if (ReadTextFile(dir / "RizeEngine.props") == wanted) return false;
	WriteTextFile(dir / "RizeEngine.props", wanted);
	LogMessage("[Scripts] Wrote GameScripts/RizeEngine.props for this PC (engine SDK: " + ResolveEngineSdkDir().string() + ").", 0);
	return true;
}

void EditorSuite::SyncScriptModuleWithProject()
{
	const fs::path projectDir = ProjectScriptsDir();
	const fs::path wanted = projectDir.empty() ? fs::path{} : currentProjectRoot_ / "Library" / "Scripts" / "GameScripts.dll";
	std::error_code ec;
	bool loadFailed = false;
	if (scriptModule_.GetDllPathOverride() != wanted) {
		// Instances live in the old DLL; the scene loaded next recreates them from the new one
		DestroyAllScriptInstances();
		scriptModule_.SetDllPath(wanted);
		if (fs::exists(scriptModule_.ResolveDllPath(), ec)) {
			std::string loadError;
			if (!scriptModule_.LoadShadowCopy(&loadError)) {
				LogMessage("[Scripts] " + loadError, projectDir.empty() ? 2 : 1);
				loadFailed = true;
			}
		} else {
			scriptModule_.Unload();
		}
		scriptSourceWatcherInitialized_ = false;
		strncpy_s(scriptOutputDirBuf_, (ResolveGameScriptsDir() / "Scripts").generic_string().c_str(), sizeof(scriptOutputDirBuf_) - 1);
	}
	if (projectDir.empty()) return;

	EnsureProjectScriptsProps();
	// A fresh clone has no Library/; a DLL from an older engine version is refused. Both just need a build.
	if ((loadFailed || !fs::exists(wanted, ec)) && !isRunningTests_) { // self-tests never start MSBuild
		LogMessage("[Scripts] Building the project's scripts (GameScripts.dll)...", 0);
		TriggerScriptBuild();
	}
}

void EditorSuite::AddScriptFilesToVcxproj(const std::vector<std::string>& classNames)
{
	const fs::path vcxprojPath = ResolveGameScriptsVcxproj();
	const fs::path filtersPath = vcxprojPath.string() + ".filters";
	const fs::path sourceDir = ResolveGameScriptsDir() / "Scripts";
	// The engine's vcxproj points at ScriptAPI.h by relative path, a project's through $(RizeEngineSDK)
	const auto headerAnchors = { "    <ClInclude Include=\"..\\RizealldayEngine\\base\\ScriptAPI.h\"", "    <ClInclude Include=\"$(RizeEngineSDK)base\\ScriptAPI.h\"" };
	std::error_code ec;
	for (const std::string& name : classNames) {
		const std::string relCpp = "Scripts\\" + name + ".cpp";
		const std::string relH = "Scripts\\" + name + ".h";
		if (fs::exists(sourceDir / (name + ".cpp"), ec)) {
			InsertBeforeAnchor(vcxprojPath, "\"" + relCpp + "\"", { "    <ClCompile Include=\"Scripts\\dllmain.cpp\" />" },
				"    <ClCompile Include=\"" + relCpp + "\" />\n");
			InsertBeforeAnchor(filtersPath, "\"" + relCpp + "\"", { "    <ClCompile Include=\"Scripts\\dllmain.cpp\">" },
				"    <ClCompile Include=\"" + relCpp + "\">\n      <Filter>Scripts</Filter>\n    </ClCompile>\n");
		}
		if (fs::exists(sourceDir / (name + ".h"), ec)) {
			InsertBeforeAnchor(vcxprojPath, "\"" + relH + "\"", headerAnchors, "    <ClInclude Include=\"" + relH + "\" />\n");
			std::string filterEntry = "    <ClInclude Include=\"" + relH + "\">\n      <Filter>Scripts</Filter>\n    </ClInclude>\n";
			InsertBeforeAnchor(filtersPath, "\"" + relH + "\"", { "    <ClInclude Include=\"..\\RizealldayEngine\\base\\ScriptAPI.h\">", "    <ClInclude Include=\"$(RizeEngineSDK)base\\ScriptAPI.h\">" }, filterEntry);
		}
	}
}

namespace {
std::string ReadWholeFile(const fs::path& path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), {});
}
}

bool EditorSuite::InstallSampleScripts(const std::string& prefix)
{
	const fs::path projectDir = ProjectScriptsDir();
	if (projectDir.empty()) return true; // workspace mode: the samples are compiled into the engine's GameScripts.dll

	std::error_code ec;
	fs::path source = ResolveEngineTemplatesDir() / "Samples" / prefix;
	if (!fs::is_directory(source, ec)) source = WorkspaceScriptSources();
	std::vector<std::string> classNames;
	bool copied = false;
	for (const auto& entry : fs::directory_iterator(source, ec)) {
		const fs::path& file = entry.path();
		const std::string ext = file.extension().string();
		if (!entry.is_regular_file(ec) || !file.filename().string().starts_with(prefix) || (ext != ".cpp" && ext != ".h")) continue;
		const fs::path target = projectDir / "Scripts" / file.filename();
		if (!fs::exists(target, ec)) {
			fs::copy_file(file, target, ec);
			copied = copied || !ec;
		} else if (ReadWholeFile(file) != ReadWholeFile(target)) {
			// An older sample would not match the scene just created. The old file may hold the user's edits: keep it next to the new one.
			fs::path backup = target;
			backup += ".old";
			for (int n = 1; fs::exists(backup, ec); ++n) {
				backup = target;
				backup += ".old" + std::to_string(n);
			}
			fs::rename(target, backup, ec);
			if (!ec) {
				fs::copy_file(file, target, ec);
				copied = copied || !ec;
				LogMessage(std::format("[Samples] Updated {}; your previous version is {}", target.filename().string(), backup.filename().string()), 1);
			}
		}
		const std::string stem = file.stem().string();
		if (std::find(classNames.begin(), classNames.end(), stem) == classNames.end()) classNames.push_back(stem);
	}
	if (classNames.empty()) {
		LogMessage("[Samples] Sample scripts '" + prefix + "*' not found in " + source.string(), 2);
		return false;
	}
	AddScriptFilesToVcxproj(classNames);
	if (copied && !isRunningTests_) {
		LogMessage(std::format("[Samples] Copied the {} sample scripts into GameScripts/Scripts; building...", prefix), 0);
		TriggerScriptBuild();
	}
	return true;
}
