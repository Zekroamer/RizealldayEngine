#include "Timeline.h"
#include "SceneJson.h"
#include <algorithm>
#include <cmath>

Vector3 TimelineTrack::Sample(float time) const
{
	if (keys.empty()) return { 0.0f, 0.0f, 0.0f };
	if (!(time > keys.front().time)) return keys.front().value; // also NaN
	if (time >= keys.back().time) return keys.back().value;
	const auto next = std::upper_bound(keys.begin(), keys.end(), time, [](float t, const TimelineKey& key) { return t < key.time; });
	const TimelineKey& a = *(next - 1);
	const TimelineKey& b = *next;
	float t = (time - a.time) / (b.time - a.time);
	if (a.smooth) t = t * t * (3.0f - 2.0f * t);
	return { a.value.x + (b.value.x - a.value.x) * t, a.value.y + (b.value.y - a.value.y) * t, a.value.z + (b.value.z - a.value.z) * t };
}

Quaternion TimelineTrack::SampleRotation(float time) const
{
	if (keys.empty()) return Quaternion::Identity();
	if (!(time > keys.front().time)) return Quaternion::FromEuler(keys.front().value); // also NaN
	if (time >= keys.back().time) return Quaternion::FromEuler(keys.back().value);
	const auto next = std::upper_bound(keys.begin(), keys.end(), time, [](float t, const TimelineKey& key) { return t < key.time; });
	const TimelineKey& a = *(next - 1);
	const TimelineKey& b = *next;
	float t = (time - a.time) / (b.time - a.time);
	if (a.smooth) t = t * t * (3.0f - 2.0f * t);
	return Quaternion::Slerp(Quaternion::FromEuler(a.value), Quaternion::FromEuler(b.value), t);
}

size_t TimelineTrack::SetKey(const TimelineKey& key)
{
	for (size_t i = 0; i < keys.size(); ++i) {
		if (std::abs(keys[i].time - key.time) < 0.001f) {
			keys[i] = key;
			return i;
		}
	}
	const auto position = std::upper_bound(keys.begin(), keys.end(), key.time, [](float t, const TimelineKey& k) { return t < k.time; });
	return static_cast<size_t>(keys.insert(position, key) - keys.begin());
}

TimelineTrack& Timeline::FindOrAddTrack(const std::string& target, TimelineProperty property)
{
	for (TimelineTrack& track : tracks) {
		if (track.target == target && track.property == property) return track;
	}
	tracks.push_back({ target, property, {} });
	return tracks.back();
}

std::vector<const TimelineEvent*> Timeline::EventsBetween(float from, float to) const
{
	std::vector<const TimelineEvent*> found;
	for (const TimelineEvent& event : events) {
		const bool inside = to >= from ? (event.time > from && event.time <= to) : (event.time > from || event.time <= to);
		if (inside) found.push_back(&event);
	}
	return found;
}

std::string Timeline::ToJson() const
{
	using SceneJson::Json;
	Json root = Json::object();
	root["duration"] = SceneJson::Num(duration);
	Json trackList = Json::array();
	for (const TimelineTrack& track : tracks) {
		Json keyList = Json::array();
		for (const TimelineKey& key : track.keys) {
			Json k = Json::object();
			k["time"] = SceneJson::Num(key.time);
			k["value"] = SceneJson::Arr(key.value);
			k["smooth"] = key.smooth;
			keyList.push_back(k);
		}
		Json t = Json::object();
		t["target"] = track.target;
		t["property"] = static_cast<int>(track.property);
		t["keys"] = keyList;
		trackList.push_back(t);
	}
	root["tracks"] = trackList;
	Json eventList = Json::array();
	for (const TimelineEvent& event : events) {
		Json e = Json::object();
		e["time"] = SceneJson::Num(event.time);
		e["name"] = event.name;
		eventList.push_back(e);
	}
	root["events"] = eventList;
	return root.dump(1, '\t');
}

bool Timeline::FromJson(const std::string& text, Timeline& out)
{
	const auto root = SceneJson::Parse(text);
	if (!root || !root->is_object()) return false;
	Timeline result;
	SceneJson::Read(*root, "duration", result.duration);
	if (const auto list = root->find("tracks"); list != root->end() && list->is_array()) {
		for (const auto& t : *list) {
			if (!t.is_object()) continue;
			TimelineTrack track;
			SceneJson::Read(t, "target", track.target);
			int property = 0;
			SceneJson::Read(t, "property", property);
			track.property = static_cast<TimelineProperty>(std::clamp(property, 0, 2));
			if (const auto keys = t.find("keys"); keys != t.end() && keys->is_array()) {
				for (const auto& k : *keys) {
					if (!k.is_object()) continue;
					TimelineKey key;
					SceneJson::Read(k, "time", key.time);
					SceneJson::Read(k, "value", key.value);
					SceneJson::Read(k, "smooth", key.smooth);
					track.SetKey(key); // a hand-edited file may be out of order
				}
			}
			result.tracks.push_back(std::move(track));
		}
	}
	if (const auto list = root->find("events"); list != root->end() && list->is_array()) {
		for (const auto& e : *list) {
			if (!e.is_object()) continue;
			TimelineEvent event;
			SceneJson::Read(e, "time", event.time);
			SceneJson::Read(e, "name", event.name);
			result.events.push_back(std::move(event));
		}
	}
	out = std::move(result);
	return true;
}

float TimelineAdvance(float time, float deltaTime, float duration, bool loop, bool& finished)
{
	finished = false;
	if (!(duration > 0.0f)) {
		finished = !loop;
		return 0.0f;
	}
	const float next = time + (std::max)(deltaTime, 0.0f);
	if (next < duration) return next;
	if (loop) return std::fmod(next, duration);
	finished = true;
	return duration;
}
