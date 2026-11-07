#include "GraphEditorModel.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------
void GraphEditorModel::Reset(const NodeGraph& g)
{
	graph = g;
	selection.clear();
	dirty = false;
	history_ = { graph.ToJson() };
	current_ = 0;
	++revision_;
}

void GraphEditorModel::Commit()
{
	std::string now = graph.ToJson();
	if (!history_.empty() && history_[current_] == now) return;
	history_.resize(current_ + 1);
	history_.push_back(std::move(now));
	current_ = history_.size() - 1;
	constexpr size_t kMaxSteps = 200;
	if (history_.size() > kMaxSteps) {
		history_.erase(history_.begin());
		--current_;
	}
	dirty = true;
	++revision_;
}

bool GraphEditorModel::Undo()
{
	if (!CanUndo()) return false;
	--current_;
	NodeGraph::FromJson(history_[current_], graph, nullptr);
	selection.erase(std::remove_if(selection.begin(), selection.end(), [&](uint32_t id) { return !graph.Find(id); }), selection.end());
	dirty = true;
	++revision_;
	return true;
}

bool GraphEditorModel::Redo()
{
	if (!CanRedo()) return false;
	++current_;
	NodeGraph::FromJson(history_[current_], graph, nullptr);
	selection.erase(std::remove_if(selection.begin(), selection.end(), [&](uint32_t id) { return !graph.Find(id); }), selection.end());
	dirty = true;
	++revision_;
	return true;
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------
uint32_t GraphEditorModel::FindOutputNode() const
{
	for (const GraphNode& n : graph.nodes) {
		const NodeDef* def = NodeLibrary::Effects().Find(n.type);
		if (def && def->isOutput) return n.id;
	}
	return 0;
}

uint32_t GraphEditorModel::AddNode(const std::string& type, float x, float y)
{
	const uint32_t id = graph.AddNode(type, x, y, NodeLibrary::Effects());
	if (id != 0) {
		selection = { id };
		Commit();
	}
	return id;
}

uint32_t GraphEditorModel::AddNodeConnected(const std::string& type, float x, float y, uint32_t fromNode, const std::string& fromPin, bool fromIsOutput, std::string* why)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	const NodeDef* def = lib.Find(type);
	if (!def) {
		if (why) *why = "Unknown node.";
		return 0;
	}
	NodeGraph trial = graph;
	const uint32_t id = trial.AddNode(type, x, y, lib);
	std::string reason;
	bool linked = false;
	if (fromIsOutput) {
		for (const PinDef& pin : def->inputs) {
			if (trial.Connect(fromNode, fromPin, id, pin.name, lib, &reason)) {
				linked = true;
				break;
			}
		}
	} else {
		for (const PinDef& pin : def->outputs) {
			if (trial.Connect(id, pin.name, fromNode, fromPin, lib, &reason)) {
				linked = true;
				break;
			}
		}
	}
	if (!linked) {
		if (why) *why = reason.empty() ? std::string("This node has no pin that fits.") : reason;
		return 0;
	}
	graph = std::move(trial);
	selection = { id };
	Commit();
	return id;
}

bool GraphEditorModel::Connect(uint32_t fromNode, const std::string& fromPin, uint32_t toNode, const std::string& toPin, std::string* why)
{
	if (!graph.Connect(fromNode, fromPin, toNode, toPin, NodeLibrary::Effects(), why)) return false;
	Commit();
	return true;
}

void GraphEditorModel::Disconnect(uint32_t toNode, const std::string& toPin)
{
	if (!graph.LinkInto(toNode, toPin)) return;
	graph.Disconnect(toNode, toPin);
	Commit();
}

void GraphEditorModel::DeleteSelection()
{
	const uint32_t output = FindOutputNode();
	bool removed = false;
	for (uint32_t id : selection) {
		if (id == output || !graph.Find(id)) continue;
		graph.RemoveNode(id);
		removed = true;
	}
	selection.erase(std::remove_if(selection.begin(), selection.end(), [&](uint32_t id) { return !graph.Find(id); }), selection.end());
	if (removed) Commit();
}

std::vector<uint32_t> GraphEditorModel::Duplicate()
{
	const uint32_t output = FindOutputNode();
	std::map<uint32_t, uint32_t> copies;
	std::vector<uint32_t> created;
	for (uint32_t id : selection) {
		const GraphNode* node = graph.Find(id);
		if (!node || id == output) continue;
		GraphNode copy = *node;
		copy.id = graph.nextId++;
		copy.x += 40.0f;
		copy.y += 40.0f;
		copies[id] = copy.id;
		created.push_back(copy.id);
		graph.nodes.push_back(std::move(copy));
	}
	const std::vector<GraphLink> links = graph.links;
	for (const GraphLink& l : links) {
		auto from = copies.find(l.fromNode);
		auto to = copies.find(l.toNode);
		if (from != copies.end() && to != copies.end()) graph.links.push_back({ from->second, l.fromPin, to->second, l.toPin });
	}
	if (!created.empty()) {
		selection = created;
		Commit();
	}
	return created;
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------
namespace {
	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}
}

std::vector<const NodeDef*> SearchNodes(const NodeLibrary& library, const std::string& query, uint32_t domain, const NodeSearchFilter& filter)
{
	std::vector<std::string> words;
	std::istringstream in(Lower(query));
	for (std::string w; in >> w;) words.push_back(w);
	std::vector<std::pair<int, const NodeDef*>> scored;
	for (const NodeDef& def : library.All()) {
		if (def.isOutput || !(def.domains & domain)) continue;
		if (filter.fromOutput) {
			const bool takes = std::any_of(def.inputs.begin(), def.inputs.end(), [&](const PinDef& p) { return CanConvert(*filter.fromOutput, p.type); });
			if (!takes) continue;
		}
		if (filter.intoInput) {
			const bool gives = std::any_of(def.outputs.begin(), def.outputs.end(), [&](const PinDef& p) { return CanConvert(p.type, *filter.intoInput); });
			if (!gives) continue;
		}
		const std::string name = Lower(def.type), category = Lower(def.category), tooltip = Lower(def.tooltip);
		int score = 0;
		bool all = true;
		for (const std::string& w : words) {
			if (name.rfind(w, 0) == 0) score += 100;
			else if (name.find(w) != std::string::npos) score += 60;
			else if (category.find(w) != std::string::npos) score += 30;
			else if (tooltip.find(w) != std::string::npos) score += 10;
			else {
				all = false;
				break;
			}
		}
		if (all) scored.push_back({ score, &def });
	}
	std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
	std::vector<const NodeDef*> out;
	for (const auto& [score, def] : scored) out.push_back(def);
	return out;
}

// ---------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------
NodeGraph MakeParticleTemplate(const std::string& name)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	NodeGraph g;
	g.kind = GraphKind::Particle;
	const auto add = [&](const char* type, float x, float y) { return g.AddNode(type, x, y, lib); };
	const auto set = [&](uint32_t id, const char* key, Vector4 v) { g.FindMutable(id)->values[key] = v; };
	const auto one = [](float v) { return Vector4{ v, v, v, v }; };
	const auto link = [&](uint32_t from, const char* fromPin, uint32_t to, const char* toPin) { g.Connect(from, fromPin, to, toPin, lib, nullptr); };
	const auto param = [&](const char* paramName, PinType type, Vector4 value, float x, float y) {
		g.parameters.push_back({ paramName, type, value });
		const uint32_t id = add("Parameter", x, y);
		g.FindMutable(id)->texts["Name"] = paramName;
		return id;
	};
	const auto keys = [&](uint32_t id, const char* key, std::vector<GraphKey> k) { WriteGraphKeys(*g.FindMutable(id), key, k); };
	// Spawn nodes on the top rows, Update nodes below them; the outputs on the right
	const uint32_t spawn = add("Spawn Output", 250.0f, 0.0f);
	const uint32_t update = add("Update Output", 250.0f, 700.0f);

	if (name == "Fountain") {
		const uint32_t cone = add("Cone Direction", -700.0f, 0.0f);
		set(cone, "Angle", one(12.0f));
		const uint32_t speed = add("Random Range", -700.0f, 180.0f);
		set(speed, "Min", one(4.0f));
		set(speed, "Max", one(6.0f));
		const uint32_t velocity = add("Multiply", -400.0f, 60.0f);
		const uint32_t life = add("Random Range", -400.0f, 260.0f);
		set(life, "Min", one(1.5f));
		set(life, "Max", one(2.2f));
		link(cone, "Direction", velocity, "A");
		link(speed, "Value", velocity, "B");
		link(velocity, "Result", spawn, "Velocity");
		link(life, "Value", spawn, "Lifetime");
		const uint32_t gravity = add("Gravity", -400.0f, 700.0f);
		const uint32_t pull = param("Gravity", PinType::Float3, { 0.0f, -9.8f, 0.0f, 0.0f }, -700.0f, 760.0f);
		const uint32_t emitter = add("Emitter", -950.0f, 960.0f);
		const uint32_t split = add("Split", -700.0f, 960.0f);
		const uint32_t floor = add("Floor Collision", -150.0f, 760.0f);
		const uint32_t color = add("Gradient", -150.0f, 1000.0f);
		keys(color, "Gradient", { { 0.0f, { 0.6f, 0.85f, 1.6f, 1.0f } }, { 1.0f, { 0.3f, 0.5f, 1.0f, 0.0f } } });
		const uint32_t size = add("Curve", -150.0f, 1180.0f);
		keys(size, "Curve", { { 0.0f, { 0.25f, 0, 0, 0 } }, { 1.0f, { 0.08f, 0, 0, 0 } } });
		link(pull, "Value", gravity, "Acceleration");
		link(gravity, "Velocity", floor, "Velocity");
		link(emitter, "Position", split, "Value");
		link(split, "G", floor, "Height");
		link(floor, "Velocity", update, "Velocity");
		link(color, "RGBA", update, "Color");
		link(size, "Value", update, "Size");
	} else if (name == "Magic Swirl") {
		const uint32_t sphere = add("Sphere Shape", -700.0f, 0.0f);
		set(sphere, "Radius", one(1.0f));
		set(sphere, "Fill", one(1.0f)); // on the surface
		const uint32_t still = add("Vector3", -700.0f, 200.0f);
		set(still, "Value", { 0.0f, 0.3f, 0.0f, 0.0f });
		const uint32_t life = add("Random Range", -400.0f, 260.0f);
		set(life, "Min", one(1.5f));
		set(life, "Max", one(3.0f));
		link(sphere, "Position", spawn, "Position");
		link(still, "Value", spawn, "Velocity");
		link(life, "Value", spawn, "Lifetime");
		const uint32_t vortex = add("Vortex", -650.0f, 700.0f);
		set(vortex, "Pull", one(1.5f));
		const uint32_t spin = param("Spin", PinType::Float, one(3.0f), -950.0f, 760.0f);
		const uint32_t curl = add("Curl Noise", -400.0f, 700.0f);
		set(curl, "Strength", one(1.5f));
		const uint32_t drag = add("Drag", -150.0f, 700.0f);
		set(drag, "Amount", one(0.8f));
		const uint32_t color = add("Gradient", -150.0f, 940.0f);
		keys(color, "Gradient", { { 0.0f, { 1.6f, 0.4f, 2.4f, 0.0f } }, { 0.2f, { 1.6f, 0.4f, 2.4f, 1.0f } }, { 0.7f, { 0.3f, 1.4f, 2.4f, 0.8f } }, { 1.0f, { 0.2f, 0.6f, 1.6f, 0.0f } } });
		const uint32_t size = add("Curve", -150.0f, 1120.0f);
		keys(size, "Curve", { { 0.0f, { 0.0f, 0, 0, 0 } }, { 0.2f, { 0.18f, 0, 0, 0 } }, { 1.0f, { 0.0f, 0, 0, 0 } } });
		link(spin, "Value", vortex, "Spin");
		link(vortex, "Velocity", curl, "Velocity");
		link(curl, "Velocity", drag, "Velocity");
		link(drag, "Velocity", update, "Velocity");
		link(color, "RGBA", update, "Color");
		link(size, "Value", update, "Size");
	} else if (name == "Burst") {
		const uint32_t sphere = add("Sphere Shape", -950.0f, 0.0f);
		set(sphere, "Radius", one(0.2f));
		const uint32_t speed = add("Random Range", -950.0f, 200.0f);
		set(speed, "Min", one(3.0f));
		set(speed, "Max", one(8.0f));
		const uint32_t power = param("Speed", PinType::Float, one(1.0f), -950.0f, 380.0f);
		const uint32_t scaled = add("Multiply", -700.0f, 200.0f);
		const uint32_t velocity = add("Multiply", -400.0f, 60.0f);
		const uint32_t life = add("Random Range", -400.0f, 260.0f);
		set(life, "Min", one(0.6f));
		set(life, "Max", one(1.2f));
		link(sphere, "Position", spawn, "Position");
		link(speed, "Value", scaled, "A");
		link(power, "Value", scaled, "B");
		link(sphere, "Direction", velocity, "A");
		link(scaled, "Result", velocity, "B");
		link(velocity, "Result", spawn, "Velocity");
		link(life, "Value", spawn, "Lifetime");
		const uint32_t drag = add("Drag", -650.0f, 700.0f);
		set(drag, "Amount", one(2.5f));
		const uint32_t gravity = add("Gravity", -400.0f, 700.0f);
		set(gravity, "Acceleration", { 0.0f, -3.0f, 0.0f, 0.0f });
		const uint32_t color = add("Gradient", -150.0f, 900.0f);
		keys(color, "Gradient", { { 0.0f, { 4.0f, 3.0f, 1.2f, 1.0f } }, { 0.3f, { 3.0f, 0.9f, 0.2f, 1.0f } }, { 1.0f, { 0.4f, 0.05f, 0.02f, 0.0f } } });
		const uint32_t size = add("Curve", -150.0f, 1080.0f);
		keys(size, "Curve", { { 0.0f, { 0.4f, 0, 0, 0 } }, { 1.0f, { 0.05f, 0, 0, 0 } } });
		link(drag, "Velocity", gravity, "Velocity");
		link(gravity, "Velocity", update, "Velocity");
		link(color, "RGBA", update, "Color");
		link(size, "Value", update, "Size");
	} else if (name == "Rising Embers") {
		const uint32_t box = add("Box Shape", -700.0f, 0.0f);
		set(box, "Size", { 2.0f, 0.2f, 2.0f, 0.0f });
		const uint32_t rise = add("Vector3", -700.0f, 180.0f);
		set(rise, "Value", { 0.0f, 0.5f, 0.0f, 0.0f });
		const uint32_t size = add("Random Range", -700.0f, 360.0f);
		set(size, "Min", one(0.05f));
		set(size, "Max", one(0.15f));
		const uint32_t life = add("Random Range", -400.0f, 260.0f);
		set(life, "Min", one(2.0f));
		set(life, "Max", one(4.0f));
		link(box, "Position", spawn, "Position");
		link(rise, "Value", spawn, "Velocity");
		link(size, "Value", spawn, "Size");
		link(life, "Value", spawn, "Lifetime");
		const uint32_t wind = add("Wind", -650.0f, 700.0f);
		set(wind, "Strength", one(0.5f));
		const uint32_t windValue = param("Wind", PinType::Float3, { 0.5f, 1.5f, 0.0f, 0.0f }, -950.0f, 760.0f);
		const uint32_t curl = add("Curl Noise", -400.0f, 700.0f);
		set(curl, "Strength", one(1.0f));
		set(curl, "Frequency", one(0.8f));
		const uint32_t color = add("Gradient", -150.0f, 900.0f);
		keys(color, "Gradient", { { 0.0f, { 4.0f, 1.6f, 0.3f, 1.0f } }, { 0.6f, { 2.0f, 0.4f, 0.1f, 1.0f } }, { 1.0f, { 0.5f, 0.1f, 0.05f, 0.0f } } });
		link(windValue, "Value", wind, "Wind Velocity");
		link(wind, "Velocity", curl, "Velocity");
		link(curl, "Velocity", update, "Velocity");
		link(color, "RGBA", update, "Color");
	}
	return g;
}

NodeGraph MakeMaterialTemplate(const std::string& name)
{
	const NodeLibrary& lib = NodeLibrary::Effects();
	NodeGraph g;
	g.kind = GraphKind::Material;
	const auto add = [&](const char* type, float x, float y) { return g.AddNode(type, x, y, lib); };
	const auto set = [&](uint32_t id, const char* key, Vector4 v) { g.FindMutable(id)->values[key] = v; };
	const auto link = [&](uint32_t from, const char* fromPin, uint32_t to, const char* toPin) { g.Connect(from, fromPin, to, toPin, lib, nullptr); };
	const uint32_t out = add("Material Output", 250.0f, 0.0f);

	if (name == "Soft Glow") {
		const uint32_t radial = add("Radial Gradient", -550.0f, 0.0f);
		set(radial, "Softness", { 1.0f, 0.0f, 0.0f, 0.0f });
		const uint32_t power = add("Power", -320.0f, 0.0f);
		const uint32_t color = add("Particle Color", -550.0f, 220.0f);
		const uint32_t alpha = add("Multiply", -80.0f, 80.0f);
		link(radial, "Mask", power, "Base");
		link(power, "Result", alpha, "A");
		link(color, "A", alpha, "B");
		link(color, "RGB", out, "Emissive Color");
		link(alpha, "Result", out, "Opacity");
	} else if (name == "Scrolling Fire") {
		const uint32_t panner = add("Panner", -1000.0f, 0.0f);
		set(panner, "Speed", { 0.0f, 1.2f, 0.0f, 0.0f }); // UV y grows downwards: + moves the picture up
		const uint32_t noise = add("Noise", -760.0f, 0.0f);
		set(noise, "Scale", { 4.0f, 0.0f, 0.0f, 0.0f });
		const uint32_t radial = add("Radial Gradient", -760.0f, 180.0f);
		set(radial, "Softness", { 1.0f, 0.0f, 0.0f, 0.0f });
		const uint32_t shape = add("Multiply", -520.0f, 80.0f);
		const uint32_t color = add("Particle Color", -520.0f, 260.0f);
		const uint32_t alpha = add("Multiply", -280.0f, 80.0f);
		const uint32_t dark = add("Color", -760.0f, 380.0f);
		set(dark, "Color", { 3.0f, 0.6f, 0.1f, 1.0f });
		const uint32_t bright = add("Color", -760.0f, 540.0f);
		set(bright, "Color", { 4.0f, 2.6f, 0.7f, 1.0f });
		const uint32_t blend = add("Lerp", -280.0f, 400.0f);
		link(panner, "UV", noise, "UV");
		link(noise, "Noise", shape, "A");
		link(radial, "Mask", shape, "B");
		link(shape, "Result", alpha, "A");
		link(color, "A", alpha, "B");
		link(dark, "RGB", blend, "A");
		link(bright, "RGB", blend, "B");
		link(noise, "Noise", blend, "Alpha");
		link(blend, "Result", out, "Emissive Color");
		link(alpha, "Result", out, "Opacity");
	} else if (name == "Dissolve") {
		const uint32_t noise = add("Noise", -800.0f, 0.0f);
		set(noise, "Scale", { 5.0f, 0.0f, 0.0f, 0.0f });
		const uint32_t dissolve = add("Dissolve", -560.0f, 0.0f);
		const uint32_t color = add("Particle Color", -800.0f, 200.0f);
		const uint32_t alpha = add("Multiply", -280.0f, 0.0f);
		const uint32_t edgeColor = add("Color", -800.0f, 380.0f);
		set(edgeColor, "Color", { 4.0f, 1.2f, 0.2f, 1.0f });
		const uint32_t glow = add("Multiply", -560.0f, 280.0f);
		const uint32_t sum = add("Add", -280.0f, 220.0f);
		link(noise, "Noise", dissolve, "Mask");
		link(dissolve, "Opacity", alpha, "A");
		link(color, "A", alpha, "B");
		link(edgeColor, "RGB", glow, "A");
		link(dissolve, "Edge", glow, "B");
		link(color, "RGB", sum, "A");
		link(glow, "Result", sum, "B");
		link(sum, "Result", out, "Emissive Color");
		link(alpha, "Result", out, "Opacity");
	} else if (name == "Flipbook Smoke") {
		const uint32_t flipbook = add("Flipbook", -700.0f, 0.0f);
		const uint32_t sample = add("Texture Sample", -460.0f, 0.0f);
		const uint32_t color = add("Particle Color", -700.0f, 220.0f);
		const uint32_t rgb = add("Multiply", -200.0f, 0.0f);
		const uint32_t alpha = add("Multiply", -200.0f, 180.0f);
		link(flipbook, "UV", sample, "UV");
		link(sample, "RGB", rgb, "A");
		link(color, "RGB", rgb, "B");
		link(sample, "A", alpha, "A");
		link(color, "A", alpha, "B");
		link(rgb, "Result", out, "Emissive Color");
		link(alpha, "Result", out, "Opacity");
	} else if (name == "Energy Ring") {
		const uint32_t polar = add("Polar Coordinates", -1000.0f, 0.0f);
		const uint32_t inner = add("Smoothstep", -760.0f, 0.0f);
		set(inner, "Min", { 0.28f, 0.0f, 0.0f, 0.0f });
		set(inner, "Max", { 0.36f, 0.0f, 0.0f, 0.0f });
		const uint32_t outer = add("Smoothstep", -760.0f, 180.0f);
		set(outer, "Min", { 0.40f, 0.0f, 0.0f, 0.0f });
		set(outer, "Max", { 0.48f, 0.0f, 0.0f, 0.0f });
		const uint32_t invert = add("One Minus", -520.0f, 180.0f);
		const uint32_t ring = add("Multiply", -280.0f, 60.0f);
		const uint32_t panner = add("Panner", -760.0f, 360.0f);
		set(panner, "Speed", { 0.3f, 0.0f, 0.0f, 0.0f });
		const uint32_t noise = add("Noise", -520.0f, 360.0f);
		set(noise, "Scale", { 8.0f, 0.0f, 0.0f, 0.0f });
		const uint32_t flow = add("Multiply", -40.0f, 200.0f);
		const uint32_t color = add("Color", -280.0f, 520.0f);
		set(color, "Color", { 0.4f, 2.5f, 4.0f, 1.0f });
		link(polar, "Distance", inner, "Value");
		link(polar, "Distance", outer, "Value");
		link(outer, "Result", invert, "Value");
		link(inner, "Result", ring, "A");
		link(invert, "Result", ring, "B");
		link(polar, "UV", panner, "UV");
		link(panner, "UV", noise, "UV");
		link(ring, "Result", flow, "A");
		link(noise, "Noise", flow, "B");
		link(color, "RGB", out, "Emissive Color");
		link(flow, "Result", out, "Opacity");
	}
	return g;
}
