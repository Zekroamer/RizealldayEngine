#include "AnimatorController.h"
#include "SceneJson.h"
#include <algorithm>
#include <cmath>

using SceneJson::Json;
using SceneJson::Num;
using SceneJson::Arr;

// ---- AnimatorController ----

void AnimState::BlendWeights(float value, std::vector<float>& outWeights) const
{
	outWeights.assign(blendClips.size(), 0.0f);
	if (blendClips.empty()) return;
	int below = -1;
	int above = -1;
	for (int i = 0; i < static_cast<int>(blendClips.size()); ++i) {
		const float t = blendClips[i].threshold;
		if (t <= value && (below < 0 || t > blendClips[below].threshold)) below = i;
		if (t >= value && (above < 0 || t < blendClips[above].threshold)) above = i;
	}
	if (below < 0) { outWeights[above] = 1.0f; return; }
	if (above < 0 || below == above) { outWeights[below] = 1.0f; return; }
	const float range = blendClips[above].threshold - blendClips[below].threshold;
	const float w = range > 0.0f ? (value - blendClips[below].threshold) / range : 1.0f;
	outWeights[below] = 1.0f - w;
	outWeights[above] = w;
}

const AnimState* AnimatorController::FindState(uint32_t id) const
{
	for (const AnimState& s : states) if (s.id == id) return &s;
	return nullptr;
}

AnimState* AnimatorController::FindState(uint32_t id)
{
	for (AnimState& s : states) if (s.id == id) return &s;
	return nullptr;
}

const AnimState* AnimatorController::FindStateByName(const std::string& name) const
{
	for (const AnimState& s : states) if (s.name == name) return &s;
	return nullptr;
}

const AnimParameter* AnimatorController::FindParameter(const std::string& name) const
{
	for (const AnimParameter& p : parameters) if (p.name == name) return &p;
	return nullptr;
}

uint32_t AnimatorController::EntryStateId() const
{
	if (FindState(entryState)) return entryState;
	return states.empty() ? 0 : states.front().id;
}

AnimState& AnimatorController::AddState(const std::string& name, Vector2 position)
{
	AnimState s;
	s.id = nextId++;
	s.name = name;
	s.position = position;
	states.push_back(std::move(s));
	if (states.size() == 1) entryState = states.back().id;
	return states.back();
}

void AnimatorController::RemoveState(uint32_t id)
{
	states.erase(std::remove_if(states.begin(), states.end(), [&](const AnimState& s) { return s.id == id; }), states.end());
	transitions.erase(std::remove_if(transitions.begin(), transitions.end(),
		[&](const AnimTransition& t) { return t.from == id || t.to == id; }), transitions.end());
	if (entryState == id) entryState = states.empty() ? 0 : states.front().id;
}

std::string AnimatorController::ToJson() const
{
	Json root = Json::object();
	root["version"] = 1;
	root["entryState"] = entryState;
	root["nextId"] = nextId;
	root["entryPosition"] = Arr(entryPosition);
	root["anyStatePosition"] = Arr(anyStatePosition);

	Json params = Json::array();
	for (const AnimParameter& p : parameters) {
		params.push_back({ { "name", p.name }, { "type", static_cast<int>(p.type) }, { "default", Num(p.defaultValue) } });
	}
	root["parameters"] = std::move(params);

	Json states_ = Json::array();
	for (const AnimState& s : states) {
		Json events = Json::array();
		for (const AnimEvent& e : s.events) events.push_back({ { "name", e.name }, { "time", Num(e.time) } });
		Json js = Json::object();
		js["id"] = s.id;
		js["name"] = s.name;
		js["clipModel"] = s.clipModel;
		js["clipName"] = s.clipName;
		js["clipIndex"] = s.clipIndex;
		js["speed"] = Num(s.speed);
		js["loop"] = s.loop;
		js["position"] = Arr(s.position);
		js["events"] = std::move(events);
		if (!s.blendClips.empty()) {
			Json clips = Json::array();
			for (const AnimBlendClip& c : s.blendClips) {
				clips.push_back({ { "clipModel", c.clipModel }, { "clipName", c.clipName }, { "clipIndex", c.clipIndex }, { "threshold", Num(c.threshold) } });
			}
			js["blendParameter"] = s.blendParameter;
			js["blendClips"] = std::move(clips);
		}
		states_.push_back(std::move(js));
	}
	root["states"] = std::move(states_);

	Json transitions_ = Json::array();
	for (const AnimTransition& t : transitions) {
		Json conditions = Json::array();
		for (const AnimCondition& c : t.conditions) {
			conditions.push_back({ { "parameter", c.parameter }, { "op", static_cast<int>(c.op) }, { "threshold", Num(c.threshold) } });
		}
		Json jt = Json::object();
		jt["from"] = (t.from == AnimTransition::kAnyState) ? Json("any") : Json(t.from);
		jt["to"] = t.to;
		jt["duration"] = Num(t.duration);
		jt["hasExitTime"] = t.hasExitTime;
		jt["exitTime"] = Num(t.exitTime);
		jt["canTransitionToSelf"] = t.canTransitionToSelf;
		jt["conditions"] = std::move(conditions);
		transitions_.push_back(std::move(jt));
	}
	root["transitions"] = std::move(transitions_);
	return root.dump(1, '\t');
}

std::optional<AnimatorController> AnimatorController::FromJson(const std::string& text, std::string* outError)
{
	std::optional<Json> parsed = SceneJson::Parse(text, outError);
	if (!parsed || !parsed->is_object()) {
		if (outError && outError->empty()) *outError = "not a JSON object";
		return std::nullopt;
	}
	const Json& root = *parsed;
	AnimatorController c;
	SceneJson::Read(root, "entryState", c.entryState);
	SceneJson::Read(root, "nextId", c.nextId);
	SceneJson::Read(root, "entryPosition", c.entryPosition);
	SceneJson::Read(root, "anyStatePosition", c.anyStatePosition);

	if (const Json* params = SceneJson::Find(root, "parameters"); params && params->is_array()) {
		for (const Json& jp : *params) {
			AnimParameter p;
			SceneJson::Read(jp, "name", p.name);
			int type = 0;
			SceneJson::Read(jp, "type", type);
			p.type = static_cast<AnimParamType>(std::clamp(type, 0, 3));
			SceneJson::Read(jp, "default", p.defaultValue);
			if (!p.name.empty()) c.parameters.push_back(std::move(p));
		}
	}
	uint32_t maxId = 0;
	if (const Json* states = SceneJson::Find(root, "states"); states && states->is_array()) {
		for (const Json& js : *states) {
			AnimState s;
			SceneJson::Read(js, "id", s.id);
			SceneJson::Read(js, "name", s.name);
			SceneJson::Read(js, "clipModel", s.clipModel);
			SceneJson::Read(js, "clipName", s.clipName);
			SceneJson::Read(js, "clipIndex", s.clipIndex);
			SceneJson::Read(js, "speed", s.speed);
			SceneJson::Read(js, "loop", s.loop);
			SceneJson::Read(js, "position", s.position);
			SceneJson::Read(js, "blendParameter", s.blendParameter);
			if (const Json* clips = SceneJson::Find(js, "blendClips"); clips && clips->is_array()) {
				for (const Json& jc : *clips) {
					AnimBlendClip c;
					SceneJson::Read(jc, "clipModel", c.clipModel);
					SceneJson::Read(jc, "clipName", c.clipName);
					SceneJson::Read(jc, "clipIndex", c.clipIndex);
					SceneJson::Read(jc, "threshold", c.threshold);
					s.blendClips.push_back(std::move(c));
				}
			}
			if (const Json* events = SceneJson::Find(js, "events"); events && events->is_array()) {
				for (const Json& je : *events) {
					AnimEvent e;
					SceneJson::Read(je, "name", e.name);
					SceneJson::Read(je, "time", e.time);
					if (!e.name.empty()) s.events.push_back(std::move(e));
				}
			}
			if (s.id == 0 || c.FindState(s.id)) continue; // ids must be unique and non-zero
			maxId = (std::max)(maxId, s.id);
			c.states.push_back(std::move(s));
		}
	}
	c.nextId = (std::max)(c.nextId, maxId + 1);
	if (const Json* transitions = SceneJson::Find(root, "transitions"); transitions && transitions->is_array()) {
		for (const Json& jt : *transitions) {
			AnimTransition t;
			if (const Json* from = SceneJson::Find(jt, "from"); from && from->is_string() && from->get<std::string>() == "any") {
				t.from = AnimTransition::kAnyState;
			} else {
				SceneJson::Read(jt, "from", t.from);
			}
			SceneJson::Read(jt, "to", t.to);
			SceneJson::Read(jt, "duration", t.duration);
			SceneJson::Read(jt, "hasExitTime", t.hasExitTime);
			SceneJson::Read(jt, "exitTime", t.exitTime);
			SceneJson::Read(jt, "canTransitionToSelf", t.canTransitionToSelf);
			if (const Json* conditions = SceneJson::Find(jt, "conditions"); conditions && conditions->is_array()) {
				for (const Json& jc : *conditions) {
					AnimCondition cond;
					SceneJson::Read(jc, "parameter", cond.parameter);
					int op = 0;
					SceneJson::Read(jc, "op", op);
					cond.op = static_cast<AnimConditionOp>(std::clamp(op, 0, 5));
					SceneJson::Read(jc, "threshold", cond.threshold);
					t.conditions.push_back(std::move(cond));
				}
			}
			// Drop transitions that point at missing states instead of failing the whole file
			if (!c.FindState(t.to) || (t.from != AnimTransition::kAnyState && !c.FindState(t.from))) continue;
			t.duration = (std::max)(0.0f, t.duration);
			c.transitions.push_back(std::move(t));
		}
	}
	return c;
}

// ---- AnimatorRuntime ----

AnimatorRuntime::Change AnimatorRuntime::Start(const AnimatorController& controller)
{
	values.clear();
	for (const AnimParameter& p : controller.parameters) values[p.name] = (p.type == AnimParamType::Trigger) ? 0.0f : p.defaultValue;
	currentState = controller.EntryStateId();
	started = true;
	transitionTime = transitionDuration = 0.0f;
	activeTransition = -1;
	return { currentState != 0, currentState, 0.0f };
}

bool AnimatorRuntime::SetValue(const AnimatorController& controller, const std::string& name, float value)
{
	const AnimParameter* p = controller.FindParameter(name);
	if (!p) return false;
	switch (p->type) {
	case AnimParamType::Int: value = std::round(value); break;
	case AnimParamType::Bool:
	case AnimParamType::Trigger: value = (value != 0.0f) ? 1.0f : 0.0f; break;
	default: break;
	}
	values[name] = value;
	return true;
}

float AnimatorRuntime::GetValue(const std::string& name) const
{
	auto it = values.find(name);
	return (it != values.end()) ? it->second : 0.0f;
}

bool AnimatorRuntime::ConditionsHold(const AnimatorController& controller, const AnimTransition& t, float normalizedTime) const
{
	if (!AnimatorController::IsUsable(t)) return false;
	if (t.hasExitTime) {
		const float reached = (t.exitTime < 1.0f) ? normalizedTime - std::floor(normalizedTime) : normalizedTime;
		if (reached < t.exitTime) return false;
	}
	for (const AnimCondition& c : t.conditions) {
		if (!controller.FindParameter(c.parameter)) return false;
		const float v = GetValue(c.parameter);
		bool ok = false;
		switch (c.op) {
		case AnimConditionOp::Greater: ok = v > c.threshold; break;
		case AnimConditionOp::Less: ok = v < c.threshold; break;
		case AnimConditionOp::Equals: ok = std::lround(v) == std::lround(c.threshold); break;
		case AnimConditionOp::NotEquals: ok = std::lround(v) != std::lround(c.threshold); break;
		case AnimConditionOp::IsTrue: ok = v != 0.0f; break;
		case AnimConditionOp::IsFalse: ok = v == 0.0f; break;
		}
		if (!ok) return false;
	}
	return true;
}

AnimatorRuntime::Change AnimatorRuntime::Take(const AnimatorController& controller, int transitionIndex)
{
	const AnimTransition& t = controller.transitions[transitionIndex];
	// Triggers are consumed the moment a transition uses them, not at the end of the frame
	for (const AnimCondition& c : t.conditions) {
		const AnimParameter* p = controller.FindParameter(c.parameter);
		if (p && p->type == AnimParamType::Trigger) values[c.parameter] = 0.0f;
	}
	currentState = t.to;
	transitionTime = 0.0f;
	transitionDuration = t.duration;
	activeTransition = (t.duration > 0.0f) ? transitionIndex : -1;
	return { true, t.to, t.duration };
}

AnimatorRuntime::Change AnimatorRuntime::Step(const AnimatorController& controller, float deltaTime, float normalizedTime)
{
	if (!started) return Start(controller);
	if (InTransition()) {
		transitionTime += deltaTime;
		if (!InTransition()) {
			transitionTime = transitionDuration = 0.0f;
			activeTransition = -1;
		}
		return {}; // v1: a running crossfade is not interrupted
	}
	// Any State first, then the current state's own transitions; list order = priority
	for (int pass = 0; pass < 2; ++pass) {
		for (int i = 0; i < static_cast<int>(controller.transitions.size()); ++i) {
			const AnimTransition& t = controller.transitions[i];
			const bool fromAny = (t.from == AnimTransition::kAnyState);
			if ((pass == 0) != fromAny) continue;
			if (!fromAny && t.from != currentState) continue;
			if (fromAny && t.to == currentState && !t.canTransitionToSelf) continue;
			if (ConditionsHold(controller, t, normalizedTime)) return Take(controller, i);
		}
	}
	return {};
}

AnimatorRuntime::Change AnimatorRuntime::ForceState(const AnimatorController& controller, const std::string& stateName, float fadeSeconds)
{
	const AnimState* s = controller.FindStateByName(stateName);
	if (!s) return {};
	if (!started) Start(controller);
	currentState = s->id;
	transitionTime = 0.0f;
	transitionDuration = (std::max)(0.0f, fadeSeconds);
	activeTransition = -1;
	return { true, s->id, transitionDuration };
}

void AnimatorRuntime::CollectEvents(const AnimState& state, float from, float to, bool loop, std::vector<std::string>& out)
{
	if (state.events.empty() || to <= from) return;
	if (!loop) {
		from = (std::min)(from, 1.0f);
		to = (std::min)(to, 1.0f);
		for (const AnimEvent& e : state.events) {
			// A one-shot fires an event at time 1.0 when it reaches the end
			if (e.time >= from && (e.time < to || (to >= 1.0f && e.time >= 1.0f && from < 1.0f))) out.push_back(e.name);
		}
		return;
	}
	const int firstPass = static_cast<int>(std::floor(from));
	const int lastPass = static_cast<int>(std::floor(to));
	for (int pass = firstPass; pass <= lastPass; ++pass) {
		for (const AnimEvent& e : state.events) {
			const float at = static_cast<float>(pass) + std::clamp(e.time, 0.0f, 0.9999f);
			if (at >= from && at < to) out.push_back(e.name);
		}
	}
}
