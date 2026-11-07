#include "EditorSuiteInternal.h"
#include "GraphEditorModel.h"
#include <fstream>
#include <sstream>

// Material Graph window: palette (left), canvas (middle), details (right). Every change is saved at once,
// so particles using the material update in the Scene view while it is edited. The editing rules live in
// GraphEditorModel (tested); this file draws them and turns mouse and keys into model calls.

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
namespace {
	/// Writes `graph` as a new file in <project>/Assets/Effects without overwriting ("Fire 1" when "Fire" exists)
	std::string WriteNewGraphFile(const std::filesystem::path& projectRoot, const std::string& name, const char* fallbackName, const char* extension, const NodeGraph& graph)
	{
		namespace fs = std::filesystem;
		if (projectRoot.empty()) return {};
		const fs::path dir = projectRoot / "Assets" / "Effects";
		std::error_code ec;
		fs::create_directories(dir, ec);
		std::string base = name.empty() ? std::string(fallbackName) : name;
		for (char& c : base) {
			if (std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos) c = '_';
		}
		std::string file = base + extension;
		for (int i = 1; fs::exists(dir / fs::path(file), ec); ++i) file = base + " " + std::to_string(i) + extension;
		std::ofstream out(dir / fs::path(file), std::ios::binary);
		if (!out) return {};
		out << graph.ToJson();
		return "Assets/Effects/" + file;
	}
}

std::string EditorSuite::CreateEffectMaterial(const std::string& name, const std::string& templateName)
{
	const std::string path = WriteNewGraphFile(currentProjectRoot_, name, "New Material", ".vfxmat.json", MakeMaterialTemplate(templateName));
	if (!path.empty()) AssetDb().Refresh();
	return path;
}

std::string EditorSuite::CreateParticleGraph(const std::string& name, const std::string& templateName)
{
	const std::string path = WriteNewGraphFile(currentProjectRoot_, name, "New Particle Graph", ".vfxparticle.json", MakeParticleTemplate(templateName));
	if (!path.empty()) AssetDb().Refresh();
	return path;
}

void EditorSuite::DrawParticleGraphRows(GameObject& obj, GpuParticlesComponent& p)
{
#ifdef USE_IMGUI
	ImGui::PushID("ParticleGraphRows");
	if (p.graph[0] != '\0') {
		const EffectMaterialEntry* entry = GetParticleGraph(p.graph);
		if (entry && !entry->ok) {
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", Tr("Particle graph has problems"));
			if (!entry->errors.empty()) ImGui::SetItemTooltip("%s", entry->errors.front().message.c_str());
		} else if (entry) {
			ImGui::TextDisabled("%s", Tr("Graph decides life, speed, shape, forces, size and colour"));
			// One row per parameter: this object's value (the graph's default until changed)
			std::map<std::string, Vector4> values = ParseEffectParameterValues(p.parameters);
			for (const GraphParameter& parameter : entry->parameters) {
				const auto own = values.find(parameter.name);
				const bool hasOwn = own != values.end();
				Vector4 v = hasOwn ? own->second : parameter.value;
				bool changed = false;
				const std::string label = parameter.name + "##param";
				if (parameter.type == PinType::Float) changed = ImGui::DragFloat(label.c_str(), &v.x, 0.01f);
				else if (parameter.type == PinType::Float3) changed = ImGui::DragFloat3(label.c_str(), &v.x, 0.01f);
				else changed = ImGui::ColorEdit4(label.c_str(), &v.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
				ImGui::SetItemTooltip("%s", Tr("A parameter of the particle graph. Scripts: SetEffectParameter(name, value)."));
				const bool editDone = ImGui::IsItemDeactivatedAfterEdit();
				bool reset = false;
				if (hasOwn) {
					ImGui::SameLine();
					reset = ImGui::SmallButton(TrId("Reset")); // back to the graph's default
				}
				if (reset) values.erase(parameter.name);
				else if (changed) values[parameter.name] = v;
				if (reset || changed) {
					strncpy_s(p.parameters, FormatEffectParameterValues(values).c_str(), _TRUNCATE);
					isSceneDirty_ = true;
				}
				if (reset || editDone) PushUndoSnapshot();
			}
		}
	}
	ImGui::SetNextItemWidth(170.0f);
	if (ImGui::BeginCombo(TrId("##NewParticleGraph"), Tr("New Particle Graph..."))) {
		for (const char* t : kParticleTemplates) {
			if (!ImGui::Selectable(Tr(t))) continue;
			const std::string path = CreateParticleGraph(obj.name, t);
			if (path.empty()) continue;
			strncpy_s(p.graph, path.c_str(), _TRUNCATE);
			p.parameters[0] = '\0';
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("%s", Tr("Makes a particle graph from a template and gives it to this emitter"));
	ImGui::PopID();
#else
	(void)obj;
	(void)p;
#endif
}

bool EditorSuite::OpenMaterialGraph(const std::string& assetPath)
{
	std::ifstream in(ResolveAssetPath(assetPath), std::ios::binary);
	if (!in) return false;
	std::stringstream text;
	text << in.rdbuf();
	NodeGraph graph;
	std::string why;
	if (!NodeGraph::FromJson(text.str(), graph, &why)) {
		LogMessage("[Material] " + assetPath + ": " + why, 2);
		return false;
	}
	ResolveGraphTextures(graph);
	materialEditor_.Reset(graph);
	materialEditorPath_ = assetPath;
	materialGraphFit_ = true;
	materialLink_ = {};
	showMaterialGraph_ = true;
	return true;
}

bool EditorSuite::SaveMaterialGraph()
{
	if (materialEditorPath_.empty()) return false;
	std::ofstream out(ResolveAssetPath(materialEditorPath_), std::ios::binary);
	if (!out) return false;
	out << materialEditor_.graph.ToJson();
	out.close();
	materialEditor_.MarkSaved();
	// Rebuild now instead of at the next twice-a-second check: the Scene view follows the edit at once
	effectMaterials_.erase(materialEditorPath_);
	GetEffectMaterial(materialEditorPath_);
	return true;
}

bool EditorSuite::AssignMaterialToSelection(const std::string& assetPath)
{
	GameObject* obj = (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) ? &gameObjects_[selectedObjectIndex_] : nullptr;
	auto* p = obj ? obj->components.Get<GpuParticlesComponent>(kGpuParticlesComponent) : nullptr;
	auto* trail = obj ? obj->components.Get<TrailComponent>(kTrailComponent) : nullptr;
	if (!p && !trail) return false;
	if (p) strncpy_s(p->material, assetPath.c_str(), _TRUNCATE);
	if (trail) strncpy_s(trail->material, assetPath.c_str(), _TRUNCATE);
	isSceneDirty_ = true;
	PushUndoSnapshot();
	return true;
}

void EditorSuite::DrawMaterialRow(GameObject& obj, char* material, size_t capacity)
{
#ifdef USE_IMGUI
	ImGui::PushID(material);
	if (material[0] != '\0') {
		if (ImGui::Button(TrId("Edit Material"))) OpenMaterialGraph(material);
		ImGui::SameLine();
		if (const EffectMaterialEntry* entry = GetEffectMaterial(material); entry && !entry->ok) {
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", Tr("Material has problems"));
			if (!entry->errors.empty()) ImGui::SetItemTooltip("%s", entry->errors.front().message.c_str());
			ImGui::SameLine();
		}
	}
	ImGui::SetNextItemWidth(150.0f);
	if (ImGui::BeginCombo(TrId("##NewEffectMaterial"), Tr("New Material..."))) {
		for (const char* t : kMaterialTemplates) {
			if (!ImGui::Selectable(Tr(t))) continue;
			const std::string path = CreateEffectMaterial(obj.name, t);
			if (path.empty()) continue;
			strncpy_s(material, capacity, path.c_str(), _TRUNCATE);
			isSceneDirty_ = true;
			PushUndoSnapshot();
			OpenMaterialGraph(path);
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("%s", Tr("Makes a material from a template, gives it to this emitter and opens the Material Graph"));
	ImGui::PopID();
#else
	(void)obj;
	(void)material;
	(void)capacity;
#endif
}

#ifdef USE_IMGUI
namespace {
	constexpr float kNodeWidth = 190.0f;
	constexpr float kHeaderHeight = 26.0f;
	constexpr float kRowHeight = 22.0f;

	ImU32 PinColor(PinType type)
	{
		switch (type) {
		case PinType::Float: return IM_COL32(170, 170, 170, 255);
		case PinType::Float2: return IM_COL32(110, 205, 110, 255);
		case PinType::Float3: return IM_COL32(235, 200, 80, 255);
		case PinType::Float4: return IM_COL32(235, 110, 185, 255);
		default: return IM_COL32(225, 225, 225, 255);
		}
	}

	ImU32 CategoryColor(const std::string& category)
	{
		if (category == "Output") return IM_COL32(150, 60, 60, 255);
		if (category == "Values") return IM_COL32(70, 90, 140, 255);
		if (category == "Particle") return IM_COL32(60, 120, 90, 255);
		if (category == "Texture") return IM_COL32(120, 80, 140, 255);
		if (category == "Shape") return IM_COL32(140, 100, 50, 255);
		if (category == "Vector") return IM_COL32(60, 110, 130, 255);
		return IM_COL32(80, 80, 92, 255); // Math
	}

	/// Rows of a node: the larger of its inputs and outputs, plus one summary row for its settings
	int NodeRows(const NodeDef& def)
	{
		return static_cast<int>((std::max)(def.inputs.size(), def.outputs.size())) + (def.params.empty() ? 0 : 1);
	}

	/// One-line summary of a value for the node body ("0.50", "Particle UV")
	std::string ValueText(const GraphNode& node, const PinDef& pin)
	{
		if (!pin.defaultExpr.empty()) return Tr(pin.defaultLabel.c_str());
		const auto it = node.values.find(pin.name);
		const Vector4 v = it != node.values.end() ? it->second : pin.defaultValue;
		switch (pin.type) {
		case PinType::Float2: return std::format("{:.2f}, {:.2f}", v.x, v.y);
		case PinType::Float3: return std::format("{:.2f}, {:.2f}, {:.2f}", v.x, v.y, v.z);
		case PinType::Float4: return std::format("{:.2f}, {:.2f}, {:.2f}, {:.2f}", v.x, v.y, v.z, v.w);
		default: return std::format("{:.2f}", v.x);
		}
	}

	/// Texture files of the project (for the Texture Sample picker)
	std::vector<std::string> ProjectTextures(const std::filesystem::path& projectRoot)
	{
		namespace fs = std::filesystem;
		std::vector<std::string> out;
		std::error_code ec;
		const fs::path assets = projectRoot / "Assets";
		for (fs::recursive_directory_iterator it(assets, ec), end; !ec && it != end; it.increment(ec)) {
			if (!it->is_regular_file(ec)) continue;
			std::string ext = it->path().extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
			if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".dds") continue;
			out.push_back("Assets/" + fs::relative(it->path(), assets, ec).generic_string());
		}
		std::sort(out.begin(), out.end());
		return out;
	}
}
#endif

void EditorSuite::DrawMaterialGraphWindow()
{
#ifdef USE_IMGUI
	// Large and centred the first time: a graph needs room to be readable
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowSize(ImVec2((std::min)(1500.0f, viewport->WorkSize.x * 0.85f), (std::min)(820.0f, viewport->WorkSize.y * 0.8f)), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin(TrId("Material Graph"), &showMaterialGraph_)) {
		ImGui::End();
		return;
	}
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) shortcutWindowFocusedNow_ = true;
	const NodeLibrary& lib = NodeLibrary::Effects();
	GraphEditorModel& m = materialEditor_;
	GameObject* selectedObj = (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) ? &gameObjects_[selectedObjectIndex_] : nullptr;
	auto* selectedParticles = selectedObj ? selectedObj->components.Get<GpuParticlesComponent>(kGpuParticlesComponent) : nullptr;
	auto* selectedTrail = selectedObj ? selectedObj->components.Get<TrailComponent>(kTrailComponent) : nullptr;
	const char* selectedMaterial = selectedParticles ? selectedParticles->material : selectedTrail ? selectedTrail->material : nullptr;

	// Nothing open: start from a template, or open one of the project's materials
	if (materialEditorPath_.empty()) {
		ImGui::TextWrapped("%s", Tr("A material decides the colour and opacity of every pixel of a particle. Start from a template or open one."));
		ImGui::Spacing();
		static char newName[64] = "New Material";
		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputText(TrId("Name"), newName, sizeof(newName));
		for (const char* t : kMaterialTemplates) {
			if (ImGui::Button(Tr(t), ImVec2(160.0f, 0.0f))) {
				const std::string path = CreateEffectMaterial(newName, t);
				if (!path.empty() && OpenMaterialGraph(path) && selectedMaterial) AssignMaterialToSelection(path);
			}
			ImGui::SameLine();
		}
		ImGui::NewLine();
		if (selectedMaterial) ImGui::TextDisabled("%s", Tr("The new material is given to the selected GPU Particles."));
		ImGui::SeparatorText(Tr("Materials in this project"));
		namespace fs = std::filesystem;
		std::error_code ec;
		for (fs::recursive_directory_iterator it(currentProjectRoot_ / "Assets", ec), end; !ec && it != end; it.increment(ec)) {
			const std::string fileName = it->path().filename().string();
			if (!fileName.ends_with(".vfxmat.json")) continue;
			const std::string path = "Assets/" + fs::relative(it->path(), currentProjectRoot_ / "Assets", ec).generic_string();
			if (ImGui::Selectable(path.c_str())) OpenMaterialGraph(path);
		}
		ImGui::End();
		return;
	}

	// Live problems: the generator's (with the node they belong to) and the GPU compiler's
	if (materialCodeRevision_ != m.Revision()) {
		materialCodeRevision_ = m.Revision();
		materialCode_ = GenerateGraphCode(m.graph, lib, kDomainMaterial);
	}
	std::vector<GraphError> problems = materialCode_.errors;
	if (materialCode_.ok && !m.dirty) {
		if (const EffectMaterialEntry* entry = GetEffectMaterial(materialEditorPath_); entry && !entry->ok) problems = entry->errors;
	}
	const auto errorsOf = [&](uint32_t id) {
		std::string text;
		for (const GraphError& e : problems) {
			if (e.nodeId == id) text += (text.empty() ? "" : "\n") + e.message;
		}
		return text;
	};

	// ---- Toolbar ----
	ImGui::TextUnformatted(materialEditorPath_.c_str());
	ImGui::SameLine();
	if (problems.empty()) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s", Tr("Compiled"));
	else if (ImGui::SmallButton(std::format("{} ({})##MatProblems", Tr("Problems"), problems.size()).c_str())) {
		materialFocusNode_ = problems.front().nodeId;
	}
	if (!problems.empty()) ImGui::SetItemTooltip("%s", problems.front().message.c_str());
	ImGui::SameLine(0.0f, 20.0f);
	ImGui::BeginDisabled(!m.CanUndo());
	if (ImGui::Button(TrId("Undo"))) m.Undo();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!m.CanRedo());
	if (ImGui::Button(TrId("Redo"))) m.Redo();
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button(TrId("Fit"))) materialGraphFit_ = true;
	ImGui::SetItemTooltip("%s", Tr("Show every node (F)"));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(170.0f);
	if (ImGui::BeginCombo(TrId("##MaterialTemplate"), Tr("Start from template..."))) {
		for (const char* t : kMaterialTemplates) {
			if (ImGui::Selectable(Tr(t))) {
				m.graph = MakeMaterialTemplate(t);
				m.selection.clear();
				m.Commit();
				materialGraphFit_ = true;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("%s", Tr("Replaces this graph with a template (Undo brings it back)"));
	ImGui::SameLine();
	ImGui::BeginDisabled(!selectedMaterial || materialEditorPath_ == selectedMaterial);
	if (ImGui::Button(TrId("Use on Selected"))) AssignMaterialToSelection(materialEditorPath_);
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("%s", Tr("Gives this material to the selected object's GPU Particles or Trail"));
	ImGui::SameLine();
	if (ImGui::Button(TrId("Close##MaterialGraph"))) {
		if (m.dirty) SaveMaterialGraph();
		materialEditorPath_.clear();
		ImGui::End();
		return;
	}

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float paletteW = 200.0f, detailsW = 270.0f;

	// ---- Palette ----
	ImGui::BeginChild("##MatPalette", ImVec2(paletteW, avail.y), true);
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##MatPaletteSearch", Tr("Search nodes..."), materialPaletteSearch_, sizeof(materialPaletteSearch_));
	const std::vector<const NodeDef*> found = SearchNodes(lib, materialPaletteSearch_, kDomainMaterial);
	std::string lastCategory;
	const bool searching = materialPaletteSearch_[0] != '\0';
	bool categoryOpen = true;
	std::vector<const NodeDef*> ordered = found;
	if (!searching) std::stable_sort(ordered.begin(), ordered.end(), [](const NodeDef* a, const NodeDef* b) { return a->category < b->category; });
	for (const NodeDef* def : ordered) {
		if (!searching && def->category != lastCategory) {
			lastCategory = def->category;
			categoryOpen = ImGui::CollapsingHeader(Tr(def->category.c_str()), ImGuiTreeNodeFlags_DefaultOpen);
		}
		if (!searching && !categoryOpen) continue;
		ImGui::PushID(def->type.c_str());
		if (ImGui::Selectable(Tr(def->type.c_str()))) {
			const float zoom = materialGraphZoom_;
			const Vector2 center((materialCanvasSize_.x * 0.5f - materialGraphPan_.x) / zoom - kNodeWidth * 0.5f, (materialCanvasSize_.y * 0.5f - materialGraphPan_.y) / zoom);
			m.AddNode(def->type, center.x, center.y);
		}
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload("GRAPH_NODE_TYPE", def->type.c_str(), def->type.size() + 1);
			ImGui::TextUnformatted(Tr(def->type.c_str()));
			ImGui::EndDragDropSource();
		}
		ImGui::SetItemTooltip("%s", Tr(def->tooltip.c_str()));
		ImGui::PopID();
	}
	ImGui::EndChild();
	ImGui::SameLine();

	// ---- Canvas ----
	ImGui::BeginChild("##MatCanvas", ImVec2((std::max)(200.0f, avail.x - paletteW - detailsW - 16.0f), avail.y), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
	materialCanvasSize_ = Vector2(canvasSize.x, canvasSize.y);
	ImGui::InvisibleButton("##MatCanvasArea", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool hovered = ImGui::IsItemHovered();
	const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	ImGuiIO& io = ImGui::GetIO();
	ImDrawList* dl = ImGui::GetWindowDrawList();

	// Fit: every node in view
	if (materialGraphFit_ && !m.graph.nodes.empty() && canvasSize.x > 50.0f) {
		materialGraphFit_ = false;
		float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
		for (const GraphNode& n : m.graph.nodes) {
			const NodeDef* def = lib.Find(n.type);
			const float h = kHeaderHeight + kRowHeight * (def ? NodeRows(*def) : 1) + 8.0f;
			minX = (std::min)(minX, n.x); minY = (std::min)(minY, n.y);
			maxX = (std::max)(maxX, n.x + kNodeWidth); maxY = (std::max)(maxY, n.y + h + 30.0f);
		}
		// Never so small that the text is unreadable: a big graph is then centred and the rest is a pan away
		const float zoom = std::clamp((std::min)((canvasSize.x - 60.0f) / (maxX - minX), (canvasSize.y - 60.0f) / (maxY - minY)), 0.55f, 1.25f);
		materialGraphZoom_ = zoom;
		materialGraphPan_ = Vector2(canvasSize.x * 0.5f - (minX + maxX) * 0.5f * zoom, canvasSize.y * 0.5f - (minY + maxY) * 0.5f * zoom);
	}
	// Jump to a node with a problem
	if (materialFocusNode_ != 0) {
		if (const GraphNode* n = m.graph.Find(materialFocusNode_)) {
			materialGraphPan_ = Vector2(canvasSize.x * 0.5f - (n->x + kNodeWidth * 0.5f) * materialGraphZoom_, canvasSize.y * 0.5f - n->y * materialGraphZoom_);
			m.selection = { n->id };
		}
		materialFocusNode_ = 0;
	}

	const float zoom = materialGraphZoom_;
	const auto toScreen = [&](float x, float y) { return ImVec2(origin.x + materialGraphPan_.x + x * zoom, origin.y + materialGraphPan_.y + y * zoom); };
	const auto toCanvas = [&](ImVec2 p) { return Vector2((p.x - origin.x - materialGraphPan_.x) / zoom, (p.y - origin.y - materialGraphPan_.y) / zoom); };

	// Pan (middle drag or Alt + left drag) and zoom around the mouse
	if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)))) {
		materialGraphPan_.x += io.MouseDelta.x;
		materialGraphPan_.y += io.MouseDelta.y;
	}
	if (hovered && io.MouseWheel != 0.0f) {
		const Vector2 before = toCanvas(io.MousePos);
		materialGraphZoom_ = std::clamp(materialGraphZoom_ * (io.MouseWheel > 0.0f ? 1.1f : 1.0f / 1.1f), 0.3f, 2.0f);
		materialGraphPan_.x = io.MousePos.x - origin.x - before.x * materialGraphZoom_;
		materialGraphPan_.y = io.MousePos.y - origin.y - before.y * materialGraphZoom_;
	}

	// Grid
	const ImVec2 canvasMax(origin.x + canvasSize.x, origin.y + canvasSize.y);
	dl->PushClipRect(origin, canvasMax, true);
	dl->AddRectFilled(origin, canvasMax, IM_COL32(28, 28, 33, 255));
	const float grid = 32.0f * zoom;
	for (float x = std::fmod(materialGraphPan_.x, grid); x < canvasSize.x; x += grid) dl->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, canvasMax.y), IM_COL32(42, 42, 48, 255));
	for (float y = std::fmod(materialGraphPan_.y, grid); y < canvasSize.y; y += grid) dl->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(canvasMax.x, origin.y + y), IM_COL32(42, 42, 48, 255));

	// Pin positions (screen) and hit testing
	struct PinHit { uint32_t node; std::string pin; bool output; PinType type; ImVec2 pos; };
	std::vector<PinHit> pins;
	const float fontSize = ImGui::GetFontSize() * zoom;
	ImFont* font = ImGui::GetFont();
	for (const GraphNode& n : m.graph.nodes) {
		const NodeDef* def = lib.Find(n.type);
		if (!def) continue;
		for (size_t i = 0; i < def->inputs.size(); ++i) {
			pins.push_back({ n.id, def->inputs[i].name, false, def->inputs[i].type, toScreen(n.x, n.y + kHeaderHeight + kRowHeight * (static_cast<float>(i) + 0.5f)) });
		}
		for (size_t i = 0; i < def->outputs.size(); ++i) {
			const PinType t = def->outputs[i].type == PinType::Any ? ResolvedOutputType(m.graph, n.id, def->outputs[i].name, lib) : def->outputs[i].type;
			pins.push_back({ n.id, def->outputs[i].name, true, t, toScreen(n.x + kNodeWidth, n.y + kHeaderHeight + kRowHeight * (static_cast<float>(i) + 0.5f)) });
		}
	}
	const auto pinAt = [&](ImVec2 p) -> const PinHit* {
		const float r = (std::max)(8.0f * zoom, 6.0f);
		for (const PinHit& h : pins) {
			if ((h.pos.x - p.x) * (h.pos.x - p.x) + (h.pos.y - p.y) * (h.pos.y - p.y) <= r * r) return &h;
		}
		return nullptr;
	};
	const auto findPin = [&](uint32_t node, const std::string& name, bool output) -> const PinHit* {
		for (const PinHit& h : pins) {
			if (h.node == node && h.pin == name && h.output == output) return &h;
		}
		return nullptr;
	};
	const auto bezier = [&](ImVec2 a, ImVec2 b, ImU32 color, float thickness) {
		const float d = (std::max)(std::abs(b.x - a.x) * 0.5f, 40.0f * zoom);
		dl->AddBezierCubic(a, ImVec2(a.x + d, a.y), ImVec2(b.x - d, b.y), b, color, thickness);
	};

	// Links
	for (const GraphLink& l : m.graph.links) {
		const PinHit* a = findPin(l.fromNode, l.fromPin, true);
		const PinHit* b = findPin(l.toNode, l.toPin, false);
		if (a && b) bezier(a->pos, b->pos, PinColor(a->type), 2.5f * zoom);
	}

	// Nodes
	const PinHit* hoveredPin = hovered ? pinAt(io.MousePos) : nullptr;
	uint32_t hoveredNode = 0;
	const uint32_t outputNode = m.FindOutputNode();
	for (const GraphNode& n : m.graph.nodes) {
		const NodeDef* def = lib.Find(n.type);
		const int rows = def ? NodeRows(*def) : 1;
		const ImVec2 p0 = toScreen(n.x, n.y);
		const ImVec2 p1 = toScreen(n.x + kNodeWidth, n.y + kHeaderHeight + kRowHeight * static_cast<float>(rows) + 6.0f);
		const bool isSelected = std::find(m.selection.begin(), m.selection.end(), n.id) != m.selection.end();
		const std::string nodeErrors = errorsOf(n.id);
		if (hovered && io.MousePos.x >= p0.x && io.MousePos.x <= p1.x && io.MousePos.y >= p0.y && io.MousePos.y <= p1.y) hoveredNode = n.id;
		dl->AddRectFilled(p0, p1, IM_COL32(44, 44, 52, 245), 6.0f * zoom);
		dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + kHeaderHeight * zoom), def ? CategoryColor(def->category) : IM_COL32(120, 40, 40, 255), 6.0f * zoom, ImDrawFlags_RoundCornersTop);
		const ImU32 border = !nodeErrors.empty() ? IM_COL32(240, 70, 70, 255) : isSelected ? IM_COL32(255, 200, 80, 255) : IM_COL32(20, 20, 24, 255);
		dl->AddRect(p0, p1, border, 6.0f * zoom, 0, (isSelected || !nodeErrors.empty()) ? 2.5f : 1.0f);
		const std::string title = std::string(nodeErrors.empty() ? "" : "! ") + Tr(n.type.c_str());
		dl->AddText(font, fontSize, ImVec2(p0.x + 8.0f * zoom, p0.y + 5.0f * zoom), IM_COL32(245, 245, 245, 255), title.c_str());
		if (!def) continue;
		for (size_t i = 0; i < def->inputs.size(); ++i) {
			const PinDef& pin = def->inputs[i];
			const float y = n.y + kHeaderHeight + kRowHeight * static_cast<float>(i);
			const bool linked = m.graph.LinkInto(n.id, pin.name) != nullptr;
			const ImVec2 at = toScreen(n.x, y + kRowHeight * 0.5f);
			if (linked) dl->AddCircleFilled(at, 5.0f * zoom, PinColor(pin.type));
			else dl->AddCircle(at, 5.0f * zoom, PinColor(pin.type), 0, 1.5f * zoom);
			std::string label = Tr(pin.name.c_str());
			if (!linked && n.id != outputNode) label += "  " + ValueText(n, pin);
			else if (!linked) label += "  (" + std::string(Tr(pin.defaultLabel.c_str())) + ")";
			dl->AddText(font, fontSize * 0.92f, ImVec2(at.x + 10.0f * zoom, at.y - fontSize * 0.5f), linked ? IM_COL32(230, 230, 230, 255) : IM_COL32(150, 150, 160, 255), label.c_str());
		}
		for (size_t i = 0; i < def->outputs.size(); ++i) {
			const PinDef& pin = def->outputs[i];
			const PinType t = pin.type == PinType::Any ? ResolvedOutputType(m.graph, n.id, pin.name, lib) : pin.type;
			const ImVec2 at = toScreen(n.x + kNodeWidth, n.y + kHeaderHeight + kRowHeight * (static_cast<float>(i) + 0.5f));
			dl->AddCircleFilled(at, 5.0f * zoom, PinColor(t));
			const char* label = Tr(pin.name.c_str());
			const ImVec2 size = font->CalcTextSizeA(fontSize * 0.92f, FLT_MAX, 0.0f, label);
			dl->AddText(font, fontSize * 0.92f, ImVec2(at.x - 10.0f * zoom - size.x, at.y - fontSize * 0.5f), IM_COL32(230, 230, 230, 255), label);
		}
		if (!def->params.empty()) {
			// Settings summary: colour swatch, texture name, or the first value
			const float y = n.y + kHeaderHeight + kRowHeight * static_cast<float>(rows - 1);
			const ImVec2 a = toScreen(n.x + 8.0f, y + 3.0f);
			const ParamDef& first = def->params.front();
			const auto vit = n.values.find(first.name);
			const Vector4 v = vit != n.values.end() ? vit->second : first.defaultValue;
			if (first.kind == ParamKind::Color) {
				const auto u8 = [](float f) { return static_cast<int>(std::clamp(f, 0.0f, 1.0f) * 255.0f); };
				dl->AddRectFilled(a, toScreen(n.x + kNodeWidth - 8.0f, y + kRowHeight - 3.0f), IM_COL32(u8(v.x), u8(v.y), u8(v.z), 255), 3.0f * zoom);
			} else {
				std::string text;
				if (first.kind == ParamKind::Texture) {
					const auto t = n.texts.find(first.name);
					text = (t == n.texts.end() || t->second.empty()) ? std::string(Tr("Emitter texture")) : std::filesystem::path(t->second).filename().string();
				} else if (first.kind == ParamKind::Choice) {
					const size_t index = static_cast<size_t>(std::clamp(v.x, 0.0f, static_cast<float>(first.choices.size() - 1)));
					text = std::string(Tr(first.name.c_str())) + ": " + Tr(first.choices[index].c_str());
				} else if (first.kind == ParamKind::Vector3) {
					text = std::format("{:.2f}, {:.2f}, {:.2f}", v.x, v.y, v.z);
				} else {
					text = std::format("{:.3g}", v.x);
				}
				dl->AddText(font, fontSize * 0.9f, a, IM_COL32(200, 200, 140, 255), text.c_str());
			}
		}
		if (!nodeErrors.empty()) {
			dl->AddText(font, fontSize * 0.85f, ImVec2(p0.x, p1.y + 3.0f * zoom), IM_COL32(255, 110, 110, 255), nodeErrors.c_str(), nullptr, (p1.x - p0.x) * 1.6f);
		}
	}

	// ---- Interaction ----
	const auto openAddMenu = [&](ImVec2 at) {
		materialAddAt_ = toCanvas(at);
		materialAddSearch_[0] = '\0';
		materialAddFocus_ = true;
		ImGui::OpenPopup("##MatAddNode");
	};
	if (materialLink_.active) {
		// Dragging a new link: compatible pins light up, the reason shows when it does not fit
		const PinHit* from = findPin(materialLink_.node, materialLink_.pin, materialLink_.fromOutput);
		const ImVec2 a = from ? from->pos : io.MousePos;
		std::string why;
		bool fits = false;
		if (hoveredPin && hoveredPin->output != materialLink_.fromOutput && hoveredPin->node != materialLink_.node) {
			NodeGraph trial = m.graph;
			fits = materialLink_.fromOutput ? trial.Connect(materialLink_.node, materialLink_.pin, hoveredPin->node, hoveredPin->pin, lib, &why)
				: trial.Connect(hoveredPin->node, hoveredPin->pin, materialLink_.node, materialLink_.pin, lib, &why);
		}
		for (const PinHit& h : pins) {
			if (h.output == materialLink_.fromOutput || h.node == materialLink_.node) continue;
			const bool shapeFits = materialLink_.fromOutput ? CanConvert(materialLink_.type, h.type) : CanConvert(h.type, materialLink_.type);
			if (shapeFits) dl->AddCircle(h.pos, 8.0f * zoom, IM_COL32(255, 255, 255, 120), 0, 1.5f);
		}
		const ImU32 lineColor = (hoveredPin && !fits) ? IM_COL32(240, 80, 80, 255) : PinColor(materialLink_.type);
		if (materialLink_.fromOutput) bezier(a, io.MousePos, lineColor, 2.5f * zoom);
		else bezier(io.MousePos, a, lineColor, 2.5f * zoom);
		if (hoveredPin && !fits && !why.empty()) ImGui::SetTooltip("%s", why.c_str());
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			if (hoveredPin && fits) {
				if (materialLink_.fromOutput) m.Connect(materialLink_.node, materialLink_.pin, hoveredPin->node, hoveredPin->pin, nullptr);
				else m.Connect(hoveredPin->node, hoveredPin->pin, materialLink_.node, materialLink_.pin, nullptr);
				materialLink_.active = false;
			} else if (!hoveredPin && hovered) {
				// Dropped on empty space: pick a node that fits, it arrives already connected
				materialPendingLink_ = materialLink_;
				materialLink_.active = false;
				openAddMenu(io.MousePos);
			} else {
				materialLink_.active = false;
			}
		}
	} else if (hovered && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		if (hoveredPin) {
			if (!hoveredPin->output && io.KeyCtrl) {
				m.Disconnect(hoveredPin->node, hoveredPin->pin); // Ctrl + click an input: remove its link
			} else {
				materialLink_ = { hoveredPin->node, hoveredPin->pin, hoveredPin->output, hoveredPin->type, true };
			}
		} else if (hoveredNode != 0) {
			const bool already = std::find(m.selection.begin(), m.selection.end(), hoveredNode) != m.selection.end();
			if (io.KeyCtrl) {
				if (already) m.selection.erase(std::find(m.selection.begin(), m.selection.end(), hoveredNode));
				else m.selection.push_back(hoveredNode);
			} else if (!already) {
				m.selection = { hoveredNode };
			}
			materialNodeDrag_ = true;
			materialNodeMoved_ = false;
		} else {
			m.selection.clear();
		}
	}
	if (materialNodeDrag_) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
				for (uint32_t id : m.selection) {
					if (GraphNode* n = m.graph.FindMutable(id)) {
						n->x += io.MouseDelta.x / zoom;
						n->y += io.MouseDelta.y / zoom;
						materialNodeMoved_ = true;
					}
				}
			}
		} else {
			materialNodeDrag_ = false;
			if (materialNodeMoved_) m.Commit();
		}
	}
	if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
		if (hoveredNode != 0) {
			if (std::find(m.selection.begin(), m.selection.end(), hoveredNode) == m.selection.end()) m.selection = { hoveredNode };
			ImGui::OpenPopup("##MatNodeMenu");
		} else {
			materialPendingLink_ = {};
			openAddMenu(io.MousePos);
		}
	}
	// Drop from the palette
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GRAPH_NODE_TYPE")) {
			const Vector2 at = toCanvas(io.MousePos);
			m.AddNode(static_cast<const char*>(payload->Data), at.x - kNodeWidth * 0.5f, at.y - 10.0f);
		}
		ImGui::EndDragDropTarget();
	}
	if (hoveredPin && !materialLink_.active) {
		const NodeDef* def = lib.Find(m.graph.Find(hoveredPin->node)->type);
		const PinDef* pin = nullptr;
		for (const PinDef& p : hoveredPin->output ? def->outputs : def->inputs) {
			if (p.name == hoveredPin->pin) pin = &p;
		}
		std::string tip = std::format("{} ({})", Tr(hoveredPin->pin.c_str()), PinTypeName(hoveredPin->type));
		if (pin && !pin->tooltip.empty()) tip += "\n" + std::string(Tr(pin->tooltip.c_str()));
		tip += "\n" + std::string(Tr(hoveredPin->output ? "Drag to an input to connect." : "Drag to an output to connect. Ctrl+click removes the link."));
		ImGui::SetTooltip("%s", tip.c_str());
	} else if (hoveredNode != 0 && !materialNodeDrag_ && !materialLink_.active) {
		const std::string errs = errorsOf(hoveredNode);
		if (!errs.empty()) ImGui::SetTooltip("%s", errs.c_str());
	}

	// Keys (window focused, not typing)
	if (focused && !io.WantTextInput) {
		if (ImGui::IsKeyPressed(ImGuiKey_Delete)) m.DeleteSelection();
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) m.Duplicate();
		if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) m.Undo();
		if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)))) m.Redo();
		if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) materialGraphFit_ = true;
	}
	if (m.graph.nodes.size() <= 1) {
		dl->AddText(ImVec2(origin.x + 16.0f, canvasMax.y - 28.0f), IM_COL32(170, 170, 180, 255),
			Tr("Right-click to add a node, or drag one from the list on the left."));
	}
	dl->PopClipRect();

	// Node menu
	if (ImGui::BeginPopup("##MatNodeMenu")) {
		if (ImGui::MenuItem(TrId("Duplicate"), "Ctrl+D")) m.Duplicate();
		if (ImGui::MenuItem(TrId("Delete"), "Del", false, !(m.selection.size() == 1 && m.selection.front() == outputNode))) m.DeleteSelection();
		ImGui::EndPopup();
	}
	// Add-node menu: type to search, Enter takes the first; after a link drop only nodes that fit are listed
	if (ImGui::BeginPopup("##MatAddNode")) {
		NodeSearchFilter filter;
		if (materialPendingLink_.active) {
			if (materialPendingLink_.fromOutput) filter.fromOutput = materialPendingLink_.type;
			else filter.intoInput = materialPendingLink_.type;
		}
		if (materialAddFocus_) {
			ImGui::SetKeyboardFocusHere();
			materialAddFocus_ = false;
		}
		ImGui::SetNextItemWidth(220.0f);
		const bool enter = ImGui::InputTextWithHint("##MatAddSearch", Tr("Search nodes..."), materialAddSearch_, sizeof(materialAddSearch_), ImGuiInputTextFlags_EnterReturnsTrue);
		const std::vector<const NodeDef*> options = SearchNodes(lib, materialAddSearch_, kDomainMaterial, filter);
		const NodeDef* picked = enter && !options.empty() ? options.front() : nullptr;
		ImGui::BeginChild("##MatAddList", ImVec2(260.0f, 280.0f));
		std::string category;
		for (const NodeDef* def : options) {
			if (materialAddSearch_[0] == '\0' && def->category != category) {
				category = def->category;
				ImGui::SeparatorText(Tr(category.c_str()));
			}
			if (ImGui::Selectable(Tr(def->type.c_str()))) picked = def;
			ImGui::SetItemTooltip("%s", Tr(def->tooltip.c_str()));
		}
		if (options.empty()) ImGui::TextDisabled("%s", Tr("No node fits here."));
		ImGui::EndChild();
		if (picked) {
			if (materialPendingLink_.active) {
				std::string why;
				if (m.AddNodeConnected(picked->type, materialAddAt_.x, materialAddAt_.y - 10.0f, materialPendingLink_.node, materialPendingLink_.pin, materialPendingLink_.fromOutput, &why) == 0) {
					LogMessage("[Material] " + why, 1);
				}
			} else {
				m.AddNode(picked->type, materialAddAt_.x, materialAddAt_.y - 10.0f);
			}
			materialPendingLink_ = {};
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	} else {
		materialPendingLink_ = {};
	}
	ImGui::EndChild();
	ImGui::SameLine();

	// ---- Details ----
	ImGui::BeginChild("##MatDetails", ImVec2(detailsW, avail.y), true);
	// Live preview of the material on a few particles
	if (MaterialPreviewActive()) {
		const float side = (std::min)(ImGui::GetContentRegionAvail().x, 220.0f);
		ImGui::Image(ImTextureRef((ImTextureID)modelPreviewRT_.GetSrvGPUHandle().ptr), ImVec2(side, side));
		ImGui::SetItemTooltip("%s", Tr("Preview: this material on a few particles (white, fading out)"));
	} else {
		ImGui::TextDisabled("%s", Tr("Preview is paused while a 3D model preview is open in the Project panel."));
	}
	GraphNode* node = m.selection.size() == 1 ? m.graph.FindMutable(m.selection.front()) : nullptr;
	const NodeDef* def = node ? lib.Find(node->type) : nullptr;
	if (!node || !def) {
		ImGui::SeparatorText(Tr("How to use"));
		const char* tips[] = {
			"Right-click the canvas (or drag from the list) to add a node.",
			"Drag from a pin to another pin to connect them. Pins of the same colour fit; a single number (grey) fits everything.",
			"Drop a link on empty space to get only the nodes that fit, already connected.",
			"Material Output decides the final colour and opacity. Unconnected, it keeps the emitter's own look.",
			"Changes are saved at once: particles using this material update in the Scene view.",
			"Middle drag or Alt + drag: move the view. Wheel: zoom. F: show all. Del: delete. Ctrl+D: duplicate.",
		};
		for (const char* tip : tips) {
			ImGui::Bullet(); // BulletText does not wrap
			ImGui::SameLine();
			ImGui::TextWrapped("%s", Tr(tip));
		}
		if (!problems.empty()) {
			ImGui::SeparatorText(Tr("Problems"));
			for (const GraphError& e : problems) {
				ImGui::PushID(&e);
				if (ImGui::Selectable(e.message.c_str())) materialFocusNode_ = e.nodeId;
				ImGui::PopID();
			}
		}
	} else {
		ImGui::TextUnformatted(Tr(def->type.c_str()));
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("%s", Tr(def->tooltip.c_str()));
		const std::string nodeErrors = errorsOf(node->id);
		if (!nodeErrors.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", nodeErrors.c_str());
		ImGui::PopTextWrapPos();
		bool edited = false, finished = false;
		const auto track = [&](bool changed) {
			edited |= changed;
			finished |= ImGui::IsItemDeactivatedAfterEdit();
		};
		if (!def->params.empty()) ImGui::SeparatorText(Tr("Settings"));
		for (const ParamDef& param : def->params) {
			ImGui::PushID(param.name.c_str());
			Vector4& v = node->values[param.name];
			const char* label = Tr(param.name.c_str());
			switch (param.kind) {
			case ParamKind::Float: track(ImGui::DragFloat(label, &v.x, 0.01f)); break;
			case ParamKind::Vector3: track(ImGui::DragFloat3(label, &v.x, 0.01f)); break;
			case ParamKind::Color: track(ImGui::ColorEdit4(label, &v.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float)); break;
			case ParamKind::Choice: {
				int index = static_cast<int>(v.x);
				if (ImGui::BeginCombo(label, Tr(param.choices[std::clamp(index, 0, static_cast<int>(param.choices.size()) - 1)].c_str()))) {
					for (int i = 0; i < static_cast<int>(param.choices.size()); ++i) {
						if (ImGui::Selectable(Tr(param.choices[i].c_str()), i == index)) {
							v.x = static_cast<float>(i);
							edited = finished = true;
						}
					}
					ImGui::EndCombo();
				}
				break;
			}
			case ParamKind::Texture: {
				const std::string path = node->texts[param.name];
				const std::string shown = path.empty() ? std::string(Tr("Emitter texture")) : std::filesystem::path(path).filename().string();
				if (ImGui::BeginCombo(label, shown.c_str())) {
					if (ImGui::Selectable(Tr("Emitter texture"), path.empty())) {
						SetMaterialTexture(*node, param.name, "");
						edited = finished = true;
					}
					for (const std::string& texture : ProjectTextures(currentProjectRoot_)) {
						if (ImGui::Selectable(texture.c_str(), texture == path)) {
							SetMaterialTexture(*node, param.name, texture);
							edited = finished = true;
						}
					}
					ImGui::EndCombo();
				}
				// Textures can also be dropped here from the Project panel
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RIZE_ASSET_PATH")) {
						const std::string dropped = AssetDb().ToAssetPath(static_cast<const char*>(payload->Data));
						if (!dropped.empty()) {
							SetMaterialTexture(*node, param.name, dropped);
							edited = finished = true;
						}
					}
					ImGui::EndDragDropTarget();
				}
				break;
			}
			}
			if (!param.tooltip.empty()) ImGui::SetItemTooltip("%s", Tr(param.tooltip.c_str()));
			ImGui::PopID();
		}
		bool anyFree = false;
		for (const PinDef& pin : def->inputs) {
			if (m.graph.LinkInto(node->id, pin.name)) continue;
			if (!anyFree) ImGui::SeparatorText(Tr("Inputs"));
			anyFree = true;
			ImGui::PushID(pin.name.c_str());
			const char* label = Tr(pin.name.c_str());
			if (!pin.defaultExpr.empty()) {
				ImGui::TextUnformatted(std::format("{}: {}", label, Tr(pin.defaultLabel.c_str())).c_str());
				ImGui::SetItemTooltip("%s", Tr("Connect a node to this input to change it."));
			} else {
				Vector4& v = node->values[pin.name];
				switch (pin.type) {
				case PinType::Float2: track(ImGui::DragFloat2(label, &v.x, 0.01f)); break;
				case PinType::Float3: track(ImGui::DragFloat3(label, &v.x, 0.01f)); break;
				case PinType::Float4: track(ImGui::ColorEdit4(label, &v.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float)); break;
				default: track(ImGui::DragFloat(label, &v.x, 0.01f)); break;
				}
				if (!pin.tooltip.empty()) ImGui::SetItemTooltip("%s", Tr(pin.tooltip.c_str()));
			}
			ImGui::PopID();
		}
		if (finished) m.Commit();
		else if (edited) materialValueEditing_ = true;
		if (materialValueEditing_ && !ImGui::IsAnyItemActive()) {
			materialValueEditing_ = false;
			m.Commit();
		}
		ImGui::Spacing();
		if (node->id != outputNode && ImGui::Button(TrId("Delete Node"))) m.DeleteSelection();
	}
	ImGui::EndChild();

	// Saved as soon as a change is finished (not in the middle of a drag): the Scene view follows
	if (m.dirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !materialValueEditing_) SaveMaterialGraph();
	ImGui::End();
#endif
}
