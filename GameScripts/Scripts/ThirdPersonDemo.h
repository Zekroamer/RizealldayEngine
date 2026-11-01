#pragma once
#include "base/ScriptAPI.h"

// GameObject > Samples > Third Person Demo: mouse orbit camera, camera-relative movement, picking through
// the screen center, and a label that follows a world position. Left click shoots, the wheel zooms,
// right click cuts the screen open (a screen effect).

class ThirdPersonPlayer : public ScriptBase {
public:
	EXPOSE_FIELD(float, moveSpeed, 6.0f);

	void Initialize() override;
	void Update(float deltaTime) override;

private:
	float fallSpeed_ = 0.0f;
	bool dummyFallen_ = false;
	float slashTime_ = 0.0f;
};

class ThirdPersonCamera : public ScriptBase {
public:
	EXPOSE_FIELD(float, sensitivity, 0.004f);
	EXPOSE_FIELD(float, distance, 7.0f);

	void Initialize() override;
	void Update(float deltaTime) override;

private:
	float pitch_ = 0.35f;
};
