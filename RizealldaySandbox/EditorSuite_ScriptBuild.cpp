#include "EditorSuiteInternal.h"

namespace {
	// Generated scripts start with a UTF-8 BOM (project rule), so Japanese comments read correctly in every tool
	constexpr const char* kUtf8Bom = "\xEF\xBB\xBF";
}

void EditorSuite::DrawCppScriptGeneratorModal()
{
#ifdef USE_IMGUI
	ImGui::OpenPopup(TrId("Unity-Style C++ Script Generator"));
	if (ImGui::BeginPopupModal(TrId("Unity-Style C++ Script Generator"), &showScriptGenModal_, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text(Tr("Create New C++ Script in GameScripts/Scripts/ (Derives from ScriptBase)"));
		ImGui::Separator();

		if (ImGui::InputText(TrId("Class Name"), scriptClassNameBuf_, sizeof(scriptClassNameBuf_))) {
			scriptGenErrorMsg_.clear();
		}
		ImGui::Checkbox(TrId("Include OnCollision Override"), &scriptIncludeCollision_);
		std::filesystem::path targetScriptsDir = ResolveGameScriptsDir() / "Scripts";
		ImGui::TextDisabled(Tr("Target folder: %s"), targetScriptsDir.string().c_str());
		ImGui::TextDisabled(Tr("Generates <ClassName>.h and <ClassName>.cpp, builds GameScripts.dll, and opens in Visual Studio."));

		if (!scriptGenErrorMsg_.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", scriptGenErrorMsg_.c_str());
		}

		ImGui::Separator();
		if (ImGui::Button(TrId("Create & Open"), ImVec2(140.0f, 0.0f))) {
			if (GenerateCppScriptFiles(scriptClassNameBuf_, targetScriptsDir.string(), scriptIncludeCollision_)) {
				showScriptGenModal_ = false;
				scriptGenTargetObjectIndex_ = -1;
				scriptGenErrorMsg_.clear();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(TrId("Cancel"), ImVec2(120.0f, 0.0f))) {
			showScriptGenModal_ = false;
			scriptGenTargetObjectIndex_ = -1;
			scriptGenErrorMsg_.clear();
		}
		ImGui::EndPopup();
	}
#endif
}

bool EditorSuite::GenerateCppScriptFiles(const std::string& className, const std::string& folderPath, bool includeCollision)
{
	// Validate C++ identifier
	if (className.empty()) {
		scriptGenErrorMsg_ = "Error: Class Name cannot be empty.";
		LogMessage("[ScriptGen] " + scriptGenErrorMsg_, 2);
		return false;
	}
	if (!(std::isalpha(static_cast<unsigned char>(className.front())) || className.front() == '_')) {
		scriptGenErrorMsg_ = "Error: Class Name must start with a letter or underscore.";
		LogMessage("[ScriptGen] " + scriptGenErrorMsg_, 2);
		return false;
	}
	for (char c : className) {
		if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
			scriptGenErrorMsg_ = "Error: Class Name must contain only letters, digits, or underscores.";
			LogMessage("[ScriptGen] " + scriptGenErrorMsg_, 2);
			return false;
		}
	}

	std::filesystem::path targetDir = folderPath.empty() ? (ResolveGameScriptsDir() / "Scripts") : std::filesystem::path(folderPath);
	std::error_code ec;
	std::filesystem::create_directories(targetDir, ec);
	std::filesystem::path headerPath = targetDir / (className + ".h");
	std::filesystem::path sourcePath = targetDir / (className + ".cpp");

	if (scriptModule_.IsRegistered(className) || std::filesystem::exists(headerPath, ec) || std::filesystem::exists(sourcePath, ec)) {
		scriptGenErrorMsg_ = std::format("Error: Script '{}' already exists.", className);
		LogMessage("[ScriptGen] " + scriptGenErrorMsg_, 2);
		return false;
	}

	// Generate <ClassName>.h deriving from ScriptBase with EXPOSE_FIELD sample
	{
		std::ofstream h(headerPath);
		if (!h.is_open()) {
			scriptGenErrorMsg_ = std::format("Error: Failed to write header file: {}", headerPath.string());
			LogMessage("[ScriptGen] " + scriptGenErrorMsg_, 2);
			return false;
		}
		h << kUtf8Bom;
		h << "#pragma once\n";
		h << "#include \"base/ScriptAPI.h\"\n\n";
		h << "// Note: Fields not marked EXPOSE_FIELD are not preserved across a script hot reload.\n";
		h << "class " << className << " : public ScriptBase {\n";
		h << "public:\n";
		h << "    EXPOSE_FIELD(float, speed, 1.0f);\n\n";
		h << "    void Initialize() override;\n";
		h << "    void Update(float deltaTime) override;\n";
		if (includeCollision) {
			h << "    void OnCollision(GameObject& other) override;\n";
		}
		h << "    void OnDestroy() override;\n";
		h << "};\n";
		h.close();
	}

	// Generate <ClassName>.cpp with method stubs and REGISTER_SCRIPT(ClassName)
	{
		std::ofstream cpp(sourcePath);
		if (!cpp.is_open()) {
			scriptGenErrorMsg_ = std::format("Error: Failed to write source file: {}", sourcePath.string());
			LogMessage("[ScriptGen] " + scriptGenErrorMsg_, 2);
			return false;
		}
		cpp << kUtf8Bom;
		cpp << "#include \"" << className << ".h\"\n\n";
		cpp << "void " << className << "::Initialize() {\n";
		cpp << "    if (GameObject* self = GetOwner()) {\n";
		cpp << "        (void)self;\n";
		cpp << "    }\n";
		cpp << "}\n\n";
		cpp << "void " << className << "::Update(float deltaTime) {\n";
		cpp << "    if (GameObject* self = GetOwner()) {\n";
		cpp << "        self->rotation.y += speed * deltaTime;\n";
		cpp << "        self->rotationQuat = Quaternion::FromEuler(self->rotation);\n";
		cpp << "    }\n";
		cpp << "}\n\n";
		if (includeCollision) {
			cpp << "void " << className << "::OnCollision(GameObject& other) {\n";
			cpp << "    (void)other;\n";
			cpp << "}\n\n";
		}
		cpp << "void " << className << "::OnDestroy() {\n";
		cpp << "}\n\n";
		cpp << "REGISTER_SCRIPT(" << className << ")\n";
		cpp.close();
	}

	AddScriptFilesToVcxproj({ className });

	LogMessage(std::format("[Scripts] Created {}.h and {}.cpp in {}", className, className, targetDir.string()), 0);

	// 1. Attach <ClassName> to the target GameObject in a "Pending / Compiling..." state
	int attachIdx = (scriptGenTargetObjectIndex_ >= 0 && scriptGenTargetObjectIndex_ < static_cast<int>(gameObjects_.size()))
		? scriptGenTargetObjectIndex_
		: selectedObjectIndex_;
	if (attachIdx >= 0 && attachIdx < static_cast<int>(gameObjects_.size())) {
		auto& targetObj = gameObjects_[attachIdx];
		GameObject::ScriptComponent sc(className);
		sc.isCompiling = true;
		sc.isMissingScript = false;
		targetObj.scripts.push_back(std::move(sc));
		targetObj.hasScript = true;
		targetObj.scriptClassName = className;
		isSceneDirty_ = true;
		PushUndoSnapshot();
		LogMessage(std::format("[Scripts] Attached '{}' (Pending / Compiling...) to '{}'", className, targetObj.name), 0);
	}

	// 2. Trigger Build Scripts automatically
	TriggerScriptBuild();

	// 3. Open <ClassName>.cpp in Visual Studio
	OpenFileInVisualStudio(sourcePath);

	return true;
}

std::filesystem::path EditorSuite::ResolveGameScriptsDir() const
{
	if (std::filesystem::path projectDir = ProjectScriptsDir(); !projectDir.empty()) return projectDir;
	std::error_code ec;
	std::filesystem::path dir = ScriptModule::ResolveWorkspaceRoot() / ScriptModule::kGameScriptsDirName;
	if (std::filesystem::exists(dir, ec)) {
		return std::filesystem::weakly_canonical(dir, ec);
	}
	return dir;
}

std::filesystem::path EditorSuite::ResolveGameScriptsVcxproj() const
{
	if (std::filesystem::path projectDir = ProjectScriptsDir(); !projectDir.empty()) return projectDir / "GameScripts.vcxproj";
	std::error_code ec;
	std::filesystem::path vcxproj = ScriptModule::ResolveWorkspaceRoot() / ScriptModule::kGameScriptsVcxprojSubpath;
	if (std::filesystem::exists(vcxproj, ec)) {
		return std::filesystem::weakly_canonical(vcxproj, ec);
	}
	return vcxproj;
}

/// The solution next to the scripts folder (*.slnx preferred, then *.sln): the engine solution or <Project>.sln
std::filesystem::path EditorSuite::ResolveSolutionFilePath() const
{
	std::error_code ec;
	const std::filesystem::path rootDir = ResolveGameScriptsVcxproj().parent_path().parent_path();
	std::filesystem::path sln;
	for (const auto& entry : std::filesystem::directory_iterator(rootDir, ec)) {
		if (!entry.is_regular_file(ec)) continue;
		const std::wstring ext = entry.path().extension().wstring();
		if (ext == L".slnx") return entry.path();
		if (ext == L".sln" && sln.empty()) sln = entry.path();
	}
	return sln;
}

std::string EditorSuite::ResolveMsBuildExe()
{
	if (!resolvedMsBuildPath_.empty() && std::filesystem::exists(resolvedMsBuildPath_)) {
		return resolvedMsBuildPath_;
	}

	std::error_code ec;
	// 1. Try vswhere.exe
	std::filesystem::path vswhere = "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";
	if (std::filesystem::exists(vswhere, ec)) {
		SECURITY_ATTRIBUTES sa{};
		sa.nLength = sizeof(sa);
		sa.bInheritHandle = TRUE;
		HANDLE rPipe = NULL, wPipe = NULL;
		if (CreatePipe(&rPipe, &wPipe, &sa, 0)) {
			SetHandleInformation(rPipe, HANDLE_FLAG_INHERIT, 0);
			STARTUPINFOW si{};
			si.cb = sizeof(si);
			si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
			si.wShowWindow = SW_HIDE;
			si.hStdOutput = wPipe;
			si.hStdError = wPipe;
			PROCESS_INFORMATION pi{};
			std::wstring cmd = L"\"" + vswhere.wstring() + L"\" -latest -requires Microsoft.Component.MSBuild -find MSBuild\\**\\Bin\\MSBuild.exe";
			std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
			cmdBuf.push_back(L'\0');
			if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
				CloseHandle(wPipe);
				wPipe = NULL;
				std::string output;
				char buf[512];
				DWORD bytesRead = 0;
				while (ReadFile(rPipe, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
					buf[bytesRead] = '\0';
					output.append(buf, bytesRead);
				}
				WaitForSingleObject(pi.hProcess, 3000);
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);
				std::istringstream iss(output);
				std::string line;
				while (std::getline(iss, line)) {
					while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
						line.pop_back();
					}
					if (!line.empty() && std::filesystem::exists(line, ec)) {
						resolvedMsBuildPath_ = line;
						CloseHandle(rPipe);
						return resolvedMsBuildPath_;
					}
				}
			}
			if (wPipe) CloseHandle(wPipe);
			CloseHandle(rPipe);
		}
	}

	// 2. Check common Visual Studio 18 / 2022 paths and fallback buffer
	std::vector<std::string> fallbacks = {
		msbuildFallbackPathBuf_,
		"C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe",
		"C:\\Program Files\\Microsoft Visual Studio\\18\\Professional\\MSBuild\\Current\\Bin\\MSBuild.exe",
		"C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\MSBuild\\Current\\Bin\\MSBuild.exe",
		"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe",
		"C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\MSBuild\\Current\\Bin\\MSBuild.exe",
		"C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\MSBuild\\Current\\Bin\\MSBuild.exe"
	};

	for (const auto& candidate : fallbacks) {
		if (!candidate.empty() && std::filesystem::exists(candidate, ec)) {
			resolvedMsBuildPath_ = candidate;
			return resolvedMsBuildPath_;
		}
	}

	return msbuildFallbackPathBuf_;
}

void EditorSuite::TriggerScriptBuild()
{
	if (isBuildingScripts_.load()) {
		queuedScriptRebuild_.store(true);
		LogMessage("[Scripts] Build already in progress; queued another rebuild when it finishes.", 0);
		return;
	}

	if (scriptBuildThread_.joinable()) {
		scriptBuildThread_.join();
	}

	std::string msbuildExe = ResolveMsBuildExe();
	std::filesystem::path vcxprojPath = ResolveGameScriptsVcxproj();
	std::error_code ec;
	if (!std::filesystem::exists(msbuildExe, ec)) {
		LogMessage(std::format("[Scripts] Error: MSBuild.exe not found at '{}'.", msbuildExe), 2);
		lastScriptBuildStatus_.store(2);
		return;
	}
	if (!std::filesystem::exists(vcxprojPath, ec)) {
		LogMessage(std::format("[Scripts] Error: GameScripts.vcxproj not found at '{}'.", vcxprojPath.string()), 2);
		lastScriptBuildStatus_.store(2);
		return;
	}
	EnsureProjectScriptsProps(); // a freshly cloned project has no props for this PC yet

	isBuildingScripts_.store(true);
	queuedScriptRebuild_.store(false);
	LogMessage("[Scripts] Building GameScripts.vcxproj (Development|x64)...", 0);

	scriptBuildThread_ = std::thread([this, msbuildExe, vcxprojPath]() {
		bool runAgain = true;
		while (runAgain) {
			runAgain = false;
			auto startTime = std::chrono::steady_clock::now();

			SECURITY_ATTRIBUTES sa{};
			sa.nLength = sizeof(sa);
			sa.bInheritHandle = TRUE;
			HANDLE rPipe = NULL, wPipe = NULL;
			if (!CreatePipe(&rPipe, &wPipe, &sa, 0)) {
				std::lock_guard<std::mutex> lock(buildLogMutex_);
				pendingBuildLogs_.push_back({ "[Scripts] Build failed: CreatePipe error.", 2 });
				lastScriptBuildStatus_.store(2);
				break;
			}
			SetHandleInformation(rPipe, HANDLE_FLAG_INHERIT, 0);

			STARTUPINFOW si{};
			si.cb = sizeof(si);
			si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
			si.wShowWindow = SW_HIDE;
			si.hStdOutput = wPipe;
			si.hStdError = wPipe;

			PROCESS_INFORMATION pi{};
			std::wstring cmdLine = L"\"" + std::filesystem::path(msbuildExe).wstring() + L"\" \""
				+ vcxprojPath.wstring() + L"\" /t:Build /p:Configuration=Development /p:Platform=x64 /v:minimal /nologo";
			std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
			cmdBuf.push_back(L'\0');

			std::wstring workDir = vcxprojPath.parent_path().wstring();
			BOOL started = CreateProcessW(
				NULL,
				cmdBuf.data(),
				NULL,
				NULL,
				TRUE,
				CREATE_NO_WINDOW,
				NULL,
				workDir.c_str(),
				&si,
				&pi);

			CloseHandle(wPipe);
			wPipe = NULL;

			if (!started) {
				CloseHandle(rPipe);
				std::lock_guard<std::mutex> lock(buildLogMutex_);
				pendingBuildLogs_.push_back({ "[Scripts] Build failed: Could not start MSBuild.exe process.", 2 });
				lastScriptBuildStatus_.store(2);
				break;
			}

			std::string pendingText;
			char buffer[1024];
			DWORD bytesRead = 0;
			auto pushOutputLine = [this](std::string line) {
				while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
					line.pop_back();
				}
				if (line.empty()) return;
				int logType = 0;
				std::string lower = line;
				std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (lower.find(": error ") != std::string::npos || lower.find("fatal error") != std::string::npos) {
					logType = 2;
				} else if (lower.find(": warning ") != std::string::npos) {
					logType = 1;
				}
				std::lock_guard<std::mutex> lock(buildLogMutex_);
				pendingBuildLogs_.push_back({ "[MSBuild] " + line, logType });
			};

			while (ReadFile(rPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
				buffer[bytesRead] = '\0';
				pendingText.append(buffer, bytesRead);
				size_t newlinePos = 0;
				while ((newlinePos = pendingText.find('\n')) != std::string::npos) {
					std::string line = pendingText.substr(0, newlinePos);
					pendingText.erase(0, newlinePos + 1);
					pushOutputLine(std::move(line));
				}
			}
			if (!pendingText.empty()) {
				pushOutputLine(std::move(pendingText));
			}

			WaitForSingleObject(pi.hProcess, INFINITE);
			DWORD exitCode = 1;
			GetExitCodeProcess(pi.hProcess, &exitCode);
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
			CloseHandle(rPipe);

			auto endTime = std::chrono::steady_clock::now();
			double elapsedSec = std::chrono::duration<double>(endTime - startTime).count();

			{
				std::lock_guard<std::mutex> lock(buildLogMutex_);
				if (exitCode == 0) {
					pendingBuildLogs_.push_back({ std::format("[Scripts] Build succeeded ({:.2f}s)", elapsedSec), 0 });
					lastScriptBuildStatus_.store(1);
				} else {
					pendingBuildLogs_.push_back({ std::format("[Scripts] Build failed (exit code {})", exitCode), 2 });
					lastScriptBuildStatus_.store(2);
				}
			}

			if (queuedScriptRebuild_.exchange(false)) {
				runAgain = true;
			}
		}

		isBuildingScripts_.store(false);
	});
}

void EditorSuite::DrainScriptBuildLogs()
{
	std::vector<PendingBuildLog> logs;
	{
		std::lock_guard<std::mutex> lock(buildLogMutex_);
		if (pendingBuildLogs_.empty()) return;
		logs.swap(pendingBuildLogs_);
	}

	bool hadError = false;
	for (const auto& entry : logs) {
		LogMessage(entry.line, entry.type);
		if (entry.type == 2) {
			hadError = true;
		}
	}
	if (hadError) {
		bottomPanelTab_ = 1; // Focus Console tab on build error
	}
}

void EditorSuite::PollScriptSourceWatcher(float deltaTime)
{
	scriptSourcePollTimer_ += deltaTime;
	if (scriptSourcePollTimer_ >= 0.25f) {
		scriptSourcePollTimer_ = 0.0f;

		std::filesystem::path scriptsDir = ResolveGameScriptsDir() / "Scripts";
		std::error_code ec;
		if (std::filesystem::exists(scriptsDir, ec) && std::filesystem::is_directory(scriptsDir, ec)) {
			std::map<std::string, std::filesystem::file_time_type> currentStamps;
			for (const auto& entry : std::filesystem::directory_iterator(scriptsDir, ec)) {
				if (!entry.is_regular_file(ec)) continue;
				std::string ext = entry.path().extension().string();
				if (ext == ".cpp" || ext == ".h") {
					currentStamps[entry.path().filename().string()] = std::filesystem::last_write_time(entry.path(), ec);
				}
			}

			if (!scriptSourceWatcherInitialized_) {
				scriptSourceTimestamps_ = std::move(currentStamps);
				scriptSourceWatcherInitialized_ = true;
			} else if (currentStamps != scriptSourceTimestamps_) {
				scriptSourceTimestamps_ = std::move(currentStamps);
				if (autoBuildScripts_) {
					scriptSourceChangePending_ = true;
					scriptSourceDebounceTimer_ = 0.0f;
				}
			}
		}
	}

	if (scriptSourceChangePending_ && autoBuildScripts_) {
		scriptSourceDebounceTimer_ += deltaTime;
		if (scriptSourceDebounceTimer_ >= 1.0f) {
			scriptSourceChangePending_ = false;
			scriptSourceDebounceTimer_ = 0.0f;
			LogMessage("[Scripts] Detected source changes in GameScripts/Scripts; triggering Auto Build...", 0);
			TriggerScriptBuild();
		}
	}
}

namespace {
	// Late-bound calls on the Visual Studio automation object (EnvDTE via IDispatch)
	bool DispInvoke(IDispatch* obj, const wchar_t* name, WORD flags, VARIANT* args, UINT argCount, VARIANT* result)
	{
		if (!obj) return false;
		LPOLESTR names[1] = { const_cast<LPOLESTR>(name) };
		DISPID id = DISPID_UNKNOWN;
		if (FAILED(obj->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &id))) return false;
		DISPPARAMS params = { args, nullptr, argCount, 0 };
		VARIANT scratch;
		VariantInit(&scratch);
		const HRESULT hr = obj->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &params, result ? result : &scratch, nullptr, nullptr);
		VariantClear(&scratch);
		return SUCCEEDED(hr);
	}

	Microsoft::WRL::ComPtr<IDispatch> DispGetObject(IDispatch* obj, const wchar_t* name)
	{
		VARIANT v;
		VariantInit(&v);
		Microsoft::WRL::ComPtr<IDispatch> out;
		if (DispInvoke(obj, name, DISPATCH_PROPERTYGET, nullptr, 0, &v) && v.vt == VT_DISPATCH && v.pdispVal) out = v.pdispVal;
		VariantClear(&v);
		return out;
	}

	/// DTE.Solution.FullName ("" while no solution is open)
	std::filesystem::path DteSolutionPath(IDispatch* dte)
	{
		Microsoft::WRL::ComPtr<IDispatch> solution = DispGetObject(dte, L"Solution");
		VARIANT v;
		VariantInit(&v);
		std::filesystem::path out;
		if (solution && DispInvoke(solution.Get(), L"FullName", DISPATCH_PROPERTYGET, nullptr, 0, &v) && v.vt == VT_BSTR && v.bstrVal) {
			out = std::filesystem::path(std::wstring(v.bstrVal, SysStringLen(v.bstrVal)));
		}
		VariantClear(&v);
		return out;
	}

	/// Every Visual Studio instance registered in the Running Object Table
	std::vector<Microsoft::WRL::ComPtr<IDispatch>> RunningVisualStudios()
	{
		std::vector<Microsoft::WRL::ComPtr<IDispatch>> result;
		Microsoft::WRL::ComPtr<IRunningObjectTable> rot;
		Microsoft::WRL::ComPtr<IEnumMoniker> monikers;
		if (FAILED(GetRunningObjectTable(0, &rot)) || !rot || FAILED(rot->EnumRunning(&monikers)) || !monikers) return result;
		Microsoft::WRL::ComPtr<IMoniker> moniker;
		while (monikers->Next(1, moniker.ReleaseAndGetAddressOf(), nullptr) == S_OK) {
			Microsoft::WRL::ComPtr<IBindCtx> bindCtx;
			if (FAILED(CreateBindCtx(0, &bindCtx))) continue;
			LPOLESTR displayName = nullptr;
			if (FAILED(moniker->GetDisplayName(bindCtx.Get(), nullptr, &displayName)) || !displayName) continue;
			const bool isDte = std::wstring_view(displayName).starts_with(L"!VisualStudio.DTE");
			CoTaskMemFree(displayName);
			if (!isDte) continue;
			Microsoft::WRL::ComPtr<IUnknown> unknown;
			Microsoft::WRL::ComPtr<IDispatch> dte;
			if (SUCCEEDED(rot->GetObject(moniker.Get(), &unknown)) && unknown && SUCCEEDED(unknown.As(&dte))) result.push_back(dte);
		}
		return result;
	}

	bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b)
	{
		if (a.empty() || b.empty()) return false;
		std::error_code ec;
		return std::filesystem::equivalent(a, b, ec);
	}

	/// ItemOperations.OpenFile(file) + MainWindow.Activate()
	bool DteOpenFile(IDispatch* dte, const std::filesystem::path& file)
	{
		Microsoft::WRL::ComPtr<IDispatch> itemOps = DispGetObject(dte, L"ItemOperations");
		if (!itemOps) return false;
		VARIANT arg;
		VariantInit(&arg);
		arg.vt = VT_BSTR;
		arg.bstrVal = SysAllocString(file.wstring().c_str());
		const bool opened = DispInvoke(itemOps.Get(), L"OpenFile", DISPATCH_METHOD, &arg, 1, nullptr);
		VariantClear(&arg);
		if (Microsoft::WRL::ComPtr<IDispatch> mainWindow = DispGetObject(dte, L"MainWindow")) {
			DispInvoke(mainWindow.Get(), L"Activate", DISPATCH_METHOD, nullptr, 0, nullptr);
		}
		return opened;
	}

	std::filesystem::path FindDevenv()
	{
		std::error_code ec;
		for (const wchar_t* version : { L"18", L"2022" }) {
			for (const wchar_t* edition : { L"Community", L"Professional", L"Enterprise", L"Insiders" }) {
				const std::filesystem::path devenv = std::filesystem::path(L"C:\\Program Files\\Microsoft Visual Studio") / version / edition / L"Common7\\IDE\\devenv.exe";
				if (std::filesystem::exists(devenv, ec)) return devenv;
			}
		}
		return {};
	}
}

// Opens a script inside the scripts solution (engine or project), so IntelliSense knows the GameScripts include paths
// (a file opened on its own lands in "Miscellaneous Files" and gets no completion for ScriptAPI.h).
void EditorSuite::OpenFileInVisualStudio(const std::filesystem::path& filePath)
{
	std::error_code ec;
	if (!std::filesystem::exists(filePath, ec)) {
		LogMessage(std::format("[Scripts] Cannot open file (does not exist): {}", filePath.string()), 1);
		return;
	}

	std::filesystem::path absFile = std::filesystem::canonical(filePath, ec);
	if (ec) absFile = std::filesystem::absolute(filePath, ec);

	const std::filesystem::path solution = ResolveSolutionFilePath();
	std::thread([this, absFile, solution]() {
		auto logAsync = [this](const std::string& text, int type = 0) {
			std::lock_guard<std::mutex> lock(buildLogMutex_);
			pendingBuildLogs_.push_back({ text, type });
		};
		const std::string fileName = absFile.filename().string();

		const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		const bool comInitialized = SUCCEEDED(hr);

		// 1. A Visual Studio that already has the engine solution open
		auto openInSolutionInstance = [&]() {
			for (const auto& dte : RunningVisualStudios()) {
				if (SamePath(DteSolutionPath(dte.Get()), solution) && DteOpenFile(dte.Get(), absFile)) return true;
			}
			return false;
		};
		bool opened = false;
		if (comInitialized && !solution.empty()) {
			opened = openInSolutionInstance();
			if (opened) logAsync(std::format("[Scripts] Opened '{}' in Visual Studio ({}).", fileName, solution.filename().string()), 0);
		}

		// 2. Start Visual Studio with the solution, then open the file once the solution is registered
		if (!opened && comInitialized && !solution.empty()) {
			bool launched = false;
			if (const std::filesystem::path devenv = FindDevenv(); !devenv.empty()) {
				std::wstring cmd = L"\"" + devenv.wstring() + L"\" \"" + solution.wstring() + L"\"";
				STARTUPINFOW si{};
				si.cb = sizeof(si);
				PROCESS_INFORMATION pi{};
				if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
					CloseHandle(pi.hProcess);
					CloseHandle(pi.hThread);
					launched = true;
				}
			}
			if (!launched) {
				launched = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", solution.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
			}
			if (launched) {
				logAsync(std::format("[Scripts] Starting Visual Studio with {} (opens '{}' when ready)...", solution.filename().string(), fileName), 0);
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
				while (!opened && std::chrono::steady_clock::now() < deadline) {
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					opened = openInSolutionInstance();
				}
				if (opened) {
					logAsync(std::format("[Scripts] Opened '{}' in Visual Studio ({}).", fileName, solution.filename().string()), 0);
				} else {
					logAsync(std::format("[Scripts] Visual Studio did not report the solution in time; open '{}' from Solution Explorer.", fileName), 1);
				}
				opened = true; // Visual Studio is running with the solution either way
			}
		}

		// 3. No solution found: any running Visual Studio
		if (!opened && comInitialized) {
			for (const auto& dte : RunningVisualStudios()) {
				if (DteOpenFile(dte.Get(), absFile)) {
					opened = true;
					logAsync(std::format("[Scripts] Opened '{}' outside the engine solution (IntelliSense may be limited).", fileName), 1);
					break;
				}
			}
		}
		if (comInitialized) CoUninitialize();
		if (opened) return;

		// 4. Default handler for the file type
		if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", absFile.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32) {
			logAsync(std::format("[Scripts] Opened '{}' with the default editor.", fileName), 0);
		} else {
			logAsync(std::format("[Scripts] Warning: Failed to open '{}' in Visual Studio or the default editor.", absFile.string()), 1);
		}
	}).detach();
}


