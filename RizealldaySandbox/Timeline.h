#pragma once
#include "math/Vector3.h"
#include "math/Quaternion.h"
#include <string>
#include <vector>

// A sequence for cutscenes and scripted moments: keyframes that move objects, named events for scripts.
// Pure data and math; the asset is a .timeline.json file.

enum class TimelineProperty : int { Position, Rotation, Scale };

struct TimelineKey {
	float time = 0.0f;
	Vector3 value{ 0.0f, 0.0f, 0.0f };
	bool smooth = true; // ease in and out towards the next key; false = constant speed
};

struct TimelineTrack {
	std::string target; // object name
	TimelineProperty property = TimelineProperty::Position;
	std::vector<TimelineKey> keys; // sorted by time

	Vector3 Sample(float time) const;
	/// Rotation tracks: keys are Euler angles, played back along the shortest turn between them
	Quaternion SampleRotation(float time) const;
	/// Replaces a key at the same time (within 1 ms), otherwise inserts it in order. Returns its index.
	size_t SetKey(const TimelineKey& key);
};

struct TimelineEvent {
	float time = 0.0f;
	std::string name;
};

struct Timeline {
	float duration = 5.0f;
	std::vector<TimelineTrack> tracks;
	std::vector<TimelineEvent> events;

	TimelineTrack& FindOrAddTrack(const std::string& target, TimelineProperty property);
	/// Events with from < time <= to; a loop that wraps (to < from) also collects the start of the next pass
	std::vector<const TimelineEvent*> EventsBetween(float from, float to) const;
	std::string ToJson() const;
	static bool FromJson(const std::string& text, Timeline& out);
};

/// Next playback time. finished = a timeline without loop reached its end.
float TimelineAdvance(float time, float deltaTime, float duration, bool loop, bool& finished);

inline constexpr const char* kTimelineComponent = "Timeline";

/// Plays a Timeline asset during Play. Scripts: SetComponentBool("Timeline", "playing", true), SetComponentFloat("Timeline", "time", 0).
struct TimelineComponent {
	char timeline[260] = {};
	bool playing = true; // a saved true = starts with Play
	bool loop = false;
	float speed = 1.0f;
	float time = 0.0f;
};
