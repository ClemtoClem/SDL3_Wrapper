// Définitions de app.hpp — fenêtre, interface, commandes et son du jeu.
#include <voxelgame/app.hpp>
#include <voxelgame/world_renderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>

#include <audio/clip.hpp>
#include <audio/engine.hpp>
#include <ecs/events.hpp>
#include <render3d/canvas.hpp>
#include <render3d/shader_compiler.hpp>
#include <sdl3/image.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>
#include <ui/viewport3d.hpp>
#include <voxel/json.hpp>
#include <voxel/scripting.hpp>

namespace voxelgame {

using namespace voxel;

// ── Options ──────────────────────────────────────────────────────────────────

const char *Usage() {
	return "voxelgame — jeu voxel piloté par les données (mods JSON + scripts)\n"
		   "\n"
		   "  --world=NOM            monde à ouvrir ou créer (défaut : Monde)\n"
		   "  --seed=N               graine d'un nouveau monde\n"
		   "  --generator=ID         générateur d'un nouveau monde (core:overworld, core:flat, core:islands…)\n"
		   "  --mods=DIR             dossier de mods (répétable ; défaut : demos/voxelgame/mods)\n"
		   "  --saves=DIR            dossier des sauvegardes (défaut : data/voxelgame/saves)\n"
		   "  --view=N               distance d'affichage en chunks (défaut : 6)\n"
		   "  --creative             mode créatif (blocs infinis, casse instantanée)\n"
		   "  --frames=N             quitte après N images\n"
		   "  --screenshot=N:FICHIER capture l'image N (répétable)\n"
		   "  --exec=N:/commande     exécute une commande de console à l'image N (répétable)\n"
		   "  --headless             sans fenêtre : simulation, commandes, rapport\n"
		   "  --width=W --height=H   taille de la fenêtre\n"
		   "\n"
		   "Commandes : ZQSD/WASD (touches physiques), souris, Espace saut, Maj accroupi,\n"
		   "Ctrl course, F vol, clic gauche casser/frapper, clic droit poser/utiliser,\n"
		   "clic milieu choisir le bloc visé, A jeter, 1-9/molette objet, E inventaire,\n"
		   "T ou / console, F3 informations, F5 enregistrer, Échap menu.\n";
}

Option<String> ParseOptions(int argc, char **argv, Options &o) {
	for (int i = 1; i < argc; ++i) {
		const String arg(argv[i]);
		auto value = [&](const char *name) -> Option<String> {
			const String prefix = String(name) + String("=");
			return arg.StartsWith(prefix.CStr()) ? Some(arg.Substring(prefix.GetSize())) : Option<String>(NONE);
		};
		auto framed = [&](const String &text, std::vector<std::pair<long, String>> &out) -> bool {
			const size_t colon = text.Find(":");
			if (colon == String::NPOS)
				return false;
			out.push_back({std::atol(text.Substring(0, colon).CStr()), text.Substring(colon + 1)});
			return true;
		};
		if (arg == "--help" || arg == "-h")
			o.help = true;
		else if (arg == "--creative")
			o.creative = true;
		else if (arg == "--headless")
			o.headless = true;
		else if (auto v = value("--world"); v.IsSome())
			o.world = v.Unwrap();
		else if (auto v = value("--seed"); v.IsSome())
			o.seed = Some(uint32_t(std::strtoul(v.Unwrap().CStr(), nullptr, 10)));
		else if (auto v = value("--generator"); v.IsSome())
			o.generator = v.Unwrap();
		else if (auto v = value("--mods"); v.IsSome())
			o.mods.push_back(v.Unwrap());
		else if (auto v = value("--saves"); v.IsSome())
			o.saves = v.Unwrap();
		else if (auto v = value("--view"); v.IsSome())
			o.view = std::clamp(std::atoi(v.Unwrap().CStr()), 2, 16);
		else if (auto v = value("--frames"); v.IsSome())
			o.frames = std::atol(v.Unwrap().CStr());
		else if (auto v = value("--width"); v.IsSome())
			o.width = std::max(320, std::atoi(v.Unwrap().CStr()));
		else if (auto v = value("--height"); v.IsSome())
			o.height = std::max(240, std::atoi(v.Unwrap().CStr()));
		else if (auto v = value("--screenshot"); v.IsSome()) {
			if (!framed(v.Unwrap(), o.screenshots))
				return Some(String("--screenshot=N:FICHIER attendu"));
		} else if (auto v = value("--exec"); v.IsSome()) {
			if (!framed(v.Unwrap(), o.commands))
				return Some(String("--exec=N:/commande attendu"));
		} else
			return Some(String::Format("option inconnue « %s »", arg.CStr()));
	}
	if (o.mods.empty())
		o.mods.push_back(String("demos/voxelgame/mods"));
	return NONE;
}

App::App(Options options) : m_options(std::move(options)) {}
App::~App() = default;

void App::LoadGame(Game &game) {
	GameOptions options;
	options.modDirectories = m_options.mods;
	options.savesDirectory = m_options.saves;
	options.viewRadius = m_options.view;
	options.verticalRadius = std::max(2, m_options.view / 2);
	options.creative = m_options.creative;
	const ModReport report = game.LoadMods(options);
	for (const ModInfo &mod : report.mods)
		std::printf("mod %s %s : %d définition(s), %d script(s)\n", mod.id.CStr(), mod.version.CStr(), mod.definitions,
					int(mod.scripts.size()));
	for (const String &e : report.errors)
		std::printf("[erreur] %s\n", e.CStr());
	for (const String &w : report.warnings)
		std::printf("[avertissement] %s\n", w.CStr());
	// Shaders des mods : chargés et compilés (GLSL → SPIR-V) pour les valider.
	for (const ShaderDef *shader : game.Content().Shaders()) {
		for (const auto &[path, stage] : {std::pair{shader->vertex, sdl3::gpu_shader_stage::VERTEX},
										  std::pair{shader->fragment, sdl3::gpu_shader_stage::FRAGMENT}}) {
			Result<String, String> source = script::LoadScriptFile(path);
			if (source.IsError()) {
				game.LogLine(String("[shader] ") + source.Error());
				continue;
			}
			auto spirv = render3d::CompileGlslToSpirv(source.Value().View(), stage, path.View());
			game.LogLine(spirv.IsOk() ? String::Format("[shader] %s : %s compilé (%d mots SPIR-V)", shader->id.CStr(), path.CStr(),
													  int(spirv.Value().size()))
									  : String::Format("[shader] %s : %s", shader->id.CStr(), spirv.Error().CStr()));
		}
	}
}

int App::Run() {
	if (m_options.help) {
		std::printf("%s", Usage());
		return 0;
	}
	Game game;
	LoadGame(game);
	if (!game.Mods().errors.empty() && game.Content().BlockCount() <= 1) {
		std::printf("aucun contenu chargé : vérifiez --mods\n");
		return 1;
	}
	Result<bool, String> opened = game.OpenWorld(m_options.world, m_options.seed, m_options.generator);
	if (opened.IsError()) {
		std::printf("monde « %s » : %s\n", m_options.world.CStr(), opened.Error().CStr());
		return 1;
	}
	const int code = m_options.headless ? RunHeadless(game) : RunWindowed(game);
	(void)game.Save();
	game.CloseWorld();
	return code;
}

int App::RunHeadless(Game &game) {
	const ecs::Entity player = game.JoinPlayer("Joueur");
	const long frames = m_options.frames > 0 ? m_options.frames : 600;
	uint64_t messages = 0;
	for (long frame = 1; frame <= frames; ++frame) {
		for (const auto &[at, command] : m_options.commands)
			if (at == frame) {
				Result<String, String> r = game.Execute(command, player);
				std::printf("%s → %s\n", command.CStr(), r.IsOk() ? r.Value().CStr() : r.Error().CStr());
			}
		game.Tick(1.f / 60.f);
		ecs::EventsOf<GameMessage>(game.Registry()).Read(messages, [](const GameMessage &m) { std::printf("[%s] %s\n", m.channel.CStr(), m.text.CStr()); });
	}
	auto t = game.Registry().GetComponent<Transform>(player);
	std::printf("%ld image(s) simulées — %d chunk(s) chargé(s), %d entité(s), joueur en %.1f %.1f %.1f\n", frames,
				int(game.GetWorld().LoadedChunkCount()), int(game.Registry().EntityCount()), double(t.Unwrap()->position.x),
				double(t.Unwrap()->position.y), double(t.Unwrap()->position.z));
	for (const String &line : game.Log())
		if (line.StartsWith("[script") || line.StartsWith("[erreur") || line.StartsWith("[shader"))
			std::printf("%s\n", line.CStr());
	return 0;
}

// ── Fenêtre de jeu ───────────────────────────────────────────────────────────

namespace {

constexpr float EYE = 1.62f;
constexpr float SLOT = 46.f;
constexpr int HOTBAR = 9;

sdl3::FColor Rgba(int r, int g, int b, int a = 255) { return sdl3::FColor{float(r) / 255.f, float(g) / 255.f, float(b) / 255.f, float(a) / 255.f}; }

class GameWindow {
public:
	GameWindow(Game &game, const Options &options, sdl3::Window &window, sdl3::Renderer &renderer, render3d::Canvas &canvas,
			   ui::Ui &gui, ecs::ArchetypeRegistry &uiWorld)
		: m_game(game), m_options(options), m_window(window), m_renderer(renderer), m_canvas(canvas), m_gui(gui),
		  m_ui(uiWorld), m_world(game, canvas), m_audio(game.Assets().Cache()) {}

	Result<bool, String> Initialize() {
		if (Result<bool, String> r = m_world.Initialize(); r.IsError())
			return r;
		m_world.rebuildBudget = 8;
		audio::RegisterResourceFactories(m_game.Assets().Cache());
		m_audio.Start();
		m_player = m_game.JoinPlayer("Joueur");
		Build();
		SetCaptured(true);
		return Ok(true);
	}

	[[nodiscard]] bool QuitRequested() const noexcept { return m_quit; }

	/// Commandes de l'application (--exec=N:!…) : « !look lacet tangage »
	/// (degrés), « !inventory », « !menu », « !console texte », « !debug ».
	void AppCommand(const String &line) {
		std::vector<String> words;
		size_t start = 0;
		while (start < line.GetSize()) {
			size_t end = line.Find(" ", start);
			if (end == String::NPOS)
				end = line.GetSize();
			if (end > start)
				words.push_back(line.Substring(start, end));
			start = end + 1;
		}
		if (words.empty())
			return;
		if (words[0] == "!look" && words.size() >= 3) {
			if (auto t = m_game.Registry().GetComponent<Transform>(m_player); t.IsSome()) {
				t.Unwrap()->yaw = float(std::atof(words[1].CStr())) * 3.14159265f / 180.f;
				t.Unwrap()->pitch = float(std::atof(words[2].CStr())) * 3.14159265f / 180.f;
			}
		} else if (words[0] == "!inventory") {
			ToggleInventory();
		} else if (words[0] == "!menu") {
			ToggleMenu();
		} else if (words[0] == "!debug") {
			m_debug = !m_debug;
		} else if (words[0] == "!console") {
			OpenConsole(line.Substring(std::min(line.GetSize(), size_t(9))));
			m_swallowText = false;
		}
	}

	void HandleEvent(sdl3::Event &e) {
		if (e.IsQuit()) {
			m_quit = true;
			return;
		}
		// La touche qui OUVRE la console ne doit pas s'y écrire.
		if (e.IsTextInput() && m_swallowText) {
			m_swallowText = false;
			return;
		}
		const bool consoleFocused = m_consoleOpen;
		if (consoleFocused && e.IsKeyDown(SDLK_TAB)) {
			Complete();
			return;
		}
		if (consoleFocused && (e.IsKeyDown(SDLK_UP) || e.IsKeyDown(SDLK_DOWN))) {
			BrowseHistory(e.IsKeyDown(SDLK_UP) ? -1 : 1);
			return;
		}
		m_gui.HandleEvent(e);
		if (e.IsKeyDown(SDLK_ESCAPE)) {
			if (m_consoleOpen)
				CloseConsole();
			else if (m_inventoryOpen)
				ToggleInventory();
			else
				ToggleMenu();
			return;
		}
		if (e.IsConsumed() || m_consoleOpen)
			return;
		if (m_menuOpen || m_inventoryOpen) {
			if (e.IsKeyDown(SDLK_E) && m_inventoryOpen)
				ToggleInventory();
			return;
		}
		// ── En jeu ──
		if (e.IsMouseMotion() && m_captured) {
			auto t = m_game.Registry().GetComponent<Transform>(m_player);
			if (t.IsSome()) {
				t.Unwrap()->yaw -= float(e.MouseMotion().xrel) * 0.0025f;
				t.Unwrap()->pitch = std::clamp(t.Unwrap()->pitch - float(e.MouseMotion().yrel) * 0.0025f, -1.55f, 1.55f);
			}
		}
		if (e.IsMouseDown(SDL_BUTTON_LEFT)) {
			if (!m_captured)
				SetCaptured(true);
			m_leftDown = true;
		}
		if (e.IsMouseUp(SDL_BUTTON_LEFT)) {
			m_leftDown = false;
			ResetBreaking();
		}
		if (e.IsMouseDown(SDL_BUTTON_RIGHT)) {
			m_rightDown = true;
			m_useTimer = 0.f;
		}
		if (e.IsMouseUp(SDL_BUTTON_RIGHT))
			m_rightDown = false;
		if (e.IsMouseDown(SDL_BUTTON_MIDDLE))
			PickBlock();
		if (e.IsMouseWheel())
			SelectSlot(SelectedSlot() - (e.MouseWheel().y > 0 ? 1 : e.MouseWheel().y < 0 ? -1 : 0));
		if (e.IsKeyDown()) {
			const SDL_Keycode key = e.Key().key;
			const SDL_Scancode scan = e.Key().scancode;
			if (scan >= SDL_SCANCODE_1 && scan <= SDL_SCANCODE_9)
				SelectSlot(int(scan - SDL_SCANCODE_1));
			else if (key == SDLK_T || key == SDLK_RETURN)
				OpenConsole(String());
			else if (key == SDLK_SLASH || key == SDLK_KP_DIVIDE || key == SDLK_COLON)
				OpenConsole(String("/"));
			else if (key == SDLK_E)
				ToggleInventory();
			else if (key == SDLK_F3)
				m_debug = !m_debug;
			else if (key == SDLK_F5) {
				Result<bool, String> r = m_game.Save();
				m_game.Message(r.IsOk() ? String("Monde enregistré") : r.Error());
			} else if (scan == SDL_SCANCODE_F)
				Notify(m_game.Execute("/fly", m_player));
			else if (scan == SDL_SCANCODE_Q && false) {
			} else if (key == SDLK_A && false) {
			}
			if (scan == SDL_SCANCODE_X || key == SDLK_DELETE)
				DropSelected();
		}
	}

	void Update(float dt, long frame) {
		// Commandes du joueur → intention de déplacement.
		UpdateControls(dt);
		if (!m_menuOpen)
			m_game.Tick(dt);
		// Minage / utilisation (après le pas de jeu : la visée est à jour).
		Option<BlockHit> target;
		if (!m_menuOpen && !m_inventoryOpen && !m_consoleOpen) {
			target = m_game.TargetBlock(m_player);
			UpdateActions(dt, target);
		}
		// Caméra subjective, interpolée entre deux pas de physique.
		render3d::Camera camera = CameraFor();
		if (auto vp = m_ui.GetComponent<ui::UiViewport3D>(m_viewport); vp.IsSome()) {
			if (vp.Unwrap()->textureHeight > 0)
				camera.aspect = float(vp.Unwrap()->textureWidth) / float(vp.Unwrap()->textureHeight);
			vp.Unwrap()->camera = camera;
		}
		m_camera = camera;
		m_world.Update(camera, m_game.GetSchedule().FixedAlpha(), target);
		UpdateSound();
		UpdateMessages(dt);
		UpdateLabels(frame);
		if (m_inventoryOpen && m_recipesDirty)
			RebuildRecipes();
		const bool wantCapture = !m_menuOpen && !m_inventoryOpen && !m_consoleOpen;
		if (wantCapture != m_captured && !wantCapture)
			SetCaptured(false);
	}

private:
	// ── Construction de l'interface ───────────────────────────────────────

	void Build() {
		ui::UiFactory &f = m_gui.Factory();
		ui::WidgetBuilder root = f.Column();
		root.Fixed().Anchor(ui::Anchor::TopLeft).W(ui::Dimension::Rpct(100.f)).H(ui::Dimension::Rpct(100.f)).Gap(0.f).Pad(0.f);
		m_root = root.Spawn();
		ui::WidgetBuilder viewport = f.Viewport3D(m_world.Root(), render3d::Camera{});
		viewport.GrowW().GrowH().Parent(m_root);
		m_viewport = viewport.Spawn();

		ui::WidgetBuilder hud = f.Canvas([this](sdl3::Renderer &ren, sdl3::FRect r) { DrawHud(ren, r); });
		hud.Absolute().Anchor(ui::Anchor::TopLeft).W(ui::Dimension::Pct(100.f)).H(ui::Dimension::Pct(100.f)).PointerThrough().Parent(m_viewport);
		(void)hud.Spawn();

		auto label = [&](ecs::Entity parent, float size, sdl3::FColor color) {
			ui::WidgetBuilder l = f.Label(String(""));
			l.WAuto().HAuto().FontSize(size).TextColor(color).PointerThrough().Parent(parent);
			return l.Spawn();
		};
		// Informations (F3) et heure, en haut à gauche.
		ui::WidgetBuilder info = f.Column();
		info.Absolute().Anchor(ui::Anchor::TopLeft).Offset(10.f, 10.f).WAuto().HAuto().Gap(1.f).Pad(math::Sides{8.f, 5.f});
		info.Radius(5.f).Bg(Rgba(0, 0, 0, 110)).PointerThrough().Parent(m_viewport);
		m_infoPanel = info.Spawn();
		for (ecs::Entity &l : m_infoLines)
			l = label(m_infoPanel, 13.f, sdl3::FColor::WHITE());
		// Messages (chat, résultats des commandes), en bas à gauche.
		ui::WidgetBuilder messages = f.Column();
		messages.Absolute().Anchor(ui::Anchor::BottomLeft).Offset(12.f, -112.f).WAuto().HAuto().Gap(2.f).PointerThrough().Parent(m_viewport);
		m_messagePanel = messages.Spawn();
		for (ecs::Entity &l : m_messageLines)
			l = label(m_messagePanel, 14.f, sdl3::FColor::WHITE());
		// Console.
		ui::WidgetBuilder console = f.Column();
		console.Absolute().Anchor(ui::Anchor::BottomLeft).Offset(10.f, -66.f).W(ui::Dimension::Pct(60.f)).HAuto().Gap(3.f);
		console.Parent(m_viewport);
		m_consolePanel = console.Spawn();
		m_completion = label(m_consolePanel, 12.f, Rgba(200, 220, 255));
		ui::WidgetBuilder input = f.Input(String("commande (/help) ou message…"));
		input.GrowW().H(ui::Dimension::Px(30.f)).FontSize(15.f).Parent(m_consolePanel);
		input.OnSubmit([this](const String &text) { Submit(text); });
		m_consoleInput = input.Spawn();
		Hide(m_consolePanel, true);
		// Menu (Échap).
		ui::WidgetBuilder menu = f.Column();
		menu.Absolute().Anchor(ui::Anchor::Center).WAuto().HAuto().Gap(8.f).Pad(math::Sides{24.f, 18.f}).Radius(8.f);
		menu.Bg(Rgba(20, 22, 30, 225)).Parent(m_viewport);
		m_menu = menu.Spawn();
		(void)label(m_menu, 22.f, sdl3::FColor::WHITE());
		SetText(m_ui.GetComponent<ui::UiLabel>(Children(m_menu).back()), String("Pause"));
		for (const char *line : {"ZQSD / WASD : se déplacer — souris : regarder", "Espace : sauter — Maj : s'accroupir — Ctrl : courir — F : voler",
								 "Clic gauche : casser / frapper — clic droit : poser / utiliser", "Clic milieu : choisir le bloc visé — X : jeter",
								 "1-9 / molette : objet — E : inventaire et fabrication", "T ou / : console (/help) — F3 : informations — F5 : enregistrer"}) {
			ecs::Entity l = label(m_menu, 14.f, Rgba(210, 215, 230));
			SetText(m_ui.GetComponent<ui::UiLabel>(l), String(line));
		}
		auto button = [&](ecs::Entity parent, const char *text, std::function<void()> fn) {
			ui::WidgetBuilder b = f.Button(String(text));
			b.GrowW().H(ui::Dimension::Px(32.f)).FontSize(15.f).OnClick(std::move(fn)).Parent(parent);
			return b.Spawn();
		};
		(void)button(m_menu, "Reprendre", [this] { ToggleMenu(); });
		(void)button(m_menu, "Enregistrer", [this] {
			Result<bool, String> r = m_game.Save();
			m_game.Message(r.IsOk() ? String("Monde enregistré") : r.Error());
		});
		(void)button(m_menu, "Enregistrer et quitter", [this] { m_quit = true; });
		Hide(m_menu, true);
		// Inventaire et fabrication (E).
		ui::WidgetBuilder inventory = f.Row();
		inventory.Absolute().Anchor(ui::Anchor::Center).WAuto().HAuto().Gap(16.f).Pad(math::Sides{18.f, 16.f}).Radius(8.f);
		inventory.Bg(Rgba(20, 22, 30, 230)).Parent(m_viewport);
		m_inventory = inventory.Spawn();
		ui::WidgetBuilder grid = f.Canvas([this](sdl3::Renderer &ren, sdl3::FRect r) { DrawInventory(ren, r); });
		grid.Size(SLOT * 9.f, SLOT * 4.f + 30.f).Parent(m_inventory);
		grid.OnClick([this] { ClickInventory(); });
		m_inventoryGrid = grid.Spawn();
		ui::WidgetBuilder recipes = f.Column();
		recipes.W(ui::Dimension::Px(300.f)).HAuto().Gap(4.f).Parent(m_inventory);
		m_recipes = recipes.Spawn();
		Hide(m_inventory, true);
	}

	std::vector<ecs::Entity> Children(ecs::Entity e) {
		std::vector<ecs::Entity> out;
		m_ui.Query<ui::UiLabel>([&](ecs::Entity c, ui::UiLabel &) {
			if (auto p = m_ui.GetComponent<ui::UiParent>(c); p.IsSome() && p.Unwrap()->parent == e)
				out.push_back(c);
		});
		return out;
	}

	static void SetText(Option<RefMut<ui::UiLabel>> label, const String &text) {
		if (label.IsSome() && label.Unwrap()->text != text)
			label.Unwrap()->text = text;
	}
	void SetLabel(ecs::Entity e, const String &text) { SetText(m_ui.GetComponent<ui::UiLabel>(e), text); }

	void Hide(ecs::Entity e, bool hidden) {
		if (hidden && !m_ui.HasComponent<ui::UiHidden>(e))
			m_ui.AddComponent(e, ui::UiHidden{});
		else if (!hidden)
			m_ui.RemoveComponent<ui::UiHidden>(e);
		m_gui.Layout().MarkDirty();
	}

	void SetCaptured(bool captured) {
		m_captured = captured;
		(void)m_window.SetRelativeMouseMode(captured);
		if (captured)
			m_gui.ClearKeyboardFocus();
	}

	// ── Panneaux ─────────────────────────────────────────────────────────

	void ToggleMenu() {
		m_menuOpen = !m_menuOpen;
		Hide(m_menu, !m_menuOpen);
		SetCaptured(!m_menuOpen);
	}

	void ToggleInventory() {
		m_inventoryOpen = !m_inventoryOpen;
		Hide(m_inventory, !m_inventoryOpen);
		m_recipesDirty = true;
		m_heldSlot = -1;
		SetCaptured(!m_inventoryOpen);
	}

	void OpenConsole(const String &prefix) {
		m_consoleOpen = true;
		Hide(m_consolePanel, false);
		if (auto input = m_ui.GetComponent<ui::UiInput>(m_consoleInput); input.IsSome()) {
			input.Unwrap()->text = prefix;
			input.Unwrap()->cursor = prefix.GetSize();
			input.Unwrap()->selectionAnchor = prefix.GetSize();
			input.Unwrap()->focused = true;
		}
		m_swallowText = true;
		m_historyIndex = int(m_history.size());
		SetCaptured(false);
	}

	void CloseConsole() {
		m_consoleOpen = false;
		Hide(m_consolePanel, true);
		SetLabel(m_completion, String());
		SetCaptured(true);
	}

	void Notify(const Result<String, String> &r) {
		const String text = r.IsOk() ? r.Value() : r.Error();
		// Une ligne de message par ligne de résultat.
		size_t start = 0;
		while (start < text.GetSize()) {
			size_t end = text.Find("\n", start);
			if (end == String::NPOS)
				end = text.GetSize();
			if (end > start)
				m_game.Message(text.Substring(start, end), String(r.IsOk() ? "console" : "erreur"));
			start = end + 1;
		}
	}

	void Submit(const String &raw) {
		const String text = raw.Trim();
		if (!text.IsEmpty()) {
			m_history.push_back(text);
			if (text.StartsWith("/"))
				Notify(m_game.Execute(text, m_player));
			else
				m_game.Message(String::Format("<Joueur> %s", text.CStr()), String("chat"));
		}
		CloseConsole();
	}

	void Complete() {
		auto input = m_ui.GetComponent<ui::UiInput>(m_consoleInput);
		if (input.IsNone())
			return;
		const String text = input.Unwrap()->text;
		const std::vector<String> options = m_game.Complete(text);
		if (options.size() == 1) {
			// Remplace le dernier mot.
			const size_t space = text.Find(" ") == String::NPOS ? String::NPOS : [&] {
				size_t last = 0;
				for (size_t i = 0; i < text.GetSize(); ++i)
					if (text.CharAt(i) == ' ')
						last = i;
				return last;
			}();
			const String completed = (space == String::NPOS ? String() : text.Substring(0, space + 1)) + options[0] + String(" ");
			input.Unwrap()->text = completed;
			input.Unwrap()->cursor = input.Unwrap()->selectionAnchor = completed.GetSize();
			SetLabel(m_completion, String());
		} else {
			String list;
			for (size_t i = 0; i < options.size() && i < 12; ++i)
				list += options[i] + String("   ");
			SetLabel(m_completion, options.empty() ? String("(aucune complétion)") : list);
		}
	}

	void BrowseHistory(int step) {
		if (m_history.empty())
			return;
		m_historyIndex = std::clamp(m_historyIndex + step, 0, int(m_history.size()));
		const String text = m_historyIndex < int(m_history.size()) ? m_history[size_t(m_historyIndex)] : String();
		if (auto input = m_ui.GetComponent<ui::UiInput>(m_consoleInput); input.IsSome()) {
			input.Unwrap()->text = text;
			input.Unwrap()->cursor = input.Unwrap()->selectionAnchor = text.GetSize();
		}
	}

	void RebuildRecipes() {
		m_recipesDirty = false;
		for (ecs::Entity child : m_recipeButtons)
			ui::DespawnTree(m_ui, child);
		m_recipeButtons.clear();
		ui::UiFactory &f = m_gui.Factory();
		ui::WidgetBuilder title = f.Label(String("Fabrication — cliquez pour fabriquer"));
		title.WAuto().HAuto().FontSize(15.f).TextColor(sdl3::FColor::WHITE()).Parent(m_recipes);
		m_recipeButtons.push_back(title.Spawn());
		auto inv = m_game.Registry().GetComponent<InventoryComponent>(m_player);
		if (inv.IsNone())
			return;
		const std::vector<const RecipeDef *> craftable = CraftableRecipes(m_game.Content(), inv.Unwrap()->inventory);
		for (const RecipeDef *r : craftable) {
			const ItemDef *item = m_game.Content().FindItem(r->result);
			const String text = String::Format("%d × %s", r->resultCount, item ? item->displayName.CStr() : r->result.CStr());
			ui::WidgetBuilder b = f.Button(text);
			b.GrowW().H(ui::Dimension::Px(28.f)).FontSize(13.f).Parent(m_recipes);
			b.OnClick([this, id = r->id] {
				Notify(m_game.Execute(String("/craft ") + id, m_player));
				m_recipesDirty = true;
			});
			m_recipeButtons.push_back(b.Spawn());
			if (m_recipeButtons.size() > 14)
				break;
		}
		if (craftable.empty()) {
			ui::WidgetBuilder none = f.Label(String("Rien à fabriquer avec cet inventaire."));
			none.WAuto().HAuto().FontSize(13.f).TextColor(Rgba(180, 185, 200)).Parent(m_recipes);
			m_recipeButtons.push_back(none.Spawn());
		}
		ui::WidgetBuilder hint = f.Label(String("Cuisson : /cook glass, /cook iron_ingot (charbon)"));
		hint.WAuto().HAuto().FontSize(12.f).TextColor(Rgba(170, 175, 190)).Parent(m_recipes);
		m_recipeButtons.push_back(hint.Spawn());
		m_gui.Layout().MarkDirty();
	}

	// ── Joueur ──────────────────────────────────────────────────────────

	Inventory *PlayerInventory() {
		auto inv = m_game.Registry().GetComponent<InventoryComponent>(m_player);
		return inv.IsSome() ? &inv.Unwrap()->inventory : nullptr;
	}
	int SelectedSlot() {
		auto p = m_game.Registry().GetComponent<Player>(m_player);
		return p.IsSome() ? p.Unwrap()->selectedSlot : 0;
	}
	void SelectSlot(int slot) {
		if (auto p = m_game.Registry().GetComponent<Player>(m_player); p.IsSome())
			p.Unwrap()->selectedSlot = ((slot % HOTBAR) + HOTBAR) % HOTBAR;
		ResetBreaking();
	}

	void UpdateControls(float) {
		auto intent = m_game.Registry().GetComponent<MoveIntent>(m_player);
		auto t = m_game.Registry().GetComponent<Transform>(m_player);
		if (intent.IsNone() || t.IsNone())
			return;
		MoveIntent &i = *intent.Unwrap();
		i.wish = {0.f, 0.f, 0.f};
		i.jump = i.sneak = i.sprint = i.descend = false;
		if (m_menuOpen || m_inventoryOpen || m_consoleOpen)
			return;
		const bool *keys = SDL_GetKeyboardState(nullptr);
		const float yaw = t.Unwrap()->yaw;
		const math::FVector3 forward{std::sin(yaw), 0.f, std::cos(yaw)}, right{-std::cos(yaw), 0.f, std::sin(yaw)};
		const float f = float(keys[SDL_SCANCODE_W]) - float(keys[SDL_SCANCODE_S]);
		const float s = float(keys[SDL_SCANCODE_D]) - float(keys[SDL_SCANCODE_A]);
		math::FVector3 wish = forward * f + right * s;
		if (wish.Length() > 1.f)
			wish = wish.Normalize();
		i.wish = wish;
		i.jump = keys[SDL_SCANCODE_SPACE];
		const bool shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
		auto body = m_game.Registry().GetComponent<PhysicsBody>(m_player);
		if (body.IsSome() && body.Unwrap()->flies)
			i.descend = shift;
		else
			i.sneak = shift;
		i.sprint = keys[SDL_SCANCODE_LCTRL];
		i.speed = float(m_game.Content().ConfigNum("player.speed", 4.3)) * (body.IsSome() && body.Unwrap()->flies ? 2.5f : 1.f);
	}

	void ResetBreaking() {
		m_breakProgress = 0.f;
		m_breaking = BlockPos{0, -100000, 0};
	}

	void UpdateActions(float dt, const Option<BlockHit> &target) {
		m_attackTimer -= dt;
		m_useTimer -= dt;
		m_breakRepeat -= dt;
		if (m_leftDown) {
			if (Option<EntityHit> entity = m_game.TargetEntity(m_player); entity.IsSome()) {
				if (m_attackTimer <= 0.f) {
					float damage = 1.f;
					if (ItemStack *held = m_game.HeldItem(m_player); held && !held->IsEmpty())
						if (const ItemDef *item = m_game.Content().FindItem(held->item); item && item->tool)
							damage = item->tool->damage;
					m_game.Damage(entity.Value().entity, damage, m_player);
					m_attackTimer = 0.45f;
				}
				ResetBreaking();
			} else if (target.IsSome()) {
				const BlockPos pos = target.Value().block;
				if (!(pos == m_breaking)) {
					m_breaking = pos;
					m_breakProgress = 0.f;
				}
				const float time = m_game.BreakTime(m_player, pos);
				if (time >= 0.f && m_breakRepeat <= 0.f) {
					m_breakProgress += time <= 0.f ? 1.f : dt / time;
					m_hitTimer -= dt;
					if (m_hitTimer <= 0.f && m_breakProgress < 1.f) { // coups de pioche
						m_game.PlayBlockSound(m_game.GetWorld().GetBlock(pos), "hit", pos);
						m_hitTimer = 0.25f;
					}
					if (m_breakProgress >= 1.f) {
						(void)m_game.BreakBlock(m_player, pos);
						ResetBreaking();
						m_breakRepeat = m_options.creative ? 0.2f : 0.05f;
					}
				}
			} else {
				ResetBreaking();
			}
		}
		if (m_rightDown && m_useTimer <= 0.f) {
			(void)m_game.UseHeldItem(m_player);
			m_useTimer = 0.25f;
		}
	}

	void PickBlock() {
		Option<BlockHit> hit = m_game.TargetBlock(m_player);
		Inventory *inv = PlayerInventory();
		if (hit.IsNone() || !inv)
			return;
		const String id = m_game.GetWorld().BlockAt(hit.Value().block).id;
		for (int i = 0; i < HOTBAR; ++i)
			if (inv->Slot(i).item == id) {
				SelectSlot(i);
				return;
			}
		if (m_game.Options().creative)
			inv->Slot(SelectedSlot()) = ItemStack{id, 64, 0, nullptr};
	}

	void DropSelected() {
		ItemStack *held = m_game.HeldItem(m_player);
		auto t = m_game.Registry().GetComponent<Transform>(m_player);
		if (!held || held->IsEmpty() || t.IsNone())
			return;
		const math::FVector3 forward = t.Unwrap()->Forward();
		(void)m_game.DropItem(ItemStack{held->item, 1, held->wear, held->meta},
							  t.Unwrap()->position + math::FVector3{0.f, EYE - 0.3f, 0.f} + forward * 0.6f, forward * 5.f);
		if (--held->count <= 0)
			*held = ItemStack{};
	}

	render3d::Camera CameraFor() {
		render3d::Camera camera;
		camera.fovYRadians = 75.f * 3.14159265f / 180.f;
		camera.nearPlane = 0.05f;
		camera.farPlane = float(m_options.view + 2) * 16.f * 1.6f;
		auto t = m_game.Registry().GetComponent<Transform>(m_player);
		if (t.IsNone())
			return camera;
		const float alpha = m_game.GetSchedule().FixedAlpha();
		const math::FVector3 feet = t.Unwrap()->previous + (t.Unwrap()->position - t.Unwrap()->previous) * alpha;
		auto intent = m_game.Registry().GetComponent<MoveIntent>(m_player);
		const float eye = EYE - (intent.IsSome() && intent.Unwrap()->sneak ? 0.15f : 0.f);
		camera.position = feet + math::FVector3{0.f, eye, 0.f};
		camera.target = camera.position + t.Unwrap()->Forward();
		return camera;
	}

	// ── Son ─────────────────────────────────────────────────────────────

	void UpdateSound() {
		ecs::EventsOf<SoundEvent>(m_game.Registry()).Read(m_soundCursor, [this](const SoundEvent &s) {
			const resources::AssetId id = m_game.Assets().SoundId(s.sound, s.variant);
			if (!m_game.Assets().Cache().Knows(id))
				return;
			audio::AudioPlayOptions options;
			options.volume = std::clamp(s.volume, 0.f, 2.f);
			options.pitch = s.pitch;
			options.spatial = s.positional;
			options.position = s.position;
			(void)m_audio.Play(resources::AssetRef<audio::AudioClipResource>{id}, options);
		});
		m_audio.SetListener(m_camera.ViewMatrix());
		m_audio.Update();
		for (const String &e : m_audio.TakeErrors())
			m_game.LogLine(String("[son] ") + e);
	}

	// ── Messages et informations ────────────────────────────────────────

	void UpdateMessages(float dt) {
		ecs::EventsOf<GameMessage>(m_game.Registry()).Read(m_messageCursor, [this](const GameMessage &m) {
			m_messages.push_back({m.text, 10.f});
			if (m_messages.size() > 40)
				m_messages.pop_front();
		});
		for (auto &[text, ttl] : m_messages)
			ttl -= dt;
		// Les plus récents en bas ; tous visibles pendant la saisie.
		std::vector<String> shown;
		for (auto it = m_messages.rbegin(); it != m_messages.rend() && shown.size() < m_messageLines.size(); ++it)
			if (it->second > 0.f || m_consoleOpen)
				shown.push_back(it->first);
		for (size_t i = 0; i < m_messageLines.size(); ++i) {
			const size_t from = m_messageLines.size() - 1 - i;
			SetLabel(m_messageLines[i], from < shown.size() ? shown[from] : String());
		}
	}

	void UpdateLabels(long frame) {
		m_fpsTimer += 1;
		const uint64_t now = sdl3::GetPerformanceCounter();
		if (m_lastTicks == 0)
			m_lastTicks = now;
		const double elapsed = double(now - m_lastTicks) / double(sdl3::GetPerformanceFrequency());
		if (elapsed > 0.5) {
			m_fps = float(m_fpsTimer / elapsed);
			m_fpsTimer = 0;
			m_lastTicks = now;
		}
		auto t = m_game.Registry().GetComponent<Transform>(m_player);
		if (t.IsNone())
			return;
		const math::FVector3 p = t.Unwrap()->position;
		const BlockPos b = BlockPos::FromWorld(p);
		const BiomeDef *biome = m_game.GetWorld().Generator() ? m_game.GetWorld().Generator()->BiomeAt(b.x, b.z) : nullptr;
		const float day = m_game.DayFraction();
		const int hours = int(day * 24.f), minutes = int((day * 24.f - float(hours)) * 60.f);
		SetLabel(m_infoLines[0], String::Format("%02d:%02d — %s%s", hours, minutes, biome ? biome->displayName.CStr() : "?",
												m_game.Options().creative ? " — créatif" : ""));
		if (m_debug) {
			SetLabel(m_infoLines[1], String::Format("%.0f img/s — %.1f %.1f %.1f — chunk %s", double(m_fps), double(p.x), double(p.y),
													double(p.z), ChunkOf(b).ToString().CStr()));
			SetLabel(m_infoLines[2], String::Format("%d chunk(s) chargé(s), %d maillé(s), %d en attente, %dk triangles",
													int(m_game.GetWorld().LoadedChunkCount()), int(m_world.ChunkMeshCount()),
													int(m_world.PendingChunks()), int(m_world.TriangleCount() / 1000)));
			SetLabel(m_infoLines[3], String::Format("%d entité(s), %d ressource(s) en mémoire, %d voix",
													int(m_game.Registry().EntityCount()), int(m_game.Assets().ResidentCount()),
													int(m_audio.VoiceCount())));
		} else {
			for (size_t i = 1; i < m_infoLines.size(); ++i)
				SetLabel(m_infoLines[i], String());
		}
		(void)frame;
	}

	// ── Dessin du HUD ───────────────────────────────────────────────────

	void EnsureIcons(sdl3::Renderer &ren) {
		if (m_icons.IsSome())
			return;
		const Image &atlas = m_world.Atlas().Pixels();
		auto surface = sdl3::Surface::CreateFromPixels(atlas.width, atlas.height, sdl3::PixelFormat::RGBA32, atlas.rgba.data(), atlas.width * 4);
		if (surface.IsError())
			return;
		auto texture = sdl3::Texture::CreateFromSurface(ren, surface.Value());
		if (texture.IsError())
			return;
		(void)texture.Value().SetScaleMode(SDL_SCALEMODE_NEAREST);
		m_icons = Some(std::move(texture).Unwrap());
	}

	void DrawItem(sdl3::Renderer &ren, const ItemStack &stack, sdl3::FRect cell) {
		if (stack.IsEmpty() || m_icons.IsNone())
			return;
		const ItemDef *item = m_game.Content().FindItem(stack.item);
		const TextureAtlas::Rect r = m_world.Atlas().Lookup(item ? item->icon : String());
		const Image &atlas = m_world.Atlas().Pixels();
		const sdl3::FRect src{r.u0 * float(atlas.width), r.v0 * float(atlas.height), (r.u1 - r.u0) * float(atlas.width),
							  (r.v1 - r.v0) * float(atlas.height)};
		const float pad = cell.w * 0.15f;
		(void)ren.Render(m_icons.Value(), src, sdl3::FRect{cell.x + pad, cell.y + pad, cell.w - 2.f * pad, cell.h - 2.f * pad});
		if (stack.count > 1) {
			const String count = String::Format("%d", stack.count);
			ren.SetDrawColor(sdl3::FColor{0.f, 0.f, 0.f, 1.f});
			(void)ren.DrawDebugText(cell.x + cell.w - 8.f * float(count.GetSize()) - 3.f, cell.y + cell.h - 10.f, count.CStr());
			ren.SetDrawColor(sdl3::FColor::WHITE());
			(void)ren.DrawDebugText(cell.x + cell.w - 8.f * float(count.GetSize()) - 4.f, cell.y + cell.h - 11.f, count.CStr());
		}
		// Usure d'un outil : barre sous l'icône.
		if (item && item->tool && item->tool->durability > 0 && stack.wear > 0) {
			const float k = 1.f - float(stack.wear) / float(item->tool->durability);
			ren.SetDrawColor(sdl3::FColor{0.f, 0.f, 0.f, 1.f});
			(void)ren.FillRect(sdl3::FRect{cell.x + 6.f, cell.y + cell.h - 6.f, cell.w - 12.f, 3.f});
			ren.SetDrawColor(sdl3::FColor{1.f - k, k, 0.1f, 1.f});
			(void)ren.FillRect(sdl3::FRect{cell.x + 6.f, cell.y + cell.h - 6.f, (cell.w - 12.f) * k, 3.f});
		}
	}

	void DrawSlot(sdl3::Renderer &ren, sdl3::FRect cell, bool selected) {
		ren.SetDrawColor(Rgba(0, 0, 0, 120));
		(void)ren.FillRect(cell);
		ren.SetDrawColor(selected ? Rgba(255, 255, 255) : Rgba(140, 140, 150, 200));
		(void)ren.DrawRect(cell);
		if (selected)
			(void)ren.DrawRect(sdl3::FRect{cell.x + 1.f, cell.y + 1.f, cell.w - 2.f, cell.h - 2.f});
	}

	void DrawHud(sdl3::Renderer &ren, sdl3::FRect r) {
		EnsureIcons(ren);
		(void)ren.SetBlendMode(sdl3::BlendMode::BLEND);
		// Sous l'eau : voile bleu.
		if (m_game.GetWorld().BlockAt(BlockPos::FromWorld(m_camera.position)).fluid) {
			ren.SetDrawColor(Rgba(30, 70, 160, 90));
			(void)ren.FillRect(r);
		}
		// Blessure : éclair rouge.
		if (auto h = m_game.Registry().GetComponent<Health>(m_player); h.IsSome()) {
			if (h.Unwrap()->current < m_lastHealth - 0.01f)
				m_hurtFlash = 0.35f;
			m_lastHealth = h.Unwrap()->current;
		}
		if (m_hurtFlash > 0.f) {
			ren.SetDrawColor(Rgba(200, 0, 0, int(m_hurtFlash * 300.f)));
			(void)ren.FillRect(r);
			m_hurtFlash -= 1.f / 60.f;
		}
		// Viseur.
		const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
		ren.SetDrawColor(Rgba(0, 0, 0, 160));
		(void)ren.FillRect(sdl3::FRect{cx - 9.f, cy - 2.f, 18.f, 4.f});
		(void)ren.FillRect(sdl3::FRect{cx - 2.f, cy - 9.f, 4.f, 18.f});
		ren.SetDrawColor(Rgba(255, 255, 255, 230));
		(void)ren.FillRect(sdl3::FRect{cx - 8.f, cy - 1.f, 16.f, 2.f});
		(void)ren.FillRect(sdl3::FRect{cx - 1.f, cy - 8.f, 2.f, 16.f});
		// Progression du minage.
		if (m_breakProgress > 0.f) {
			ren.SetDrawColor(Rgba(0, 0, 0, 150));
			(void)ren.FillRect(sdl3::FRect{cx - 30.f, cy + 16.f, 60.f, 6.f});
			ren.SetDrawColor(Rgba(250, 250, 250, 230));
			(void)ren.FillRect(sdl3::FRect{cx - 29.f, cy + 17.f, 58.f * std::min(1.f, m_breakProgress), 4.f});
		}
		// Barre d'objets.
		Inventory *inv = PlayerInventory();
		const float width = SLOT * float(HOTBAR);
		const float x0 = cx - width * 0.5f, y0 = r.y + r.h - SLOT - 12.f;
		for (int i = 0; i < HOTBAR; ++i) {
			const sdl3::FRect cell{x0 + float(i) * SLOT, y0, SLOT - 2.f, SLOT - 2.f};
			DrawSlot(ren, cell, i == SelectedSlot());
			if (inv)
				DrawItem(ren, inv->Slot(i), cell);
		}
		// Nom de l'objet tenu.
		if (ItemStack *held = m_game.HeldItem(m_player); held && !held->IsEmpty())
			if (const ItemDef *item = m_game.Content().FindItem(held->item)) {
				ren.SetDrawColor(Rgba(255, 255, 255, 220));
				(void)ren.DrawDebugText(x0, y0 - 14.f, item->displayName.CStr());
			}
		// Santé : cœurs (un cœur = 2 points).
		if (auto h = m_game.Registry().GetComponent<Health>(m_player); h.IsSome() && !m_game.Options().creative) {
			const int hearts = int(std::ceil(h.Unwrap()->max / 2.f));
			for (int i = 0; i < hearts; ++i) {
				const float hx = x0 + width - float(hearts - i) * 14.f, hy = y0 - 16.f;
				const float fill = std::clamp(h.Unwrap()->current / 2.f - float(i), 0.f, 1.f);
				ren.SetDrawColor(Rgba(40, 0, 0, 200));
				(void)ren.FillRect(sdl3::FRect{hx, hy, 11.f, 10.f});
				ren.SetDrawColor(Rgba(220, 30, 40));
				(void)ren.FillRect(sdl3::FRect{hx + 1.f, hy + 1.f, 9.f * fill, 8.f});
			}
		}
	}

	void DrawInventory(sdl3::Renderer &ren, sdl3::FRect r) {
		EnsureIcons(ren);
		m_gridRect = r;
		ren.SetDrawColor(sdl3::FColor::WHITE());
		(void)ren.DrawDebugText(r.x, r.y + 4.f, "Inventaire — cliquez deux cases pour les echanger");
		Inventory *inv = PlayerInventory();
		if (!inv)
			return;
		for (int i = 0; i < std::min(inv->Size(), 36); ++i) {
			// Barre d'objets (0-8) en bas, comme d'habitude.
			const int row = i < HOTBAR ? 3 : (i - HOTBAR) / HOTBAR, col = i % HOTBAR;
			const sdl3::FRect cell{r.x + float(col) * SLOT, r.y + 24.f + float(row) * SLOT + (row == 3 ? 6.f : 0.f), SLOT - 2.f, SLOT - 2.f};
			DrawSlot(ren, cell, i == m_heldSlot);
			DrawItem(ren, inv->Slot(i), cell);
		}
	}

	void ClickInventory() {
		float mx = 0.f, my = 0.f;
		(void)SDL_GetMouseState(&mx, &my);
		const int col = int((mx - m_gridRect.x) / SLOT);
		const float ry = my - m_gridRect.y - 24.f;
		int row = int(ry / SLOT);
		if (col < 0 || col >= HOTBAR || ry < 0.f || row > 3)
			return;
		const int slot = row == 3 ? col : HOTBAR + row * HOTBAR + col;
		Inventory *inv = PlayerInventory();
		if (!inv || slot >= inv->Size())
			return;
		if (m_heldSlot < 0) {
			m_heldSlot = slot;
		} else {
			std::swap(inv->Slot(m_heldSlot), inv->Slot(slot));
			m_heldSlot = -1;
			m_recipesDirty = true;
		}
	}

	Game &m_game;
	const Options &m_options;
	sdl3::Window &m_window;
	sdl3::Renderer &m_renderer;
	render3d::Canvas &m_canvas;
	ui::Ui &m_gui;
	ecs::ArchetypeRegistry &m_ui;
	WorldRenderer m_world;
	audio::AudioEngine m_audio;
	ecs::Entity m_player{};
	render3d::Camera m_camera;

	ecs::Entity m_root{}, m_viewport{}, m_infoPanel{}, m_messagePanel{}, m_consolePanel{}, m_consoleInput{}, m_completion{};
	ecs::Entity m_menu{}, m_inventory{}, m_inventoryGrid{}, m_recipes{};
	std::array<ecs::Entity, 4> m_infoLines{};
	std::array<ecs::Entity, 7> m_messageLines{};
	std::vector<ecs::Entity> m_recipeButtons;
	Option<sdl3::Texture> m_icons = NONE;
	sdl3::FRect m_gridRect{};

	bool m_quit = false, m_captured = false, m_menuOpen = false, m_inventoryOpen = false, m_consoleOpen = false;
	bool m_debug = false, m_swallowText = false, m_recipesDirty = true;
	bool m_leftDown = false, m_rightDown = false;
	int m_heldSlot = -1;
	float m_breakProgress = 0.f, m_hitTimer = 0.f, m_attackTimer = 0.f, m_useTimer = 0.f, m_breakRepeat = 0.f;
	BlockPos m_breaking{0, -100000, 0};
	float m_lastHealth = 1e9f, m_hurtFlash = 0.f;
	std::deque<std::pair<String, float>> m_messages;
	std::vector<String> m_history;
	int m_historyIndex = 0;
	uint64_t m_soundCursor = 0, m_messageCursor = 0;
	float m_fps = 0.f;
	long m_fpsTimer = 0;
	uint64_t m_lastTicks = 0;
};

} // namespace

int App::RunWindowed(Game &game) {
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO | sdl3::init_flags::EVENTS | sdl3::init_flags::AUDIO);
	if (!sdl) {
		std::printf("SDL : %s\n", String(sdl.Error()).CStr());
		return 1;
	}
	auto ttf = sdl3::TtfContext::Create();
	if (!ttf) {
		std::printf("TTF : %s\n", String(ttf.Error()).CStr());
		return 1;
	}
	constexpr float FONT_SIZE = 15.f;
	auto fontResult = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, FONT_SIZE);
	if (!fontResult)
		fontResult = sdl3::Font::Open(String("assets/fonts/DejaVuSans.ttf"), FONT_SIZE);
	if (!fontResult) {
		std::printf("police introuvable\n");
		return 1;
	}
	sdl3::Font &font = fontResult.Value();
	auto windowResult = sdl3::Window::Create(String::Format("Voxel — %s", m_options.world.CStr()), m_options.width, m_options.height,
											 sdl3::window_flags::RESIZABLE);
	if (!windowResult) {
		std::printf("fenêtre : %s\n", String(windowResult.Error()).CStr());
		return 1;
	}
	sdl3::Window &window = windowResult.Value();
	auto rendererResult = sdl3::Renderer::Create(window, "gpu");
	if (!rendererResult) {
		std::printf("renderer : %s\n", String(rendererResult.Error()).CStr());
		return 1;
	}
	sdl3::Renderer &renderer = rendererResult.Value();
	auto canvasResult = render3d::Canvas::Create(renderer);
	if (!canvasResult) {
		std::printf("canvas 3D : %s\n", String(canvasResult.Error()).CStr());
		return 1;
	}
	render3d::Canvas canvas = std::move(canvasResult.Value());
	auto engineResult = sdl3::TextEngine::Create(renderer);
	if (!engineResult) {
		std::printf("moteur de texte : %s\n", String(engineResult.Error()).CStr());
		return 1;
	}
	ecs::ArchetypeRegistry uiWorld;
	ui::Ui gui(uiWorld, window, renderer, ui::UiTheme::Dark());
	gui.Initialize(canvas);
	gui.SetTextEngine(engineResult.Value(), font);
	gui.Layout().measureText = [&font](const String &text, float fontSize) -> sdl3::FPoint {
		if (auto size = font.Measure(text); size.IsSome())
			return {float(size.Unwrap().x) * (fontSize / FONT_SIZE), fontSize * 1.35f};
		return {float(text.GetSize()) * fontSize * 0.55f, fontSize * 1.3f};
	};

	GameWindow session(game, m_options, window, renderer, canvas, gui, uiWorld);
	if (Result<bool, String> r = session.Initialize(); r.IsError()) {
		std::printf("initialisation : %s\n", r.Error().CStr());
		return 1;
	}
	uint64_t previous = sdl3::GetPerformanceCounter();
	long frame = 0;
	double totalSeconds = 0.0;
	while (!session.QuitRequested()) {
		const uint64_t now = sdl3::GetPerformanceCounter();
		const float dt = std::min(0.1f, float(double(now - previous) / double(sdl3::GetPerformanceFrequency())));
		previous = now;
		++frame;
		totalSeconds += dt;
		while (auto event = sdl3::PollEvent())
			session.HandleEvent(event.Value());
		for (const auto &[at, command] : m_options.commands)
			if (at == frame) {
				if (command.StartsWith("!")) { // commande de l'application (captures pilotées)
					session.AppCommand(command);
					continue;
				}
				Result<String, String> r = game.Execute(command);
				game.Message(r.IsOk() ? r.Value() : r.Error(), String("exec"));
			}
		session.Update(dt, frame);
		gui.Tick(dt);
		renderer.SetDrawColor(sdl3::FColor{0.f, 0.f, 0.f, 1.f});
		renderer.Clear();
		gui.Render();
		for (const auto &[at, path] : m_options.screenshots)
			if (at == frame) {
				auto surface = renderer.ReadPixels();
				if (surface)
					(void)sdl3::ImgSavePng(surface.Value(), path);
			}
		renderer.Present();
		if (m_options.frames > 0 && frame >= m_options.frames)
			break;
	}
	std::printf("%ld image(s), %.1f img/s en moyenne\n", frame, totalSeconds > 0.0 ? double(frame) / totalSeconds : 0.0);
	for (const String &line : game.Log())
		if (line.StartsWith("[script") || line.StartsWith("[erreur") || line.StartsWith("[son") || line.StartsWith("[shader"))
			std::printf("%s\n", line.CStr());
	return 0;
}

} // namespace voxelgame
