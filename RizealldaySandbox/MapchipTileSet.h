#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

// Tile set of the Mapchip editor (pure, no editor or GPU knowledge): the kinds of tile a game's levels use.
// Grids and CSV files store tile ids, so an id never changes once given out and is never given out again:
// renaming a tile keeps every level meaning the same, and a deleted id cannot come back as another tile.
// Id 0 is always Empty and is not part of the list.

enum class MapchipShape : int {
	Block = 0,   // solid static cube
	Trigger = 1, // cube that reports contacts without blocking
	Marker = 2,  // empty object (spawn points and the like)
};
inline constexpr int kMapchipShapeCount = 3;

struct MapchipTileKind {
	int id = 0;
	std::string name;
	std::array<float, 4> color = { 0.6f, 0.6f, 0.6f, 1.0f }; // grid cell colour in the editor, and the fallback cube's
	std::string prefab; // prefab name (file name without .prefab.json); empty = the shape below
	MapchipShape shape = MapchipShape::Block;
	std::string tag; // empty = the name

	const std::string& Tag() const { return tag.empty() ? name : tag; }
};

struct MapchipTileSet {
	static constexpr int kMaxKinds = 63;
	static constexpr const char* kExtension = ".tileset.json";

	std::vector<MapchipTileKind> kinds;
	int nextId = 1;

	/// Wall (1, Block), Spike (2, Trigger), Spawn (3, Marker), Goal (4, Marker): what grids meant before tile sets existed
	static MapchipTileSet Default();

	const MapchipTileKind* Find(int id) const;
	MapchipTileKind* Find(int id);
	/// New kind with the next unused id, or nullptr when kMaxKinds kinds exist already
	MapchipTileKind* Add(const std::string& name);
	/// Removes a kind; its id is never handed out again
	bool Remove(int id);

	std::string ToJson() const;
	/// nullopt (and a reason) when the text is not a tile set
	static std::optional<MapchipTileSet> FromJson(const std::string& text, std::string* error = nullptr);
};
