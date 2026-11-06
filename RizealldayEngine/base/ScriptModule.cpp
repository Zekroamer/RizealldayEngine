#include "base/ScriptModule.h"
#include "base/ScriptBase.h"
#include <fstream>
#include <format>
#include <algorithm>

ScriptModule::~ScriptModule() {
	Unload();
}

std::filesystem::path ScriptModule::ResolveWorkspaceRoot() {
	std::error_code ec;
	std::vector<std::filesystem::path> roots;

	char exeBuf[MAX_PATH] = {};
	if (GetModuleFileNameA(nullptr, exeBuf, MAX_PATH) > 0) {
		std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
		// When running from $(SolutionDir)bin\$(Configuration)\RizealldaySandbox.exe, parent.parent is $(SolutionDir)
		roots.push_back(exeDir.parent_path().parent_path());
		roots.push_back(exeDir.parent_path());
		roots.push_back(exeDir);
	}

	std::filesystem::path cwd = std::filesystem::current_path(ec);
	if (!cwd.empty()) {
		roots.push_back(cwd);
		roots.push_back(cwd.parent_path());
	}

	for (const auto& root : roots) {
		if (!root.empty() && std::filesystem::exists(root / kGameScriptsVcxprojSubpath, ec)) {
			return std::filesystem::weakly_canonical(root, ec);
		}
	}

	// Standalone build fallback: prefer directory containing kScriptsDllSubpath
	for (const auto& root : roots) {
		if (!root.empty() && std::filesystem::exists(root / kScriptsDllSubpath, ec)) {
			return std::filesystem::weakly_canonical(root, ec);
		}
	}

	return !cwd.empty() ? cwd : std::filesystem::path(".");
}

std::filesystem::path ScriptModule::ResolveDllPath() const {
	std::error_code ec;
	if (!dllPathOverride_.empty()) return std::filesystem::weakly_canonical(dllPathOverride_, ec);
	std::filesystem::path root = ResolveWorkspaceRoot();
	return std::filesystem::weakly_canonical(root / kScriptsDllSubpath, ec);
}

void ScriptModule::SetDllPath(const std::filesystem::path& dllPath) {
	dllPathOverride_ = dllPath;
	hasLoadedWriteTime_ = false;
}

void ScriptModule::CleanupOldShadowCopies(const std::filesystem::path& shadowDir) {
	std::error_code ec;
	if (std::filesystem::exists(shadowDir, ec) && std::filesystem::is_directory(shadowDir, ec)) {
		for (const auto& entry : std::filesystem::directory_iterator(shadowDir, ec)) {
			if (entry.is_regular_file(ec)) {
				std::filesystem::remove(entry.path(), ec);
			}
		}
	}

	// Also clean up old unlocked timestamped PDBs in the parent Scripts directory on startup
	std::filesystem::path scriptsDir = shadowDir.parent_path();
	if (std::filesystem::exists(scriptsDir, ec) && std::filesystem::is_directory(scriptsDir, ec)) {
		for (const auto& entry : std::filesystem::directory_iterator(scriptsDir, ec)) {
			if (entry.is_regular_file(ec) && entry.path().extension() == ".pdb") {
				std::filesystem::remove(entry.path(), ec);
			}
		}
	}
}

bool ScriptModule::Initialize(const EngineAPI* engineApi) {
	engineApi_ = engineApi;
	dllSourcePath_ = ResolveDllPath();

	std::error_code ec;
	std::filesystem::path shadowDir = dllSourcePath_.parent_path() / "Shadow";
	std::filesystem::create_directories(shadowDir, ec);
	CleanupOldShadowCopies(shadowDir);

	if (std::filesystem::exists(dllSourcePath_, ec)) {
		lastLoadError_.clear();
		return LoadShadowCopy(&lastLoadError_);
	}
	return false;
}

void ScriptModule::Unload() {
	registeredNames_.clear();
	fnInit_ = nullptr;
	fnCreateScript_ = nullptr;
	fnDestroyScript_ = nullptr;
	fnGetScriptCount_ = nullptr;
	fnGetScriptName_ = nullptr;
	fnGetFieldCount_ = nullptr;
	fnGetFieldInfo_ = nullptr;
	fnGetFieldName_ = nullptr;
	fnGetFieldType_ = nullptr;
	fnGetFieldData_ = nullptr;

	if (dllHandle_) {
		FreeLibrary(dllHandle_);
		dllHandle_ = nullptr;
	}
	ScriptRegistry::GetInstance().SetActiveModule(this);
}

bool ScriptModule::LoadShadowCopy(std::string* outErrorMsg) {
	lastLoadApiMismatch_ = false;
	Unload();

	dllSourcePath_ = ResolveDllPath();
	std::error_code ec;
	if (!std::filesystem::exists(dllSourcePath_, ec)) {
		if (outErrorMsg) *outErrorMsg = std::format("GameScripts.dll not found at {}", dllSourcePath_.string());
		return false;
	}

	// Verify file is readable and non-empty
	auto fileSize = std::filesystem::file_size(dllSourcePath_, ec);
	if (ec || fileSize == 0) {
		if (outErrorMsg) *outErrorMsg = "GameScripts.dll is empty or still being written.";
		return false;
	}

	std::filesystem::path shadowDir = dllSourcePath_.parent_path() / "Shadow";
	std::filesystem::create_directories(shadowDir, ec);

	++shadowGeneration_;
	activeShadowPath_ = shadowDir / std::format("GameScripts_{}.dll", shadowGeneration_);

	if (!std::filesystem::copy_file(dllSourcePath_, activeShadowPath_, std::filesystem::copy_options::overwrite_existing, ec)) {
		if (outErrorMsg) *outErrorMsg = std::format("Failed to shadow-copy GameScripts.dll to {}: {}", activeShadowPath_.string(), ec.message());
		return false;
	}

	lastLoadedWriteTime_ = std::filesystem::last_write_time(dllSourcePath_, ec);
	hasLoadedWriteTime_ = !ec;

	dllHandle_ = LoadLibraryA(activeShadowPath_.string().c_str());
	if (!dllHandle_) {
		DWORD err = GetLastError();
		if (outErrorMsg) *outErrorMsg = std::format("LoadLibrary failed on {} (Win32 error {})", activeShadowPath_.string(), err);
		return false;
	}

	fnInit_           = reinterpret_cast<FnGameScripts_Init>(GetProcAddress(dllHandle_, "GameScripts_Init"));
	fnCreateScript_   = reinterpret_cast<FnCreateScript>(GetProcAddress(dllHandle_, "CreateScript"));
	fnDestroyScript_  = reinterpret_cast<FnDestroyScript>(GetProcAddress(dllHandle_, "DestroyScript"));
	fnGetScriptCount_ = reinterpret_cast<FnGetScriptCount>(GetProcAddress(dllHandle_, "GetScriptCount"));
	fnGetScriptName_  = reinterpret_cast<FnGetScriptName>(GetProcAddress(dllHandle_, "GetScriptName"));
	fnGetFieldCount_  = reinterpret_cast<FnGetScriptFieldCount>(GetProcAddress(dllHandle_, "GetScriptFieldCount"));
	fnGetFieldInfo_   = reinterpret_cast<FnGetScriptFieldInfo>(GetProcAddress(dllHandle_, "GetScriptFieldInfo"));
	fnGetFieldName_   = reinterpret_cast<FnGetScriptFieldName>(GetProcAddress(dllHandle_, "GetScriptFieldName"));
	fnGetFieldType_   = reinterpret_cast<FnGetScriptFieldType>(GetProcAddress(dllHandle_, "GetScriptFieldType"));
	fnGetFieldData_   = reinterpret_cast<FnGetScriptFieldData>(GetProcAddress(dllHandle_, "GetScriptFieldData"));

	if (!fnInit_ || !fnCreateScript_ || !fnDestroyScript_ || !fnGetScriptCount_ || !fnGetScriptName_) {
		if (outErrorMsg) *outErrorMsg = "GameScripts.dll is missing required exported entry points.";
		Unload();
		return false;
	}

	// A DLL built against another ScriptAPI.h would read EngineAPI / views with the wrong layout
	using FnGetApiFingerprint = uint32_t(*)();
	auto fnFingerprint = reinterpret_cast<FnGetApiFingerprint>(GetProcAddress(dllHandle_, "GameScripts_GetApiFingerprint"));
	const uint32_t dllFingerprint = fnFingerprint ? fnFingerprint() : 0u;
	if (dllFingerprint != ScriptApiFingerprint()) {
		lastLoadApiMismatch_ = true;
		if (outErrorMsg) {
			*outErrorMsg = std::format("GameScripts.dll was built against a different engine version (API fingerprint {:08X}, engine {:08X}). "
				"Rebuild the whole solution so the engine and the scripts match.", dllFingerprint, ScriptApiFingerprint());
		}
		Unload();
		return false;
	}

	fnInit_(engineApi_);

	int count = fnGetScriptCount_();
	for (int i = 0; i < count; ++i) {
		const char* name = fnGetScriptName_(i);
		if (name && name[0] != '\0') {
			registeredNames_.emplace_back(name);
		}
	}

	ScriptRegistry::GetInstance().SetActiveModule(this);
	return true;
}

bool ScriptModule::PollDllChanged(float deltaTime, bool isBuildInProgress) {
	if (isBuildInProgress) {
		return false;
	}

	pollTimer_ += deltaTime;
	if (pollTimer_ < 0.5f) {
		return false;
	}
	pollTimer_ = 0.0f;

	std::filesystem::path currentDllPath = ResolveDllPath();
	std::error_code ec;
	if (!std::filesystem::exists(currentDllPath, ec)) {
		return false;
	}

	auto wt = std::filesystem::last_write_time(currentDllPath, ec);
	if (ec) {
		return false;
	}

	if (!hasLoadedWriteTime_ || wt > lastLoadedWriteTime_) {
		// Ensure the linker has finished writing and closed the file handle
		HANDLE hFile = CreateFileA(
			currentDllPath.string().c_str(),
			GENERIC_READ,
			FILE_SHARE_READ,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (hFile == INVALID_HANDLE_VALUE) {
			return false;
		}
		LARGE_INTEGER sz{};
		BOOL okSize = GetFileSizeEx(hFile, &sz);
		CloseHandle(hFile);
		if (!okSize || sz.QuadPart == 0) {
			return false;
		}

		dllSourcePath_ = currentDllPath;
		return true;
	}

	return false;
}

ScriptBase* ScriptModule::CreateScript(const std::string& className) const {
	if (!dllHandle_ || !fnCreateScript_ || className.empty()) {
		return nullptr;
	}
	ScriptBase* instance = fnCreateScript_(className.c_str());
	if (instance) {
		instance->SetEngineAPI(engineApi_);
	}
	return instance;
}

void ScriptModule::DestroyScript(ScriptBase* instance) const {
	if (!instance) return;
	if (dllHandle_ && fnDestroyScript_) {
		fnDestroyScript_(instance);
	}
}

bool ScriptModule::IsRegistered(const std::string& className) const {
	return std::find(registeredNames_.begin(), registeredNames_.end(), className) != registeredNames_.end();
}

int ScriptModule::GetFieldCount(ScriptBase* instance) const {
	if (!instance) return 0;
	if (dllHandle_ && fnGetFieldCount_) {
		return fnGetFieldCount_(instance);
	}
	return instance->GetExposedFieldCount();
}

bool ScriptModule::GetFieldInfo(ScriptBase* instance, int index, ScriptFieldInfo& outInfo) const {
	if (!instance) return false;
	if (dllHandle_ && fnGetFieldInfo_) {
		return fnGetFieldInfo_(instance, index, &outInfo);
	}
	if (const ScriptFieldInfo* info = instance->GetExposedField(index)) {
		outInfo = *info;
		return true;
	}
	return false;
}
