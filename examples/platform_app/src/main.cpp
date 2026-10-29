/**
 * platform_app — une application complète en ~80 lignes sur platform::.
 *
 * Un carré rebondit dans la fenêtre (simulation à 120 Hz, rendu interpolé) ;
 * Espace change sa couleur, Échap quitte. Les touches arrivent par le bus
 * d'événements (platform::KeyChanged), les mesures par diagnostics::.
 *
 *   ./cpp-maker run platform_app
 *   ./cpp-maker run platform_app -- --frames=300     # s'arrête seul
 */
#include <cstdlib>
#include <cstring>

#include <diagnostics/diagnostics.hpp>
#include <platform/platform.hpp>

namespace {

class BouncingSquare final : public platform::Application {
	float m_x = 100.f, m_y = 100.f, m_prevX = 100.f, m_prevY = 100.f;
	float m_vx = 240.f, m_vy = 180.f;
	bool m_warm = false;
	events::ScopedConnection m_keys;

protected:
	bool OnInit() override {
		m_keys = GetEvents().Subscribe<platform::KeyChanged>([this](const platform::KeyChanged &k) {
			if (k.down && k.scancode == SDL_SCANCODE_ESCAPE)
				RequestQuit();
			if (k.down && !k.repeat && k.scancode == SDL_SCANCODE_SPACE)
				m_warm = !m_warm;
		});
		DIAG_INFO("app", "prêt — Espace : couleur, Échap : quitter");
		return true;
	}

	void OnFixedUpdate(double dt) override {
		DIAG_PROFILE_SCOPE("app.simulate");
		m_prevX = m_x;
		m_prevY = m_y;
		const sdl3::Point size = GetWindow().GetSize();
		m_x += m_vx * float(dt);
		m_y += m_vy * float(dt);
		if (m_x < 0.f || m_x + 60.f > float(size.x)) m_vx = -m_vx;
		if (m_y < 0.f || m_y + 60.f > float(size.y)) m_vy = -m_vy;
	}

	void OnRender(double alpha) override {
		sdl3::Renderer &r = GetRenderer();
		r.SetDrawColor(sdl3::Color{18, 22, 30, 255});
		r.Clear();
		const float a = float(alpha);
		const float x = m_prevX + (m_x - m_prevX) * a, y = m_prevY + (m_y - m_prevY) * a;
		r.SetDrawColor(m_warm ? sdl3::Color{240, 140, 60, 255} : sdl3::Color{80, 170, 240, 255});
		SDL_FRect rect{x, y, 60.f, 60.f};
		SDL_RenderFillRect(r.Get(), &rect);
		r.Present();
	}

	void OnShutdown() override {
		const auto sim = diagnostics::Profiler::Global().Timing("app.simulate");
		DIAG_INFO("app", "{} images, {} pas simulés ({:.4f} ms/pas)", FrameCount(), sim.count, sim.MeanMs());
	}
};

} // namespace

int main(int argc, char **argv) {
	diagnostics::Logger::Global().AddSink(diagnostics::ConsoleSink());
	platform::AppConfig config{.title = "platform_app", .width = 800, .height = 600};
	config.clock.stepSeconds = 1.0 / 120.0;
	for (int i = 1; i < argc; ++i)
		if (std::strncmp(argv[i], "--frames=", 9) == 0)
			config.maxFrames = std::strtoull(argv[i] + 9, nullptr, 10);
	BouncingSquare app;
	return app.Run(config);
}
