#include "EditorSuiteInternal.h"

void EditorSuite::DrawMapchipInspectorSection()
{
#ifdef USE_IMGUI
	if (DrawComponentHeader("Mapchip Level Editor", ComponentKind::Mapchip)) {
		ImGui::Checkbox(TrId("Render 3D Blocks in Viewport"), &renderMapchip3DBlocks_);
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
		if (ImGui::Button(TrId("Save Mapchip CSV"), ImVec2(140.0f, 24.0f))) {
			SaveMapchipCSV(mapFilePathBuf_);
		}
		ImGui::SameLine();
		if (ImGui::Button(TrId("Load Mapchip CSV"), ImVec2(140.0f, 24.0f))) {
			LoadMapchipCSV(mapFilePathBuf_);
		}
	}
#endif
}

bool EditorSuite::SaveMapchipCSV(const std::string& filePath)
{
	std::filesystem::path p(filePath);
	if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());

	std::ofstream file(filePath);
	if (!file.is_open()) return false;

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
	if (!file.is_open()) return false;

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
		mapCols_ = static_cast<int>(newGrid[0].size());
		mapGrid_ = newGrid;
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

