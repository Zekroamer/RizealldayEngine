#include "EditorSuiteInternal.h"
#include "base/TextureManager.h"

// Game UI saved in the scene: canvases and UI elements are scene objects with data components (UIComponents.h).
// Layout is UILayout (pure); drawing goes through the UI renderer's canvas layer, below the immediate UI.

namespace {
	// The object itself or its nearest ancestor that has `component`; the walk ends at a Canvas
	GameObject* SelfOrAncestorWith(EditorSuite& s, GameObject* obj, const char* component)
	{
		for (int depth = 0; obj && depth < 64; ++depth) { // a parent cycle in a broken file
			if (obj->components.Has(component)) return obj;
			if (obj->components.Has(kCanvasComponent) || obj->parentId < 0) return nullptr;
			obj = s.FindGameObjectById(static_cast<uint32_t>(obj->parentId), false);
		}
		return nullptr;
	}
}

void EditorSuite::RegisterGameUIComponents()
{
	RegisterDataComponent<CanvasComponent>(kCanvasComponent, "UI", {});
	ComponentInfo& rect = RegisterDataComponent<UIRectComponent>(kUIRectComponent, "UI", {
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
	rect.drawExtra = [](EditorSuite& s, GameObject& o, ComponentData&) { DrawUIRectAnchorPresets(s, o); }; // EditorSuite_UIEditor.cpp
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
	RegisterDataComponent<UIButtonComponent>(kUIButtonComponent, "UI", {
		RIZE_FIELD(UIButtonComponent, label, ComponentFieldType::Text, 0.0f, 0.0f),
		RIZE_FIELD(UIButtonComponent, labelSize, ComponentFieldType::Float, 1.0f, 512.0f),
		RIZE_FIELD(UIButtonComponent, normalColor, ComponentFieldType::Color, 0.0f, 0.0f),
		RIZE_FIELD(UIButtonComponent, hoverColor, ComponentFieldType::Color, 0.0f, 0.0f),
		RIZE_FIELD(UIButtonComponent, pressedColor, ComponentFieldType::Color, 0.0f, 0.0f) });
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

	static const char* const kNames[] = { "Canvas", "Panel", "Image", "Text", "Progress Bar", "Button" };
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
	} else if (kind == UIObjectKind::Button) {
		addData(obj, kUIRectComponent);
		addData(obj, kUIButtonComponent);
		auto* rect = obj.components.Get<UIRectComponent>(kUIRectComponent);
		rect->width = 240.0f;
		rect->height = 60.0f;
		strncpy_s(obj.components.Get<UIButtonComponent>(kUIButtonComponent)->label, "Button", _TRUNCATE);
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

void EditorSuite::ForEachGameUIElement(float viewWidth, float viewHeight, const std::function<void(const GameObject&, const UILayout::Rect&)>& fn)
{
	if (viewWidth <= 0.0f || viewHeight <= 0.0f) return;
	const float scale = UILayout::CanvasScale(viewHeight);
	const auto visit = [&](auto& self, const GameObject& parent, const UILayout::Rect& parentRect, int depth) -> void {
		if (depth > 64) return; // a parent cycle in a broken file
		for (const uint32_t childId : parent.childrenIds) {
			const GameObject* child = FindGameObjectById(childId, false);
			if (!child || !child->isActive) continue;
			const UIRectComponent* placement = child->components.Get<UIRectComponent>(kUIRectComponent);
			if (!placement) continue; // not a UI element: neither it nor its children are drawn
			const UILayout::Rect r = UILayout::Place(parentRect, *placement, scale);
			fn(*child, r);
			self(self, *child, r, depth + 1);
		}
	};
	for (const GameObject& obj : gameObjects_) {
		if (obj.isActive && obj.components.Has(kCanvasComponent)) visit(visit, obj, { 0.0f, 0.0f, viewWidth, viewHeight }, 0);
	}
}

const GameObject* EditorSuite::HitTestGameUI(float x, float y, float viewWidth, float viewHeight)
{
	const GameObject* top = nullptr;
	ForEachGameUIElement(viewWidth, viewHeight, [&](const GameObject& obj, const UILayout::Rect& r) {
		if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) top = &obj; // later = drawn on top
	});
	return top;
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
		// Centred vertically; the glyphs keep their shape on any aspect ratio (widthScale)
		const auto drawText = [&](const char* text, size_t capacity, float size, const Vector4& color, int alignment, const UILayout::Rect& r) {
			const float lineHeight = size * scale;
			const int lines = 1 + static_cast<int>(std::count(text, text + strnlen_s(text, capacity), '\n'));
			const float top = r.y + (r.height - lineHeight * static_cast<float>(lines)) * 0.5f;
			alignment = std::clamp(alignment, 0, 2);
			const float anchorX = (r.x + r.width * 0.5f * static_cast<float>(alignment)) * toCanvasX;
			ui->DrawString(text, anchorX, top * toCanvasY, lineHeight * toCanvasY, color, static_cast<UIRenderer::Align>(alignment), toCanvasX / toCanvasY);
		};
		ForEachGameUIElement(viewWidth, viewHeight, [&](const GameObject& obj, const UILayout::Rect& r) {
			const float x = r.x * toCanvasX, y = r.y * toCanvasY, w = r.width * toCanvasX, h = r.height * toCanvasY;
			if (const UIImageComponent* image = obj.components.Get<UIImageComponent>(kUIImageComponent)) {
				if (image->texture[0] != '\0') {
					ui->DrawImage(TextureManager::GetInstance()->LoadTexture(ResolveAssetPath(image->texture)), x, y, w, h, image->color);
				} else {
					ui->DrawRect(x, y, w, h, image->color);
				}
			}
			if (const UIProgressBarComponent* bar = obj.components.Get<UIProgressBarComponent>(kUIProgressBarComponent)) {
				const float fill = std::clamp(bar->value, 0.0f, 1.0f);
				ui->DrawRect(x, y, w, h, bar->backgroundColor);
				if (fill > 0.0f) ui->DrawRect(x, y, w * fill, h, bar->fillColor);
			}
			if (const UIButtonComponent* button = obj.components.Get<UIButtonComponent>(kUIButtonComponent)) {
				const bool hovered = uiHoverId_ == obj.id || uiSelectedButtonId_ == obj.id;
				const Vector4& color = (uiPressedId_ == obj.id && uiHoverId_ == obj.id) ? button->pressedColor : (hovered ? button->hoverColor : button->normalColor);
				ui->DrawRect(x, y, w, h, color);
			}
			EditorSuiteDetail::ForEachUIText(obj, [&](const char* text, size_t capacity, float size, const Vector4& color, int alignment) {
				drawText(text, capacity, size, color, alignment, r);
			});
		});
	}
	ui->EndCanvasLayer();
}

GameObject* EditorSuite::FindUIElementByName(const char* name)
{
	if (!name || name[0] == '\0') return nullptr;
	for (GameObject& obj : gameObjects_) {
		if (obj.name == name && obj.components.Has(kUIRectComponent) && SelfOrAncestorWith(*this, &obj, kCanvasComponent)) return &obj;
	}
	return nullptr;
}

void EditorSuite::UpdateGameUIInput(const GameUIPointer& pointer)
{
	const bool running = sceneState_ == EditorSceneState::Play || IsStandaloneMode();
	if (!running) {
		uiHoverId_ = uiPressedId_ = 0;
		uiPointerWasDown_ = false;
		return;
	}
	const GameObject* hit = pointer.inside ? HitTestGameUI(pointer.x, pointer.y, pointer.viewWidth, pointer.viewHeight) : nullptr;
	// A label or icon on a button is part of it; anything else on top (a panel) blocks the button
	hit = hit ? SelfOrAncestorWith(*this, FindGameObjectById(hit->id, false), kUIButtonComponent) : nullptr;
	uiHoverId_ = hit ? hit->id : 0;

	std::string clicked;
	if (pointer.down && !uiPointerWasDown_) uiPressedId_ = uiHoverId_;
	if (!pointer.down && uiPointerWasDown_) {
		// A click = released over the button it went down on
		if (uiPressedId_ != 0 && uiPressedId_ == uiHoverId_) clicked = hit->name;
		uiPressedId_ = 0;
	}
	uiPointerWasDown_ = pointer.down;
	if (pointer.confirm && clicked.empty() && uiSelectedButtonId_ != 0) {
		// Only a selected button that is drawn can be confirmed
		ForEachGameUIElement(pointer.viewWidth, pointer.viewHeight, [&](const GameObject& obj, const UILayout::Rect&) {
			if (obj.id == uiSelectedButtonId_ && obj.components.Has(kUIButtonComponent)) clicked = obj.name;
		});
	}
	if (!clicked.empty()) DispatchUIButtonClick(clicked.c_str());
}

void EditorSuite::DispatchUIButtonClick(const char* name)
{
	// Every script hears every click. Callbacks never resize gameObjects_ (spawns and deletes are deferred).
	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		if (!gameObjects_[i].isActive || gameObjects_[i].scripts.empty()) continue;
		const uint32_t objId = gameObjects_[i].id;
		for (size_t s = 0; s < gameObjects_[i].scripts.size(); ++s) {
			ScriptBase* instance = gameObjects_[i].scripts[s].instance.get();
			ScriptGameObjectView* view = GetOrUpdateScriptViewById(objId);
			if (!instance || !view) continue;
			instance->SetOwner(view, objId);
			instance->SetEngineAPI(&engineApi_);
			instance->SetHost(this, this);
			CallScript(instance, objId, "OnUIButton", [&] { instance->OnUIButton(name); });
			SyncViewToGameObject(*view, gameObjects_[i]);
		}
	}
}
