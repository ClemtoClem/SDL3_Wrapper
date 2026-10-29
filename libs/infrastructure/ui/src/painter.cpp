// Définitions de ui/painter.hpp
#include <ui/painter.hpp>

#include <cmath>

namespace ui {

namespace {

constexpr float DEG = math::PI / 180.f;

bool Empty(const sdl3::FRect &r) noexcept { return r.w <= 0.f || r.h <= 0.f; }

sdl3::Vertex V(float x, float y, Color c) { return sdl3::Vertex{sdl3::FPoint{x, y}, c, sdl3::FPoint{0.f, 0.f}}; }

/// Contour d'un rectangle arrondi (sens horaire depuis le haut-gauche).
std::vector<sdl3::FPoint> RoundedOutline(const sdl3::FRect &r, const math::Corners &c) {
	std::vector<sdl3::FPoint> pts;
	auto corner = [&](float cx, float cy, float radius, float startDeg) {
		if (radius <= 0.f) {
			pts.push_back({cx + std::cos(startDeg * DEG + 0.25f * math::PI) * 0.f, cy});
			return;
		}
		const int n = math::Max(3, int(std::ceil(radius * 0.6f)));
		for (int i = 0; i <= n; ++i) {
			const float a = (startDeg + 90.f * float(i) / float(n)) * DEG;
			pts.push_back({cx + std::cos(a) * radius, cy + std::sin(a) * radius});
		}
	};
	// haut-gauche (180→270), haut-droit (270→360), bas-droit (0→90), bas-gauche (90→180)
	if (c.tl > 0.f)
		corner(r.x + c.tl, r.y + c.tl, c.tl, 180.f);
	else
		pts.push_back({r.x, r.y});
	if (c.tr > 0.f)
		corner(r.x + r.w - c.tr, r.y + c.tr, c.tr, 270.f);
	else
		pts.push_back({r.x + r.w, r.y});
	if (c.br > 0.f)
		corner(r.x + r.w - c.br, r.y + r.h - c.br, c.br, 0.f);
	else
		pts.push_back({r.x + r.w, r.y + r.h});
	if (c.bl > 0.f)
		corner(r.x + c.bl, r.y + r.h - c.bl, c.bl, 90.f);
	else
		pts.push_back({r.x, r.y + r.h});
	return pts;
}

bool HasRadius(const math::Corners &c) noexcept { return c.tl > 0.f || c.tr > 0.f || c.bl > 0.f || c.br > 0.f; }

} // namespace

// ============================================================================
// Fonctions libres
// ============================================================================

sdl3::Rect ToClipRect(const sdl3::FRect &r) noexcept {
	const int x0 = int(std::floor(r.x));
	const int y0 = int(std::floor(r.y));
	const int x1 = int(std::ceil(r.x + r.w));
	const int y1 = int(std::ceil(r.y + r.h));
	return sdl3::Rect{x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0};
}

math::Corners ClampRadius(const sdl3::FRect &r, const math::Corners &c) noexcept {
	const float m = math::Max(0.f, math::Min(r.w, r.h) * 0.5f);
	return math::Corners(math::Clamp(c.tl, 0.f, m), math::Clamp(c.tr, 0.f, m), math::Clamp(c.bl, 0.f, m),
						 math::Clamp(c.br, 0.f, m));
}

sdl3::FRect FitRect(sdl3::FPoint size, const sdl3::FRect &box, ObjectFit fit) noexcept {
	if (size.x <= 0.f || size.y <= 0.f || fit == ObjectFit::Fill)
		return box;
	float scale = 1.f;
	switch (fit) {
	case ObjectFit::Contain:
		scale = math::Min(box.w / size.x, box.h / size.y);
		break;
	case ObjectFit::Cover:
		scale = math::Max(box.w / size.x, box.h / size.y);
		break;
	case ObjectFit::ScaleDown:
		scale = math::Min(1.f, math::Min(box.w / size.x, box.h / size.y));
		break;
	case ObjectFit::None:
	case ObjectFit::Fill:
		break;
	}
	const float w = size.x * scale, h = size.y * scale;
	return {box.x + (box.w - w) * 0.5f, box.y + (box.h - h) * 0.5f, w, h};
}

// ============================================================================
// Textures
// ============================================================================

void Textures::Add(const String &key, sdl3::Texture texture) {
	m_failed.erase(key);
	m_textures.insert_or_assign(key, std::move(texture));
}

sdl3::Texture *Textures::Get(const String &key) {
	if (key.IsEmpty())
		return nullptr;
	if (auto it = m_textures.find(key); it != m_textures.end())
		return &it->second;
	if (!m_renderer || m_failed.count(key))
		return nullptr;
	auto tex = sdl3::Texture::CreateFromFile(*m_renderer, key);
	if (!tex) {
		m_failed[key] = true;
		return nullptr;
	}
	auto it = m_textures.insert_or_assign(key, std::move(tex.Value())).first;
	return &it->second;
}

sdl3::FPoint Textures::SizeOf(const String &key) {
	if (sdl3::Texture *t = Get(key))
		return t->GetSize();
	return {0.f, 0.f};
}

// ============================================================================
// Painter
// ============================================================================

void Painter::SetRenderer(sdl3::Renderer *renderer) {
	m_texts.clear();
	m_engine = NONE;
	m_renderer = renderer;
	if (renderer)
		if (auto engine = sdl3::TextEngine::Create(*renderer); engine)
			m_engine = Some(std::move(engine.Value()));
}

void Painter::BeginFrame(sdl3::FRect viewport) {
	++m_frame;
	m_viewport = viewport;
	m_clips.clear();
	m_clip = viewport;
	m_opacity = 1.f;
	if (m_renderer) {
		(void)m_renderer->SetBlendMode(sdl3::BlendMode::BLEND);
		ApplyClip();
	}
}

void Painter::EndFrame() {
	if (m_renderer)
		(void)m_renderer->ClearClipRect();
	// Textes inutilisés depuis 300 images : libérés.
	if (m_frame % 60 == 0)
		std::erase_if(m_texts, [&](const auto &kv) { return kv.second.lastUse + 300 < m_frame; });
}

void Painter::ApplyClip() {
	if (!m_renderer)
		return;
	if (Empty(m_clip))
		return; // rien ne sera dessiné (cf. IsClippedOut) : SDL désactiverait le clip
	(void)m_renderer->SetClipRect(ToClipRect(m_clip));
}

void Painter::PushClip(const sdl3::FRect &r) {
	m_clips.push_back(m_clip);
	m_clip = m_clip.Intersection(r);
	ApplyClip();
}

void Painter::PopClip() {
	if (m_clips.empty())
		return;
	m_clip = m_clips.back();
	m_clips.pop_back();
	ApplyClip();
}

void Painter::ResetClip(const sdl3::FRect &r) {
	m_clips.clear();
	m_clip = r;
	ApplyClip();
}

bool Painter::IsClippedOut(const sdl3::FRect &r) const noexcept {
	return Empty(m_clip) || !m_clip.Intersects(r);
}

void Painter::FillRect(const sdl3::FRect &r, Color c) {
	if (!m_renderer || c.a <= 0.f || Empty(r) || IsClippedOut(r))
		return;
	(void)m_renderer->SetDrawColor(Fade(c));
	(void)m_renderer->FillRect(r);
}

void Painter::FillRoundedRect(const sdl3::FRect &r, const math::Corners &radius, Color c) {
	if (!HasRadius(radius)) {
		FillRect(r, c);
		return;
	}
	if (!m_renderer || c.a <= 0.f || Empty(r) || IsClippedOut(r))
		return;
	(void)m_renderer->SetDrawColor(Fade(c));
	(void)m_renderer->FillRoundedRect(r, ClampRadius(r, radius));
}

void Painter::StrokeRoundedRect(const sdl3::FRect &r, const math::Corners &radius, const math::Sides &width, Color c) {
	if (!m_renderer || c.a <= 0.f || Empty(r) || IsClippedOut(r))
		return;
	if (!HasRadius(radius)) {
		FillRect({r.x, r.y, r.w, width.top}, c);
		FillRect({r.x, r.y + r.h - width.bottom, r.w, width.bottom}, c);
		FillRect({r.x, r.y + width.top, width.left, r.h - width.top - width.bottom}, c);
		FillRect({r.x + r.w - width.right, r.y + width.top, width.right, r.h - width.top - width.bottom}, c);
		return;
	}
	// Anneaux concentriques d'un pixel, rayons décroissants.
	const float w = math::Max(math::Max(width.left, width.right), math::Max(width.top, width.bottom));
	const int rings = math::Max(1, int(std::ceil(w)));
	(void)m_renderer->SetDrawColor(Fade(c));
	const math::Corners base = ClampRadius(r, radius);
	for (int i = 0; i < rings; ++i) {
		const float k = float(i);
		const sdl3::FRect ring{r.x + k, r.y + k, r.w - 2.f * k, r.h - 2.f * k};
		if (Empty(ring))
			break;
		const math::Corners rc(math::Max(0.f, base.tl - k), math::Max(0.f, base.tr - k), math::Max(0.f, base.bl - k),
							   math::Max(0.f, base.br - k));
		(void)m_renderer->DrawRoundedRect(ring, rc);
	}
}

void Painter::Triangles(std::span<const sdl3::Vertex> vertices) {
	if (!m_renderer || vertices.empty() || Empty(m_clip))
		return;
	if (m_opacity >= 1.f) {
		(void)m_renderer->RenderGeometry(vertices);
		return;
	}
	std::vector<sdl3::Vertex> faded(vertices.begin(), vertices.end());
	for (sdl3::Vertex &v : faded)
		v.color.a *= m_opacity;
	(void)m_renderer->RenderGeometry(faded);
}

void Painter::VGradient(const sdl3::FRect &r, Color top, Color bottom) { QuadGradient(r, top, top, bottom, bottom); }

void Painter::HGradient(const sdl3::FRect &r, Color left, Color right) { QuadGradient(r, left, right, left, right); }

void Painter::QuadGradient(const sdl3::FRect &r, Color tl, Color tr, Color bl, Color br) {
	if (Empty(r) || IsClippedOut(r))
		return;
	const sdl3::Vertex v[6] = {V(r.x, r.y, tl),			  V(r.x + r.w, r.y, tr),	   V(r.x, r.y + r.h, bl),
							   V(r.x + r.w, r.y, tr), V(r.x + r.w, r.y + r.h, br), V(r.x, r.y + r.h, bl)};
	Triangles(v);
}

void Painter::HueBar(const sdl3::FRect &r, bool vertical) {
	constexpr int K_STOPS = 6;
	for (int i = 0; i < K_STOPS; ++i) {
		const Color a = HsvToColor(float(i) * 60.f, 1.f, 1.f);
		const Color b = HsvToColor(float(i + 1) * 60.f, 1.f, 1.f);
		if (vertical) {
			const float segH = r.h / float(K_STOPS);
			VGradient({r.x, r.y + float(i) * segH, r.w, segH + 0.5f}, a, b);
		} else {
			const float segW = r.w / float(K_STOPS);
			HGradient({r.x + float(i) * segW, r.y, segW + 0.5f, r.h}, a, b);
		}
	}
}

void Painter::Checkerboard(const sdl3::FRect &r, float cell) {
	FillRect(r, Rgba(200, 200, 200));
	const int cols = int(std::ceil(r.w / cell)), rows = int(std::ceil(r.h / cell));
	for (int row = 0; row < rows; ++row)
		for (int col = 0; col < cols; ++col) {
			if ((row + col) % 2 != 0)
				continue;
			const sdl3::FRect c =
				sdl3::FRect{r.x + float(col) * cell, r.y + float(row) * cell, cell, cell}.Intersection(r);
			FillRect(c, Rgba(150, 150, 150));
		}
}

void Painter::FillPaint(const sdl3::FRect &r, const Paint &paint, const math::Corners &radius, ObjectFit fit) {
	if (Empty(r) || IsClippedOut(r))
		return;
	switch (paint.kind) {
	case PaintKind::None:
		return;
	case PaintKind::Hue:
		HueBar(r, std::fabs(paint.angle - 180.f) < 45.f || std::fabs(paint.angle) < 45.f);
		return;
	case PaintKind::Checker:
		Checkerboard(r);
		return;
	case PaintKind::Image:
		Image(paint.source, r, fit);
		return;
	case PaintKind::Linear:
	case PaintKind::Bilinear:
		break;
	}
	// Dégradés : couleur calculée à chaque sommet du contour (arrondis
	// compris), puis éventail de triangles depuis le centre — exact pour un
	// dégradé linéaire ou bilinéaire, quelle que soit la forme.
	auto colorAt = [&](sdl3::FPoint p) -> Color {
		const float u = r.w > 0.f ? (p.x - r.x) / r.w : 0.f;
		const float v = r.h > 0.f ? (p.y - r.y) / r.h : 0.f;
		if (paint.kind == PaintKind::Bilinear) {
			const Color top = Mix(paint.colors[0], paint.colors[1], u);
			const Color bottom = Mix(paint.colors[2], paint.colors[3], u);
			return Mix(top, bottom, v);
		}
		// CSS : 0deg pointe vers le haut, 90deg vers la droite.
		const float dx = std::sin(paint.angle * DEG), dy = -std::cos(paint.angle * DEG);
		const float len = std::fabs(r.w * dx) + std::fabs(r.h * dy);
		const float t = len > 0.f ? ((p.x - (r.x + r.w * 0.5f)) * dx + (p.y - (r.y + r.h * 0.5f)) * dy) / len + 0.5f : 0.f;
		return Mix(paint.colors[0], paint.colors[1], math::Clamp(t, 0.f, 1.f));
	};
	if (!HasRadius(radius)) {
		QuadGradient(r, colorAt({r.x, r.y}), colorAt({r.x + r.w, r.y}), colorAt({r.x, r.y + r.h}),
					 colorAt({r.x + r.w, r.y + r.h}));
		return;
	}
	const std::vector<sdl3::FPoint> outline = RoundedOutline(r, ClampRadius(r, radius));
	const sdl3::FPoint center{r.x + r.w * 0.5f, r.y + r.h * 0.5f};
	std::vector<sdl3::Vertex> verts;
	verts.reserve(outline.size() * 3);
	for (size_t i = 0; i < outline.size(); ++i) {
		const sdl3::FPoint a = outline[i], b = outline[(i + 1) % outline.size()];
		verts.push_back(V(center.x, center.y, colorAt(center)));
		verts.push_back(V(a.x, a.y, colorAt(a)));
		verts.push_back(V(b.x, b.y, colorAt(b)));
	}
	Triangles(verts);
}

void Painter::DrawShadow(const sdl3::FRect &r, const math::Corners &radius, const Shadow &shadow) {
	if (!shadow.IsVisible())
		return;
	const sdl3::FRect base{r.x + shadow.x - shadow.spread, r.y + shadow.y - shadow.spread, r.w + 2.f * shadow.spread,
						   r.h + 2.f * shadow.spread};
	// Flou approché : couches concentriques d'alpha décroissant.
	const int layers = math::Max(1, int(std::ceil(shadow.blur / 2.f)));
	for (int i = layers; i >= 1; --i) {
		const float grow = shadow.blur * float(i) / float(layers) * 0.5f;
		const sdl3::FRect rr{base.x - grow, base.y - grow, base.w + 2.f * grow, base.h + 2.f * grow};
		const math::Corners rc(radius.tl + grow, radius.tr + grow, radius.bl + grow, radius.br + grow);
		FillRoundedRect(rr, rc, WithAlpha(shadow.color, shadow.color.a / float(layers + 1)));
	}
	FillRoundedRect(base, radius, WithAlpha(shadow.color, shadow.color.a / float(layers + 1)));
}

void Painter::DrawGloss(const sdl3::FRect &r, float radius, float strength) {
	if (strength <= 0.f)
		return;
	const float inset = math::Max(1.f, radius * 0.4f);
	const sdl3::FRect band{r.x + inset, r.y + inset, r.w - 2.f * inset, r.h * 0.42f};
	if (Empty(band))
		return;
	VGradient(band, Color{1.f, 1.f, 1.f, math::Clamp(strength, 0.f, 1.f)}, Color{1.f, 1.f, 1.f, 0.f});
}

void Painter::DrawGlow(const sdl3::FRect &r, float radius, float strength, Color c) {
	if (!m_renderer || strength <= 0.f)
		return;
	constexpr int K_RINGS = 3;
	for (int i = 0; i < K_RINGS; ++i) {
		const float t = float(i) / float(K_RINGS);
		const float alpha = strength * (1.f - t) * 0.5f;
		if (alpha < 0.01f)
			continue;
		const float outset = 1.f + float(i) * 1.5f;
		const sdl3::FRect ring{r.x - outset, r.y - outset, r.w + 2.f * outset, r.h + 2.f * outset};
		if (IsClippedOut(ring))
			continue;
		(void)m_renderer->SetDrawColor(Fade(WithAlpha(c, alpha)));
		(void)m_renderer->DrawRoundedRect(ring, math::Corners(radius + outset));
	}
}

void Painter::Line(sdl3::FPoint a, sdl3::FPoint b, float width, Color c) {
	const float dx = b.x - a.x, dy = b.y - a.y;
	const float len = std::sqrt(dx * dx + dy * dy);
	if (len <= 0.f || c.a <= 0.f)
		return;
	const float h = math::Max(0.5f, width * 0.5f);
	const float nx = -dy / len * h, ny = dx / len * h;
	const sdl3::Vertex v[6] = {V(a.x + nx, a.y + ny, c), V(b.x + nx, b.y + ny, c), V(b.x - nx, b.y - ny, c),
							   V(a.x + nx, a.y + ny, c), V(b.x - nx, b.y - ny, c), V(a.x - nx, a.y - ny, c)};
	Triangles(v);
}

void Painter::Polyline(std::span<const sdl3::FPoint> pts, float width, Color c, bool closed) {
	if (pts.size() < 2)
		return;
	for (size_t i = 0; i + 1 < pts.size(); ++i)
		Line(pts[i], pts[i + 1], width, c);
	if (closed)
		Line(pts.back(), pts.front(), width, c);
	// Jointures : un disque à chaque sommet intérieur, pour un trait continu.
	if (width > 2.f)
		for (size_t i = 1; i + 1 < pts.size(); ++i)
			FillCircle(pts[i], width * 0.5f, c);
}

void Painter::FillPolygon(std::span<const sdl3::FPoint> pts, Color c) {
	if (pts.size() < 3 || c.a <= 0.f)
		return;
	std::vector<sdl3::Vertex> verts;
	verts.reserve((pts.size() - 2) * 3);
	for (size_t i = 1; i + 1 < pts.size(); ++i) {
		verts.push_back(V(pts[0].x, pts[0].y, c));
		verts.push_back(V(pts[i].x, pts[i].y, c));
		verts.push_back(V(pts[i + 1].x, pts[i + 1].y, c));
	}
	Triangles(verts);
}

void Painter::FillPie(sdl3::FPoint center, float radius, float startDeg, float sweepDeg, Color c) {
	if (radius <= 0.f || c.a <= 0.f)
		return;
	const int n = math::Max(6, int(std::ceil(std::fabs(sweepDeg) / 360.f * radius * 0.8f + 6.f)));
	std::vector<sdl3::Vertex> verts;
	verts.reserve(size_t(n) * 3);
	for (int i = 0; i < n; ++i) {
		const float a0 = (startDeg + sweepDeg * float(i) / float(n)) * DEG;
		const float a1 = (startDeg + sweepDeg * float(i + 1) / float(n)) * DEG;
		verts.push_back(V(center.x, center.y, c));
		verts.push_back(V(center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius, c));
		verts.push_back(V(center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius, c));
	}
	Triangles(verts);
}

void Painter::FillCircle(sdl3::FPoint center, float radius, Color c) { FillPie(center, radius, 0.f, 360.f, c); }

void Painter::StrokeCircle(sdl3::FPoint center, float radius, float width, Color c) {
	Arc(center, radius - width * 0.5f, 0.f, 360.f, width, c);
}

void Painter::Arc(sdl3::FPoint center, float radius, float startDeg, float sweepDeg, float width, Color c) {
	if (radius <= 0.f || width <= 0.f || c.a <= 0.f)
		return;
	const float r0 = math::Max(0.f, radius - width * 0.5f), r1 = radius + width * 0.5f;
	const int n = math::Max(6, int(std::ceil(std::fabs(sweepDeg) / 360.f * r1 * 0.8f + 6.f)));
	std::vector<sdl3::Vertex> verts;
	verts.reserve(size_t(n) * 6);
	for (int i = 0; i < n; ++i) {
		const float a0 = (startDeg + sweepDeg * float(i) / float(n)) * DEG;
		const float a1 = (startDeg + sweepDeg * float(i + 1) / float(n)) * DEG;
		const sdl3::FPoint i0{center.x + std::cos(a0) * r0, center.y + std::sin(a0) * r0};
		const sdl3::FPoint o0{center.x + std::cos(a0) * r1, center.y + std::sin(a0) * r1};
		const sdl3::FPoint i1{center.x + std::cos(a1) * r0, center.y + std::sin(a1) * r0};
		const sdl3::FPoint o1{center.x + std::cos(a1) * r1, center.y + std::sin(a1) * r1};
		verts.push_back(V(i0.x, i0.y, c));
		verts.push_back(V(o0.x, o0.y, c));
		verts.push_back(V(o1.x, o1.y, c));
		verts.push_back(V(i0.x, i0.y, c));
		verts.push_back(V(o1.x, o1.y, c));
		verts.push_back(V(i1.x, i1.y, c));
	}
	Triangles(verts);
}

sdl3::FPoint Painter::MeasureText(const String &line, sdl3::Font *font, float size) {
	if (m_fonts)
		return m_fonts->Measure(font, line, size);
	return HeuristicMeasure(size)(line);
}

void Painter::Text(const String &line, sdl3::Font *font, Color c, float x, float y) {
	if (!m_renderer || !m_engine.IsSome() || !font || line.IsEmpty() || c.a <= 0.f || Empty(m_clip))
		return;
	TextKey key{font, line};
	auto it = m_texts.find(key);
	if (it == m_texts.end()) {
		auto created = sdl3::Text::Create(m_engine.Value(), *font, line);
		if (!created)
			return;
		CachedText cached{std::move(created.Value()), Color{-1.f, 0.f, 0.f, 0.f}, 0, {}};
		if (auto s = cached.text.GetSize(); s.IsSome())
			cached.size = {float(s.Unwrap().x), float(s.Unwrap().y)};
		it = m_texts.emplace(std::move(key), std::move(cached)).first;
	}
	CachedText &t = it->second;
	t.lastUse = m_frame;
	if (x > m_clip.x + m_clip.w || y > m_clip.y + m_clip.h || x + t.size.x < m_clip.x || y + t.size.y < m_clip.y)
		return;
	const Color faded = Fade(c);
	if (!(t.color == faded)) {
		(void)t.text.SetColor(faded);
		t.color = faded;
	}
	(void)t.text.Draw(std::round(x), std::round(y));
}

void Painter::Image(const sdl3::Texture &tex, const sdl3::FRect &dst, ObjectFit fit, Option<sdl3::FRect> src) {
	if (!m_renderer || Empty(dst) || IsClippedOut(dst))
		return;
	const sdl3::FPoint size = tex.GetSize();
	(void)const_cast<sdl3::Texture &>(tex).SetAlphaMod(uint8_t(math::Clamp(m_opacity, 0.f, 1.f) * 255.f));
	if (src.IsSome()) {
		(void)m_renderer->Render(tex, src.Unwrap(), dst);
		return;
	}
	if (fit == ObjectFit::None) {
		// Taille d'origine centrée, rognée à la boîte.
		const sdl3::FRect placed{dst.x + (dst.w - size.x) * 0.5f, dst.y + (dst.h - size.y) * 0.5f, size.x, size.y};
		const sdl3::FRect visible = placed.Intersection(dst);
		const sdl3::FRect from{visible.x - placed.x, visible.y - placed.y, visible.w, visible.h};
		(void)m_renderer->Render(tex, from, visible);
		return;
	}
	if (fit == ObjectFit::Cover) {
		// Rognage côté source : pas de dépassement hors de la boîte.
		const float scale = math::Max(dst.w / size.x, dst.h / size.y);
		const float sw = dst.w / scale, sh = dst.h / scale;
		(void)m_renderer->Render(tex, sdl3::FRect{(size.x - sw) * 0.5f, (size.y - sh) * 0.5f, sw, sh}, dst);
		return;
	}
	(void)m_renderer->Render(tex, FitRect(size, dst, fit));
}

void Painter::Image(const String &key, const sdl3::FRect &dst, ObjectFit fit) {
	if (!m_textures)
		return;
	if (sdl3::Texture *t = m_textures->Get(key))
		Image(*t, dst, fit);
}

} // namespace ui
