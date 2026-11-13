#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Scene merge (pure): combines two people's changes to the same scene or prefab file object by object
// (matched by id) and field by field, instead of line by line. No editor, GPU or GameObject knowledge.
namespace SceneMerge {

/// Highest "formatVersion" (scenes) / "prefabVersion" (prefabs) this editor understands; newer files are refused
constexpr int kKnownFormatVersion = 1;

/// The same field of the same object changed to different values on both sides. Ours is kept in the result.
struct Conflict {
	uint32_t objectId = 0;  ///< 0 = a scene-level setting (skybox, postProcess, mapchip, sceneName, ...)
	std::string objectName;
	std::string field;      ///< "/"-separated path inside the object or the file root: "translation",
	                        ///< "components/Animator/speed", "postProcess/fogColor", "mapchip/data/3/5"
	std::string ours;       ///< Compact JSON text of each value; empty = the key is missing on that side
	std::string theirs;
};

enum class NoteKind {
	KeptDeleted, ///< deleted on one side, changed on the other: kept with the changes
	MovedToRoot, ///< its parent was deleted: now a root object
	IdChanged,   ///< both sides added a different object with this id: theirs got a new id
};

struct Note {
	NoteKind kind = NoteKind::KeptDeleted;
	uint32_t objectId = 0; ///< the id in the merged file
	std::string objectName;
	std::string message;   ///< English sentence for the Console / report
};

struct Result {
	bool ok = false;
	std::string error;      ///< why the merge was refused (malformed input, newer format, ...)
	std::string mergedText; ///< merged file, written like the editor writes scenes
	std::vector<Conflict> conflicts;
	std::vector<Note> notes;
};

/// Three-way merge of scene or prefab JSON texts. An empty `base` means "no common ancestor".
Result Merge(const std::string& base, const std::string& ours, const std::string& theirs);

/// Merge report (`<file>.merge.json`): {"version": 1, "conflicts": [...], "notes": [...]}; values are embedded JSON
std::string ReportToJson(const Result& result);
/// Reads a report back; false (with `error`) when the text is not a report
bool ReportFromJson(const std::string& text, std::vector<Conflict>& conflicts, std::vector<Note>& notes, std::string* error = nullptr);

} // namespace SceneMerge
