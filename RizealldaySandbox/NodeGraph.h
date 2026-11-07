#pragma once
#include "math/Vector4.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Node graphs of the effect system (pure: no GPU, no editor). One model for two kinds of graph:
//   * Material graphs  -> the pixel shader of particles and trails (colour and opacity of every pixel)
//   * Particle graphs  -> the compute shader of an emitter (what new particles start with, how they move)
// A graph is nodes + links between their pins; GenerateGraphCode turns the part that reaches the output node
// into HLSL statements. NodeLibrary::Effects() holds the node types (GraphNodes.cpp).

/// Value shape of a pin. Any = takes every shape and becomes as wide as the widest input (math nodes).
enum class PinType : uint8_t { Any = 0, Float = 1, Float2 = 2, Float3 = 3, Float4 = 4 };
const char* PinTypeName(PinType type);
const char* HlslTypeName(PinType type);
/// A Float spreads over every channel, a wider value gives its first channels; nothing else converts
bool CanConvert(PinType from, PinType to);

enum class GraphKind : uint8_t { Material, Particle };
/// Where a node may be used (NodeDef::domains)
inline constexpr uint32_t kDomainMaterial = 1u;
inline constexpr uint32_t kDomainParticleSpawn = 2u;
inline constexpr uint32_t kDomainParticleUpdate = 4u;
inline constexpr uint32_t kDomainAll = kDomainMaterial | kDomainParticleSpawn | kDomainParticleUpdate;

struct PinDef {
	std::string name;
	PinType type = PinType::Float;
	Vector4 defaultValue{ 0.0f, 0.0f, 0.0f, 0.0f }; // used while nothing is connected (editable on the node)
	std::string tooltip;
	std::string defaultExpr; // when set, an unconnected pin reads this HLSL instead of a value (e.g. "uv", "gTime")
	std::string defaultLabel; // what the editor shows for it ("Particle UV")
};

/// Curve / Gradient: keys over 0..1 (value.x of a Curve, rgba of a Gradient). Parameter: the name of one of the graph's parameters.
enum class ParamKind : uint8_t { Float, Vector3, Color, Choice, Texture, Curve, Gradient, Parameter };
/// A value set on the node itself (not a pin): constants, choices, textures
struct ParamDef {
	std::string name;
	ParamKind kind = ParamKind::Float;
	Vector4 defaultValue{ 0.0f, 0.0f, 0.0f, 0.0f };
	float minValue = 0.0f, maxValue = 0.0f; // drag limits (equal = none)
	std::vector<std::string> choices;       // Choice: the options, value.x = index
	std::string tooltip;
};

struct GraphNode {
	uint32_t id = 0;
	std::string type;
	float x = 0.0f, y = 0.0f;              // position in the editor
	std::map<std::string, Vector4> values; // parameters and the values of unconnected inputs, by name
	std::map<std::string, std::string> texts; // texture paths etc.
};

/// One key of a Curve or Gradient parameter
struct GraphKey {
	float time = 0.0f;
	Vector4 value{ 0.0f, 0.0f, 0.0f, 0.0f };
};
inline constexpr size_t kMaxGraphKeys = 8;
/// Keys of `param` sorted by time (stored as "<param> i" = value, "<param> Time i".x = time, "<param> Count".x)
std::vector<GraphKey> ReadGraphKeys(const GraphNode& node, const std::string& param);
/// Stores up to kMaxGraphKeys keys (at least one is kept)
void WriteGraphKeys(GraphNode& node, const std::string& param, const std::vector<GraphKey>& keys);

/// A value of the whole graph that each GPU Particles object (and scripts) can change: "Strength", "Tint"
struct GraphParameter {
	std::string name;
	PinType type = PinType::Float; // Float, Float3 or Float4 (a colour)
	Vector4 value{ 0.0f, 0.0f, 0.0f, 0.0f }; // the default
};
inline constexpr size_t kMaxGraphParameters = 8;

struct GraphLink {
	uint32_t fromNode = 0;
	std::string fromPin;
	uint32_t toNode = 0;
	std::string toPin;
};

class NodeLibrary;

struct NodeGraph {
	GraphKind kind = GraphKind::Material;
	std::vector<GraphNode> nodes;
	std::vector<GraphLink> links;
	std::vector<GraphParameter> parameters; // particle graphs only; slot i = parameters[i]
	uint32_t nextId = 1;

	/// Index of the parameter called `name`, -1 when there is none
	int FindParameter(const std::string& name) const;

	/// A node of `type` with its default values; 0 for an unknown type
	uint32_t AddNode(const std::string& type, float x, float y, const NodeLibrary& library);
	/// Links an output to an input (replacing the input's old link). False with a reason when the shapes do not fit,
	/// a pin does not exist, or the link would make a loop.
	bool Connect(uint32_t fromNode, const std::string& fromPin, uint32_t toNode, const std::string& toPin, const NodeLibrary& library, std::string* why);
	void Disconnect(uint32_t toNode, const std::string& toPin);
	void RemoveNode(uint32_t id);
	const GraphNode* Find(uint32_t id) const;
	GraphNode* FindMutable(uint32_t id);
	const GraphLink* LinkInto(uint32_t toNode, const std::string& toPin) const;
	/// True when `to` is `from` or depends on it through links
	bool DependsOn(uint32_t to, uint32_t from) const;

	std::string ToJson() const;
	/// Unknown node types are kept (the compiler reports them); links to missing nodes are dropped
	static bool FromJson(const std::string& text, NodeGraph& out, std::string* why);
};

/// The shape an output gives, with Any resolved from what is connected
PinType ResolvedOutputType(const NodeGraph& graph, uint32_t nodeId, const std::string& pin, const NodeLibrary& library);

/// What a node's code generator sees: input expressions (already converted to the pin's shape), output names
struct NodeEmitContext {
	const GraphNode* node = nullptr;
	std::vector<std::string> in;   // per input pin
	std::vector<std::string> out;  // variable name per output pin
	std::vector<PinType> outTypes; // resolved shape per output pin
	PinType anyType = PinType::Float; // what Any pins of this node became
	std::vector<std::string>* textures = nullptr; // texture paths of the shader; a node adds its own and gets a slot
	uint32_t domain = kDomainMaterial; // the stage being generated
	const NodeGraph* graph = nullptr;
	std::string* problem = nullptr;    // a node that cannot be generated says why here (reported on the node)

	Vector4 Value(const std::string& name) const;
	std::string Text(const std::string& name) const;
	std::string Out(size_t i) const { return out.at(i); }
	/// "float3 n4_0 = " for output i
	std::string Declare(size_t i) const;
	int AddTexture(const std::string& path) const;
};

struct NodeDef {
	std::string type;     // stable id, also the English label
	std::string category; // menu group
	std::string tooltip;  // what it does, in one sentence
	uint32_t domains = kDomainAll;
	bool isOutput = false;
	std::vector<PinDef> inputs;
	std::vector<PinDef> outputs;
	std::vector<ParamDef> params;
	/// HLSL statements that declare every output (or, for an output node, write the shader's results)
	std::function<std::string(const NodeEmitContext&)> emit;
};

class NodeLibrary {
public:
	/// Every node type of the effect system
	static const NodeLibrary& Effects();
	void Register(NodeDef def);
	const NodeDef* Find(const std::string& type) const;
	const std::vector<NodeDef>& All() const { return defs_; }

private:
	std::vector<NodeDef> defs_;
};

struct GraphError {
	uint32_t nodeId = 0; // 0 = the whole graph
	std::string message; // shown on the node in the editor
};

struct GeneratedGraphCode {
	bool ok = false;
	std::string body;                  // HLSL statements for the shader template
	std::vector<std::string> textures; // texture slot i = textures[i]
	std::vector<GraphError> errors;
};

/// The HLSL of everything that reaches the output node of `domain`. Errors name the node they belong to.
GeneratedGraphCode GenerateGraphCode(const NodeGraph& graph, const NodeLibrary& library, uint32_t domain);
/// Different textures a material may use (slot 0 = the emitter's own texture)
inline constexpr size_t kMaxGraphTextures = 4;
/// "float3(1.0, 0.5, 0.0)" for the first channels of `value`
std::string HlslLiteral(const Vector4& value, PinType type);
