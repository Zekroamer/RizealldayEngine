#pragma once
#include <Windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <Xinput.h>
#include <wrl.h>
#include <cstdint>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "xinput.lib")

class Input
{
public:
	static Input* GetInstance();

	void Initialize(HINSTANCE hInstance, HWND hwnd);
	void Update();

	// Keyboard
	bool PushKey(BYTE keyNumber) const;
	bool TriggerKey(BYTE keyNumber) const;
	bool ReleaseKey(BYTE keyNumber) const;

	// Mouse
	int GetMouseMoveX() const { return mouseState_.lX; }
	int GetMouseMoveY() const { return mouseState_.lY; }
	int GetMouseX() const { return mouseState_.lX; }
	int GetMouseY() const { return mouseState_.lY; }
	int GetMouseWheel() const { return mouseState_.lZ; }
	bool IsPressMouse(int button) const { return button >= 0 && button < 4 && (mouseState_.rgbButtons[button] & 0x80) != 0; }
	bool PushMouseButton(int button) const { return IsPressMouse(button); }
	bool IsTriggerMouse(int button) const { return button >= 0 && button < 4 && (mouseState_.rgbButtons[button] & 0x80) && !(preMouseState_.rgbButtons[button] & 0x80); }
	// Self-tests have no real mouse movement; the next Update overwrites these values
	void InjectMouseMotion(int dx, int dy, int wheel) { mouseState_.lX = dx; mouseState_.lY = dy; mouseState_.lZ = wheel; }

	// Gamepad (XInput)
	bool IsGamepadConnected(int padIndex = 0) const;
	bool PushGamepadButton(WORD button, int padIndex = 0) const;
	bool TriggerGamepadButton(WORD button, int padIndex = 0) const;
	float GetLeftStickX(int padIndex = 0) const;
	float GetLeftStickY(int padIndex = 0) const;
	float GetRightStickX(int padIndex = 0) const;
	float GetRightStickY(int padIndex = 0) const;
	float GetLeftTrigger(int padIndex = 0) const;  // 0..1
	float GetRightTrigger(int padIndex = 0) const; // 0..1
	void SetVibration(int padIndex, float leftMotor, float rightMotor);

private:
	Input() = default;
	~Input() = default;
	Input(const Input&) = delete;
	Input& operator=(const Input&) = delete;

	Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;

	BYTE key_[256] = {};
	BYTE preKey_[256] = {};
	DIMOUSESTATE mouseState_ = {};
	DIMOUSESTATE preMouseState_ = {};

	// Gamepad
	static const int kMaxGamepads = 4;
	XINPUT_STATE gamepadState_[kMaxGamepads] = {};
	XINPUT_STATE preGamepadState_[kMaxGamepads] = {};
	bool gamepadConnected_[kMaxGamepads] = {};
	float deadZone_ = 0.2f;
};
