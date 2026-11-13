#include "2d/UIRenderer.h"
#include "base/DirectXCommon.h"
#include "base/DXCManager.h"
#include "base/TextureManager.h"
#include "base/Profiler.h"
#include "base/Log.h"
#include <Windows.h>
#include <algorithm>
#include <cassert>
#include <cstring>

UIRenderer* UIRenderer::GetInstance()
{
	static UIRenderer instance;
	return &instance;
}

void UIRenderer::Initialize(DirectXCommon* dxCommon, DXCManager* dxc)
{
	dxCommon_ = dxCommon;
	vertices_.reserve(6 * 1024);
	CreateRootSignature();
	CreatePipeline(dxc);
	whiteTexture_ = TextureManager::GetInstance()->GetDefaultWhiteTexture();
	InitializeFont();
}

void UIRenderer::Finalize()
{
	// Called after the GPU has been flushed (RizeEngine::Finalize)
	pso_.Reset();
	rootSignature_.Reset();
	Clear();
	FinalizeFont();
	dxCommon_ = nullptr;
}

void UIRenderer::CreateRootSignature()
{
	// [0] t0 texture (pixel), static linear-clamp sampler s0
	D3D12_DESCRIPTOR_RANGE range{};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.NumDescriptors = 1;
	range.BaseShaderRegister = 0;
	range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParameters[1] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].DescriptorTable.pDescriptorRanges = &range;
	rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC sigDesc{};
	sigDesc.pParameters = rootParameters;
	sigDesc.NumParameters = _countof(rootParameters);
	sigDesc.pStaticSamplers = &sampler;
	sigDesc.NumStaticSamplers = 1;
	sigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&sigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		if (errorBlob) {
			Log::GetInstance()->logOutput(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		}
		assert(false);
		return;
	}
	hr = dxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));
}

void UIRenderer::CreatePipeline(DXCManager* dxc)
{
	Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxc->CompileShader(L"shaders/UI.VS.hlsl", L"vs_6_0", dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
	Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxc->CompileShader(L"shaders/UI.PS.hlsl", L"ps_6_0", dxc->GetDxcUtils(), dxc->GetDxcCompiler(), dxc->GetIncludeHandler());
	assert(vsBlob != nullptr && psBlob != nullptr);
	if (!vsBlob || !psBlob || !rootSignature_) {
		return;
	}

	D3D12_INPUT_ELEMENT_DESC inputElements[3] = {};
	inputElements[0].SemanticName = "POSITION";
	inputElements[0].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElements[1].SemanticName = "TEXCOORD";
	inputElements[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElements[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElements[2].SemanticName = "COLOR";
	inputElements[2].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElements[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_RASTERIZER_DESC rasterDesc{};
	rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterDesc.DepthClipEnable = TRUE;

	D3D12_DEPTH_STENCIL_DESC depthDesc{};
	depthDesc.DepthEnable = FALSE;
	depthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	// Keep destination alpha (the editor shows the game render target through ImGui::Image)
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = rootSignature_.Get();
	psoDesc.InputLayout = { inputElements, _countof(inputElements) };
	psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
	psoDesc.RasterizerState = rasterDesc;
	psoDesc.DepthStencilState = depthDesc;
	psoDesc.BlendState = blendDesc;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	HRESULT hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso_));
	assert(SUCCEEDED(hr));
	(void)hr;
}

// ---- Font: GDI rasterizes each glyph on first use (no font file to ship) ----

struct UIRenderer::GlyphRasterizer {
	static constexpr int kScratchSize = 128; // one glyph at kFontPixelSize fits easily

	HDC dc = nullptr;
	HBITMAP bitmap = nullptr;
	HGDIOBJ oldBitmap = nullptr;
	void* bits = nullptr;
	HFONT primary = nullptr;  // Segoe UI: Latin, symbols
	HFONT fallback = nullptr; // Japanese UI font for kana / kanji the primary font lacks
	int primaryAscent = 0;
	int fallbackAscent = 0;
	int lineHeight = 0;

	~GlyphRasterizer()
	{
		if (dc && oldBitmap) SelectObject(dc, oldBitmap);
		if (primary) DeleteObject(primary);
		if (fallback) DeleteObject(fallback);
		if (bitmap) DeleteObject(bitmap);
		if (dc) DeleteDC(dc);
	}

	static HFONT MakeFont(const wchar_t* face)
	{
		return CreateFontW(-static_cast<int>(kFontPixelSize), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
	}

	/// True when every UTF-16 unit of `text` has a glyph in `font` (surrogate pairs are left to the fallback)
	bool FontHas(HFONT font, const wchar_t* text, int length) const
	{
		if (!font || length != 1) return false;
		SelectObject(dc, font);
		WORD index = 0xFFFF;
		return GetGlyphIndicesW(dc, text, 1, &index, GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR && index != 0xFFFF;
	}
};

UIRenderer::UIRenderer() = default;
UIRenderer::~UIRenderer() = default;

void UIRenderer::InitializeFont()
{
	auto r = std::make_unique<GlyphRasterizer>();
	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = GlyphRasterizer::kScratchSize;
	bmi.bmiHeader.biHeight = -GlyphRasterizer::kScratchSize; // top-down rows
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	r->dc = CreateCompatibleDC(nullptr);
	r->bitmap = r->dc ? CreateDIBSection(r->dc, &bmi, DIB_RGB_COLORS, &r->bits, nullptr, 0) : nullptr;
	r->primary = GlyphRasterizer::MakeFont(L"Segoe UI");
	if (!r->dc || !r->bitmap || !r->bits || !r->primary) {
		Log::GetInstance()->logOutput("[UIRenderer] Font rasterizer creation failed; text will not be drawn.\n");
		return;
	}
	r->oldBitmap = SelectObject(r->dc, r->bitmap);
	SetBkMode(r->dc, TRANSPARENT);
	SetTextColor(r->dc, RGB(255, 255, 255));

	TEXTMETRICW tm{};
	SelectObject(r->dc, r->primary);
	GetTextMetricsW(r->dc, &tm);
	r->lineHeight = (std::max)(1, static_cast<int>(tm.tmHeight));
	r->primaryAscent = tm.tmAscent;

	// CreateFont silently substitutes a missing face, so confirm the face that was actually selected
	for (const wchar_t* face : { L"Yu Gothic UI", L"Meiryo UI", L"MS UI Gothic" }) {
		HFONT candidate = GlyphRasterizer::MakeFont(face);
		if (!candidate) continue;
		SelectObject(r->dc, candidate);
		wchar_t selected[LF_FACESIZE]{};
		GetTextFaceW(r->dc, LF_FACESIZE, selected);
		if (_wcsicmp(selected, face) == 0) {
			TEXTMETRICW ftm{};
			GetTextMetricsW(r->dc, &ftm);
			r->fallback = candidate;
			r->fallbackAscent = ftm.tmAscent;
			break;
		}
		DeleteObject(candidate);
	}
	if (!r->fallback) {
		Log::GetInstance()->logOutput("[UIRenderer] No Japanese UI font found; Japanese text is drawn as boxes.\n");
	}

	fontLineHeight_ = static_cast<float>(r->lineHeight);
	rasterizer_ = std::move(r);

	pages_.clear();
	glyphs_.clear();
	penX_ = 0;
	penY_ = 0;
	// ASCII and the box glyph up front, so the common case never uploads during a frame
	GetGlyph(0x25A1);
	for (uint32_t c = 32; c < 127; ++c) GetGlyph(c);
	UploadFontPages();
}

void UIRenderer::FinalizeFont()
{
	rasterizer_.reset();
	glyphs_.clear();
	pages_.clear();
}

uint32_t UIRenderer::DecodeUtf8(const char*& p, const char* end)
{
	const auto byte = [](const char* q) { return static_cast<uint8_t>(*q); };
	const uint8_t lead = byte(p++);
	if (lead < 0x80) return lead;
	int extra = 0;
	uint32_t cp = 0;
	if ((lead & 0xE0) == 0xC0) { extra = 1; cp = lead & 0x1F; }
	else if ((lead & 0xF0) == 0xE0) { extra = 2; cp = lead & 0x0F; }
	else if ((lead & 0xF8) == 0xF0) { extra = 3; cp = lead & 0x07; }
	else return 0xFFFD;
	for (int i = 0; i < extra; ++i) {
		if (p >= end || (byte(p) & 0xC0) != 0x80) return 0xFFFD; // truncated sequence: the next byte starts over
		cp = (cp << 6) | (byte(p++) & 0x3F);
	}
	// Overlong forms, surrogates and out-of-range values are invalid
	static constexpr uint32_t kMinForLength[4] = { 0, 0x80, 0x800, 0x10000 };
	if (cp < kMinForLength[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0xFFFD;
	return cp;
}

bool UIRenderer::RasterizeGlyph(uint32_t codepoint, Glyph& out) const
{
	GlyphRasterizer* r = rasterizer_.get();
	if (!r) return false;

	wchar_t text[2]{};
	int length = 1;
	if (codepoint >= 0x10000) {
		const uint32_t v = codepoint - 0x10000;
		text[0] = static_cast<wchar_t>(0xD800 + (v >> 10));
		text[1] = static_cast<wchar_t>(0xDC00 + (v & 0x3FF));
		length = 2;
	} else {
		text[0] = static_cast<wchar_t>(codepoint);
	}

	HFONT font = nullptr;
	int baselineShift = 0;
	if (r->FontHas(r->primary, text, length)) {
		font = r->primary;
	} else if (r->fallback && (length == 2 || r->FontHas(r->fallback, text, length))) {
		font = r->fallback;
		baselineShift = r->primaryAscent - r->fallbackAscent; // share the primary font's baseline
	} else {
		return false;
	}

	SelectObject(r->dc, font);
	SIZE size{};
	GetTextExtentPoint32W(r->dc, text, length, &size);
	const int w = std::clamp(static_cast<int>(size.cx), 0, GlyphRasterizer::kScratchSize);
	const int h = (std::min)(r->lineHeight, GlyphRasterizer::kScratchSize);
	constexpr int kPad = 2;

	// Next free cell: rows left to right, a new page when this one is full
	if (pages_.empty() || penX_ + w + kPad > kPageSize) {
		penX_ = kPad;
		penY_ += pages_.empty() ? kPad : r->lineHeight + kPad;
	}
	if (pages_.empty() || penY_ + h + kPad > kPageSize) {
		if (pages_.size() >= kMaxFontPages) return false;
		pages_.push_back({ std::vector<uint8_t>(static_cast<size_t>(kPageSize) * kPageSize * 4, 0), 0, true });
		// White RGB everywhere; coverage goes to alpha
		std::vector<uint8_t>& px = pages_.back().rgba;
		for (size_t i = 0; i < px.size(); i += 4) { px[i] = 255; px[i + 1] = 255; px[i + 2] = 255; }
		penX_ = kPad;
		penY_ = kPad;
	}

	std::memset(r->bits, 0, static_cast<size_t>(GlyphRasterizer::kScratchSize) * GlyphRasterizer::kScratchSize * 4);
	TextOutW(r->dc, 0, baselineShift, text, length);
	GdiFlush();

	FontPage& page = pages_.back();
	const uint8_t* src = static_cast<const uint8_t*>(r->bits);
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const uint8_t* s = src + (static_cast<size_t>(y) * GlyphRasterizer::kScratchSize + x) * 4;
			const uint8_t coverage = (std::max)({ s[0], s[1], s[2] }); // GDI writes coverage into RGB
			page.rgba[(static_cast<size_t>(penY_ + y) * kPageSize + penX_ + x) * 4 + 3] = coverage;
		}
	}
	page.dirty = true;

	out.u0 = static_cast<float>(penX_) / kPageSize;
	out.v0 = static_cast<float>(penY_) / kPageSize;
	out.u1 = static_cast<float>(penX_ + w) / kPageSize;
	out.v1 = static_cast<float>(penY_ + h) / kPageSize;
	out.width = static_cast<float>(w);
	out.advance = static_cast<float>(size.cx);
	out.page = static_cast<uint32_t>(pages_.size() - 1);
	out.missing = false;
	penX_ += w + kPad;
	return true;
}

const UIRenderer::Glyph& UIRenderer::GetGlyph(uint32_t codepoint) const
{
	if (auto it = glyphs_.find(codepoint); it != glyphs_.end()) return it->second;
	Glyph glyph;
	if (!RasterizeGlyph(codepoint, glyph)) {
		// No font has it (or the atlas is full): reuse the box glyph's cell
		static const Glyph kNone{};
		const auto box = glyphs_.find(0x25A1);
		glyph = (box != glyphs_.end() && codepoint != 0x25A1) ? box->second : kNone;
		glyph.missing = true;
	}
	return glyphs_.emplace(codepoint, glyph).first->second;
}

bool UIRenderer::HasGlyph(uint32_t codepoint) const
{
	return !GetGlyph(codepoint).missing;
}

void UIRenderer::UploadFontPages()
{
	if (!dxCommon_ || dxCommon_->IsDeviceLost()) return;
	TextureManager* textures = TextureManager::GetInstance();
	for (size_t i = 0; i < pages_.size(); ++i) {
		FontPage& page = pages_[i];
		if (!page.dirty) continue;
		if (page.texture == 0) {
			page.texture = textures->CreateTextureFromRGBA("UIRenderer/FontPage" + std::to_string(i), page.rgba.data(), kPageSize, kPageSize);
		} else {
			textures->UpdateTextureFromRGBA(page.texture, page.rgba.data(), kPageSize, kPageSize);
		}
		page.dirty = false;
	}
}

void UIRenderer::Clear()
{
	if (inCanvasLayer_) EndCanvasLayer();
	vertices_.clear();
	batches_.clear();
}

void UIRenderer::BeginCanvasLayer()
{
	if (inCanvasLayer_) return;
	vertices_.swap(canvasVertices_);
	batches_.swap(canvasBatches_);
	vertices_.clear();
	batches_.clear();
	inCanvasLayer_ = true;
}

void UIRenderer::EndCanvasLayer()
{
	if (!inCanvasLayer_) return;
	vertices_.swap(canvasVertices_);
	batches_.swap(canvasBatches_);
	inCanvasLayer_ = false;
}

void UIRenderer::PushQuad(uint32_t texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1, const Vector4& color)
{
	if (vertices_.size() + 6 > kMaxVertices || w == 0.0f || h == 0.0f) return;

	if (batches_.empty() || batches_.back().texture != texture) {
		batches_.push_back({ texture, static_cast<uint32_t>(vertices_.size()), 0 });
	}

	// Canvas pixels -> NDC (y down in the canvas, up in NDC)
	const float l = x / kCanvasWidth * 2.0f - 1.0f;
	const float r = (x + w) / kCanvasWidth * 2.0f - 1.0f;
	const float t = 1.0f - y / kCanvasHeight * 2.0f;
	const float b = 1.0f - (y + h) / kCanvasHeight * 2.0f;

	vertices_.push_back({ l, t, u0, v0, color });
	vertices_.push_back({ r, t, u1, v0, color });
	vertices_.push_back({ l, b, u0, v1, color });
	vertices_.push_back({ l, b, u0, v1, color });
	vertices_.push_back({ r, t, u1, v0, color });
	vertices_.push_back({ r, b, u1, v1, color });
	batches_.back().vertexCount += 6;
}

void UIRenderer::DrawRect(float x, float y, float w, float h, const Vector4& color)
{
	PushQuad(whiteTexture_, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color);
}

void UIRenderer::DrawImage(uint32_t textureHandle, float x, float y, float w, float h, const Vector4& color)
{
	PushQuad(textureHandle, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color);
}

float UIRenderer::MeasureLine(const char* begin, const char* end, float scale) const
{
	float width = 0.0f;
	for (const char* p = begin; p < end;) {
		const uint32_t c = DecodeUtf8(p, end);
		if (c == '\r') continue;
		width += GetGlyph(c).advance * scale;
	}
	return width;
}

float UIRenderer::MeasureText(const char* text, float size) const
{
	if (!text) return 0.0f;
	const float scale = size / fontLineHeight_;
	float widest = 0.0f;
	const char* lineStart = text;
	for (const char* p = text;; ++p) {
		if (*p == '\n' || *p == '\0') {
			widest = (std::max)(widest, MeasureLine(lineStart, p, scale));
			if (*p == '\0') break;
			lineStart = p + 1;
		}
	}
	return widest;
}

void UIRenderer::DrawString(const char* text, float x, float y, float size, const Vector4& color, Align align, float widthScale)
{
	if (!text || pages_.empty() || size <= 0.0f) return;
	const float scale = size / fontLineHeight_;
	const float scaleX = scale * widthScale;
	float lineY = y;
	const char* lineStart = text;
	for (const char* p = text;; ++p) {
		if (*p != '\n' && *p != '\0') continue;

		float penX = x;
		if (align != Align::Left) {
			const float lineW = MeasureLine(lineStart, p, scaleX);
			penX -= (align == Align::Center) ? lineW * 0.5f : lineW;
		}
		for (const char* q = lineStart; q < p;) {
			const uint32_t c = DecodeUtf8(q, p);
			if (c == '\r') continue;
			const Glyph& g = GetGlyph(c);
			if (c != ' ' && c != 0x3000) { // ASCII and ideographic spaces draw nothing
				PushQuad(kFontPageTag | g.page, penX, lineY, g.width * scaleX, size, g.u0, g.v0, g.u1, g.v1, color);
			}
			penX += g.advance * scaleX;
		}

		if (*p == '\0') break;
		lineStart = p + 1;
		lineY += size;
	}
}

void UIRenderer::Render(ID3D12GraphicsCommandList* cmdList, bool immediate)
{
	EndCanvasLayer();
	const size_t immediateCount = immediate ? vertices_.size() : 0;
	const size_t total = canvasVertices_.size() + immediateCount;
	if (total == 0 || !pso_ || !rootSignature_ || !dxCommon_ || dxCommon_->IsDeviceLost() || !cmdList) {
		return;
	}
	// Glyphs first used this frame go to the GPU before anything samples them
	UploadFontPages();

	// Per-frame linear upload memory: rendering the same commands into several targets is safe
	const size_t bytes = sizeof(Vertex) * total;
	DirectXCommon::UploadAllocation alloc = dxCommon_->AllocateUpload(bytes, 16);
	if (!alloc.cpu) return;
	auto* dst = static_cast<Vertex*>(alloc.cpu);
	std::memcpy(dst, canvasVertices_.data(), sizeof(Vertex) * canvasVertices_.size());
	if (immediateCount > 0) std::memcpy(dst + canvasVertices_.size(), vertices_.data(), sizeof(Vertex) * immediateCount);

	D3D12_VERTEX_BUFFER_VIEW vbView{};
	vbView.BufferLocation = alloc.gpu;
	vbView.SizeInBytes = static_cast<UINT>(bytes);
	vbView.StrideInBytes = sizeof(Vertex);

	TextureManager* textures = TextureManager::GetInstance();
	textures->SetDescriptorHeaps(cmdList);
	cmdList->SetGraphicsRootSignature(rootSignature_.Get());
	cmdList->SetPipelineState(pso_.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->IASetVertexBuffers(0, 1, &vbView);
	const auto drawBatches = [&](const std::vector<Batch>& batches, uint32_t vertexBase) {
		for (const Batch& batch : batches) {
			if (batch.vertexCount == 0) continue;
			uint32_t texture = batch.texture;
			if (texture & kFontPageTag) {
				const size_t page = texture & ~kFontPageTag;
				texture = (page < pages_.size()) ? pages_[page].texture : 0;
				if (texture == 0) continue;
			}
			cmdList->SetGraphicsRootDescriptorTable(0, textures->GetGPUHandle(texture));
			cmdList->DrawInstanced(batch.vertexCount, 1, vertexBase + batch.firstVertex, 0);
			Profiler::GetInstance()->CountDrawCall();
		}
	};
	drawBatches(canvasBatches_, 0);
	if (immediateCount > 0) drawBatches(batches_, static_cast<uint32_t>(canvasVertices_.size()));
}
