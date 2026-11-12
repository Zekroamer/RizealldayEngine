// Input actions per project (ProjectSettings/input.json), the device source for scripts and the Options > Input Actions window.
#include "EditorSuiteInternal.h"
#include "input/Input.h"

namespace {
	std::filesystem::path InputActionsFile(const std::filesystem::path& projectRoot)
	{
		return projectRoot / "ProjectSettings" / "input.json";
	}
}

void EditorSuite::ReloadInputActions()
{
	inputActionsRoot_ = currentProjectRoot_;
	inputActions_ = InputActions::Defaults();
	if (currentProjectRoot_.empty()) return;
	std::ifstream in(InputActionsFile(currentProjectRoot_), std::ios::binary);
	if (!in) return; // no file: the defaults
	std::stringstream buffer;
	buffer << in.rdbuf();
	std::string error;
	if (auto loaded = InputActions::FromJson(buffer.str(), &error)) {
		inputActions_ = std::move(*loaded);
	} else {
		LogMessage(std::format("[Input] ProjectSettings/input.json is invalid ({}); using the default actions.", error), 1);
	}
}

const InputActions& EditorSuite::GetInputActions()
{
	if (inputActionsRoot_ != currentProjectRoot_) ReloadInputActions();
	return inputActions_;
}

bool EditorSuite::SaveInputActions(const InputActions& actions)
{
	if (currentProjectRoot_.empty()) {
		LogMessage("[Input] Open a project first.", 1);
		return false;
	}
	const std::filesystem::path file = InputActionsFile(currentProjectRoot_);
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	if (!WriteProjectFile(file, actions.ToJson() + '\n')) {
		LogMessage(std::format("[Input] Cannot write {}", file.string()), 2);
		return false;
	}
	inputActions_ = actions;
	inputActionsRoot_ = currentProjectRoot_;
	return true;
}

InputActions::Source EditorSuite::InputSource() const
{
	InputActions::Source src;
	const Input* in = currentInput_;
	if (!in) return src;
	src.keyDown = [in](int code) { return code > 0 && code < 256 && in->PushKey(static_cast<BYTE>(code)); };
	// Like IsMouseDown for scripts: only clicks inside the Game view count
	src.mouseDown = [this, in](int button) {
		float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
		return GetGameViewMousePosition(x, y, w, h) && in->IsPressMouse(button);
	};
	src.padDown = [in](int bits) { return in->PushGamepadButton(static_cast<WORD>(bits & 0xFFFF), 0); };
	src.padAxis = [in](int axis) {
		switch (static_cast<InputBinding::Axis>(axis)) {
		case InputBinding::Axis::LeftX: return in->GetLeftStickX(0);
		case InputBinding::Axis::LeftY: return in->GetLeftStickY(0);
		case InputBinding::Axis::RightX: return in->GetRightStickX(0);
		case InputBinding::Axis::RightY: return in->GetRightStickY(0);
		case InputBinding::Axis::LeftTrigger: return in->GetLeftTrigger(0);
		case InputBinding::Axis::RightTrigger: return in->GetRightTrigger(0);
		}
		return 0.0f;
	};
	return src;
}

void EditorSuite::DrawInputActionsWindow()
{
#ifdef USE_IMGUI
	if (!inputWindowWorking_ || inputWindowRoot_ != currentProjectRoot_) {
		inputWindowWorking_ = GetInputActions();
		inputWindowRoot_ = currentProjectRoot_;
		inputCaptureAction_.clear();
	}
	ImGui::SetNextWindowSize(ImVec2(620.0f, 520.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TrId("Input Actions"), &showInputActionsWindow_)) {
		ImGui::End();
		return;
	}
	InputActions& work = *inputWindowWorking_;
	ImGui::TextDisabled("%s", Tr("Scripts: IsActionDown(\"Jump\"), IsActionTriggered(\"Attack\"), GetAxis(\"MoveX\"). Saved to ProjectSettings/input.json."));

	// Waiting for a key / pad button / stick to bind
	if (!inputCaptureAction_.empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), Tr("%s '%s' (Esc = cancel)"), Tr("Press a key, a gamepad button or move a stick for"), inputCaptureAction_.c_str());
		std::optional<InputBinding> captured;
		if (const Input* in = currentInput_) {
			if (in->TriggerKey(KEY_ESCAPE)) {
				inputCaptureAction_.clear();
			} else {
				for (int k = 1; k < 256 && !captured; ++k) if (in->TriggerKey(static_cast<BYTE>(k))) captured = InputBinding::Key(k);
				for (int bit = 0; bit < 16 && !captured; ++bit) {
					if (in->TriggerGamepadButton(static_cast<WORD>(1u << bit), 0)) captured = InputBinding::Pad(1 << bit);
				}
				const float axes[] = { in->GetLeftStickX(0), in->GetLeftStickY(0), in->GetRightStickX(0), in->GetRightStickY(0), in->GetLeftTrigger(0), in->GetRightTrigger(0) };
				for (int a = 0; a < 6 && !captured; ++a) {
					if (std::fabs(axes[a]) > 0.8f) captured = InputBinding::PadAxis(static_cast<InputBinding::Axis>(a), axes[a] < 0.0f ? -1.0f : 1.0f);
				}
			}
		}
		if (captured && !inputCaptureAction_.empty()) {
			if (InputAction* a = work.Find(inputCaptureAction_)) a->bindings.push_back(*captured);
			inputCaptureAction_.clear();
			inputWindowDirty_ = true;
		}
	}

	std::string removeAction;
	ImGui::BeginChild("##InputActionList", ImVec2(0.0f, -70.0f), ImGuiChildFlags_Borders);
	for (InputAction& action : work.All()) {
		ImGui::PushID(action.name.c_str());
		ImGui::SeparatorText(action.name.c_str());
		int removeBinding = -1;
		for (int i = 0; i < static_cast<int>(action.bindings.size()); ++i) {
			ImGui::PushID(i);
			InputBinding& b = action.bindings[i];
			ImGui::Bullet();
			ImGui::SameLine();
			ImGui::TextUnformatted(InputActions::BindingLabel(b).c_str());
			ImGui::SameLine(300.0f);
			if (ImGui::SmallButton(b.sign < 0.0f ? "-1" : "+1")) { b.sign = -b.sign; inputWindowDirty_ = true; }
			ImGui::SetItemTooltip("%s", Tr("Direction for GetAxis (e.g. A = -1, D = +1)"));
			ImGui::SameLine();
			if (ImGui::SmallButton("x")) removeBinding = i;
			ImGui::PopID();
		}
		if (removeBinding >= 0) {
			action.bindings.erase(action.bindings.begin() + removeBinding);
			inputWindowDirty_ = true;
		}
		if (ImGui::SmallButton(TrId("+ Key / Pad"))) inputCaptureAction_ = action.name;
		for (int m = 0; m < 3; ++m) {
			ImGui::SameLine();
			const std::string label = std::format("+ Mouse {}", m);
			if (ImGui::SmallButton(label.c_str())) { action.bindings.push_back(InputBinding::Mouse(m)); inputWindowDirty_ = true; }
		}
		ImGui::SameLine();
		if (ImGui::SmallButton(TrId("Remove Action"))) removeAction = action.name;
		ImGui::PopID();
	}
	ImGui::EndChild();
	if (!removeAction.empty()) {
		work.Remove(removeAction);
		inputWindowDirty_ = true;
	}

	ImGui::SetNextItemWidth(180.0f);
	ImGui::InputTextWithHint("##NewAction", Tr("Dash"), inputNewActionBuf_, sizeof(inputNewActionBuf_));
	ImGui::SameLine();
	if (ImGui::Button(TrId("Add Action")) && inputNewActionBuf_[0] != '\0') {
		work.Add(inputNewActionBuf_);
		inputNewActionBuf_[0] = '\0';
		inputWindowDirty_ = true;
	}
	ImGui::SameLine();
	if (ImGui::Button(TrId("Reset to Defaults"))) {
		work = InputActions::Defaults();
		inputWindowDirty_ = true;
	}
	ImGui::BeginDisabled(!inputWindowDirty_ || currentProjectRoot_.empty());
	if (ImGui::Button(TrId("Save"), ImVec2(120.0f, 0.0f)) && SaveInputActions(work)) {
		inputWindowDirty_ = false;
		LogMessage("[Input] Saved ProjectSettings/input.json", 0);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!inputWindowDirty_);
	if (ImGui::Button(TrId("Revert"), ImVec2(120.0f, 0.0f))) {
		work = GetInputActions();
		inputWindowDirty_ = false;
	}
	ImGui::EndDisabled();
	ImGui::End();
#endif
}
