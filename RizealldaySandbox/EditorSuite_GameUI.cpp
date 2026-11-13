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

	static const char* const kNames[] = { "Canvas", "Panel", "Image" };
	GameObject& obj = CreateGameObject(kNames[static_cast<int>(kind)], kind == UIObjectKind::Canvas ? GameObjectType::Canvas : GameObjectType::Empty);
	if (kind == UIObjectKind::Canvas) {
		addData(obj, kCanvasComponent);
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
				if (const UIImageComponent* image = child->components.Get<UIImageComponent>(kUIImageComponent)) {
					const float x = r.x * toCanvasX, y = r.y * toCanvasY, w = r.width * toCanvasX, h = r.height * toCanvasY;
					if (image->texture[0] != '\0') {
						ui->DrawImage(TextureManager::GetInstance()->LoadTexture(ResolveAssetPath(image->texture)), x, y, w, h, image->color);
					} else {
						ui->DrawRect(x, y, w, h, image->color);
					}
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
