#include "EditorSuiteInternal.h"

// Tetromino definitions: 7 shapes (I, O, T, S, Z, J, L) in a 4x4 coordinate space
static const int kTetrominoShapes[7][4][2] = {
	{ {0, 1}, {1, 1}, {2, 1}, {3, 1} }, // I (Cyan)
	{ {1, 1}, {2, 1}, {1, 2}, {2, 2} }, // O (Yellow)
	{ {1, 1}, {0, 1}, {2, 1}, {1, 2} }, // T (Purple)
	{ {1, 1}, {2, 1}, {0, 2}, {1, 2} }, // S (Green)
	{ {0, 1}, {1, 1}, {1, 2}, {2, 2} }, // Z (Red)
	{ {0, 2}, {0, 1}, {1, 1}, {2, 1} }, // J (Blue)
	{ {2, 2}, {0, 1}, {1, 1}, {2, 1} }  // L (Orange)
};

static const Vector4 kTetrominoColors[7] = {
	{ 0.15f, 0.85f, 0.95f, 1.0f }, // I: Cyan
	{ 0.95f, 0.85f, 0.15f, 1.0f }, // O: Yellow
	{ 0.70f, 0.20f, 0.85f, 1.0f }, // T: Purple
	{ 0.20f, 0.85f, 0.25f, 1.0f }, // S: Green
	{ 0.90f, 0.20f, 0.20f, 1.0f }, // Z: Red
	{ 0.20f, 0.40f, 0.95f, 1.0f }, // J: Blue
	{ 0.95f, 0.55f, 0.15f, 1.0f }  // L: Orange
};

void EditorSuite::LoadTetrisTemplate()
{
	gameObjects_.clear();
	currentSceneName_ = "3DTetrisScene";
	isTetrisActive_ = true;
	tetrisScore_ = 0;
	tetrisLinesCleared_ = 0;
	tetrisLevel_ = 1;
	tetrisGameOver_ = false;
	tetrisFallTimer_ = 0.0f;
	currentPieceCubeIds_.clear();
	std::memset(tetrisBoard_, 0, sizeof(tetrisBoard_));
	std::memset(tetrisBlockIds_, 0, sizeof(tetrisBlockIds_));

	// 1. Camera centered directly in front of 10x20 Tetris Grid
	{
		auto& cam = CreateGameObject("Tetris Camera", GameObjectType::Camera);
		cam.translation = { 4.5f, 9.5f, -19.5f };
		cam.rotation = { 0.0f, 0.0f, 0.0f };
		cam.hasCamera = true;
		cam.fov = 50.0f;
		if (gameCamera_) {
			gameCamera_->translation = cam.translation;
			gameCamera_->rotation = cam.rotation;
		}
	}

	// 2. Directional Sun Light
	{
		auto& sun = CreateGameObject("Sun Light", GameObjectType::DirectionalLight);
		sun.hasLight = true;
		sun.lightType = GameObject::LightType::Directional;
		sun.lightDirection = { 0.3f, -1.0f, 0.4f };
		{
			Vector3 d = sun.lightDirection;
			float dLen = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
			if (dLen > 0.0001f) { d.x /= dLen; d.y /= dLen; d.z /= dLen; }
			sun.rotation = { std::asin(std::clamp(-d.y, -1.0f, 1.0f)), std::atan2(d.x, d.z), 0.0f };
			sun.rotationQuat = Quaternion::FromEuler(sun.rotation);
		}
		sun.lightColor = { 1.0f, 0.95f, 0.9f };
		sun.lightIntensity = 1.2f;
	}

	// 3. Point Light
	{
		auto& pt = CreateGameObject("Board Glow Light", GameObjectType::PointLight);
		pt.translation = { 4.5f, 10.0f, -6.0f };
		pt.hasLight = true;
		pt.lightType = GameObject::LightType::Point;
		pt.lightColor = { 0.6f, 0.8f, 1.0f };
		pt.lightIntensity = 1.5f;
		pt.lightRange = 30.0f;
	}

	// 4. Board Frame: Left Border, Right Border, and Floor
	Vector4 frameColor = { 0.15f, 0.16f, 0.18f, 1.0f };
	for (int y = 0; y < 20; ++y) {
		auto& cube = CreateCube("Wall_Left", { -1.0f, static_cast<float>(y), 0.0f });
		cube.materialColor = frameColor;
		cube.isStatic = true;
	}
	for (int y = 0; y < 20; ++y) {
		auto& cube = CreateCube("Wall_Right", { 10.0f, static_cast<float>(y), 0.0f });
		cube.materialColor = frameColor;
		cube.isStatic = true;
	}
	for (int x = -1; x <= 10; ++x) {
		auto& cube = CreateCube("Floor", { static_cast<float>(x), -1.0f, 0.0f });
		cube.materialColor = frameColor;
		cube.isStatic = true;
	}

	// 5. Backboard Plane
	{
		auto& back = CreateCube("BackBoard", { 4.5f, 9.5f, 0.6f });
		back.scale = { 12.0f, 21.0f, 0.2f };
		back.materialColor = { 0.07f, 0.08f, 0.10f, 1.0f };
		back.isStatic = true;
	}

	// 6. Row Clear Particle Explosion System
	{
		auto& ps = CreateGameObject("Row Clear Fireworks", GameObjectType::ParticleSystem);
		ps.translation = { 4.5f, 10.0f, 0.0f };
		if (AddComponentByName(ps, kGpuParticlesComponent)) {
			if (auto* p = ps.components.Get<GpuParticlesComponent>(kGpuParticlesComponent)) {
				ApplyGpuParticlePreset(*p, "Explosion");
				p->emitting = false; // bursts when a row clears
			}
		}
	}

	RestartTetrisGame();
	isSceneDirty_ = false;
	LogMessage("[Tetris] Loaded 3D Tetris Template. Press [ > Play ] to start playing!", 0);
}

void EditorSuite::RestartTetrisGame()
{
	for (int r = 0; r < 20; ++r) {
		for (int c = 0; c < 10; ++c) {
			if (tetrisBlockIds_[r][c] != 0) {
				for (auto it = gameObjects_.begin(); it != gameObjects_.end(); ++it) {
					if (it->id == tetrisBlockIds_[r][c]) {
						gameObjects_.erase(it);
						break;
					}
				}
			}
			tetrisBoard_[r][c] = 0;
			tetrisBlockIds_[r][c] = 0;
		}
	}

	tetrisScore_ = 0;
	tetrisLinesCleared_ = 0;
	tetrisLevel_ = 1;
	tetrisGameOver_ = false;
	tetrisFallTimer_ = 0.0f;

	currentTetrominoType_ = rand() % 7;
	currentTetrominoRot_ = 0;
	currentTetrominoX_ = 3;
	currentTetrominoY_ = 18;

	currentPieceCubeIds_.clear();
	for (int i = 0; i < 4; ++i) {
		auto& cube = CreateCube("FallingBlock", { 0.0f, 0.0f, 0.0f });
		cube.materialColor = kTetrominoColors[currentTetrominoType_];
		cube.lightingType = 4;
		cube.shininess = 64.0f;
		cube.specularColor = { 1.0f, 1.0f, 1.0f };
		cube.hasRigidbody = false;
		cube.hasBoxCollider = false;
		currentPieceCubeIds_.push_back(cube.id);
	}
}

void EditorSuite::UpdateTetrisGame(float deltaTime, Input* input)
{
	if (!isTetrisActive_ || tetrisGameOver_ || sceneState_ != EditorSceneState::Play) {
		return;
	}

	auto IsValidPosition = [&](int testX, int testY, int testRot) -> bool {
		for (int i = 0; i < 4; ++i) {
			int bx = kTetrominoShapes[currentTetrominoType_][i][0];
			int by = kTetrominoShapes[currentTetrominoType_][i][1];

			for (int r = 0; r < testRot % 4; ++r) {
				int tempX = bx;
				bx = by;
				by = 3 - tempX;
			}

			int gridX = testX + bx;
			int gridY = testY + by;

			if (gridX < 0 || gridX >= 10 || gridY < 0) return false;
			if (gridY < 20 && tetrisBoard_[gridY][gridX] != 0) return false;
		}
		return true;
	};

#ifdef USE_IMGUI
	ImGuiIO& io = ImGui::GetIO();
	if (ImGui::IsKeyPressed(ImGuiKey_A) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
		if (IsValidPosition(currentTetrominoX_ - 1, currentTetrominoY_, currentTetrominoRot_)) {
			currentTetrominoX_--;
		}
	}
	if (ImGui::IsKeyPressed(ImGuiKey_D) || ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
		if (IsValidPosition(currentTetrominoX_ + 1, currentTetrominoY_, currentTetrominoRot_)) {
			currentTetrominoX_++;
		}
	}
	if (ImGui::IsKeyPressed(ImGuiKey_W) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
		int nextRot = (currentTetrominoRot_ + 1) % 4;
		if (IsValidPosition(currentTetrominoX_, currentTetrominoY_, nextRot)) {
			currentTetrominoRot_ = nextRot;
		}
	}
	float fallSpeed = (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) ? 0.08f : tetrisFallInterval_;

	if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
		while (IsValidPosition(currentTetrominoX_, currentTetrominoY_ - 1, currentTetrominoRot_)) {
			currentTetrominoY_--;
			tetrisScore_ += 2;
		}
		tetrisFallTimer_ = 999.0f;
	}
#else
	float fallSpeed = tetrisFallInterval_;
#endif

	tetrisFallTimer_ += deltaTime;
	if (tetrisFallTimer_ >= fallSpeed) {
		tetrisFallTimer_ = 0.0f;
		if (IsValidPosition(currentTetrominoX_, currentTetrominoY_ - 1, currentTetrominoRot_)) {
			currentTetrominoY_--;
		} else {
			for (int i = 0; i < 4; ++i) {
				int bx = kTetrominoShapes[currentTetrominoType_][i][0];
				int by = kTetrominoShapes[currentTetrominoType_][i][1];
				for (int r = 0; r < currentTetrominoRot_ % 4; ++r) {
					int tempX = bx;
					bx = by;
					by = 3 - tempX;
				}
				int gx = currentTetrominoX_ + bx;
				int gy = currentTetrominoY_ + by;
				if (gx >= 0 && gx < 10 && gy >= 0 && gy < 20) {
					tetrisBoard_[gy][gx] = currentTetrominoType_ + 1;
					if (i < static_cast<int>(currentPieceCubeIds_.size())) {
						tetrisBlockIds_[gy][gx] = currentPieceCubeIds_[i];
					}
				}
			}

			int clearedRows = 0;
			for (int y = 0; y < 20; ++y) {
				bool rowFull = true;
				for (int x = 0; x < 10; ++x) {
					if (tetrisBoard_[y][x] == 0) { rowFull = false; break; }
				}

				if (rowFull) {
					clearedRows++;
					for (GameObject& fx : gameObjects_) {
						auto* p = fx.name == "Row Clear Fireworks" ? fx.components.Get<GpuParticlesComponent>(kGpuParticlesComponent) : nullptr;
						if (!p) continue;
						fx.translation = { 4.5f, static_cast<float>(y), 0.0f };
						p->burst += 35;
					}

					for (int x = 0; x < 10; ++x) {
						uint32_t cubeId = tetrisBlockIds_[y][x];
						for (auto it = gameObjects_.begin(); it != gameObjects_.end(); ++it) {
							if (it->id == cubeId) { gameObjects_.erase(it); break; }
						}
						tetrisBoard_[y][x] = 0;
						tetrisBlockIds_[y][x] = 0;
					}

					for (int ry = y; ry < 19; ++ry) {
						for (int rx = 0; rx < 10; ++rx) {
							tetrisBoard_[ry][rx] = tetrisBoard_[ry + 1][rx];
							tetrisBlockIds_[ry][rx] = tetrisBlockIds_[ry + 1][rx];
							if (tetrisBlockIds_[ry][rx] != 0) {
								for (auto& obj : gameObjects_) {
									if (obj.id == tetrisBlockIds_[ry][rx]) {
										obj.translation.y -= 1.0f;
										break;
									}
								}
							}
						}
					}
					for (int rx = 0; rx < 10; ++rx) {
						tetrisBoard_[19][rx] = 0;
						tetrisBlockIds_[19][rx] = 0;
					}
					y--;
				}
			}

			if (clearedRows > 0) {
				tetrisLinesCleared_ += clearedRows;
				tetrisScore_ += (clearedRows == 1 ? 100 : (clearedRows == 2 ? 300 : (clearedRows == 3 ? 500 : 800))) * tetrisLevel_;
				tetrisLevel_ = 1 + (tetrisLinesCleared_ / 10);
				tetrisFallInterval_ = (std::max)(0.15f, 0.7f - (tetrisLevel_ - 1) * 0.06f);
			}

			currentTetrominoType_ = rand() % 7;
			currentTetrominoRot_ = 0;
			currentTetrominoX_ = 3;
			currentTetrominoY_ = 18;

			if (!IsValidPosition(currentTetrominoX_, currentTetrominoY_, currentTetrominoRot_)) {
				tetrisGameOver_ = true;
				LogMessage(std::format("[Tetris] GAME OVER! Final Score: {}", tetrisScore_), 0);
				return;
			}

			currentPieceCubeIds_.clear();
			for (int i = 0; i < 4; ++i) {
				auto& cube = CreateCube("FallingBlock", { 0.0f, 0.0f, 0.0f });
				cube.materialColor = kTetrominoColors[currentTetrominoType_];
				cube.lightingType = 4;
				cube.shininess = 64.0f;
				cube.specularColor = { 1.0f, 1.0f, 1.0f };
				cube.hasRigidbody = false;
				cube.hasBoxCollider = false;
				currentPieceCubeIds_.push_back(cube.id);
			}
		}
	}

	Quaternion pieceQuat = Quaternion::FromAxisAngle({ 0.0f, 0.0f, 1.0f }, currentTetrominoRot_ * (3.14159265f / 2.0f));
	for (int i = 0; i < 4 && i < static_cast<int>(currentPieceCubeIds_.size()); ++i) {
		int bx = kTetrominoShapes[currentTetrominoType_][i][0];
		int by = kTetrominoShapes[currentTetrominoType_][i][1];
		for (int r = 0; r < currentTetrominoRot_ % 4; ++r) {
			int tempX = bx;
			bx = by;
			by = 3 - tempX;
		}

		for (auto& obj : gameObjects_) {
			if (obj.id == currentPieceCubeIds_[i]) {
				obj.translation = { static_cast<float>(currentTetrominoX_ + bx), static_cast<float>(currentTetrominoY_ + by), 0.0f };
				obj.rotationQuat = pieceQuat;
				obj.rotation = pieceQuat.ToEuler();
				break;
			}
		}
	}
}

void EditorSuite::DrawTetrisHUD(const ImVec2& vpPos, const ImVec2& vpSize)
{
#ifdef USE_IMGUI
	if (!isTetrisActive_) return;

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 hudPos = ImVec2(vpPos.x + 16.0f, vpPos.y + 16.0f);

	drawList->AddRectFilled(hudPos, ImVec2(hudPos.x + 200.0f, hudPos.y + 130.0f), IM_COL32(20, 24, 30, 220), 6.0f);
	drawList->AddRect(hudPos, ImVec2(hudPos.x + 200.0f, hudPos.y + 130.0f), IM_COL32(0, 180, 255, 200), 6.0f);

	std::string scoreStr = std::format("SCORE: {}", tetrisScore_);
	std::string linesStr = std::format("LINES: {}", tetrisLinesCleared_);
	std::string levelStr = std::format("LEVEL: {}", tetrisLevel_);

	drawList->AddText(ImVec2(hudPos.x + 12.0f, hudPos.y + 10.0f), IM_COL32(255, 255, 255, 255), "3D TETRIS DX12");
	drawList->AddText(ImVec2(hudPos.x + 12.0f, hudPos.y + 36.0f), IM_COL32(255, 215, 0, 255), scoreStr.c_str());
	drawList->AddText(ImVec2(hudPos.x + 12.0f, hudPos.y + 60.0f), IM_COL32(100, 220, 255, 255), linesStr.c_str());
	drawList->AddText(ImVec2(hudPos.x + 12.0f, hudPos.y + 84.0f), IM_COL32(100, 255, 120, 255), levelStr.c_str());
	drawList->AddText(ImVec2(hudPos.x + 12.0f, hudPos.y + 106.0f), IM_COL32(180, 180, 180, 255), "A/D: Move | W: Rot | Space: Drop");

	if (tetrisGameOver_) {
		ImVec2 center = ImVec2(vpPos.x + vpSize.x * 0.5f, vpPos.y + vpSize.y * 0.5f);
		drawList->AddRectFilled(ImVec2(center.x - 140.0f, center.y - 45.0f), ImVec2(center.x + 140.0f, center.y + 45.0f), IM_COL32(150, 0, 0, 230), 8.0f);
		drawList->AddText(ImVec2(center.x - 50.0f, center.y - 30.0f), IM_COL32(255, 255, 255, 255), "GAME OVER");

		ImGui::SetCursorScreenPos(ImVec2(center.x - 60.0f, center.y + 2.0f));
		if (ImGui::Button(TrId("Restart Tetris"), ImVec2(120.0f, 26.0f))) {
			RestartTetrisGame();
		}
	}
#endif
}

