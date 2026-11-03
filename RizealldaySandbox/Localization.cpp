#include "Localization.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {
	struct State {
		fs::path directory = fs::path("Resources") / "Localization";
		std::string language = "en";
		std::unordered_map<std::string, std::string> table;  // English -> translation
		std::unordered_map<std::string, std::string> idCache; // English -> "translation###English"
	};

	State& S()
	{
		static State state;
		return state;
	}

	std::optional<nlohmann::json> ReadJson(const fs::path& file)
	{
		std::ifstream in(file, std::ios::binary);
		if (!in) return std::nullopt;
		std::stringstream buffer;
		buffer << in.rdbuf();
		nlohmann::json j = nlohmann::json::parse(buffer.str(), nullptr, false);
		if (!j.is_object()) return std::nullopt;
		return j;
	}

	fs::path& SettingsOverride()
	{
		static fs::path path;
		return path;
	}

	fs::path SettingsFilePath()
	{
		if (!SettingsOverride().empty()) return SettingsOverride();
		char* localAppData = nullptr;
		size_t len = 0;
		fs::path base;
		if (_dupenv_s(&localAppData, &len, "LOCALAPPDATA") == 0 && localAppData) {
			base = localAppData;
		}
		free(localAppData);
		if (base.empty()) return {};
		return base / "RizealldayEngine" / "editor_settings.json";
	}
}

namespace Localization {

void SetLanguageDirectory(const fs::path& directory)
{
	S().directory = directory;
}

const fs::path& GetLanguageDirectory()
{
	return S().directory;
}

std::vector<LanguageInfo> AvailableLanguages()
{
	std::vector<LanguageInfo> result{ { "en", "English" } };
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(S().directory, ec)) {
		if (!entry.is_regular_file(ec) || entry.path().extension() != ".json") continue;
		const std::string code = entry.path().stem().string();
		if (code == "en") continue;
		std::string name = code;
		if (const auto j = ReadJson(entry.path())) {
			if (auto it = j->find("_language"); it != j->end() && it->is_string()) name = it->get<std::string>();
		}
		result.push_back({ code, name });
	}
	return result;
}

bool SetLanguage(const std::string& code)
{
	State& s = S();
	if (code.empty() || code == "en") {
		s.language = "en";
		s.table.clear();
		s.idCache.clear();
		return true;
	}
	const auto j = ReadJson(s.directory / (code + ".json"));
	if (!j) return false;
	std::unordered_map<std::string, std::string> table;
	for (auto it = j->begin(); it != j->end(); ++it) {
		if (it.key().starts_with('_') || !it->is_string()) continue;
		const std::string value = it->get<std::string>();
		if (!value.empty()) table.emplace(it.key(), value);
	}
	s.language = code;
	s.table = std::move(table);
	s.idCache.clear();
	return true;
}

const std::string& CurrentLanguage()
{
	return S().language;
}

size_t TranslationCount()
{
	return S().table.size();
}

const char* Tr(const char* english)
{
	if (!english) return "";
	const State& s = S();
	if (s.table.empty()) return english;
	auto it = s.table.find(english);
	return (it != s.table.end()) ? it->second.c_str() : english;
}

const char* TrId(const char* english)
{
	if (!english) return "";
	State& s = S();
	if (s.table.empty()) return english; // English: labels (and IDs) exactly as written
	auto cached = s.idCache.find(english);
	if (cached != s.idCache.end()) return cached->second.c_str();

	// "Label##suffix": translate the visible part, keep the whole English text as the ID
	const std::string_view text(english);
	const size_t hashes = text.find("##");
	const std::string visible(text.substr(0, hashes));
	const std::string id = (hashes != std::string_view::npos && text.compare(hashes, 3, "###") == 0)
		? std::string(text.substr(hashes)) // already "###id"
		: "###" + std::string(text);
	auto translated = s.table.find(visible);
	if (translated == s.table.end()) return english;
	return s.idCache.emplace(english, translated->second + id).first->second.c_str();
}

void LoadPreference()
{
	const fs::path file = SettingsFilePath();
	if (file.empty()) return;
	const auto j = ReadJson(file);
	if (!j) return;
	if (auto it = j->find("language"); it != j->end() && it->is_string()) {
		SetLanguage(it->get<std::string>());
	}
}

bool EditorSettingBool(const char* key, bool fallback)
{
	const fs::path file = SettingsFilePath();
	if (file.empty() || !key) return fallback;
	const auto j = ReadJson(file);
	if (!j) return fallback;
	const auto it = j->find(key);
	return (it != j->end() && it->is_boolean()) ? it->get<bool>() : fallback;
}

void SetEditorSettingBool(const char* key, bool value)
{
	const fs::path file = SettingsFilePath();
	if (file.empty() || !key) return;
	nlohmann::json j = ReadJson(file).value_or(nlohmann::json::object()); // keep the other settings
	j[key] = value;
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	if (out) out << j.dump(1, '\t') << '\n';
}

void SetSettingsFileOverride(const std::filesystem::path& file)
{
	SettingsOverride() = file;
}

void SavePreference()
{
	const fs::path file = SettingsFilePath();
	if (file.empty()) return;
	nlohmann::json j = ReadJson(file).value_or(nlohmann::json::object()); // keep settings written by others
	j["language"] = S().language;
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	if (out) out << j.dump(1, '\t') << '\n';
}

} // namespace Localization
