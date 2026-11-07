#pragma once
#include "render/PostProcess.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

// Editor helpers for the game's look: one-click presets, the impact pulse and the Screen Effect studio.

/// Starting points of the Post Processing "Look Preset" menu
inline constexpr const char* kPostLookPresets[] = { "Clean Default", "Vivid Action", "Cinematic Warm", "Neon Glow", "Dark Moody" };

/// Sets the colour grading, bloom, vignette and letterbox of a look and turns post-processing on.
/// Lighting quality (shadows, SSAO, environment) and fog belong to the scene and stay. False for an unknown name.
inline bool ApplyPostLookPreset(PostProcessSettings& s, const char* name)
{
	const std::string_view look = name ? name : "";
	const auto set = [&](float exposure, float threshold, float bloom, float contrast, float saturation, const Vector3& tint, float vignette, float letterbox) {
		s.enabled = true;
		s.toneMapping = true;
		s.bloom = true;
		s.exposure = exposure;
		s.bloomThreshold = threshold;
		s.bloomIntensity = bloom;
		s.contrast = contrast;
		s.saturation = saturation;
		s.tint = tint;
		s.vignette = vignette;
		s.letterbox = letterbox;
	};
	if (look == "Clean Default") set(1.0f, 1.0f, 0.4f, 1.0f, 1.0f, { 1.0f, 1.0f, 1.0f }, 0.0f, 0.0f);
	else if (look == "Vivid Action") set(1.1f, 0.9f, 0.6f, 1.15f, 1.25f, { 1.0f, 1.0f, 1.0f }, 0.15f, 0.0f);
	else if (look == "Cinematic Warm") set(1.0f, 1.0f, 0.5f, 1.1f, 0.95f, { 1.08f, 1.0f, 0.88f }, 0.35f, 0.06f);
	else if (look == "Neon Glow") set(1.0f, 0.6f, 1.2f, 1.1f, 1.3f, { 0.95f, 1.0f, 1.1f }, 0.25f, 0.0f);
	else if (look == "Dark Moody") set(0.7f, 1.2f, 0.3f, 1.2f, 0.6f, { 0.9f, 0.95f, 1.05f }, 0.5f, 0.0f);
	else return false;
	return true;
}

/// A short hit flash: extra bloom and a radial blur that fade out over `seconds`, plus a camera shake
struct ImpactPulse {
	float bloomBoost = 1.0f;
	float radialBlur = 0.2f; // 0..1, strength of shaders/ScreenEffect.RadialBlur.PS.hlsl
	float shake = 0.15f;     // camera shake amplitude (world units)
	float seconds = 0.3f;
};

/// Screen effect slot of the impact pulse; scripts use the slots below it
inline constexpr int kImpactEffectSlot = PostProcess::kMaxScreenEffects - 1;

/// File names of the screen effect shaders ("ScreenEffect.*.PS.hlsl") in `folder`, sorted
inline std::vector<std::string> ListScreenEffectShaders(const std::filesystem::path& folder)
{
	std::vector<std::string> names;
	std::error_code ec;
	for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
		const std::string name = it->path().filename().string();
		if (name.starts_with("ScreenEffect.") && name.ends_with(".PS.hlsl")) names.push_back(name);
	}
	std::sort(names.begin(), names.end());
	return names;
}

/// The comment lines at the top of a shader (before its code), which describe its parameters
inline std::string ScreenEffectShaderHint(const std::filesystem::path& file)
{
	std::ifstream in(file);
	std::string line, hint;
	bool started = false;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
		if (line.starts_with("//")) {
			std::string_view text = std::string_view(line).substr(2);
			while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
			if (!hint.empty()) hint += '\n';
			hint += text;
			started = true;
		} else if (started && !line.empty()) {
			break; // the code starts
		} else if (!line.empty() && !line.starts_with("#")) {
			break; // code before any comment
		}
	}
	return hint;
}
