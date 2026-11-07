#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Component registry (roadmap 4.1, stage 1).
//
// Every component the editor knows is described by one ComponentInfo: its name, Add Component menu entry,
// Inspector drawing and removal. Two kinds exist:
//   * Built-in components (Mesh, Colliders, Rigidbody, ...) keep their data inline in GameObject and their
//     flat JSON keys (scene format unchanged). Their callbacks are registered in EditorSuite_Components.cpp.
//   * Data components are plain structs with a field list. They live in GameObject::components and get the
//     Inspector, JSON ("components" object), undo, prefabs and Play-mode restore for free:
//
//       struct Health { float maxHp = 10.0f; float hp = 10.0f; bool invincible = false; };
//       RegisterDataComponent<Health>("Health", "Gameplay", {
//           RIZE_FIELD(Health, maxHp, ComponentFieldType::Float, 1.0f, 9999.0f),
//           RIZE_FIELD(Health, hp, ComponentFieldType::Float, 0.0f, 9999.0f),
//           RIZE_FIELD(Health, invincible, ComponentFieldType::Bool, 0.0f, 0.0f) });

struct GameObject;
class EditorSuite;

// Asset = Text holding a project-relative asset path; JSON also stores its GUID ("<field>Guid"), so renames keep it
enum class ComponentFieldType { Float, Int, Bool, Vector3, Color, Text, Asset };

struct ComponentField {
	const char* name = "";
	ComponentFieldType type = ComponentFieldType::Float;
	size_t offset = 0;
	size_t size = 0;      // bytes (Text: buffer size including the terminator)
	float minValue = 0.0f; // Float / Int drag limits (min == max: unlimited)
	float maxValue = 0.0f;
	const char* tooltip = nullptr;
};

#define RIZE_FIELD(Type, member, fieldType, minV, maxV) \
	ComponentField{ #member, fieldType, offsetof(Type, member), sizeof(Type::member), minV, maxV, nullptr }

/// Type-erased instance of a data component (value semantics through Clone)
struct ComponentData {
	virtual ~ComponentData() = default;
	virtual std::unique_ptr<ComponentData> Clone() const = 0;
	virtual void* Data() = 0;
	virtual const void* Data() const = 0;
};

template <typename T>
struct ComponentDataT final : ComponentData {
	T value{};
	std::unique_ptr<ComponentData> Clone() const override { return std::make_unique<ComponentDataT<T>>(*this); }
	void* Data() override { return &value; }
	const void* Data() const override { return &value; }
};

/// Data components attached to one GameObject. Copying deep-copies every component.
class ComponentStore {
public:
	struct Entry {
		std::string name;
		std::unique_ptr<ComponentData> data;
	};

	ComponentStore() = default;
	ComponentStore(const ComponentStore& other) { CopyFrom(other); }
	ComponentStore& operator=(const ComponentStore& other) {
		if (this != &other) CopyFrom(other);
		return *this;
	}
	ComponentStore(ComponentStore&&) noexcept = default;
	ComponentStore& operator=(ComponentStore&&) noexcept = default;

	bool Has(const std::string& name) const { return Find(name) != nullptr; }
	ComponentData* Find(const std::string& name);
	const ComponentData* Find(const std::string& name) const;
	ComponentData* Add(const std::string& name, std::unique_ptr<ComponentData> data); // replaces an existing one
	bool Remove(const std::string& name);
	void Clear() { entries_.clear(); unknown_.clear(); }
	bool Empty() const { return entries_.empty() && unknown_.empty(); }
	const std::vector<Entry>& Entries() const { return entries_; }

	template <typename T>
	T* Get(const std::string& name) {
		auto* typed = dynamic_cast<ComponentDataT<T>*>(Find(name));
		return typed ? &typed->value : nullptr;
	}
	template <typename T>
	const T* Get(const std::string& name) const {
		auto* typed = dynamic_cast<const ComponentDataT<T>*>(Find(name));
		return typed ? &typed->value : nullptr;
	}

	// Components found in a file but not registered in this build: kept as JSON text so saving does not lose them
	std::map<std::string, std::string>& Unknown() { return unknown_; }
	const std::map<std::string, std::string>& Unknown() const { return unknown_; }

private:
	void CopyFrom(const ComponentStore& other);
	std::vector<Entry> entries_;
	std::map<std::string, std::string> unknown_;
};

struct ComponentInfo {
	std::string name;           // Inspector header, JSON key of data components, Add Component label
	std::string category;       // Add Component submenu ("" = top level)
	int builtinKind = -1;       // EditorSuite::ComponentKind of built-ins, -1 for data components
	bool showInAddMenu = true;

	// Built-in callbacks (data components get default implementations from the registry)
	std::function<bool(const GameObject&)> has;
	std::function<void(EditorSuite&, GameObject&)> add;
	std::function<void(EditorSuite&, GameObject&)> drawInspector; // draws its own header
	// Extra Add Component entries for the same component (e.g. "Textured Quad" for the mesh)
	std::vector<std::pair<std::string, std::function<void(EditorSuite&, GameObject&)>>> addVariants;

	// Data components
	std::vector<ComponentField> fields;
	std::function<std::unique_ptr<ComponentData>()> create;
	/// Optional extra Inspector UI drawn below the fields of a data component (buttons, live state)
	std::function<void(EditorSuite&, GameObject&, ComponentData&)> drawExtra;

	bool IsDataComponent() const { return builtinKind < 0; }
};

class ComponentRegistry {
public:
	static ComponentRegistry& Get();

	/// Registers (or replaces, by name) a component. Order of registration = Inspector / menu order.
	ComponentInfo& Register(ComponentInfo info);
	bool Unregister(const std::string& name);
	const ComponentInfo* Find(const std::string& name) const;
	const std::vector<ComponentInfo>& All() const { return infos_; }

private:
	std::vector<ComponentInfo> infos_;
};

/// Inspector label of a field: "emitRate" -> "Emit Rate" (the key in the translation files)
inline std::string HumanizeFieldName(const char* name)
{
	std::string out;
	for (const char* c = name; c && *c; ++c) {
		const bool upper = *c >= 'A' && *c <= 'Z';
		const bool afterLower = c != name && ((c[-1] >= 'a' && c[-1] <= 'z') || (c[-1] >= '0' && c[-1] <= '9'));
		if (upper && afterLower) out += ' ';
		out += (c == name && *c >= 'a' && *c <= 'z') ? static_cast<char>(*c - 'a' + 'A') : *c;
	}
	return out;
}

/// Registers a data component type T (default-constructed T = values of a newly added component)
template <typename T>
ComponentInfo& RegisterDataComponent(const std::string& name, const std::string& category, std::vector<ComponentField> fields)
{
	ComponentInfo info;
	info.name = name;
	info.category = category;
	info.fields = std::move(fields);
	info.create = []() -> std::unique_ptr<ComponentData> { return std::make_unique<ComponentDataT<T>>(); };
	return ComponentRegistry::Get().Register(std::move(info));
}
