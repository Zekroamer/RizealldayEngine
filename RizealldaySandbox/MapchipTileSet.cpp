#include "MapchipTileSet.h"
#include <nlohmann/json.hpp>
#include <algorithm>

MapchipTileSet MapchipTileSet::Default()
{
	MapchipTileSet set;
	auto add = [&](const char* name, std::array<float, 4> color, MapchipShape shape) {
		MapchipTileKind* kind = set.Add(name);
		kind->color = color;
		kind->shape = shape;
	};
	add("Wall", { 0.71f, 0.47f, 0.27f, 1.0f }, MapchipShape::Block);
	add("Spike", { 0.86f, 0.24f, 0.24f, 1.0f }, MapchipShape::Trigger);
	add("Spawn", { 0.24f, 0.71f, 0.86f, 1.0f }, MapchipShape::Marker);
	add("Goal", { 0.24f, 0.86f, 0.39f, 1.0f }, MapchipShape::Marker);
	return set;
}

const MapchipTileKind* MapchipTileSet::Find(int id) const
{
	auto it = std::find_if(kinds.begin(), kinds.end(), [id](const MapchipTileKind& k) { return k.id == id; });
	return it != kinds.end() ? &*it : nullptr;
}

MapchipTileKind* MapchipTileSet::Find(int id)
{
	return const_cast<MapchipTileKind*>(std::as_const(*this).Find(id));
}

MapchipTileKind* MapchipTileSet::Add(const std::string& name)
{
	if (static_cast<int>(kinds.size()) >= kMaxKinds) return nullptr;
	MapchipTileKind kind;
	kind.id = nextId++;
	kind.name = name;
	kinds.push_back(std::move(kind));
	return &kinds.back();
}

bool MapchipTileSet::Remove(int id)
{
	return std::erase_if(kinds, [id](const MapchipTileKind& k) { return k.id == id; }) > 0;
}

std::string MapchipTileSet::ToJson() const
{
	nlohmann::ordered_json root;
	root["version"] = 1;
	root["nextId"] = nextId;
	nlohmann::ordered_json list = nlohmann::ordered_json::array();
	for (const MapchipTileKind& k : kinds) {
		nlohmann::ordered_json j;
		j["id"] = k.id;
		j["name"] = k.name;
		j["color"] = { k.color[0], k.color[1], k.color[2], k.color[3] };
		j["shape"] = static_cast<int>(k.shape);
		if (!k.prefab.empty()) j["prefab"] = k.prefab;
		if (!k.tag.empty()) j["tag"] = k.tag;
		list.push_back(std::move(j));
	}
	root["kinds"] = std::move(list);
	return root.dump(1, '\t');
}

std::optional<MapchipTileSet> MapchipTileSet::FromJson(const std::string& text, std::string* error)
{
	auto fail = [&](const char* why) -> std::optional<MapchipTileSet> {
		if (error) *error = why;
		return std::nullopt;
	};
	const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
	if (!root.is_object()) return fail("not a JSON object");
	const auto list = root.find("kinds");
	if (list == root.end() || !list->is_array()) return fail("no \"kinds\" list");

	MapchipTileSet set;
	for (const nlohmann::json& j : *list) {
		if (!j.is_object() || !j.contains("id") || !j["id"].is_number_integer()) return fail("a kind has no id");
		MapchipTileKind k;
		k.id = j["id"].get<int>();
		if (k.id <= 0) return fail("id 0 is Empty and ids are positive");
		if (set.Find(k.id)) return fail("two kinds share an id");
		k.name = j.value("name", std::string("Tile"));
		k.prefab = j.value("prefab", std::string());
		k.tag = j.value("tag", std::string());
		k.shape = static_cast<MapchipShape>(std::clamp(j.value("shape", 0), 0, kMapchipShapeCount - 1));
		if (const auto c = j.find("color"); c != j.end() && c->is_array() && c->size() == 4) {
			for (size_t i = 0; i < 4; ++i) k.color[i] = (*c)[i].is_number() ? (*c)[i].get<float>() : 1.0f;
		}
		set.kinds.push_back(std::move(k));
		if (static_cast<int>(set.kinds.size()) > kMaxKinds) return fail("more than 63 kinds");
	}
	int highest = 0;
	for (const MapchipTileKind& k : set.kinds) highest = (std::max)(highest, k.id);
	set.nextId = (std::max)(root.value("nextId", 1), highest + 1); // never hand out an id that is or was in use
	return set;
}
