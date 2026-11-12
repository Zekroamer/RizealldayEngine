#include "AssetDatabase.h"
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <cstdio>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {
	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	bool StartsWithFolder(const std::string& key, const std::string& folderKey)
	{
		return key == folderKey || (key.size() > folderKey.size() && key.starts_with(folderKey) && key[folderKey.size()] == '/');
	}

	fs::path MetaPathOf(const fs::path& file)
	{
		fs::path meta = file;
		meta += AssetDatabase::kMetaExtension;
		return meta;
	}

	std::string ReadMetaGuid(const fs::path& metaPath)
	{
		std::ifstream in(metaPath, std::ios::binary);
		if (!in) return {};
		std::stringstream buffer;
		buffer << in.rdbuf();
		const nlohmann::json j = nlohmann::json::parse(buffer.str(), nullptr, false);
		if (!j.is_object()) return {};
		auto it = j.find("guid");
		return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
	}

	/// 32 hex digits from a project-relative path (two 64-bit FNV-1a hashes); `salt` gives another one when it is taken
	std::string PathGuid(const std::string& key, int salt)
	{
		const std::string text = key + '#' + std::to_string(salt);
		uint64_t a = 14695981039346656037ull;
		uint64_t b = 0x84222325cbf29ce4ull;
		for (unsigned char c : text) {
			a = (a ^ c) * 1099511628211ull;
			b = (b ^ c) * 1099511628211ull;
			b ^= b >> 29;
		}
		char hex[33];
		std::snprintf(hex, sizeof(hex), "%016llx%016llx", static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
		return hex;
	}

	/// A save in progress (EditorSuiteDetail::WriteFileAtomic) or one a crash left behind: never an asset
	bool IsTemporaryFile(const fs::path& path) { return path.extension() == ".tmp"; }

	bool WriteMeta(const fs::path& metaPath, const std::string& guid)
	{
		nlohmann::ordered_json j;
		j["guid"] = guid;
		j["version"] = 1;
		std::ofstream out(metaPath, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		out << j.dump(1, '\t') << '\n';
		return static_cast<bool>(out);
	}
}

void AssetDatabase::SetRoot(const fs::path& projectRoot)
{
	if (projectRoot == root_) return;
	root_ = projectRoot;
	Refresh();
}

void AssetDatabase::Refresh()
{
	guidToPath_.clear();
	keyToGuid_.clear();
	if (root_.empty()) return;

	std::error_code ec;
	const fs::path assetsDir = root_ / "Assets";
	if (!fs::is_directory(assetsDir, ec)) return;

	std::vector<std::pair<fs::path, std::string>> files;
	for (auto it = fs::recursive_directory_iterator(assetsDir, fs::directory_options::skip_permission_denied, ec);
		!ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
		// Dot files (.gitkeep) are repository plumbing, not assets
		if (!it->is_regular_file(ec) || IsMetaFile(it->path()) || IsTemporaryFile(it->path()) || it->path().filename().native().starts_with(L'.')) continue;
		const fs::path rel = it->path().lexically_relative(assetsDir);
		files.emplace_back(it->path(), "Assets/" + rel.generic_string());
	}

	// Copies of a file usually have the longer name ("foo - Copy.png"): registering short paths first lets the
	// original keep its GUID while the copy (same .meta content) is given a new one
	std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
		if (a.second.size() != b.second.size()) return a.second.size() < b.second.size();
		return a.second < b.second;
	});
	for (const auto& [abs, assetPath] : files) {
		RegisterFile(abs, assetPath);
	}
}

std::vector<std::string> AssetDatabase::AllGuids() const
{
	std::vector<std::string> guids;
	guids.reserve(guidToPath_.size());
	for (const auto& [guid, path] : guidToPath_) guids.push_back(guid);
	return guids;
}

std::string AssetDatabase::RegisterFile(const fs::path& absFile, const std::string& assetPath)
{
	const fs::path metaPath = MetaPathOf(absFile);
	std::string guid = ReadMetaGuid(metaPath);
	const bool usable = IsValidGuid(guid) && !guidToPath_.contains(guid);
	if (!usable) {
		if (readOnly_) return {};
		// From the path, not random: every teammate's editor writes the same .meta, so git sees no change to fight over
		guid.clear();
		for (int salt = 0; guid.empty() || guidToPath_.contains(guid); ++salt) guid = PathGuid(Key(assetPath), salt);
		if (!WriteMeta(metaPath, guid)) return {};
	}
	guidToPath_[guid] = assetPath;
	keyToGuid_[Key(assetPath)] = guid;
	return guid;
}

std::string AssetDatabase::GuidForPath(const std::string& path)
{
	const std::string assetPath = ToAssetPath(path);
	if (assetPath.empty()) return {};
	if (auto it = keyToGuid_.find(Key(assetPath)); it != keyToGuid_.end()) return it->second;

	std::error_code ec;
	const fs::path abs = ToAbsolute(assetPath);
	if (!fs::is_regular_file(abs, ec) || IsMetaFile(abs) || IsTemporaryFile(abs)) return {};
	return RegisterFile(abs, assetPath);
}

std::string AssetDatabase::FindGuid(const std::string& path) const
{
	const std::string assetPath = ToAssetPath(path);
	if (assetPath.empty()) return {};
	auto it = keyToGuid_.find(Key(assetPath));
	return (it != keyToGuid_.end()) ? it->second : std::string{};
}

std::string AssetDatabase::PathForGuid(const std::string& guid) const
{
	auto it = guidToPath_.find(guid);
	return (it != guidToPath_.end()) ? it->second : std::string{};
}

std::string AssetDatabase::FindByFileName(const std::string& fileName) const
{
	const std::string wanted = Lower(fileName);
	std::string best;
	for (const auto& [guid, assetPath] : guidToPath_) {
		const size_t slash = assetPath.find_last_of('/');
		const std::string name = (slash == std::string::npos) ? assetPath : assetPath.substr(slash + 1);
		// Deterministic choice when several folders hold the same name: the shortest, then alphabetical path
		if (Lower(name) == wanted && (best.empty() || assetPath.size() < best.size() || (assetPath.size() == best.size() && assetPath < best))) {
			best = assetPath;
		}
	}
	return best;
}

bool AssetDatabase::MoveAsset(const std::string& from, const std::string& to, std::string* outError)
{
	auto fail = [&](const std::string& message) {
		if (outError) *outError = message;
		return false;
	};
	if (readOnly_) return fail("The asset database is read-only.");

	const std::string src = ToAssetPath(from);
	const std::string dst = ToAssetPath(to);
	if (src.empty() || dst.empty()) return fail("Only files inside the project's Assets folder can be moved.");
	if (Key(src) == Key("Assets") || Key(dst) == Key("Assets")) return fail("The Assets folder itself cannot be moved.");
	if (src == dst) return true;
	if (StartsWithFolder(Key(dst), Key(src)) && Key(dst) != Key(src)) return fail("A folder cannot be moved into itself.");

	std::error_code ec;
	const fs::path srcAbs = ToAbsolute(src);
	const fs::path dstAbs = ToAbsolute(dst);
	if (!fs::exists(srcAbs, ec)) return fail(src + " does not exist.");
	const bool caseOnlyRename = (Key(src) == Key(dst));
	if (!caseOnlyRename && fs::exists(dstAbs, ec)) return fail(dst + " already exists.");

	const bool isDirectory = fs::is_directory(srcAbs, ec);
	if (!isDirectory) GuidForPath(src); // make sure the GUID exists before the file moves

	fs::create_directories(dstAbs.parent_path(), ec);
	fs::rename(srcAbs, dstAbs, ec);
	if (ec) return fail("Rename failed: " + ec.message());

	if (!isDirectory) {
		const fs::path srcMeta = MetaPathOf(srcAbs);
		const fs::path dstMeta = MetaPathOf(dstAbs);
		if (fs::exists(srcMeta, ec)) {
			if (!caseOnlyRename) fs::remove(dstMeta, ec); // a stale .meta without its asset
			fs::rename(srcMeta, dstMeta, ec);
			if (ec) return fail("The file moved but its .meta did not: " + ec.message());
		}
	}
	Rebase(src, dst);
	return true;
}

bool AssetDatabase::DeleteAsset(const std::string& path, std::string* outError)
{
	auto fail = [&](const std::string& message) {
		if (outError) *outError = message;
		return false;
	};
	if (readOnly_) return fail("The asset database is read-only.");
	const std::string assetPath = ToAssetPath(path);
	if (assetPath.empty() || Key(assetPath) == Key("Assets")) return fail("Only files inside the project's Assets folder can be deleted.");

	std::error_code ec;
	const fs::path abs = ToAbsolute(assetPath);
	if (!fs::exists(abs, ec)) return fail(assetPath + " does not exist.");

	// SHFileOperation takes a double-null-terminated list; FOF_ALLOWUNDO sends it to the recycle bin
	std::wstring list = abs.wstring();
	list.push_back(L'\0');
	const fs::path meta = MetaPathOf(abs);
	if (fs::exists(meta, ec)) {
		list += meta.wstring();
		list.push_back(L'\0');
	}
	list.push_back(L'\0');
	SHFILEOPSTRUCTW op{};
	op.wFunc = FO_DELETE;
	op.pFrom = list.c_str();
	op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
	if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted) return fail("Could not move " + assetPath + " to the recycle bin.");

	Forget(assetPath);
	return true;
}

std::string AssetDatabase::ToAssetPath(const std::string& path) const
{
	if (root_.empty() || path.empty()) return {};
	fs::path p(path);
	fs::path rel;
	if (p.is_absolute()) {
		rel = p.lexically_normal().lexically_relative(root_.lexically_normal());
		if (rel.empty() || rel.native().starts_with(L"..")) {
			std::error_code ec;
			rel = fs::weakly_canonical(p, ec).lexically_relative(fs::weakly_canonical(root_, ec));
		}
	} else {
		rel = p.lexically_normal();
	}
	std::string s = rel.generic_string();
	if (s.starts_with("./")) s.erase(0, 2);
	while (s.ends_with("/")) s.pop_back();
	const std::string key = Lower(s);
	if (key.find("..") != std::string::npos) return {};
	if (key != "assets" && !key.starts_with("assets/")) return {};
	return "Assets" + s.substr(6);
}

bool AssetDatabase::IsMetaFile(const fs::path& path)
{
	return Lower(path.extension().string()) == kMetaExtension;
}

bool AssetDatabase::IsValidGuid(const std::string& guid)
{
	return guid.size() == 32 && std::all_of(guid.begin(), guid.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string AssetDatabase::Key(const std::string& assetPath)
{
	return Lower(assetPath);
}

void AssetDatabase::Forget(const std::string& assetPath)
{
	const std::string folderKey = Key(assetPath);
	for (auto it = keyToGuid_.begin(); it != keyToGuid_.end();) {
		if (StartsWithFolder(it->first, folderKey)) {
			guidToPath_.erase(it->second);
			it = keyToGuid_.erase(it);
		} else {
			++it;
		}
	}
}

void AssetDatabase::Rebase(const std::string& from, const std::string& to)
{
	const std::string folderKey = Key(from);
	std::vector<std::pair<std::string, std::string>> moved; // guid, new path
	for (auto it = keyToGuid_.begin(); it != keyToGuid_.end();) {
		if (StartsWithFolder(it->first, folderKey)) {
			const std::string& oldPath = guidToPath_[it->second];
			moved.emplace_back(it->second, to + oldPath.substr(from.size()));
			it = keyToGuid_.erase(it);
		} else {
			++it;
		}
	}
	for (const auto& [guid, newPath] : moved) {
		guidToPath_[guid] = newPath;
		keyToGuid_[Key(newPath)] = guid;
	}
}
