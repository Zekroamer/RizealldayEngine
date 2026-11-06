#pragma once
#include "base/ScriptAPI.h"
#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>

/// Manages loading, shadow-copying, and hot-reloading of bin/Scripts/GameScripts.dll.
class ScriptModule {
public:
	using FnGameScripts_Init    = void (*)(const EngineAPI*);
	using FnCreateScript        = ScriptBase* (*)(const char*);
	using FnDestroyScript       = void (*)(ScriptBase*);
	using FnGetScriptCount      = int (*)();
	using FnGetScriptName       = const char* (*)(int);
	using FnGetScriptFieldCount = int (*)(ScriptBase*);
	using FnGetScriptFieldInfo  = bool (*)(ScriptBase*, int, ScriptFieldInfo*);
	using FnGetScriptFieldName  = const char* (*)(ScriptBase*, int);
	using FnGetScriptFieldType  = int (*)(ScriptBase*, int);
	using FnGetScriptFieldData  = void* (*)(ScriptBase*, int, uint32_t*);

	ScriptModule() = default;
	~ScriptModule();

	ScriptModule(const ScriptModule&) = delete;
	ScriptModule& operator=(const ScriptModule&) = delete;

	/// Locate paths, delete old shadow copies, and perform initial load if GameScripts.dll exists.
	bool Initialize(const EngineAPI* engineApi);

	/// Unload the currently loaded shadow DLL. All script instances must be destroyed before calling this.
	void Unload();

	/// Shadow-copy bin/Scripts/GameScripts.dll to bin/Scripts/Shadow/GameScripts_<n>.dll and load it.
	bool LoadShadowCopy(std::string* outErrorMsg = nullptr);

	/// Poll bin/Scripts/GameScripts.dll timestamp every 0.5 seconds. Returns true if a newer unlocked DLL is ready.
	bool PollDllChanged(float deltaTime, bool isBuildInProgress);

	/// Create a script instance inside the DLL via exported CreateScript(name).
	ScriptBase* CreateScript(const std::string& className) const;

	/// Destroy a script instance inside the DLL via exported DestroyScript(ptr).
	void DestroyScript(ScriptBase* instance) const;

	/// Query whether a script class name is registered in the currently loaded DLL.
	bool IsRegistered(const std::string& className) const;

	/// Return all registered script class names from the currently loaded DLL.
	const std::vector<std::string>& GetRegisteredNames() const { return registeredNames_; }

	/// Query exposed fields on a live script instance via DLL exports.
	int GetFieldCount(ScriptBase* instance) const;
	bool GetFieldInfo(ScriptBase* instance, int index, ScriptFieldInfo& outInfo) const;

	bool IsLoaded() const { return dllHandle_ != nullptr; }
	/// Why the startup load failed (empty when it succeeded or no DLL exists yet)
	const std::string& GetLastLoadError() const { return lastLoadError_; }
	/// The last LoadShadowCopy refused the DLL because it was built against another ScriptAPI.h (stale objects: rebuild all)
	bool LastLoadWasApiMismatch() const { return lastLoadApiMismatch_; }
	uint32_t GetReloadCount() const { return shadowGeneration_; }
	const std::filesystem::path& GetDllSourcePath() const { return dllSourcePath_; }
	const std::filesystem::path& GetActiveShadowPath() const { return activeShadowPath_; }

	static constexpr const char* kScriptsDllSubpath = "bin/Scripts/GameScripts.dll";
	static constexpr const char* kGameScriptsDirName = "GameScripts";
	static constexpr const char* kGameScriptsVcxprojSubpath = "GameScripts/GameScripts.vcxproj";

	/// Resolve the workspace/solution root directory containing GameScripts/GameScripts.vcxproj, or fallback to current/exe directory.
	static std::filesystem::path ResolveWorkspaceRoot();

	/// The DLL to load: the one set with SetDllPath (a project's scripts), else <workspace>/bin/Scripts/GameScripts.dll.
	std::filesystem::path ResolveDllPath() const;

	/// Switches to another GameScripts.dll (empty = workspace default). The next PollDllChanged reports it as changed.
	void SetDllPath(const std::filesystem::path& dllPath);
	const std::filesystem::path& GetDllPathOverride() const { return dllPathOverride_; }

private:
	void CleanupOldShadowCopies(const std::filesystem::path& shadowDir);

	HMODULE dllHandle_ = nullptr;
	const EngineAPI* engineApi_ = nullptr;

	FnGameScripts_Init    fnInit_           = nullptr;
	FnCreateScript        fnCreateScript_   = nullptr;
	FnDestroyScript       fnDestroyScript_  = nullptr;
	FnGetScriptCount      fnGetScriptCount_ = nullptr;
	FnGetScriptName       fnGetScriptName_  = nullptr;
	FnGetScriptFieldCount fnGetFieldCount_  = nullptr;
	FnGetScriptFieldInfo  fnGetFieldInfo_   = nullptr;
	FnGetScriptFieldName  fnGetFieldName_   = nullptr;
	FnGetScriptFieldType  fnGetFieldType_   = nullptr;
	FnGetScriptFieldData  fnGetFieldData_   = nullptr;

	std::vector<std::string> registeredNames_;
	std::string lastLoadError_;
	bool lastLoadApiMismatch_ = false;
	std::filesystem::path dllSourcePath_;
	std::filesystem::path dllPathOverride_;
	std::filesystem::path activeShadowPath_;
	std::filesystem::file_time_type lastLoadedWriteTime_{};
	bool hasLoadedWriteTime_ = false;
	uint32_t shadowGeneration_ = 0;
	float pollTimer_ = 0.0f;
};
