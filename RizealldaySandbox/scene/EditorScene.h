#pragma once
#include <RizeEngine.h>
#include "EditorSuite.h"
#include <memory>

/**
 * @brief EditorScene is an application state hosting the full EditorSuite.
 *
 * NOTE ON SCENE TERMINOLOGY:
 * EditorScene is a top-level application state managed by SceneManager (State Pattern).
 * This is distinct from editor scene files (*.scene.json), which are serialized GameObject
 * scenes loaded, saved, and manipulated within EditorSuite.
 */
class EditorScene : public BaseScene {
public:
	EditorScene() = default;
	~EditorScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update(float deltaTime) override;
	void Draw() override;

private:
	Camera gameCamera_{};
	std::unique_ptr<LightGroup> lights_;
	std::unique_ptr<EditorSuite> editorSuite_;
	float lastDeltaTime_ = 0.016f;
};
