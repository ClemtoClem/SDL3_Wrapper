// Définitions de ui/window_manager.hpp
#include <ui/window_manager.hpp>

#include <algorithm>
#include <filesystem>

#include <sdl3/image.hpp>

namespace ui {

namespace {

/// Identifiants de routage des contextes sans fenêtre : hors de la plage
/// des SDL_WindowID réels (petits entiers croissants).
constexpr SDL_WindowID HEADLESS_ROUTING = 0x8000'0000u;

/// Modificateurs normalisés : gauche ou droite indifférent, verrous ignorés.
SDL_Keymod NormalizeMods(SDL_Keymod mods) noexcept {
	SDL_Keymod out = SDL_KMOD_NONE;
	if (mods & SDL_KMOD_CTRL)
		out |= SDL_KMOD_CTRL;
	if (mods & SDL_KMOD_SHIFT)
		out |= SDL_KMOD_SHIFT;
	if (mods & SDL_KMOD_ALT)
		out |= SDL_KMOD_ALT;
	if (mods & SDL_KMOD_GUI)
		out |= SDL_KMOD_GUI;
	return out;
}

bool HasWindow(uint32_t type) noexcept {
	return (type >= SDL_EVENT_WINDOW_FIRST && type <= SDL_EVENT_WINDOW_LAST) || type == SDL_EVENT_KEY_DOWN ||
		   type == SDL_EVENT_KEY_UP || type == SDL_EVENT_TEXT_EDITING || type == SDL_EVENT_TEXT_INPUT ||
		   type == SDL_EVENT_MOUSE_MOTION || type == SDL_EVENT_MOUSE_BUTTON_DOWN || type == SDL_EVENT_MOUSE_BUTTON_UP ||
		   type == SDL_EVENT_MOUSE_WHEEL || type == SDL_EVENT_FINGER_DOWN || type == SDL_EVENT_FINGER_UP ||
		   type == SDL_EVENT_FINGER_MOTION || type == SDL_EVENT_DROP_BEGIN || type == SDL_EVENT_DROP_FILE ||
		   type == SDL_EVENT_DROP_TEXT || type == SDL_EVENT_DROP_COMPLETE || type == SDL_EVENT_DROP_POSITION ||
		   type == SDL_EVENT_PEN_PROXIMITY_IN || type == SDL_EVENT_PEN_PROXIMITY_OUT || type == SDL_EVENT_PEN_DOWN ||
		   type == SDL_EVENT_PEN_UP || type == SDL_EVENT_PEN_MOTION || type == SDL_EVENT_PEN_BUTTON_DOWN ||
		   type == SDL_EVENT_PEN_BUTTON_UP || type == SDL_EVENT_PEN_AXIS;
}

/// Surface RGBA32 d'une capture (ReadPixels rend le format du renderer).
SDL_Surface *ToRgba(const sdl3::Surface &surface) {
	return surface.Get() ? SDL_ConvertSurface(surface.Get(), SDL_PIXELFORMAT_RGBA32) : nullptr;
}

Result<bool, String> SavePng(SDL_Surface *surface, const String &path) {
	std::error_code ec;
	const std::filesystem::path parent = std::filesystem::path(path.CStr()).parent_path();
	if (!parent.empty())
		std::filesystem::create_directories(parent, ec);
	if (!IMG_SavePNG(surface, path.CStr()))
		return Err(String(SDL_GetError()));
	return Ok(true);
}

} // namespace

SDL_WindowID EventWindowId(const sdl3::Event &ev) noexcept {
	const uint32_t type = ev.raw.type;
	if (!HasWindow(type))
		return 0;
	if (type >= SDL_EVENT_WINDOW_FIRST && type <= SDL_EVENT_WINDOW_LAST)
		return ev.raw.window.windowID;
	switch (type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		return ev.raw.key.windowID;
	case SDL_EVENT_TEXT_EDITING:
		return ev.raw.edit.windowID;
	case SDL_EVENT_TEXT_INPUT:
		return ev.raw.text.windowID;
	case SDL_EVENT_MOUSE_MOTION:
		return ev.raw.motion.windowID;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return ev.raw.button.windowID;
	case SDL_EVENT_MOUSE_WHEEL:
		return ev.raw.wheel.windowID;
	case SDL_EVENT_FINGER_DOWN:
	case SDL_EVENT_FINGER_UP:
	case SDL_EVENT_FINGER_MOTION:
		return ev.raw.tfinger.windowID;
	case SDL_EVENT_DROP_BEGIN:
	case SDL_EVENT_DROP_FILE:
	case SDL_EVENT_DROP_TEXT:
	case SDL_EVENT_DROP_COMPLETE:
	case SDL_EVENT_DROP_POSITION:
		return ev.raw.drop.windowID;
	case SDL_EVENT_PEN_PROXIMITY_IN:
	case SDL_EVENT_PEN_PROXIMITY_OUT:
		return ev.raw.pproximity.windowID;
	case SDL_EVENT_PEN_DOWN:
	case SDL_EVENT_PEN_UP:
		return ev.raw.ptouch.windowID;
	case SDL_EVENT_PEN_MOTION:
		return ev.raw.pmotion.windowID;
	case SDL_EVENT_PEN_BUTTON_DOWN:
	case SDL_EVENT_PEN_BUTTON_UP:
		return ev.raw.pbutton.windowID;
	case SDL_EVENT_PEN_AXIS:
		return ev.raw.paxis.windowID;
	default:
		return 0;
	}
}

// ============================================================================
// WindowContext
// ============================================================================

WindowContext::~WindowContext() {
	// L'interface (et son contexte, rangé dans les ressources du monde) avant
	// le renderer dont elle détient les textures.
	m_gui.reset();
	m_renderer.reset();
	m_window.reset();
}

void WindowContext::SetContentScale(float scale) {
	m_contentScale = scale > 0.f ? scale : 1.f;
	m_gui->SetContentScale(m_contentScale);
}

bool WindowContext::TextFieldHasFocus() const {
	const Context &ctx = const_cast<WindowContext *>(this)->m_gui->Ctx();
	const ecs::Entity focus = ctx.input.focus;
	if (!focus.Valid() || !m_world.IsAlive(focus))
		return false;
	auto editor = m_world.GetComponent<TextEditor>(focus);
	return editor.IsSome() && IoCanWrite(editor.Unwrap()->mode);
}

bool WindowContext::PointerCapture() const {
	const Context &ctx = const_cast<WindowContext *>(this)->m_gui->Ctx();
	return ctx.input.drag != Interaction::Drag::None || ctx.input.pressed.Valid();
}

// ============================================================================
// WindowManager
// ============================================================================

WindowManager::~WindowManager() {
	while (!m_windows.empty())
		(void)Close(m_windows.back()->Id());
	if (m_gpu) {
		SDL_DestroyGPUDevice(m_gpu);
		m_gpu = nullptr;
	}
}

SDL_GPUDevice *WindowManager::GpuDevice() const noexcept { return m_gpu; }

Result<bool, String> WindowManager::EnsureGpuDevice() {
	if (m_gpu)
		return Ok(true);
	// Mêmes formats que render3d::Canvas::Create : le Canvas emprunte ce device.
	m_gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL, false,
								nullptr);
	if (!m_gpu)
		return Err(String(SDL_GetError()));
	return Ok(true);
}

Result<WindowContext *, String> WindowManager::Create(WindowSpec spec) {
	const WindowId id = m_nextId++;
	std::unique_ptr<WindowContext> ctx(new WindowContext(id, std::move(spec)));
	const WindowSpec &s = ctx->m_spec;
	const sdl3::FPoint size{float(std::max(1, s.width)), float(std::max(1, s.height))};

	if (s.headless) {
		ctx->m_routingId = HEADLESS_ROUTING | id;
		ctx->m_gui = std::make_unique<Ui>(ctx->m_world, size, s.theme);
	} else {
		if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO))
			return Err(String("WindowManager::Create : vidéo SDL non initialisée"));
		// Cachée le temps de la placer : pas de saut visible à l'écran.
		auto window = sdl3::Window::Create(s.title, s.width, s.height, s.flags | sdl3::window_flags::HIDDEN);
		if (!window)
			return Err(String("fenêtre « ") + s.title + String(" » : ") + String(window.Error()));
		ctx->m_window = std::make_unique<sdl3::Window>(std::move(window.Value()));
		sdl3::Window &win = *ctx->m_window;
		if (s.position.IsSome()) {
			win.SetPosition(s.position.Value().x, s.position.Value().y);
		} else if (s.display.IsSome()) {
			const std::vector<SDL_DisplayID> displays = sdl3::GetDisplays();
			if (s.display.Value() < displays.size()) {
				const SDL_DisplayID display = displays[s.display.Value()];
				win.SetPosition(int(SDL_WINDOWPOS_CENTERED_DISPLAY(display)), int(SDL_WINDOWPOS_CENTERED_DISPLAY(display)));
			}
		}
		// Placement asynchrone chez certains pilotes : appliqué avant la suite
		// (capture du bureau, positions relues).
		(void)SDL_SyncWindow(win.Get());
		if (s.renderDriver == "gpu") {
			if (Result<bool, String> gpu = EnsureGpuDevice(); gpu.IsOk()) {
				if (auto r = sdl3::Renderer::CreateOnGpuDevice(m_gpu, win); r) {
					ctx->m_renderer = std::make_unique<sdl3::Renderer>(std::move(r.Value()));
					ctx->m_sharesGpu = true;
				}
			}
			// Pas de GPU (pilote absent, Xvfb sans Vulkan) : renderer par défaut,
			// l'interface 2D fonctionne, sans vue 3D DIRECT partagée.
			if (!ctx->m_renderer)
				if (auto r = sdl3::Renderer::Create(win); r)
					ctx->m_renderer = std::make_unique<sdl3::Renderer>(std::move(r.Value()));
		} else if (auto r = sdl3::Renderer::Create(win, s.renderDriver.IsEmpty() ? nullptr : s.renderDriver.CStr()); r) {
			ctx->m_renderer = std::make_unique<sdl3::Renderer>(std::move(r.Value()));
		}
		if (!ctx->m_renderer)
			return Err(String("renderer de « ") + s.title + String(" » : ") + String(SDL_GetError()));
		(void)ctx->m_renderer->SetVSync(s.vsync ? sdl3::Renderer::VSync::EVERY_REFRESH : sdl3::Renderer::VSync::DISABLED);
		ctx->m_routingId = win.GetId();
		ctx->m_gui = std::make_unique<Ui>(ctx->m_world, win, *ctx->m_renderer, s.theme);
	}

	for (const String &path : s.styleSheets)
		if (Result<bool, String> r = ctx->m_gui->LoadStyleSheet(path); r.IsError())
			return Err(String("feuille ") + path + String(" : ") + r.Error());
	if (!s.css.IsEmpty())
		ctx->m_gui->AddStyleSheet(s.css.View());
	if (ctx->m_window) {
		if (s.followDisplayScale)
			ctx->SetContentScale(ctx->m_window->DisplayScale());
		ctx->m_window->Show();
		(void)SDL_SyncWindow(ctx->m_window->Get());
		ctx->m_desktopPosition = s.position.IsSome() ? s.position.Value() : ctx->m_window->GetPosition();
	}

	WindowContext *out = ctx.get();
	m_windows.push_back(std::move(ctx));
	if (m_active == 0)
		m_active = id;
	return Ok(out);
}

Result<WindowContext *, String> WindowManager::CreateOnDisplay(size_t displayIndex, WindowSpec spec) {
	spec.position = NONE;
	spec.display = Some(displayIndex);
	return Create(std::move(spec));
}

bool WindowManager::Close(WindowId id) {
	auto it = std::find_if(m_windows.begin(), m_windows.end(), [id](const auto &w) { return w->Id() == id; });
	if (it == m_windows.end())
		return false;
	// Le contexte est retiré du registre AVANT onClose : un rappel qui
	// parcourt Windows() ne retrouve pas la fenêtre en cours de fermeture.
	std::unique_ptr<WindowContext> closing = std::move(*it);
	m_windows.erase(it);
	m_captures.erase(std::remove_if(m_captures.begin(), m_captures.end(),
									[id](const PendingCapture &c) { return c.id == id; }),
					 m_captures.end());
	if (closing->onClose)
		closing->onClose(*closing);
	closing.reset();
	if (m_active == id)
		m_active = m_windows.empty() ? 0 : m_windows.front()->Id();
	return true;
}

bool WindowManager::Focus(WindowId id) {
	WindowContext *ctx = Find(id);
	if (!ctx)
		return false;
	m_active = id;
	if (ctx->m_window)
		ctx->m_window->Raise();
	return true;
}

WindowContext *WindowManager::Find(WindowId id) noexcept {
	for (const auto &w : m_windows)
		if (w->Id() == id)
			return w.get();
	return nullptr;
}

WindowContext *WindowManager::FindByRole(StringView role) noexcept {
	for (const auto &w : m_windows)
		if (w->Role().View() == role)
			return w.get();
	return nullptr;
}

WindowContext *WindowManager::FindByRoutingId(SDL_WindowID routingId) noexcept {
	if (routingId == 0)
		return nullptr;
	for (const auto &w : m_windows)
		if (w->RoutingId() == routingId)
			return w.get();
	return nullptr;
}

std::vector<WindowContext *> WindowManager::Windows() {
	std::vector<WindowContext *> out;
	out.reserve(m_windows.size());
	for (const auto &w : m_windows)
		out.push_back(w.get());
	return out;
}

WindowContext *WindowManager::ActiveWindow() noexcept {
	if (WindowContext *active = Find(m_active))
		return active;
	for (const auto &w : m_windows)
		if (w->IsPrimary())
			return w.get();
	return m_windows.empty() ? nullptr : m_windows.front().get();
}

void WindowManager::AddGlobalShortcut(GlobalShortcut shortcut) {
	shortcut.mods = NormalizeMods(shortcut.mods);
	(void)RemoveGlobalShortcut(shortcut.name.View());
	m_shortcuts.push_back(std::move(shortcut));
}

bool WindowManager::RemoveGlobalShortcut(StringView name) {
	if (name.IsEmpty())
		return false;
	const size_t before = m_shortcuts.size();
	std::erase_if(m_shortcuts, [name](const GlobalShortcut &s) { return s.name.View() == name; });
	return m_shortcuts.size() != before;
}

WindowContext *WindowManager::Target(const sdl3::Event &ev) {
	if (WindowContext *target = FindByRoutingId(EventWindowId(ev)))
		return target;
	// Évènement sans fenêtre (manette, joystick…) ou fenêtre inconnue : la
	// fenêtre active. Un évènement de fenêtre inconnue n'est pas redistribué.
	const uint32_t type = ev.raw.type;
	if (type >= SDL_EVENT_WINDOW_FIRST && type <= SDL_EVENT_WINDOW_LAST)
		return nullptr;
	return ActiveWindow();
}

bool WindowManager::TriggerShortcut(sdl3::Event &ev, WindowContext &origin, DispatchResult *out) {
	if (ev.raw.type != SDL_EVENT_KEY_DOWN || ev.raw.key.repeat)
		return false;
	if (origin.TextFieldHasFocus() && !DefaultKeyPassThrough(ev))
		return false;
	const SDL_Keymod mods = NormalizeMods(ev.raw.key.mod);
	for (GlobalShortcut &shortcut : m_shortcuts) {
		if (shortcut.key != ev.raw.key.key || shortcut.mods != mods)
			continue;
		ev.consumed = true;
		if (out) {
			out->consumed = true;
			out->shortcut = shortcut.name;
		}
		if (shortcut.action)
			shortcut.action(origin);
		return true;
	}
	return false;
}

WindowContext *WindowManager::Observe(const sdl3::Event &ev) {
	const uint32_t type = ev.raw.type;
	if (type == SDL_EVENT_QUIT) {
		m_quit = true;
		return nullptr;
	}
	WindowContext *target = Target(ev);
	if (!target)
		return nullptr;
	switch (type) {
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		m_active = target->Id();
		break;
	case SDL_EVENT_WINDOW_MOVED:
		target->m_desktopPosition = {ev.raw.window.data1, ev.raw.window.data2};
		break;
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		target->RequestClose();
		break;
	case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
	case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
		if (target->m_window && target->Spec().followDisplayScale)
			target->SetContentScale(target->m_window->DisplayScale());
		break;
	default:
		break;
	}
	return target;
}

DispatchResult WindowManager::Dispatch(sdl3::Event &ev) {
	DispatchResult out;
	const uint32_t type = ev.raw.type;
	WindowContext *target = Observe(ev);
	out.target = target;
	if (type == SDL_EVENT_QUIT || type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
		out.consumed = true;
		return out;
	}
	if (!target)
		return out;
	if (TriggerShortcut(ev, *target, &out))
		return out;
	// Pointeur en points de la fenêtre → unités logiques de l'interface.
	if (target->m_renderer && target->ContentScale() != 1.f)
		(void)SDL_ConvertEventToRenderCoordinates(target->m_renderer->Get(), &ev.raw);
	out.consumed = target->m_gui->HandleEvent(ev);
	return out;
}

size_t WindowManager::HandleCloseRequests() {
	std::vector<WindowId> closing;
	for (const auto &w : m_windows)
		if (w->CloseRequested())
			closing.push_back(w->Id());
	size_t closed = 0;
	for (WindowId id : closing) {
		WindowContext *w = Find(id);
		if (!w)
			continue;
		if (w->IsPrimary() || m_windows.size() == 1) {
			// La principale reste ouverte : l'hôte décide (sauvegarde, confirmation).
			m_quit = true;
			w->ClearCloseRequest();
			continue;
		}
		closed += Close(id) ? 1u : 0u;
	}
	return closed;
}

void WindowManager::Update(float dt) {
	for (const auto &w : m_windows)
		w->m_gui->Update(dt);
}

void WindowManager::RequestCapture(WindowId id, String path) { m_captures.push_back({id, std::move(path)}); }

void WindowManager::RequestDesktopCapture(String path) { m_desktopCaptures.push_back(std::move(path)); }

void WindowManager::Render(sdl3::FColor clear, const std::function<void(WindowContext &)> &draw) {
	Draw(clear, draw);
	Present();
}

void WindowManager::Draw(sdl3::FColor clear, const std::function<void(WindowContext &)> &draw) {
	m_captureErrors.clear();
	const bool wantDesktop = !m_desktopCaptures.empty();
	for (const auto &w : m_windows) {
		if (!w->m_renderer) {
			w->m_gui->Layout();
			continue;
		}
		sdl3::Renderer &renderer = *w->m_renderer;
		(void)renderer.SetDrawColor(clear);
		renderer.Clear();
		if (draw)
			draw(*w);
		w->m_gui->Render();
		const bool wantWindow = std::any_of(m_captures.begin(), m_captures.end(),
											[&](const PendingCapture &c) { return c.id == w->Id(); });
		if (!wantWindow && !wantDesktop)
			continue;
		// ReadPixels lit en pixels de sortie : échelle de rendu neutralisée.
		float sx = 1.f, sy = 1.f;
		(void)SDL_GetRenderScale(renderer.Get(), &sx, &sy);
		(void)renderer.SetScale(1.f, 1.f);
		auto surface = renderer.ReadPixels();
		(void)renderer.SetScale(sx, sy);
		SDL_Surface *rgba = surface ? ToRgba(surface.Value()) : nullptr;
		if (!rgba) {
			m_captureErrors.push_back(String("capture de « ") + w->Spec().title + String(" » : ") + String(SDL_GetError()));
			continue;
		}
		for (const PendingCapture &c : m_captures)
			if (c.id == w->Id())
				if (Result<bool, String> r = SavePng(rgba, c.path); r.IsError())
					m_captureErrors.push_back(c.path + String(" : ") + r.Error());
		if (wantDesktop)
			m_desktopShots.push_back({rgba, w->m_desktopPosition});
		else
			SDL_DestroySurface(rgba);
	}
	m_captures.clear();
	if (wantDesktop)
		ComposeDesktop();
}

void WindowManager::Present() {
	for (const auto &w : m_windows)
		if (w->m_renderer)
			w->m_renderer->Present();
}

void WindowManager::ComposeDesktop() {
	// Boîte englobante des fenêtres dans le bureau virtuel.
	int minX = 0, minY = 0, maxX = 1, maxY = 1;
	bool first = true;
	for (const Shot &shot : m_desktopShots) {
		const int x0 = shot.position.x, y0 = shot.position.y;
		const int x1 = x0 + shot.pixels->w, y1 = y0 + shot.pixels->h;
		minX = first ? x0 : std::min(minX, x0);
		minY = first ? y0 : std::min(minY, y0);
		maxX = first ? x1 : std::max(maxX, x1);
		maxY = first ? y1 : std::max(maxY, y1);
		first = false;
	}
	SDL_Surface *canvas = SDL_CreateSurface(std::max(1, maxX - minX), std::max(1, maxY - minY), SDL_PIXELFORMAT_RGBA32);
	if (canvas) {
		(void)SDL_FillSurfaceRect(canvas, nullptr, SDL_MapSurfaceRGBA(canvas, 0, 0, 0, 255));
		for (const Shot &shot : m_desktopShots) {
			SDL_Rect at{shot.position.x - minX, shot.position.y - minY, shot.pixels->w, shot.pixels->h};
			(void)SDL_BlitSurface(shot.pixels, nullptr, canvas, &at);
		}
		for (const String &path : m_desktopCaptures)
			if (Result<bool, String> r = SavePng(canvas, path); r.IsError())
				m_captureErrors.push_back(path + String(" : ") + r.Error());
		SDL_DestroySurface(canvas);
	} else {
		m_captureErrors.push_back(String("capture du bureau : ") + String(SDL_GetError()));
	}
	for (const Shot &shot : m_desktopShots)
		SDL_DestroySurface(shot.pixels);
	m_desktopShots.clear();
	m_desktopCaptures.clear();
}

} // namespace ui
