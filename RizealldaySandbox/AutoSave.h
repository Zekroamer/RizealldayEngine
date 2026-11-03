#pragma once
#include <filesystem>

// Auto-save rules (pure). The editor writes the open scene to Library/AutoSave while it has unsaved changes,
// deletes the copy when the scene is saved, and offers it back when the scene is opened and the copy is newer.
namespace AutoSave {

/// True when a copy should be written now: changes not saved, Edit mode, and `interval` seconds since the last copy
bool IsDue(double now, double lastWrite, double interval, bool dirty, bool editing);

/// <project>/Library/AutoSave/<scene file name>.autosave.json ("Untitled" for a scene never saved)
std::filesystem::path PathFor(const std::filesystem::path& projectRoot, const std::filesystem::path& scenePath);

/// The copy exists and was written after the scene file (a missing scene file counts as older)
bool IsNewerThanScene(const std::filesystem::path& copy, const std::filesystem::path& scene);

} // namespace AutoSave
