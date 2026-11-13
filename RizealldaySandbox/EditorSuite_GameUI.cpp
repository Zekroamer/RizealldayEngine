#include "EditorSuiteInternal.h"
#include "base/TextureManager.h"

// Game UI saved in the scene: canvases and UI elements are scene objects with data components (UIComponents.h).
// Layout is UILayout (pure); drawing goes through the UI renderer's canvas layer, below the immediate UI.

void EditorSuite::RegisterGameUIComponents()
{
	RegisterDataComponent<CanvasComponent>(kCanvasComponent, "UI", {});
	RegisterDataComponent<UIRectComponent>(kUIRectComponent, "UI", {
		RIZE_FIELD(UIRectComponent, anchorMinX, ComponentFieldType::Float, 0.0f, 1.0f),
		RIZE_FIELD(UIRectComponent, anchorMinY, ComponentFieldType::Float, 0.0f, 1.0f),
		RIZE_FIELD(UIRectComponent, anchorMaxX, ComponentFieldType::Float, 0.0f, 1.0f),
		RIZE_FIELD(UIRectComponent, anchorMaxY, ComponentFieldType::Float, 0.0f, 1.0f),
		RIZE_FIELD(UIRectComponent, pivotX, ComponentFieldType::Float, 0.0f, 1.0f),
		RIZE_FIELD(UIRectComponent, pivotY, ComponentFieldType::Float, 0.0f, 1.0f),
		RIZE_FIELD(UIRectComponent, offsetX, ComponentFieldType::Float, 0.0f, 0.0f),
		RIZE_FIELD(UIRectComponent, offsetY, ComponentFieldType::Float, 0.0f, 0.0f),
		RIZE_FIELD(UIRectComponent, width, ComponentFieldType::Float, 0.0f, 0.0f),
		RIZE_FIELD(UIRectComponent, height, ComponentFieldType::Float, 0.0f, 0.0f) });
	RegisterDataComponent<UIImageComponent>(kUIImageComponent, "UI", {
		RIZE_FIELD(UIImageComponent, texture, ComponentFieldType::Asset, 0.0f, 0.0f),
		RIZE_FIELD(UIImageComponent, color, ComponentFieldType::Color, 0.0f, 0.0f) });
	ComponentInfo& text = RegisterDataComponent<UITextComponent>(kUITextComponent, "UI", {
		RIZE_FIELD(UITextComponent, text, ComponentFieldType::Text, 0.0f, 0.0f),
		RIZE_FIELD(UITextComponent, size, ComponentFieldType::Float, 1.0f, 512.0f),
		RIZE_FIELD(UITextComponent, color, ComponentFieldType::Color, 0.0f, 0.0f),
		RIZE_FIELD(UITextComponent, alignment, ComponentFieldType::Int, 0.0f, 2.0f) });
	for (ComponentField& field : text.fields) {
		if (std::string_view(field.name) == "alignment") field.tooltip = "0 Left, 1 Centre, 2 Right";
	}
	RegisterDataComponent<UIProgressBarComponent>(kUIProgressBarComponent, "UI", {
		RIZE_FIELD(UIProgressBarComponent, value, ComponentFieldType::Float, 0.0f, 1.0f),
		RIZE_FIELD(UIProgressBarComponent, fillColor, ComponentFieldType::Color, 0.0f, 0.0f),
		RIZE_FIELD(UIProgressBarComponent, backgroundColor, ComponentFieldType::Color, 0.0f, 0.0f) });
}

uint32_t EditorSuite::CreateUIObject(UIObjectKind kind)
{
	const auto isUi = [](const GameObject& o) { return o.components.Has(kCanvasComponent) || o.components.Has(kUIRectComponent); };
	const auto addData = [](GameObject& o, const char* name) { o.components.Add(name, ComponentRegistry::Get().Find(name)->create()); };

	int parentId = -1;
	if (kind != UIObjectKind::Canvas) {
		if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size()) && isUi(gameObjects_[selectedObjectIndex_])) {
			parentId = static_cast<int>(gameObjects_[selectedObjectIndex_].id);
		} else {
			for (const GameObject& o : gameObjects_) {
				if (o.components.Has(kCanvasComponent)) { parentId = static_cast<int>(o.id); break; }
			}
		}
		if (parentId == -1) parentId = static_cast<int>(CreateUIObject(UIObjectKind::Canvas));
	}

	static const char* const kNames[] = { "Canvas", "Panel", "Image", "Text", "Progress Bar" };
	GameObject& obj = CreateGameObject(kNames[static_cast<int>(kind)], kind == UIObjectKind::Canvas ? GameObjectType::Canvas : GameObjectType::Empty);
	if (kind == UIObjectKind::Canvas) {
		addData(obj, kCanvasComponent);
	} else if (kind == UIObjectKind::Text) {
		addData(obj, kUIRectComponent);
		addData(obj, kUITextComponent);
		auto* rect = obj.components.Get<UIRectComponent>(kUIRectComponent);
		rect->width = 300.0f;
		rect->height = 60.0f;
		strncpy_s(obj.components.Get<UITextComponent>(kUITextComponent)->text, "New Text", _TRUNCATE);
	} else if (kind == UIObjectKind::ProgressBar) {
		addData(obj, kUIRectComponent);
		addData(obj, kUIProgressBarComponent);
		auto* rect = obj.components.Get<UIRectComponent>(kUIRectComponent);
		rect->width = 300.0f;
		rect->height = 30.0f;
	} else {
		addData(obj, kUIRectComponent);
		addData(obj, kUIImageComponent);
		auto* rect = obj.components.Get<UIRectComponent>(kUIRectComponent);
		auto* image = obj.components.Get<UIImageComponent>(kUIImageComponent);
		if (kind == UIObjectKind::Panel) {
			rect->width = 400.0f;
			rect->height = 300.0f;
			image->color = { 0.1f, 0.1f, 0.1f, 0.7f };
		}
	}
	obj.parentId = parentId;
	const uint32_t id = obj.id;
	RebuildHierarchyLinks();
	UpdateWorldMatrices();
	return id;
}

void EditorSuite::DrawGameUI(float viewWidth, float viewHeight)
{
	UIRenderer* ui = UIRenderer::GetInstance();
	ui->BeginCanvasLayer();
	if (viewWidth > 0.0f && viewHeight > 0.0f) {
		const float scale = UILayout::CanvasScale(viewHeight);
		// View pixels -> the UI renderer's 1280x720 canvas, which is stretched over the view
		const float toCanvasX = UIRenderer::kCanvasWidth / viewWidth;
		const float toCanvasY = UIRenderer::kCanvasHeight / viewHeight;
		const auto drawChildren = [&](auto& self, const GameObject& parent, const UILayout::Rect& parentRect, int depth) -> void {
			if (depth > 64) return; // a parent cycle in a broken file
			for (const uint32_t childId : parent.childrenIds) {
				const GameObject* child = FindGameObjectById(childId, false);
				if (!child || !child->isActive) continue;
				const UIRectComponent* placement = child->components.Get<UIRectComponent>(kUIRectComponent);
				if (!placement) continue; // not a UI element: neither it nor its children are drawn
				const UILayout::Rect r = UILayout::Place(parentRect, *placement, scale);
				const float x = r.x * toCanvasX, y = r.y * toCanvasY, w = r.width * toCanvasX, h = r.height * toCanvasY;
				if (const UIImageComponent* image = child->components.Get<UIImageComponent>(kUIImageComponent)) {
					if (image->texture[0] != '\0') {
						ui->DrawImage(TextureManager::GetInstance()->LoadTexture(ResolveAssetPath(image->texture)), x, y, w, h, image->color);
					} else {
						ui->DrawRect(x, y, w, h, image->color);
					}
				}
				if (const UIProgressBarComponent* bar = child->components.Get<UIProgressBarComponent>(kUIProgressBarComponent)) {
					const float fill = std::clamp(bar->value, 0.0f, 1.0f);
					ui->DrawRect(x, y, w, h, bar->backgroundColor);
					if (fill > 0.0f) ui->DrawRect(x, y, w * fill, h, bar->fillColor);
				}
				if (const UITextComponent* text = child->components.Get<UITextComponent>(kUITextComponent)) {
					// Centred vertically; the glyphs keep their shape on any aspect ratio (widthScale)
					const float lineHeight = text->size * scale;
					const int lines = 1 + static_cast<int>(std::count(text->text, text->text + strnlen_s(text->text, sizeof(text->text)), '\n'));
					const float top = r.y + (r.height - lineHeight * static_cast<float>(lines)) * 0.5f;
					const int alignment = std::clamp(text->alignment, 0, 2);
					const float anchorX = x + w * 0.5f * static_cast<float>(alignment);
					ui->DrawString(text->text, anchorX, top * toCanvasY, lineHeight * toCanvasY, text->color,
						static_cast<UIRenderer::Align>(alignment), toCanvasX / toCanvasY);
				}
				self(self, *child, r, depth + 1);
			}
		};
		for (const GameObject& obj : gameObjects_) {
			if (obj.isActive && obj.components.Has(kCanvasComponent)) drawChildren(drawChildren, obj, { 0.0f, 0.0f, viewWidth, viewHeight }, 0);
		}
	}
	ui->EndCanvasLayer();
}
