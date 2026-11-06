#include "EditorSuiteInternal.h"
#include "SceneJson.h"
#include "EngineUpdate.h"
#include "base/EngineVersion.h"

namespace {
	/// Writes ProjectSettings/project.json (names are escaped by the JSON writer).
	void WriteProjectSettingsFile(const std::filesystem::path& file, const std::string& projectName, const std::string& startupScene,
		const std::string& gameTitle = {})
	{
		SceneJson::Json j = SceneJson::Json::object();
		j["projectName"] = projectName;
		j["engineVersion"] = kEngineVersion;
		j["startupScene"] = startupScene;
		j["gameTitle"] = gameTitle.empty() ? projectName : gameTitle;
		std::ofstream pj(file);
		pj << SceneJson::DumpCompact(j);
	}

	void WriteFile(const std::filesystem::path& file, const std::string& text)
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out << text;
	}

	/// Files that make a new project ready to push to its own git repository
	void WriteProjectRepoFiles(const std::filesystem::path& root, const std::string& name)
	{
		WriteFile(root / ".gitignore",
			"# Rizeallday project. Generated and per-PC files stay out of git.\n"
			"Library/\n"
			"Build/\n"
			"SaveData/\n"
			"logs/\n"
			".vs/\n"
			"*.user\n"
			"imgui.ini\n"
			"GameScripts/RizeEngine.props\n"
			"GameScripts/x64/\n");
		WriteFile(root / ".gitattributes",
			"* text=auto\n"
			"*.png binary\n*.jpg binary\n*.jpeg binary\n*.dds binary\n*.tga binary\n*.bmp binary\n"
			"*.fbx binary\n*.glb binary\n*.bin binary\n"
			"*.wav binary\n*.mp3 binary\n*.ogg binary\n*.m4a binary\n");
		WriteFile(root / "README.md",
			"# " + name + "\n\n"
			"Game project for Rizeallday Engine " + kEngineVersion + ".\n\n"
			"## Open\n"
			"1. Start the engine editor (`Editor/RizealldaySandbox.exe` of the engine zip).\n"
			"2. File > Open Project... and pick this folder. Scripts are built automatically (Visual Studio with C++ is required).\n\n"
			"## Team rules\n"
			"- Close the editor (or stop Play) before `git pull`.\n"
			"- One person edits a scene at a time; shared parts go into prefabs.\n"
			"- Commit the `.meta` files next to every asset.\n"
			"- Scripts are in `GameScripts/Scripts`; open `" + name + ".sln` in Visual Studio.\n");
	}

	/// Adds every quoted "Resources/..." path in a text file (scene, prefab or script source) to `out`
	void CollectResourceReferences(const std::filesystem::path& file, std::set<std::string>& out)
	{
		std::ifstream in(file, std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		const std::string key = "\"Resources";
		for (size_t pos = text.find(key); pos != std::string::npos; pos = text.find(key, pos + 1)) {
			const size_t end = text.find_first_of("\"\r\n", pos + 1);
			if (end == std::string::npos || text[end] != '"') continue;
			std::string path = text.substr(pos + 1, end - pos - 1);
			for (size_t slash = path.find("\\\\"); slash != std::string::npos; slash = path.find("\\\\", slash)) path.replace(slash, 2, "/");
			if (path.starts_with("Resources/") && path.size() > 10) out.insert(path);
		}
	}

	/// `git init` + first commit when git is installed. Returns a log line.
	std::string InitGitRepository(const std::filesystem::path& root)
	{
		std::string output;
		if (EngineUpdate::RunHiddenProcess(L"git init", root, &output) != 0) {
			return "[Project] Git is not installed (or failed); skipped \"Initialize Git repository\".";
		}
		EngineUpdate::RunHiddenProcess(L"git add -A", root, &output);
		if (EngineUpdate::RunHiddenProcess(L"git commit -q -m \"Initial project\"", root, &output) != 0) {
			return "[Project] Git repository created; the first commit failed (set git user.name / user.email, then commit).";
		}
		return "[Project] Git repository created with a first commit. Add a remote and push: git remote add origin <url> && git push -u origin HEAD";
	}
}

std::string EditorSuite::OpenNativeFolderDialog()
{
	std::string folderPath = "";
	HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	bool shouldUninit = SUCCEEDED(hrInit);
	{
		Microsoft::WRL::ComPtr<IFileOpenDialog> pFileOpen = nullptr;
		HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_PPV_ARGS(&pFileOpen));
		if (SUCCEEDED(hr) && pFileOpen) {
			DWORD dwOptions;
			if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
				pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
			}
			if (SUCCEEDED(pFileOpen->Show(NULL))) {
				Microsoft::WRL::ComPtr<IShellItem> pItem = nullptr;
				if (SUCCEEDED(pFileOpen->GetResult(&pItem)) && pItem) {
					PWSTR pszFilePath = nullptr;
					if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)) && pszFilePath) {
						int len = WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, NULL, 0, NULL, NULL);
						if (len > 1) {
							folderPath.resize(len - 1);
							WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, folderPath.data(), len, NULL, NULL);
						}
						CoTaskMemFree(pszFilePath);
					}
				}
			}
		}
	}
	if (shouldUninit) {
		CoUninitialize();
	}
	return folderPath;
}

bool EditorSuite::CreateNewProject(const std::string& parentFolder, const std::string& projectName)
{
	projectModalError_.clear();

	// 1. Validate input
	std::string name = projectName;
	name.erase(0, name.find_first_not_of(" \t"));
	name.erase(name.find_last_not_of(" \t") + 1);
	if (name.empty()) {
		projectModalError_ = "Project name is empty.";
		return false;
	}
	if (name.find_first_of("\\/:*?\"<>|") != std::string::npos || name == "." || name == "..") {
		projectModalError_ = "Project name contains invalid characters ( \\ / : * ? \" < > | ).";
		return false;
	}
	if (parentFolder.empty()) {
		projectModalError_ = "Parent directory is empty.";
		return false;
	}

	try {
		const std::filesystem::path projPath = std::filesystem::absolute(std::filesystem::path(parentFolder) / name);

		// Never overwrite an existing project or any non-empty folder
		if (std::filesystem::exists(projPath) &&
			(!std::filesystem::is_directory(projPath) || !std::filesystem::is_empty(projPath))) {
			projectModalError_ = std::format("Folder already exists and is not empty: {}", projPath.string());
			return false;
		}

		// 2. Leave play mode and clear the current scene before switching projects
		if (sceneState_ != EditorSceneState::Edit) {
			StopPlayMode();
		}

		// 3. Folder layout (.gitkeep so git keeps the empty folders)
		for (const char* folder : { "Scenes", "Models", "Textures", "Audio", "Prefabs", "Animators" }) {
			std::filesystem::create_directories(projPath / "Assets" / folder);
			if (std::string(folder) != "Scenes") WriteFile(projPath / "Assets" / folder / ".gitkeep", "");
		}
		std::filesystem::create_directories(projPath / "ProjectSettings");

		// 4. ProjectSettings/project.json (scene path is relative to the project root)
		const std::string startupSceneRel = "Assets/Scenes/SampleScene.scene.json";
		WriteProjectSettingsFile(projPath / "ProjectSettings" / "project.json", name, startupSceneRel);

		// 5. Its own scripts project (GameScripts/ + <name>.sln) and the git files
		std::string scriptsError;
		if (!CreateProjectScripts(projPath, name, &scriptsError)) {
			LogMessage("[Project] " + scriptsError + " (the project uses the engine workspace scripts).", 1);
		}
		WriteProjectRepoFiles(projPath, name);

		// 6. Switch the editor to the new project *before* creating the scene so every path resolves inside it
		currentProjectRoot_ = projPath;
		currentDirectory_ = projPath / "Assets";
		SyncScriptModuleWithProject();

		// 7. Fresh default scene (Main Camera, Directional Light, Ground Plane) saved into the project
		NewScene();
		currentScenePath_ = (projPath / startupSceneRel).string();
		currentSceneName_ = "SampleScene";
		if (!SaveScene(currentScenePath_)) {
			projectModalError_ = "Project folders were created, but the default scene could not be saved.";
			return false;
		}
		ResetEditorHistory();

		strncpy_s(openProjectFolderBuf_, projPath.string().c_str(), sizeof(openProjectFolderBuf_) - 1);
		strncpy_s(saveSceneAsPathBuf_, currentScenePath_.c_str(), sizeof(saveSceneAsPathBuf_) - 1);
		strncpy_s(openScenePathBuf_, currentScenePath_.c_str(), sizeof(openScenePathBuf_) - 1);
		strncpy_s(buildStartupSceneBuf_, currentScenePath_.c_str(), sizeof(buildStartupSceneBuf_) - 1);
		strncpy_s(buildGameTitleBuf_, name.c_str(), sizeof(buildGameTitleBuf_) - 1);
		strncpy_s(buildOutputDirBuf_, (projPath / "Build").string().c_str(), sizeof(buildOutputDirBuf_) - 1);
		sSessionProjectRoot_ = projPath;
		AddRecentProject(projPath);
		// .meta files first, so the first commit is complete
		AssetDb().Refresh();
		if (newProjectInitGit_ && !isRunningTests_) LogMessage(InitGitRepository(projPath), 0);

		LogMessage(std::format("[Project] Created new project '{}' at: {}", name, projPath.string()), 0);
		return true;
	} catch (const std::exception& e) {
		projectModalError_ = std::format("Failed to create project: {}", e.what());
		LogMessage(std::format("[Project] {}", projectModalError_), 2);
		return false;
	}
}

std::string EditorSuite::ToProjectRelativePath(const std::filesystem::path& path) const
{
	std::error_code ec;
	const std::filesystem::path absPath = std::filesystem::weakly_canonical(std::filesystem::absolute(path, ec), ec);

	auto relativeTo = [&](const std::filesystem::path& base, std::string& out) {
		if (base.empty()) return false;
		std::error_code bec;
		const std::filesystem::path absBase = std::filesystem::weakly_canonical(std::filesystem::absolute(base, bec), bec);
		const std::filesystem::path rel = absPath.lexically_relative(absBase);
		if (rel.empty() || rel.native().starts_with(L"..")) return false;
		out = rel.generic_string();
		return true;
	};

	std::string result;
	if (relativeTo(currentProjectRoot_, result)) return result;            // e.g. Assets/Textures/foo.png
	if (relativeTo(std::filesystem::current_path(ec), result)) return result; // e.g. Resources/circle.png
	return path.generic_string();
}

std::string EditorSuite::ResolveAssetPath(const std::string& path) const
{
	if (path.empty() || currentProjectRoot_.empty()) return path;
	const std::filesystem::path p(path);
	if (p.is_absolute()) return path;

	// Called every frame for custom models, so cache the filesystem lookup (cleared on project switch)
	if (resolvedAssetRoot_ != currentProjectRoot_) {
		resolvedAssetCache_.clear();
		resolvedAssetRoot_ = currentProjectRoot_;
	}
	if (auto it = resolvedAssetCache_.find(path); it != resolvedAssetCache_.end()) {
		return it->second;
	}

	std::error_code ec;
	const std::filesystem::path inProject = currentProjectRoot_ / p;
	// Engine assets such as Resources/... are not in the project and stay relative to the working directory
	std::string resolved = std::filesystem::exists(inProject, ec) ? inProject.string() : path;
	resolvedAssetCache_.emplace(path, resolved);
	return resolved;
}

std::filesystem::path EditorSuite::GetScenesDirectory() const
{
	if (!currentProjectRoot_.empty()) return currentProjectRoot_ / "Assets" / "Scenes";
	return std::filesystem::path("Assets") / "Scenes";
}

std::string EditorSuite::NormalizeScenePath(const std::string& input) const
{
	std::string trimmed = input;
	trimmed.erase(0, trimmed.find_first_not_of(" \t"));
	trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
	if (trimmed.empty()) return trimmed;

	std::filesystem::path p(trimmed);
	// A bare name ("Level1") goes into the scenes folder of the current project
	if (!p.has_parent_path()) {
		p = GetScenesDirectory() / p;
	}
	// Always use the .scene.json extension so the file shows up in Open Scene
	const std::string fileName = p.filename().string();
	if (!fileName.ends_with(".scene.json")) {
		std::string stem = fileName;
		if (stem.ends_with(".json")) stem.resize(stem.size() - 5);
		p.replace_filename(stem + ".scene.json");
	}
	return p.string();
}

void EditorSuite::ResetEditorHistory()
{
	ResetUndoHistory();
	isSceneDirty_ = false;
	selectedObjectIndex_ = -1;
}

bool EditorSuite::IsPathInsideProject(const std::filesystem::path& path) const
{
	if (currentProjectRoot_.empty()) return false;
	std::error_code ec;
	const std::filesystem::path absRoot = std::filesystem::weakly_canonical(currentProjectRoot_, ec);
	const std::filesystem::path absPath = std::filesystem::weakly_canonical(std::filesystem::absolute(path, ec), ec);
	const std::filesystem::path rel = absPath.lexically_relative(absRoot);
	return !rel.empty() && !rel.native().starts_with(L"..");
}

namespace {
	/// %LOCALAPPDATA%/RizealldayEngine/recent_projects.txt (one project folder per line, newest first)
	std::filesystem::path RecentProjectsFilePath()
	{
		char* localAppData = nullptr;
		size_t len = 0;
		std::filesystem::path base;
		if (_dupenv_s(&localAppData, &len, "LOCALAPPDATA") == 0 && localAppData) {
			base = localAppData;
		}
		free(localAppData);
		if (base.empty()) return {};
		return base / "RizealldayEngine" / "recent_projects.txt";
	}

	constexpr size_t kMaxRecentProjects = 10;

	void SaveRecentProjectsFile(const std::vector<std::filesystem::path>& projects)
	{
		const std::filesystem::path file = RecentProjectsFilePath();
		if (file.empty()) return;
		std::error_code ec;
		std::filesystem::create_directories(file.parent_path(), ec);
		std::ofstream out(file, std::ios::trunc);
		for (const auto& p : projects) {
			out << p.string() << "\n";
		}
	}
}

void EditorSuite::LoadRecentProjects()
{
	recentProjects_.clear();
	selectedRecentProject_ = -1;
	const std::filesystem::path file = RecentProjectsFilePath();
	if (file.empty()) return;

	std::ifstream in(file);
	std::string line;
	std::error_code ec;
	while (std::getline(in, line) && recentProjects_.size() < kMaxRecentProjects) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;
		const std::filesystem::path projPath(line);
		// Skip projects that were moved or deleted
		if (std::filesystem::exists(projPath / "ProjectSettings" / "project.json", ec)) {
			recentProjects_.push_back(projPath);
		}
	}
	// Nothing is preselected: "Open Selected" only opens what the user clicked
}

void EditorSuite::AddRecentProject(const std::filesystem::path& projectRoot)
{
	// Self-tests create throw-away projects in the temp folder; they must not appear in the user's list
	if (isRunningTests_) return;
	LoadRecentProjects();
	std::error_code ec;
	const std::filesystem::path root = std::filesystem::weakly_canonical(projectRoot, ec);
	// Throw-away folders (tests, demos in %TEMP%) are not projects the user wants to reopen
	const std::filesystem::path temp = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(ec), ec);
	if (!temp.empty()) {
		const std::filesystem::path rel = root.lexically_relative(temp);
		if (!rel.empty() && !rel.native().starts_with(L"..")) return;
	}
	std::erase_if(recentProjects_, [&](const std::filesystem::path& p) {
		std::error_code eqEc;
		return std::filesystem::equivalent(p, root, eqEc);
	});
	recentProjects_.insert(recentProjects_.begin(), root);
	if (recentProjects_.size() > kMaxRecentProjects) recentProjects_.resize(kMaxRecentProjects);

	SaveRecentProjectsFile(recentProjects_);
}

bool EditorSuite::DrawRecentProjectsList(float height)
{
	bool opened = false;
#ifdef USE_IMGUI
	ImGui::BeginChild("##RecentProjects", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
	if (recentProjects_.empty()) {
		ImGui::TextDisabled("%s", Tr("No recent projects."));
		ImGui::TextDisabled("%s", Tr("Use \"Browse...\" to open a project folder."));
	}
	int removeIndex = -1;
	for (int i = 0; i < static_cast<int>(recentProjects_.size()); ++i) {
		const auto& proj = recentProjects_[i];
		std::error_code ec;
		const bool exists = std::filesystem::exists(proj, ec);
		ImGui::PushID(i);
		const std::string name = proj.filename().string() + (exists ? "" : std::string("  ") + Tr("(missing)"));
		if (!exists) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		if (ImGui::Selectable(name.c_str(), selectedRecentProject_ == i, ImGuiSelectableFlags_AllowDoubleClick)) {
			selectedRecentProject_ = i;
			if (ImGui::IsMouseDoubleClicked(0) && exists) opened = OpenProject(proj.string());
		}
		if (!exists) ImGui::PopStyleColor();
		if (ImGui::BeginPopupContextItem()) {
			if (ImGui::MenuItem(TrId("Remove from List"))) removeIndex = i;
			ImGui::EndPopup();
		}
		ImGui::Indent(12.0f);
		ImGui::TextDisabled("%s", proj.string().c_str());
		ImGui::Unindent(12.0f);
		ImGui::PopID();
		if (opened) break; // OpenProject reorders the list
	}
	ImGui::EndChild();
	if (removeIndex >= 0 && removeIndex < static_cast<int>(recentProjects_.size())) {
		recentProjects_.erase(recentProjects_.begin() + removeIndex);
		selectedRecentProject_ = recentProjects_.empty() ? -1 : 0;
		if (!isRunningTests_) SaveRecentProjectsFile(recentProjects_);
	}
#else
	(void)height;
#endif
	return opened;
}

void EditorSuite::DrawProjectHub()
{
#ifdef USE_IMGUI
	const char* popupId = "Rizeallday Engine##ProjectHub";
	ImGui::OpenPopup(popupId);

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(820.0f, 0.0f), ImGuiCond_Always);

	// No close button: the editor needs a project before it can be used
	if (!ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}

	ImGui::TextUnformatted("Welcome to Rizeallday Engine");
	ImGui::TextDisabled(Tr("Create a new game project or open an existing one to start."));
	ImGui::Spacing();

	bool opened = false;
	if (ImGui::BeginTable("##HubColumns", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
		ImGui::TableNextRow();

		// Left: open an existing project
		ImGui::TableSetColumnIndex(0);
		ImGui::SeparatorText(Tr("Open Project"));
		opened = DrawRecentProjectsList(210.0f);

		const bool hasSelection = selectedRecentProject_ >= 0 && selectedRecentProject_ < static_cast<int>(recentProjects_.size());
		ImGui::BeginDisabled(!hasSelection);
		if (ImGui::Button(TrId("Open Selected")) && hasSelection) {
			opened = OpenProject(recentProjects_[selectedRecentProject_].string());
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button(TrId("Browse...##OpenProject"))) {
			const std::string chosen = OpenNativeFolderDialog();
			if (!chosen.empty()) {
				opened = OpenProject(chosen);
			}
		}
		ImGui::SetItemTooltip(Tr("Pick a project folder (contains ProjectSettings/project.json or Assets/)"));

		// Right: create a new project
		ImGui::TableSetColumnIndex(1);
		ImGui::SeparatorText(Tr("New Project"));
		ImGui::TextUnformatted("Project Name");
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputText("##HubProjectName", projectNameBuf_, sizeof(projectNameBuf_));
		ImGui::TextUnformatted("Location");
		ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Browse...").x - ImGui::GetStyle().FramePadding.x * 2.0f - ImGui::GetStyle().ItemSpacing.x);
		ImGui::InputText("##HubProjectFolder", projectFolderBuf_, sizeof(projectFolderBuf_));
		ImGui::SameLine();
		if (ImGui::Button(TrId("Browse...##NewProject"))) {
			const std::string chosen = OpenNativeFolderDialog();
			if (!chosen.empty()) strncpy_s(projectFolderBuf_, chosen.c_str(), sizeof(projectFolderBuf_) - 1);
		}
		const std::filesystem::path previewPath = std::filesystem::path(projectFolderBuf_) / projectNameBuf_;
		ImGui::TextWrapped(Tr("Will be created at: %s"), previewPath.string().c_str());
		ImGui::TextDisabled(Tr("Includes Assets/ folders, C++ scripts, project settings and a default scene."));
		ImGui::Checkbox(TrId("Initialize Git repository"), &newProjectInitGit_);
		ImGui::SetItemTooltip("%s", Tr("git init + first commit (needs Git). Then add a remote and push."));
		ImGui::Spacing();
		if (ImGui::Button(TrId("Create Project"), ImVec2(-1.0f, 0.0f))) {
			opened = CreateNewProject(projectFolderBuf_, projectNameBuf_);
		}

		ImGui::EndTable();
	}

	if (!projectModalError_.empty()) {
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", projectModalError_.c_str());
	}

	if (opened) {
		projectModalError_.clear();
		showProjectHub_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
#endif
}


bool EditorSuite::OpenProject(const std::string& folderPath)
{
	if (folderPath.empty()) return false;

	std::filesystem::path projPath(folderPath);
	if (!std::filesystem::exists(projPath / "ProjectSettings" / "project.json") &&
		projPath.has_parent_path() &&
		std::filesystem::exists(projPath.parent_path() / "ProjectSettings" / "project.json")) {
		projPath = projPath.parent_path();
	}

	projectModalError_.clear();
	std::filesystem::path projJsonPath = projPath / "ProjectSettings" / "project.json";
	if (!std::filesystem::exists(projJsonPath)) {
		// A folder with an Assets/ directory (e.g. an older workspace) is adopted as a project
		std::error_code ec;
		if (std::filesystem::is_directory(projPath / "Assets", ec)) {
			std::filesystem::create_directories(projPath / "ProjectSettings", ec);
			std::filesystem::create_directories(projPath / "Assets" / "Scenes", ec);
			WriteProjectSettingsFile(projJsonPath, projPath.filename().string(), "Assets/Scenes/SampleScene.scene.json");
			LogMessage(std::format("[Project] Converted folder into a project (created ProjectSettings/project.json): {}", projPath.string()), 0);
		} else {
			projectModalError_ = std::format("Not a project folder (no ProjectSettings/project.json or Assets/): {}", folderPath);
			LogMessage(std::format("[Project] {}", projectModalError_), 1);
			return false;
		}
	}

	std::string projectName = projPath.filename().string();
	std::string startupScene = "Assets/Scenes/SampleScene.scene.json";
	std::string gameTitle;

	std::ifstream pj(projJsonPath);
	if (pj.is_open()) {
		std::stringstream buf;
		buf << pj.rdbuf();
		pj.close();
		const std::string content = buf.str();

		std::string parseError;
		if (auto settings = SceneJson::Parse(content, &parseError)) {
			SceneJson::Read(*settings, "projectName", projectName);
			SceneJson::Read(*settings, "startupScene", startupScene);
			SceneJson::Read(*settings, "gameTitle", gameTitle);
			std::string projectEngine;
			SceneJson::Read(*settings, "engineVersion", projectEngine);
			const int cmp = CompareEngineVersions(projectEngine, kEngineVersion);
			if (cmp > 0) {
				LogMessage(std::format("[Project] This project was saved with engine {} but this editor is {}. Install the newer engine (Help > Install Engine Update...) before editing.",
					projectEngine, kEngineVersion), 1);
				projectEngineWarning_ = std::format("Saved with engine {} (this editor: {}). Update the engine.", projectEngine, kEngineVersion);
			} else {
				projectEngineWarning_.clear();
				if (cmp < 0 && !isRunningTests_) {
					// Teammates on the older engine get the warning above once this is pushed
					(*settings)["engineVersion"] = kEngineVersion;
					std::ofstream out(projJsonPath, std::ios::trunc);
					out << SceneJson::DumpCompact(*settings);
				}
			}
		} else {
			LogMessage(std::format("[Project] Warning: could not parse project.json ({}). Using defaults.", parseError), 1);
		}
	}

	if (sceneState_ != EditorSceneState::Edit) {
		StopPlayMode();
	}

	currentProjectRoot_ = std::filesystem::absolute(projPath);
	currentDirectory_ = currentProjectRoot_ / "Assets";
	if (!std::filesystem::exists(currentDirectory_)) currentDirectory_ = currentProjectRoot_;
	strncpy_s(openProjectFolderBuf_, currentProjectRoot_.string().c_str(), sizeof(openProjectFolderBuf_) - 1);
	strncpy_s(buildGameTitleBuf_, (gameTitle.empty() ? projectName : gameTitle).c_str(), sizeof(buildGameTitleBuf_) - 1);
	strncpy_s(buildOutputDirBuf_, (currentProjectRoot_ / "Build").string().c_str(), sizeof(buildOutputDirBuf_) - 1); // not next to the editor: updates replace that folder
	SyncScriptModuleWithProject(); // before the scene so its scripts come from the project's DLL

	std::filesystem::path sceneFullPath = currentProjectRoot_ / startupScene;
	if (!std::filesystem::exists(sceneFullPath)) {
		sceneFullPath = currentProjectRoot_ / "Assets" / "Scenes" / "SampleScene.scene.json";
	}
	if (!std::filesystem::exists(sceneFullPath)) {
		// No startup scene: open the first scene the project has (Assets/Scenes first) instead of an empty one
		std::vector<std::filesystem::path> scenes;
		std::error_code ec;
		for (const std::filesystem::path& dir : { currentProjectRoot_ / "Assets" / "Scenes", currentProjectRoot_ / "Assets" }) {
			for (auto it = std::filesystem::recursive_directory_iterator(dir, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
				if (it->is_regular_file() && it->path().filename().string().ends_with(".scene.json")) scenes.push_back(it->path());
			}
			if (!scenes.empty()) break;
			ec.clear();
		}
		std::sort(scenes.begin(), scenes.end());
		if (!scenes.empty()) sceneFullPath = scenes.front();
	}
	if (std::filesystem::exists(sceneFullPath)) {
		LoadScene(sceneFullPath.string());
		strncpy_s(buildStartupSceneBuf_, sceneFullPath.string().c_str(), sizeof(buildStartupSceneBuf_) - 1);
	} else {
		// Project without a scene yet: start from a fresh default scene inside the project
		NewScene();
	}
	const std::string sceneDefault = (GetScenesDirectory() / (currentSceneName_ + ".scene.json")).string();
	strncpy_s(saveSceneAsPathBuf_, sceneDefault.c_str(), sizeof(saveSceneAsPathBuf_) - 1);
	strncpy_s(openScenePathBuf_, currentScenePath_.c_str(), sizeof(openScenePathBuf_) - 1);
	ResetEditorHistory();
	sSessionProjectRoot_ = currentProjectRoot_;
	AddRecentProject(currentProjectRoot_);

	LogMessage(std::format("[Project] Opened project '{}' at: {}", projectName, projPath.string()), 0);
	return true;
}

void EditorSuite::ToggleStandalonePreview()
{
	if (!isStandalonePreview_) {
		isStandalonePreview_ = true;
		if (sceneState_ == EditorSceneState::Edit) {
			StartPlayMode();
		} else {
			sceneState_ = EditorSceneState::Play;
		}
		LogMessage("[Standalone] Entered Standalone Game Preview (F11 / Esc to exit).", 0);
	} else {
		isStandalonePreview_ = false;
		if (sceneState_ != EditorSceneState::Edit) {
			StopPlayMode();
		}
		LogMessage("[Standalone] Exited Standalone Game Preview.", 0);
	}
}

bool EditorSuite::BuildScriptsForExport(std::filesystem::path& outDll)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	outDll.clear();
	if (isRunningTests_) { // self-tests never start MSBuild
		outDll = ScriptModule::ResolveWorkspaceRoot() / ScriptModule::kScriptsDllSubpath;
		return true;
	}
	const fs::path vcxproj = ResolveGameScriptsVcxproj();
	if (!fs::exists(vcxproj, ec)) {
		LogMessage("[Build] This project has no scripts project; exporting without scripts.", 1);
		return true;
	}
	const std::string msbuild = ResolveMsBuildExe();
	if (!fs::exists(msbuild, ec)) {
		LogMessage(std::format("[Build] MSBuild.exe not found ('{}'). Install Visual Studio with C++.", msbuild), 2);
		return false;
	}
	EnsureProjectScriptsProps();
	// Own output folders: the editor keeps its hot-reloaded Development DLL
	const fs::path output = vcxproj.parent_path().parent_path() / (ProjectScriptsDir().empty() ? "bin" : "Library") / "ScriptsExport";
	fs::create_directories(output, ec);
	const std::wstring outText = output.wstring();
	// Rebuild: object files left from an older engine SDK would give a DLL the game refuses (API fingerprint)
	const std::wstring cmd = L"\"" + fs::path(msbuild).wstring() + L"\" \"" + vcxproj.wstring() + L"\" /t:Rebuild /p:Configuration=Release /p:Platform=x64"
		L" \"/p:OutDir=" + outText + L"\\\\\" \"/p:IntDir=" + outText + L"\\obj\\\\\" /m /v:minimal /nologo";
	LogMessage("[Build] Compiling the scripts (Release)...", 0);
	std::string log;
	const int code = EngineUpdate::RunHiddenProcess(cmd, vcxproj.parent_path(), &log);
	if (code != 0 || !fs::exists(output / "GameScripts.dll", ec)) {
		std::istringstream lines(log);
		for (std::string line; std::getline(lines, line);) {
			if (line.find(": error") != std::string::npos) LogMessage("[MSBuild] " + line, 2);
		}
		LogMessage(std::format("[Build] Export stopped: the scripts did not compile (exit code {}).", code), 2);
		bottomPanelTab_ = 1;
		return false;
	}
	outDll = output / "GameScripts.dll";
	return true;
}

bool EditorSuite::ExportStandaloneGame()
{
	try {
		namespace fs = std::filesystem;
		const fs::path outDir = fs::absolute(buildOutputDirBuf_);

		// Assets are copied below, so the game cannot live inside them
		if (!currentProjectRoot_.empty()) {
			std::error_code relEc;
			const fs::path rel = fs::weakly_canonical(outDir, relEc).lexically_relative(fs::weakly_canonical(currentProjectRoot_ / "Assets", relEc));
			if (!rel.empty() && !rel.native().starts_with(L"..")) {
				LogMessage("[Build] Choose an output folder outside Assets/.", 2);
				return false;
			}
		}

		// The game runs on the Release runtime (no editor, no ImGui)
		const fs::path runtimeDir = ResolveRuntimeDir();
		const fs::path runtimeExe = runtimeDir / "RizealldaySandbox.exe";
		if (!fs::exists(runtimeExe)) {
			LogMessage(std::format("[Build] Export needs the Release runtime, which is missing: {}. Build the Release configuration, or reinstall the engine zip.", runtimeExe.string()), 2);
			return false;
		}

		// Fresh, optimized scripts first: nothing is written if they do not compile
		fs::path scriptsDll;
		if (!BuildScriptsForExport(scriptsDll)) return false;

		fs::create_directories(outDir);
		std::string gameTitle = (buildGameTitleBuf_[0] != '\0') ? buildGameTitleBuf_ : "RizealldayGame";
		std::string targetExeName = gameTitle + ".exe";
		fs::copy_file(runtimeExe, outDir / targetExeName, fs::copy_options::overwrite_existing);
		for (const auto& entry : fs::directory_iterator(runtimeDir)) {
			if (entry.is_regular_file() && entry.path().extension() == ".dll") {
				fs::copy_file(entry.path(), outDir / entry.path().filename(), fs::copy_options::overwrite_existing);
			}
		}
		if (!scriptsDll.empty()) {
			const fs::path dstScriptsDll = outDir / ScriptModule::kScriptsDllSubpath;
			fs::create_directories(dstScriptsDll.parent_path());
			fs::copy_file(scriptsDll, dstScriptsDll, fs::copy_options::overwrite_existing);
		}

		// Shaders are compiled at startup
		if (fs::exists("shaders")) {
			fs::copy("shaders", outDir / "shaders", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
		}

		// The project's Assets/ and input map. Scenes store paths relative to the project root, which becomes the game's working directory.
		if (!currentProjectRoot_.empty() && fs::exists(currentProjectRoot_ / "Assets")) {
			fs::copy(currentProjectRoot_ / "Assets", outDir / "Assets", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
		}
		if (!currentProjectRoot_.empty() && fs::exists(currentProjectRoot_ / "ProjectSettings" / "input.json")) {
			fs::create_directories(outDir / "ProjectSettings");
			fs::copy_file(currentProjectRoot_ / "ProjectSettings" / "input.json", outDir / "ProjectSettings" / "input.json", fs::copy_options::overwrite_existing);
		}

		// Only the engine resources that are used: what the runtime always loads, plus every "Resources/..." the project names
		std::set<std::string> resources = { "Resources/circle.png", "Resources/skybox.dds", "Resources/Model/white.png" };
		if (!currentProjectRoot_.empty()) {
			std::error_code scanEc;
			for (const fs::path& dir : { currentProjectRoot_ / "Assets", currentProjectRoot_ / "GameScripts" / "Scripts" }) {
				for (auto it = fs::recursive_directory_iterator(dir, scanEc); !scanEc && it != fs::recursive_directory_iterator(); it.increment(scanEc)) {
					const std::string ext = it->path().extension().string();
					if (ext == ".json" || ext == ".cpp" || ext == ".h") CollectResourceReferences(it->path(), resources);
				}
				scanEc.clear();
			}
		}
		for (const std::string& resource : resources) {
			const fs::path source(resource);
			std::error_code copyEc;
			if (!fs::is_regular_file(source, copyEc)) {
				LogMessage("[Build] Referenced resource not found, not exported: " + resource, 1);
				continue;
			}
			fs::create_directories((outDir / source).parent_path());
			fs::copy_file(source, outDir / source, fs::copy_options::overwrite_existing);
			for (const std::string& companion : EditorSuiteDetail::ModelCompanionFiles(source)) {
				fs::create_directories((outDir / source.parent_path() / companion).parent_path());
				fs::copy_file(source.parent_path() / companion, outDir / source.parent_path() / companion, fs::copy_options::overwrite_existing, copyEc);
			}
			const fs::path fbm = source.parent_path() / (source.stem().string() + ".fbm");
			if (fs::is_directory(fbm, copyEc)) {
				fs::copy(fbm, outDir / fbm, fs::copy_options::recursive | fs::copy_options::overwrite_existing, copyEc);
			}
		}

		// Determine relative path for startup scene inside exported directory
		std::string rawStartup = (buildStartupSceneBuf_[0] != '\0') ? buildStartupSceneBuf_ : "Assets/Scenes/SampleScene.scene.json";
		std::filesystem::path srcScenePath(rawStartup);
		std::string relStartup = "Assets/Scenes/SampleScene.scene.json";
		const std::string projectRelStartup = ToProjectRelativePath(srcScenePath);
		if (!srcScenePath.is_absolute()) {
			relStartup = srcScenePath.generic_string();
		} else if (!std::filesystem::path(projectRelStartup).is_absolute()) {
			relStartup = projectRelStartup;
		} else if (srcScenePath.has_filename()) {
			relStartup = (std::filesystem::path("Assets/Scenes") / srcScenePath.filename()).generic_string();
		}

		std::filesystem::path destScenePath = outDir / relStartup;
		std::filesystem::create_directories(destScenePath.parent_path());

		if (std::filesystem::exists(srcScenePath)) {
			std::error_code ec;
			if (!std::filesystem::equivalent(srcScenePath, destScenePath, ec)) {
				std::filesystem::copy_file(srcScenePath, destScenePath, std::filesystem::copy_options::overwrite_existing, ec);
			}
		} else {
			std::ofstream sf(destScenePath);
			if (sf.is_open()) {
				sf << SerializeSceneToString();
			}
		}

		int resW = 1920, resH = 1080;
		if (buildResolutionIndex_ == 1) { resW = 1280; resH = 720; }
		else if (buildResolutionIndex_ == 2) { resW = 2560; resH = 1440; }

		{
			std::ofstream cfg((outDir / kExportedGameConfigFile).string());
			cfg << "standaloneGameMode = true\n";
			cfg << "gameTitle = " << gameTitle << "\n";
			cfg << "resolutionWidth = " << resW << "\n";
			cfg << "resolutionHeight = " << resH << "\n";
			cfg << "fullscreen = " << (buildFullscreen_ ? "true" : "false") << "\n";
			cfg << "startupScene = " << relStartup << "\n";
		}

		// Keep the build settings with the project (shared through git)
		if (!currentProjectRoot_.empty()) {
			const std::filesystem::path projJson = currentProjectRoot_ / "ProjectSettings" / "project.json";
			std::ifstream in(projJson);
			std::stringstream text;
			text << in.rdbuf();
			in.close();
			std::string projectName = currentProjectRoot_.filename().string();
			if (const auto settings = SceneJson::Parse(text.str())) SceneJson::Read(*settings, "projectName", projectName);
			WriteProjectSettingsFile(projJson, projectName, relStartup, gameTitle);
		}

		LogMessage(std::format("[Build] Successfully exported standalone game '{}' to: {}", targetExeName, outDir.string()), 0);
		return true;
	} catch (const std::exception& e) {
		LogMessage(std::format("[Build] Standalone export failed: {}", e.what()), 2);
		return false;
	}
}

