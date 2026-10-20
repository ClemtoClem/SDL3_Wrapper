#pragma once
/**
 * platform::Application — cycle de vie d'une application SDL : initialisation,
 * fenêtre, boucle d'événements, simulation à pas fixe, rendu, arrêt.
 *
 * @code{.cpp}
 * class MyGame : public platform::Application {
 *     bool OnInit() override { ...; return true; }
 *     void OnFixedUpdate(double dt) override { world.Step(dt); }
 *     void OnRender(double alpha) override {
 *         GetRenderer().SetDrawColor(sdl3::Color::BLACK());
 *         GetRenderer().Clear();
 *         ...
 *     }
 * };
 *
 * int main() {
 *     MyGame game;
 *     return game.Run({.title = "Mon jeu", .width = 1280, .height = 720});
 * }
 * @endcode
 *
 * Les événements SDL sont traduits en événements de plate-forme (events.hpp)
 * publiés sur GetEvents() ; GetInput() donne l'état clavier/souris de l'image.
 * Le journal SDL est redirigé vers diagnostics::Logger::Global() (catégorie
 * "sdl"). Les applications sans fenêtre (outils, serveurs, tests) peuvent
 * passer `headless = true` : la boucle tourne sans Window ni Renderer.
 */
#include <cstdint>
#include <string>

#include <core/core.hpp>
#include <events/event_bus.hpp>
#include <sdl3/render.hpp>
#include <sdl3/sdl_context.hpp>

#include "clock.hpp"
#include "events.hpp"
#include "input_state.hpp"

namespace platform {

struct AppConfig {
	std::string title = "Application";
	int width = 1280;
	int height = 720;
	bool resizable = true;
	bool vsync = true;
	/// Aucune fenêtre ni rendu : OnRender n'est pas appelé.
	bool headless = false;
	/// 0 = illimité ; sinon la boucle s'arrête après ce nombre d'images
	/// (exécutions automatisées, captures, tests de fumée).
	uint64_t maxFrames = 0;
	FixedStepClock::Config clock{};
};

class Application {
public:
	Application() = default;
	virtual ~Application() = default;
	Application(const Application &) = delete;
	Application &operator=(const Application &) = delete;

	/// Initialise SDL et la fenêtre, exécute la boucle, puis arrête tout.
	/// Code de sortie : 0 si tout s'est bien passé, 1 si l'initialisation a échoué.
	int Run(const AppConfig &config = {});

	/// Demande l'arrêt à la fin de l'image courante.
	void RequestQuit() noexcept { m_running = false; }
	[[nodiscard]] bool IsRunning() const noexcept { return m_running; }

	[[nodiscard]] events::EventBus &GetEvents() noexcept { return m_events; }
	[[nodiscard]] const InputState &GetInput() const noexcept { return m_input; }
	[[nodiscard]] const FixedStepClock &GetClock() const noexcept { return m_clock; }
	[[nodiscard]] uint64_t FrameCount() const noexcept { return m_frames; }
	/// Fenêtre / rendu : invalides en mode headless.
	[[nodiscard]] sdl3::Window &GetWindow() noexcept { return m_window; }
	[[nodiscard]] sdl3::Renderer &GetRenderer() noexcept { return m_renderer; }

protected:
	/// Après création de la fenêtre ; faux = abandon (Run rend 1).
	virtual bool OnInit() { return true; }
	/// Chaque événement SDL brut, AVANT traduction (pour l'UI, par exemple).
	virtual void OnEvent(const sdl3::Event &) {}
	/// Simulation, appelée 0..n fois par image avec un pas constant.
	virtual void OnFixedUpdate(double /*stepSeconds*/) {}
	/// Une fois par image, avec la durée réelle de l'image.
	virtual void OnUpdate(double /*frameSeconds*/) {}
	/// Rendu ; `alpha` = fraction du pas fixe suivant (interpolation).
	virtual void OnRender(double /*alpha*/) {}
	/// Avant destruction de la fenêtre.
	virtual void OnShutdown() {}

private:
	void PumpEvents();

	events::EventBus m_events;
	InputState m_input;
	FixedStepClock m_clock;
	sdl3::Window m_window;
	sdl3::Renderer m_renderer;
	events::ScopedConnection m_quitConnection;
	uint64_t m_frames = 0;
	bool m_running = false;
};

} // namespace platform
