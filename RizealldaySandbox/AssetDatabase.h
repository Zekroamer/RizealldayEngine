#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// Asset database (roadmap 4.2).
//
// Every file under <project>/Assets gets a "<file>.meta" next to it holding a stable GUID:
//     { "guid": "3f2a...32 hex...", "version": 1 }
// Scenes and prefabs store the GUID next to the old path ("textureGuid" next to "texturePath"), so an asset
// renamed or moved inside the editor (or moved together with its .meta in Explorer) is still found.
// Paths handed in and out are project-relative with forward slashes ("Assets/Textures/foo.png").
// Files outside Assets/ (engine Resources/, absolute paths elsewhere) are not managed and have no GUID.
class AssetDatabase {
public:
	static constexpr const char* kMetaExtension = ".meta";

	/// Points the database at a project root and rescans when it changed (empty root = no project)
	void SetRoot(const std::filesystem::path& projectRoot);
	const std::filesystem::path& Root() const { return root_; }

	/// Read-only databases never write .meta files (exported games): unknown assets simply have no GUID
	void SetReadOnly(bool readOnly) { readOnly_ = readOnly; }
	bool IsReadOnly() const { return readOnly_; }

	/// Rescans Assets/: registers every .meta, creates missing ones, gives duplicated GUIDs (copied .meta) a new one
	void Refresh();

	/// GUID of an asset, registering it (writing its .meta) on first use. "" when the path is not a managed asset.
	std::string GuidForPath(const std::string& path);
	/// GUID of an already registered asset ("" otherwise). Never touches the disk.
	std::string FindGuid(const std::string& path) const;
	/// Current project-relative path of a GUID, "" when unknown
	std::string PathForGuid(const std::string& guid) const;
	/// Project-relative path of the first asset whose file name matches (case-insensitive), "" when none
	std::string FindByFileName(const std::string& fileName) const;

	/// Moves / renames a file or folder inside Assets/ together with its .meta files. Paths are project-relative.
	bool MoveAsset(const std::string& from, const std::string& to, std::string* outError = nullptr);
	/// Sends a file or folder (and its .meta) to the recycle bin
	bool DeleteAsset(const std::string& path, std::string* outError = nullptr);

	/// "Assets/..." form of a path (absolute or project-relative), or "" when it is outside the project's Assets/
	std::string ToAssetPath(const std::string& path) const;
	std::filesystem::path ToAbsolute(const std::string& assetPath) const { return root_ / std::filesystem::path(assetPath); }

	size_t Count() const { return guidToPath_.size(); }
	/// GUIDs of every registered asset, in no particular order
	std::vector<std::string> AllGuids() const;
	static bool IsMetaFile(const std::filesystem::path& path);
	static bool IsValidGuid(const std::string& guid);
	static std::string NewGuid();

private:
	static std::string Key(const std::string& assetPath); // case-insensitive lookup key
	std::string RegisterFile(const std::filesystem::path& absFile, const std::string& assetPath);
	void Forget(const std::string& assetPath);
	void Rebase(const std::string& from, const std::string& to);

	std::filesystem::path root_;
	bool readOnly_ = false;
	std::unordered_map<std::string, std::string> guidToPath_; // guid -> "Assets/..."
	std::unordered_map<std::string, std::string> keyToGuid_;  // lower-case "assets/..." -> guid
};
