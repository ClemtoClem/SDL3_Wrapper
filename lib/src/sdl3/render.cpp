// Définitions de sdl3/render.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/render.hpp"

namespace sdl3 {

// ── Surface ──────────────────────────────────────────────────────────────────

Result<Surface, Error> Surface::Create(int w, int h, PixelFormat fmt) {
	auto *s = SDL_CreateSurface(w, h, detail::ToSDL(fmt));
	if (!s)
		return Err(GetError());
	return Ok(Surface(s));
}

Result<Surface, StringView> Surface::CreateFromPixels(int w, int h, PixelFormat fmt, const void *pixels, int pitch) {
	if (w <= 0 || h <= 0 || !pixels || pitch <= 0)
		return Err(StringView("Surface::CreateFromPixels: dimensions, pitch ou tampon invalides"));
	auto *s = SDL_CreateSurface(w, h, detail::ToSDL(fmt));
	if (!s)
		return Err(GetError());
	Surface surface(s);
	if (!SDL_LockSurface(s))
		return Err(GetError());
	const size_t rowBytes = size_t(pitch < s->pitch ? pitch : s->pitch);
	for (int y = 0; y < h; ++y)
		SDL_memcpy(static_cast<uint8_t *>(s->pixels) + size_t(y) * size_t(s->pitch),
				   static_cast<const uint8_t *>(pixels) + size_t(y) * size_t(pitch), rowBytes);
	SDL_UnlockSurface(s);
	return Ok(std::move(surface));
}

Result<Surface, StringView> Surface::CreateFromFile(const String &path) {
	auto *s = IMG_Load(path.c_str());
	if (!s)
		return Err(GetError());
	return Ok(Surface(s));
}

Result<Surface, StringView> Surface::Convert(PixelFormat fmt) const {
	auto *s = SDL_ConvertSurface(m_handle, detail::ToSDL(fmt));
	if (!s)
		return Err(GetError());
	return Ok(Surface(s));
}

bool Surface::Fill(Color c) {
	if (!m_handle)
		return false;
	uint32_t mapped = SDL_MapSurfaceRGBA(m_handle, c.r, c.g, c.b, c.a);
	return SDL_FillSurfaceRect(m_handle, nullptr, mapped);
}

bool Surface::Blit(const Surface &src, Point dest) {
	if (!m_handle || !src.m_handle)
		return false;
	SDL_Rect dstR{dest.x, dest.y, src.GetWidth(), src.GetHeight()};
	return SDL_BlitSurface(src.m_handle, nullptr, m_handle, &dstR);
}

bool Surface::SetColorKey(bool enable, Color c) {
	if (!m_handle)
		return false;
	uint32_t key = SDL_MapSurfaceRGBA(m_handle, c.r, c.g, c.b, c.a);
	return SDL_SetSurfaceColorKey(m_handle, enable, key);
}

bool Surface::SetBlendMode(BlendMode mode) {
	return m_handle && SDL_SetSurfaceBlendMode(m_handle, SDL_BlendMode(int(mode)));
}

// ── Window ───────────────────────────────────────────────────────────────────

Result<Window, Error> Window::Create(const String &title, int w, int h, SDL_WindowFlags flags) {
	auto *win = SDL_CreateWindow(title.c_str(), w, h, flags);
	if (!win)
		return Err(GetError());
	return Ok(Window(win));
}

Result<Window, Error> Window::Create(const String &title, std::span<const int> size, SDL_WindowFlags flags) {
	auto *win = SDL_CreateWindow(title.c_str(), size[0], size[1], flags);
	if (!win)
		return Err(GetError());
	return Ok(Window(win));
}

Result<Window, Error> Window::Create(const String &title, std::span<const int> pos,
		std::span<const int> size, SDL_WindowFlags flags) {
	auto *win = SDL_CreateWindow(title.c_str(), size[0], size[1], flags);
	if (!win)
		return Err(GetError());
	SDL_SetWindowPosition(win, pos[0], pos[1]);
	return Ok(Window(win));
}

int Window::GetWidth() const {
	int w = 0;
	if (m_handle)
		SDL_GetWindowSize(m_handle, &w, nullptr);
	return w;
}

int Window::GetHeight() const {
	int h = 0;
	if (m_handle)
		SDL_GetWindowSize(m_handle, nullptr, &h);
	return h;
}

Point Window::GetSize() const {
	int w = 0, h = 0;
	if (m_handle)
		SDL_GetWindowSize(m_handle, &w, &h);
	return {w, h};
}

int Window::GetX() const {
	int x = 0;
	if (m_handle)
		SDL_GetWindowPosition(m_handle, &x, nullptr);
	return x;
}

int Window::GetY() const {
	int y = 0;
	if (m_handle)
		SDL_GetWindowPosition(m_handle, nullptr, &y);
	return y;
}

Point Window::GetPosition() const {
	int x = 0, y = 0;
	if (m_handle)
		SDL_GetWindowPosition(m_handle, &x, &y);
	return {x, y};
}

bool Window::HasInputFocus() const {
	return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_INPUT_FOCUS;
}

bool Window::HasMouseFocus() const {
	return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_MOUSE_FOCUS;
}

String Window::Title() const {
	const char *t = m_handle ? SDL_GetWindowTitle(m_handle) : "";
	return String(t ? t : "");
}

void Window::SetTitle(const String &t) {
	if (m_handle)
		SDL_SetWindowTitle(m_handle, t.CStr());
}

void Window::SetSize(int w, int h) {
	if (m_handle)
		SDL_SetWindowSize(m_handle, w, h);
}

void Window::SetPosition(int x, int y) {
	if (m_handle)
		SDL_SetWindowPosition(m_handle, x, y);
}

void Window::SetFullscreen(bool on) {
	if (m_handle)
		SDL_SetWindowFullscreen(m_handle, on);
}

void Window::SetResizable(bool on) {
	if (m_handle)
		SDL_SetWindowResizable(m_handle, on);
}

void Window::SetBordered(bool on) {
	if (m_handle)
		SDL_SetWindowBordered(m_handle, on);
}

void Window::SetOpacity(float opacity) {
	if (m_handle)
		SDL_SetWindowOpacity(m_handle, opacity);
}

void Window::StartTextInput() {
	if (m_handle)
		SDL_StartTextInput(m_handle);
}

void Window::StopTextInput() {
	if (m_handle)
		SDL_StopTextInput(m_handle);
}

void Window::Show() {
	if (m_handle)
		SDL_ShowWindow(m_handle);
}

void Window::Hide() {
	if (m_handle)
		SDL_HideWindow(m_handle);
}

void Window::Raise() {
	if (m_handle)
		SDL_RaiseWindow(m_handle);
}

void Window::Maximize() {
	if (m_handle)
		SDL_MaximizeWindow(m_handle);
}

void Window::Minimize() {
	if (m_handle)
		SDL_MinimizeWindow(m_handle);
}

void Window::Restore() {
	if (m_handle)
		SDL_RestoreWindow(m_handle);
}

bool Window::SetHitTest(std::function<SDL_HitTestResult(const SDL_Point &)> fn) {
	if (!m_handle)
		return false;
	if (!fn) {
		hitTest.reset();
		return SDL_SetWindowHitTest(m_handle, nullptr, nullptr);
	}
	hitTest = std::make_unique<std::function<SDL_HitTestResult(const SDL_Point &)>>(std::move(fn));
	return SDL_SetWindowHitTest(m_handle, &Window::HitTestTrampoline, hitTest.get());
}

SDL_HitTestResult SDLCALL Window::HitTestTrampoline(SDL_Window *, const SDL_Point *area, void *data) {
	auto *fn = static_cast<std::function<SDL_HitTestResult(const SDL_Point &)> *>(data);
	return (*fn)(*area);
}

// ── Texture ──────────────────────────────────────────────────────────────────

FPoint Texture::GetSize() const {
	float w = 0, h = 0;
	if (m_handle)
		SDL_GetTextureSize(m_handle, &w, &h);
	return {w, h};
}

bool Texture::SetBlendMode(BlendMode mode) {
	return m_handle && SDL_SetTextureBlendMode(m_handle, SDL_BlendMode(int(mode)));
}

bool Texture::Update(const void *pixels, int pitch, Option<Rect> rect) {
	if (rect.IsSome()) {
		SDL_Rect r = SDL_Rect(rect.Unwrap());
		return m_handle && SDL_UpdateTexture(m_handle, &r, pixels, pitch);
	}
	return m_handle && SDL_UpdateTexture(m_handle, nullptr, pixels, pitch);
}

namespace shapes {

std::vector<FPoint> GenerateCirclePoints(FPoint center, float radius) {
	std::vector<FPoint> points;
	int x = 0, y = int(radius);
	int d = 3 - 2 * int(radius);
	while (x <= y) {
		float fx = float(x), fy = float(y);
		points.push_back({center.x + fx, center.y + fy});
		points.push_back({center.x + fx, center.y - fy});
		points.push_back({center.x - fx, center.y + fy});
		points.push_back({center.x - fx, center.y - fy});
		points.push_back({center.x + fy, center.y + fx});
		points.push_back({center.x + fy, center.y - fx});
		points.push_back({center.x - fy, center.y + fx});
		points.push_back({center.x - fy, center.y - fx});
		d += (d < 0) ? 4 * x + 6 : 4 * (x - y--) + 10;
		++x;
	}
	return points;
}

std::vector<FPoint> GenerateEllipsePoints(FPoint center, float radiusX, float radiusY, float rotationDeg) {
	std::vector<FPoint> points;
	if (radiusX <= 0.f || radiusY <= 0.f) {
		points.push_back(center);
		return points;
	}

	float maxRadius = sdl3::Max(radiusX, radiusY);
	int n = sdl3::Max(8, int(std::ceil(2.f * PI_F * maxRadius)));

	float rad = rotationDeg * (PI_F / 180.f);
	float cosA = std::cos(rad), sinA = std::sin(rad);
	float dTheta = 2.f * PI_F / float(n);

	points.reserve(size_t(n));
	for (int i = 0; i < n; ++i) {
		float theta = float(i) * dTheta;
		float cosT = std::cos(theta), sinT = std::sin(theta);
		points.push_back({center.x + radiusX * cosT * cosA - radiusY * sinT * sinA,
						  center.y + radiusX * cosT * sinA + radiusY * sinT * cosA});
	}
	return points;
}

std::vector<FPoint> GenerateArcPoints(FPoint center, float radius, float startAngleDeg, float endAngleDeg) {
	std::vector<FPoint> points;
	if (radius <= 0.f) {
		points.push_back(center);
		return points;
	}

	float span = endAngleDeg - startAngleDeg;
	while (span < 0.f)
		span += 360.f;
	while (span > 360.f)
		span -= 360.f;
	if (span == 0.f)
		span = 360.f;

	int n = sdl3::Max(4, int(std::ceil(PI_F * radius * span / 180.f)));

	float startRad = startAngleDeg * (PI_F / 180.f);
	float spanRad = span * (PI_F / 180.f);

	points.reserve(size_t(n) + 1);
	for (int i = 0; i <= n; ++i) {
		float t = float(i) / float(n);
		float ang = startRad + t * spanRad;
		points.push_back({center.x + radius * std::cos(ang), center.y + radius * std::sin(ang)});
	}
	return points;
}

} // namespace shapes

// ── Renderer ─────────────────────────────────────────────────────────────────

Result<Renderer, Error> Renderer::Create(Window &window, const char *driver) {
	auto *r = SDL_CreateRenderer(window, driver);
	if (!r)
		return Err(GetError());
	return Ok(Renderer(r));
}

Option<Renderer::VSync> Renderer::GetVSync() const {
	int mode = 0;
	if (!m_handle || !SDL_GetRenderVSync(m_handle, &mode))
		return NONE;
	return Some(VSync(mode));
}

Result<Renderer, StringView> Renderer::CreateSoftware(Surface &surface) {
	auto *r = SDL_CreateSoftwareRenderer(surface);
	if (!r)
		return Err(GetError());
	return Ok(Renderer(r));
}

Point Renderer::GetOutputSize() const {
	int x, y;
	SDL_GetRenderOutputSize(m_handle, &x, &y);
	return Point{x, y};
}

int Renderer::GetOutputWidth() const {
	return GetOutputSize().x;
}

int Renderer::GetOutputHeight() const {
	return GetOutputSize().y;
}

bool Renderer::SetDrawColor(FColor c) {
	return (!m_handle) ? false : SDL_SetRenderDrawColorFloat(m_handle, c.r, c.g, c.b, c.a);
}

Color Renderer::GetDrawColor() const {
	Color c{};
	if (m_handle)
		SDL_GetRenderDrawColor(m_handle, &c.r, &c.g, &c.b, &c.a);
	return c;
}

FColor Renderer::GetDrawColorFloat() const {
	FColor c{};
	if (m_handle)
		SDL_GetRenderDrawColorFloat(m_handle, &c.r, &c.g, &c.b, &c.a);
	return c;
}

bool Renderer::SetBlendMode(BlendMode mode) {
	return m_handle && SDL_SetRenderDrawBlendMode(m_handle, SDL_BlendMode(int(mode)));
}

bool Renderer::SetClipRect(const Rect &r) {
	SDL_Rect sr = r;
	return m_handle && SDL_SetRenderClipRect(m_handle, &sr);
}

bool Renderer::SetClipRect(const FRect &r) {
	SDL_Rect sr = SDL_Rect(r);
	return m_handle && SDL_SetRenderClipRect(m_handle, &sr);
}

bool Renderer::SetViewport(const Rect &r) {
	SDL_Rect sr = r;
	return m_handle && SDL_SetRenderViewport(m_handle, &sr);
}

bool Renderer::SetViewport(const FRect &r) {
	SDL_Rect sr = SDL_Rect(r);
	return m_handle && SDL_SetRenderViewport(m_handle, &sr);
}

bool Renderer::DrawPoints(std::span<const FPoint> pts) {
	return m_handle && SDL_RenderPoints(m_handle, (SDL_FPoint*)(pts.data()), int(pts.size()));
}

bool Renderer::DrawLine(float x1, float y1, float x2, float y2) {
	return m_handle && SDL_RenderLine(m_handle, x1, y1, x2, y2);
}

bool Renderer::DrawLines(std::span<const FPoint> pts) {
	return m_handle && SDL_RenderLines(m_handle, (SDL_FPoint*)(pts.data()), int(pts.size()));
}

bool Renderer::DrawRect(const FRect &r) {
	SDL_FRect sr = r;
	return m_handle && SDL_RenderRect(m_handle, &sr);
}

bool Renderer::FillRect(const FRect &r) {
	SDL_FRect sr = r;
	return m_handle && SDL_RenderFillRect(m_handle, &sr);
}

bool Renderer::DrawRects(std::span<const FRect> rects) {
	return m_handle && SDL_RenderRects(m_handle, (SDL_FRect*)(rects.data()), int(rects.size()));
}

bool Renderer::FillRects(const FRect *rects, int count) {
	return m_handle && SDL_RenderFillRects(m_handle, (SDL_FRect*)rects, count);
}

bool Renderer::FillRects(std::span<const FRect> rects) {
	return m_handle && SDL_RenderFillRects(m_handle, (SDL_FRect*)(rects.data()), int(rects.size()));
}

bool Renderer::Render(const Texture &tex, float x, float y) {
	float w, h;
	SDL_GetTextureSize(tex.Get(), &w, &h);
	SDL_FRect d{x, y, w, h};
	return m_handle && SDL_RenderTexture(m_handle, tex.Get(), nullptr, &d);
}

bool Renderer::Render(const Texture &tex, const FRect &dst) {
	SDL_FRect d = dst;
	return m_handle && SDL_RenderTexture(m_handle, tex.Get(), nullptr, &d);
}

bool Renderer::Render(const Texture &tex, const FRect &src, const FRect &dst) {
	SDL_FRect s = src, d = dst;
	return m_handle && SDL_RenderTexture(m_handle, tex.Get(), &s, &d);
}

bool Renderer::RenderRotated(const Texture &tex, const FRect &dst, double angleDeg, FPoint center, FlipMode flip) {
	SDL_FRect d = dst;
	SDL_FPoint c = center;
	return m_handle &&
		   SDL_RenderTextureRotated(m_handle, tex.Get(), nullptr, &d, angleDeg, &c, SDL_FlipMode(int(flip)));
}

bool Renderer::RenderGeometry(std::span<const Vertex> vertices, std::span<const int> indices) {
	return m_handle && SDL_RenderGeometry(m_handle, nullptr, (SDL_Vertex*)(vertices.data()), int(vertices.size()),
										  indices.empty() ? nullptr : indices.data(), int(indices.size()));
}

bool Renderer::RenderGeometry(const Texture &tex, std::span<const Vertex> vertices, std::span<const int> indices) {
	return m_handle && SDL_RenderGeometry(m_handle, tex.Get(), (SDL_Vertex*)(vertices.data()), int(vertices.size()),
										  indices.empty() ? nullptr : indices.data(), int(indices.size()));
}

bool Renderer::RenderGeometryFromPoints(std::span<const FPoint> points) {
	if (points.size() < 3)
		return false;
	FColor color = GetDrawColorFloat();

	float cx = 0.f, cy = 0.f;
	for (auto &p : points) {
		cx += p.x;
		cy += p.y;
	}
	cx /= float(points.size());
	cy /= float(points.size());

	std::vector<Vertex> verts;
	verts.reserve(points.size() * 3);
	Vertex centerV{{cx, cy}, color, {0.f, 0.f}};
	for (size_t i = 0; i < points.size(); ++i) {
		size_t j = (i + 1) % points.size();
		verts.push_back(centerV);
		verts.push_back({{points[i].x, points[i].y}, color, {0.f, 0.f}});
		verts.push_back({{points[j].x, points[j].y}, color, {0.f, 0.f}});
	}
	return RenderGeometry(verts);
}

bool Renderer::FillCircle(FPoint center, float radius) {
	return RenderGeometryFromPoints(shapes::GenerateEllipsePoints(center, radius, radius, 0.f));
}

bool Renderer::DrawEllipse(FPoint center, float radiusX, float radiusY, float rotationDeg) {
	return DrawPoints(shapes::GenerateEllipsePoints(center, radiusX, radiusY, rotationDeg));
}

bool Renderer::FillEllipse(FPoint center, float radiusX, float radiusY, float rotationDeg) {
	return RenderGeometryFromPoints(shapes::GenerateEllipsePoints(center, radiusX, radiusY, rotationDeg));
}

bool Renderer::DrawArc(FPoint center, float radius, float startAngleDeg, float endAngleDeg) {
	return DrawLines(shapes::GenerateArcPoints(center, radius, startAngleDeg, endAngleDeg));
}

bool Renderer::DrawPie(FPoint center, float radius, float startAngleDeg, float endAngleDeg) {
	auto arc = shapes::GenerateArcPoints(center, radius, startAngleDeg, endAngleDeg);
	bool ok = DrawLines(arc);
	if (!arc.empty()) {
		ok = DrawLine(center, arc.front()) && ok;
		ok = DrawLine(center, arc.back()) && ok;
	}
	return ok;
}

Corners Renderer::ClampCorners(const FRect &rect, const Corners &c) noexcept {
	const float limit = sdl3::Max(0.f, sdl3::Min(rect.w, rect.h) * 0.5f);
	return Corners{sdl3::Clamp(c.tl, 0.f, limit), sdl3::Clamp(c.tr, 0.f, limit), sdl3::Clamp(c.bl, 0.f, limit),
				   sdl3::Clamp(c.br, 0.f, limit)};
}

bool Renderer::DrawRoundedRect(const FRect &outer, const Corners &corners) {
	if (outer.w <= 0.f || outer.h <= 0.f)
		return true;
	const FRect rect{outer.x, outer.y, Max(0.f, outer.w - 1.f), Max(0.f, outer.h - 1.f)};
	const Corners c = ClampCorners(rect, corners);
	bool ok = true;
	ok = DrawArc({rect.x + c.tl, rect.y + c.tl}, c.tl, 180.f, 270.f) && ok;
	ok = DrawArc({rect.x + rect.w - c.tr, rect.y + c.tr}, c.tr, 270.f, 360.f) && ok;
	ok = DrawArc({rect.x + rect.w - c.br, rect.y + rect.h - c.br}, c.br, 0.f, 90.f) && ok;
	ok = DrawArc({rect.x + c.bl, rect.y + rect.h - c.bl}, c.bl, 90.f, 180.f) && ok;
	ok = DrawLine({rect.x + c.tl, rect.y}, {rect.x + rect.w - c.tr, rect.y}) && ok;
	ok = DrawLine({rect.x + rect.w, rect.y + c.tr}, {rect.x + rect.w, rect.y + rect.h - c.br}) && ok;
	ok = DrawLine({rect.x + rect.w - c.br, rect.y + rect.h}, {rect.x + c.bl, rect.y + rect.h}) && ok;
	ok = DrawLine({rect.x, rect.y + c.bl}, {rect.x, rect.y + rect.h - c.bl}) && ok;
	return ok;
}

bool Renderer::FillRoundedRect(const FRect &rect, const Corners &corners) {
	const Corners c = ClampCorners(rect, corners);
	FColor color = GetDrawColorFloat();
	bool ok = true;

	auto fillSector = [&](FPoint arcCenter, float r, float startDeg, float endDeg) {
		if (r <= 0.f)
			return true;
		int n = sdl3::Max(4, int(std::ceil(PI_F * r * 0.5f)));
		float startRad = startDeg * (PI_F / 180.f), endRad = endDeg * (PI_F / 180.f);
		Vertex cv{{arcCenter.x, arcCenter.y}, color, {0.f, 0.f}};
		std::vector<Vertex> verts;
		verts.reserve(size_t(n) * 3);
		for (int i = 0; i < n; ++i) {
			float t0 = float(i) / float(n), t1 = float(i + 1) / float(n);
			float a0 = Lerp(startRad, endRad, t0), a1 = Lerp(startRad, endRad, t1);
			verts.push_back(cv);
			verts.push_back({{arcCenter.x + r * std::cos(a0), arcCenter.y + r * std::sin(a0)}, color, {0.f, 0.f}});
			verts.push_back({{arcCenter.x + r * std::cos(a1), arcCenter.y + r * std::sin(a1)}, color, {0.f, 0.f}});
		}
		return RenderGeometry(verts);
	};

	if (c.tl > 0.f)
		ok = fillSector({rect.x + c.tl, rect.y + c.tl}, c.tl, 180.f, 270.f) && ok;
	if (c.tr > 0.f)
		ok = fillSector({rect.x + rect.w - c.tr, rect.y + c.tr}, c.tr, 270.f, 360.f) && ok;
	if (c.br > 0.f)
		ok = fillSector({rect.x + rect.w - c.br, rect.y + rect.h - c.br}, c.br, 0.f, 90.f) && ok;
	if (c.bl > 0.f)
		ok = fillSector({rect.x + c.bl, rect.y + rect.h - c.bl}, c.bl, 90.f, 180.f) && ok;

	float innerLeft = rect.x + sdl3::Max(c.tl, c.bl);
	float innerRight = rect.x + rect.w - sdl3::Max(c.tr, c.br);
	if (innerRight > innerLeft)
		ok = FillRect({innerLeft, rect.y, innerRight - innerLeft, rect.h}) && ok;

	float leftW = sdl3::Max(c.tl, c.bl), leftH = rect.h - c.tl - c.bl;
	if (leftW > 0.f && leftH > 0.f)
		ok = FillRect({rect.x, rect.y + c.tl, leftW, leftH}) && ok;

	float rightW = sdl3::Max(c.tr, c.br), rightH = rect.h - c.tr - c.br;
	if (rightW > 0.f && rightH > 0.f)
		ok = FillRect({rect.x + rect.w - rightW, rect.y + c.tr, rightW, rightH}) && ok;

	return ok;
}

bool Renderer::DrawRoundedBorderedRect(const FRect &rect, const Sides &border, const Corners &c) {
	FColor color = GetDrawColorFloat();
	bool ok = true;

	auto quad = [&](FPoint a0, FPoint a1, FPoint b0, FPoint b1) {
		Vertex va0{{a0.x, a0.y}, color, {}}, va1{{a1.x, a1.y}, color, {}};
		Vertex vb0{{b0.x, b0.y}, color, {}}, vb1{{b1.x, b1.y}, color, {}};
		std::array<Vertex, 6> v{va0, va1, vb0, va1, vb1, vb0};
		return RenderGeometry(v);
	};

	auto arcAnnulus = [&](FPoint center, float innerR, float outerR, float startDeg, float endDeg) {
		if (outerR <= 0.f)
			return true;
		float clampedInner = sdl3::Max(0.f, innerR);
		int n = sdl3::Max(4, int(std::ceil(PI_F * outerR * 0.5f)));
		float sRad = startDeg * (PI_F / 180.f), eRad = endDeg * (PI_F / 180.f);
		std::vector<Vertex> verts;
		verts.reserve(size_t(n) * 6);
		for (int i = 0; i < n; ++i) {
			float t0 = float(i) / float(n), t1 = float(i + 1) / float(n);
			float a0 = Lerp(sRad, eRad, t0), a1 = Lerp(sRad, eRad, t1);
			float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
			Vertex i0{{center.x + clampedInner * c0, center.y + clampedInner * s0}, color, {}};
			Vertex i1{{center.x + clampedInner * c1, center.y + clampedInner * s1}, color, {}};
			Vertex o0{{center.x + outerR * c0, center.y + outerR * s0}, color, {}};
			Vertex o1{{center.x + outerR * c1, center.y + outerR * s1}, color, {}};
			verts.insert(verts.end(), {i0, o0, i1, i1, o0, o1});
		}
		return RenderGeometry(verts);
	};

	if (float x0 = rect.x + c.tl, x1 = rect.x + rect.w - c.tr; x1 > x0)
		ok = quad({x0, rect.y}, {x1, rect.y}, {x0, rect.y - border.top}, {x1, rect.y - border.top}) && ok;
	if (float x0 = rect.x + c.bl, x1 = rect.x + rect.w - c.br; x1 > x0)
		ok = quad({x0, rect.y + rect.h}, {x1, rect.y + rect.h}, {x0, rect.y + rect.h + border.bottom},
				  {x1, rect.y + rect.h + border.bottom}) &&
			 ok;
	if (float y0 = rect.y + c.tl, y1 = rect.y + rect.h - c.bl; y1 > y0)
		ok = quad({rect.x, y0}, {rect.x, y1}, {rect.x - border.left, y0}, {rect.x - border.left, y1}) && ok;
	if (float y0 = rect.y + c.tr, y1 = rect.y + rect.h - c.br; y1 > y0)
		ok = quad({rect.x + rect.w, y0}, {rect.x + rect.w, y1}, {rect.x + rect.w + border.right, y0},
				  {rect.x + rect.w + border.right, y1}) &&
			 ok;

	ok = arcAnnulus({rect.x + c.tl, rect.y + c.tl}, c.tl, c.tl + sdl3::Max(border.left, border.top), 180.f, 270.f) &&
		 ok;
	ok = arcAnnulus({rect.x + rect.w - c.tr, rect.y + c.tr}, c.tr, c.tr + sdl3::Max(border.right, border.top), 270.f,
					360.f) &&
		 ok;
	ok = arcAnnulus({rect.x + rect.w - c.br, rect.y + rect.h - c.br}, c.br,
					c.br + sdl3::Max(border.right, border.bottom), 0.f, 90.f) &&
		 ok;
	ok = arcAnnulus({rect.x + c.bl, rect.y + rect.h - c.bl}, c.bl, c.bl + sdl3::Max(border.left, border.bottom),
					90.f, 180.f) &&
		 ok;
	return ok;
}

bool Renderer::DrawPolygon(std::span<const FPoint> points) {
	if (points.size() < 2)
		return false;
	std::vector<FPoint> closed(points.begin(), points.end());
	closed.push_back(closed.front());
	return DrawLines(closed);
}

bool Renderer::FillPolygon(std::span<const FPoint> points) {
	if (points.size() < 3)
		return false;
	return RenderGeometryFromPoints(points);
}

bool Renderer::DrawBezier(std::span<const FPoint> controlPoints, float step) {
	if (controlPoints.size() < 2)
		return false;
	step = Clamp(step, 0.001f, 1.0f);

	size_t n = controlPoints.size();
	std::vector<float> wx(n), wy(n);
	int numSamples = int(std::ceil(1.f / step)) + 1;

	std::vector<FPoint> curve;
	curve.reserve(size_t(numSamples));
	for (int s = 0; s < numSamples; ++s) {
		float t = (s == numSamples - 1) ? 1.f : float(s) * step;
		for (size_t i = 0; i < n; ++i) {
			wx[i] = controlPoints[i].x;
			wy[i] = controlPoints[i].y;
		}
		for (size_t r = 1; r < n; ++r) {
			for (size_t i = 0; i + r < n; ++i) {
				wx[i] = Lerp(wx[i], wx[i + 1], t);
				wy[i] = Lerp(wy[i], wy[i + 1], t);
			}
		}
		curve.push_back({wx[0], wy[0]});
	}
	return DrawLines(curve);
}

Result<Texture, StringView> Renderer::CreateTexture(PixelFormat fmt, TextureAccess access, int w, int h) {
	auto *t = SDL_CreateTexture(m_handle, detail::ToSDL(fmt), detail::ToSDL(access), w, h);
	if (!t) return Err(GetError());
	return Ok(Texture(t));
}

Result<Texture, StringView> Renderer::CreateTextureFromSurface(const Surface &surf) {
	auto *t = SDL_CreateTextureFromSurface(m_handle, surf.Get());
	if (!t) return Err(GetError());
	return Ok(Texture(t));
}

Result<Surface, StringView> Renderer::ReadPixels(Option<Rect> rect) const {
	if (!m_handle)
		return Err(StringView("Renderer::ReadPixels: null renderer"));
	SDL_Rect sr{};
	const SDL_Rect *rectPtr = nullptr;
	if (rect.IsSome()) {
		sr = SDL_Rect(rect.Unwrap());
		rectPtr = &sr;
	}
	auto *s = SDL_RenderReadPixels(m_handle, rectPtr);
	if (!s)
		return Err(GetError());
	return Ok(Surface(s));
}

Point Renderer::OutputSize() const {
	int w = 0, h = 0;
	if (m_handle)
		SDL_GetRenderOutputSize(m_handle, &w, &h);
	return {w, h};
}

Result<Texture, StringView> Texture::Create(Renderer &ren, int w, int h, PixelFormat fmt) {
	auto *t = SDL_CreateTexture(ren.Get(), detail::ToSDL(fmt), SDL_TEXTUREACCESS_STATIC, w, h);
	if (!t)
		return Err(GetError());
	return Ok(Texture(t));
}

Result<Texture, StringView> Texture::CreateFromSurface(Renderer &ren, const Surface &surf) {
	auto *t = SDL_CreateTextureFromSurface(ren.Get(), surf.Get());
	if (!t)
		return Err(GetError());
	return Ok(Texture(t));
}

Result<Texture, StringView> Texture::CreateFromFile(Renderer &ren, const String &path) {
	auto *t = IMG_LoadTexture(ren.Get(), path.c_str());
	if (!t)
		return Err(GetError());
	return Ok(Texture(t));
}

} // namespace sdl3
