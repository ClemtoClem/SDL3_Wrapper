// Définitions de shell.hpp — écrans, dialogues, notifications, parties.
#include <voxelgame/shell.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>

#include <SDL3_image/SDL_image.h>
#include <audio/clip.hpp>
#include <sdl3/image.hpp>
#include <ui/viewport3d.hpp>
#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxelgame/screens.hpp>
#include <voxelgame/session.hpp>
#include <voxelgame/world_renderer.hpp>

namespace voxelgame {

namespace fs = std::filesystem;
using ui::Dimension;
using ui::MaterialIcons;
using ui::WidgetBuilder;

Screen::Screen(Shell &shell) : shell(shell), kit(shell.K()), f(shell.K().Factory()) {}

// ── Monde de fond des menus ──────────────────────────────────────────────────

/// Un monde voxel vu par une caméra qui tourne lentement : l'arrière-plan
/// vivant des menus. Il charge tous les mods installés (icônes, sons, skins
/// des écrans de gestion) ; il n'est jamais joué.
class Shell::Backdrop {
public:
	explicit Backdrop(Shell &shell) : m_shell(shell) {}

	Result<bool, String> Initialize(const std::vector<String> &modDirs, const String &cacheDir) {
		voxel::GameOptions o;
		o.modDirectories = modDirs;
		o.savesDirectory = cacheDir;
		o.viewRadius = 5;
		o.verticalRadius = 3;
		o.chunkBudget = 4;
		game = std::make_unique<voxel::Game>();
		const voxel::ModReport report = game->LoadMods(o);
		for (const String &e : report.errors)
			m_shell.Log(String("[mods] ") + e);
		if (Result<bool, String> r = game->OpenWorld("menu", Some(uint32_t(424242))); r.IsError())
			return r;
		game->SetGameTime(0.32 * double(game->Options().dayLength));
		renderer = std::make_unique<WorldRenderer>(*game, m_shell.Canvas());
		if (Result<bool, String> r = renderer->Initialize(); r.IsError())
			return r;
		renderer->rebuildBudget = 4;
		center = game->SpawnPoint();
		audio = std::make_unique<audio::AudioEngine>(game->Assets().Cache());
		audio::RegisterResourceFactories(game->Assets().Cache());
		audio->Start();
		return Ok(true);
	}

	void Update(float dt, bool active) {
		if (!active || !game)
			return;
		const bool still = m_shell.Prefs().reduceMotion;
		angle += dt * (still ? 0.f : 0.035f);
		game->SetGameTime(game->GameTime() + double(dt) * (still ? 0.0 : 4.0));
		const voxel::BlockPos anchor = voxel::BlockPos::FromWorld(center);
		(void)game->GetWorld().UpdateLoadedArea({anchor}, 5, 3, 3);
		camera.position = center + math::FVector3{std::sin(angle) * 34.f, 26.f, std::cos(angle) * 34.f};
		camera.target = center + math::FVector3{0.f, 4.f, 0.f};
		camera.fovYRadians = 60.f * 3.14159265f / 180.f;
		camera.nearPlane = 0.1f;
		camera.farPlane = 220.f;
		renderer->Update(camera, 1.f, NONE);
		audio->Update();
	}

	std::unique_ptr<voxel::Game> game;
	std::unique_ptr<WorldRenderer> renderer;
	std::unique_ptr<audio::AudioEngine> audio;
	render3d::Camera camera;
	math::FVector3 center{0.f, 64.f, 0.f};
	float angle = 0.6f;

private:
	Shell &m_shell;
};

// ── Construction ─────────────────────────────────────────────────────────────

Shell::Shell(Options options, sdl3::Window &window, sdl3::Renderer &renderer, render3d::Canvas &canvas, ui::Ui &gui)
	: m_options(std::move(options)), m_window(window), m_renderer(renderer), m_canvas(canvas), m_gui(gui),
	  m_worlds(m_options.saves), m_mods(m_options.mods, m_options.repository) {}

Shell::~Shell() {
	m_stack.clear();
	if (m_session)
		m_session->Save(false);
	m_session.reset();
	m_backdrop.reset();
}

voxel::Game *Shell::MenuGame() noexcept { return m_backdrop ? m_backdrop->game.get() : nullptr; }
WorldRenderer *Shell::MenuRenderer() noexcept { return m_backdrop ? m_backdrop->renderer.get() : nullptr; }

void Shell::Log(const String &line) {
	m_log.push_back(line);
	if (m_log.size() > 400)
		m_log.erase(m_log.begin());
}

Result<bool, String> Shell::Initialize() {
	m_settings = Settings::Load(m_options.settingsPath);
	if (m_options.width != 1280 || m_options.height != 760) {
		m_settings.width = m_options.width;
		m_settings.height = m_options.height;
	}
	m_kit = std::make_unique<Kit>(m_gui, m_settings);
	(void)m_kit->Pixel().Load(m_renderer, m_options.mods.front() + String("/core/textures/fonts/ascii.png"));
	m_mods.Refresh();
	m_backdrop = std::make_unique<Backdrop>(*this);
	if (Result<bool, String> r = m_backdrop->Initialize(m_options.mods, m_options.cache); r.IsError())
		return Err(String::Format("monde des menus : %s", r.Error().CStr()));
	BuildLayers();
	ApplySettings(false);
	Home();
	return Ok(true);
}

void Shell::BuildLayers() {
	auto full = [&](WidgetBuilder b) {
		b.Fixed().Anchor(ui::Anchor::TopLeft).W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f));
		return b;
	};
	ui::UiFactory &f = m_gui.Factory();
	WidgetBuilder viewport = full(f.Viewport3D(m_backdrop->renderer->Root(), m_backdrop->camera));
	m_viewport = viewport.Spawn();
	WidgetBuilder hud = full(f.Column());
	hud.PointerThrough().Name("hud-layer");
	m_hudLayer = hud.Spawn();
	WidgetBuilder screens = full(f.Column());
	screens.PointerThrough();
	m_screenLayer = screens.Spawn();
	WidgetBuilder toasts = f.Column();
	toasts.Fixed().Anchor(ui::Anchor::TopRight).Offset(-16.f, 16.f).W(Dimension::Px(360.f)).HAuto().Gap(8.f).PointerThrough();
	m_toastLayer = toasts.Spawn();
}

ecs::Entity Shell::MakeLayerRoot(bool overlay) {
	WidgetBuilder root = m_gui.Factory().Column();
	root.Absolute().Anchor(ui::Anchor::TopLeft).W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f)).Parent(m_screenLayer);
	if (overlay)
		root.Bg(WithAlpha(m_kit->Colors().scrim, m_settings.highContrast ? 0.85f : 0.66f));
	return root.Spawn();
}

// ── Navigation ───────────────────────────────────────────────────────────────

void Shell::DestroyLayer(Layer &layer) {
	if (layer.root.Valid())
		m_kit->Destroy(layer.root);
	layer.root = ecs::Entity{};
}

void Shell::ShowTop() {
	// Écrans visibles : le dernier non superposé et tous ceux au-dessus.
	size_t first = m_stack.size();
	for (size_t i = m_stack.size(); i-- > 0;) {
		first = i;
		if (!m_stack[i].screen->Overlay())
			break;
	}
	for (size_t i = 0; i < m_stack.size(); ++i) {
		Layer &layer = m_stack[i];
		if (i < first) {
			DestroyLayer(layer);
		} else if (!layer.root.Valid()) {
			layer.root = MakeLayerRoot(layer.screen->Overlay());
			layer.screen->Build(layer.root);
		}
	}
	m_gui.Layout().MarkDirty();
	if (m_session)
		m_session->SetCaptured(m_stack.empty() || m_stack.back().screen->PassThrough());
}

void Shell::Push(std::unique_ptr<Screen> screen) {
	if (!screen)
		return;
	m_gui.ClearKeyboardFocus();
	m_stack.push_back(Layer{std::move(screen), ecs::Entity{}});
	ShowTop();
}

void Shell::Pop() {
	if (m_stack.empty())
		return;
	DestroyLayer(m_stack.back());
	m_stack.pop_back();
	if (m_stack.empty() && !m_session) {
		Home();
		return;
	}
	ShowTop();
}

void Shell::Replace(std::unique_ptr<Screen> screen) {
	if (!m_stack.empty()) {
		DestroyLayer(m_stack.back());
		m_stack.pop_back();
	}
	Push(std::move(screen));
}

void Shell::ClearScreens() {
	for (Layer &layer : m_stack)
		DestroyLayer(layer);
	m_stack.clear();
	m_gui.ClearKeyboardFocus();
	if (m_session)
		m_session->SetCaptured(true);
	m_gui.Layout().MarkDirty();
}

void Shell::Home() {
	ClearScreens();
	Push(MakeMainMenu(*this));
}

bool Shell::HasScreen(const String &id) const {
	return std::any_of(m_stack.begin(), m_stack.end(), [&](const Layer &l) { return l.screen->Id() == id; });
}

void Shell::Rebuild() {
	if (m_stack.empty())
		return;
	Layer &top = m_stack.back();
	DestroyLayer(top);
	top.root = MakeLayerRoot(top.screen->Overlay());
	top.screen->Build(top.root);
	m_gui.Layout().MarkDirty();
}

void Shell::RebuildAll() {
	for (Layer &layer : m_stack)
		DestroyLayer(layer);
	ShowTop();
	if (m_session) {
		m_session->BuildHud(m_hudLayer);
	}
}

// ── Dialogues et notifications ───────────────────────────────────────────────

void Shell::Confirm(ConfirmSpec spec) { Push(MakeConfirm(*this, std::move(spec))); }

void Shell::Notify(Tone tone, const String &title, const String &text, bool important) {
	if (m_settings.notifications == 0 || (m_settings.notifications == 1 && !important && tone != Tone::DANGER && tone != Tone::WARNING))
		return;
	Kit &k = *m_kit;
	const Palette &p = k.Colors();
	const sdl3::FColor c = k.ToneColor(tone);
	WidgetBuilder card = k.Row(12.f);
	card.GrowW().Pad(math::Sides(14.f, 12.f)).Radius(10.f).Bg(WithAlpha(p.bg1, 0.96f)).BorderColor(WithAlpha(c, 0.7f), 1.f);
	card.Align(ui::CrossAlign::Start).PointerThrough().Parent(m_toastLayer);
	const ecs::Entity e = card.Spawn();
	const MaterialIcons icon = tone == Tone::DANGER ? MaterialIcons::ERROR
							   : tone == Tone::WARNING ? MaterialIcons::WARNING
							   : tone == Tone::SUCCESS ? MaterialIcons::CHECK_CIRCLE
													   : MaterialIcons::INFO;
	k.Icon(icon, 22.f, Some(c)).Parent(e).Spawn();
	WidgetBuilder col = k.Column(2.f);
	col.GrowW().Parent(e);
	const ecs::Entity t = col.Spawn();
	k.Label(title, Text::BODY).Bold().GrowW().TextWrap().Parent(t).Spawn();
	if (!text.IsEmpty())
		k.Paragraph(text, Text::SMALL).Parent(t).Spawn();
	m_toasts.push_back(Toast{e, std::max(1.5f, m_settings.notificationSeconds) + (tone == Tone::DANGER ? 2.f : 0.f)});
	while (m_toasts.size() > 4) {
		m_kit->Destroy(m_toasts.front().entity);
		m_toasts.pop_front();
	}
	UiSound(tone == Tone::DANGER ? "ui_error" : tone == Tone::WARNING ? "ui_warning" : "ui_notify");
	m_gui.Layout().MarkDirty();
}

void Shell::UpdateToasts(float dt) {
	for (Toast &t : m_toasts)
		t.ttl -= dt;
	while (!m_toasts.empty() && m_toasts.front().ttl <= 0.f) {
		m_kit->Destroy(m_toasts.front().entity);
		m_toasts.pop_front();
	}
}

void Shell::ShowError(const String &title, const String &message, const String &details) {
	Log(String("[erreur] ") + title + String(" : ") + message);
	Push(MakeError(*this, title, message, details));
}

// ── Parties ──────────────────────────────────────────────────────────────────

void Shell::PlayWorld(const String &folder) {
	if (m_session)
		LeaveWorld(true);
	ClearScreens();
	m_loadingWorld = folder;
	Push(MakeLoading(*this, folder));
	m_loadingDelay = 2; // l'écran de chargement s'affiche avant le travail
	if (m_menuMusic != audio::AudioEngine::NO_VOICE) {
		m_backdrop->audio->Stop(m_menuMusic);
		m_menuMusic = audio::AudioEngine::NO_VOICE;
	}
}

void Shell::StartLoading() {
	m_session = std::make_unique<Session>(*this, m_loadingWorld);
	m_loadingWorld = String();
}

void Shell::LeaveWorld(bool save) {
	if (!m_session)
		return;
	if (save)
		m_session->Save(false);
	m_kit->Destroy(m_hudLayer);
	WidgetBuilder hud = m_gui.Factory().Column();
	hud.Fixed().Anchor(ui::Anchor::TopLeft).W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f)).PointerThrough().Name("hud-layer");
	m_hudLayer = hud.Spawn();
	m_gui.World().Query<ui::UiName>([](ecs::Entity, ui::UiName &) {});
	(void)m_window.SetRelativeMouseMode(false);
	m_session.reset();
	m_worlds = WorldCatalog(m_options.saves);
	Home();
}

// ── Préférences ──────────────────────────────────────────────────────────────

void Shell::SaveSettings() {
	if (Result<bool, String> r = m_settings.Save(m_options.settingsPath); r.IsError())
		Notify(Tone::DANGER, "Réglages non enregistrés", r.Error(), true);
}

void Shell::ApplySettings(bool rebuild) {
	m_kit->Apply();
	(void)m_renderer.SetVSync(m_settings.vsync ? sdl3::Renderer::VSync::EVERY_REFRESH : sdl3::Renderer::VSync::DISABLED);
	m_window.SetFullscreen(m_settings.displayMode == 1);
	if (m_settings.displayMode == 0 && !m_options.headless && (m_window.GetWidth() != m_settings.width || m_window.GetHeight() != m_settings.height))
		m_window.SetSize(m_settings.width, m_settings.height);
	const float master = m_settings.masterVolume;
	if (m_backdrop && m_backdrop->audio)
		m_backdrop->audio->SetMasterVolume(master);
	if (m_session)
		m_session->ApplySettings();
	SaveSettings();
	if (rebuild)
		m_rebuildAll = true;
}

void Shell::UiSound(const char *id) {
	audio::AudioEngine *engine = m_session ? &m_session->Audio() : m_backdrop ? m_backdrop->audio.get() : nullptr;
	voxel::Game *game = m_session ? &m_session->Game() : MenuGame();
	if (!engine || !game)
		return;
	const String sound = String("core:") + String(id);
	const voxel::SoundDef *def = game->Content().FindSound(sound);
	if (!def)
		return;
	const int variant = def->paths.size() > 1 ? int(std::rand() % int(def->paths.size())) : 0;
	audio::AudioPlayOptions o;
	o.volume = std::clamp(def->volume * m_settings.uiVolume, 0.f, 2.f);
	(void)engine->Play(resources::AssetRef<audio::AudioClipResource>{game->Assets().SoundId(sound, variant)}, o);
}

void Shell::UpdateMenuAudio(float) {
	if (m_session || !m_backdrop || !m_backdrop->audio)
		return;
	voxel::Game *game = MenuGame();
	if (m_menuMusic != audio::AudioEngine::NO_VOICE && m_backdrop->audio->IsPlaying(m_menuMusic)) {
		(void)m_backdrop->audio->SetVolume(m_menuMusic, 0.4f * m_settings.musicVolume);
		return;
	}
	const voxel::SoundDef *def = game ? game->Content().FindSound("core:music_menu") : nullptr;
	if (!def || m_settings.musicVolume <= 0.f)
		return;
	audio::AudioPlayOptions o;
	o.volume = 0.4f * m_settings.musicVolume;
	o.loop = true;
	m_menuMusic = m_backdrop->audio->Play(resources::AssetRef<audio::AudioClipResource>{game->Assets().SoundId("core:music_menu", 0)}, o);
}

void Shell::Screenshot(const String &path) {
	String target = path;
	if (target.IsEmpty()) {
		const std::time_t t = std::time(nullptr);
		char name[64];
		std::strftime(name, sizeof(name), "capture-%Y%m%d-%H%M%S.png", std::localtime(&t));
		target = m_options.screenshots + String("/") + String(name);
	}
	std::error_code ec;
	fs::create_directories(fs::path(target.CStr()).parent_path(), ec);
	if (auto surface = m_renderer.ReadPixels(); surface && sdl3::ImgSavePng(surface.Value(), target))
		Notify(Tone::SUCCESS, "Capture enregistrée", target);
	else
		Notify(Tone::DANGER, "Capture impossible", String(), true);
}

// ── Boucle ───────────────────────────────────────────────────────────────────

void Shell::HandleEvent(sdl3::Event &e) {
	if (e.IsQuit()) {
		m_running = false;
		return;
	}
	// Mode « saisie d'une touche » : la touche va à l'écran, pas à l'interface.
	Screen *top = Top();
	if (top && top->Id() == "keycapture") {
		if (e.IsKeyDown() && top->OnKey(int(e.Key().scancode)))
			return;
		if (e.IsMouseDown() && top->OnKey(MOUSE_BASE + int(e.MouseButton().button)))
			return;
		if (e.IsMouseWheel() && top->OnKey(e.MouseWheel().y > 0 ? WHEEL_UP : WHEEL_DOWN))
			return;
	}
	if (e.Type() == SDL_EVENT_WINDOW_FOCUS_LOST && m_session && m_settings.pauseOnFocusLoss &&
		m_stack.empty())
		Push(MakePause(*this));
	// Complétion et historique des champs de saisie (discussion, terminal).
	if (top && e.IsKeyDown() && (e.Key().scancode == SDL_SCANCODE_TAB || e.Key().scancode == SDL_SCANCODE_UP ||
								 e.Key().scancode == SDL_SCANCODE_DOWN) &&
		top->OnKey(int(e.Key().scancode)))
		return;
	const bool captured = m_session && m_session->Captured();
	if (!captured)
		m_gui.HandleEvent(e);
	if (e.IsConsumed())
		return;
	if (e.IsKeyDown()) {
		const int code = int(e.Key().scancode);
		if (m_settings.Matches("fullscreen", code)) {
			m_settings.displayMode = m_settings.displayMode == 1 ? 0 : 1;
			ApplySettings(false);
			return;
		}
		if (m_settings.Matches("screenshot", code)) {
			Screenshot();
			return;
		}
		if (top) {
			if (top->OnKey(code))
				return;
			if (code == SDL_SCANCODE_ESCAPE) {
				if (!top->OnBack()) {
					UiSound("ui_back");
					if (top->Id() == "mainmenu")
						return;
					Pop();
				}
				return;
			}
		}
	}
	if (m_session && m_session->Ready() && (!top || top->PassThrough()))
		m_session->HandleEvent(e, true);
}

void Shell::Update(float dt, long frame) {
	(void)frame;
	if (m_loadingDelay > 0 && --m_loadingDelay == 0 && !m_loadingWorld.IsEmpty())
		StartLoading();
	if (m_rebuildAll) {
		m_rebuildAll = false;
		RebuildAll();
	}
	const bool blocking = !m_stack.empty() && !m_stack.back().screen->PassThrough();
	if (m_session && m_session->Ready()) {
		const bool paused = HasScreen("pause") || HasScreen("settings") || HasScreen("worldsettings");
		m_session->Update(dt, paused, blocking);
		// Sauvegarde automatique (et vignette du monde).
		if (m_settings.autosaveMinutes > 0 && !paused) {
			m_autosaveTimer += dt;
			if (m_autosaveTimer > float(m_settings.autosaveMinutes) * 60.f) {
				m_autosaveTimer = 0.f;
				if (!blocking)
					pendingThumbnail = m_worlds.Directory() + String("/") + m_session->Folder() + String("/thumbnail.png");
				m_session->Save(true);
			}
		}
	}
	if (m_backdrop)
		m_backdrop->Update(dt, !m_session || !m_session->Ready());
	UpdateViewport();
	UpdateMenuAudio(dt);
	for (size_t i = 0; i < m_stack.size(); ++i)
		if (m_stack[i].root.Valid())
			m_stack[i].screen->Update(dt);
	UpdateToasts(dt);
}

void Shell::UpdateViewport() {
	auto vp = m_gui.World().GetComponent<ui::UiViewport3D>(m_viewport);
	if (vp.IsNone())
		return;
	render3d::Camera camera;
	render3d::Object3D *root = nullptr;
	if (m_session && m_session->Ready()) {
		camera = m_session->Camera();
		root = &m_session->Renderer().Root();
	} else if (m_backdrop && m_backdrop->renderer) {
		camera = m_backdrop->camera;
		root = &m_backdrop->renderer->Root();
	}
	if (vp.Unwrap()->textureHeight > 0)
		camera.aspect = float(vp.Unwrap()->textureWidth) / float(vp.Unwrap()->textureHeight);
	vp.Unwrap()->camera = camera;
	vp.Unwrap()->root = root;
}

// ── Automatisation (captures d'écran pilotées) ──────────────────────────────

void Shell::GameCommand(const String &line) {
	if (m_session && m_session->Ready())
		(void)m_session->Command(line);
}

void Shell::AppCommand(const String &line) {
	std::vector<String> w;
	size_t start = 0;
	while (start < line.GetSize()) {
		size_t end = line.Find(" ", start);
		if (end == String::NPOS)
			end = line.GetSize();
		if (end > start)
			w.push_back(line.Substring(start, end));
		start = end + 1;
	}
	if (w.empty())
		return;
	const String cmd = w[0];
	const String arg = w.size() > 1 ? w[1] : String();
	const String arg2 = w.size() > 2 ? w[2] : String();
	auto rest = [&](size_t from) {
		String out;
		for (size_t i = from; i < w.size(); ++i)
			out += (i > from ? String(" ") : String()) + w[i];
		return out;
	};
	if (cmd == "!play") {
		PlayWorld(arg);
	} else if (cmd == "!create") { // !create <nom> [graine] [générateur] : crée puis joue
		dataio::NodePtr settings = dataio::Node::MakeObject();
		settings->Set("gameMode", voxel::json::MakeStr(arg2.IsEmpty() ? String("survival") : arg2));
		if (Result<String, String> r = m_worlds.Create(arg, uint32_t(std::atol(w.size() > 3 ? w[3].CStr() : "1234")), String(), settings);
			r.IsOk())
			PlayWorld(r.Value());
	} else if (cmd == "!back") {
		Pop();
	} else if (cmd == "!home") {
		if (m_session)
			LeaveWorld(true);
		else
			Home();
	} else if (cmd == "!clear") {
		ClearScreens();
	} else if (cmd == "!notify") {
		const Tone tones[] = {Tone::INFO, Tone::SUCCESS, Tone::WARNING, Tone::DANGER};
		Notify(tones[std::clamp(std::atoi(arg.CStr()), 0, 3)], rest(2), String(), true);
	} else if (cmd == "!screen") {
		std::unique_ptr<Screen> s;
		if (arg == "main")
			s = MakeMainMenu(*this);
		else if (arg == "worlds")
			s = MakeWorldSelect(*this);
		else if (arg == "create")
			s = MakeCreateWorld(*this);
		else if (arg == "worldsettings")
			s = MakeWorldSettings(*this, arg2);
		else if (arg == "rename")
			s = MakeRenameWorld(*this, arg2);
		else if (arg == "mods")
			s = MakeModBrowser(*this);
		else if (arg == "mod")
			s = MakeModDetails(*this, arg2);
		else if (arg == "worldmods")
			s = MakeWorldMods(*this, arg2);
		else if (arg == "modconfig")
			s = MakeModConfig(*this, arg2, w.size() > 3 ? w[3] : String());
		else if (arg == "organize")
			s = MakeModOrganize(*this);
		else if (arg == "settings")
			s = MakeSettings(*this, SettingsTab(std::clamp(std::atoi(arg2.CStr()), 0, 5)));
		else if (arg == "skins")
			s = MakeSkinPicker(*this);
		else if (arg == "error")
			s = MakeError(*this, "Impossible de charger le mod « night_terrors »",
						  "Le script scripts/night.script contient une erreur : la créature « night_terrors:brute » "
						  "renvoie à un comportement inconnu.",
						  "[script night_terrors] 12:5: fonction inconnue « entity.summon »\n"
						  "  appel depuis voxel.behavior(\"rage\")\n  fichier : mods/night_terrors/scripts/night.script\n"
						  "Moteur 0.6.0 — sauvegarde v2 — SDL3 — 2 mods actifs");
		else if (arg == "pause")
			s = MakePause(*this);
		else if (arg == "inventory")
			s = MakeInventory(*this);
		else if (arg == "crafting")
			s = MakeCrafting(*this);
		else if (arg == "chat")
			s = MakeChat(*this, rest(2));
		else if (arg == "console")
			s = MakeConsole(*this, rest(2));
		else if (arg == "info")
			s = MakeWorldInfo(*this);
		else if (arg == "keycapture")
			s = MakeKeyCapture(*this, "Sauter / monter", [](int) {});
		if (s)
			Push(std::move(s));
	} else if (cmd == "!confirm") { // démonstration d'une confirmation
		ConfirmSpec spec;
		spec.title = arg == "quit" ? String("Quitter le monde ?") : String("Supprimer « Vallée des cerisiers » ?");
		spec.message = arg == "quit" ? String("La partie sera enregistrée avant de revenir au menu principal.")
									 : String("Ce monde et toutes ses constructions seront définitivement effacés.");
		spec.details = arg == "quit" ? std::vector<String>{"Progression enregistrée automatiquement", "Durée de la session : 42 min"}
									 : std::vector<String>{"Taille : 18,4 Mo — 12 h de jeu", "Cette action est irréversible"};
		spec.confirmText = arg == "quit" ? String("Quitter") : String("Supprimer définitivement");
		spec.tone = arg == "quit" ? Tone::PRIMARY : Tone::DANGER;
		spec.icon = arg == "quit" ? MaterialIcons::EXIT_TO_APP : MaterialIcons::DELETE;
		if (arg != "quit") {
			spec.checkboxText = "Créer d'abord une sauvegarde de sécurité";
			spec.checkboxValue = true;
		}
		Confirm(std::move(spec));
	} else if (cmd == "!modchanges") {
		ModChanges c;
		c.worldFolder = arg;
		c.worldName = "Vallée des cerisiers";
		c.added = {"Lanternes et luminaires 1.2.0", "Chants d'oiseaux 0.9.1"};
		c.removed = {"Nuits de terreur 0.5.0"};
		c.updated = {"Extra 0.3.0 → 0.4.0"};
		c.affectedDependencies = {"Contenu de base (requis par Lanternes) : déjà actif"};
		c.risks = {"Les blocs « night_terrors:* » déjà posés deviendront des blocs inconnus",
				   "Les créatures « night_terrors:brute » enregistrées seront ignorées"};
		Push(MakeModChanges(*this, std::move(c), [](bool) {}));
	} else if (cmd == "!trade" || cmd == "!dialog") {
		if (m_session && m_session->Ready()) {
			voxel::Game &game = m_session->Game();
			const String id = cmd == "!trade" ? String(arg.IsEmpty() ? "core:villager" : arg.CStr()) : String("core:guard");
			const ecs::Entity npc = game.SpawnEntity(id, game.SpawnPoint() + math::FVector3{2.f, 0.f, 2.f});
			(void)game.Interact(m_session->Player(), npc);
		}
	} else if (cmd == "!cmd") {
		GameCommand(rest(1));
	} else if (cmd == "!type") { // texte dans le champ qui a le focus
		if (auto in = m_gui.World().GetComponent<ui::UiInput>(m_gui.KeyboardFocus()); in.IsSome()) {
			in.Unwrap()->text = rest(1);
			in.Unwrap()->cursor = in.Unwrap()->text.GetSize();
			m_gui.Layout().MarkDirty();
		}
	} else if (cmd == "!look" && m_session && m_session->Ready()) {
		if (auto t = m_session->Game().Registry().GetComponent<voxel::Transform>(m_session->Player()); t.IsSome()) {
			t.Unwrap()->yaw = float(std::atof(arg.CStr())) * 3.14159265f / 180.f;
			t.Unwrap()->pitch = float(std::atof(arg2.CStr())) * 3.14159265f / 180.f;
		}
	} else if (cmd == "!set") { // !set réglage valeur
		dataio::NodePtr json = m_settings.ToJson();
		if (voxel::json::Has(json, arg.CStr())) {
			const dataio::NodePtr current = voxel::json::At(json, arg.CStr());
			if (current->IsBool())
				json->Set(arg, voxel::json::MakeBool(arg2 == "true" || arg2 == "1"));
			else if (current->IsString())
				json->Set(arg, voxel::json::MakeStr(arg2));
			else
				json->Set(arg, voxel::json::MakeNum(std::atof(arg2.CStr())));
			m_settings.FromJson(json);
			ApplySettings(true);
		}
	} else if (cmd == "!key") { // touche simulée (scancode)
		sdl3::Event ev;
		(void)ev;
		if (Screen *top = Top())
			(void)top->OnKey(std::atoi(arg.CStr()));
	} else if (m_session && m_session->Ready()) {
		GameCommand(line.Substring(1));
	}
}

} // namespace voxelgame
