#include "base/DXCManager.h"
#include "base/Log.h"
#include "base/ConvertString.h"
#include <filesystem>
#include <vector>

void DXCManager::Initialize()
{
	HRESULT hr;
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));

	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));
}

Microsoft::WRL::ComPtr<IDxcBlob> DXCManager::CompileShader(const std::wstring& filePath, const wchar_t* profile, IDxcUtils* dxcUtils, IDxcCompiler3* dxcCompiler, IDxcIncludeHandler* includeHandler)
{
	ConvertStrings converter;
	Log* logger = Log::GetInstance();

	logger->logOutput(std::format("Begin Compiling Shader, path:{}, profile:{}\n", converter.ConvertString(filePath), converter.ConvertString(profile)));

	// Resolve path: try several locations
	std::wstring resolvedPath = filePath;
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(resolvedPath.c_str(), nullptr, &shaderSource);

	if (FAILED(hr))
	{
		// Try next to executable
		wchar_t exePath[MAX_PATH];
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		std::filesystem::path pExe(exePath);
		std::filesystem::path candidate = pExe.parent_path() / filePath;
		if (std::filesystem::exists(candidate))
		{
			resolvedPath = candidate.wstring();
			hr = dxcUtils->LoadFile(resolvedPath.c_str(), nullptr, &shaderSource);
		}
	}

	if (FAILED(hr))
	{
		// Try parent directory of executable (e.g. from x64/Debug/ up to project)
		wchar_t exePath[MAX_PATH];
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		std::filesystem::path pExe(exePath);
		std::filesystem::path candidate = pExe.parent_path().parent_path() / L"RizealldaySandbox" / filePath;
		if (std::filesystem::exists(candidate))
		{
			resolvedPath = candidate.wstring();
			hr = dxcUtils->LoadFile(resolvedPath.c_str(), nullptr, &shaderSource);
		}
	}

	if (FAILED(hr))
	{
		// Try engine shader directory
		wchar_t exePath[MAX_PATH];
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		std::filesystem::path pExe(exePath);
		std::filesystem::path candidate = pExe.parent_path().parent_path() / L"RizealldayEngine" / filePath;
		if (std::filesystem::exists(candidate))
		{
			resolvedPath = candidate.wstring();
			hr = dxcUtils->LoadFile(resolvedPath.c_str(), nullptr, &shaderSource);
		}
	}

	if (FAILED(hr))
	{
		std::string errMsg = std::format("Failed to load shader file: {}\nCould not locate file in working directory, executable directory, or engine directory.\n", converter.ConvertString(filePath));
		logger->logOutput(errMsg);
		OutputDebugStringA(errMsg.c_str());
		MessageBoxA(nullptr, errMsg.c_str(), "Shader Load Error", MB_OK | MB_ICONERROR);
		assert(SUCCEEDED(hr));
		return nullptr;
	}

	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;

	// Extract directory for include resolving
	std::filesystem::path shaderPath(resolvedPath);
	std::wstring includeDir = shaderPath.parent_path().wstring();

	// Compile arguments
	std::vector<LPCWSTR> arguments;
	arguments.push_back(resolvedPath.c_str());
	arguments.push_back(L"-E"); arguments.push_back(L"main");
	arguments.push_back(L"-T"); arguments.push_back(profile);
	arguments.push_back(L"-I"); arguments.push_back(includeDir.c_str());
	arguments.push_back(L"-Zi"); arguments.push_back(L"-Qembed_debug");
	arguments.push_back(L"-Od");
	arguments.push_back(L"-Zpr");

	Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer,
		arguments.data(),
		static_cast<UINT32>(arguments.size()),
		includeHandler,
		IID_PPV_ARGS(&shaderResult));
	assert(SUCCEEDED(hr));

	// Check Errors
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> errorMessages = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errorMessages), nullptr);
	if (errorMessages != nullptr && errorMessages->GetStringLength() != 0)
	{
		logger->logOutput(std::format("Shader Compile Error: {}\n", errorMessages->GetStringPointer()));
		assert(false);
	}

	// Get Compiled Shader
	Microsoft::WRL::ComPtr<IDxcBlob> compiledShader = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&compiledShader), nullptr);
	assert(SUCCEEDED(hr));
	logger->logOutput(std::format("Shader Compile Succeeded, Path: {}, profile: {}\n", converter.ConvertString(resolvedPath), converter.ConvertString(profile)));

	return compiledShader;
}

Microsoft::WRL::ComPtr<IDxcBlob> DXCManager::TryCompileShader(const std::wstring& filePath, const wchar_t* profile, std::string* error)
{
	ConvertStrings converter;
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> source;
	if (FAILED(dxcUtils->LoadFile(filePath.c_str(), nullptr, &source)) || !source) {
		if (error) *error = "Cannot read " + converter.ConvertString(filePath);
		return nullptr;
	}
	DxcBuffer buffer{ source->GetBufferPointer(), source->GetBufferSize(), DXC_CP_UTF8 };
	const std::wstring includeDir = std::filesystem::path(filePath).parent_path().wstring();
	// Same arguments as CompileShader
	// Second include folder: screen effects in Assets/ include the engine's Post.hlsli
	LPCWSTR arguments[] = { filePath.c_str(), L"-E", L"main", L"-T", profile, L"-I", includeDir.c_str(), L"-I", L"shaders", L"-Zi", L"-Qembed_debug", L"-Od", L"-Zpr" };
	Microsoft::WRL::ComPtr<IDxcResult> result;
	HRESULT status = E_FAIL;
	if (FAILED(dxcCompiler->Compile(&buffer, arguments, static_cast<UINT32>(std::size(arguments)), includeHandler.Get(), IID_PPV_ARGS(&result))) ||
		!result || FAILED(result->GetStatus(&status)) || FAILED(status)) {
		Microsoft::WRL::ComPtr<IDxcBlobUtf8> messages;
		if (result) result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&messages), nullptr);
		if (error) *error = (messages && messages->GetStringLength() > 0) ? std::string(messages->GetStringPointer()) : "Compile failed: " + converter.ConvertString(filePath);
		return nullptr;
	}
	Microsoft::WRL::ComPtr<IDxcBlob> compiled;
	result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&compiled), nullptr);
	if (!compiled && error) *error = "No shader object produced for " + converter.ConvertString(filePath);
	return compiled;
}
