#include "EditorSuiteInternal.h"
#include "SceneJson.h"
#include <fstream>
#include <sstream>

// Timeline component and window (Fase 7). The data and its math are in Timeline.h.

void EditorSuite::RegisterTimelineComponent()
{
	ComponentInfo& info = RegisterDataComponent<TimelineComponent>(kTimelineComponent, "Rendering", {
		RIZE_FIELD(TimelineComponent, timeline, ComponentFieldType::Asset, 0.0f, 0.0f),
		RIZE_FIELD(TimelineComponent, playing, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(TimelineComponent, loop, ComponentFieldType::Bool, 0.0f, 0.0f),
		RIZE_FIELD(TimelineComponent, speed, ComponentFieldType::Float, 0.0f, 100.0f),
		RIZE_FIELD(TimelineComponent, time, ComponentFieldType::Float, 0.0f, 3600.0f) });
#ifdef USE_IMGUI
	info.drawExtra = [](EditorSuite& suite, GameObject& obj, ComponentData& data) {
		auto* component = static_cast<TimelineComponent*>(data.Data());
		if (component->timeline[0] == '\0' && ImGui::Button(TrId("New Timeline Asset"))) {
			const std::string asset = suite.CreateTimelineAsset(obj.name);
			if (!asset.empty()) {
				strncpy_s(component->timeline, asset.c_str(), _TRUNCATE);
				suite.isSceneDirty_ = true;
				suite.PushUndoSnapshot();
			}
		}
		if (component->timeline[0] != '\0' && ImGui::Button(TrId("Open Timeline Window"))) suite.showTimelineWindow_ = true;
	};
#else
	(void)info;
#endif
}

std::shared_ptr<Timeline> EditorSuite::GetTimeline(const std::string& assetPath)
{
	if (assetPath.empty()) return nullptr;
	if (const auto it = timelines_.find(assetPath); it != timelines_.end()) return it->second;
	std::shared_ptr<Timeline> loaded;
	std::ifstream in(currentProjectRoot_ / std::filesystem::path(assetPath), std::ios::binary);
	if (in) {
		std::stringstream text;
		text << in.rdbuf();
		auto timeline = std::make_shared<Timeline>();
		if (Timeline::FromJson(text.str(), *timeline)) {
			loaded = timeline;
		} else {
			LogMessage(std::format("[Timeline] Could not read {}", assetPath), 2);
		}
	}
	timelines_[assetPath] = loaded; // a missing file is not looked for again every frame
	return loaded;
}

bool EditorSuite::SaveTimeline(const std::string& assetPath, const Timeline& timeline)
{
	const std::filesystem::path file = currentProjectRoot_ / std::filesystem::path(assetPath);
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	if (!out) {
		LogMessage(std::format("[Timeline] Could not write {}", assetPath), 2);
		return false;
	}
	out << timeline.ToJson();
	out.close();
	auto& cached = timelines_[assetPath];
	if (!cached) cached = std::make_shared<Timeline>();
	if (cached.get() != &timeline) *cached = timeline;
	return true;
}

std::string EditorSuite::CreateTimelineAsset(const std::string& name)
{
	std::string base;
	for (char c : name) base += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
	if (base.empty()) base = "New";
	std::error_code ec;
	std::string assetPath;
	for (int n = 0; n < 1000; ++n) {
		assetPath = std::format("Assets/Timelines/{}{}.timeline.json", base, n == 0 ? std::string() : std::to_string(n));
		if (!std::filesystem::exists(currentProjectRoot_ / std::filesystem::path(assetPath), ec)) break;
	}
	if (!SaveTimeline(assetPath, Timeline{})) return {};
	AssetDb().Refresh();
	LogMessage(std::format("[Timeline] Created {}", assetPath), 0);
	return assetPath;
}

void EditorSuite::ApplyTimeline(const Timeline& timeline, float time)
{
	for (const TimelineTrack& track : timeline.tracks) {
		if (track.keys.empty()) continue;
		GameObject* obj = FindGameObjectByName(track.target, false);
		if (!obj) continue;
		const Vector3 value = track.Sample(time);
		switch (track.property) {
		case TimelineProperty::Position:
			obj->translation = value;
			break;
		case TimelineProperty::Rotation:
			obj->rotationQuat = track.SampleRotation(time); // the short way round, not angle by angle
			obj->rotation = obj->rotationQuat.ToEuler();
			break;
		case TimelineProperty::Scale:
			obj->scale = value;
			break;
		}
		// Scripts read the view; without this they would write the old transform back
		if (sceneState_ != EditorSceneState::Edit || IsStandaloneMode()) GetOrUpdateScriptViewById(obj->id);
	}
}

void EditorSuite::UpdateTimelines(float deltaTime)
{
	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		if (!gameObjects_[i].isActive) continue;
		auto* component = gameObjects_[i].components.Get<TimelineComponent>(kTimelineComponent);
		if (!component || !component->playing) continue;
		const std::shared_ptr<Timeline> timeline = GetTimeline(component->timeline);
		if (!timeline) continue;
		const uint32_t ownerId = gameObjects_[i].id;
		const float before = component->time;
		bool finished = false;
		const float after = TimelineAdvance(before, deltaTime * component->speed, timeline->duration, component->loop, finished);
		component->time = after;
		if (finished) component->playing = false;
		ApplyTimeline(*timeline, after);
		// From the very start, events at 0 count too
		for (const TimelineEvent* event : timeline->EventsBetween(before <= 0.0f ? -1.0f : before, after)) {
			++timelineEventsFired_;
			DispatchScriptAnimationEvent(ownerId, event->name.c_str());
		}
	}
}

void EditorSuite::DrawTimelineWindow()
{
#ifdef USE_IMGUI
	const auto stopPreview = [&] {
		for (const TimelineRestore& restore : timelineRestore_) {
			if (GameObject* obj = FindGameObjectById(restore.id, false)) {
				obj->translation = restore.translation;
				obj->rotation = restore.rotation;
				obj->rotationQuat = Quaternion::FromEuler(restore.rotation);
				obj->scale = restore.scale;
			}
		}
		timelineRestore_.clear();
		timelinePreview_ = false;
	};

	if (!ImGui::Begin(TrId("Timeline"), &showTimelineWindow_)) {
		ImGui::End();
		return;
	}
	if (!showTimelineWindow_ && timelinePreview_) stopPreview();

	GameObject* selected = (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) ? &gameObjects_[selectedObjectIndex_] : nullptr;
	auto* component = selected ? selected->components.Get<TimelineComponent>(kTimelineComponent) : nullptr;
	const std::shared_ptr<Timeline> timeline = component ? GetTimeline(component->timeline) : nullptr;
	if (!timeline) {
		ImGui::TextDisabled("%s", Tr("Select an object with a Timeline component that has an asset."));
		if (timelinePreview_) stopPreview();
		ImGui::End();
		return;
	}
	const std::string asset = component->timeline;
	bool changed = false;

	if (ImGui::Checkbox(TrId("Preview"), &timelinePreview_)) {
		if (timelinePreview_) {
			timelineRestore_.clear();
			for (const TimelineTrack& track : timeline->tracks) {
				if (const GameObject* obj = FindGameObjectByName(track.target, false)) {
					timelineRestore_.push_back({ obj->id, obj->translation, obj->rotation, obj->scale });
				}
			}
		} else {
			stopPreview();
		}
	}
	ImGui::SetItemTooltip("%s", Tr("Shows the time on the slider in the scene. Turning it off puts the objects back."));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100.0f);
	changed |= ImGui::DragFloat(TrId("Duration"), &timeline->duration, 0.05f, 0.01f, 3600.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat(TrId("Time"), &timelineCursor_, 0.0f, timeline->duration, "%.2f s");
	if (timelinePreview_) ApplyTimeline(*timeline, timelineCursor_);

	// Adding keys: the transform of the object selected here, at the slider's time
	static int targetIndex = 0;
	targetIndex = std::clamp(targetIndex, 0, (std::max)(static_cast<int>(gameObjects_.size()) - 1, 0));
	if (!gameObjects_.empty()) {
		if (ImGui::BeginCombo(TrId("Target"), gameObjects_[targetIndex].name.c_str())) {
			for (int i = 0; i < static_cast<int>(gameObjects_.size()); ++i) {
				ImGui::PushID(i);
				if (ImGui::Selectable(gameObjects_[i].name.c_str(), i == targetIndex)) targetIndex = i;
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		GameObject& target = gameObjects_[targetIndex];
		const auto addKey = [&](TimelineProperty property, const Vector3& value) {
			timeline->FindOrAddTrack(target.name, property).SetKey({ timelineCursor_, value, true });
			changed = true;
		};
		if (timelinePreview_) ImGui::BeginDisabled(); // the transform on screen is the preview's, not the object's
		if (ImGui::Button(TrId("Key Position"))) addKey(TimelineProperty::Position, target.translation);
		ImGui::SameLine();
		if (ImGui::Button(TrId("Key Rotation"))) addKey(TimelineProperty::Rotation, target.rotation);
		ImGui::SameLine();
		if (ImGui::Button(TrId("Key Scale"))) addKey(TimelineProperty::Scale, target.scale);
		if (timelinePreview_) ImGui::EndDisabled();
	}

	// One row per track: a key is a diamond. Click to select, drag to move.
	ImGui::Separator();
	static const char* const kPropertyNames[] = { "Position", "Rotation", "Scale" };
	const float labelWidth = 200.0f;
	for (int t = 0; t < static_cast<int>(timeline->tracks.size()); ++t) {
		TimelineTrack& track = timeline->tracks[t];
		ImGui::PushID(t);
		ImGui::AlignTextToFramePadding();
		ImGui::Text(Tr("%s / %s"), track.target.c_str(), Tr(kPropertyNames[static_cast<int>(track.property)]));
		ImGui::SameLine(labelWidth);
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = (std::max)(ImGui::GetContentRegionAvail().x, 50.0f);
		const float height = ImGui::GetFrameHeight();
		ImGui::InvisibleButton("bar", ImVec2(width, height));
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(40, 42, 48, 255));
		const float scale = width / timeline->duration;
		const float cursorX = origin.x + timelineCursor_ * scale;
		draw->AddLine(ImVec2(cursorX, origin.y), ImVec2(cursorX, origin.y + height), IM_COL32(255, 200, 80, 255));
		const float midY = origin.y + height * 0.5f;
		for (int k = 0; k < static_cast<int>(track.keys.size()); ++k) {
			const float x = origin.x + track.keys[k].time * scale;
			const bool isSelected = t == timelineSelectedTrack_ && k == timelineSelectedKey_;
			draw->AddQuadFilled(ImVec2(x, midY - 6.0f), ImVec2(x + 6.0f, midY), ImVec2(x, midY + 6.0f), ImVec2(x - 6.0f, midY),
				isSelected ? IM_COL32(255, 220, 90, 255) : IM_COL32(170, 190, 230, 255));
			if (ImGui::IsItemClicked() && std::abs(ImGui::GetIO().MousePos.x - x) < 7.0f) {
				timelineSelectedTrack_ = t;
				timelineSelectedKey_ = k;
			}
		}
		if (t == timelineSelectedTrack_ && timelineSelectedKey_ >= 0 && timelineSelectedKey_ < static_cast<int>(track.keys.size()) && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
			TimelineKey moved = track.keys[timelineSelectedKey_];
			track.keys.erase(track.keys.begin() + timelineSelectedKey_);
			moved.time = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / scale, 0.0f, timeline->duration);
			timelineSelectedKey_ = static_cast<int>(track.SetKey(moved));
			changed = true;
		}
		ImGui::PopID();
	}
	if (timelineSelectedTrack_ >= 0 && timelineSelectedTrack_ < static_cast<int>(timeline->tracks.size())) {
		TimelineTrack& track = timeline->tracks[timelineSelectedTrack_];
		if (timelineSelectedKey_ >= 0 && timelineSelectedKey_ < static_cast<int>(track.keys.size())) {
			TimelineKey& key = track.keys[timelineSelectedKey_];
			ImGui::Text(Tr("%s %.2f s"), Tr("Key at"), key.time);
			ImGui::SameLine();
			changed |= ImGui::Checkbox(TrId("Smooth"), &key.smooth);
			ImGui::SameLine();
			if (ImGui::Button(TrId("Delete Key"))) {
				track.keys.erase(track.keys.begin() + timelineSelectedKey_);
				timelineSelectedKey_ = -1;
				if (track.keys.empty()) {
					timeline->tracks.erase(timeline->tracks.begin() + timelineSelectedTrack_);
					timelineSelectedTrack_ = -1;
				}
				changed = true;
			}
		}
	}

	ImGui::Separator();
	ImGui::TextDisabled("%s", Tr("Events: scripts on this object receive OnAnimationEvent(name)."));
	for (int e = 0; e < static_cast<int>(timeline->events.size()); ++e) {
		TimelineEvent& event = timeline->events[e];
		ImGui::PushID(1000 + e);
		char name[64] = {};
		strncpy_s(name, event.name.c_str(), _TRUNCATE);
		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::InputText("##name", name, sizeof(name))) {
			event.name = name;
			changed = true;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		changed |= ImGui::DragFloat("##time", &event.time, 0.01f, 0.0f, timeline->duration, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SameLine();
		bool removed = false;
		if (ImGui::Button("X")) {
			timeline->events.erase(timeline->events.begin() + e);
			removed = true;
			changed = true;
		}
		ImGui::PopID();
		if (removed) break;
	}
	if (ImGui::Button(TrId("Add Event"))) {
		timeline->events.push_back({ timelineCursor_, "event" });
		changed = true;
	}

	if (changed) SaveTimeline(asset, *timeline);
	ImGui::End();
#endif
}
