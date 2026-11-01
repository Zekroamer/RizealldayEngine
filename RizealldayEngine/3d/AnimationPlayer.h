#pragma once
#include "3d/Model.h"
#include <vector>

/// Plays skeletal animation clips with crossfade (roadmap 5.1).
/// A clip may come from any model with the same bone names (`source`); nullptr = the model being animated.
/// Advance() moves time (call it from the game update with scaled game time), Evaluate() builds the skin matrices.
class AnimationPlayer {
public:
	struct Track {
		const Model* source = nullptr; // owner of the clip (nullptr = the target model)
		int clip = -1;                 // clip index in `source`, -1 = bind pose
		float time = 0.0f;             // seconds inside the clip (wrapped when looping)
		float elapsed = 0.0f;          // seconds since the track started (not wrapped, scaled by speed)
		float speed = 1.0f;
		bool loop = true;
		// Blend tree: these clips play at one shared phase, mixed by weight. `clip` then only marks the track as playing.
		struct BlendClip {
			const Model* source = nullptr;
			int clip = 0;
			float weight = 0.0f;
		};
		std::vector<BlendClip> blend;
	};

	/// Starts `clip`. fadeSeconds > 0 crossfades from the current pose; 0 switches instantly.
	void Play(const Model* source, int clip, bool loop = true, float speed = 1.0f, float fadeSeconds = 0.0f, float startTime = 0.0f);
	/// An overlay on top of the state machine (e.g. slash with the upper body while running). `maskBone` limits it to
	/// that bone and everything below it; empty = the whole body. Layers apply in index order. One-shots stop when finished.
	void PlayLayer(int layer, const Model& target, const Model* source, int clip, const std::string& maskBone, bool loop, float speed, float fadeSeconds);
	void StopLayer(int layer, float fadeSeconds);
	bool IsLayerPlaying(int layer) const;
	static constexpr int kMaxLayers = 4;

	/// Holds until changed; weight 0 removes it. Target and pole are in model space.
	void SetIK(const std::string& endBone, const Vector3& target, const Vector3& pole, float weight);

	/// The root bone stays in place horizontally; ConsumeRootDelta returns the movement the clip would have made
	void SetRootMotion(bool enabled) { rootMotion_ = enabled; }
	Vector3 ComputeRootDelta(const Model& target);

	/// Node index -> model-space matrix, replacing the animated pose (ragdoll). Empty = none.
	std::vector<std::pair<int, Matrix4x4>>& Overrides() { return overrides_; }
	bool GetBoneMatrix(const Model& target, const std::string& bone, Matrix4x4& out) const;

	/// `target` = the animated model (resolves tracks whose source is nullptr)
	void Advance(float deltaTime, const Model* target);
	void Evaluate(const Model& target, std::vector<Matrix4x4>& outSkinMatrices) const;
	/// The blended local pose (skeleton overlay, tests)
	void EvaluatePose(const Model& target, LocalPose& outPose) const;

	Track& Current() { return current_; }
	const Track& Current() const { return current_; }
	const Track& Previous() const { return previous_; }
	bool IsFading() const { return fadeDuration_ > 0.0f && fadeTime_ < fadeDuration_; }
	/// 0 at the start of a crossfade, 1 when it is done (1 when not fading)
	float FadeProgress() const { return IsFading() ? fadeTime_ / fadeDuration_ : 1.0f; }
	float FadeDuration() const { return fadeDuration_; }

	/// Clip length in seconds of a track (0 for the bind pose); `target` resolves a null source
	static float Duration(const Track& track, const Model* target);
	/// elapsed / duration: 0..1 for the first pass, 2.5 = middle of the third loop (exit times, events)
	float NormalizedTime(const Model* target) const;
	/// Clip time window covered by the last Advance() of the current track (animation events)
	float LastAdvanceStart() const { return lastStart_; }
	float LastAdvanceEnd() const { return current_.elapsed; }

private:
	struct Layer {
		Track track;
		std::vector<uint8_t> mask;
		float weight = 0.0f;
		float targetWeight = 0.0f;
		float fadeSpeed = 0.0f; // weight per second, 0 = at the target
	};
	struct IkRequest {
		std::string endBone;
		Vector3 target{};
		Vector3 pole{};
		float weight = 0.0f;
	};

	static void AdvanceTrack(Track& track, float deltaTime, const Model* target);
	void SampleTrack(const Model& target, const Track& track, LocalPose& outPose) const;

	Track current_;
	Track previous_;
	float fadeTime_ = 0.0f;
	float fadeDuration_ = 0.0f;
	float lastStart_ = 0.0f;
	Layer layers_[kMaxLayers];
	std::vector<IkRequest> ik_;
	std::vector<std::pair<int, Matrix4x4>> overrides_;
	bool rootMotion_ = false;
	float previousTime_ = 0.0f;
	mutable const Model* lastTarget_ = nullptr; // last target seen (NormalizedTime without a target)
	mutable LocalPose poseA_;
	mutable LocalPose poseB_;
	mutable LocalPose blended_;
};
