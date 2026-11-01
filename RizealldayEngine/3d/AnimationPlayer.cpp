#include "3d/AnimationPlayer.h"
#include <algorithm>
#include <cmath>

void AnimationPlayer::Play(const Model* source, int clip, bool loop, float speed, float fadeSeconds, float startTime)
{
	// A crossfade starting while another one runs fades from the current track (the older one is dropped)
	if (fadeSeconds > 0.0f && current_.clip >= 0) {
		previous_ = current_;
		fadeTime_ = 0.0f;
		fadeDuration_ = fadeSeconds;
	} else {
		previous_ = Track{};
		fadeTime_ = 0.0f;
		fadeDuration_ = 0.0f;
	}
	current_ = Track{};
	current_.source = source;
	current_.clip = clip;
	current_.loop = loop;
	current_.speed = speed;
	current_.time = startTime;
	current_.elapsed = startTime;
	lastStart_ = startTime;
}

float AnimationPlayer::Duration(const Track& track, const Model* target)
{
	if (!track.blend.empty()) {
		float sum = 0.0f;
		float weights = 0.0f;
		for (const Track::BlendClip& c : track.blend) {
			const Model* model = c.source ? c.source : target;
			if (model && c.weight > 0.0f) {
				sum += model->GetAnimationDuration(c.clip) * c.weight;
				weights += c.weight;
			}
		}
		return weights > 0.0f ? sum / weights : 0.0f;
	}
	const Model* model = track.source ? track.source : target;
	return (model && track.clip >= 0) ? model->GetAnimationDuration(track.clip) : 0.0f;
}

void AnimationPlayer::AdvanceTrack(Track& track, float deltaTime, const Model* target)
{
	const float step = deltaTime * track.speed;
	track.elapsed += step;
	track.time += step;
	const float duration = Duration(track, target);
	if (duration <= 0.0f) return;
	if (track.loop) {
		track.time = std::fmod(track.time, duration);
		if (track.time < 0.0f) track.time += duration;
	} else {
		track.time = std::clamp(track.time, 0.0f, duration);
		track.elapsed = (std::min)(track.elapsed, duration); // a finished one-shot stays at 1.0 normalized
	}
}

void AnimationPlayer::Advance(float deltaTime, const Model* target)
{
	if (target) lastTarget_ = target;
	lastStart_ = current_.elapsed;
	previousTime_ = current_.time;
	AdvanceTrack(current_, deltaTime, lastTarget_);
	if (IsFading()) {
		AdvanceTrack(previous_, deltaTime, lastTarget_);
		fadeTime_ += deltaTime;
		if (fadeTime_ >= fadeDuration_) {
			previous_ = Track{};
			fadeDuration_ = 0.0f;
			fadeTime_ = 0.0f;
		}
	}
	for (Layer& layer : layers_) {
		if (layer.track.clip < 0) continue;
		AdvanceTrack(layer.track, deltaTime, lastTarget_);
		if (!layer.track.loop && layer.targetWeight > 0.0f) {
			const float duration = Duration(layer.track, lastTarget_);
			if (duration > 0.0f && layer.track.time >= duration) StopLayer(static_cast<int>(&layer - layers_), 0.15f);
		}
		if (layer.fadeSpeed > 0.0f) {
			const float step = layer.fadeSpeed * deltaTime;
			layer.weight = layer.weight < layer.targetWeight ? (std::min)(layer.weight + step, layer.targetWeight) : (std::max)(layer.weight - step, layer.targetWeight);
		} else {
			layer.weight = layer.targetWeight;
		}
		if (layer.weight <= 0.0f && layer.targetWeight <= 0.0f) layer.track = Track{};
	}
}

void AnimationPlayer::PlayLayer(int layer, const Model& target, const Model* source, int clip, const std::string& maskBone, bool loop, float speed, float fadeSeconds)
{
	if (layer < 0 || layer >= kMaxLayers) return;
	Layer& l = layers_[layer];
	l.track = Track{};
	l.track.source = source;
	l.track.clip = clip;
	l.track.loop = loop;
	l.track.speed = speed;
	l.mask = target.MakeNodeMask(maskBone);
	l.targetWeight = 1.0f;
	l.fadeSpeed = fadeSeconds > 0.0f ? 1.0f / fadeSeconds : 0.0f;
	if (fadeSeconds <= 0.0f) l.weight = 1.0f;
}

void AnimationPlayer::StopLayer(int layer, float fadeSeconds)
{
	if (layer < 0 || layer >= kMaxLayers) return;
	Layer& l = layers_[layer];
	l.targetWeight = 0.0f;
	l.fadeSpeed = fadeSeconds > 0.0f ? 1.0f / fadeSeconds : 0.0f;
	if (fadeSeconds <= 0.0f) {
		l.weight = 0.0f;
		l.track = Track{};
	}
}

bool AnimationPlayer::IsLayerPlaying(int layer) const
{
	return layer >= 0 && layer < kMaxLayers && layers_[layer].track.clip >= 0;
}

void AnimationPlayer::SetIK(const std::string& endBone, const Vector3& target, const Vector3& pole, float weight)
{
	auto it = std::find_if(ik_.begin(), ik_.end(), [&](const IkRequest& r) { return r.endBone == endBone; });
	if (weight <= 0.0f) {
		if (it != ik_.end()) ik_.erase(it);
		return;
	}
	if (it == ik_.end()) {
		ik_.push_back({ endBone, target, pole, weight });
		return;
	}
	it->target = target;
	it->pole = pole;
	it->weight = weight;
}

Vector3 AnimationPlayer::ComputeRootDelta(const Model& target)
{
	const int root = target.RootBoneNode();
	if (!rootMotion_ || root < 0 || current_.clip < 0) return { 0.0f, 0.0f, 0.0f };
	const auto positionAt = [&](float time) {
		Track at = current_;
		at.time = time;
		LocalPose pose;
		SampleTrack(target, at, pose);
		Matrix4x4 m;
		return target.GetNodeModelMatrix(pose, root, m) ? Vector3{ m.m[3][0], m.m[3][1], m.m[3][2] } : Vector3{ 0.0f, 0.0f, 0.0f };
	};
	const float now = current_.time;
	const float before = previousTime_;
	previousTime_ = now;
	if (now == before) return { 0.0f, 0.0f, 0.0f };
	if (now > before) return positionAt(now) - positionAt(before);
	// Wrapped around the end of the loop
	const float duration = Duration(current_, &target);
	// Just short of the end: sampling at the duration itself wraps back to the first frame
	return (positionAt(duration * 0.9999f) - positionAt(before)) + (positionAt(now) - positionAt(0.0f));
}

bool AnimationPlayer::GetBoneMatrix(const Model& target, const std::string& bone, Matrix4x4& out) const
{
	LocalPose pose;
	EvaluatePose(target, pose);
	return target.GetNodeModelMatrix(pose, target.FindNode(bone), out, overrides_.empty() ? nullptr : &overrides_);
}

float AnimationPlayer::NormalizedTime(const Model* target) const
{
	const float duration = Duration(current_, target ? target : lastTarget_);
	return (duration > 0.0f) ? current_.elapsed / duration : 0.0f;
}

void AnimationPlayer::SampleTrack(const Model& target, const Track& track, LocalPose& outPose) const
{
	if (track.blend.empty()) {
		const Model* model = track.source ? track.source : &target;
		target.SampleLocalPose(model->GetAnimationClip(track.clip), track.time, track.loop, outPose);
		return;
	}
	// One phase for all clips, so feet stay in step between a walk and a run of different lengths
	const float duration = Duration(track, &target);
	const float phase = duration > 0.0f ? track.time / duration : 0.0f;
	float total = 0.0f;
	LocalPose clipPose, mixed;
	outPose.clear();
	for (const Track::BlendClip& c : track.blend) {
		if (c.weight <= 0.0f) continue;
		const Model* model = c.source ? c.source : &target;
		target.SampleLocalPose(model->GetAnimationClip(c.clip), phase * model->GetAnimationDuration(c.clip), track.loop, clipPose);
		total += c.weight;
		if (outPose.empty()) {
			outPose = clipPose;
		} else {
			target.BlendPoses(outPose, clipPose, c.weight / total, mixed);
			outPose.swap(mixed);
		}
	}
	if (outPose.empty()) target.SampleLocalPose(nullptr, 0.0f, true, outPose);
}

void AnimationPlayer::EvaluatePose(const Model& target, LocalPose& outPose) const
{
	lastTarget_ = &target;
	if (!IsFading()) {
		SampleTrack(target, current_, outPose);
	} else {
		SampleTrack(target, previous_, poseA_);
		SampleTrack(target, current_, poseB_);
		target.BlendPoses(poseA_, poseB_, FadeProgress(), outPose);
	}

	if (rootMotion_) {
		// Keep the root over the object: the object itself moves by ComputeRootDelta
		const int root = target.RootBoneNode();
		if (root >= 0 && root < static_cast<int>(outPose.size()) && !outPose[root].bind) {
			Track start = current_;
			start.time = 0.0f;
			SampleTrack(target, start, poseA_);
			if (!poseA_[root].bind) {
				outPose[root].translation.x = poseA_[root].translation.x;
				outPose[root].translation.z = poseA_[root].translation.z;
			}
		}
	}

	for (const Layer& layer : layers_) {
		if (layer.track.clip < 0 || layer.weight <= 0.0f) continue;
		SampleTrack(target, layer.track, poseB_);
		target.BlendPoses(outPose, poseB_, layer.weight, blended_, &layer.mask);
		outPose = blended_;
	}

	for (const IkRequest& request : ik_) {
		target.SolveTwoBoneIK(outPose, target.FindNode(request.endBone), request.target, request.pole, request.weight);
	}
}

void AnimationPlayer::Evaluate(const Model& target, std::vector<Matrix4x4>& outSkinMatrices) const
{
	LocalPose pose;
	EvaluatePose(target, pose);
	target.BuildSkinMatrices(pose, outSkinMatrices, overrides_.empty() ? nullptr : &overrides_);
}
