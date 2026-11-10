#include "EditorSuiteInternal.h"

void EditorSuite::DrawMapchipInspectorSection()
{
#ifdef USE_IMGUI
	if (DrawComponentHeader("Mapchip Level Editor", ComponentKind::Mapchip)) {
		ImGui::TextUnformatted(Tr("Palette:"));
		ImGui::SameLine();
		ImGui::TextDisabled("%s", mapTileSetPath_.empty() ? Tr("Built-in tiles") : mapTileSetPath_.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton(TrId("Edit Tile Set..."))) showTileSetWindow_ = true;

		// One swatch per kind, wrapping to the next line when the Inspector is narrow
		const float rightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
		bool first = true;
		auto swatch = [&](int id, const char* label, ImVec4 color) {
			const float width = 18.0f + ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
			if (!first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width < rightEdge) ImGui::SameLine();
			first = false;
			ImGui::PushID(id);
			ImGui::BeginGroup();
			if (ImGui::ColorButton("##swatch", color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(14.0f, 14.0f))) selectedBrushTile_ = id;
			ImGui::SameLine(0.0f, 4.0f);
			if (ImGui::Selectable(label, selectedBrushTile_ == id, 0, ImGui::CalcTextSize(label))) selectedBrushTile_ = id;
			ImGui::EndGroup();
			ImGui::PopID();
		};
		swatch(0, Tr("Empty"), ImVec4(0.16f, 0.16f, 0.18f, 1.0f));
		for (const MapchipTileKind& k : mapTileSet_.kinds) swatch(k.id, k.name.c_str(), ImVec4(k.color[0], k.color[1], k.color[2], 1.0f));

		ImGui::Spacing();
		ImGui::Text(Tr("Level Grid (%dx%d):"), mapCols_, mapRows_);

		float cellW = 18.0f;
		float cellH = 18.0f;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 startPos = ImGui::GetCursorScreenPos();

		for (int r = 0; r < mapRows_; ++r) {
			for (int c = 0; c < mapCols_; ++c) {
				ImVec2 pMin = ImVec2(startPos.x + c * (cellW + 2.0f), startPos.y + r * (cellH + 2.0f));
				ImVec2 pMax = ImVec2(pMin.x + cellW, pMin.y + cellH);

				const int tile = mapGrid_[r][c];
				ImU32 col = IM_COL32(40, 40, 45, 255);
				if (const MapchipTileKind* kind = mapTileSet_.Find(tile)) {
					col = ImGui::ColorConvertFloat4ToU32(ImVec4(kind->color[0], kind->color[1], kind->color[2], 1.0f));
				} else if (tile != 0) {
					col = IM_COL32(150, 40, 150, 255); // an id the tile set does not have: Build skips it
				}

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
		ImGui::SetItemTooltip("%s", Tr("Creates one child object per tile: its prefab, or a cube / marker when it has none.\n"
			"Each gets the tile's tag. Building again replaces them."));

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

	// The previous build goes (prefab children too); the user's own children of the map object stay
	std::unordered_set<uint32_t> old;
	for (const GameObject& o : gameObjects_) {
		if (o.parentId == static_cast<int>(mapId) && o.name.starts_with(kBuiltPrefix)) old.insert(o.id);
	}
	for (bool grew = !old.empty(); grew;) {
		grew = false;
		for (const GameObject& o : gameObjects_) {
			if (o.parentId >= 0 && old.contains(static_cast<uint32_t>(o.parentId)) && old.insert(o.id).second) grew = true;
		}
	}
	std::erase_if(gameObjects_, [&](const GameObject& o) { return old.contains(o.id); });

	int made = 0;
	std::vector<uint32_t> prefabRoots;
	const bool wasSpawning = isSpawningFromScript_;
	isSpawningFromScript_ = true; // no per-object log line, selection change or undo step: one of each for the whole build below
	for (int r = 0; r < mapRows_; ++r) {
		for (int c = 0; c < mapCols_; ++c) {
			const MapchipTileKind* kind = mapTileSet_.Find(mapGrid_[r][c]);
			if (!kind) continue; // Empty, or an id the tile set does not have (any more)
			const std::string name = std::format("{}{} ({},{})", kBuiltPrefix, kind->name, c, r);
			// Row 0 is the top of the level: X to the right, Y up, one tile = mapTileSize_ in the map object's space
			const Vector3 pos = { c * mapTileSize_, (mapRows_ - 1 - r) * mapTileSize_, 0.0f };
			const uint32_t prefabId = kind->prefab.empty() ? 0u : InstantiatePrefabNow(kind->prefab, pos);
			GameObject* o = prefabId ? FindGameObjectById(prefabId, false) : nullptr;
			if (o) {
				prefabRoots.push_back(prefabId); // the prefab keeps its own size (decided: models are made to scale)
			} else {
				if (!kind->prefab.empty()) {
					LogMessage(std::format("[Mapchip] Prefab '{}' of tile '{}' was not found: using its shape instead.", kind->prefab, kind->name), 1);
				}
				o = kind->shape == MapchipShape::Marker ? &CreateGameObject(name, GameObjectType::Empty) : &CreateCube(name, pos);
				o->scale = { mapTileSize_, mapTileSize_, mapTileSize_ };
				if (o->hasMesh) o->materialColor = { kind->color[0], kind->color[1], kind->color[2], kind->color[3] };
				o->isStatic = kind->shape == MapchipShape::Block;    // never moves: static body, a NavGrid blocker
				o->isTrigger = kind->shape == MapchipShape::Trigger; // reports OnCollisionEnter without blocking
			}
			o->name = name;
			o->translation = pos;
			o->tag = kind->Tag();
			o->parentId = static_cast<int>(mapId);
			++made;
		}
	}
	isSpawningFromScript_ = wasSpawning;
	RebuildHierarchyLinks();
	// Built tiles are plain objects, not linked prefab instances: a scene load would otherwise rebuild them from the
	// prefab and drop the tile's name, tag and place. Prefab edits reach the level with the next Build.
	std::vector<uint32_t> unlink = prefabRoots;
	for (size_t i = 0; i < unlink.size(); ++i) {
		GameObject* o = FindGameObjectById(unlink[i], false);
		if (!o) continue;
		o->prefabSource.clear();
		o->prefabNodeIndex = -1;
		o->prefabNodeKey = -1;
		o->prefabOverrides.clear();
		o->nestedPrefab.clear();
		unlink.insert(unlink.end(), o->childrenIds.begin(), o->childrenIds.end());
	}
	selectedObjectIndex_ = FindGameObjectIndexById(mapId);
	extraSelection_.clear();
	isSceneDirty_ = true;
	undoPushPending_ = true; // one undo step for the whole rebuild
	LogMessage(std::format("[Mapchip] Built {} level object(s) under '{}'.", made, FindGameObjectById(mapId, false)->name), 0);
	return true;
}

namespace {
	std::filesystem::path TileSetFile(const std::filesystem::path& projectRoot, const std::string& path)
	{
		const std::filesystem::path p(path);
		return p.is_relative() && !projectRoot.empty() ? projectRoot / p : p;
	}
}

bool EditorSuite::UseMapchipTileSet(const std::string& path)
{
	mapTileSetPath_ = path;
	mapTileSet_ = MapchipTileSet::Default();
	mapTileSetUnreadable_ = false;
	if (selectedBrushTile_ != 0 && !mapTileSet_.Find(selectedBrushTile_)) selectedBrushTile_ = 0;
	if (path.empty()) return true;
	std::ifstream in(TileSetFile(currentProjectRoot_, path), std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	std::string why = "the file cannot be opened";
	std::optional<MapchipTileSet> set = in ? MapchipTileSet::FromJson(text.str(), &why) : std::nullopt;
	if (!set) {
		// The scene keeps naming the file, so fixing or restoring it is enough; until then the built-in set is shown
		LogMessage(std::format("[Mapchip] Tile set '{}' cannot be used ({}). Showing the built-in tiles.", path, why), 1);
		mapTileSetUnreadable_ = true;
		return false;
	}
	mapTileSet_ = std::move(*set);
	if (selectedBrushTile_ != 0 && !mapTileSet_.Find(selectedBrushTile_)) selectedBrushTile_ = 0; // the brush never paints an id this set lacks
	return true;
}

bool EditorSuite::CreateMapchipTileSet(const std::string& path)
{
	const std::string previous = mapTileSetPath_;
	const bool previousUnreadable = mapTileSetUnreadable_;
	mapTileSetPath_ = path;
	mapTileSetUnreadable_ = false; // a new file: writing the shown tiles to it loses nothing
	if (!SaveMapchipTileSet()) {
		mapTileSetPath_ = previous;
		mapTileSetUnreadable_ = previousUnreadable;
		return false;
	}
	AssetDb().Refresh();
	isSceneDirty_ = true; // the scene now names this file
	PushUndoSnapshot();
	return true;
}

bool EditorSuite::SaveMapchipTileSet()
{
	if (mapTileSetPath_.empty() || mapTileSetUnreadable_) return false;
	const std::filesystem::path file = TileSetFile(currentProjectRoot_, mapTileSetPath_);
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	if (!EditorSuiteDetail::WriteFileAtomic(file, mapTileSet_.ToJson())) {
		LogMessage(std::format("[Mapchip] Cannot write tile set {}", file.string()), 2);
		return false;
	}
	return true;
}

void EditorSuite::ResetMapchip()
{
	mapRows_ = 10;
	mapCols_ = 15;
	mapTileSize_ = 1.0f;
	mapTileSet_ = MapchipTileSet::Default();
	mapTileSetPath_.clear();
	mapTileSetUnreadable_ = false;
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

int EditorSuite::CountMapchipTiles(int id) const
{
	int count = 0;
	for (const auto& row : mapGrid_) count += static_cast<int>(std::count(row.begin(), row.end(), id));
	return count;
}

int EditorSuite::RemoveMapchipTileKind(int id)
{
	int cleared = 0;
	for (auto& row : mapGrid_) {
		for (int& cell : row) {
			if (cell == id) { cell = 0; ++cleared; }
		}
	}
	mapTileSet_.Remove(id);
	SaveMapchipTileSet();
	if (selectedBrushTile_ == id) selectedBrushTile_ = 0;
	if (cleared > 0) {
		isSceneDirty_ = true;
		PushUndoSnapshot();
	}
	return cleared;
}

void EditorSuite::DrawMapchipTileSetWindow()
{
#ifdef USE_IMGUI
	ImGui::SetNextWindowSize(ImVec2(720.0f, 420.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TrId("Tile Set Editor"), &showTileSetWindow_)) {
		ImGui::End();
		return;
	}

	// Which file the open scene uses
	ImGui::TextUnformatted(Tr("Tile set of this scene:"));
	ImGui::SameLine();
	ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "%s", mapTileSetPath_.empty() ? Tr("Built-in tiles") : mapTileSetPath_.c_str());
	ImGui::SetNextItemWidth(260.0f);
	if (ImGui::BeginCombo("##OpenTileSet", Tr("Use another tile set..."))) {
		if (ImGui::Selectable(Tr("Built-in tiles"), mapTileSetPath_.empty())) {
			UseMapchipTileSet("");
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		std::error_code ec;
		const std::filesystem::path assets = currentProjectRoot_ / "Assets";
		if (!currentProjectRoot_.empty() && std::filesystem::is_directory(assets, ec)) {
			for (auto it = std::filesystem::recursive_directory_iterator(assets, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
				if (!it->is_regular_file(ec) || !it->path().filename().string().ends_with(MapchipTileSet::kExtension)) continue;
				const std::string rel = ToProjectRelativePath(it->path());
				if (ImGui::Selectable(rel.c_str(), rel == mapTileSetPath_)) {
					UseMapchipTileSet(rel);
					isSceneDirty_ = true;
					PushUndoSnapshot();
				}
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(260.0f);
	const std::string suggested = "Assets/Levels/" + (currentSceneName_.empty() ? std::string("Tiles") : currentSceneName_) + MapchipTileSet::kExtension;
	ImGui::InputTextWithHint("##NewTileSet", suggested.c_str(), newTileSetPathBuf_, sizeof(newTileSetPathBuf_));
	ImGui::SameLine();
	ImGui::BeginDisabled(currentProjectRoot_.empty()); // a tile set is a project asset
	if (ImGui::Button(TrId("Save As New Tile Set"))) {
		std::string path = newTileSetPathBuf_[0] != '\0' ? std::string(newTileSetPathBuf_) : suggested;
		if (!path.ends_with(MapchipTileSet::kExtension)) path += MapchipTileSet::kExtension;
		if (CreateMapchipTileSet(path)) newTileSetPathBuf_[0] = '\0';
	}
	ImGui::SetItemTooltip("%s", Tr("Writes the tiles below to a new file in Assets and makes this scene use it"));
	ImGui::EndDisabled();
	ImGui::Separator();

	// The built-in set is the same for every scene: it is a starting point, edited once saved as a file.
	// An unreadable file shows the built-in tiles meanwhile; editing them would overwrite the user's file.
	const bool locked = mapTileSetPath_.empty() || mapTileSetUnreadable_;
	if (mapTileSetUnreadable_) {
		ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", Tr("This tile set file cannot be read (see the Console). Fix the file or choose another tile set."));
	} else if (locked) {
		ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "%s", Tr("Built-in tiles cannot be changed. Use \"Save As New Tile Set\" to make your own."));
	}
	ImGui::BeginDisabled(locked);
	bool changed = false;
	int removeId = 0;
	const char* shapes[] = { Tr("Block (solid cube)"), Tr("Trigger (cube you can pass)"), Tr("Marker (empty object)") };
	if (ImGui::BeginTable("##TileKinds", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn(TrId("Id"), ImGuiTableColumnFlags_WidthFixed, 28.0f);
		ImGui::TableSetupColumn(TrId("Colour"), ImGuiTableColumnFlags_WidthFixed, 46.0f);
		ImGui::TableSetupColumn(TrId("Name"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
		ImGui::TableSetupColumn(TrId("Prefab"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
		ImGui::TableSetupColumn(TrId("Without prefab"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
		ImGui::TableSetupColumn(TrId("Tag"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed, 24.0f);
		ImGui::TableHeadersRow();
		for (MapchipTileKind& k : mapTileSet_.kinds) {
			ImGui::PushID(k.id);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("%d", k.id);
			ImGui::TableNextColumn();
			ImGui::ColorEdit3("##Colour", k.color.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
			changed |= ImGui::IsItemDeactivatedAfterEdit(); // once when the picker closes, not every frame of a drag
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			char name[64];
			strncpy_s(name, k.name.c_str(), _TRUNCATE);
			if (ImGui::InputText("##Name", name, sizeof(name))) k.name = name;
			changed |= ImGui::IsItemDeactivatedAfterEdit();
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			char prefab[128];
			strncpy_s(prefab, k.prefab.c_str(), _TRUNCATE);
			if (ImGui::InputTextWithHint("##Prefab", Tr("drop a prefab here"), prefab, sizeof(prefab))) k.prefab = prefab;
			changed |= ImGui::IsItemDeactivatedAfterEdit();
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RIZE_ASSET_PATH")) {
					std::string file = std::filesystem::path(static_cast<const char*>(payload->Data)).filename().string();
					constexpr std::string_view kPrefabExt = ".prefab.json";
					if (file.ends_with(kPrefabExt)) {
						k.prefab = file.substr(0, file.size() - kPrefabExt.size());
						changed = true;
					} else {
						LogMessage(std::format("[Mapchip] '{}' is not a prefab.", file), 1);
					}
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::SetItemTooltip("%s", Tr("Prefab used for this tile (drag one from the Project panel). Empty = the shape on the right."));
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			int shape = static_cast<int>(k.shape);
			if (ImGui::Combo("##Shape", &shape, shapes, kMapchipShapeCount)) {
				k.shape = static_cast<MapchipShape>(shape);
				changed = true;
			}
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			char tag[64];
			strncpy_s(tag, k.tag.c_str(), _TRUNCATE);
			if (ImGui::InputTextWithHint("##Tag", k.name.c_str(), tag, sizeof(tag))) k.tag = tag;
			changed |= ImGui::IsItemDeactivatedAfterEdit();
			ImGui::SetItemTooltip("%s", Tr("Tag of the built objects, for scripts. Empty = the name."));
			ImGui::TableNextColumn();
			if (ImGui::SmallButton("X")) removeId = k.id;
			ImGui::SetItemTooltip("%s", Tr("Remove this tile"));
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	const bool full = static_cast<int>(mapTileSet_.kinds.size()) >= MapchipTileSet::kMaxKinds;
	ImGui::BeginDisabled(full);
	if (ImGui::Button(TrId("+ Add Tile"))) {
		if (MapchipTileKind* k = mapTileSet_.Add("Tile")) {
			k->name = std::format("Tile {}", k->id);
			changed = true;
		}
	}
	ImGui::EndDisabled();
	if (full) {
		ImGui::SameLine();
		ImGui::TextDisabled(Tr("A tile set holds at most %d tiles."), MapchipTileSet::kMaxKinds);
	}
	ImGui::EndDisabled();
	if (changed) SaveMapchipTileSet(); // like the graph windows: every finished edit is on disk at once

	// Removing a tile that cells still use asks first: those cells become Empty
	if (removeId != 0) {
		if (CountMapchipTiles(removeId) == 0) {
			RemoveMapchipTileKind(removeId);
		} else {
			pendingTileRemoveId_ = removeId;
			ImGui::OpenPopup(TrId("Remove Tile"));
		}
	}
	if (ImGui::BeginPopupModal(TrId("Remove Tile"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		const MapchipTileKind* kind = mapTileSet_.Find(pendingTileRemoveId_);
		ImGui::Text(Tr("\"%s\" is used by %d cell(s) of this scene's grid."), kind ? kind->name.c_str() : "?", CountMapchipTiles(pendingTileRemoveId_));
		ImGui::TextDisabled("%s", Tr("Those cells become Empty. Other scenes that use this tile set keep the number, which then means nothing.\n"
			"Undo brings the cells back, but not the tile: they show in purple until you paint them again."));
		if (ImGui::Button(TrId("Remove"), ImVec2(110.0f, 0.0f))) {
			RemoveMapchipTileKind(pendingTileRemoveId_);
			pendingTileRemoveId_ = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(TrId("Cancel"), ImVec2(110.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			pendingTileRemoveId_ = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::End();
#endif
}

