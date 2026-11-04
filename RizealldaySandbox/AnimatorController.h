#pragma once
#include "math/Vector2.h"
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Animator (roadmap 5.1): state machine asset + per-object runtime.
//
// AnimatorController = shared asset (Assets/Animators/<Name>.animator.json): parameters, states, transitions,
// animation events and node positions for the Animator window. AnimatorRuntime = per-object instance: parameter
// values, the active state and the transition in progress. Hundreds of enemies share one controller.
// Neither type knows about models or rendering: the editor glue (EditorSuite_Animator.cpp) maps states to clips.

enum class AnimParamType : int { Float = 0, Int = 1, Bool = 2, Trigger = 3 };

struct AnimParameter {
	std::string name;
	AnimParamType type = AnimParamType::Float;
	float defaultValue = 0.0f; // Bool / Trigger: 0 or 1
};

enum class AnimConditionOp : int {
	Greater = 0,   // Float, Int
	Less = 1,      // Float, Int
	Equals = 2,    // Int
	NotEquals = 3, // Int
	IsTrue = 4,    // Bool, Trigger
	IsFalse = 5,   // Bool
};

struct AnimCondition {
	std::string parameter;
	AnimConditionOp op = AnimConditionOp::Greater;
	float threshold = 0.0f;
};

/// Fired once per pass of the clip when its normalized time crosses `time` (0..1)
struct AnimEvent {
	std::string name;
	float time = 0.5f;
};

/// One clip of a state's blend tree
struct AnimBlendClip {
	std::string clipModel; // like AnimState
	std::string clipName;
	int clipIndex = 0;
	float threshold = 0.0f; // value of the blend parameter where this clip plays alone
	float thresholdY = 0.0f; // 2D trees only: position = (threshold, thresholdY)
};

struct AnimState {
	uint32_t id = 0;
	std::string name;
	std::string clipModel; // project-relative model that owns the clip ("" = the object's own model)
	std::string clipName;  // preferred over clipIndex, so re-exported files with reordered clips still match
	int clipIndex = 0;
	float speed = 1.0f;
	bool loop = true;
	std::vector<AnimEvent> events;
	Vector2 position;      // Animator window, canvas space
	std::string blendParameter;          // float parameter choosing the mix
	std::string blendParameterY;         // set = 2D blend tree (x = blendParameter, y = this)
	std::vector<AnimBlendClip> blendClips; // not empty = blend tree; the clip fields above are not used

	bool IsBlend2D() const { return !blendParameterY.empty(); }
	/// One weight per blend clip, summing to 1. 1D: the two clips around `value` mix, the others are 0.
	/// 2D: gradient-band interpolation over the clip positions (a clip's own position = that clip alone).
	void BlendWeights(float value, std::vector<float>& outWeights) const { BlendWeights(value, 0.0f, outWeights); }
	void BlendWeights(float x, float y, std::vector<float>& outWeights) const;
};

struct AnimTransition {
	static constexpr uint32_t kAnyState = 0xFFFFFFFFu;
	uint32_t from = 0;     // state id, or kAnyState
	uint32_t to = 0;
	std::vector<AnimCondition> conditions; // all must hold (AND)
	float duration = 0.2f;                 // crossfade seconds
	bool hasExitTime = false;
	float exitTime = 1.0f;                 // normalized; < 1 = that point of every loop
	bool canTransitionToSelf = false;      // Any State only: false keeps "Any -> Die" from restarting Die every frame
};

struct AnimatorController {
	std::vector<AnimParameter> parameters;
	std::vector<AnimState> states;
	std::vector<AnimTransition> transitions; // evaluation order = priority (Any State transitions are checked first)
	uint32_t entryState = 0;                 // 0 = the first state
	uint32_t nextId = 1;
	Vector2 entryPosition{ -220.0f, 0.0f };
	Vector2 anyStatePosition{ -220.0f, 120.0f };

	const AnimState* FindState(uint32_t id) const;
	AnimState* FindState(uint32_t id);
	const AnimState* FindStateByName(const std::string& name) const;
	const AnimParameter* FindParameter(const std::string& name) const;
	uint32_t EntryStateId() const; // entryState when valid, else the first state, 0 when there are none

	AnimState& AddState(const std::string& name, Vector2 position);
	void RemoveState(uint32_t id); // also removes its transitions
	/// A transition is usable when it can ever fire (at least one condition or an exit time)
	static bool IsUsable(const AnimTransition& t) { return !t.conditions.empty() || t.hasExitTime; }

	std::string ToJson() const;
	static std::optional<AnimatorController> FromJson(const std::string& text, std::string* outError = nullptr);
};

struct AnimatorRuntime {
	/// What the glue must do this frame after Step()
	struct Change {
		bool changed = false;
		uint32_t state = 0;
		float fadeSeconds = 0.0f;
	};

	uint32_t currentState = 0;
	bool started = false;
	std::unordered_map<std::string, float> values;
	float transitionTime = 0.0f;     // > 0 while a crossfade runs (no interruption in v1)
	float transitionDuration = 0.0f;
	int activeTransition = -1;       // index in controller.transitions while fading (Animator window)

	/// Parameters at their defaults, entry state. Returns the entry state to play.
	Change Start(const AnimatorController& controller);
	/// Advances the transition timer and checks transitions. `normalizedTime` = current state's clip progress.
	Change Step(const AnimatorController& controller, float deltaTime, float normalizedTime);
	/// Moves straight to a state (script CrossFade); false when the name is unknown
	Change ForceState(const AnimatorController& controller, const std::string& stateName, float fadeSeconds);

	bool SetValue(const AnimatorController& controller, const std::string& name, float value); // false = no such parameter
	float GetValue(const std::string& name) const;
	void ResetTrigger(const std::string& name) { values[name] = 0.0f; }
	bool InTransition() const { return transitionDuration > 0.0f && transitionTime < transitionDuration; }

	/// Events of `state` crossed by the normalized window [from, to) (loops fire once per pass)
	static void CollectEvents(const AnimState& state, float from, float to, bool loop, std::vector<std::string>& out);

private:
	bool ConditionsHold(const AnimatorController& controller, const AnimTransition& t, float normalizedTime) const;
	Change Take(const AnimatorController& controller, int transitionIndex);
};

/// Data of the "Animator" component (GameObject::components). Only `controller` and `speed` are saved;
/// the runtime part restarts at the entry state whenever Play starts.
struct AnimatorComponent {
	char controller[260] = ""; // Assets/Animators/<Name>.animator.json
	float speed = 1.0f;        // multiplies every state's speed
	bool rootMotion = false;   // the clip's root bone moves the object

	AnimatorRuntime runtime;
	std::string runtimeController; // controller path the runtime was started with
};
