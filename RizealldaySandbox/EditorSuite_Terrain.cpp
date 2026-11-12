#include "EditorSuiteInternal.h"
#include "PhysicsComponents.h"
#include "math/MathUtil.h"
#include <DirectXTex.h>
#include <fstream>

// Terrain component: bakes its height grid into Assets/Models and makes that the object's model.
// Sculpting edits the grid in memory, bakes it a few times a second while dragging, and saves it to
// Assets/Terrains/<name>.terrainheights when the mouse is released (heightData then wins over noise and heightmap).

namespace {
	constexpr const char* kMeshColliderComponent = "Mesh Collider";

	/// Grayscale bytes of an image file; false when it cannot be read
	bool ReadGrayscale(const std::filesystem::path& file, std::vector<uint8_t>& pixels, int& width, int& height)
	{
		DirectX::ScratchImage loaded;
		if (FAILED(DirectX::LoadFromWICFile(file.wstring().c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, loaded))) return false;
		DirectX::ScratchImage converted;
		const DirectX::Image* image = loaded.GetImage(0, 0, 0);
		if (image->format != DXGI_FORMAT_R8G8B8A8_UNORM) {
			if (FAILED(DirectX::Convert(*image, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted))) return false;
			image = converted.GetImage(0, 0, 0);
		}
		width = static_cast<int>(image->width);
		height = static_cast<int>(image->height);
		pixels.resize(static_cast<size_t>(width) * height);
		for (int y = 0; y < height; ++y) {
			const uint8_t* row = image->pixels + y * image->rowPitch;
			for (int x = 0; x < width; ++x) pixels[static_cast<size_t>(y) * width + x] = row[x * 4];
		}
		return true;
	}
}

void EditorSuite::RegisterTerrainComponent()
{
	ComponentInfo& info = RegisterDataComponent<TerrainComponent>(kTerrainComponent, "Rendering", {
		RIZE_FIELD(TerrainComponent, size, ComponentFieldType::Float, 1.0f, 10000.0f),
		RIZE_FIELD(TerrainComponent, height, ComponentFieldType::Float, 0.0f, 1000.0f),
		RIZE_FIELD(TerrainComponent, resolution, ComponentFieldType::Int, 2.0f, 513.0f),
		RIZE_FIELD(TerrainComponent, heightmap, ComponentFieldType::Asset, 0.0f, 0.0f),
		RIZE_FIELD(TerrainComponent, heightData, ComponentFieldType::Asset, 0.0f, 0.0f),
		RIZE_FIELD(TerrainComponent, seed, ComponentFieldType::Int, 0.0f, 100000.0f),
		RIZE_FIELD(TerrainComponent, frequency, ComponentFieldType::Float, 0.1f, 100.0f),
		RIZE_FIELD(TerrainComponent, octaves, ComponentFieldType::Int, 1.0f, 8.0f),
		RIZE_FIELD(TerrainComponent, uvRepeat, ComponentFieldType::Float, 0.01f, 1000.0f) });
#ifdef USE_IMGUI
	info.drawExtra = [](EditorSuite& suite, GameObject& obj, ComponentData&) {
		if (ImGui::Button(TrId("Generate Terrain"))) suite.GenerateTerrain(obj);
		ImGui::SetItemTooltip("%s", Tr("Writes a mesh to Assets/Models and uses it as this object's model, with a Mesh Collider."));
		TerrainSculptState& sculpt = suite.terrainSculpt_;
		bool sculpting = sculpt.active && sculpt.objectId == obj.id;
		if (ImGui::Checkbox(TrId("Sculpt"), &sculpting)) {
			if (!sculpting) suite.FinishTerrainSculpt();
			sculpt.active = sculpting;
			sculpt.objectId = sculpting ? obj.id : 0u;
			sculpt.heights = {};
		}
		ImGui::SetItemTooltip("%s", Tr("Left-drag on the terrain in the Scene view to paint. The gizmo is hidden meanwhile."));
		if (sculpting) {
			const char* brushes[] = { Tr("Raise"), Tr("Lower"), Tr("Smooth"), Tr("Flatten") };
			int brush = static_cast<int>(sculpt.brush);
			if (ImGui::Combo(TrId("Brush"), &brush, brushes, 4)) sculpt.brush = static_cast<TerrainBrush>(brush);
			ImGui::SliderFloat(TrId("Brush Size"), &sculpt.radius, 0.01f, 0.5f, "%.2f");
			ImGui::SliderFloat(TrId("Brush Strength"), &sculpt.strength, 0.001f, 0.2f, "%.3f");
			if (sculpt.brush == TerrainBrush::Flatten) ImGui::SliderFloat(TrId("Flatten Height"), &sculpt.flattenTo, 0.0f, 1.0f, "%.2f");
		}
		auto* terrain = obj.components.Get<TerrainComponent>(kTerrainComponent);
		if (terrain && terrain->heightData[0] != '\0' && ImGui::Button(TrId("Clear Sculpt"))) {
			terrain->heightData[0] = '\0';
			sculpt.heights = {};
			suite.GenerateTerrain(obj);
		}
	};
#else
	(void)info;
#endif
}

bool EditorSuite::ComputeTerrainHeights(const TerrainComponent& terrain, TerrainHeights& heights)
{
	if (terrain.heightData[0] != '\0') {
		std::ifstream in(currentProjectRoot_ / std::filesystem::path(terrain.heightData), std::ios::binary);
		const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (DecodeTerrainHeights(bytes, heights)) return true;
		LogMessage(std::format("[Terrain] Could not read the sculpted heights {}; using the other settings", terrain.heightData), 1);
	}
	if (terrain.heightmap[0] != '\0') {
		std::vector<uint8_t> pixels;
		int width = 0;
		int height = 0;
		if (!ReadGrayscale(currentProjectRoot_ / std::filesystem::path(terrain.heightmap), pixels, width, height)) {
			LogMessage(std::format("[Terrain] Could not read the heightmap {}", terrain.heightmap), 2);
			return false;
		}
		heights = TerrainFromImage(pixels, width, height, terrain.resolution);
		return true;
	}
	heights = GenerateTerrainNoise(terrain.resolution, terrain.seed, terrain.frequency, terrain.octaves);
	return true;
}

bool EditorSuite::GenerateTerrain(GameObject& obj, const TerrainHeights* sculpted, bool preview)
{
	auto* terrain = obj.components.Get<TerrainComponent>(kTerrainComponent);
	if (!terrain || currentProjectRoot_.empty()) return false;

	TerrainHeights heights;
	if (sculpted) heights = *sculpted;
	else if (!ComputeTerrainHeights(*terrain, heights)) return false;

	// A new file every time: models are cached by path
	std::string base;
	for (char c : obj.name) base += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
	if (base.empty()) base = "Terrain";
	std::error_code ec;
	std::string assetPath;
	// Never a name used before in this run: a deleted bake's path would still hit the model cache
	static int nextBake = 0;
	for (int tries = 0; tries < 100000; ++tries) {
		assetPath = std::format("Assets/Models/{}_terrain{}.obj", base, nextBake++);
		if (!std::filesystem::exists(currentProjectRoot_ / std::filesystem::path(assetPath), ec)) break;
	}
	const std::filesystem::path file = currentProjectRoot_ / std::filesystem::path(assetPath);
	std::filesystem::create_directories(file.parent_path(), ec);
	if (!WriteProjectFile(file, BuildTerrainObj(heights, terrain->size, terrain->height, terrain->uvRepeat))) {
		LogMessage(std::format("[Terrain] Could not write {}", assetPath), 2);
		return false;
	}

	// The previous bake: a preview made while dragging goes now; any other may come back with Undo,
	// so it is removed when the scene is closed and nothing uses it (CollectTerrainGarbage)
	const std::string previous = obj.modelPath;
	if (previous.find("_terrain") != std::string::npos && previous.rfind("Assets/Models/", 0) == 0) {
		if (terrainPreviewBakes_.erase(previous) > 0) {
			std::filesystem::remove(currentProjectRoot_ / std::filesystem::path(previous), ec);
			std::filesystem::remove(currentProjectRoot_ / std::filesystem::path(previous + ".meta"), ec);
		} else {
			terrainGarbage_.push_back(previous);
		}
	}
	if (preview) terrainPreviewBakes_.insert(assetPath);
	AssetDb().Refresh();

	obj.hasMesh = true;
	obj.meshEnabled = true;
	obj.meshType = GameObject::MeshType::CustomModel;
	obj.modelPath = assetPath;
	obj.editObject3d.reset();
	obj.gameObject3d.reset();
	obj.isStatic = true;
	obj.hasBoxCollider = false;
	obj.hasSphereCollider = false;
	if (!obj.components.Has(kMeshColliderComponent)) AddComponentByName(obj, kMeshColliderComponent);
	isSceneDirty_ = true;
	if (!sculpted) {
		// Its own undo step (a sculpt pushes once, when the stroke ends)
		undoMergeAllowed_ = false;
		PushUndoSnapshot();
		undoMergeAllowed_ = false;
	}
	if (!sculpted) LogMessage(std::format("[Terrain] Generated {} ({} x {} vertices)", assetPath, heights.resolution, heights.resolution), 0);
	return true;
}

bool EditorSuite::SculptTerrainAt(uint32_t objectId, const Vector3& rayOrigin, const Vector3& rayDirection)
{
	GameObject* obj = FindGameObjectById(objectId, false);
	const TerrainComponent* terrain = obj ? obj->components.Get<TerrainComponent>(kTerrainComponent) : nullptr;
	if (!terrain || currentProjectRoot_.empty()) return false;
	float u = 0.0f;
	float v = 0.0f;
	if (!HitSculptedTerrain(*obj, *terrain, rayOrigin, rayDirection, u, v)) return false;
	TerrainSculptState& sculpt = terrainSculpt_;
	SculptTerrain(sculpt.heights, u, v, sculpt.radius, sculpt.strength, sculpt.brush, sculpt.flattenTo);
	sculpt.changed = true;
	return true;
}

bool EditorSuite::HitSculptedTerrain(const GameObject& obj, const TerrainComponent& terrain, const Vector3& rayOrigin, const Vector3& rayDirection, float& u, float& v)
{
	// The heights shown right now: reloaded for another object, or after Undo put another file back
	TerrainSculptState& sculpt = terrainSculpt_;
	if (sculpt.objectId != obj.id || sculpt.heights.resolution < 2 || (!sculpt.changed && sculpt.source != terrain.heightData)) {
		sculpt.objectId = obj.id;
		sculpt.heights = {};
		sculpt.source = terrain.heightData;
		if (!ComputeTerrainHeights(terrain, sculpt.heights) || sculpt.heights.resolution < 2) return false;
	}
	// The ray in the terrain's own space (its scale and rotation included)
	const Matrix4x4 toLocal = Mathematics::Inverse(obj.worldMatrix);
	const Vector3 origin = TransformUtil::TransformPoint(rayOrigin, toLocal);
	const Vector3 direction = TransformUtil::TransformDirection(rayDirection, toLocal);
	return RaycastTerrain(sculpt.heights, terrain.size, terrain.height, origin, direction, u, v);
}

bool EditorSuite::TerrainBrushOutline(uint32_t objectId, const Vector3& rayOrigin, const Vector3& rayDirection, std::vector<Vector3>& ring)
{
	ring.clear();
	GameObject* obj = FindGameObjectById(objectId, false);
	const TerrainComponent* terrain = obj ? obj->components.Get<TerrainComponent>(kTerrainComponent) : nullptr;
	float u = 0.0f;
	float v = 0.0f;
	if (!terrain || !HitSculptedTerrain(*obj, *terrain, rayOrigin, rayDirection, u, v)) return false;
	constexpr int kPoints = 48;
	const float radius = terrainSculpt_.radius;
	for (int i = 0; i <= kPoints; ++i) {
		const float angle = 6.2831853f * static_cast<float>(i) / kPoints;
		const float pu = u + std::cos(angle) * radius;
		const float pv = v + std::sin(angle) * radius;
		const float height = TerrainHeightAt(terrainSculpt_.heights, std::clamp(pu, 0.0f, 1.0f), std::clamp(pv, 0.0f, 1.0f)) * terrain->height;
		const Vector3 local{ (pu - 0.5f) * terrain->size, height + 0.05f, (pv - 0.5f) * terrain->size };
		ring.push_back(TransformUtil::TransformPoint(local, obj->worldMatrix));
	}
	return true;
}

bool EditorSuite::FinishTerrainSculpt()
{
	TerrainSculptState& sculpt = terrainSculpt_;
	if (!sculpt.changed) return true;
	sculpt.changed = false;
	GameObject* obj = FindGameObjectById(sculpt.objectId, false);
	auto* terrain = obj ? obj->components.Get<TerrainComponent>(kTerrainComponent) : nullptr;
	if (!terrain || currentProjectRoot_.empty()) return false;
	// A new file per stroke: Undo brings the previous path back, so that file must stay as it was
	std::string assetPath;
	{
		std::string base;
		for (char c : obj->name) base += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
		if (base.empty()) base = "Terrain";
		std::error_code ec;
		static int nextStroke = 0;
		for (int tries = 0; tries < 100000; ++tries) {
			assetPath = std::format("Assets/Terrains/{}_{}.terrainheights", base, nextStroke++);
			if (!std::filesystem::exists(currentProjectRoot_ / std::filesystem::path(assetPath), ec)) break;
		}
	}
	if (terrain->heightData[0] != '\0') terrainGarbage_.push_back(terrain->heightData);
	const std::filesystem::path file = currentProjectRoot_ / std::filesystem::path(assetPath);
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	{
		const std::vector<uint8_t> bytes = EncodeTerrainHeights(sculpt.heights);
		if (!WriteProjectFile(file, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()))) {
			LogMessage(std::format("[Terrain] Could not write {}", assetPath), 2);
			return false;
		}
	}
	strncpy_s(terrain->heightData, assetPath.c_str(), sizeof(terrain->heightData) - 1);
	const TerrainHeights heights = sculpt.heights;
	if (!GenerateTerrain(*obj, &heights)) return false;
	sculpt.source = assetPath;
	undoMergeAllowed_ = false; // one stroke = one undo step, never merged with the edit before it
	PushUndoSnapshot();
	undoMergeAllowed_ = false; // nor with the next one
	return true;
}

void EditorSuite::CollectTerrainGarbage()
{
	if (keepTerrainGarbage_ || (terrainGarbage_.empty() && terrainPreviewBakes_.empty())) return;
	std::unordered_set<std::string> used;
	for (GameObject& obj : gameObjects_) {
		used.insert(obj.modelPath);
		if (const auto* terrain = obj.components.Get<TerrainComponent>(kTerrainComponent)) used.insert(terrain->heightData);
	}
	// Saved scenes and prefabs keep theirs, even when the open scene was closed without saving
	std::string savedText;
	std::error_code ec;
	if (!currentProjectRoot_.empty()) {
		for (auto it = std::filesystem::recursive_directory_iterator(currentProjectRoot_ / "Assets", ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
			const std::string name = it->path().filename().string();
			if (!it->is_regular_file(ec) || !(name.ends_with(".scene.json") || name.ends_with(".prefab.json"))) continue;
			std::ifstream in(it->path(), std::ios::binary);
			savedText.append(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}
	}
	const auto keep = [&](const std::string& file) { return used.contains(file) || savedText.find(""" + file + """) != std::string::npos; };
	ec.clear();
	for (const std::string& file : terrainGarbage_) {
		if (keep(file) || currentProjectRoot_.empty()) continue;
		std::filesystem::remove(currentProjectRoot_ / std::filesystem::path(file), ec);
		std::filesystem::remove(currentProjectRoot_ / std::filesystem::path(file + ".meta"), ec);
	}
	for (const std::string& file : terrainPreviewBakes_) {
		if (keep(file) || currentProjectRoot_.empty()) continue;
		std::filesystem::remove(currentProjectRoot_ / std::filesystem::path(file), ec);
		std::filesystem::remove(currentProjectRoot_ / std::filesystem::path(file + ".meta"), ec);
	}
	terrainGarbage_.clear();
	terrainPreviewBakes_.clear();
	AssetDb().Refresh();
}

void EditorSuite::UpdateTerrainSculptInput(const ImVec2& imagePos, const ImVec2& imageSize, bool hovered)
{
#ifdef USE_IMGUI
	TerrainSculptState& sculpt = terrainSculpt_;
	if (!sculpt.active) return;
	GameObject* selected = (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) ? &gameObjects_[selectedObjectIndex_] : nullptr;
	if (!selected || selected->id != sculpt.objectId || sceneState_ != EditorSceneState::Edit) {
		FinishTerrainSculpt();
		sculpt.active = false;
		sculpt.objectId = 0;
		return;
	}
	const ImGuiIO& io = ImGui::GetIO();
	if (!io.WantTextInput) {
		if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
			sculpt.radius = (std::max)(0.02f, sculpt.radius * 0.8f);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
			sculpt.radius = (std::min)(0.4f, sculpt.radius * 1.25f);
		}
	}
	if (!hovered || !(imageSize.x > 0.0f) || !(imageSize.y > 0.0f)) {
		if (sculpt.changed && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) FinishTerrainSculpt();
		return;
	}
	const float ndcX = (io.MousePos.x - imagePos.x) / imageSize.x * 2.0f - 1.0f;
	const float ndcY = 1.0f - (io.MousePos.y - imagePos.y) / imageSize.y * 2.0f;
	const Matrix4x4 viewProjection = editorCamera_.GetViewProjectionMatrix();
	const Matrix4x4 inv = Mathematics::Inverse(viewProjection);
	const auto unprojectAt = [&](float z) {
		const float x = ndcX * inv.m[0][0] + ndcY * inv.m[1][0] + z * inv.m[2][0] + inv.m[3][0];
		const float y = ndcX * inv.m[0][1] + ndcY * inv.m[1][1] + z * inv.m[2][1] + inv.m[3][1];
		const float zz = ndcX * inv.m[0][2] + ndcY * inv.m[1][2] + z * inv.m[2][2] + inv.m[3][2];
		const float w = ndcX * inv.m[0][3] + ndcY * inv.m[1][3] + z * inv.m[2][3] + inv.m[3][3];
		return Vector3{ x / w, y / w, zz / w };
	};
	// Where the brush would paint
	std::vector<Vector3> ring;
	if (TerrainBrushOutline(sculpt.objectId, unprojectAt(0.0f), unprojectAt(1.0f) - unprojectAt(0.0f), ring)) {
		std::vector<ImVec2> points;
		for (const Vector3& p : ring) {
			const float cx = p.x * viewProjection.m[0][0] + p.y * viewProjection.m[1][0] + p.z * viewProjection.m[2][0] + viewProjection.m[3][0];
			const float cy = p.x * viewProjection.m[0][1] + p.y * viewProjection.m[1][1] + p.z * viewProjection.m[2][1] + viewProjection.m[3][1];
			const float cw = p.x * viewProjection.m[0][3] + p.y * viewProjection.m[1][3] + p.z * viewProjection.m[2][3] + viewProjection.m[3][3];
			if (cw <= 1.0e-4f) { points.clear(); break; } // behind the camera
			points.push_back({ imagePos.x + (cx / cw * 0.5f + 0.5f) * imageSize.x, imagePos.y + (0.5f - cy / cw * 0.5f) * imageSize.y });
		}
		if (points.size() > 1) {
			ImDrawList* draw = ImGui::GetWindowDrawList();
			draw->PushClipRect(imagePos, ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y), true);
			draw->AddPolyline(points.data(), static_cast<int>(points.size()), IM_COL32(255, 220, 90, 230), ImDrawFlags_None, 2.0f);
			draw->PopClipRect();
		}
	}
	if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		const TerrainBrush savedBrush = sculpt.brush;
		if (io.KeyShift) {
			sculpt.brush = TerrainBrush::Smooth;
		} else if (io.KeyCtrl) {
			if (sculpt.brush == TerrainBrush::Raise) sculpt.brush = TerrainBrush::Lower;
			else if (sculpt.brush == TerrainBrush::Lower) sculpt.brush = TerrainBrush::Raise;
		}
		const Vector3 nearPoint = unprojectAt(0.0f);
		const Vector3 farPoint = unprojectAt(1.0f);
		SculptTerrainAt(sculpt.objectId, nearPoint, farPoint - nearPoint);
		sculpt.brush = savedBrush;
		// Show the shape while dragging, a few times a second (each bake writes a mesh file)
		const double now = ImGui::GetTime();
		if (sculpt.changed && now - sculpt.lastBake > 0.15) {
			sculpt.lastBake = now;
			const TerrainHeights heights = sculpt.heights;
			GenerateTerrain(*selected, &heights, true);
		}
	} else if (sculpt.changed && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		FinishTerrainSculpt();
	}
#else
	(void)imagePos; (void)imageSize; (void)hovered;
#endif
}
