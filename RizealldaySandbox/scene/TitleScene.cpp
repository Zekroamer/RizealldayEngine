#include "scene/TitleScene.h"
#include "scene/EditorScene.h"
#include "scene/SceneManager.h"
#include "base/TextureManager.h"
#include "base/WinApp.h"
#include <algorithm>
#include <cmath>

namespace {
float Clamp01(float x) {
	return std::clamp(x, 0.0f, 1.0f);
}

float EaseOutCubic(float t) {
	t = Clamp01(t);
	float u = 1.0f - t;
	return 1.0f - u * u * u;
}

float EaseInCubic(float t) {
	t = Clamp01(t);
	return t * t * t;
}
} // namespace

TitleScene::~TitleScene() {
	Finalize();
}

void TitleScene::Initialize() {
	elapsed_ = 0.0f;
	transitioned_ = false;

	auto* dxCommon = RizeEngine::GetDirectXCommon();

	uint32_t bgTex = TextureManager::GetInstance()->LoadTexture("Resources/Title/IntroPage_bg.png");
	uint32_t iconTex = TextureManager::GetInstance()->LoadTexture("Resources/Title/rizeallday_icon.png");
	uint32_t textTex = TextureManager::GetInstance()->LoadTexture("Resources/Title/IntroPage_text.png");
	uint32_t whiteTex = TextureManager::GetInstance()->LoadTexture("Resources/Title/white.png");

	bgSprite_ = std::make_unique<Sprite>();
	bgSprite_->Initialize(dxCommon, bgTex);
	bgSprite_->SetPosition({ 0.0f, 0.0f });
	bgSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight) });

	iconSprite_ = std::make_unique<Sprite>();
	iconSprite_->Initialize(dxCommon, iconTex);

	textSprite_ = std::make_unique<Sprite>();
	textSprite_->Initialize(dxCommon, textTex);

	lineSprite_ = std::make_unique<Sprite>();
	lineSprite_->Initialize(dxCommon, whiteTex);
}

void TitleScene::Finalize() {
	if (lineSprite_) {
		lineSprite_->Finalize();
		lineSprite_.reset();
	}
	if (textSprite_) {
		textSprite_->Finalize();
		textSprite_.reset();
	}
	if (iconSprite_) {
		iconSprite_->Finalize();
		iconSprite_.reset();
	}
	if (bgSprite_) {
		bgSprite_->Finalize();
		bgSprite_.reset();
	}
}

void TitleScene::Update(float deltaTime) {
	if (transitioned_) {
		return;
	}

	elapsed_ += std::clamp(deltaTime, 0.0f, 0.05f);

	Input* input = RizeEngine::GetInput();
	bool skipRequested = false;
	if (input) {
		skipRequested = input->TriggerKey(DIK_SPACE) || input->TriggerKey(DIK_RETURN) || input->TriggerKey(DIK_ESCAPE);
	}

	constexpr float kTotalSplashDuration = 3.2f;
	if (elapsed_ >= kTotalSplashDuration || skipRequested) {
		transitioned_ = true;
		if (sceneManager_) {
			sceneManager_->ChangeScene(std::make_unique<EditorScene>());
		}
		return;
	}
}

void TitleScene::Draw() {
	if (!bgSprite_ || !iconSprite_ || !textSprite_ || !lineSprite_) {
		return;
	}

	auto* dxCommon = RizeEngine::GetDirectXCommon();
	auto* cmdList = dxCommon ? dxCommon->GetCommandList() : nullptr;
	if (!cmdList) {
		return;
	}

	if (auto* pipeline = RizeEngine::GetGraphicPipeline()) {
		pipeline->BindForBackBuffer(cmdList, BlendMode::AlphaBlend);
	}

	const float screenW = static_cast<float>(WinApp::kClientWidth);
	const float screenH = static_cast<float>(WinApp::kClientHeight);
	const float cx = screenW * 0.5f;
	const float cy = screenH * 0.5f;

	// Timeline matching Rizeallday Engine intro sequence:
	// 1. Line grows vertically:    0.10s .. 0.45s
	// 2. Icon & Text slide out:    0.35s .. 1.00s
	// 3. Hold full logo:           1.00s .. 2.00s
	// 4. Icon & Text slide back:   2.00s .. 2.65s
	// 5. Line shrinks vertically:  2.55s .. 3.00s
	// 6. Brief pause -> Editor:    3.00s .. 3.20s
	const float tLineIn = EaseOutCubic((elapsed_ - 0.10f) / 0.35f);
	const float tSlideIn = EaseOutCubic((elapsed_ - 0.35f) / 0.65f);
	const float tSlideOut = EaseInCubic((elapsed_ - 2.00f) / 0.65f);
	const float tLineOut = EaseInCubic((elapsed_ - 2.55f) / 0.45f);

	float lineProgress = tLineIn;
	if (elapsed_ >= 2.55f) {
		lineProgress = 1.0f - tLineOut;
	}
	lineProgress = Clamp01(lineProgress);

	float slideProgress = tSlideIn;
	if (elapsed_ >= 2.00f) {
		slideProgress = 1.0f - tSlideOut;
	}
	slideProgress = Clamp01(slideProgress);

	const float lineW = 4.0f;
	const float maxLineH = 140.0f;
	const float currentLineH = maxLineH * lineProgress;

	// Logo sizes (fixed height, width from the texture aspect ratio)
	const float logoH = 100.0f;
	const Vector2 iconTexSize = TextureManager::GetInstance()->GetTextureSize(iconSprite_->GetTextureHandle());
	const Vector2 textTexSize = TextureManager::GetInstance()->GetTextureSize(textSprite_->GetTextureHandle());
	const float iconW = (iconTexSize.y > 0.0f) ? logoH * (iconTexSize.x / iconTexSize.y) : 100.0f;
	const float textW = (textTexSize.y > 0.0f) ? logoH * (textTexSize.x / textTexSize.y) : 280.0f;

	// Center the whole "icon | text" group on screen. The divider line sits between icon and text,
	// so it is offset from the screen center whenever the icon and text widths differ.
	const float gap = 24.0f;
	const float groupW = iconW + gap + lineW + gap + textW;
	const float groupLeft = cx - groupW * 0.5f;
	const float lineX = groupLeft + iconW + gap + lineW * 0.5f;
	const float slitLeft = lineX - (lineW * 0.5f) - 2.0f;
	const float slitRight = lineX + (lineW * 0.5f) + 2.0f;

	// 1. Background (1280x720)
	bgSprite_->SetPosition({ 0.0f, 0.0f });
	bgSprite_->SetSize({ screenW, screenH });
	bgSprite_->Update();
	bgSprite_->Draw(cmdList);

	// 2. Icon (Left side, clipped at slitLeft using Chapter 05 SetTextureRect)
	if (slideProgress > 0.001f) {
		const float iconH = logoH;
		const float iconFinalX = groupLeft;
		const float iconStartX = slitLeft + 10.0f;
		const float curIconX = iconStartX + (iconFinalX - iconStartX) * slideProgress;
		const float iconY = cy - iconH * 0.5f;

		const float visibleRight = (std::min)(curIconX + iconW, slitLeft);
		const float visibleW = visibleRight - curIconX;

		if (visibleW > 0.5f && iconW > 0.0f) {
			const float visibleRatio = Clamp01(visibleW / iconW);
			iconSprite_->SetPosition({ curIconX, iconY });
			iconSprite_->SetSize({ visibleW, iconH });
			iconSprite_->SetTextureRect({ 0.0f, 0.0f }, { iconTexSize.x * visibleRatio, iconTexSize.y });
			iconSprite_->SetColor({ 1.0f, 1.0f, 1.0f, slideProgress });
			iconSprite_->Update();
			iconSprite_->Draw(cmdList);
		}
	}

	// 3. Engine Text (Right side, clipped at slitRight using Chapter 05 SetTextureRect)
	if (slideProgress > 0.001f) {
		const float textH = logoH;
		const float textFinalX = slitRight + gap - 2.0f;
		const float textStartX = slitRight - textW - 10.0f;
		const float curTextX = textStartX + (textFinalX - textStartX) * slideProgress;
		const float textY = cy - textH * 0.5f;

		const float visibleLeft = (std::max)(curTextX, slitRight);
		const float visibleRight = curTextX + textW;
		const float visibleW = visibleRight - visibleLeft;

		if (visibleW > 0.5f && textW > 0.0f) {
			const float cropOffsetRatio = Clamp01((visibleLeft - curTextX) / textW);
			const float visibleRatio = Clamp01(visibleW / textW);
			textSprite_->SetPosition({ visibleLeft, textY });
			textSprite_->SetSize({ visibleW, textH });
			textSprite_->SetTextureRect(
				{ textTexSize.x * cropOffsetRatio, 0.0f },
				{ textTexSize.x * visibleRatio, textTexSize.y }
			);
			textSprite_->SetColor({ 1.0f, 1.0f, 1.0f, slideProgress });
			textSprite_->Update();
			textSprite_->Draw(cmdList);
		}
	}

	// 4. Center Vertical White Line
	if (currentLineH > 0.5f) {
		lineSprite_->SetPosition({ lineX - lineW * 0.5f, cy - currentLineH * 0.5f });
		lineSprite_->SetSize({ lineW, currentLineH });
		lineSprite_->SetColor({ 1.0f, 1.0f, 1.0f, lineProgress });
		lineSprite_->Update();
		lineSprite_->Draw(cmdList);
	}
}
