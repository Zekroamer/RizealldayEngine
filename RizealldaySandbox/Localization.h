#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Editor UI language (Options > Language).
//
// The English text written in the code is the key. Translations live in Resources/Localization/<code>.json:
//     { "_language": "<name shown in the menu>", "File": "<translation>", "Save Scene": "<translation>", ... }
// A missing entry shows the English text, so a language file can be completed step by step without code changes.
//   Tr("Save Scene")   -> translated text (Text, tooltips, menu items whose ID does not matter)
//   TrId("Inspector")  -> "<translation>###Inspector": the ImGui ID stays the English text in every language,
//                         so windows, tabs, popups and imgui.ini settings do not change when the language does.
namespace Localization {

/// What a person sees of an ImGui label: "Label##id" and "Translated###English" show only the part before ##
inline std::string_view VisibleLabel(std::string_view label)
{
	return label.substr(0, label.find("##"));
}

struct LanguageInfo {
	std::string code;        // file name without extension ("ja"); "en" = built-in English
	std::string displayName; // "_language" of the file
};

/// Folder that holds the <code>.json files (default Resources/Localization, relative to the working directory)
void SetLanguageDirectory(const std::filesystem::path& directory);
const std::filesystem::path& GetLanguageDirectory();

/// English first, then every language file found in the language directory (rescanned on each call)
std::vector<LanguageInfo> AvailableLanguages();
/// Loads `code` ("en" or a file in the language directory). Returns false and keeps the current language on failure.
bool SetLanguage(const std::string& code);
const std::string& CurrentLanguage();
size_t TranslationCount();

const char* Tr(const char* english);
const char* TrId(const char* english);

/// Remembered choice: %LOCALAPPDATA%/RizealldayEngine/editor_settings.json ("language")
void LoadPreference();
void SavePreference();
/// Other editor preferences in the same file (e.g. "autoSave"); other keys in it are kept
bool EditorSettingBool(const char* key, bool fallback);
void SetEditorSettingBool(const char* key, bool value);
/// Self-tests: use this file instead of %LOCALAPPDATA%; empty = back to the real one
void SetSettingsFileOverride(const std::filesystem::path& file);

} // namespace Localization
