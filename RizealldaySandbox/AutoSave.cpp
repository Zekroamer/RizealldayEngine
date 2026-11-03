#include "AutoSave.h"

namespace AutoSave {

bool IsDue(double now, double lastWrite, double interval, bool dirty, bool editing)
{
	return dirty && editing && interval > 0.0 && now - lastWrite >= interval;
}

std::filesystem::path PathFor(const std::filesystem::path& projectRoot, const std::filesystem::path& scenePath)
{
	const std::filesystem::path name = scenePath.empty() ? std::filesystem::path("Untitled") : scenePath.filename();
	std::filesystem::path file = projectRoot / "Library" / "AutoSave" / name;
	file += ".autosave.json";
	return file;
}

bool IsNewerThanScene(const std::filesystem::path& copy, const std::filesystem::path& scene)
{
	std::error_code ec;
	const auto copyTime = std::filesystem::last_write_time(copy, ec);
	if (ec) return false;
	const auto sceneTime = std::filesystem::last_write_time(scene, ec);
	return ec || copyTime > sceneTime;
}

} // namespace AutoSave
