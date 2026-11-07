#include "NodeGraph.h"
#include <format>

// The node types of the effect graphs. Names are stable ids (saved in graph files) and the English labels.
// Every node: one sentence of tooltip, inputs with useful defaults, outputs that say what they give.

namespace {
	PinDef In(const char* name, PinType type, Vector4 value = { 0.0f, 0.0f, 0.0f, 0.0f }, const char* tooltip = "")
	{
		return { name, type, value, tooltip };
	}
	PinDef Out(const char* name, PinType type, const char* tooltip = "") { return { name, type, {}, tooltip }; }
	/// An input that, left unconnected, reads `expr` (shown as `label` in the editor)
	PinDef Auto(const char* name, PinType type, const char* expr, const char* label, const char* tooltip = "")
	{
		PinDef pin{ name, type, {}, tooltip };
		pin.defaultExpr = expr;
		pin.defaultLabel = label;
		return pin;
	}
	Vector4 F(float v) { return { v, v, v, v }; }

	/// A node with one Any result made from its inputs: `pattern` uses {0}, {1}, ... for the inputs
	NodeDef AnyMath(const char* type, const char* tooltip, std::vector<PinDef> inputs, const char* pattern)
	{
		NodeDef def;
		def.type = type;
		def.category = "Math";
		def.tooltip = tooltip;
		def.inputs = std::move(inputs);
		def.outputs = { Out("Result", PinType::Any) };
		const std::string fmt = pattern;
		def.emit = [fmt](const NodeEmitContext& c) {
			std::string expr = fmt;
			for (size_t i = 0; i < c.in.size(); ++i) {
				const std::string key = "{" + std::to_string(i) + "}";
				for (size_t at = expr.find(key); at != std::string::npos; at = expr.find(key, at + c.in[i].size())) expr.replace(at, key.size(), c.in[i]);
			}
			return c.Declare(0) + expr + ";";
		};
		return def;
	}

	void RegisterCommonNodes(NodeLibrary& lib)
	{
		// ---- Values ----
		{
			NodeDef def;
			def.type = "Float";
			def.category = "Values";
			def.tooltip = "A single number you choose.";
			def.params = { { "Value", ParamKind::Float, F(1.0f) } };
			def.outputs = { Out("Value", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + HlslLiteral(c.Value("Value"), PinType::Float) + ";"; };
			lib.Register(def);
		}
		{
			NodeDef def;
			def.type = "Vector3";
			def.category = "Values";
			def.tooltip = "Three numbers you choose (a direction or a position).";
			def.params = { { "Value", ParamKind::Vector3, { 0.0f, 1.0f, 0.0f, 0.0f } } };
			def.outputs = { Out("Value", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + HlslLiteral(c.Value("Value"), PinType::Float3) + ";"; };
			lib.Register(def);
		}
		{
			NodeDef def;
			def.type = "Color";
			def.category = "Values";
			def.tooltip = "A colour you pick. RGB is the colour, A its opacity; values above 1 glow.";
			def.params = { { "Color", ParamKind::Color, { 1.0f, 1.0f, 1.0f, 1.0f } } };
			def.outputs = { Out("RGB", PinType::Float3), Out("RGBA", PinType::Float4), Out("A", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) {
				return c.Declare(1) + HlslLiteral(c.Value("Color"), PinType::Float4) + ";\n" +
					c.Declare(0) + c.Out(1) + ".rgb;\n" + c.Declare(2) + c.Out(1) + ".a;";
			};
			lib.Register(def);
		}
		{
			NodeDef def;
			def.type = "Time";
			def.category = "Values";
			def.tooltip = "Seconds since the game started. Feed it to Sin or Panner to make things move.";
			def.outputs = { Out("Time", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "gTime;"; };
			lib.Register(def);
		}

		// ---- Math (work on any size: a Float, a colour, a direction) ----
		lib.Register(AnyMath("Add", "A + B.", { In("A", PinType::Any), In("B", PinType::Any) }, "{0} + {1}"));
		lib.Register(AnyMath("Subtract", "A - B.", { In("A", PinType::Any), In("B", PinType::Any) }, "{0} - {1}"));
		lib.Register(AnyMath("Multiply", "A x B. Use it to tint (colour x colour) or to scale.", { In("A", PinType::Any, F(1.0f)), In("B", PinType::Any, F(1.0f)) }, "{0} * {1}"));
		lib.Register(AnyMath("Divide", "A / B (B = 0 gives 0).", { In("A", PinType::Any, F(1.0f)), In("B", PinType::Any, F(1.0f)) }, "{0} * sign({1}) / max(abs({1}), 1e-6)"));
		lib.Register(AnyMath("Min", "The smaller of A and B.", { In("A", PinType::Any), In("B", PinType::Any) }, "min({0}, {1})"));
		lib.Register(AnyMath("Max", "The larger of A and B.", { In("A", PinType::Any), In("B", PinType::Any) }, "max({0}, {1})"));
		lib.Register(AnyMath("Power", "Base to the power Exponent. Above 1 makes soft gradients sharper.", { In("Base", PinType::Any, F(0.5f)), In("Exponent", PinType::Float, F(2.0f)) }, "pow(max({0}, 0), {1})"));
		lib.Register(AnyMath("Lerp", "Blends from A (Alpha 0) to B (Alpha 1).", { In("A", PinType::Any), In("B", PinType::Any, F(1.0f)), In("Alpha", PinType::Float, F(0.5f)) }, "lerp({0}, {1}, {2})"));
		lib.Register(AnyMath("Clamp", "Keeps Value between Min and Max.", { In("Value", PinType::Any), In("Min", PinType::Float, F(0.0f)), In("Max", PinType::Float, F(1.0f)) }, "clamp({0}, {1}, {2})"));
		lib.Register(AnyMath("Saturate", "Keeps Value between 0 and 1.", { In("Value", PinType::Any) }, "saturate({0})"));
		lib.Register(AnyMath("Abs", "Removes the minus sign.", { In("Value", PinType::Any) }, "abs({0})"));
		lib.Register(AnyMath("Frac", "The part after the decimal point: 2.75 gives 0.75. Repeats a 0..1 ramp.", { In("Value", PinType::Any) }, "frac({0})"));
		lib.Register(AnyMath("One Minus", "1 - Value. Turns a mask inside out.", { In("Value", PinType::Any) }, "1 - {0}"));
		lib.Register(AnyMath("Sin", "A wave between -1 and 1. Feed it Time to pulse.", { In("Value", PinType::Any) }, "sin({0})"));
		lib.Register(AnyMath("Cos", "Like Sin, a quarter wave later.", { In("Value", PinType::Any) }, "cos({0})"));
		lib.Register(AnyMath("Step", "1 where Value is at least Edge, else 0. A hard cut-off (dissolve).", { In("Edge", PinType::Float, F(0.5f)), In("Value", PinType::Any) }, "step({0}, {1})"));
		lib.Register(AnyMath("Smoothstep", "Like Step, with a soft edge from Min to Max.", { In("Min", PinType::Float, F(0.0f)), In("Max", PinType::Float, F(1.0f)), In("Value", PinType::Any) }, "smoothstep({0}, {1}, {2})"));
		lib.Register(AnyMath("Remap", "Moves Value from the In range to the Out range.",
			{ In("Value", PinType::Any), In("In Min", PinType::Float, F(0.0f)), In("In Max", PinType::Float, F(1.0f)), In("Out Min", PinType::Float, F(0.0f)), In("Out Max", PinType::Float, F(1.0f)) },
			"{3} + ({0} - {1}) * ({4} - {3}) * (abs({2} - {1}) > 1e-6 ? 1.0 / ({2} - {1}) : 0.0)"));

		// ---- Vectors ----
		{
			NodeDef def;
			def.type = "Length";
			def.category = "Vector";
			def.tooltip = "How long a vector is (distance from zero).";
			def.inputs = { In("Value", PinType::Any) };
			def.outputs = { Out("Result", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "length(" + c.in[0] + ");"; };
			lib.Register(def);
		}
		lib.Register([] {
			NodeDef def = AnyMath("Normalize", "The same direction with length 1 (zero stays zero).", { In("Value", PinType::Any, { 0.0f, 1.0f, 0.0f, 0.0f }) }, "({0}) * (length({0}) > 1e-6 ? 1.0 / length({0}) : 0.0)");
			def.category = "Vector";
			return def;
		}());
		{
			NodeDef def;
			def.type = "Dot";
			def.category = "Vector";
			def.tooltip = "How much A points along B (1 = same way, 0 = across, -1 = opposite, for length-1 vectors).";
			def.inputs = { In("A", PinType::Any), In("B", PinType::Any) };
			def.outputs = { Out("Result", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "dot(" + c.in[0] + ", " + c.in[1] + ");"; };
			lib.Register(def);
		}
		{
			NodeDef def;
			def.type = "Distance";
			def.category = "Vector";
			def.tooltip = "How far apart A and B are.";
			def.inputs = { In("A", PinType::Any), In("B", PinType::Any) };
			def.outputs = { Out("Result", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "distance(" + c.in[0] + ", " + c.in[1] + ");"; };
			lib.Register(def);
		}
		{
			NodeDef def;
			def.type = "Cross";
			def.category = "Vector";
			def.tooltip = "A direction at right angles to both A and B.";
			def.inputs = { In("A", PinType::Float3, { 1.0f, 0.0f, 0.0f, 0.0f }), In("B", PinType::Float3, { 0.0f, 1.0f, 0.0f, 0.0f }) };
			def.outputs = { Out("Result", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "cross(" + c.in[0] + ", " + c.in[1] + ");"; };
			lib.Register(def);
		}
		{
			NodeDef def;
			def.type = "Split";
			def.category = "Vector";
			def.tooltip = "Takes a colour or vector apart into its channels (missing channels give 0).";
			def.inputs = { In("Value", PinType::Any) };
			def.outputs = { Out("R", PinType::Float), Out("G", PinType::Float), Out("B", PinType::Float), Out("A", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) {
				static const char* pad[] = { "", "float4({0}, 0, 0, 0)", "float4({0}, 0, 0)", "float4({0}, 0)", "{0}" };
				std::string v = pad[static_cast<int>(c.anyType)];
				v.replace(v.find("{0}"), 3, c.in[0]);
				const std::string tmp = c.Out(0) + "_v";
				return "float4 " + tmp + " = " + v + ";\n" + c.Declare(0) + tmp + ".x;\n" + c.Declare(1) + tmp + ".y;\n" + c.Declare(2) + tmp + ".z;\n" + c.Declare(3) + tmp + ".w;";
			};
			lib.Register(def);
		}
		{
			NodeDef def;
			def.type = "Combine";
			def.category = "Vector";
			def.tooltip = "Builds a vector or colour from single channels.";
			def.inputs = { In("R", PinType::Float), In("G", PinType::Float), In("B", PinType::Float), In("A", PinType::Float, F(1.0f)) };
			def.outputs = { Out("RG", PinType::Float2), Out("RGB", PinType::Float3), Out("RGBA", PinType::Float4) };
			def.emit = [](const NodeEmitContext& c) {
				return c.Declare(2) + "float4(" + c.in[0] + ", " + c.in[1] + ", " + c.in[2] + ", " + c.in[3] + ");\n" +
					c.Declare(0) + c.Out(2) + ".xy;\n" + c.Declare(1) + c.Out(2) + ".xyz;";
			};
			lib.Register(def);
		}
	}

	NodeDef MaterialNode(const char* type, const char* category, const char* tooltip)
	{
		NodeDef def;
		def.type = type;
		def.category = category;
		def.tooltip = tooltip;
		def.domains = kDomainMaterial;
		return def;
	}

	void RegisterMaterialNodes(NodeLibrary& lib)
	{
		// ---- What the pixel knows ----
		{
			NodeDef def = MaterialNode("UV", "Particle", "Where the pixel is on the particle: (0,0) top left, (1,1) bottom right.");
			def.outputs = { Out("UV", PinType::Float2) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "uv;"; };
			lib.Register(def);
		}
		{
			NodeDef def = MaterialNode("Particle Color", "Particle", "The particle's colour from its GPU Particles settings (Color Start to Color End).");
			def.domains = kDomainAll;
			def.outputs = { Out("RGB", PinType::Float3), Out("A", PinType::Float), Out("RGBA", PinType::Float4) };
			def.emit = [](const NodeEmitContext& c) {
				const char* v = c.domain == kDomainMaterial ? "vertexColor" : "color";
				return c.Declare(0) + std::format("{}.rgb;\n", v) + c.Declare(1) + std::format("{}.a;\n", v) + c.Declare(2) + v + ";";
			};
			lib.Register(def);
		}
		{
			NodeDef def = MaterialNode("Particle Age", "Particle", "0 when the particle is born, 1 when it dies.");
			def.domains = kDomainAll;
			def.outputs = { Out("Age", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "particleAge;"; };
			lib.Register(def);
		}
		{
			NodeDef def = MaterialNode("Particle Random", "Particle", "A number from 0 to 1, different for every particle. Use it so they do not all look the same.");
			def.domains = kDomainAll;
			def.outputs = { Out("Random", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "particleRandom;"; };
			lib.Register(def);
		}

		// ---- Textures and UV ----
		{
			NodeDef def = MaterialNode("Texture Sample", "Texture", "Reads a texture. No texture chosen = the GPU Particles' own Texture.");
			def.params = { { "Texture", ParamKind::Texture, {}, 0.0f, 0.0f, {}, "The picture to read. Empty = the emitter's own texture." },
				{ "Wrap", ParamKind::Choice, { 0.0f, 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f, { "Clamp", "Repeat" }, "Repeat when the UV scrolls (Panner) or tiles." } };
			def.inputs = { Auto("UV", PinType::Float2, "uv", "Particle UV") };
			def.outputs = { Out("RGB", PinType::Float3), Out("A", PinType::Float), Out("RGBA", PinType::Float4) };
			def.emit = [](const NodeEmitContext& c) {
				const int slot = c.AddTexture(c.Text("Texture"));
				const char* sampler = c.Value("Wrap").x >= 0.5f ? "gWrapSampler" : "gSampler";
				return c.Declare(2) + std::format("gTexture{}.Sample({}, {});\n", (std::min)(slot, 3), sampler, c.in[0]) +
					c.Declare(0) + c.Out(2) + ".rgb;\n" + c.Declare(1) + c.Out(2) + ".a;";
			};
			lib.Register(def);
		}
		{
			NodeDef def = MaterialNode("Panner", "Texture", "Slides the UV over time: scrolling fire, flowing energy. Set the texture to Repeat.");
			def.inputs = { Auto("UV", PinType::Float2, "uv", "Particle UV"), In("Speed", PinType::Float2, { 0.0f, -0.5f, 0.0f, 0.0f }, "How far per second (1 = the whole texture)."),
				Auto("Time", PinType::Float, "gTime", "Game Time") };
			def.outputs = { Out("UV", PinType::Float2) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + c.in[0] + " + " + c.in[1] + " * " + c.in[2] + ";"; };
			lib.Register(def);
		}
		{
			NodeDef def = MaterialNode("Rotator", "Texture", "Turns the UV around Center over time: spinning rings and portals.");
			def.inputs = { Auto("UV", PinType::Float2, "uv", "Particle UV"), In("Center", PinType::Float2, { 0.5f, 0.5f, 0.0f, 0.0f }),
				In("Turns per Second", PinType::Float, F(0.25f)), Auto("Time", PinType::Float, "gTime", "Game Time") };
			def.outputs = { Out("UV", PinType::Float2) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "GraphRotate(" + c.in[0] + ", " + c.in[1] + ", " + c.in[2] + " * " + c.in[3] + " * 6.2831853);"; };
			lib.Register(def);
		}
		{
			NodeDef def = MaterialNode("Flipbook", "Texture", "Plays a sprite sheet (Columns x Rows pictures) over the particle's life.");
			def.inputs = { Auto("UV", PinType::Float2, "uv", "Particle UV"), In("Columns", PinType::Float, F(4.0f)), In("Rows", PinType::Float, F(4.0f)),
				Auto("Progress", PinType::Float, "particleAge", "Particle Age", "0 = first picture, 1 = last.") };
			def.outputs = { Out("UV", PinType::Float2) };
			def.emit = [](const NodeEmitContext& c) {
				return c.Declare(0) + "GraphFlipbook(" + c.in[0] + ", " + c.in[1] + ", " + c.in[2] + ", " + c.in[3] + " * max(floor(" + c.in[1] + "), 1) * max(floor(" + c.in[2] + "), 1));";
			};
			lib.Register(def);
		}
		{
			NodeDef def = MaterialNode("Polar Coordinates", "Texture", "Turns the square UV into a circle: Angle goes around, Distance goes out from the centre.");
			def.inputs = { Auto("UV", PinType::Float2, "uv", "Particle UV"), In("Center", PinType::Float2, { 0.5f, 0.5f, 0.0f, 0.0f }) };
			def.outputs = { Out("UV", PinType::Float2), Out("Angle", PinType::Float), Out("Distance", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) {
				return c.Declare(0) + "GraphPolar(" + c.in[0] + ", " + c.in[1] + ");\n" + c.Declare(1) + c.Out(0) + ".x;\n" + c.Declare(2) + c.Out(0) + ".y;";
			};
			lib.Register(def);
		}

		// ---- Shapes and masks ----
		{
			NodeDef def = MaterialNode("Noise", "Shape", "Soft random clouds from 0 to 1. Scale = how many blobs across the particle.");
			def.inputs = { Auto("UV", PinType::Float2, "uv", "Particle UV"), In("Scale", PinType::Float, F(6.0f)) };
			def.outputs = { Out("Noise", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "GraphFractalNoise(" + c.in[0] + " * " + c.in[1] + ");"; };
			lib.Register(def);
		}
		{
			NodeDef def = MaterialNode("Radial Gradient", "Shape", "1 in the middle, fading to 0 at Radius. Softness 0 = a hard circle.");
			def.inputs = { Auto("UV", PinType::Float2, "uv", "Particle UV"), In("Center", PinType::Float2, { 0.5f, 0.5f, 0.0f, 0.0f }),
				In("Radius", PinType::Float, F(0.5f)), In("Softness", PinType::Float, F(0.5f)) };
			def.outputs = { Out("Mask", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) {
				return c.Declare(0) + "1.0 - smoothstep(" + c.in[2] + " * (1.0 - saturate(" + c.in[3] + ")) - 1e-4, " + c.in[2] + ", distance(" + c.in[0] + ", " + c.in[1] + "));";
			};
			lib.Register(def);
		}
		{
			NodeDef def = MaterialNode("Dissolve", "Shape", "Eats the particle away where Mask is below Amount (feed it Noise). Edge is a thin band to make glow.");
			def.inputs = { In("Mask", PinType::Float, F(0.5f), "Where it disappears first (low values go first)."),
				Auto("Amount", PinType::Float, "particleAge", "Particle Age", "0 = whole, 1 = gone."),
				In("Edge Width", PinType::Float, F(0.05f)) };
			def.outputs = { Out("Opacity", PinType::Float), Out("Edge", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) {
				return c.Declare(0) + "step(" + c.in[1] + ", " + c.in[0] + ");\n" +
					c.Declare(1) + c.Out(0) + " - step(" + c.in[1] + " + " + c.in[2] + ", " + c.in[0] + ");";
			};
			lib.Register(def);
		}
	}

	constexpr uint32_t kDomainParticle = kDomainParticleSpawn | kDomainParticleUpdate;

	NodeDef ParticleNode(const char* type, const char* category, const char* tooltip, uint32_t domains)
	{
		NodeDef def;
		def.type = type;
		def.category = category;
		def.tooltip = tooltip;
		def.domains = domains;
		return def;
	}

	PinDef CurrentVelocity() { return Auto("Velocity", PinType::Float3, "velocity", "Current Velocity", "The velocity so far (chain forces one after another)."); }

	/// Piecewise linear over the sorted keys of `param`, evaluated at `t` (0..1)
	std::string KeyedValue(const NodeEmitContext& c, const std::string& param, PinType type, const std::string& t, const std::string& name)
	{
		const std::vector<GraphKey> keys = ReadGraphKeys(*c.node, param);
		const char* hlsl = HlslTypeName(type);
		std::string code = std::format("const float {0}_t = saturate({1});\n{2} {0} = {3};\n", name, t, hlsl, HlslLiteral(keys[0].value, type));
		for (size_t i = 1; i < keys.size(); ++i) {
			code += std::format("{0} = {0}_t >= {1} ? lerp({2}, {3}, saturate(({0}_t - {1}) / {4})) : {0};\n", name, HlslLiteral(Vector4{ keys[i - 1].time, 0, 0, 0 }, PinType::Float),
				HlslLiteral(keys[i - 1].value, type), HlslLiteral(keys[i].value, type), HlslLiteral(Vector4{ (std::max)(keys[i].time - keys[i - 1].time, 1.0e-4f), 0, 0, 0 }, PinType::Float));
		}
		return code;
	}

	void RegisterParticleNodes(NodeLibrary& lib)
	{
		// ---- What a particle knows ----
		{
			NodeDef def = ParticleNode("Particle Position", "Particle", "Where the particle is now (world space).", kDomainParticle);
			def.outputs = { Out("Position", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "position;"; };
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Particle Velocity", "Particle", "Which way and how fast the particle flies (units per second).", kDomainParticle);
			def.outputs = { Out("Velocity", PinType::Float3), Out("Speed", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "velocity;\n" + c.Declare(1) + "length(velocity);"; };
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Particle Lifetime", "Particle", "How many seconds the particle has lived, and how long it lives in all.", kDomainParticleUpdate);
			def.outputs = { Out("Age (s)", PinType::Float), Out("Lifetime (s)", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "age;\n" + c.Declare(1) + "life;"; };
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Particle Size", "Particle", "The particle's size now (what Spawn gave it, or the last Update).", kDomainParticleUpdate);
			def.outputs = { Out("Size", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "size;"; };
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Delta Time", "Values", "Seconds since the last frame. Multiply a speed by it to get a step.", kDomainParticle);
			def.outputs = { Out("Seconds", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "gDeltaTime;"; };
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Emitter", "Particle", "The GPU Particles object: its position and the direction it points (its Direction, turned with the object).", kDomainParticle);
			def.outputs = { Out("Position", PinType::Float3), Out("Direction", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + "gOrigin;\n" + c.Declare(1) + "gDirection;"; };
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Parameter", "Values", "A value of this effect that each object and scripts can change (SetEffectParameter). Make them in the Parameters list.", kDomainParticle);
			def.params = { { "Name", ParamKind::Parameter, {}, 0.0f, 0.0f, {}, "Which parameter of this graph to read." } };
			def.outputs = { Out("Value", PinType::Float4, "A number fills every channel; a vector or colour gives its channels.") };
			def.emit = [](const NodeEmitContext& c) {
				const std::string name = c.Text("Name");
				const int index = c.graph ? c.graph->FindParameter(name) : -1;
				if (index < 0) {
					if (c.problem) *c.problem = name.empty() ? "Choose a parameter (add one in the Parameters list first)." : std::format("There is no parameter called \"{}\" any more.", name);
					return c.Declare(0) + "0;";
				}
				const PinType type = c.graph->parameters[index].type;
				const std::string slot = std::format("gParams[{}]", index);
				if (type == PinType::Float) return c.Declare(0) + slot + ".xxxx;";
				if (type == PinType::Float3) return c.Declare(0) + "float4(" + slot + ".xyz, 0);";
				return c.Declare(0) + slot + ";";
			};
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Random Range", "Values", "A random value between Min and Max, different for every particle (and fixed for its whole life).", kDomainParticle);
			def.inputs = { In("Min", PinType::Any), In("Max", PinType::Any, F(1.0f)) };
			def.outputs = { Out("Value", PinType::Any) };
			def.emit = [](const NodeEmitContext& c) {
				return c.Declare(0) + std::format("lerp({}, {}, ({})GraphRandom4(seed, {}u));", c.in[0], c.in[1], HlslTypeName(c.anyType), c.node->id);
			};
			lib.Register(def);
		}

		// ---- Where new particles start ----
		{
			NodeDef def = ParticleNode("Sphere Shape", "Spawn Shape", "A random point in a ball (or on its surface). Direction points away from the centre: use it as the velocity for a burst.", kDomainParticleSpawn);
			def.params = { { "Fill", ParamKind::Choice, {}, 0.0f, 0.0f, { "Volume", "Surface" }, "Volume = anywhere inside, Surface = only on the outside." } };
			def.inputs = { In("Radius", PinType::Float, F(0.5f)), Auto("Center", PinType::Float3, "gOrigin", "Emitter Position") };
			def.outputs = { Out("Position", PinType::Float3), Out("Direction", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) {
				const uint32_t id = c.node->id;
				const std::string distance = c.Value("Fill").x >= 0.5f ? c.in[0] : std::format("{} * pow(GraphRandom(seed, {}u), 1.0 / 3.0)", c.in[0], id * 4u + 2u);
				return c.Declare(1) + std::format("GraphUnitSphere(GraphRandom(seed, {}u), GraphRandom(seed, {}u));\n", id * 4u, id * 4u + 1u) +
					c.Declare(0) + c.in[1] + " + " + c.Out(1) + " * " + distance + ";";
			};
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Box Shape", "Spawn Shape", "A random point in a box of this Size around Center.", kDomainParticleSpawn);
			def.inputs = { In("Size", PinType::Float3, { 1.0f, 1.0f, 1.0f, 0.0f }), Auto("Center", PinType::Float3, "gOrigin", "Emitter Position") };
			def.outputs = { Out("Position", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) {
				return c.Declare(0) + std::format("{} + ((float3)GraphRandom4(seed, {}u) - 0.5) * {};", c.in[1], c.node->id, c.in[0]);
			};
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Ring Shape", "Spawn Shape", "A random point on a circle around Axis. Direction points outwards: a shockwave.", kDomainParticleSpawn);
			def.inputs = { In("Radius", PinType::Float, F(1.0f)), Auto("Center", PinType::Float3, "gOrigin", "Emitter Position"), Auto("Axis", PinType::Float3, "gDirection", "Emitter Direction") };
			def.outputs = { Out("Position", PinType::Float3), Out("Direction", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) {
				const std::string t = c.Out(0) + "_t", b = c.Out(0) + "_b", a = c.Out(0) + "_a";
				return std::format("float3 {0}, {1};\nGraphTangents(GraphSafeNormalize({2}, float3(0, 1, 0)), {0}, {1});\nconst float {3} = GraphRandom(seed, {4}u) * 6.2831853;\n", t, b, c.in[2], a, c.node->id) +
					c.Declare(1) + std::format("{} * cos({}) + {} * sin({});\n", t, a, b, a) + c.Declare(0) + c.in[1] + " + " + c.Out(1) + " * " + c.in[0] + ";";
			};
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Cone Direction", "Spawn Shape", "A random direction at most Angle degrees away from Direction (0 = a straight line, 180 = every way).", kDomainParticle);
			def.inputs = { Auto("Direction", PinType::Float3, "gDirection", "Emitter Direction"), In("Angle", PinType::Float, F(30.0f)) };
			def.outputs = { Out("Direction", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) {
				const uint32_t id = c.node->id;
				return c.Declare(0) + std::format("GraphConeDirection({}, {}, GraphRandom(seed, {}u), GraphRandom(seed, {}u));", c.in[0], c.in[1], id * 2u, id * 2u + 1u);
			};
			lib.Register(def);
		}

		// ---- Forces: each takes the velocity so far and gives the new one ----
		{
			NodeDef def = ParticleNode("Gravity", "Forces", "Pulls every particle the same way (default: down, like the real world).", kDomainParticleUpdate);
			def.inputs = { CurrentVelocity(), In("Acceleration", PinType::Float3, { 0.0f, -9.8f, 0.0f, 0.0f }) };
			def.outputs = { Out("Velocity", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + c.in[0] + " + " + c.in[1] + " * gDeltaTime;"; };
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Drag", "Forces", "Air resistance: slows particles down. 1 = loses most of its speed in about a second.", kDomainParticleUpdate);
			def.inputs = { CurrentVelocity(), In("Amount", PinType::Float, F(1.0f)) };
			def.outputs = { Out("Velocity", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + c.in[0] + " * saturate(1.0 - max(" + c.in[1] + ", 0) * gDeltaTime);"; };
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Wind", "Forces", "Particles drift towards the wind's velocity. Strength = how quickly they catch it.", kDomainParticleUpdate);
			def.inputs = { CurrentVelocity(), In("Wind Velocity", PinType::Float3, { 2.0f, 0.0f, 0.0f, 0.0f }), In("Strength", PinType::Float, F(1.0f)) };
			def.outputs = { Out("Velocity", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) { return c.Declare(0) + std::format("{0} + ({1} - {0}) * saturate(max({2}, 0) * gDeltaTime);", c.in[0], c.in[1], c.in[2]); };
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Vortex", "Forces", "Spins particles around an axis through Center: tornados, magic circles. Pull draws them towards the axis.", kDomainParticleUpdate);
			def.inputs = { CurrentVelocity(), Auto("Center", PinType::Float3, "gOrigin", "Emitter Position"), In("Axis", PinType::Float3, { 0.0f, 1.0f, 0.0f, 0.0f }),
				In("Spin", PinType::Float, F(3.0f), "How hard it turns them (negative = the other way)."), In("Pull", PinType::Float, F(0.0f)) };
			def.outputs = { Out("Velocity", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) {
				const std::string axis = c.Out(0) + "_axis", r = c.Out(0) + "_r";
				return std::format("const float3 {0} = GraphSafeNormalize({1}, float3(0, 1, 0));\nfloat3 {2} = position - {3};\n{2} -= {0} * dot({2}, {0});\n", axis, c.in[2], r, c.in[1]) +
					c.Declare(0) + std::format("{} + (cross({}, {}) * {} - {} * {}) * gDeltaTime;", c.in[0], axis, r, c.in[3], r, c.in[4]);
			};
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Curl Noise", "Forces", "Swirls particles along a smooth random flow: smoke, magic dust. Frequency = how small the swirls are.", kDomainParticleUpdate);
			def.inputs = { CurrentVelocity(), In("Strength", PinType::Float, F(3.0f)), In("Frequency", PinType::Float, F(1.0f)), In("Change Speed", PinType::Float, F(0.3f), "How fast the flow itself changes over time.") };
			def.outputs = { Out("Velocity", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) {
				return c.Declare(0) + std::format("{} + GraphCurlNoise(position * {} + gTime * {}) * {} * gDeltaTime;", c.in[0], c.in[2], c.in[3], c.in[1]);
			};
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Point Attractor", "Forces", "Pulls particles towards Target (negative Strength pushes them away). Radius 0 = from any distance.", kDomainParticleUpdate);
			def.inputs = { CurrentVelocity(), Auto("Target", PinType::Float3, "gOrigin", "Emitter Position"), In("Strength", PinType::Float, F(5.0f)), In("Radius", PinType::Float, F(0.0f)) };
			def.outputs = { Out("Velocity", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) {
				const std::string d = c.Out(0) + "_d";
				return std::format("const float3 {} = {} - position;\n", d, c.in[1]) +
					c.Declare(0) + std::format("{0} + GraphSafeNormalize({1}, 0) * {2} * gDeltaTime * (({3} <= 0 || length({1}) < {3}) ? 1.0 : 0.0);", c.in[0], d, c.in[2], c.in[3]);
			};
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Floor Collision", "Forces", "Particles bounce off a flat floor at Height (world Y) instead of falling through.", kDomainParticleUpdate);
			def.inputs = { CurrentVelocity(), In("Height", PinType::Float, F(0.0f)), In("Bounce", PinType::Float, F(0.3f), "0 = stops, 1 = keeps its speed.") };
			def.outputs = { Out("Velocity", PinType::Float3) };
			def.emit = [](const NodeEmitContext& c) {
				const std::string v = c.Out(0);
				return c.Declare(0) + c.in[0] + ";\n" +
					std::format("if (position.y + {0}.y * gDeltaTime < {1})\n{{\nif ({0}.y < 0) {{ {0}.y *= -saturate({2}); {0}.xz *= 0.8; }}\nfloorClamp = max(floorClamp, {1});\n}}", v, c.in[1], c.in[2]);
			};
			lib.Register(def);
		}

		// ---- Over the particle's life ----
		{
			NodeDef def = ParticleNode("Curve", "Over Life", "A value that changes over the particle's life, drawn as keys (size over life, fade out).", kDomainAll);
			def.params = { { "Curve", ParamKind::Curve, { 1.0f, 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f, {}, "Time 0 = born, 1 = dies. Add keys to shape it." } };
			def.inputs = { Auto("Time", PinType::Float, "particleAge", "Particle Age") };
			def.outputs = { Out("Value", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) { return KeyedValue(c, "Curve", PinType::Float, c.in[0], c.Out(0)); };
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Gradient", "Over Life", "A colour that changes over the particle's life (fire: yellow, red, smoke).", kDomainAll);
			def.params = { { "Gradient", ParamKind::Gradient, { 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, 0.0f, {}, "Time 0 = born, 1 = dies. Add keys for more colours." } };
			def.inputs = { Auto("Time", PinType::Float, "particleAge", "Particle Age") };
			def.outputs = { Out("RGBA", PinType::Float4), Out("RGB", PinType::Float3), Out("A", PinType::Float) };
			def.emit = [](const NodeEmitContext& c) {
				return KeyedValue(c, "Gradient", PinType::Float4, c.in[0], c.Out(0)) + c.Declare(1) + c.Out(0) + ".rgb;\n" + c.Declare(2) + c.Out(0) + ".a;";
			};
			lib.Register(def);
		}
	}

	void RegisterParticleOutputs(NodeLibrary& lib)
	{
		{
			NodeDef def = ParticleNode("Spawn Output", "Output", "What every new particle starts with. Runs once, when it is born.", kDomainParticleSpawn);
			def.isOutput = true;
			def.inputs = { Auto("Position", PinType::Float3, "gOrigin", "Emitter Position"),
				Auto("Velocity", PinType::Float3, "gDirection", "Emitter Direction", "Units per second. Use a Spawn Shape's Direction x a speed for a burst."),
				In("Lifetime", PinType::Float, F(1.0f), "Seconds until it disappears."),
				In("Color", PinType::Float4, { 1.0f, 1.0f, 1.0f, 1.0f }, "Above 1 glows. The material sees it as the particle colour."),
				In("Size", PinType::Float, F(0.2f)), In("Rotation", PinType::Float, F(0.0f), "Degrees.") };
			def.emit = [](const NodeEmitContext& c) {
				const std::string o = std::format("o{}_", c.node->id);
				return std::format("const float3 {0}p = {1};\nconst float3 {0}v = {2};\nconst float {0}l = {3};\nconst float4 {0}c = {4};\nconst float {0}s = {5};\nconst float {0}r = {6};\n"
					"position = {0}p;\nvelocity = {0}v;\nlife = {0}l;\ncolor = {0}c;\nsize = {0}s;\nrotation = {0}r;", o, c.in[0], c.in[1], c.in[2], c.in[3], c.in[4], c.in[5]);
			};
			lib.Register(def);
		}
		{
			NodeDef def = ParticleNode("Update Output", "Output", "Runs every frame for every living particle: its new velocity, colour, size; Kill removes it.", kDomainParticleUpdate);
			def.isOutput = true;
			def.inputs = { Auto("Velocity", PinType::Float3, "velocity", "Current Velocity", "The particle moves by this every second."),
				Auto("Color", PinType::Float4, "color", "Current Color"), Auto("Size", PinType::Float, "size", "Current Size"),
				Auto("Rotation", PinType::Float, "rotation", "Current Rotation", "Degrees."),
				In("Kill", PinType::Float, F(0.0f), "Above 0.5 removes the particle now (Step makes a good switch).") };
			def.emit = [](const NodeEmitContext& c) {
				const std::string o = std::format("o{}_", c.node->id);
				return std::format("const float3 {0}v = {1};\nconst float4 {0}c = {2};\nconst float {0}s = {3};\nconst float {0}r = {4};\nconst float {0}k = {5};\n"
					"velocity = {0}v;\ncolor = {0}c;\nsize = {0}s;\nrotation = {0}r;\nkill = {0}k;", o, c.in[0], c.in[1], c.in[2], c.in[3], c.in[4]);
			};
			lib.Register(def);
		}
	}

	void RegisterMaterialOutput(NodeLibrary& lib)
	{
		NodeDef def;
		def.type = "Material Output";
		def.category = "Output";
		def.tooltip = "What every pixel finally shows: its colour (above 1 glows through the bloom) and opacity.";
		def.domains = kDomainMaterial;
		def.isOutput = true;
		// Left unconnected they keep what the emitter shows without a material (its texture x its colour),
		// so a new material looks the same and only the parts that are connected change
		def.inputs = { Auto("Emissive Color", PinType::Float3, "baseColor.rgb", "Emitter Look", "The colour of the pixel. Values above 1 glow."),
			Auto("Opacity", PinType::Float, "baseColor.a", "Emitter Look", "0 = invisible, 1 = solid. Additive materials use it as brightness.") };
		def.emit = [](const NodeEmitContext& c) { return "emissive = " + c.in[0] + ";\nopacity = " + c.in[1] + ";"; };
		lib.Register(def);
	}
}

const NodeLibrary& NodeLibrary::Effects()
{
	static const NodeLibrary library = [] {
		NodeLibrary lib;
		RegisterCommonNodes(lib);
		RegisterMaterialNodes(lib);
		RegisterMaterialOutput(lib);
		RegisterParticleNodes(lib);
		RegisterParticleOutputs(lib);
		return lib;
	}();
	return library;
}
