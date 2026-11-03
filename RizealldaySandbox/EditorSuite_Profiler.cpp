#include "EditorSuiteInternal.h"

#ifdef USE_IMGUI

#include <cstdio>

namespace {

constexpr float kBudget60Ms = 16.6f;
constexpr float kBudget30Ms = 33.3f;
const ImVec4 kWarningColor(1.0f, 0.75f, 0.2f, 1.0f);

/// One table row per scope: name (indented by nesting depth), moving average, last frame.
void DrawScopeTable(const char* id, const std::vector<ProfilerScopeResult>& rows, const char* emptyText)
{
	if (rows.empty()) {
		ImGui::TextDisabled("%s", emptyText);
		return;
	}
	if (ImGui::BeginTable(id, 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 2.0f);
		ImGui::TableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("Last (ms)", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableHeadersRow();
		for (const ProfilerScopeResult& row : rows) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			const float indent = 12.0f * static_cast<float>(row.depth);
			if (indent > 0.0f) ImGui::Indent(indent);
			ImGui::TextUnformatted(row.name);
			if (indent > 0.0f) ImGui::Unindent(indent);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%.3f", row.avgMs);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%.3f", row.ms);
		}
		ImGui::EndTable();
	}
}

} // namespace

void EditorSuite::DrawProfilerWindow()
{
	// First open: tall enough to show every section, docked over the right part of the viewport
	// (not on top of the Hierarchy panel)
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowSize(ImVec2(440.0f, 700.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.75f - 460.0f, viewport->WorkPos.y + 110.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TrId("Profiler"), &showProfiler_)) {
		ImGui::End();
		return;
	}

	const ProfilerSnapshot& snap = Profiler::GetInstance()->GetLastFrame();

	// ---- 1. Header ----
	const bool slowFrame = snap.frameAvgMs > kBudget30Ms;
	if (slowFrame) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
	ImGui::Text(Tr("FPS: %.1f"), snap.fps);
	ImGui::SameLine();
	ImGui::Text(Tr("| CPU: %.2f ms"), snap.frameAvgMs);
	if (slowFrame) ImGui::PopStyleColor();
	ImGui::SameLine();
	if (snap.gpuValid) {
		ImGui::Text(Tr("| GPU: %.2f ms"), snap.gpuFrameAvgMs);
	} else {
		ImGui::TextDisabled(Tr("| GPU: n/a"));
	}
	if (dxCommon_) {
		ImGui::TextDisabled(Tr("Adapter: %s"), dxCommon_->GetAdapterName().c_str());
	}
	if (!Profiler::GetInstance()->IsGpuEnabled()) {
		ImGui::TextDisabled(Tr("GPU timestamps are unavailable on this device."));
	}

	ImGui::Separator();

	// ---- 2. Frame time graph ----
	{
		float peak = 0.0f;
		for (uint32_t i = 0; i < snap.frameHistoryCount; ++i) {
			peak = (std::max)(peak, snap.frameHistory[i]);
		}
		const float scaleMax = (std::max)(40.0f, peak * 1.1f);
		char overlay[96];
		std::snprintf(overlay, sizeof(overlay), "%.2f ms  (refs: %.1f ms = 60 FPS, %.1f ms = 30 FPS)",
			snap.frameMs, kBudget60Ms, kBudget30Ms);
		ImGui::PlotLines("##FrameTimes", snap.frameHistory, static_cast<int>(ProfilerSnapshot::kFrameHistorySize),
			static_cast<int>(snap.frameHistoryOffset), overlay, 0.0f, scaleMax, ImVec2(-1.0f, 80.0f));
	}

	// ---- 3. CPU / GPU tables ----
	if (ImGui::CollapsingHeader(TrId("CPU"), ImGuiTreeNodeFlags_DefaultOpen)) {
		DrawScopeTable("##CpuScopes", snap.cpu, "No CPU scopes recorded yet.");
	}
	if (ImGui::CollapsingHeader(TrId("GPU"), ImGuiTreeNodeFlags_DefaultOpen)) {
		DrawScopeTable("##GpuScopes", snap.gpu, "No GPU timings yet (results arrive about 2 frames late).");
	}

	// ---- 4. Counters ----
	if (ImGui::CollapsingHeader(TrId("Counters"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(Tr("Draw calls: %u (%u instances)"), snap.drawCalls, snap.instances);

		const float uploadKb = static_cast<float>(snap.uploadBytesUsed) / 1024.0f;
		const float uploadCapMb = static_cast<float>(snap.uploadCapacity) / (1024.0f * 1024.0f);
		const float uploadFraction = (snap.uploadCapacity > 0)
			? static_cast<float>(snap.uploadBytesUsed) / static_cast<float>(snap.uploadCapacity) : 0.0f;
		const bool uploadWarning = uploadFraction > 0.8f;
		char uploadText[64];
		std::snprintf(uploadText, sizeof(uploadText), "%.1f KB / %.0f MB", uploadKb, uploadCapMb);
		ImGui::TextUnformatted("Upload memory (this frame)");
		if (uploadWarning) ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kWarningColor);
		ImGui::ProgressBar(uploadFraction, ImVec2(-1.0f, 0.0f), uploadText);
		if (uploadWarning) ImGui::PopStyleColor();

		ImGui::Text(Tr("SRV descriptors: %u / %u"), snap.srvUsed, snap.srvCapacity);
		ImGui::Text(Tr("GameObjects: %zu"), gameObjects_.size());
	}

	ImGui::End();
}

#endif // USE_IMGUI
