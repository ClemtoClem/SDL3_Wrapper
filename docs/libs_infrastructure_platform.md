# platform — cycle de vie d'une application

`libs/infrastructure/platform` · couche **infrastructure** · `libplatform.a` · espace de noms `platform::`

## Présentation

`platform` fournit le squelette d'une application SDL :

- l'initialisation et la fenêtre ;
- la boucle d'évènements ;
- une simulation **à pas fixe**, découplée de la cadence d'affichage ;
- le rendu interpolé et l'arrêt.

Les évènements SDL sont traduits en évènements simples, publiés sur un
[`events::EventBus`](libs_foundation_events.md). Le journal de SDL est
redirigé vers [`diagnostics::Logger`](libs_foundation_diagnostics.md).

La logique est séparée de SDL : `FixedStepClock`, `InputState` et
`TranslateEvent` sont testables sans fenêtre ni `SDL_Init`.

Dans le dépôt, ce module est utilisé par l'exemple
[platform_app](example_platform_app.md). Les grandes applications (sandbox,
voxelia, émulateur) ont leur propre boucle, construite sur
[`ui::WindowManager`](libs_infrastructure_ui.md).

## Architecture interne

| Fichier | Contenu |
|---|---|
| `platform.hpp` | En-tête parapluie |
| `clock.hpp`, `src/clock.cpp` | `FixedStepClock` (logique pure) |
| `input_state.hpp`, `src/input_state.cpp` | `InputState`, `MouseButton` (logique pure) |
| `events.hpp`, `src/events.cpp` | Évènements de plate-forme et `TranslateEvent` |
| `sdl_log.hpp`, `src/sdl_log.cpp` | Pont `SDL_Log` → `diagnostics::Logger` |
| `application.hpp`, `src/application.cpp` | `AppConfig`, `Application` |

Dépendances : `core`, `sdl3`, `events`, `diagnostics`.

## API publique

### `AppConfig`

| Champ | Défaut | Rôle |
|---|---|---|
| `title` | `"Application"` | Titre de la fenêtre |
| `width`, `height` | 1280 × 720 | Taille de la fenêtre |
| `resizable`, `vsync` | `true`, `true` | Options de fenêtre et de rendu |
| `headless` | `false` | Aucune fenêtre ni rendu : `OnRender` n'est pas appelé (outils, serveurs, tests) |
| `maxFrames` | 0 | 0 = illimité ; sinon la boucle s'arrête après N images |
| `clock` | `{1/60 s, 0,25 s, 8}` | Configuration du `FixedStepClock` |

### `Application`

Classe à dériver. `int Run(const AppConfig&)` initialise, exécute la boucle,
puis arrête tout. Elle rend 0, ou 1 si l'initialisation a échoué.

| Point d'extension (virtuel) | Appel |
|---|---|
| `bool OnInit()` | Après la création de la fenêtre ; `false` abandonne |
| `void OnEvent(const sdl3::Event&)` | Chaque évènement SDL brut, **avant** traduction (pour brancher une UI) |
| `void OnFixedUpdate(double pas)` | 0 à n fois par image, avec un pas constant |
| `void OnUpdate(double durée)` | Une fois par image, avec la durée réelle |
| `void OnRender(double alpha)` | Rendu ; `alpha` est la fraction du pas suivant, pour interpoler |
| `void OnShutdown()` | Avant la destruction de la fenêtre |

Accesseurs :

- `GetEvents()`, `GetInput()`, `GetClock()` ;
- `GetWindow()`, `GetRenderer()` (invalides en mode `headless`) ;
- `FrameCount()`, `IsRunning()`, `RequestQuit()`.

**Déroulement d'une image** :

1. `InputState::BeginFrame()` ;
2. lecture des évènements SDL (`OnEvent`, puis `TranslateEvent`) ;
3. `EventBus::Dispatch()` ;
4. `FixedStepClock::Advance()`, puis n × `OnFixedUpdate` ;
5. `OnUpdate`, puis `OnRender`.

`QuitRequested` termine la boucle.

### `FixedStepClock`

Cette classe applique la méthode « Fix Your Timestep ».

- `int Advance(secondesRéelles)` rend le nombre de pas fixes à simuler.
- `Alpha()` rend la fraction de pas déjà écoulée, pour l'interpolation.
- Deux bornes évitent la « spirale de la mort » :
  - un temps d'image aberrant (débogueur, fenêtre déplacée) est plafonné à
    `maxFrameTime` ;
  - le nombre de pas par image est plafonné à `maxStepsPerFrame`.
- Lecture : `StepSeconds`, `SimulatedSeconds`, `TotalSteps`,
  `DroppedSeconds` (temps ignoré au-delà des bornes), `Reset`.

### `InputState`

État du clavier et de la souris, avec les transitions **de l'image courante**.

- **Alimentation** :
  - `BeginFrame()` en début d'image ;
  - `OnKey(scancode, down)`, `OnMouseButton`, `OnMouseMove`,
    `OnMouseWheel` ;
  - `ReleaseAll()` à la perte du focus.
- **Lecture** : `IsDown`, `WasPressed`, `WasReleased` (scancode ou
  `MouseButton`), `MouseX/Y`, `MouseDeltaX/Y`, `WheelX/Y`.

Un test peut simuler une frappe par `OnKey(scancode, true)` puis vérifier
`WasPressed`.

### Évènements de plate-forme

Ce sont des structures simples publiées sur le bus :

- `QuitRequested`, `WindowResized {width, height}`, `FocusChanged {focused}` ;
- `KeyChanged {scancode, keycode, down, repeat}` ;
- `MouseButtonChanged {button, down, x, y, clicks}`,
  `MouseMoved {x, y, dx, dy}`, `MouseWheelMoved {dx, dy}` ;
- `TextEntered {text}`.

`bool TranslateEvent(const sdl3::Event&, InputState&, events::EventBus&)` met
l'état à jour et publie l'évènement correspondant. Elle n'appelle aucune
fonction SDL, et rend faux pour un type non traduit.

### Journal SDL

- `size_t RouteSdlLog(diagnostics::Logger& = Global())` installe le pont. On
  peut l'appeler plusieurs fois sans effet supplémentaire. Les messages SDL
  arrivent dans la catégorie `sdl.<catégorie>`.
- `ToDiagnosticsLevel(sdl3::LogPriority)` convertit une priorité SDL en
  niveau du journal.
- `Application::Run` installe ce pont automatiquement.

## Exemple

Extrait de [`examples/platform_app/src/main.cpp`](../examples/platform_app/src/main.cpp) :

```cpp
#include <diagnostics/diagnostics.hpp>
#include <platform/platform.hpp>

class BouncingSquare final : public platform::Application {
	float m_x = 100.f, m_prevX = 100.f, m_vx = 240.f;
	events::ScopedConnection m_keys;

protected:
	bool OnInit() override {
		m_keys = GetEvents().Subscribe<platform::KeyChanged>([this](const platform::KeyChanged &k) {
			if (k.down && k.scancode == SDL_SCANCODE_ESCAPE)
				RequestQuit();
		});
		return true;
	}
	void OnFixedUpdate(double dt) override {
		m_prevX = m_x;
		m_x += m_vx * float(dt);
		if (m_x < 0.f || m_x + 60.f > float(GetWindow().GetSize().x))
			m_vx = -m_vx;
	}
	void OnRender(double alpha) override {
		sdl3::Renderer &r = GetRenderer();
		r.SetDrawColor(sdl3::Color{18, 22, 30, 255});
		r.Clear();
		const float x = m_prevX + (m_x - m_prevX) * float(alpha); // interpolation
		r.SetDrawColor(sdl3::Color{80, 170, 240, 255});
		r.FillRect(sdl3::FRect{x, 100.f, 60.f, 60.f});
		r.Present();
	}
};

int main() {
	diagnostics::Logger::Global().AddSink(diagnostics::ConsoleSink());
	platform::AppConfig config{.title = "platform_app", .width = 800, .height = 600};
	config.clock.stepSeconds = 1.0 / 120.0; // simulation à 120 Hz
	BouncingSquare app;
	return app.Run(config);
}
```

## Intégration

`modules = ["platform"]`. Ajoutez un sink au `diagnostics::Logger` pour voir
les messages, y compris ceux de SDL.

## Tests

```sh
cpp-maker test -m platform
```

Les tests couvrent l'horloge à pas fixe, `InputState` et `TranslateEvent`
sur des évènements construits à la main.
