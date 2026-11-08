#include "EditorSuiteInternal.h"
#include "render/GpuParticles.h"
#include "base/TextureManager.h"
#include <chrono>
#include <format>
#include <fstream>
#include <sstream>

// Effect node graphs at runtime: material graphs (.vfxmat.json) become particle pixel shaders.
// The graph model and its code generator are pure (NodeGraph.h); this file loads, builds and caches them.

namespace {
	constexpr std::string_view kGuidSuffix = " Guid";
}

void EditorSuite::SetMaterialTexture(GraphNode& node, const std::string& param, const std::string& assetPath)
{
	node.texts[param] = assetPath;
	node.texts[param + std::string(kGuidSuffix)] = assetPath.empty() ? std::string() : AssetDb().GuidForPath(assetPath);
}

void EditorSuite::ResolveGraphTextures(NodeGraph& graph)
{
	for (GraphNode& node : graph.nodes) {
		for (auto& [key, guid] : node.texts) {
			if (guid.empty() || !key.ends_with(kGuidSuffix)) continue;
			const std::string now = AssetDb().PathForGuid(guid);
			if (!now.empty()) node.texts[key.substr(0, key.size() - kGuidSuffix.size())] = now;
		}
	}
}

bool EditorSuite::ReloadGraphFile(EffectMaterialEntry& entry, const std::string& assetPath, const char* label, NodeGraph& graph)
{
	// Files are checked twice a second at most: saving from the graph editor or another program shows up on its own
	const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	if (entry.lastCheck >= 0.0 && now - entry.lastCheck < 0.5) return false;
	entry.lastCheck = now;

	namespace fs = std::filesystem;
	const fs::path file = ResolveAssetPath(assetPath);
	std::error_code ec;
	const fs::file_time_type writeTime = fs::last_write_time(file, ec);
	if (ec) {
		if (entry.ok || entry.errors.empty()) LogMessage(std::format("[{}] Not found: {}", label, assetPath), 2);
		entry = EffectMaterialEntry{};
		entry.lastCheck = now;
		entry.errors = { { 0, "The graph file was not found." } };
		return false;
	}
	if (entry.loaded && writeTime == entry.writeTime) return false;
	entry.loaded = true;
	entry.writeTime = writeTime;
	entry.ok = false;
	entry.key = 0;
	entry.errors.clear();
	entry.textureHandles.clear();
	entry.parameters.clear();

	std::ifstream in(file, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	std::string why;
	if (!NodeGraph::FromJson(text.str(), graph, &why)) {
		entry.errors = { { 0, why } };
		LogMessage(std::format("[{}] {}: {}", label, assetPath, why), 2);
		return false;
	}
	return true;
}

const EffectMaterialEntry* EditorSuite::GetParticleGraph(const std::string& assetPath)
{
	if (assetPath.empty()) return nullptr;
	EffectMaterialEntry& entry = particleGraphs_[assetPath];
	const uint64_t before = entry.key;
	NodeGraph graph;
	if (!ReloadGraphFile(entry, assetPath, "Particle Graph", graph)) {
		ReleaseReplacedGraph(before, entry.key, true); // a broken or deleted file
		return &entry;
	}
	const NodeLibrary& lib = NodeLibrary::Effects();
	const GeneratedGraphCode spawn = GenerateGraphCode(graph, lib, kDomainParticleSpawn);
	// Update is optional: without an Update Output particles keep flying as they were born
	bool hasUpdate = false;
	for (const GraphNode& node : graph.nodes) hasUpdate |= node.type == "Update Output";
	const GeneratedGraphCode update = hasUpdate ? GenerateGraphCode(graph, lib, kDomainParticleUpdate) : GeneratedGraphCode{ true };
	entry.errors = spawn.errors;
	entry.errors.insert(entry.errors.end(), update.errors.begin(), update.errors.end());
	if (graph.parameters.size() > GpuParticles::kGraphParameters) {
		entry.errors.push_back({ 0, std::format("A particle graph can have at most {} parameters.", GpuParticles::kGraphParameters) });
	}
	if (entry.errors.empty()) {
		const uint64_t key = std::hash<std::string>{}(spawn.body + "\n// update\n" + update.body) | 1u;
		std::string why;
		if (GpuParticles::GetInstance()->BuildParticleGraph(key, spawn.body, update.body, &why)) {
			entry.ok = true;
			entry.key = key;
			entry.parameters = graph.parameters;
		} else {
			entry.errors = { { 0, "The particle graph does not compile: " + why } };
		}
	}
	for (const GraphError& e : entry.errors) LogMessage("[Particle Graph] " + assetPath + ": " + e.message, 2);
	ReleaseReplacedGraph(before, entry.key, true);
	return &entry;
}

std::string EditorSuite::EffectGraphProblem()
{
	for (const GameObject& obj : gameObjects_) {
		const auto check = [&](const char* path, bool particleGraph) -> std::string {
			if (path[0] == '\0') return {};
			const EffectMaterialEntry* entry = particleGraph ? GetParticleGraph(path) : GetEffectMaterial(path);
			if (entry && entry->ok) return {};
			const std::string why = !entry ? "not found" : entry->errors.empty() ? "did not compile" : entry->errors.front().message;
			return std::format("{} '{}' on '{}': {}", particleGraph ? "Particle graph" : "Material graph", path, obj.name, why);
		};
		std::string problem;
		if (const auto* p = obj.components.Get<GpuParticlesComponent>(kGpuParticlesComponent)) {
			problem = check(p->graph, true);
			if (problem.empty()) problem = check(p->material, false);
		}
		if (const auto* t = obj.components.Get<TrailComponent>(kTrailComponent); problem.empty() && t) problem = check(t->material, false);
		if (!problem.empty()) return problem;
	}
	return {};
}

bool EditorSuite::SetEffectParameter(uint32_t objectId, const std::string& name, const Vector4& value)
{
	const GameObject* obj = FindGameObjectById(objectId);
	const auto* p = obj ? obj->components.Get<GpuParticlesComponent>(kGpuParticlesComponent) : nullptr;
	const EffectMaterialEntry* graph = p && p->graph[0] != '\0' ? GetParticleGraph(p->graph) : nullptr;
	if (!graph) return false;
	bool known = false;
	for (const GraphParameter& parameter : graph->parameters) known |= parameter.name == name;
	if (!known) return false;
	effectParameterOverrides_[objectId][name] = value;
	return true;
}

const EffectMaterialEntry* EditorSuite::GetEffectMaterial(const std::string& assetPath)
{
	if (assetPath.empty()) return nullptr;
	EffectMaterialEntry& entry = effectMaterials_[assetPath];
	const uint64_t before = entry.key;
	NodeGraph graph;
	if (!ReloadGraphFile(entry, assetPath, "Material", graph)) {
		ReleaseReplacedGraph(before, entry.key, false);
		return &entry;
	}
	std::string why;
	{
		ResolveGraphTextures(graph);
		const GeneratedGraphCode code = GenerateGraphCode(graph, NodeLibrary::Effects(), kDomainMaterial);
		entry.errors = code.errors;
		if (code.ok) {
			// The key is the shader itself: two files with the same graph share one pipeline, an edit makes a new one
			const uint64_t key = std::hash<std::string>{}(code.body) | 1u;
			if (GpuParticles::GetInstance()->BuildMaterial(key, code.body, &why)) {
				entry.ok = true;
				entry.key = key;
				entry.body = code.body;
			} else {
				entry.errors = { { 0, "The material does not compile: " + why } };
			}
			for (const std::string& texture : code.textures) {
				uint32_t handle = 0; // 0 = the emitter's own texture
				if (!texture.empty()) {
					namespace fs = std::filesystem;
					std::error_code ec;
					const fs::path path = ResolveAssetPath(texture);
					if (fs::exists(path, ec)) handle = TextureManager::GetInstance()->LoadTexture(path.string());
					else LogMessage("[Material] " + assetPath + ": texture not found: " + texture, 1);
				}
				entry.textureHandles.push_back(handle);
			}
		}
	}
	for (const GraphError& e : entry.errors) LogMessage("[Material] " + assetPath + ": " + e.message, 2);
	ReleaseReplacedGraph(before, entry.key, false);
	return &entry;
}

void EditorSuite::ReleaseReplacedGraph(uint64_t oldKey, uint64_t newKey, bool particleGraph)
{
	if (oldKey == 0 || oldKey == newKey) return;
	// Two files with the same graph share one pipeline
	for (const auto& [path, entry] : particleGraph ? particleGraphs_ : effectMaterials_) {
		if (entry.key == oldKey) return;
	}
	if (particleGraph) GpuParticles::GetInstance()->ReleaseParticleGraph(oldKey);
	else GpuParticles::GetInstance()->ReleaseMaterial(oldKey);
}
