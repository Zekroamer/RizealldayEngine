#pragma once
#include <Windows.h>
#include <dxcapi.h>
#include <cassert>
#include <string>
#include <wrl.h>
#pragma comment(lib, "dxcompiler.lib")
class DXCManager
{
public:
	void Initialize();
	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
		const std::wstring& filePath,
		const wchar_t* profile,
		IDxcUtils* dxcUtils,
		IDxcCompiler3* dxcCompiler,
		IDxcIncludeHandler* includeHandler);
	/// Like CompileShader, but never asserts or shows a message box (shader hot reload):
	/// loads exactly `filePath`, returns nullptr and the compiler output in `error` on failure.
	Microsoft::WRL::ComPtr<IDxcBlob> TryCompileShader(const std::wstring& filePath, const wchar_t* profile, std::string* error);
	/// Compiles HLSL text made at runtime (node graphs). `name` appears in error messages; includes resolve from shaders/.
	Microsoft::WRL::ComPtr<IDxcBlob> TryCompileShaderSource(const std::string& source, const std::wstring& name, const wchar_t* profile, std::string* error,
		const std::wstring& includeDir = L"shaders");
	IDxcUtils* GetDxcUtils() const { return dxcUtils.Get(); }
	IDxcCompiler3* GetDxcCompiler() const { return dxcCompiler.Get(); }
	IDxcIncludeHandler* GetIncludeHandler() const { return includeHandler.Get(); }



private:
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils = nullptr;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler = nullptr;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler = nullptr;
};

