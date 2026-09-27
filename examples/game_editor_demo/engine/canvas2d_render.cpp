// Définitions de canvas2d_render.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "canvas2d_render.hpp"

namespace game_editor {

// ── Canvas2DRenderer ─────────────────────────────────────────────────────────

void Canvas2DRenderer::SetFontPath(String path) {
	m_fontPath = std::move(path);
	m_fonts.clear();
	m_texts.clear();
}

math::FVector2 Canvas2DRenderer::MeasureText(const String &text, float fontSize) {
	sdl3::Font *font = FontFor(int(std::lround(fontSize)));
	if (!font || text.IsEmpty())
		return EstimateText(text, fontSize);
	Option<sdl3::Point> size = font->Measure(text);
	if (size.IsNone())
		return EstimateText(text, fontSize);
	const float scale = fontSize / float(std::max(1L, std::lround(fontSize)));
	return {float(size.Unwrap().x) * scale, float(size.Unwrap().y) * scale};
}

TextMeasure Canvas2DRenderer::Measure() {
	return [this](const String &text, float fontSize) { return MeasureText(text, fontSize); };
}

void Canvas2DRenderer::FillRect(sdl3::Renderer &ren, sdl3::FRect rect, sdl3::Color color) {
	ren.SetDrawColor(ToF(color));
	ren.FillRect(rect);
}

void Canvas2DRenderer::Draw(sdl3::Renderer &ren, const View2D &view, const std::vector<DrawItem2D> &items) {
	++m_frame;
	ren.SetBlendMode(sdl3::BlendMode::BLEND);
	for (const DrawItem2D &item : items)
		DrawItem(ren, view.For(item.space) * item.transform, item.item);
	if (m_frame % 120 == 0)
		EvictStaleTexts();
}

void Canvas2DRenderer::DrawItem(sdl3::Renderer &ren, const Transform2D &toPixels, const CanvasItemDesc &item) {
	switch (item.kind) {
		case CanvasItemKind::RECT: {
			const Bounds2D b = LocalBounds(item);
			DrawOutlinedPolygon(ren, toPixels, item, {b.min, {b.max.x, b.min.y}, b.max, {b.min.x, b.max.y}}, true);
			break;
		}
		case CanvasItemKind::CIRCLE:
			DrawOutlinedPolygon(ren, toPixels, item, EllipsePoints(item, toPixels), true);
			break;
		case CanvasItemKind::POLYGON:
			DrawOutlinedPolygon(ren, toPixels, item, item.points, false);
			break;
		case CanvasItemKind::LINE: {
			std::vector<sdl3::FPoint> points;
			for (const math::FVector2 &p : item.points)
				points.push_back(ToPoint(toPixels.Apply(p)));
			const float width = std::max(item.outline, 1.f) * toPixels.MeanScale();
			Stroke(ren, points, false, width, ToF(item.color));
			break;
		}
		case CanvasItemKind::SPRITE:
			DrawSprite(ren, toPixels, item);
			break;
		case CanvasItemKind::TEXT:
			DrawText(ren, toPixels, item);
			break;
	}
}

void Canvas2DRenderer::Stroke(sdl3::Renderer &ren, const std::vector<sdl3::FPoint> &points, bool closed, float width,
		sdl3::FColor color) {
	if (points.size() < 2 || width <= 0.f)
		return;
	std::vector<sdl3::Vertex> vertices;
	std::vector<int> indices;
	const size_t segments = closed ? points.size() : points.size() - 1;
	const float half = width * 0.5f;
	for (size_t i = 0; i < segments; ++i) {
		const sdl3::FPoint a = points[i], b = points[(i + 1) % points.size()];
		float dx = b.x - a.x, dy = b.y - a.y;
		const float len = std::sqrt(dx * dx + dy * dy);
		if (len < 1e-4f)
			continue;
		const float nx = -dy / len * half, ny = dx / len * half;
		const int base = int(vertices.size());
		vertices.push_back(Vertex({a.x + nx, a.y + ny}, color));
		vertices.push_back(Vertex({b.x + nx, b.y + ny}, color));
		vertices.push_back(Vertex({b.x - nx, b.y - ny}, color));
		vertices.push_back(Vertex({a.x - nx, a.y - ny}, color));
		indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
	}
	ren.RenderGeometry(vertices, indices);
	if (width > 2.f)
		for (size_t i = closed ? 0 : 1; i < (closed ? points.size() : points.size() - 1); ++i)
			FillDisc(ren, points[i], half, color);
}

void Canvas2DRenderer::FillDisc(sdl3::Renderer &ren, sdl3::FPoint center, float radius, sdl3::FColor color) {
	const int n = std::clamp(int(radius), 8, 48);
	std::vector<sdl3::Vertex> vertices{Vertex(center, color)};
	std::vector<int> indices;
	for (int i = 0; i <= n; ++i) {
		const float a = float(i) / float(n) * 6.2831853f;
		vertices.push_back(Vertex({center.x + std::cos(a) * radius, center.y + std::sin(a) * radius}, color));
		if (i > 0)
			indices.insert(indices.end(), {0, i, i + 1});
	}
	ren.RenderGeometry(vertices, indices);
}

sdl3::FColor Canvas2DRenderer::ToF(sdl3::Color c) noexcept {
	return sdl3::FColor{float(c.r) / 255.f, float(c.g) / 255.f, float(c.b) / 255.f, float(c.a) / 255.f};
}

sdl3::Vertex Canvas2DRenderer::Vertex(sdl3::FPoint p, sdl3::FColor color, sdl3::FPoint uv) {
	return sdl3::Vertex(p, color, uv);
}

std::vector<math::FVector2> Canvas2DRenderer::EllipsePoints(const CanvasItemDesc &item, const Transform2D &t) {
	const math::FVector2 center = item.centered ? math::FVector2{} : math::FVector2{item.size.x * 0.5f, item.size.y * 0.5f};
	const float rx = item.size.x * 0.5f, ry = item.size.y * 0.5f;
	const int n = std::clamp(int(std::max(rx, ry) * t.MeanScale() * 0.5f), 16, 128);
	std::vector<math::FVector2> points;
	for (int i = 0; i < n; ++i) {
		const float a = float(i) / float(n) * 6.2831853f;
		points.push_back({center.x + std::cos(a) * rx, center.y + std::sin(a) * ry});
	}
	return points;
}

void Canvas2DRenderer::DrawOutlinedPolygon(sdl3::Renderer &ren, const Transform2D &t, const CanvasItemDesc &item,
		const std::vector<math::FVector2> &local, bool convex) {
	if (local.size() < 3)
		return;
	std::vector<sdl3::FPoint> screen;
	for (const math::FVector2 &p : local)
		screen.push_back(ToPoint(t.Apply(p)));
	const sdl3::FColor fill = ToF(item.color);
	if (item.filled) {
		std::vector<sdl3::Vertex> vertices;
		std::vector<int> indices;
		if (convex) {
			for (const sdl3::FPoint &p : screen)
				vertices.push_back(Vertex(p, fill));
			for (int i = 1; i + 1 < int(screen.size()); ++i)
				indices.insert(indices.end(), {0, i, i + 1});
		} else {
			auto [merged, triangles] = render3d::Shape2D(local).Triangulate();
			for (const math::FVector2 &p : merged)
				vertices.push_back(Vertex(ToPoint(t.Apply(p)), fill));
			for (uint32_t index : triangles)
				indices.push_back(int(index));
		}
		ren.RenderGeometry(vertices, indices);
	}
	const float outline = item.filled ? item.outline : std::max(item.outline, 1.f);
	if (outline > 0.f)
		Stroke(ren, screen, true, outline * t.MeanScale(),
			   item.filled ? ToF(item.outlineColor) : fill);
}

void Canvas2DRenderer::TexturedQuad(sdl3::Renderer &ren, const sdl3::Texture &texture, const Transform2D &t, Bounds2D box,
		sdl3::FColor tint, bool flipH, bool flipV) {
	const float u0 = flipH ? 1.f : 0.f, u1 = flipH ? 0.f : 1.f;
	const float v0 = flipV ? 1.f : 0.f, v1 = flipV ? 0.f : 1.f;
	const sdl3::Vertex vertices[4] = {
		Vertex(ToPoint(t.Apply(box.min)), tint, {u0, v0}),
		Vertex(ToPoint(t.Apply({box.max.x, box.min.y})), tint, {u1, v0}),
		Vertex(ToPoint(t.Apply(box.max)), tint, {u1, v1}),
		Vertex(ToPoint(t.Apply({box.min.x, box.max.y})), tint, {u0, v1}),
	};
	const int indices[6] = {0, 1, 2, 0, 2, 3};
	ren.RenderGeometry(texture, vertices, indices);
}

void Canvas2DRenderer::DrawSprite(sdl3::Renderer &ren, const Transform2D &t, const CanvasItemDesc &item) {
	const Bounds2D box = LocalBounds(item);
	if (const sdl3::Texture *texture = TextureFor(ren, item.texture)) {
		TexturedQuad(ren, *texture, t, box, ToF(item.color), item.flipH, item.flipV);
		return;
	}
	// Image absente : mire magenta barrée, impossible à confondre.
	CanvasItemDesc placeholder = item;
	placeholder.kind = CanvasItemKind::RECT;
	placeholder.color = sdl3::Color{200, 40, 160, 150};
	placeholder.outline = 2.f;
	placeholder.outlineColor = sdl3::Color{255, 90, 210, 255};
	DrawOutlinedPolygon(ren, t, placeholder, {box.min, {box.max.x, box.min.y}, box.max, {box.min.x, box.max.y}},
						true);
	const float width = 2.f;
	Stroke(ren, {ToPoint(t.Apply(box.min)), ToPoint(t.Apply(box.max))}, false, width, ToF(placeholder.outlineColor));
	Stroke(ren, {ToPoint(t.Apply({box.max.x, box.min.y})), ToPoint(t.Apply({box.min.x, box.max.y}))}, false, width,
		   ToF(placeholder.outlineColor));
}

void Canvas2DRenderer::DrawText(sdl3::Renderer &ren, const Transform2D &t, const CanvasItemDesc &item) {
	if (item.text.IsEmpty())
		return;
	// Rastérisé à la taille qu'il aura à l'écran, pour rester net.
	const int pixels = std::clamp(int(std::lround(item.fontSize * t.MeanScale())), 4, 400);
	CachedText *text = TextFor(ren, item.text, pixels);
	if (!text) {
		// Sans police : un simple cadre à la place du texte.
		CanvasItemDesc frame = item;
		frame.filled = false;
		frame.outline = 1.f;
		const Bounds2D b = LocalBounds(item);
		DrawOutlinedPolygon(ren, t, frame, {b.min, {b.max.x, b.min.y}, b.max, {b.min.x, b.max.y}}, true);
		return;
	}
	text->lastUse = m_frame;
	// Boîte locale : taille de la texture ramenée à la taille de police.
	const float scale = item.fontSize / float(pixels);
	const math::FVector2 size{text->size.x * scale, text->size.y * scale};
	const float left = item.align == TextAlign2D::LEFT	 ? 0.f
					   : item.align == TextAlign2D::RIGHT ? -size.x
														  : -size.x * 0.5f;
	const float top = item.centered ? -size.y * 0.5f : 0.f;
	TexturedQuad(ren, text->texture, t, Bounds2D{{left, top}, {left + size.x, top + size.y}}, ToF(item.color),
				 false, false);
}

const sdl3::Texture * Canvas2DRenderer::TextureFor(sdl3::Renderer &ren, const String &path) {
	if (path.IsEmpty())
		return nullptr;
	const std::string key(path.CStr());
	auto found = m_textures.find(key);
	if (found == m_textures.end()) {
		const String file = m_resolver ? m_resolver(path) : path;
		auto loaded = sdl3::ImgLoadTexture(ren, file);
		Option<sdl3::Texture> texture = NONE;
		if (loaded.IsOk()) {
			loaded.Value().SetBlendMode(sdl3::BlendMode::BLEND);
			texture = Some(std::move(loaded.Value()));
		}
		found = m_textures.emplace(key, std::move(texture)).first;
	}
	return found->second.IsSome() ? &found->second.Value() : nullptr;
}

sdl3::Font * Canvas2DRenderer::FontFor(int pixels) {
	pixels = std::clamp(pixels, 4, 400);
	auto found = m_fonts.find(pixels);
	if (found != m_fonts.end())
		return found->second.IsSome() ? &found->second.Value() : nullptr;
	Option<sdl3::Font> font = NONE;
	if (!m_fontPath.IsEmpty())
		if (auto opened = sdl3::Font::Open(m_fontPath, float(pixels)); opened.IsOk())
			font = Some(std::move(opened.Value()));
	if (font.IsNone())
		if (auto local = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, float(pixels)); local.IsOk())
			font = Some(std::move(local.Value()));
	auto inserted = m_fonts.emplace(pixels, std::move(font)).first;
	return inserted->second.IsSome() ? &inserted->second.Value() : nullptr;
}

Canvas2DRenderer::CachedText * Canvas2DRenderer::TextFor(sdl3::Renderer &ren, const String &text, int pixels) {
	const std::string key = std::to_string(pixels) + '\x1f' + text.CStr();
	auto found = m_texts.find(key);
	if (found != m_texts.end())
		return &found->second;
	sdl3::Font *font = FontFor(pixels);
	if (!font)
		return nullptr;
	auto surface = font->RenderBlended(text, sdl3::Color{255, 255, 255, 255});
	if (surface.IsError())
		return nullptr;
	auto texture = sdl3::Texture::CreateFromSurface(ren, surface.Value());
	if (texture.IsError())
		return nullptr;
	texture.Value().SetBlendMode(sdl3::BlendMode::BLEND);
	const sdl3::FPoint size = texture.Value().GetSize();
	auto inserted = m_texts.emplace(key, CachedText{std::move(texture.Value()), size, m_frame});
	return &inserted.first->second;
}

void Canvas2DRenderer::EvictStaleTexts() {
	for (auto it = m_texts.begin(); it != m_texts.end();)
		it = m_frame - it->second.lastUse > 300 ? m_texts.erase(it) : std::next(it);
}

} // namespace game_editor
