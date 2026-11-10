#include "EditorSuiteInternal.h"

void EditorSuite::DrawMapchipInspectorSection()
{
#ifdef USE_IMGUI
	if (DrawComponentHeader("Mapchip Level Editor", ComponentKind::Mapchip)) {
		ImGui::Text(Tr("Palette:"));
		const char* tileNames[] = { "0: Empty", "1: Wall Block", "2: Spike", "3: Spawn", "4: Goal" };
		for (int t = 0; t < 5; ++t) {
			ImGui::RadioButton(tileNames[t], &selectedBrushTile_, t);
			if (t < 4) ImGui::SameLine();
		}

		ImGui::Spacing();
		ImGui::Text(Tr("Level Grid (15x10):"));

		float cellW = 18.0f;
		float cellH = 18.0f;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 startPos = ImGui::GetCursorScreenPos();

		for (int r = 0; r < mapRows_; ++r) {
			for (int c = 0; c < mapCols_; ++c) {
				ImVec2 pMin = ImVec2(startPos.x + c * (cellW + 2.0f), startPos.y + r * (cellH + 2.0f));
				ImVec2 pMax = ImVec2(pMin.x + cellW, pMin.y + cellH);

				int tile = mapGrid_[r][c];
				ImU32 col = IM_COL32(40, 40, 45, 255);
				if (tile == 1) col = IM_COL32(180, 120, 70, 255); // Wall
				else if (tile == 2) col = IM_COL32(220, 60, 60, 255); // Spike
				else if (tile == 3) col = IM_COL32(60, 180, 220, 255); // Spawn
				else if (tile == 4) col = IM_COL32(60, 220, 100, 255); // Goal

				drawList->AddRectFilled(pMin, pMax, col, 2.0f);

				ImGui::SetCursorScreenPos(pMin);
				if (ImGui::InvisibleButton(std::format("##Tile_{}_{}", r, c).c_str(), ImVec2(cellW, cellH))) {
					mapGrid_[r][c] = selectedBrushTile_;
					isSceneDirty_ = true;
					PushUndoSnapshot();
				}
			}
		}

		ImGui::SetCursorScreenPos(ImVec2(startPos.x, startPos.y + mapRows_ * (cellH + 2.0f) + 8.0f));
		if (ImGui::Button(TrId("Build Level Objects"), ImVec2(-1.0f, 26.0f))) {
			if (selectedObjectIndex_ >= 0) BuildMapchipObjects(gameObjects_[selectedObjectIndex_].id);
		}
		ImGui::SetItemTooltip("%s", Tr("Creates one child object per tile: Wall = static cube, Spike = trigger cube,\n"
			"Spawn / Goal = empty markers. Each gets the tile name as its tag. Building again replaces them."));

		ImGui::Spacing();
		ImGui::SetNextItemWidth(-1.0f);
		const std::string defaultCsv = ToProjectRelativePath(MapchipCsvPath());
		ImGui::InputTextWithHint("##MapchipCsv", defaultCsv.c_str(), mapFilePathBuf_, sizeof(mapFilePathBuf_));
		ImGui::SetItemTooltip("%s", Tr("CSV file of this grid. Empty = Assets/Levels/<scene name>.csv"));
		if (ImGui::Button(TrId("Save Mapchip CSV"), ImVec2(140.0f, 24.0f))) {
			SaveMapchipCSV(MapchipCsvPath().string());
		}
		ImGui::SameLine();
		if (ImGui::Button(TrId("Load Mapchip CSV"), ImVec2(140.0f, 24.0f))) {
			LoadMapchipCSV(MapchipCsvPath().string());
		}
	}
#endif
}

namespace {
	constexpr std::string_view kBuiltPrefix = "[Mapchip] "; // names of the objects Build made, so a rebuild finds them
	struct MapchipTile { const char* name; Vector4 color; };
	// Index = tile id (0 = Empty makes nothing). The name is also the tag scripts can look for.
	const MapchipTile kMapchipTiles[] = {
		{ nullptr, {} },
		{ "Wall", { 0.71f, 0.47f, 0.27f, 1.0f } },
		{ "Spike", { 0.86f, 0.24f, 0.24f, 1.0f } },
		{ "Spawn", { 0.24f, 0.71f, 0.86f, 1.0f } },
		{ "Goal", { 0.24f, 0.86f, 0.39f, 1.0f } },
	};
}

std::filesystem::path EditorSuite::MapchipCsvPath() const
{
	std::filesystem::path path = mapFilePathBuf_[0] != '\0' ? std::filesystem::path(mapFilePathBuf_)
		: currentProjectRoot_.empty() ? std::filesystem::path("Resources/levels/stage01.csv")
		: std::filesystem::path("Assets/Levels") / (currentSceneName_ + ".csv");
	if (path.is_relative() && !currentProjectRoot_.empty()) path = currentProjectRoot_ / path;
	return path;
}

bool EditorSuite::BuildMapchipObjects(uint32_t mapId)
{
	if (sceneState_ != EditorSceneState::Edit) {
		LogMessage("[Mapchip] Stop Play mode before building level objects.", 1);
		return false;
	}
	if (!FindGameObjectById(mapId, false)) return false;

	// The previous build goes; the user's own children of the map object stay
	std::erase_if(gameObjects_, [&](const GameObject& o) { return o.parentId == static_cast<int>(mapId) && o.name.starts_with(kBuiltPrefix); });

	int made = 0;
	const bool wasSpawning = isSpawningFromScript_;
	isSpawningFromScript_ = true; // no per-cube log line, selection change or undo step: one of each for the whole build below
	for (int r = 0; r < mapRows_; ++r) {
		for (int c = 0; c < mapCols_; ++c) {
			const int tile = mapGrid_[r][c];
			if (tile <= 0 || tile >= static_cast<int>(std::size(kMapchipTiles))) continue;
			const MapchipTile& look = kMapchipTiles[tile];
			const std::string name = std::format("{}{} ({},{})", kBuiltPrefix, look.name, c, r);
			// Row 0 is the top of the level: X to the right, Y up, one tile = mapTileSize_ in the map object's space
			const Vector3 pos = { c * mapTileSize_, (mapRows_ - 1 - r) * mapTileSize_, 0.0f };
			GameObject& o = (tile == 1 || tile == 2) ? CreateCube(name, pos) : CreateGameObject(name, GameObjectType::Empty);
			o.translation = pos;
			o.scale = { mapTileSize_, mapTileSize_, mapTileSize_ };
			o.tag = look.name;
			o.parentId = static_cast<int>(mapId);
			if (o.hasMesh) o.materialColor = look.color;
			o.isStatic = tile == 1;  // walls never move: static bodies, and NavGrid treats them as blockers
			o.isTrigger = tile == 2; // spikes report OnCollisionEnter without blocking
			++made;
		}
	}
	isSpawningFromScript_ = wasSpawning;
	RebuildHierarchyLinks();
	selectedObjectIndex_ = FindGameObjectIndexById(mapId);
	extraSelection_.clear();
	isSceneDirty_ = true;
	undoPushPending_ = true; // one undo step for the whole rebuild
	LogMessage(std::format("[Mapchip] Built {} level object(s) under '{}'.", made, FindGameObjectById(mapId, false)->name), 0);
	return true;
}

void EditorSuite::ResetMapchip()
{
	mapRows_ = 10;
	mapCols_ = 15;
	mapTileSize_ = 1.0f;
	mapGrid_.assign(mapRows_, std::vector<int>(mapCols_, 0));
	for (int c = 0; c < mapCols_; ++c) mapGrid_[mapRows_ - 1][c] = 1; // floor
}

bool EditorSuite::SaveMapchipCSV(const std::string& filePath)
{
	std::filesystem::path p(filePath);
	std::error_code ec;
	if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);

	std::ofstream file(filePath);
	if (!file.is_open()) {
		LogMessage(std::format("[Mapchip] Cannot write {}", filePath), 2);
		return false;
	}

	for (int r = 0; r < mapRows_; ++r) {
		for (int c = 0; c < mapCols_; ++c) {
			file << mapGrid_[r][c];
			if (c < mapCols_ - 1) file << ",";
		}
		file << "\n";
	}
	file.close();
	LogMessage(std::format("[Mapchip] Saved: {}", filePath), 0);
	return true;
}

bool EditorSuite::LoadMapchipCSV(const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file.is_open()) {
		LogMessage(std::format("[Mapchip] Cannot open {}", filePath), 2);
		return false;
	}

	std::vector<std::vector<int>> newGrid;
	std::string line;
	while (std::getline(file, line)) {
		if (line.empty()) continue;
		std::vector<int> row;
		std::stringstream ss(line);
		std::string val;
		while (std::getline(ss, val, ',')) {
			try { row.push_back(std::stoi(val)); } catch (...) { row.push_back(0); }
		}
		newGrid.push_back(row);
	}
	file.close();

	if (!newGrid.empty()) {
		mapRows_ = static_cast<int>(newGrid.size());
		mapCols_ = 0;
		for (const auto& row : newGrid) mapCols_ = (std::max)(mapCols_, static_cast<int>(row.size()));
		for (auto& row : newGrid) row.resize(mapCols_, 0); // short rows (a hand-edited CSV) end in Empty tiles
		mapGrid_ = std::move(newGrid);
		LogMessage(std::format("[Mapchip] Loaded ({}x{}): {}", mapCols_, mapRows_, filePath), 0);
		return true;
	}
	return false;
}

bool EditorSuite::ExportMapchipJSON(const std::string& filePath)
{
	std::filesystem::path p(filePath);
	if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());

	std::ofstream file(filePath);
	if (!file.is_open()) return false;

	file << "{\n";
	file << "  \"cols\": " << mapCols_ << ",\n";
	file << "  \"rows\": " << mapRows_ << ",\n";
	file << "  \"tileSize\": " << mapTileSize_ << ",\n";
	file << "  \"data\": [\n";
	for (int r = 0; r < mapRows_; ++r) {
		file << "    [";
		for (int c = 0; c < mapCols_; ++c) {
			file << mapGrid_[r][c];
			if (c < mapCols_ - 1) file << ", ";
		}
		file << "]";
		if (r < mapRows_ - 1) file << ",";
		file << "\n";
	}
	file << "  ]\n";
	file << "}\n";
	file.close();
	LogMessage(std::format("[Mapchip] Exported JSON: {}", filePath), 0);
	return true;
}

