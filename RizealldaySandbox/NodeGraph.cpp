#include "NodeGraph.h"
#include "SceneJson.h"
#include <algorithm>
#include <format>
#include <functional>
#include <set>

const char* PinTypeName(PinType type)
{
	switch (type) {
	case PinType::Float: return "Float";
	case PinType::Float2: return "Float2";
	case PinType::Float3: return "Float3";
	case PinType::Float4: return "Float4";
	default: return "Any";
	}
}

const char* HlslTypeName(PinType type)
{
	switch (type) {
	case PinType::Float2: return "float2";
	case PinType::Float3: return "float3";
	case PinType::Float4: return "float4";
	default: return "float";
	}
}

bool CanConvert(PinType from, PinType to)
{
	if (to == PinType::Any || from == PinType::Any || from == to) return true;
	return from == PinType::Float || static_cast<int>(from) > static_cast<int>(to);
}

std::string HlslLiteral(const Vector4& value, PinType type)
{
	const auto f = [](float v) { return std::isfinite(v) ? std::format("{:.6g}", v) + (std::format("{:.6g}", v).find_first_of(".e") == std::string::npos ? ".0" : "") : std::string("0.0"); };
	switch (type) {
	case PinType::Float2: return std::format("float2({}, {})", f(value.x), f(value.y));
	case PinType::Float3: return std::format("float3({}, {}, {})", f(value.x), f(value.y), f(value.z));
	case PinType::Float4: return std::format("float4({}, {}, {}, {})", f(value.x), f(value.y), f(value.z), f(value.w));
	default: return f(value.x);
	}
}

namespace {
	int Width(PinType t) { return t == PinType::Any ? 1 : static_cast<int>(t); }

	/// `expr` of shape `from` as shape `to` (callers checked CanConvert)
	std::string Convert(const std::string& expr, PinType from, PinType to)
	{
		if (from == to || to == PinType::Any) return expr;
		if (from == PinType::Float || from == PinType::Any) {
			if (to == PinType::Float) return expr;
			std::string out = std::string(HlslTypeName(to)) + "(";
			for (int i = 0; i < Width(to); ++i) out += (i ? ", " : "") + expr;
			return out + ")";
		}
		static const char* swizzles[] = { "", ".x", ".xy", ".xyz", ".xyzw" };
		return "(" + expr + ")" + swizzles[Width(to)];
	}

	const PinDef* FindPin(const std::vector<PinDef>& pins, const std::string& name, size_t* index = nullptr)
	{
		for (size_t i = 0; i < pins.size(); ++i) {
			if (pins[i].name == name) {
				if (index) *index = i;
				return &pins[i];
			}
		}
		return nullptr;
	}

	const char* DomainName(uint32_t domain)
	{
		if (domain & kDomainMaterial) return "Material";
		if (domain & kDomainParticleSpawn) return "Particle Spawn";
		return "Particle Update";
	}

	/// What the Any pins of a node become: the widest connected input of an Any pin, Float when none is connected
	PinType ResolveAny(const NodeGraph& graph, const GraphNode& node, const NodeDef& def, const NodeLibrary& library, std::set<uint32_t>& visiting)
	{
		int width = 1;
		for (const PinDef& pin : def.inputs) {
			if (pin.type != PinType::Any) continue;
			const GraphLink* link = graph.LinkInto(node.id, pin.name);
			if (!link) continue;
			const GraphNode* from = graph.Find(link->fromNode);
			const NodeDef* fromDef = from ? library.Find(from->type) : nullptr;
			const PinDef* out = fromDef ? FindPin(fromDef->outputs, link->fromPin) : nullptr;
			if (!out) continue;
			PinType t = out->type;
			if (t == PinType::Any && !visiting.count(from->id)) {
				visiting.insert(from->id);
				t = ResolveAny(graph, *from, *fromDef, library, visiting);
				visiting.erase(from->id);
			}
			width = (std::max)(width, Width(t));
		}
		return static_cast<PinType>(width);
	}
}

PinType ResolvedOutputType(const NodeGraph& graph, uint32_t nodeId, const std::string& pin, const NodeLibrary& library)
{
	const GraphNode* node = graph.Find(nodeId);
	const NodeDef* def = node ? library.Find(node->type) : nullptr;
	const PinDef* out = def ? FindPin(def->outputs, pin) : nullptr;
	if (!out) return PinType::Float;
	if (out->type != PinType::Any) return out->type;
	std::set<uint32_t> visiting = { nodeId };
	return ResolveAny(graph, *node, *def, library, visiting);
}

// ---------------------------------------------------------------------------
// Graph editing
// ---------------------------------------------------------------------------
std::vector<GraphKey> ReadGraphKeys(const GraphNode& node, const std::string& param)
{
	const auto value = [&](const std::string& name) {
		const auto it = node.values.find(name);
		return it != node.values.end() ? it->second : Vector4{ 0.0f, 0.0f, 0.0f, 0.0f };
	};
	const int count = std::clamp(static_cast<int>(value(param + " Count").x), 1, static_cast<int>(kMaxGraphKeys));
	std::vector<GraphKey> keys;
	for (int i = 0; i < count; ++i) {
		const std::string index = std::to_string(i);
		keys.push_back({ std::clamp(value(param + " Time " + index).x, 0.0f, 1.0f), value(param + " " + index) });
	}
	std::stable_sort(keys.begin(), keys.end(), [](const GraphKey& a, const GraphKey& b) { return a.time < b.time; });
	return keys;
}

void WriteGraphKeys(GraphNode& node, const std::string& param, const std::vector<GraphKey>& keys)
{
	for (size_t i = 0; i < kMaxGraphKeys; ++i) {
		node.values.erase(param + " " + std::to_string(i));
		node.values.erase(param + " Time " + std::to_string(i));
	}
	const size_t count = std::clamp(keys.size(), size_t{ 1 }, kMaxGraphKeys);
	for (size_t i = 0; i < count; ++i) {
		const GraphKey key = i < keys.size() ? keys[i] : GraphKey{};
		const float t = std::clamp(key.time, 0.0f, 1.0f);
		node.values[param + " " + std::to_string(i)] = key.value;
		node.values[param + " Time " + std::to_string(i)] = { t, t, t, t };
	}
	const float n = static_cast<float>(count);
	node.values[param + " Count"] = { n, n, n, n };
}

int NodeGraph::FindParameter(const std::string& name) const
{
	for (size_t i = 0; i < parameters.size(); ++i) {
		if (parameters[i].name == name) return static_cast<int>(i);
	}
	return -1;
}

uint32_t NodeGraph::AddNode(const std::string& type, float x, float y, const NodeLibrary& library)
{
	const NodeDef* def = library.Find(type);
	if (!def) return 0;
	GraphNode node;
	node.id = nextId++;
	node.type = type;
	node.x = x;
	node.y = y;
	for (const PinDef& pin : def->inputs) node.values[pin.name] = pin.defaultValue;
	for (const ParamDef& param : def->params) {
		if (param.kind == ParamKind::Texture || param.kind == ParamKind::Parameter) {
			node.texts[param.name] = "";
		} else if (param.kind == ParamKind::Curve) {
			// defaultValue.x at the start, .y at the end
			const Vector4 a{ param.defaultValue.x, 0.0f, 0.0f, 0.0f }, b{ param.defaultValue.y, 0.0f, 0.0f, 0.0f };
			WriteGraphKeys(node, param.name, { { 0.0f, a }, { 1.0f, b } });
		} else if (param.kind == ParamKind::Gradient) {
			// defaultValue at the start, fading out to its own colour with alpha 0
			Vector4 end = param.defaultValue;
			end.w = 0.0f;
			WriteGraphKeys(node, param.name, { { 0.0f, param.defaultValue }, { 1.0f, end } });
		} else {
			node.values[param.name] = param.defaultValue;
		}
	}
	nodes.push_back(std::move(node));
	return nodes.back().id;
}

const GraphNode* NodeGraph::Find(uint32_t id) const
{
	for (const GraphNode& n : nodes) {
		if (n.id == id) return &n;
	}
	return nullptr;
}

GraphNode* NodeGraph::FindMutable(uint32_t id)
{
	return const_cast<GraphNode*>(static_cast<const NodeGraph*>(this)->Find(id));
}

const GraphLink* NodeGraph::LinkInto(uint32_t toNode, const std::string& toPin) const
{
	for (const GraphLink& l : links) {
		if (l.toNode == toNode && l.toPin == toPin) return &l;
	}
	return nullptr;
}

bool NodeGraph::DependsOn(uint32_t to, uint32_t from) const
{
	std::vector<uint32_t> stack = { to };
	std::set<uint32_t> seen;
	while (!stack.empty()) {
		const uint32_t id = stack.back();
		stack.pop_back();
		if (id == from) return true;
		if (!seen.insert(id).second) continue;
		for (const GraphLink& l : links) {
			if (l.toNode == id) stack.push_back(l.fromNode);
		}
	}
	return false;
}

bool NodeGraph::Connect(uint32_t fromNode, const std::string& fromPin, uint32_t toNode, const std::string& toPin, const NodeLibrary& library, std::string* why)
{
	const auto fail = [&](const std::string& reason) {
		if (why) *why = reason;
		return false;
	};
	const GraphNode* from = Find(fromNode);
	const GraphNode* to = Find(toNode);
	const NodeDef* fromDef = from ? library.Find(from->type) : nullptr;
	const NodeDef* toDef = to ? library.Find(to->type) : nullptr;
	if (!fromDef || !toDef) return fail("Both nodes must exist and be known.");
	const PinDef* out = FindPin(fromDef->outputs, fromPin);
	const PinDef* in = FindPin(toDef->inputs, toPin);
	if (!out) return fail(std::format("{} has no output called {}.", from->type, fromPin));
	if (!in) return fail(std::format("{} has no input called {}.", to->type, toPin));
	if (fromNode == toNode || DependsOn(fromNode, toNode)) return fail("That link would make a loop: a node cannot use its own result.");
	const PinType given = ResolvedOutputType(*this, fromNode, fromPin, library);
	if (!CanConvert(given, in->type)) {
		return fail(std::format("{}.{} needs {}, but {}.{} gives {}.", to->type, toPin, PinTypeName(in->type), from->type, fromPin, PinTypeName(given)));
	}
	if (in->type == PinType::Any) {
		// The Any inputs of one node must agree: a Float goes with everything, two wider values must be the same width
		for (const PinDef& other : toDef->inputs) {
			if (other.type != PinType::Any || other.name == toPin) continue;
			const GraphLink* link = LinkInto(toNode, other.name);
			if (!link) continue;
			const PinType t = ResolvedOutputType(*this, link->fromNode, link->fromPin, library);
			if (t != PinType::Float && given != PinType::Float && t != given) {
				return fail(std::format("{}: {} is {} but {} is {}. Use the same size, or a single Float.", to->type, other.name, PinTypeName(t), toPin, PinTypeName(given)));
			}
		}
	}
	Disconnect(toNode, toPin);
	links.push_back({ fromNode, fromPin, toNode, toPin });
	return true;
}

void NodeGraph::Disconnect(uint32_t toNode, const std::string& toPin)
{
	links.erase(std::remove_if(links.begin(), links.end(), [&](const GraphLink& l) { return l.toNode == toNode && l.toPin == toPin; }), links.end());
}

void NodeGraph::RemoveNode(uint32_t id)
{
	links.erase(std::remove_if(links.begin(), links.end(), [&](const GraphLink& l) { return l.fromNode == id || l.toNode == id; }), links.end());
	nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const GraphNode& n) { return n.id == id; }), nodes.end());
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------
std::string NodeGraph::ToJson() const
{
	using SceneJson::Json;
	Json root;
	root["kind"] = kind == GraphKind::Particle ? "particle" : "material";
	root["nextId"] = nextId;
	Json nodeArray = Json::array();
	for (const GraphNode& n : nodes) {
		Json o;
		o["id"] = n.id;
		o["type"] = n.type;
		o["x"] = SceneJson::Num(n.x);
		o["y"] = SceneJson::Num(n.y);
		Json values = Json::object();
		for (const auto& [name, v] : n.values) values[name] = SceneJson::Arr(v);
		o["values"] = std::move(values);
		Json texts = Json::object();
		for (const auto& [name, t] : n.texts) texts[name] = t;
		o["texts"] = std::move(texts);
		nodeArray.push_back(std::move(o));
	}
	root["nodes"] = std::move(nodeArray);
	Json linkArray = Json::array();
	for (const GraphLink& l : links) {
		linkArray.push_back({ { "from", l.fromNode }, { "fromPin", l.fromPin }, { "to", l.toNode }, { "toPin", l.toPin } });
	}
	root["links"] = std::move(linkArray);
	if (!parameters.empty()) {
		Json params = Json::array();
		for (const GraphParameter& p : parameters) params.push_back({ { "name", p.name }, { "type", PinTypeName(p.type) }, { "value", SceneJson::Arr(p.value) } });
		root["parameters"] = std::move(params);
	}
	return root.dump(1, '\t');
}

bool NodeGraph::FromJson(const std::string& text, NodeGraph& out, std::string* why)
{
	using SceneJson::Json;
	const std::optional<Json> parsed = SceneJson::Parse(text);
	if (!parsed || !parsed->is_object()) {
		if (why) *why = "The graph file is not valid JSON.";
		return false;
	}
	const Json& root = *parsed;
	NodeGraph g;
	std::string kind;
	SceneJson::Read(root, "kind", kind);
	g.kind = kind == "particle" ? GraphKind::Particle : GraphKind::Material;
	SceneJson::Read(root, "nextId", g.nextId);
	std::set<uint32_t> ids;
	if (const Json* nodes = SceneJson::Find(root, "nodes"); nodes && nodes->is_array()) {
		for (const Json& o : *nodes) {
			if (!o.is_object()) continue;
			GraphNode n;
			SceneJson::Read(o, "id", n.id);
			SceneJson::Read(o, "type", n.type);
			SceneJson::Read(o, "x", n.x);
			SceneJson::Read(o, "y", n.y);
			if (n.id == 0 || n.type.empty() || !ids.insert(n.id).second) continue;
			if (const Json* values = SceneJson::Find(o, "values"); values && values->is_object()) {
				for (auto it = values->begin(); it != values->end(); ++it) {
					Vector4 v{ 0.0f, 0.0f, 0.0f, 0.0f };
					if (it->is_array()) {
						float* c[4] = { &v.x, &v.y, &v.z, &v.w };
						for (size_t i = 0; i < 4 && i < it->size(); ++i) {
							if ((*it)[i].is_number()) *c[i] = (*it)[i].get<float>();
						}
					}
					n.values[it.key()] = v;
				}
			}
			if (const Json* texts = SceneJson::Find(o, "texts"); texts && texts->is_object()) {
				for (auto it = texts->begin(); it != texts->end(); ++it) {
					if (it->is_string()) n.texts[it.key()] = it->get<std::string>();
				}
			}
			g.nextId = (std::max)(g.nextId, n.id + 1);
			g.nodes.push_back(std::move(n));
		}
	}
	if (const Json* links = SceneJson::Find(root, "links"); links && links->is_array()) {
		for (const Json& o : *links) {
			if (!o.is_object()) continue;
			GraphLink l;
			SceneJson::Read(o, "from", l.fromNode);
			SceneJson::Read(o, "fromPin", l.fromPin);
			SceneJson::Read(o, "to", l.toNode);
			SceneJson::Read(o, "toPin", l.toPin);
			if (!ids.count(l.fromNode) || !ids.count(l.toNode) || g.LinkInto(l.toNode, l.toPin)) continue;
			g.links.push_back(std::move(l));
		}
	}
	if (const Json* params = SceneJson::Find(root, "parameters"); params && params->is_array()) {
		for (const Json& o : *params) {
			if (!o.is_object() || g.parameters.size() >= kMaxGraphParameters) continue;
			GraphParameter p;
			std::string type;
			SceneJson::Read(o, "name", p.name);
			SceneJson::Read(o, "type", type);
			p.type = type == PinTypeName(PinType::Float3) ? PinType::Float3 : type == PinTypeName(PinType::Float4) ? PinType::Float4 : PinType::Float;
			if (const Json* v = SceneJson::Find(o, "value"); v && v->is_array()) {
				float* c[4] = { &p.value.x, &p.value.y, &p.value.z, &p.value.w };
				for (size_t i = 0; i < 4 && i < v->size(); ++i) {
					if ((*v)[i].is_number()) *c[i] = (*v)[i].get<float>();
				}
			}
			if (!p.name.empty() && g.FindParameter(p.name) < 0) g.parameters.push_back(std::move(p));
		}
	}
	out = std::move(g);
	return true;
}

// ---------------------------------------------------------------------------
// Code generation
// ---------------------------------------------------------------------------
Vector4 NodeEmitContext::Value(const std::string& name) const
{
	if (node) {
		if (auto it = node->values.find(name); it != node->values.end()) return it->second;
	}
	return { 0.0f, 0.0f, 0.0f, 0.0f };
}

std::string NodeEmitContext::Text(const std::string& name) const
{
	if (node) {
		if (auto it = node->texts.find(name); it != node->texts.end()) return it->second;
	}
	return {};
}

std::string NodeEmitContext::Declare(size_t i) const
{
	return std::string(HlslTypeName(outTypes.at(i))) + " " + out.at(i) + " = ";
}

int NodeEmitContext::AddTexture(const std::string& path) const
{
	if (!textures) return 0;
	for (size_t i = 0; i < textures->size(); ++i) {
		if ((*textures)[i] == path) return static_cast<int>(i);
	}
	textures->push_back(path);
	return static_cast<int>(textures->size()) - 1;
}

GeneratedGraphCode GenerateGraphCode(const NodeGraph& graph, const NodeLibrary& library, uint32_t domain)
{
	GeneratedGraphCode code;
	const auto error = [&](uint32_t node, std::string message) { code.errors.push_back({ node, std::move(message) }); };
	if (domain & kDomainMaterial) code.textures.push_back(""); // slot 0: the emitter's own texture

	// The output node of this domain
	const GraphNode* output = nullptr;
	std::string outputName = "Material Output";
	for (const NodeDef& def : library.All()) {
		if (def.isOutput && (def.domains & domain)) outputName = def.type;
	}
	for (const GraphNode& n : graph.nodes) {
		const NodeDef* def = library.Find(n.type);
		if (!def || !def->isOutput || !(def->domains & domain)) continue;
		if (output) error(n.id, std::format("Only one {} is used; delete this one.", outputName));
		else output = &n;
	}
	if (!output) {
		error(0, std::format("Add a {} node: it decides the final result.", outputName));
		return code;
	}

	// Nodes the output depends on, dependencies first
	std::vector<const GraphNode*> order;
	std::map<uint32_t, int> state; // 1 = visiting, 2 = done
	std::function<void(const GraphNode&)> visit = [&](const GraphNode& node) {
		state[node.id] = 1;
		const NodeDef* def = library.Find(node.type);
		if (def) {
			for (const PinDef& pin : def->inputs) {
				const GraphLink* link = graph.LinkInto(node.id, pin.name);
				const GraphNode* from = link ? graph.Find(link->fromNode) : nullptr;
				if (!from) continue;
				if (state[from->id] == 1) {
					error(node.id, "This node is part of a loop: a node cannot use its own result.");
					continue;
				}
				if (state[from->id] == 0) visit(*from);
			}
		}
		state[node.id] = 2;
		order.push_back(&node);
	};
	visit(*output);

	std::map<std::pair<uint32_t, std::string>, std::pair<std::string, PinType>> outputs; // (node, pin) -> variable, shape
	for (const GraphNode* node : order) {
		const NodeDef* def = library.Find(node->type);
		if (!def) {
			error(node->id, std::format("Unknown node \"{}\" (made with a newer engine?). Delete it or replace it.", node->type));
			continue;
		}
		if (!(def->domains & domain)) {
			error(node->id, std::format("{} cannot be used in a {} graph.", node->type, DomainName(domain)));
			continue;
		}
		NodeEmitContext ctx;
		ctx.node = node;
		ctx.textures = &code.textures;
		ctx.domain = domain;
		ctx.graph = &graph;
		std::string problem;
		ctx.problem = &problem;
		std::set<uint32_t> visiting = { node->id };
		ctx.anyType = ResolveAny(graph, *node, *def, library, visiting);
		bool inputsOk = true;
		for (const PinDef& pin : def->inputs) {
			const PinType wanted = pin.type == PinType::Any ? ctx.anyType : pin.type;
			const GraphLink* link = graph.LinkInto(node->id, pin.name);
			auto source = link ? outputs.find({ link->fromNode, link->fromPin }) : outputs.end();
			if (link && source == outputs.end()) {
				inputsOk = false; // its source failed and already reported
				ctx.in.push_back("0");
				continue;
			}
			if (link) {
				if (!CanConvert(source->second.second, wanted)) {
					error(node->id, std::format("Input {} needs {}, but gets {}.", pin.name, PinTypeName(wanted), PinTypeName(source->second.second)));
					inputsOk = false;
				}
				ctx.in.push_back(Convert(source->second.first, source->second.second, wanted));
			} else if (!pin.defaultExpr.empty()) {
				ctx.in.push_back(Convert(pin.defaultExpr, pin.type == PinType::Any ? PinType::Float : pin.type, wanted));
			} else {
				const auto it = node->values.find(pin.name);
				const Vector4 value = it != node->values.end() ? it->second : pin.defaultValue;
				ctx.in.push_back(pin.type == PinType::Any ? Convert(HlslLiteral(value, PinType::Float), PinType::Float, wanted) : HlslLiteral(value, wanted));
			}
		}
		for (size_t i = 0; i < def->outputs.size(); ++i) {
			ctx.out.push_back(std::format("n{}_{}", node->id, i));
			ctx.outTypes.push_back(def->outputs[i].type == PinType::Any ? ctx.anyType : def->outputs[i].type);
		}
		if (!inputsOk) continue;
		code.body += std::format("// {} {}\n", node->id, node->type);
		code.body += def->emit ? def->emit(ctx) : std::string();
		if (!problem.empty()) error(node->id, problem);
		if (!code.body.empty() && code.body.back() != '\n') code.body += '\n';
		for (size_t i = 0; i < def->outputs.size(); ++i) outputs[{ node->id, def->outputs[i].name }] = { ctx.out[i], ctx.outTypes[i] };
	}
	if (code.textures.size() > kMaxGraphTextures) {
		error(0, std::format("A material can use at most {} textures (the emitter's own counts as one). It uses {}.", kMaxGraphTextures, code.textures.size()));
	}
	code.ok = code.errors.empty();
	return code;
}

// ---------------------------------------------------------------------------
// Library
// ---------------------------------------------------------------------------
void NodeLibrary::Register(NodeDef def)
{
	for (NodeDef& existing : defs_) {
		if (existing.type == def.type) {
			existing = std::move(def);
			return;
		}
	}
	defs_.push_back(std::move(def));
}

const NodeDef* NodeLibrary::Find(const std::string& type) const
{
	for (const NodeDef& def : defs_) {
		if (def.type == type) return &def;
	}
	return nullptr;
}
