#include "EditorSuiteInternal.h"
#include "SceneJson.h"

using SceneJson::Json;

namespace {
constexpr const char* kPbrMaterialComponent = "PBR Material";

const std::vector<ComponentField>& MaterialFields()
{
	return ComponentRegistry::Get().Find(kPbrMaterialComponent)->fields;
}

bool SameValues(const PbrMaterialComponent& a, const PbrMaterialComponent& b)
{
	return a.metallic == b.metallic && a.roughness == b.roughness && a.normalStrength == b.normalStrength &&
		a.emissive.x == b.emissive.x && a.emissive.y == b.emissive.y && a.emissive.z == b.emissive.z &&
		a.emissiveStrength == b.emissiveStrength && std::strcmp(a.albedoMap, b.albedoMap) == 0 &&
		std::strcmp(a.normalMap, b.normalMap) == 0 && std::strcmp(a.ormMap, b.ormMap) == 0 && std::strcmp(a.emissiveMap, b.emissiveMap) == 0;
}

void CopyValues(const PbrMaterialComponent& from, PbrMaterialComponent& to)
{
	char keep[sizeof(to.material)];
	std::memcpy(keep, to.material, sizeof(keep));
	to = from;
	std::memcpy(to.material, keep, sizeof(keep));
}
}

void EditorSuite::RegisterMaterialComponent()
{
	if (ComponentRegistry::Get().Find(kPbrMaterialComponent)) return;
	const auto asset = [](const char* name, size_t offset, const char* tooltip) {
		return ComponentField{ name, ComponentFieldType::Asset, offset, 260, 0.0f, 0.0f, tooltip };
	};
	ComponentInfo& info = RegisterDataComponent<PbrMaterialComponent>(kPbrMaterialComponent, "Rendering", {
		asset("material", offsetof(PbrMaterialComponent, material), "Shared material asset (.material.json). Empty = the values below belong to this object."),
		RIZE_FIELD(PbrMaterialComponent, metallic, ComponentFieldType::Float, 0.0f, 1.0f),
		RIZE_FIELD(PbrMaterialComponent, roughness, ComponentFieldType::Float, 0.0f, 1.0f),
		RIZE_FIELD(PbrMaterialComponent, normalStrength, ComponentFieldType::Float, 0.0f, 2.0f),
		RIZE_FIELD(PbrMaterialComponent, emissive, ComponentFieldType::Color, 0.0f, 0.0f),
		RIZE_FIELD(PbrMaterialComponent, emissiveStrength, ComponentFieldType::Float, 0.0f, 100.0f),
		asset("albedoMap", offsetof(PbrMaterialComponent, albedoMap), "Color texture. Empty = the object's texture."),
		asset("normalMap", offsetof(PbrMaterialComponent, normalMap), "Normal map (green up). Empty = the model's own."),
		asset("ormMap", offsetof(PbrMaterialComponent, ormMap), "R occlusion, G roughness, B metallic; multiplied with the values above. Empty = the model's own."),
		asset("emissiveMap", offsetof(PbrMaterialComponent, emissiveMap), "Glow texture. Empty = the model's own.") });
	info.drawExtra = [](EditorSuite& s, GameObject& obj, ComponentData& data) {
#ifdef USE_IMGUI
		auto& material = *static_cast<PbrMaterialComponent*>(data.Data());
		if (material.material[0] == '\0') {
			if (ImGui::Button(TrId("Save As Material Asset"))) s.SaveMaterialAsset(obj, material);
			return;
		}
		// Edits go to the asset, so every object using it follows. The file is written once the widget is released.
		if (const auto entry = s.materialAssets_.find(material.material); entry != s.materialAssets_.end()) {
			if (!SameValues(entry->second.values, material)) {
				entry->second.values = material;
				entry->second.dirty = true;
			}
			if (entry->second.dirty && !ImGui::IsAnyItemActive()) {
				entry->second.dirty = false;
				s.WriteMaterialAssetFile(material.material, material);
			}
		}
#else
		(void)s; (void)obj; (void)data;
#endif
	};
}

bool EditorSuite::WriteMaterialAssetFile(const std::string& assetPath, const PbrMaterialComponent& values)
{
	Json root = Json::object();
	const auto* base = reinterpret_cast<const unsigned char*>(&values);
	for (const ComponentField& field : MaterialFields()) {
		const void* ptr = base + field.offset;
		if (std::strcmp(field.name, "material") == 0) continue;
		if (field.type == ComponentFieldType::Float) root[field.name] = SceneJson::Num(*static_cast<const float*>(ptr));
		else if (field.type == ComponentFieldType::Color) root[field.name] = SceneJson::Arr(*static_cast<const Vector3*>(ptr));
		else if (field.type == ComponentFieldType::Asset) {
			const std::string text(static_cast<const char*>(ptr));
			root[field.name] = text;
			if (const std::string guid = text.empty() ? std::string() : AssetDb().GuidForPath(text); !guid.empty()) root[std::string(field.name) + "Guid"] = guid;
		}
	}
	std::ofstream file(currentProjectRoot_ / std::filesystem::path(assetPath)); // a new file: ResolveAssetPath only finds existing ones
	if (!file) {
		LogMessage("[Material] Could not write " + assetPath, 2);
		return false;
	}
	file << root.dump(2);
	return true;
}

void EditorSuite::SaveMaterialAsset(GameObject& obj, PbrMaterialComponent& material)
{
	std::error_code ec;
	const std::filesystem::path folder = currentProjectRoot_ / "Assets" / "Materials";
	std::filesystem::create_directories(folder, ec);
	std::string assetPath;
	for (int i = 0; assetPath.empty(); ++i) {
		const std::string name = obj.name + (i == 0 ? "" : " " + std::to_string(i)) + ".material.json";
		if (!std::filesystem::exists(folder / std::filesystem::path(name), ec)) assetPath = "Assets/Materials/" + name;
	}
	if (!WriteMaterialAssetFile(assetPath, material)) return;
	AssetDb().Refresh();
	strncpy_s(material.material, assetPath.c_str(), _TRUNCATE);
	materialAssets_[assetPath] = { material, false };
	isSceneDirty_ = true;
	PushUndoSnapshot();
}

const PbrMaterialComponent* EditorSuite::FindMaterialAsset(const std::string& assetPath)
{
	if (const auto found = materialAssets_.find(assetPath); found != materialAssets_.end()) {
		return found->second.failed ? nullptr : &found->second.values;
	}
	MaterialAssetEntry entry;
	std::ifstream file{ currentProjectRoot_ / std::filesystem::path(assetPath) };
	std::optional<Json> parsed;
	if (file) parsed = SceneJson::Parse(std::string(std::istreambuf_iterator<char>(file), {}));
	if (!parsed) {
		LogMessage("[Material] Could not read " + assetPath, 1);
		entry.failed = true; // asked for every frame: complain once
		materialAssets_.emplace(assetPath, entry);
		return nullptr;
	}
	const Json& root = *parsed;
	auto* base = reinterpret_cast<unsigned char*>(&entry.values);
	for (const ComponentField& field : MaterialFields()) {
		void* ptr = base + field.offset;
		if (std::strcmp(field.name, "material") == 0) continue;
		if (field.type == ComponentFieldType::Float) SceneJson::Read(root, field.name, *static_cast<float*>(ptr));
		else if (field.type == ComponentFieldType::Color) SceneJson::Read(root, field.name, *static_cast<Vector3*>(ptr));
		else if (field.type == ComponentFieldType::Asset) {
			std::string text;
			SceneJson::Read(root, field.name, text);
			std::string guid;
			if (SceneJson::Read(root, (std::string(field.name) + "Guid").c_str(), guid)) {
				if (const std::string current = AssetDb().PathForGuid(guid); !current.empty()) text = current;
			}
			strncpy_s(static_cast<char*>(ptr), field.size, text.c_str(), _TRUNCATE);
		}
	}
	return &materialAssets_.emplace(assetPath, entry).first->second.values;
}

uint32_t EditorSuite::LoadMaterialTexture(const char* assetPath, bool linear)
{
	if (!assetPath || assetPath[0] == '\0') return 0;
	const std::string key = std::string(assetPath) + (linear ? "|linear" : "");
	if (const auto found = materialTextures_.find(key); found != materialTextures_.end()) return found->second;
	const std::string resolved = ResolveAssetPath(assetPath);
	std::error_code ec;
	const uint32_t handle = std::filesystem::exists(resolved, ec) ? TextureManager::GetInstance()->LoadTexture(resolved, linear) : 0;
	materialTextures_[key] = handle; // a missing file is remembered too: this runs for every draw
	return handle;
}

bool EditorSuite::ApplyPbrMaterial(GameObject& obj, Material& material, MaterialMaps& maps, uint32_t& albedoHandle)
{
	PbrMaterialComponent* pbr = obj.components.Empty() ? nullptr : obj.components.Get<PbrMaterialComponent>(kPbrMaterialComponent);
	if (!pbr) return false;
	if (pbr->material[0] != '\0') {
		if (const PbrMaterialComponent* shared = FindMaterialAsset(pbr->material)) CopyValues(*shared, *pbr);
	}
	material.lightingType = 6;
	material.metallic = pbr->metallic;
	material.roughness = pbr->roughness;
	material.normalStrength = pbr->normalStrength;
	material.emissiveColor = pbr->emissive;
	material.emissiveStrength = pbr->emissiveStrength;
	if (const uint32_t albedo = LoadMaterialTexture(pbr->albedoMap, false)) albedoHandle = albedo;
	maps.normal = LoadMaterialTexture(pbr->normalMap, true);
	maps.orm = LoadMaterialTexture(pbr->ormMap, true);
	maps.emissive = LoadMaterialTexture(pbr->emissiveMap, false);
	return true;
}
