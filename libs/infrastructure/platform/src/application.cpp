// Définitions de platform/application.hpp
#include <platform/application.hpp>
#include <platform/sdl_log.hpp>

#include <diagnostics/log.hpp>
#include <sdl3/time.hpp>

namespace platform {

namespace {
std::string_view View(StringView s) { return {s.GetData(), s.GetSize()}; }
} // namespace

int Application::Run(const AppConfig &config) {
	RouteSdlLog();
	m_clock = FixedStepClock(config.clock);
	m_frames = 0;

	sdl3::SdlContext sdl(config.headless ? sdl3::init_flags::EVENTS : sdl3::init_flags::VIDEO);
	if (!sdl) {
		DIAG_CRITICAL("platform", "SDL_Init : {}", View(sdl3::GetError()));
		return 1;
	}
	if (!config.headless) {
		SDL_WindowFlags flags = config.resizable ? SDL_WINDOW_RESIZABLE : 0;
		auto window = sdl3::Window::Create(String(config.title.c_str()), config.width, config.height, flags);
		if (!window) {
			DIAG_CRITICAL("platform", "création de la fenêtre : {}", View(window.Error()));
			return 1;
		}
		m_window = std::move(window.Value());
		auto renderer = sdl3::Renderer::Create(m_window);
		if (!renderer) {
			DIAG_CRITICAL("platform", "création du rendu : {}", View(renderer.Error()));
			return 1;
		}
		m_renderer = std::move(renderer.Value());
		SDL_SetRenderVSync(m_renderer.Get(), config.vsync ? 1 : 0);
	}

	m_quitConnection = m_events.Subscribe<QuitRequested>([this](const QuitRequested &) { RequestQuit(); });
	m_running = true;
	if (!OnInit()) {
		DIAG_ERROR("platform", "OnInit a échoué : arrêt");
		m_running = false;
		m_renderer = {};
		m_window = {};
		return 1;
	}

	uint64_t last = sdl3::GetTicksNS();
	while (m_running) {
		const uint64_t now = sdl3::GetTicksNS();
		const double frameSeconds = double(now - last) / 1e9;
		last = now;

		m_input.BeginFrame();
		PumpEvents();
		m_events.Dispatch();

		const int steps = m_clock.Advance(frameSeconds);
		for (int i = 0; i < steps; ++i)
			OnFixedUpdate(m_clock.StepSeconds());
		OnUpdate(frameSeconds);
		if (!config.headless)
			OnRender(m_clock.Alpha());

		++m_frames;
		if (config.maxFrames && m_frames >= config.maxFrames)
			m_running = false;
	}

	OnShutdown();
	m_renderer = {};
	m_window = {};
	return 0;
}

void Application::PumpEvents() {
	while (auto event = sdl3::PollEvent()) {
		OnEvent(*event);
		TranslateEvent(*event, m_input, m_events);
	}
}

} // namespace platform
