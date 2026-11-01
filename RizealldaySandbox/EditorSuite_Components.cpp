#include "EditorSuiteInternal.h"
#include "SceneJson.h"

// Component registry glue (roadmap 4.1): built-in registrations, the Inspector / Add Component loop,
// by-name add/remove and the JSON of data components. See ComponentRegistry.h.

void EditorSuite::RegisterBuiltinComponents()
{
	ComponentRegistry& registry = ComponentRegistry::Get();
	if (registry.Find("Mesh Filter & Renderer")) return; // already registered (several EditorSuites / tests)

	auto builtin = [&](const char* name, const char* category, ComponentKind kind,
		std::function<bool(const GameObject&)> has, std::function<void(EditorSuite&, GameObject&)> add,
		std::function<void(EditorSuite&, GameObject&)> draw) -> ComponentInfo& {
		ComponentInfo info;
		info.name = name;
		info.category = category;
		info.builtinKind = static_cast<int>(kind);
		info.has = std::move(has);
		info.add = std::move(add);
		info.drawInspector = std::move(draw);
		return registry.Register(std::move(info));
	};

	builtin("Mesh Filter & Renderer", "Rendering", ComponentKind::Mesh,
		[](const GameObject& o) { return o.hasMesh; },
		[](EditorSuite&, GameObject& o) { o.hasMesh = true; o.meshType = GameObject::MeshType::Cube; },
		[](EditorSuite& s, GameObject& o) { s.DrawMeshComponentInspector(o); })
		.addVariants.push_back({ "Textured Quad", [](EditorSuite&, GameObject& o) { o.hasMesh = true; o.meshType = GameObject::MeshType::Quad; } });
	builtin("Box Collider", "Physics", ComponentKind::BoxCollider,
		[](const GameObject& o) { return o.hasBoxCollider; },
		[](EditorSuite&, GameObject& o) { o.hasBoxCollider = true; },
		[](EditorSuite& s, GameObject& o) { s.DrawBoxColliderComponentInspector(o); });
	builtin("Sphere Collider", "Physics", ComponentKind::SphereCollider,
		[](const GameObject& o) { return o.hasSphereCollider; },
		[](EditorSuite&, GameObject& o) { o.hasSphereCollider = true; },
		[](EditorSuite& s, GameObject& o) { s.DrawSphereColliderComponentInspector(o); });
	builtin("Rigidbody", "Physics", ComponentKind::Rigidbody,
		[](const GameObject& o) { return o.hasRigidbody; },
		[](EditorSuite&, GameObject& o) { o.hasRigidbody = true; o.rbEnabled = true; },
		[](EditorSuite& s, GameObject& o) { s.DrawRigidbodyComponentInspector(o); });
	builtin("Camera", "Rendering", ComponentKind::Camera,
		[](const GameObject& o) { return o.hasCamera; },
		[](EditorSuite&, GameObject& o) { o.hasCamera = true; },
		[](EditorSuite& s, GameObject& o) { s.DrawCameraComponentInspector(o); });
	builtin("Light", "Rendering", ComponentKind::Light,
		[](const GameObject& o) { return o.hasLight; },
		[](EditorSuite&, GameObject& o) { o.hasLight = true; },
		[](EditorSuite& s, GameObject& o) { s.DrawLightComponentInspector(o); });
	builtin("Particle System", "Effects", ComponentKind::ParticleSystem,
		[](const GameObject& o) { return o.hasParticleSystem; },
		[](EditorSuite& s, GameObject& o) { o.hasParticleSystem = true; s.EnsureParticleQuadInitialized(o); },
		[](EditorSuite& s, GameObject& o) { s.DrawParticleSystemComponentInspector(o); });
	builtin("Mapchip Level Editor", "", ComponentKind::Mapchip,
		[](const GameObject& o) { return o.hasMapchip || o.type == GameObjectType::MapchipGrid; },
		[](EditorSuite&, GameObject& o) { o.hasMapchip = true; },
		[](EditorSuite& s, GameObject&) { s.DrawMapchipInspectorSection(); })
		.showInAddMenu = false; // created from GameObject > Mapchip Grid
	builtin("Audio Source", "Audio", ComponentKind::AudioSource,
		[](const GameObject& o) { return o.hasAudioSource || o.type == GameObjectType::AudioSource; },
		[](EditorSuite&, GameObject& o) { o.hasAudioSource = true; },
		[](EditorSuite& s, GameObject& o) { s.DrawAudioSourceComponentInspector(o); });

	// Data components owned by engine systems
	RegisterAnimatorComponent();
	RegisterPhysicsComponents();
	RegisterMaterialComponent();
	RegisterVfxComponents();
}

// ---------------------------------------------------------------------------
// By-name access
// ---------------------------------------------------------------------------
bool EditorSuite::HasComponentByName(const GameObject& obj, const std::string& name) const
{
	const ComponentInfo* info = ComponentRegistry::Get().Find(name);
	if (!info) return false;
	if (info->IsDataComponent()) return obj.components.Has(name);
	return info->has && info->has(obj);
}

bool EditorSuite::AddComponentByName(GameObject& obj, const std::string& name)
{
	const ComponentInfo* info = ComponentRegistry::Get().Find(name);
	if (!info || HasComponentByName(obj, name)) return false;
	if (info->IsDataComponent()) {
		if (!info->create) return false;
		obj.components.Add(name, info->create());
	} else if (info->add) {
		info->add(*this, obj);
	} else {
		return false;
	}
	isSceneDirty_ = true;
	PushUndoSnapshot();
	return true;
}

bool EditorSuite::RemoveComponentByName(GameObject& obj, const std::string& name)
{
	const ComponentInfo* info = ComponentRegistry::Get().Find(name);
	if (!info || info->IsDataComponent()) {
		// Data component, or one stored as unknown JSON because this build does not register it
		if (!obj.components.Remove(name)) return false;
		isSceneDirty_ = true;
		PushUndoSnapshot();
		LogMessage(std::format("[Inspector] Removed {} from '{}'.", name, obj.name), 0);
		return true;
	}
	if (!info->has || !info->has(obj)) return false;
	RemoveComponent(obj, static_cast<ComponentKind>(info->builtinKind)); // records undo + log
	return true;
}

// ---------------------------------------------------------------------------
// Inspector
// ---------------------------------------------------------------------------
void EditorSuite::DrawComponentInspectors(GameObject& obj)
{
#ifdef USE_IMGUI
	for (const ComponentInfo& info : ComponentRegistry::Get().All()) {
		if (info.IsDataComponent()) {
			if (obj.components.Has(info.name)) DrawDataComponentInspector(obj, info);
		} else if (info.has && info.has(obj) && info.drawInspector) {
			info.drawInspector(*this, obj);
		}
	}

	// Components saved by a build that registered them: shown read-only and kept when saving
	for (const auto& [name, json] : obj.components.Unknown()) {
		ImGui::PushID(name.c_str());
		bool keep = true;
		if (ImGui::CollapsingHeader(std::format("{} (unknown component)", name).c_str(), &keep)) {
			ImGui::TextDisabled("Not registered in this engine build; its data is kept as-is.");
			ImGui::TextWrapped("%s", json.c_str());
		}
		if (!keep) pendingDataComponentRemoval_ = name;
		ImGui::PopID();
	}
#else
	(void)obj;
#endif
}

void EditorSuite::DrawDataComponentInspector(GameObject& obj, const ComponentInfo& info)
{
#ifdef USE_IMGUI
	ComponentData* data = obj.components.Find(info.name);
	if (!data) return;

	ImGui::PushID(info.name.c_str());
	bool keep = true;
	const bool open = ImGui::CollapsingHeader(TrId(info.name.c_str()), &keep, ImGuiTreeNodeFlags_DefaultOpen);
	ImGui::SetItemTooltip("%s", Tr("Right-click or press x to remove this component"));
	if (ImGui::BeginPopupContextItem()) {
		if (ImGui::MenuItem(TrId("Remove Component"))) keep = false;
		if (ImGui::MenuItem(TrId("Reset Values")) && info.create) {
			obj.components.Add(info.name, info.create());
			data = obj.components.Find(info.name);
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		ImGui::EndPopup();
	}
	if (!keep) pendingDataComponentRemoval_ = info.name;

	if (open && keep && data) {
		auto* base = static_cast<unsigned char*>(data->Data());
		for (const ComponentField& field : info.fields) {
			void* ptr = base + field.offset;
			const bool limited = field.minValue < field.maxValue;
			bool changed = false;
			bool commitNow = false; // checkbox-like widgets record undo immediately, drags when released
			switch (field.type) {
			case ComponentFieldType::Float:
				changed = ImGui::DragFloat(field.name, static_cast<float*>(ptr), 0.05f,
					limited ? field.minValue : 0.0f, limited ? field.maxValue : 0.0f, "%.3f",
					limited ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None);
				break;
			case ComponentFieldType::Int:
				changed = ImGui::DragInt(field.name, static_cast<int*>(ptr), 0.2f,
					limited ? static_cast<int>(field.minValue) : 0, limited ? static_cast<int>(field.maxValue) : 0, "%d",
					limited ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None);
				break;
			case ComponentFieldType::Bool:
				changed = ImGui::Checkbox(field.name, static_cast<bool*>(ptr));
				commitNow = changed;
				break;
			case ComponentFieldType::Vector3:
				changed = DrawVec3Control(field.name, *static_cast<Vector3*>(ptr), 0.0f);
				commitNow = changed; // merged into one undo step while dragging
				break;
			case ComponentFieldType::Color:
				changed = (field.size >= sizeof(float) * 4)
					? ImGui::ColorEdit4(field.name, static_cast<float*>(ptr))
					: ImGui::ColorEdit3(field.name, static_cast<float*>(ptr));
				break;
			case ComponentFieldType::Text:
				changed = ImGui::InputText(field.name, static_cast<char*>(ptr), field.size);
				break;
			case ComponentFieldType::Asset:
				changed = ImGui::InputText(field.name, static_cast<char*>(ptr), field.size);
				// Drop a file from the Project panel onto the field
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RIZE_ASSET_PATH")) {
						strncpy_s(static_cast<char*>(ptr), field.size, static_cast<const char*>(payload->Data), _TRUNCATE);
						changed = true;
						commitNow = true;
					}
					ImGui::EndDragDropTarget();
				}
				break;
			}
			if (field.tooltip) ImGui::SetItemTooltip("%s", Tr(field.tooltip));
			if (changed) isSceneDirty_ = true;
			if (commitNow || (field.type != ComponentFieldType::Vector3 && ImGui::IsItemDeactivatedAfterEdit())) {
				PushUndoSnapshot();
			}
		}
		if (info.drawExtra && data) info.drawExtra(*this, obj, *data);
	}
	ImGui::PopID();
#else
	(void)obj;
	(void)info;
#endif
}

void EditorSuite::DrawAddComponentMenuItems(GameObject& obj)
{
#ifdef USE_IMGUI
	const auto& all = ComponentRegistry::Get().All();
	// Category submenus in first-registration order; uncategorised entries at the top level
	std::vector<std::string> categories;
	for (const ComponentInfo& info : all) {
		if (info.showInAddMenu && !info.category.empty() &&
			std::find(categories.begin(), categories.end(), info.category) == categories.end()) {
			categories.push_back(info.category);
		}
	}

	std::string requested;
	std::function<void(EditorSuite&, GameObject&)> requestedVariant;
	auto items = [&](const std::string& category) {
		for (const ComponentInfo& info : all) {
			if (!info.showInAddMenu || info.category != category) continue;
			const bool present = HasComponentByName(obj, info.name);
			if (ImGui::MenuItem(TrId(info.name.c_str()), nullptr, false, !present)) requested = info.name;
			for (const auto& [label, fn] : info.addVariants) {
				if (ImGui::MenuItem(TrId(label.c_str()), nullptr, false, !present)) {
					requested = info.name;
					requestedVariant = fn;
				}
			}
		}
	};
	items("");
	for (const std::string& category : categories) {
		if (ImGui::BeginMenu(TrId(category.c_str()))) {
			items(category);
			ImGui::EndMenu();
		}
	}

	if (!requested.empty()) {
		if (requestedVariant) {
			requestedVariant(*this, obj);
			isSceneDirty_ = true;
			PushUndoSnapshot();
		} else {
			AddComponentByName(obj, requested);
		}
	}
#else
	(void)obj;
#endif
}

// ---------------------------------------------------------------------------
// JSON of data components: "components": { "Health": { "hp": 10, ... }, ... }
// ---------------------------------------------------------------------------
void EditorSuite::WriteDataComponentsJson(const GameObject& obj, void* outObjectJson) const
{
	using SceneJson::Json;
	if (obj.components.Empty()) return;
	Json& o = *static_cast<Json*>(outObjectJson);
	Json comps = Json::object();
	for (const auto& entry : obj.components.Entries()) {
		const ComponentInfo* info = ComponentRegistry::Get().Find(entry.name);
		if (!info || !entry.data) continue;
		const auto* base = static_cast<const unsigned char*>(entry.data->Data());
		Json c = Json::object();
		for (const ComponentField& field : info->fields) {
			const void* ptr = base + field.offset;
			switch (field.type) {
			case ComponentFieldType::Float: c[field.name] = SceneJson::Num(*static_cast<const float*>(ptr)); break;
			case ComponentFieldType::Int: c[field.name] = *static_cast<const int*>(ptr); break;
			case ComponentFieldType::Bool: c[field.name] = *static_cast<const bool*>(ptr); break;
			case ComponentFieldType::Vector3: c[field.name] = SceneJson::Arr(*static_cast<const Vector3*>(ptr)); break;
			case ComponentFieldType::Color:
				c[field.name] = (field.size >= sizeof(float) * 4)
					? SceneJson::Arr(*static_cast<const Vector4*>(ptr))
					: SceneJson::Arr(*static_cast<const Vector3*>(ptr));
				break;
			case ComponentFieldType::Text:
			case ComponentFieldType::Asset: {
				const char* text = static_cast<const char*>(ptr);
				const std::string value(text, strnlen(text, field.size));
				c[field.name] = value;
				if (field.type == ComponentFieldType::Asset && !value.empty()) {
					if (const std::string guid = AssetDb().GuidForPath(value); !guid.empty()) c[std::string(field.name) + "Guid"] = guid;
				}
				break;
			}
			}
		}
		comps[entry.name] = std::move(c);
	}
	for (const auto& [name, text] : obj.components.Unknown()) {
		if (auto parsed = SceneJson::Parse(text)) comps[name] = std::move(*parsed);
	}
	o["components"] = std::move(comps);
}

void EditorSuite::ReadDataComponentsJson(const void* objectJson, GameObject& obj) const
{
	using SceneJson::Json;
	obj.components.Clear();
	const Json& j = *static_cast<const Json*>(objectJson);
	const Json* comps = SceneJson::Find(j, "components");
	if (!comps || !comps->is_object()) return;

	for (const auto& [name, value] : comps->items()) {
		const ComponentInfo* info = ComponentRegistry::Get().Find(name);
		if (!info || !info->IsDataComponent() || !info->create || !value.is_object()) {
			obj.components.Unknown()[name] = SceneJson::DumpCompact(value);
			continue;
		}
		// Start from the defaults so fields added to the struct later keep sensible values
		ComponentData* data = obj.components.Add(name, info->create());
		auto* base = static_cast<unsigned char*>(data->Data());
		for (const ComponentField& field : info->fields) {
			void* ptr = base + field.offset;
			switch (field.type) {
			case ComponentFieldType::Float: SceneJson::Read(value, field.name, *static_cast<float*>(ptr)); break;
			case ComponentFieldType::Int: SceneJson::Read(value, field.name, *static_cast<int*>(ptr)); break;
			case ComponentFieldType::Bool: SceneJson::Read(value, field.name, *static_cast<bool*>(ptr)); break;
			case ComponentFieldType::Vector3: SceneJson::Read(value, field.name, *static_cast<Vector3*>(ptr)); break;
			case ComponentFieldType::Color:
				if (field.size >= sizeof(float) * 4) SceneJson::Read(value, field.name, *static_cast<Vector4*>(ptr));
				else SceneJson::Read(value, field.name, *static_cast<Vector3*>(ptr));
				break;
			case ComponentFieldType::Text:
			case ComponentFieldType::Asset: {
				std::string text;
				bool found = SceneJson::Read(value, field.name, text);
				// A known GUID wins over the stored path (the asset was renamed or moved)
				std::string guid;
				if (field.type == ComponentFieldType::Asset && SceneJson::Read(value, (std::string(field.name) + "Guid").c_str(), guid)) {
					if (const std::string current = AssetDb().PathForGuid(guid); !current.empty()) {
						text = current;
						found = true;
					}
				}
				if (found && field.size > 0) {
					strncpy_s(static_cast<char*>(ptr), field.size, text.c_str(), _TRUNCATE);
				}
				break;
			}
			}
		}
	}
}
