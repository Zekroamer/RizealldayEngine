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

		// Size, tile size and layout of the level
		ImGui::Spacing();
		if (!mapSizeEditing_) {
			mapSizeEdit_[0] = mapCols_;
			mapSizeEdit_[1] = mapRows_;
			mapSizeEdit_[2] = static_cast<int>(mapLayers_.size());
		}
		ImGui::SetNextItemWidth(200.0f);
		ImGui::InputInt3(TrId("Columns / Rows / Layers"), mapSizeEdit_);
		mapSizeEditing_ = ImGui::IsItemActive();
		if (ImGui::IsItemDeactivatedAfterEdit()) ResizeMapchip(mapSizeEdit_[0], mapSizeEdit_[1], mapSizeEdit_[2]);
		ImGui::SetItemTooltip(Tr("Grid size: columns and rows 1 to %d, layers 1 to %d (the third axis).\nTiles that still fit stay, new cells are Empty."),
			kMaxMapchipSide, kMaxMapchipLayers);
		ImGui::SetNextItemWidth(160.0f);
		ImGui::DragFloat(TrId("Tile Size"), &mapTileSize_, 0.05f, 0.1f, 100.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			MarkMapchipChanged();
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		ImGui::SetItemTooltip("%s", Tr("Distance between tiles in the built level, and the size of the fallback cubes"));
		int plane = mapPlaneXZ_ ? 1 : 0;
		const char* planes[] = { Tr("Side view (X / Y)"), Tr("Top-down (X / Z)") };
		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::Combo(TrId("Layout"), &plane, planes, 2)) {
			mapPlaneXZ_ = plane == 1;
			MarkMapchipChanged();
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		ImGui::SetItemTooltip("%s", Tr("Side view: rows go up (platformer). Top-down: rows go into the distance, the level lies on the floor."));

		// Which layer the grid below paints, plus the layer tools
		const int layerCount = static_cast<int>(mapLayers_.size());
		mapEditLayer_ = std::clamp(mapEditLayer_, 0, layerCount - 1);
		// Always shown so it can be found; with one layer it is greyed out and says how to get more
		ImGui::BeginDisabled(layerCount < 2);
		ImGui::SetNextItemWidth(160.0f);
		ImGui::SliderInt(TrId("Edit Layer"), &mapEditLayer_, 0, (std::max)(layerCount - 1, 0));
		ImGui::EndDisabled();
		if (layerCount < 2) {
			ImGui::SetItemTooltip("%s", Tr("Only one layer: set the third number of Columns / Rows / Layers, or use Copy Layer to Next."));
		} else {
			ImGui::SetItemTooltip("%s", mapPlaneXZ_ ? Tr("Layer 0 is the ground floor; each next layer is one tile higher.")
				: Tr("Layer 0 is the front; each next layer is one tile further back."));
		}
		if (ImGui::SmallButton(TrId("Copy Layer to Next"))) CopyMapchipLayerToNext();
		ImGui::SetItemTooltip("%s", Tr("Copies this layer onto the next one (adds a layer when this is the last) and edits that one"));
		ImGui::SameLine();
		if (ImGui::SmallButton(TrId("Clear Layer"))) ClearMapchipLayer();
		int filled = 0;
		for (const auto& layer : mapLayers_) {
			for (const auto& row : layer) filled += static_cast<int>(std::count_if(row.begin(), row.end(), [](int t) { return t != 0; }));
		}
		if (filled > 20000) {
			ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), Tr("%d filled tiles: Build makes one object for each, which can make the scene slow."), filled);
		}

		// The grid: drag to paint with the brush, right mouse button erases. One undo step per stroke.
		ImGui::Spacing();
		ImGui::TextUnformatted(Tr("Level Grid:"));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(110.0f);
		ImGui::SliderFloat("##CellZoom", &mapCellPx_, 6.0f, 32.0f, Tr("Zoom %.0f px"));
		const float step = mapCellPx_ + 2.0f;
		const ImVec2 gridSize(mapCols_ * step, mapRows_ * step);
		const float viewHeight = (std::min)(gridSize.y + ImGui::GetStyle().ScrollbarSize + 6.0f, 360.0f);
		if (ImGui::BeginChild("##MapchipGrid", ImVec2(-1.0f, viewHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("##Cells", gridSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
			const bool active = ImGui::IsItemActive();
			const bool hovered = ImGui::IsItemHovered();

			// Only the cells inside the scrolled view are drawn: a 256x256 grid stays cheap
			const ImVec2 clipMin = drawList->GetClipRectMin();
			const ImVec2 clipMax = drawList->GetClipRectMax();
			const int c0 = (std::max)(0, static_cast<int>((clipMin.x - origin.x) / step));
			const int c1 = (std::min)(mapCols_ - 1, static_cast<int>((clipMax.x - origin.x) / step));
			const int r0 = (std::max)(0, static_cast<int>((clipMin.y - origin.y) / step));
			const int r1 = (std::min)(mapRows_ - 1, static_cast<int>((clipMax.y - origin.y) / step));
			auto& grid = EditedMapchipLayer();
			const auto* below = mapEditLayer_ > 0 ? &mapLayers_[mapEditLayer_ - 1] : nullptr; // shown faintly as a guide
			for (int r = r0; r <= r1; ++r) {
				for (int c = c0; c <= c1; ++c) {
					const ImVec2 pMin(origin.x + c * step, origin.y + r * step);
					const int tile = grid[r][c];
					ImU32 col = IM_COL32(40, 40, 45, 255);
					if (const MapchipTileKind* kind = mapTileSet_.Find(tile)) {
						col = ImGui::ColorConvertFloat4ToU32(ImVec4(kind->color[0], kind->color[1], kind->color[2], 1.0f));
					} else if (tile != 0) {
						col = IM_COL32(150, 40, 150, 255); // an id the tile set does not have: Build skips it
					} else if (const MapchipTileKind* under = below ? mapTileSet_.Find((*below)[r][c]) : nullptr) {
						col = ImGui::ColorConvertFloat4ToU32(ImVec4(under->color[0] * 0.35f + 0.1f, under->color[1] * 0.35f + 0.1f, under->color[2] * 0.35f + 0.1f, 1.0f));
					}
					drawList->AddRectFilled(pMin, ImVec2(pMin.x + mapCellPx_, pMin.y + mapCellPx_), col, 2.0f);
				}
			}

			const ImVec2 mouse = ImGui::GetIO().MousePos;
			const int mc = static_cast<int>(std::floor((mouse.x - origin.x) / step));
			const int mr = static_cast<int>(std::floor((mouse.y - origin.y) / step));
			const bool inGrid = mc >= 0 && mc < mapCols_ && mr >= 0 && mr < mapRows_;
			if (active && inGrid) {
				const int paint = ImGui::IsMouseDown(ImGuiMouseButton_Left) ? selectedBrushTile_ : 0;
				if (grid[mr][mc] != paint) {
					grid[mr][mc] = paint;
					MarkMapchipChanged();
					isSceneDirty_ = true;
				}
				mapPainting_ = true;
			}
			if (hovered && inGrid) {
				const MapchipTileKind* kind = mapTileSet_.Find(grid[mr][mc]);
				ImGui::SetTooltip("(%d, %d, L%d)  %s", mc, mr, mapEditLayer_, kind ? kind->name.c_str() : Tr("Empty"));
			}
		}
		ImGui::EndChild();
		if (mapPainting_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			mapPainting_ = false;
			PushUndoSnapshot();
			if (mapAutoBuild_ && selectedObjectIndex_ >= 0) BuildMapchipObjects(gameObjects_[selectedObjectIndex_].id);
		}
		ImGui::TextDisabled("%s", Tr("Left drag paints, right drag erases."));

		if (ImGui::Button(TrId("Build Level Objects"), ImVec2(-1.0f, 26.0f))) {
			if (selectedObjectIndex_ >= 0) BuildMapchipObjects(gameObjects_[selectedObjectIndex_].id);
		}
		ImGui::SetItemTooltip("%s", Tr("Creates one child object per tile: its prefab, or a cube / marker when it has none.\n"
			"Each gets the tile's tag. Building again replaces them."));
		ImGui::Checkbox(TrId("Auto Build"), &mapAutoBuild_);
		ImGui::SetItemTooltip("%s", Tr("Build the level again after every finished paint stroke"));
		if (!MapchipBuildIsCurrent()) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "%s", Tr("The grid has changed since the last Build."));
		}

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
	const bool layered = mapLayers_.size() > 1;
	for (int l = 0; l < static_cast<int>(mapLayers_.size()); ++l) {
	for (int r = 0; r < mapRows_; ++r) {
		for (int c = 0; c < mapCols_; ++c) {
			const MapchipTileKind* kind = mapTileSet_.Find(mapLayers_[l][r][c]);
			if (!kind) continue; // Empty, or an id the tile set does not have (any more)
			const std::string name = layered ? std::format("{}{} ({},{},L{})", kBuiltPrefix, kind->name, c, r, l)
				: std::format("{}{} ({},{})", kBuiltPrefix, kind->name, c, r);
			// Row 0 is the top of the grid: the top of a side view (X/Y), the far end (+Z) of a top-down map (X/Z).
			// Layers are the third axis: behind a side view (+Z), above a top-down map (+Y). One tile = mapTileSize_.
			const float up = (mapRows_ - 1 - r) * mapTileSize_;
			const float depth = l * mapTileSize_;
			const Vector3 pos = mapPlaneXZ_ ? Vector3{ c * mapTileSize_, depth, up } : Vector3{ c * mapTileSize_, up, depth };
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
	mapBuiltRevision_ = mapGridRevision_;
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

bool EditorSuite::ResizeMapchip(int cols, int rows, int layers)
{
	cols = std::clamp(cols, 1, kMaxMapchipSide);
	rows = std::clamp(rows, 1, kMaxMapchipSide);
	layers = layers < 0 ? static_cast<int>(mapLayers_.size()) : std::clamp(layers, 1, kMaxMapchipLayers);
	if (cols == mapCols_ && rows == mapRows_ && layers == static_cast<int>(mapLayers_.size())) return true;
	mapLayers_.resize(layers);
	for (auto& layer : mapLayers_) {
		layer.resize(rows);
		for (auto& row : layer) row.resize(cols, 0);
	}
	mapCols_ = cols;
	mapRows_ = rows;
	mapEditLayer_ = (std::min)(mapEditLayer_, layers - 1);
	MarkMapchipChanged();
	isSceneDirty_ = true;
	PushUndoSnapshot();
	return true;
}

bool EditorSuite::CopyMapchipLayerToNext()
{
	if (mapEditLayer_ + 1 >= static_cast<int>(mapLayers_.size())) {
		if (static_cast<int>(mapLayers_.size()) >= kMaxMapchipLayers) {
			LogMessage(std::format("[Mapchip] A grid holds at most {} layers.", kMaxMapchipLayers), 1);
			return false;
		}
		mapLayers_.push_back({});
	}
	mapLayers_[mapEditLayer_ + 1] = mapLayers_[mapEditLayer_];
	++mapEditLayer_;
	MarkMapchipChanged();
	isSceneDirty_ = true;
	PushUndoSnapshot();
	return true;
}

void EditorSuite::ClearMapchipLayer()
{
	for (auto& row : EditedMapchipLayer()) std::fill(row.begin(), row.end(), 0);
	MarkMapchipChanged();
	isSceneDirty_ = true;
	PushUndoSnapshot();
}

void EditorSuite::ResetMapchip()
{
	mapRows_ = 10;
	mapCols_ = 15;
	mapTileSize_ = 1.0f;
	mapPlaneXZ_ = false;
	MarkMapchipChanged();
	mapTileSet_ = MapchipTileSet::Default();
	mapTileSetPath_.clear();
	mapTileSetUnreadable_ = false;
	mapLayers_.assign(1, std::vector<std::vector<int>>(mapRows_, std::vector<int>(mapCols_, 0)));
	mapEditLayer_ = 0;
	for (int c = 0; c < mapCols_; ++c) mapLayers_[0][mapRows_ - 1][c] = 1; // floor
}

bool EditorSuite::SaveMapchipCSV(const std::string& filePath)
{
	// Layers one after another, an empty line between them (a one-layer grid is the plain old CSV)
	std::string text;
	for (size_t l = 0; l < mapLayers_.size(); ++l) {
		if (l > 0) text += '\n';
		for (const auto& row : mapLayers_[l]) {
			for (size_t c = 0; c < row.size(); ++c) {
				if (c > 0) text += ',';
				text += std::to_string(row[c]);
			}
			text += '\n';
		}
	}
	const std::filesystem::path p(filePath);
	std::error_code ec;
	if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
	if (!EditorSuiteDetail::WriteFileAtomic(p, text)) {
		LogMessage(std::format("[Mapchip] Cannot write {}", filePath), 2);
		return false;
	}
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

	// An empty line ends a layer; empty lines before the first row or after the last one change nothing
	std::vector<std::vector<std::vector<int>>> layers(1);
	std::string line;
	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) {
			if (!layers.back().empty()) layers.emplace_back();
			continue;
		}
		std::vector<int> row;
		std::stringstream ss(line);
		std::string val;
		while (std::getline(ss, val, ',')) {
			try { row.push_back(std::stoi(val)); } catch (...) { row.push_back(0); }
		}
		layers.back().push_back(std::move(row));
	}
	file.close();
	if (layers.back().empty()) layers.pop_back();
	if (layers.empty()) return false;
	if (static_cast<int>(layers.size()) > kMaxMapchipLayers) layers.resize(kMaxMapchipLayers);

	// Every layer gets the same size: short rows and short layers end in Empty tiles
	int rows = 0, cols = 0;
	for (const auto& layer : layers) {
		rows = (std::max)(rows, static_cast<int>(layer.size()));
		for (const auto& row : layer) cols = (std::max)(cols, static_cast<int>(row.size()));
	}
	rows = (std::min)(rows, kMaxMapchipSide);
	cols = (std::min)(cols, kMaxMapchipSide);
	for (auto& layer : layers) {
		layer.resize(rows);
		for (auto& row : layer) row.resize(cols, 0);
	}
	mapRows_ = rows;
	mapCols_ = cols;
	mapLayers_ = std::move(layers);
	mapEditLayer_ = (std::min)(mapEditLayer_, static_cast<int>(mapLayers_.size()) - 1);
	MarkMapchipChanged();
	isSceneDirty_ = true; // the grid is saved with the scene: closing must ask, Ctrl+Z must undo the load
	PushUndoSnapshot();
	LogMessage(std::format("[Mapchip] Loaded ({}x{}, {} layer(s)): {}", mapCols_, mapRows_, mapLayers_.size(), filePath), 0);
	return true;
}

int EditorSuite::CountMapchipTiles(int id) const
{
	int count = 0;
	for (const auto& layer : mapLayers_) {
		for (const auto& row : layer) count += static_cast<int>(std::count(row.begin(), row.end(), id));
	}
	return count;
}

int EditorSuite::RemoveMapchipTileKind(int id)
{
	int cleared = 0;
	for (auto& layer : mapLayers_) {
		for (auto& row : layer) {
			for (int& cell : row) {
				if (cell == id) { cell = 0; ++cleared; }
			}
		}
	}
	mapTileSet_.Remove(id);
	SaveMapchipTileSet();
	if (selectedBrushTile_ == id) selectedBrushTile_ = 0;
	if (cleared > 0) {
		MarkMapchipChanged();
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

