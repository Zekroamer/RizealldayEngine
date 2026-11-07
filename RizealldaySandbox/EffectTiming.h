#pragma once
#include "math/Vector3.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// Layered effects are prefabs: a root object with an "Effect Timing" component and child objects with
// GPU Particles / Trail (the layers). This header is the pure timing logic; EditorSuite_Effects.cpp applies it.

inline constexpr const char* kEffectTimingComponent = "Effect Timing";

/// When an effect layer emits, and the impact pulse it fires when it starts
struct EffectTimingComponent {
	float startDelay = 0.0f;     // seconds after the effect starts
	float duration = 1.0f;       // how long this layer emits; 0 = until the effect is removed
	bool loop = false;           // root: start over when every layer stopped emitting
	bool despawnWhenDone = true; // root, in Play: the instance goes back to its pool when its last particles died
	float pulseBloom = 0.0f;     // impact pulse fired when this layer starts (all 0 = none)
	float pulseBlur = 0.0f;
	float pulseShake = 0.0f;
	float pulseSeconds = 0.3f;

	// Runtime, not saved (kept on the root)
	float time = 0.0f;
	bool finished = false;
	std::vector<uint32_t> started; // layers whose start (burst, pulse) already fired this round
};

/// Starting points of the Effect Editor and GameObject > Visual Effects
inline constexpr const char* kEffectTemplates[] = { "Hit Spark", "Explosion", "Magic Aura", "Slash Trail" };

enum class EffectPhase { Waiting, Emitting, Done };

inline EffectPhase EffectLayerPhase(float startDelay, float duration, float time)
{
	if (time < startDelay) return EffectPhase::Waiting;
	if (duration <= 0.0f || time < startDelay + duration) return EffectPhase::Emitting;
	return EffectPhase::Done;
}

struct EffectLayerWindow {
	float startDelay = 0.0f;
	float duration = 0.0f;
	float tail = 0.0f; // how long its last particles / trail points live after it stops
};

/// When the last layer is finished; with `includeTail` the particles it emitted last have died too.
/// Infinite when a layer emits until the effect is removed.
inline float EffectLength(const std::vector<EffectLayerWindow>& layers, bool includeTail = true)
{
	float length = 0.0f;
	for (const EffectLayerWindow& layer : layers) {
		if (layer.duration <= 0.0f) return std::numeric_limits<float>::infinity();
		length = (std::max)(length, layer.startDelay + layer.duration + (includeTail ? (std::max)(layer.tail, 0.0f) : 0.0f));
	}
	return length;
}

struct EffectClock {
	float time = 0.0f;
	bool restarted = false; // a loop started over this step
	bool finished = false;  // a one-shot effect reached its end this step
};

/// One step of an effect's clock. `length` is the loop period or the full length of a one-shot effect.
inline EffectClock AdvanceEffect(float time, float deltaTime, float length, bool loop)
{
	EffectClock clock{ time };
	if (!std::isfinite(deltaTime) || deltaTime < 0.0f) return clock;
	clock.time = time + deltaTime;
	if (!std::isfinite(length) || clock.time < length) return clock;
	if (loop && length > 0.0f) {
		clock.time = std::fmod(clock.time, length);
		clock.restarted = true;
	} else {
		clock.time = length;
		clock.finished = true;
	}
	return clock;
}

/// Edit-mode test movement of the previewed effect (the saved position does not change)
enum class EffectPreviewMotion { None, Circle, FigureEight, Line, Count };
inline constexpr const char* kEffectPreviewMotionNames[] = { "None", "Circle", "Figure Eight", "Line" };

/// Offset from the effect's position at `time`: `radius` world units, `cyclesPerSecond` rounds per second
inline Vector3 EffectPreviewOffset(EffectPreviewMotion motion, float radius, float cyclesPerSecond, float time)
{
	const float angle = 6.2831853f * cyclesPerSecond * time;
	switch (motion) {
	case EffectPreviewMotion::Circle: return { radius * std::cos(angle), 0.0f, radius * std::sin(angle) };
	case EffectPreviewMotion::FigureEight: return { radius * std::sin(angle), 0.5f * radius * std::sin(2.0f * angle), 0.0f }; // a sword swing
	case EffectPreviewMotion::Line: return { radius * std::sin(angle), 0.0f, 0.0f };
	default: return { 0.0f, 0.0f, 0.0f };
	}
}
