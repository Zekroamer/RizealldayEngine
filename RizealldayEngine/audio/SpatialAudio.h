#pragma once
#include "math/Vector3.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Pure audio decisions (no XAudio2): where a 3D sound sits for the listener, and which voice makes room for a new one.
namespace SpatialAudio {

struct Listener {
	Vector3 position{ 0.0f, 0.0f, 0.0f };
	Vector3 right{ 1.0f, 0.0f, 0.0f }; // unit vector towards the right ear
};

struct Levels {
	float gain = 0.0f;  // distance only, 0..1
	float left = 0.0f;  // final level of each ear
	float right = 0.0f;
};

/// Full volume up to minDistance, silent at maxDistance. Only the side matters: in front and behind sound alike.
inline Levels Compute(const Listener& listener, const Vector3& source, float minDistance, float maxDistance)
{
	const float dx = source.x - listener.position.x;
	const float dy = source.y - listener.position.y;
	const float dz = source.z - listener.position.z;
	const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
	if (!std::isfinite(distance) || !std::isfinite(minDistance) || !std::isfinite(maxDistance)) {
		return {};
	}
	const float nearLimit = (std::max)(minDistance, 0.01f);
	const float farLimit = (std::max)(maxDistance, nearLimit + 0.01f);

	Levels levels;
	if (distance <= nearLimit) {
		levels.gain = 1.0f;
	} else if (distance < farLimit) {
		const float t = (distance - nearLimit) / (farLimit - nearLimit);
		levels.gain = nearLimit / distance * (1.0f - t * t); // inverse distance, eased to reach exactly 0
	}

	float side = 0.0f;
	if (distance > 1.0e-5f) {
		side = (dx * listener.right.x + dy * listener.right.y + dz * listener.right.z) / distance;
		side = std::clamp(side, -1.0f, 1.0f);
	}
	// 0.85: the far ear still hears some of it. Inside minDistance the sound moves to the centre, so passing through the head does not click.
	const float pan = side * 0.85f * (std::min)(distance / nearLimit, 1.0f);
	const float angle = (pan + 1.0f) * 0.785398163f; // constant power: same loudness at every angle
	levels.left = std::cos(angle) * levels.gain;
	levels.right = std::sin(angle) * levels.gain;
	return levels;
}

/// Pitch factor from motion along the line between them (speeds in m/s). 1 = no change; kept within 0.5 .. 2.
inline float DopplerFactor(const Vector3& listener, const Vector3& listenerVelocity, const Vector3& source, const Vector3& sourceVelocity, float speedOfSound = 343.0f)
{
	const float dx = source.x - listener.x;
	const float dy = source.y - listener.y;
	const float dz = source.z - listener.z;
	const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
	if (!(distance > 1.0e-4f) || !std::isfinite(distance)) return 1.0f;
	// Speeds no object has are teleports (respawn, camera cut), not motion
	const auto plausible = [](const Vector3& v) {
		const float speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		return std::isfinite(speed) && speed < 1000.0f ? v : Vector3{ 0.0f, 0.0f, 0.0f };
	};
	const Vector3 vl = plausible(listenerVelocity);
	const Vector3 vs = plausible(sourceVelocity);
	// Along the line from the listener to the sound: positive = moving away from the listener
	const float listenerAlong = (vl.x * dx + vl.y * dy + vl.z * dz) / distance;
	const float sourceAlong = (vs.x * dx + vs.y * dy + vs.z * dz) / distance;
	const float denominator = (std::max)(speedOfSound + sourceAlong, 1.0f);
	return std::clamp((speedOfSound + listenerAlong) / denominator, 0.5f, 2.0f);
}

struct VoiceInfo {
	uint32_t sound = 0;
	float loudness = 0.0f;  // volume x distance gain
	bool isProtected = false; // music is never replaced by an effect
};

constexpr int kNoSteal = -1; // there is room
constexpr int kRefuse = -2;  // no room, and nothing may be replaced

/// voices = oldest first
inline int PickVoiceToSteal(const std::vector<VoiceInfo>& voices, uint32_t sound, int maxVoices, int maxVoicesPerSound)
{
	int sameSound = 0;
	int oldestSameSound = -1;
	int quietest = -1;
	for (int i = 0; i < static_cast<int>(voices.size()); ++i) {
		const VoiceInfo& voice = voices[static_cast<size_t>(i)];
		if (voice.sound == sound) {
			++sameSound;
			if (!voice.isProtected && oldestSameSound < 0) oldestSameSound = i;
		}
		if (!voice.isProtected && (quietest < 0 || voice.loudness < voices[static_cast<size_t>(quietest)].loudness)) quietest = i;
	}
	if (sameSound >= maxVoicesPerSound) {
		return oldestSameSound >= 0 ? oldestSameSound : kRefuse;
	}
	if (static_cast<int>(voices.size()) >= maxVoices) {
		return quietest >= 0 ? quietest : kRefuse;
	}
	return kNoSteal;
}

} // namespace SpatialAudio
