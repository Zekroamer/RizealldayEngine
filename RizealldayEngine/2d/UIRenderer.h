#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include "math/Vector4.h"

class DirectXCommon;
class DXCManager;

/// Immediate-mode game UI (HUD, menus). Works in every build configuration, including Release without ImGui.
/// Coordinates are in a fixed 1280 x 720 canvas (origin top-left) that is stretched over the game view.
/// Commands are kept until Clear(), so the same UI can be rendered into several targets in one frame.
class UIRenderer
{
public:
	static constexpr float kCanvasWidth = 1280.0f;
	static constexpr float kCanvasHeight = 720.0f;
	static constexpr float kFontPixelSize = 32.0f; // atlas glyph height; text is scaled from this

	enum class Align : int { Left = 0, Center = 1, Right = 2 };

	static UIRenderer* GetInstance();

	void Initialize(DirectXCommon* dxCommon, DXCManager* dxc);
	void Finalize();

	/// Clears the immediate commands (scripts); the canvas layer is kept
	void Clear();
	bool HasCommands() const { return !vertices_.empty(); }
	size_t GetQuadCount() const { return vertices_.size() / 6; }

	void DrawRect(float x, float y, float w, float h, const Vector4& color);
	void DrawImage(uint32_t textureHandle, float x, float y, float w, float h, const Vector4& color);
	/// UTF-8 text (Japanese included); '\n' starts a new line. `size` is the line height in canvas pixels.
	/// `widthScale` multiplies glyph widths (the canvas layer undoes the canvas stretch with it so text keeps its shape).
	void DrawString(const char* text, float x, float y, float size, const Vector4& color, Align align = Align::Left, float widthScale = 1.0f);
	/// Width of the widest line of `text` at `size`
	float MeasureText(const char* text, float size) const;

	/// Canvas layer (scene UI elements): drawn below the immediate commands. BeginCanvasLayer clears it and sends
	/// every draw call to it until EndCanvasLayer.
	void BeginCanvasLayer();
	void EndCanvasLayer();
	size_t GetCanvasQuadCount() const { return (inCanvasLayer_ ? vertices_.size() : canvasVertices_.size()) / 6; }

	/// Draws the canvas layer, then (if `immediate`) the immediate commands into the currently bound render target
	void Render(ID3D12GraphicsCommandList* cmdList, bool immediate = true);

	/// Glyphs rasterized so far / atlas pages in use (tests, profiler)
	size_t GetGlyphCount() const { return glyphs_.size(); }
	size_t GetFontPageCount() const { return pages_.size(); }
	/// True when `codepoint` has a real glyph (primary or fallback font), false when it is drawn as a box
	bool HasGlyph(uint32_t codepoint) const;
	/// Next code point of a UTF-8 string (invalid bytes -> U+FFFD); advances `p`
	static uint32_t DecodeUtf8(const char*& p, const char* end);

private:
	UIRenderer();
	~UIRenderer();
	UIRenderer(const UIRenderer&) = delete;
	UIRenderer& operator=(const UIRenderer&) = delete;

	struct Vertex {
		float x, y;   // NDC
		float u, v;
		Vector4 color;
	};
	struct Batch {
		uint32_t texture = 0;
		uint32_t firstVertex = 0;
		uint32_t vertexCount = 0;
	};
	struct Glyph {
		float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
		float width = 0;   // pixels at kFontPixelSize
		float advance = 0;
		uint32_t page = 0;
		bool missing = false; // drawn with the box glyph
	};
	// One atlas texture. Glyphs are rasterized into `rgba` on first use and uploaded once per frame (Render)
	struct FontPage {
		std::vector<uint8_t> rgba;
		uint32_t texture = 0;
		bool dirty = false;
	};
	struct GlyphRasterizer; // GDI state (UIRenderer.cpp)

	void CreateRootSignature();
	void CreatePipeline(DXCManager* dxc);
	void InitializeFont();
	void FinalizeFont();
	const Glyph& GetGlyph(uint32_t codepoint) const;
	bool RasterizeGlyph(uint32_t codepoint, Glyph& out) const;
	void UploadFontPages();
	void PushQuad(uint32_t texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1, const Vector4& color);
	float MeasureLine(const char* begin, const char* end, float scale) const;

	static constexpr size_t kMaxVertices = 6 * 16384;
	static constexpr int kPageSize = 1024;
	static constexpr size_t kMaxFontPages = 4;            // 4 x 1024^2: a few thousand CJK glyphs at 32 px
	static constexpr uint32_t kFontPageTag = 0x80000000u; // Batch::texture = tag | page, resolved in Render

	DirectXCommon* dxCommon_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;
	std::vector<Vertex> vertices_;
	std::vector<Batch> batches_;
	// The canvas layer while not recording it (swapped with vertices_ / batches_ between Begin/EndCanvasLayer)
	std::vector<Vertex> canvasVertices_;
	std::vector<Batch> canvasBatches_;
	bool inCanvasLayer_ = false;
	// Measuring rasterizes too, so the glyph cache is mutable
	mutable std::unordered_map<uint32_t, Glyph> glyphs_;
	mutable std::vector<FontPage> pages_;
	mutable int penX_ = 0;
	mutable int penY_ = 0;
	std::unique_ptr<GlyphRasterizer> rasterizer_;
	uint32_t whiteTexture_ = 0;
	float fontLineHeight_ = kFontPixelSize;
};
