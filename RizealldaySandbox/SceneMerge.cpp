#include "SceneMerge.h"
#include "SceneJson.h"
#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <set>

namespace SceneMerge {
namespace {

using Json = SceneJson::Json;

std::string Text(const Json* v) { return v ? v->dump(-1, ' ', false, Json::error_handler_t::replace) : std::string(); }
bool Same(const Json* a, const Json* b) { return (a && b) ? *a == *b : a == b; }
const Json* Get(const Json* obj, const std::string& key)
{
	if (!obj || !obj->is_object()) return nullptr;
	const auto it = obj->find(key);
	return (it != obj->end()) ? &*it : nullptr;
}
std::string NameOf(const Json* o) { const Json* n = Get(o, "name"); return (n && n->is_string()) ? n->get<std::string>() : std::string(); }

struct Ctx {
	Result& out;
	uint32_t objectId = 0;
	std::string objectName;
};

std::optional<Json> MergeValue(Ctx& c, const std::string& path, const Json* b, const Json* o, const Json* t, bool cells);

// Per key: ours' key order, then keys only theirs has in theirs' order
Json MergeFields(Ctx& c, const std::string& path, const Json* b, const Json& o, const Json& t, bool cells, bool fileRoot)
{
	Json result = Json::object();
	auto one = [&](const std::string& key) {
		const std::string child = path.empty() ? key : path + "/" + key;
		const bool childCells = cells || (fileRoot && key == "mapchip"); // the Mapchip grid merges cell by cell
		if (std::optional<Json> v = MergeValue(c, child, Get(b, key), Get(&o, key), Get(&t, key), childCells)) result[key] = std::move(*v);
	};
	for (auto it = o.begin(); it != o.end(); ++it) one(it.key());
	for (auto it = t.begin(); it != t.end(); ++it) {
		if (!o.contains(it.key())) one(it.key());
	}
	return result;
}

// nullopt = the key is absent in the result
std::optional<Json> MergeValue(Ctx& c, const std::string& path, const Json* b, const Json* o, const Json* t, bool cells)
{
	auto copy = [](const Json* v) { return v ? std::optional<Json>(*v) : std::nullopt; };
	if (Same(o, t) || Same(b, t)) return copy(o);
	if (Same(b, o)) return copy(t);
	// Both sides changed it differently: look inside objects; inside the Mapchip, inside arrays of equal length
	if (o && t && o->is_object() && t->is_object()) {
		return MergeFields(c, path, (b && b->is_object()) ? b : nullptr, *o, *t, cells, false);
	}
	if (cells && o && t && o->is_array() && t->is_array() && o->size() == t->size()) {
		// shortcut: a side that adds or removes a row/layer while the other paints the same grid is one conflict, fine while grids are resized rarely
		Json result = Json::array();
		const bool baseArray = b && b->is_array();
		for (size_t i = 0; i < o->size(); ++i) {
			const Json* bi = (baseArray && i < b->size()) ? &(*b)[i] : nullptr;
			std::optional<Json> v = MergeValue(c, path + "/" + std::to_string(i), bi, &(*o)[i], &(*t)[i], true);
			result.push_back(v ? std::move(*v) : Json(0));
		}
		return result;
	}
	c.out.conflicts.push_back({ c.objectId, c.objectName, path, Text(o), Text(t) });
	return copy(o);
}

std::optional<uint32_t> IdOf(const Json& o)
{
	const Json* id = Get(&o, "id");
	if (!id || !id->is_number_integer()) return std::nullopt;
	const int64_t v = id->get<int64_t>();
	if (v <= 0 || v > 0xFFFFFFFFll) return std::nullopt;
	return static_cast<uint32_t>(v);
}

struct Side {
	Json root = Json::object();
	bool empty = true;
	bool prefab = false;
	std::vector<uint32_t> order;          // ids in file order
	std::map<uint32_t, const Json*> byId; // points into root[listKey]
};

const char* ListKey(const Json& root) { return root.contains("objects") ? "objects" : "gameObjects"; }

bool Load(const std::string& text, const char* label, bool allowEmpty, Side& side, std::string& error)
{
	if (allowEmpty && text.find_first_not_of(" \t\r\n") == std::string::npos) return true; // no common ancestor
	std::string why;
	std::optional<Json> parsed = SceneJson::Parse(text, &why);
	if (!parsed || !parsed->is_object()) {
		error = std::format("{} is not a scene or prefab file{}", label, why.empty() ? "" : ": " + why);
		return false;
	}
	side.root = std::move(*parsed);
	side.empty = false;
	side.prefab = side.root.contains("objects") || side.root.contains("prefabVersion");
	for (const char* key : { "formatVersion", "prefabVersion" }) {
		const Json* v = Get(&side.root, key);
		if (v && (!v->is_number() || v->get<double>() > kKnownFormatVersion)) {
			error = std::format("{} was written by a newer editor ({} {}); update the engine before merging", label, key, Text(v));
			return false;
		}
	}
	return true;
}

// Builds the id index (after any id remapping, since it points into root)
bool Index(const char* label, Side& side, std::string& error)
{
	side.order.clear();
	side.byId.clear();
	if (side.empty) return true;
	const Json* list = Get(&side.root, ListKey(side.root));
	if (!list) return true;
	if (!list->is_array()) {
		error = std::format("{}: \"{}\" is not a list", label, ListKey(side.root));
		return false;
	}
	for (const Json& o : *list) {
		const std::optional<uint32_t> id = IdOf(o);
		if (!o.is_object() || !id) {
			error = std::format("{}: an object has no valid id", label);
			return false;
		}
		if (!side.byId.emplace(*id, &o).second) {
			error = std::format("{}: id {} is used twice", label, *id);
			return false;
		}
		side.order.push_back(*id);
	}
	return true;
}

const Json* Find(const Side& s, uint32_t id)
{
	const auto it = s.byId.find(id);
	return (it != s.byId.end()) ? it->second : nullptr;
}

bool InIdOrder(const Side& s) { return std::is_sorted(s.order.begin(), s.order.end()); }

} // namespace

Result Merge(const std::string& base, const std::string& ours, const std::string& theirs)
{
	Result r;
	Side b, o, t;
	if (!Load(base, "Base", true, b, r.error) || !Load(ours, "Ours", false, o, r.error) || !Load(theirs, "Theirs", false, t, r.error)) return r;
	if (o.prefab != t.prefab || (!b.empty && b.prefab != o.prefab)) {
		r.error = "The three files are not the same kind (scene vs prefab)";
		return r;
	}
	if (!Index("Base", b, r.error) || !Index("Ours", o, r.error) || !Index("Theirs", t, r.error)) return r;
	const char* listKey = ListKey(o.root);
	const bool idOrder = InIdOrder(b) && InIdOrder(o) && InIdOrder(t); // before theirs' ids change

	// Both sides added a different object with the same id (sequential ids before 2.12): theirs gets a new id
	uint32_t maxId = 0;
	for (const Side* s : { &b, &o, &t }) {
		if (!s->order.empty()) maxId = (std::max)(maxId, *std::max_element(s->order.begin(), s->order.end()));
	}
	std::map<int64_t, uint32_t> remap;
	for (const uint32_t id : t.order) {
		const Json* mine = Find(o, id);
		// Without a base (git add/add) the same id on both sides is the same object, merged field by field
		if (mine && !b.empty && !Find(b, id) && *mine != *Find(t, id)) remap[id] = 0;
	}
	if (!remap.empty()) {
		// Prefab file ids are prefabNodeKey + 1: a renumbered prefab node also gets a fresh key
		int64_t maxKey = -1;
		for (const Side* s : { &b, &o, &t }) {
			for (const auto& entry : s->byId) {
				const Json* k = Get(entry.second, "prefabNodeKey");
				if (k && k->is_number_integer()) maxKey = (std::max)(maxKey, k->get<int64_t>());
			}
		}
		for (Json& obj : t.root[ListKey(t.root)]) {
			const auto it = remap.find(*IdOf(obj));
			if (it == remap.end()) continue;
			if (o.prefab && obj.contains("prefabNodeKey")) {
				maxKey = (std::max)(maxKey + 1, static_cast<int64_t>(maxId));
				obj["prefabNodeKey"] = maxKey;
				maxId = static_cast<uint32_t>(maxKey + 1);
			} else {
				++maxId;
			}
			it->second = maxId;
			obj["id"] = maxId;
			r.notes.push_back({ NoteKind::IdChanged, maxId, NameOf(&obj),
				std::format("'{}' was added on both sides with id {}: theirs now has id {}.", NameOf(&obj), it->first, maxId) });
		}
		auto relink = [&](Json& holder, const char* key) {
			Json* p = holder.contains(key) ? &holder[key] : nullptr;
			if (!p || !p->is_number_integer()) return;
			if (const auto it = remap.find(p->get<int64_t>()); it != remap.end()) *p = it->second;
		};
		for (Json& obj : t.root[ListKey(t.root)]) {
			relink(obj, "parentId");
			relink(obj, "attachedParentId");
			Json* c = obj.contains("components") ? &obj["components"] : nullptr;
			if (c && c->is_object() && c->contains("Joint") && (*c)["Joint"].is_object()) relink((*c)["Joint"], "connectedObjectId");
		}
		Index("Theirs", t, r.error);
	}

	// Output order: id order when every file is in id order, else ours' order with theirs' additions after their neighbour
	std::vector<uint32_t> ids = o.order;
	if (idOrder) {
		std::set<uint32_t> all(ids.begin(), ids.end());
		all.insert(t.order.begin(), t.order.end());
		ids.assign(all.begin(), all.end());
	} else {
		size_t anchor = 0;
		for (const uint32_t id : t.order) {
			const auto it = std::find(ids.begin(), ids.end(), id);
			if (it != ids.end()) { anchor = static_cast<size_t>(it - ids.begin()) + 1; continue; }
			ids.insert(ids.begin() + anchor++, id);
		}
	}

	Json objects = Json::array();
	for (const uint32_t id : ids) {
		const Json* bo = Find(b, id);
		const Json* oo = Find(o, id);
		const Json* to = Find(t, id);
		Ctx c{ r, id, NameOf(oo ? oo : to) };
		if (oo && to) {
			objects.push_back(*MergeValue(c, "", bo, oo, to, false));
			continue;
		}
		const Json* kept = oo ? oo : to;
		if (bo && *bo == *kept) continue; // deleted on one side, untouched on the other
		if (bo) {
			r.notes.push_back({ NoteKind::KeptDeleted, id, c.objectName,
				std::format("'{}' (id {}) was deleted on one side and changed on the other: kept with the changes.", c.objectName, id) });
		}
		objects.push_back(*kept);
	}

	// Children whose parent is gone become root objects
	std::set<int64_t> present;
	for (const Json& obj : objects) present.insert(*IdOf(obj));
	for (Json& obj : objects) {
		Json* p = obj.contains("parentId") ? &obj["parentId"] : nullptr;
		if (!p || !p->is_number_integer() || p->get<int64_t>() <= 0 || present.count(p->get<int64_t>())) continue;
		*p = -1;
		r.notes.push_back({ NoteKind::MovedToRoot, *IdOf(obj), NameOf(&obj),
			std::format("'{}' (id {}) lost its parent (deleted on the other side): moved to the root.", NameOf(&obj), *IdOf(obj)) });
	}

	// Root keys (scene settings, unknown keys) per field; the object list stays where ours has it
	for (Side* s : { &b, &o, &t }) {
		if (!s->empty && s->root.contains(ListKey(s->root))) s->root[ListKey(s->root)] = 0;
	}
	Ctx sceneCtx{ r, 0, std::string() };
	Json root = MergeFields(sceneCtx, "", b.empty ? nullptr : &b.root, o.root, t.root, false, true);
	if (root.contains(listKey) || !objects.empty()) root[listKey] = std::move(objects);

	r.mergedText = SceneJson::DumpCompact(root);
	r.ok = true;
	return r;
}

namespace {
const char* KindName(NoteKind k)
{
	switch (k) {
	case NoteKind::MovedToRoot: return "movedToRoot";
	case NoteKind::IdChanged: return "idChanged";
	default: return "keptDeleted";
	}
}
}

std::string ReportToJson(const Result& result)
{
	Json conflicts = Json::array();
	for (const Conflict& c : result.conflicts) {
		Json e = { { "objectId", c.objectId }, { "objectName", c.objectName }, { "field", c.field } };
		if (!c.ours.empty()) e["ours"] = Json::parse(c.ours);
		if (!c.theirs.empty()) e["theirs"] = Json::parse(c.theirs);
		conflicts.push_back(std::move(e));
	}
	Json notes = Json::array();
	for (const Note& n : result.notes) {
		notes.push_back(Json{ { "kind", KindName(n.kind) }, { "objectId", n.objectId }, { "objectName", n.objectName }, { "message", n.message } });
	}
	return SceneJson::DumpCompact(Json{ { "version", 1 }, { "conflicts", std::move(conflicts) }, { "notes", std::move(notes) } });
}

bool ReportFromJson(const std::string& text, std::vector<Conflict>& conflicts, std::vector<Note>& notes, std::string* error)
{
	auto fail = [&](const char* why) { if (error) *error = why; return false; };
	const std::optional<Json> j = SceneJson::Parse(text);
	if (!j || !j->is_object() || !Get(&*j, "conflicts") || !(*j)["conflicts"].is_array()) return fail("Not a merge report");
	if (const Json* v = Get(&*j, "version"); !v || !v->is_number() || v->get<double>() > 1) return fail("Merge report from a newer editor");
	conflicts.clear();
	notes.clear();
	for (const Json& e : (*j)["conflicts"]) {
		Conflict c;
		SceneJson::Read(e, "objectId", c.objectId);
		SceneJson::Read(e, "objectName", c.objectName);
		SceneJson::Read(e, "field", c.field);
		c.ours = Text(Get(&e, "ours"));
		c.theirs = Text(Get(&e, "theirs"));
		conflicts.push_back(std::move(c));
	}
	if (const Json* list = Get(&*j, "notes"); list && list->is_array()) {
		for (const Json& e : *list) {
			Note n;
			std::string kind;
			SceneJson::Read(e, "kind", kind);
			n.kind = kind == "movedToRoot" ? NoteKind::MovedToRoot : kind == "idChanged" ? NoteKind::IdChanged : NoteKind::KeptDeleted;
			SceneJson::Read(e, "objectId", n.objectId);
			SceneJson::Read(e, "objectName", n.objectName);
			SceneJson::Read(e, "message", n.message);
			notes.push_back(std::move(n));
		}
	}
	return true;
}

} // namespace SceneMerge
