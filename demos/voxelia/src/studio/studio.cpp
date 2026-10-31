// Définitions de voxelia/studio/studio.hpp
#include <voxelia/studio/studio.hpp>

#include <algorithm>
#include <cmath>

#include <voxel/components.hpp>
#include <voxelia/session.hpp>
#include <voxelia/shell.hpp>

namespace voxelia::studio {

namespace {

constexpr float OUTLINER_PERIOD = 0.2f; ///< 5 Hz
constexpr float PITCH_LIMIT = 1.5f;
constexpr float LOOK_SPEED = 0.004f;
constexpr float FLY_SPEED = 12.f;

/// Feuille du studio : propre à SA fenêtre (la fenêtre de jeu garde la sienne).
constexpr const char *STUDIO_CSS = R"css(
#studio { background: #1b1d22; color: #d8dce4; }
.studio-toolbar { background: #262a31; padding: 6px 8px; gap: 6px; align-items: center; }
.studio-title { font-size: 15px; color: #8fd16a; padding-right: 12px; }
.studio-status { color: #9aa3b2; font-size: 12px; }
.studio-dock { background: #20232a; padding: 6px; gap: 4px; }
.studio-dock-title { color: #8fd16a; font-size: 13px; padding-bottom: 4px; }
.studio-group { color: #e0b45a; font-size: 12px; padding-top: 6px; }
.studio-row { padding: 2px 6px; font-size: 12px; }
.studio-row:hover { background: #2f3540; }
.studio-row.selected { background: #35506b; }
.studio-dead { color: #c96565; }
.studio-section { color: #e0b45a; font-size: 12px; padding-top: 8px; }
.studio-key { color: #9aa3b2; font-size: 12px; width: 130px; }
.studio-value { font-size: 12px; }
.studio-log-ok { color: #8fd16a; font-size: 12px; }
.studio-log-error { color: #ff6b6b; font-size: 12px; }
.studio-log-warning { color: #f0c14b; font-size: 12px; }
.studio-log-confirm { color: #f0c14b; font-size: 12px; }
.studio-log-pending { color: #7fb6ff; font-size: 12px; }
.studio-log-input { color: #d8dce4; font-size: 12px; }
.studio-log-info { color: #9aa3b2; font-size: 12px; }
)css";

math::FVector3 Add(const math::FVector3 &a, const math::FVector3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
math::FVector3 Scale(const math::FVector3 &a, float s) { return {a.x * s, a.y * s, a.z * s}; }

Option<float> ParseFloat(const String &s) {
	char *end = nullptr;
	const float v = std::strtof(s.CStr(), &end);
	if (!end || end == s.CStr() || *end != '\0')
		return NONE;
	return Some(v);
}

} // namespace

// ── Caméra ───────────────────────────────────────────────────────────────────

math::FVector3 EditorCamera::Forward() const noexcept {
	const float c = std::cos(pitch);
	return {std::sin(yaw) * c, std::sin(pitch), std::cos(yaw) * c};
}

render3d::Camera EditorCamera::ToCamera(float aspect, float farPlane) const {
	render3d::Camera camera;
	camera.position = position;
	camera.target = Add(position, Forward());
	camera.aspect = aspect > 0.f ? aspect : 16.f / 9.f;
	camera.farPlane = farPlane;
	return camera;
}

void EditorCamera::Look(float dyaw, float dpitch) noexcept {
	yaw += dyaw;
	pitch = std::clamp(pitch + dpitch, -PITCH_LIMIT, PITCH_LIMIT);
}

void EditorCamera::LookAt(const math::FVector3 &target) noexcept {
	const float dx = target.x - position.x, dy = target.y - position.y, dz = target.z - position.z;
	const float flat = std::sqrt(dx * dx + dz * dz);
	if (flat < 1e-4f && std::fabs(dy) < 1e-4f)
		return;
	yaw = std::atan2(dx, dz);
	pitch = std::clamp(std::atan2(dy, flat), -PITCH_LIMIT, PITCH_LIMIT);
}

// ── Studio ───────────────────────────────────────────────────────────────────

Studio::Studio(Shell &shell, ui::WindowContext &window, StudioReport *report, String viewportTransfer)
	: m_shell(shell), m_window(window), m_report(report), m_transfer(std::move(viewportTransfer)) {
	m_window.Gui().AddStyleSheet(StringView(STUDIO_CSS));
	m_blocks = std::make_unique<BlockEditor>(m_shell, m_report, [this] {
		// Le bloc visé : 4 blocs devant la caméra de l'éditeur.
		return voxel::BlockPos::FromWorld(Add(m_camera.position, Scale(m_camera.Forward(), 4.f)));
	});
	RegisterCommands();
	Build();
	m_console.Print("Voxelia Studio — « help » liste les commandes ; toute autre ligne est une commande du jeu.");
	Visit("studio.open", FeatureStatus::Ok,
		  String(m_window.SharesGpuDevice() ? "device GPU partagé avec la fenêtre de jeu" : "device GPU NON partagé"));
}

Studio::~Studio() {
	if (m_flying && m_window.Window())
		(void)m_window.Window()->SetRelativeMouseMode(false);
	m_window.Root().Clear();
}

voxel::Game *Studio::Game() {
	Session *session = m_shell.Game();
	return session && session->Ready() ? &session->Game() : nullptr;
}

void Studio::Visit(const char *feature, FeatureStatus status, String detail) {
	if (m_report)
		m_report->Visit(String(feature), status, std::move(detail));
}

void Studio::Build() {
	ui::Ui &gui = m_window.Gui();
	ui::Widget filter, blockCatalog, blockEditor, rightTabs;
	gui.Mount(ui::Column().Id("studio").Fill().Children({
		ui::Row().Class("studio-toolbar").Children({
			ui::Label("Voxelia Studio").Class("studio-title"),
			ui::Button("Pause").Id("studio-pause").Ref(m_pauseButton).Tooltip("Suspendre la simulation").OnClick([this] {
				(void)Execute(m_shell.SimulationHeld() ? String("studio.play") : String("studio.pause"));
			}),
			ui::Button("Pas").Id("studio-step").Tooltip("Avancer d'un pas de simulation").OnClick([this] {
				(void)Execute(String("studio.step"));
			}),
			ui::Button("Suivre").Id("studio-follow").Ref(m_followButton).Tooltip("Caméra poursuite de la sélection").OnClick([this] {
				(void)Execute(String("studio.follow"));
			}),
			ui::Button("Au joueur").Id("studio-to-player").OnClick([this] { (void)Execute(String("studio.camera player")); }),
			ui::Spacer(),
			ui::Label("").Class("studio-status").Ref(m_status),
		}),
		ui::Splitter(
			ui::TabView({String("Outliner"), String("Blocs")}).Class("studio-dock").Fill().Children({
			ui::Column().Id("studio-outliner").Fill().Children({
				ui::Label("World Outliner").Class("studio-dock-title"),
				ui::Input("Filtrer (nom, id)…").Id("outliner-filter").Ref(filter).OnTextChange([this](const String &text) {
					m_filter.text = text;
					m_outlinerTimer = OUTLINER_PERIOD; // rafraîchi à la prochaine image
					Visit("outliner.filter");
				}),
				ui::Label("").Class("studio-status").Ref(m_outlinerCount),
				ui::ScrollView().Id("outliner-list").Grow().Ref(m_outlinerList),
			}),
			ui::Column().Id("studio-block-catalog").Fill().Ref(blockCatalog),
			}),
			ui::Splitter(
				ui::Splitter(ui::Viewport3D(m_emptyScene).Id("studio-viewport").Fill().Ref(m_viewport),
							 ui::Column().Class("studio-dock").Id("studio-console").Fill().Children({
								 ui::Label("Console").Class("studio-dock-title"),
								 ui::ScrollView().Id("console-log").Grow().Ref(m_consoleLog),
								 ui::Row().Ref(m_consoleBar).Children({
									 ui::Input("commande (help)").Id("console-input").Grow().Ref(m_consoleInput)
										 .OnSubmit([this](const String &line) {
											 (void)Execute(line);
											 m_consoleInput.SetText(String());
										 })
										 .OnKey(sdl3::keyboard::Key::UP, [this] { m_consoleInput.SetText(m_console.HistoryStep(-1)); })
										 .OnKey(sdl3::keyboard::Key::DOWN, [this] { m_consoleInput.SetText(m_console.HistoryStep(1)); }),
								 }),
							 }),
							 ui::Orientation::Vertical, 520.f, 160.f, 90.f),
				ui::TabView({String("Inspecteur"), String("Bloc")}).Id("studio-right-tabs").Class("studio-dock").Fill().Ref(rightTabs).Children({
				ui::Column().Id("studio-inspector").Fill().Children({
					ui::Label("Inspecteur").Class("studio-dock-title"),
					ui::Row().Gap(4.f).Children({
						ui::Button("Soigner").Id("inspector-heal").OnClick([this] { (void)Execute(String("studio.heal")); }),
						ui::Button("Tuer").Id("inspector-kill").OnClick([this] { (void)Execute(String("studio.kill")); }),
						ui::Button("Voir").Id("inspector-focus").OnClick([this] { (void)Execute(String("studio.focus")); }),
					}),
					ui::ScrollView().Id("inspector").Grow().Ref(m_inspector),
				}),
				ui::Column().Id("studio-block-editor").Fill().Ref(blockEditor),
				}),
				ui::Orientation::Horizontal, 680.f, 320.f, 220.f),
			ui::Orientation::Horizontal, 260.f, 180.f, 500.f)
			.Grow(),
		ui::Label("").Id("studio-stats").Class("studio-status").Ref(m_stats),
	}));
	m_stats.Hide();
	m_blocks->BuildCatalog(blockCatalog);
	m_blocks->Build(blockEditor);
	m_blocks->onDocumentOpened = [rightTabs]() mutable { rightTabs.SetSelected(1, true); };
	if (auto vp = m_viewport.Get<ui::Viewport3DState>(); vp.IsSome())
		vp.Unwrap()->transfer = m_transfer == "readback"	? ui::ViewportTransfer::READBACK
								: m_transfer == "pipelined" ? ui::ViewportTransfer::PIPELINED
															: ui::ViewportTransfer::DIRECT;
	RebuildOutliner();
}

void Studio::RegisterCommands() {
	m_console.Register("studio.select", "studio.select player | <entité|nom> — sélectionne une entité", [this](const auto &args) {
		if (args.size() < 2)
			return CommandResult::Error(String("usage : studio.select <entité|nom>"));
		voxel::Game *game = Game();
		if (!game)
			return CommandResult::Error(String("aucune partie en cours"));
		if (args[1] == "player") {
			Select(m_shell.Game()->Player());
			return CommandResult::Ok(String("sélection : joueur"));
		}
		const std::vector<OutlinerEntry> all = CollectEntities(*game, {}, m_camera.position);
		const Option<float> number = ParseFloat(args[1]);
		for (const OutlinerEntry &e : all)
			if ((number.IsSome() && e.entity.id == uint32_t(number.Value())) || e.name == args[1] || e.id == args[1]) {
				Select(e.entity);
				return CommandResult::Ok(String("sélection : ") + e.name);
			}
		return CommandResult::Error(String("entité introuvable : ") + args[1]);
	});
	m_console.Register("studio.deselect", "studio.deselect — vide la sélection", [this](const auto &) {
		Select(ecs::Entity{});
		return CommandResult::Ok();
	});
	m_console.Register("studio.focus", "studio.focus — caméra sur la sélection", [this](const auto &) {
		if (!m_selected.Valid())
			return CommandResult::Error(String("aucune sélection"));
		FocusOn(m_selected);
		return CommandResult::Ok();
	});
	m_console.Register("studio.follow", "studio.follow — poursuite de la sélection (bascule)", [this](const auto &) {
		if (!m_follow && !m_selected.Valid())
			return CommandResult::Error(String("aucune sélection à suivre"));
		m_follow = !m_follow;
		Visit("camera.follow");
		return CommandResult::Ok(String(m_follow ? "poursuite" : "caméra libre"));
	});
	m_console.Register("studio.camera", "studio.camera player | <x> <y> <z> — place la caméra", [this](const auto &args) {
		voxel::Game *game = Game();
		if (args.size() == 2 && args[1] == "player") {
			if (!game)
				return CommandResult::Error(String("aucune partie en cours"));
			m_follow = false;
			FocusOn(m_shell.Game()->Player());
			return CommandResult::Ok();
		}
		if (args.size() != 4)
			return CommandResult::Error(String("usage : studio.camera player | <x> <y> <z>"));
		const Option<float> x = ParseFloat(args[1]), y = ParseFloat(args[2]), z = ParseFloat(args[3]);
		if (x.IsNone() || y.IsNone() || z.IsNone())
			return CommandResult::Error(String("coordonnées invalides"));
		m_follow = false;
		m_camera.position = {x.Value(), y.Value(), z.Value()};
		m_cameraPlaced = true;
		Visit("camera.move");
		return CommandResult::Ok();
	});
	m_console.Register("studio.pause", "studio.pause — suspend la simulation", [this](const auto &) {
		m_shell.HoldSimulation(true);
		Visit("simulation.pause");
		return CommandResult::Ok(String("simulation suspendue"));
	});
	m_console.Register("studio.play", "studio.play — reprend la simulation", [this](const auto &) {
		m_shell.HoldSimulation(false);
		Visit("simulation.play");
		return CommandResult::Ok(String("simulation reprise"));
	});
	m_console.Register("studio.step", "studio.step [n] — n pas de simulation (pause)", [this](const auto &args) {
		int steps = 1;
		if (args.size() > 1) {
			const Option<float> n = ParseFloat(args[1]);
			if (n.IsNone() || n.Value() < 1.f)
				return CommandResult::Error(String("nombre de pas invalide"));
			steps = int(n.Value());
		}
		m_shell.HoldSimulation(true);
		m_shell.StepSimulation(steps);
		Visit("simulation.step");
		return CommandResult::Ok(String::Format("%d pas", steps));
	});
	m_console.Register("studio.heal", "studio.heal — rend ses PV à la sélection", [this](const auto &) {
		voxel::Game *game = Game();
		if (!game || !m_selected.Valid())
			return CommandResult::Error(String("aucune sélection"));
		auto health = game->Registry().GetComponent<voxel::Health>(m_selected);
		if (health.IsNone())
			return CommandResult::Warning(String("cette entité n'a pas de santé"));
		health.Unwrap()->current = health.Unwrap()->max;
		Visit("inspector.edit");
		return CommandResult::Ok(String("PV restaurés"));
	});
	m_console.Register("studio.kill", "studio.kill — tue la sélection (confirmation)", [this](const auto &) {
		if (!Game() || !m_selected.Valid())
			return CommandResult::Error(String("aucune sélection"));
		const ecs::Entity target = m_selected;
		return CommandResult::Confirm(String("Tuer l'entité sélectionnée ?"), [this, target] {
			voxel::Game *game = Game();
			if (!game || !game->Registry().IsAlive(target))
				return CommandResult::Error(String("l'entité n'existe plus"));
			game->Kill(target);
			Visit("inspector.kill");
			return CommandResult::Ok(String("entité tuée"));
		});
	});
	m_console.Register("studio.stats", "studio.stats — statistiques détaillées (F3)", [this](const auto &) {
		m_showStats = !m_showStats;
		m_showStats ? m_stats.Show() : m_stats.Hide();
		Visit("stats.toggle");
		return CommandResult::Ok();
	});
	m_console.Register("reload", "reload [block <id> | mod <nom> | dimension <id> | biome <id>] — rechargement à chaud", [this](const auto &args) {
		if (args.size() == 1) // le bloc ouvert dans l'éditeur
			return m_blocks->HasDocument() ? m_blocks->Apply() : CommandResult::Error(String("usage : reload block <id> | reload mod <nom>"));
		if (args.size() == 3 && args[1] == "block")
			return m_blocks->ReloadFromDisk(args[2]);
		if (args.size() == 3 && args[1] == "mod")
			return m_blocks->ReloadMod(args[2]);
		if (args.size() == 3 && (args[1] == "dimension" || args[1] == "biome"))
			return CommandResult::Warning(String("reload ") + args[1] +
										  String(" : le terrain déjà généré ne change pas — régénération dans une dimension temporaire : étape 7 du studio"));
		return CommandResult::Error(String("usage : reload [block <id> | mod <nom>]"));
	});
	m_console.Register("studio.block.open", "studio.block.open <id> — ouvre un bloc dans l'éditeur", [this](const auto &args) {
		return args.size() == 2 ? m_blocks->Open(args[1]) : CommandResult::Error(String("usage : studio.block.open <id>"));
	});
	m_console.Register("studio.block.new", "studio.block.new <id> [mod] — nouveau bloc", [this](const auto &args) {
		return args.size() >= 2 ? m_blocks->New(args[1], args.size() > 2 ? args[2] : String("core"))
								: CommandResult::Error(String("usage : studio.block.new <id> [mod]"));
	});
	m_console.Register("studio.block.set", "studio.block.set <champ> <json> — écrit un champ (null : le retire)", [this](const auto &args) {
		if (args.size() < 3)
			return CommandResult::Error(String("usage : studio.block.set <champ> <json>"));
		String value = args[2];
		for (size_t i = 3; i < args.size(); ++i)
			value += String(" ") + args[i];
		return m_blocks->SetField(args[1], value);
	});
	m_console.Register("studio.block.scope", "studio.block.scope [état] — portée d'édition", [this](const auto &args) {
		return m_blocks->SetScope(args.size() > 1 ? args[1] : String());
	});
	m_console.Register("studio.block.apply", "studio.block.apply — rechargement à chaud (Ctrl+Maj+R)", [this](const auto &) { return m_blocks->Apply(); });
	m_console.Register("studio.block.save", "studio.block.save — enregistre dans le mod", [this](const auto &) { return m_blocks->Save(); });
	m_console.Register("studio.block.place", "studio.block.place — pose devant la caméra", [this](const auto &) { return m_blocks->Place(); });
	m_console.Register("studio.block.undo", "studio.block.undo — annule (Ctrl+Z)", [this](const auto &) { return m_blocks->Undo(); });
	m_console.Register("studio.block.redo", "studio.block.redo — rétablit (Ctrl+Y)", [this](const auto &) { return m_blocks->Redo(); });
	m_console.SetFallback([this](const String &line) {
		Session *session = m_shell.Game();
		if (!session || !session->Ready())
			return CommandResult::Error(String("aucune partie en cours : commande du jeu impossible"));
		Result<String, String> r = session->Command(line);
		Visit("console.game_command", r.IsOk() ? FeatureStatus::Ok : FeatureStatus::Warning);
		return r.IsOk() ? CommandResult::Ok(r.Value()) : CommandResult::Error(r.Error());
	});
}

CommandResult Studio::Execute(const String &line) {
	CommandResult result = m_console.Submit(line);
	if (m_report)
		m_report->Count(String("console.") + String(CommandStatusId(result.status)));
	Visit("console.command", FeatureStatus::Ok, String(CommandStatusId(result.status)));
	return result;
}

void Studio::Select(ecs::Entity entity) {
	if (entity == m_selected)
		return;
	m_selected = entity;
	if (!entity.Valid())
		m_follow = false;
	m_inspectorSignature = String();
	m_outlinerSignature = String(); // surlignage de la ligne
	RefreshOutliner();
	if (entity.Valid())
		Visit("outliner.select");
}

void Studio::FocusOn(ecs::Entity entity) {
	voxel::Game *game = Game();
	if (!game || !game->Registry().IsAlive(entity))
		return;
	auto t = game->Registry().GetComponent<voxel::Transform>(entity);
	if (t.IsNone())
		return;
	const math::FVector3 at = Add(t.Unwrap()->position, {0.f, 1.f, 0.f});
	m_camera.position = Add(at, {-4.f, 3.f, -4.f});
	m_camera.LookAt(at);
	m_cameraPlaced = true;
	Visit("camera.focus");
}

void Studio::HandleEvent(const sdl3::Event &ev, bool consumed) {
	const uint32_t type = ev.raw.type;
	const sdl3::FRect box = m_viewport.Bounds();
	auto inside = [&](float x, float y) { return x >= box.x && y >= box.y && x < box.x + box.w && y < box.y + box.h; };
	if (type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.raw.button.button == SDL_BUTTON_RIGHT && inside(ev.raw.button.x, ev.raw.button.y)) {
		m_flying = true;
		m_follow = false;
		m_window.SetKeyboardCapture(true);
		if (m_window.Window())
			(void)m_window.Window()->SetRelativeMouseMode(true);
		Visit("camera.fly");
		return;
	}
	if (type == SDL_EVENT_MOUSE_BUTTON_UP && ev.raw.button.button == SDL_BUTTON_RIGHT && m_flying) {
		m_flying = false;
		m_window.SetKeyboardCapture(false);
		if (m_window.Window())
			(void)m_window.Window()->SetRelativeMouseMode(false);
		return;
	}
	if (type == SDL_EVENT_MOUSE_MOTION && m_flying) {
		m_camera.Look(-ev.raw.motion.xrel * LOOK_SPEED, -ev.raw.motion.yrel * LOOK_SPEED);
		m_cameraPlaced = true;
		return;
	}
	if (type == SDL_EVENT_MOUSE_WHEEL && !consumed && inside(ev.raw.wheel.mouse_x, ev.raw.wheel.mouse_y)) {
		m_wheel += ev.raw.wheel.y;
		return;
	}
	if (consumed || type != SDL_EVENT_KEY_DOWN || m_window.TextFieldHasFocus())
		return;
	const bool ctrl = (ev.raw.key.mod & SDL_KMOD_CTRL) != 0, shift = (ev.raw.key.mod & SDL_KMOD_SHIFT) != 0;
	if (ctrl && shift && ev.raw.key.key == SDLK_R) {
		(void)Execute(String("studio.block.apply"));
		return;
	}
	if (ctrl && !shift && ev.raw.key.key == SDLK_Z) {
		(void)m_blocks->Undo();
		return;
	}
	if (ctrl && (ev.raw.key.key == SDLK_Y || (shift && ev.raw.key.key == SDLK_Z))) {
		(void)m_blocks->Redo();
		return;
	}
	switch (ev.raw.key.key) {
	case SDLK_ESCAPE:
		// Échap : poursuite, puis sélection — jamais une sortie.
		if (m_follow)
			m_follow = false;
		else
			Select(ecs::Entity{});
		break;
	case SDLK_F3:
		(void)Execute(String("studio.stats"));
		break;
	case SDLK_F:
		if (m_selected.Valid())
			FocusOn(m_selected);
		break;
	default:
		break;
	}
}

void Studio::UpdateCamera(float dt) {
	Session *session = m_shell.Game();
	voxel::Game *game = Game();
	if (!game)
		return;
	if (!m_cameraPlaced) {
		// Premier affichage : la caméra du joueur.
		const render3d::Camera &c = session->Camera();
		m_camera.position = c.position;
		m_camera.LookAt(c.target);
		m_cameraPlaced = true;
	}
	if (m_follow && m_selected.Valid() && game->Registry().IsAlive(m_selected)) {
		auto t = game->Registry().GetComponent<voxel::Transform>(m_selected);
		if (t.IsSome()) {
			// Cap pris sur la VITESSE (pas sur l'orientation du modèle, qui
			// tourne sur place) ; à l'arrêt, le dernier cap est gardé.
			if (auto v = game->Registry().GetComponent<voxel::Velocity>(m_selected); v.IsSome()) {
				const math::FVector3 vel = v.Unwrap()->linear;
				const float speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
				if (speed > 0.5f)
					m_followHeading = {vel.x / speed, 0.f, vel.z / speed};
			}
			const math::FVector3 at = Add(t.Unwrap()->position, {0.f, 1.2f, 0.f});
			const math::FVector3 wanted = Add(at, Add(Scale(m_followHeading, -6.f), {0.f, 2.5f, 0.f}));
			const float k = std::min(1.f, dt * 6.f);
			const math::FVector3 eased = Add(m_camera.position, Scale(Add(wanted, Scale(m_camera.position, -1.f)), k));
			// Jamais dans le terrain : de la cible vers la caméra, arrêt avant
			// le premier bloc plein (une colline derrière la cible).
			const math::FVector3 ray = Add(eased, Scale(at, -1.f));
			const float length = std::sqrt(ray.x * ray.x + ray.y * ray.y + ray.z * ray.z);
			math::FVector3 safe = eased;
			if (length > 1e-3f) {
				const voxel::World &world = game->GetWorld();
				for (float d = 0.25f; d <= length; d += 0.25f) {
					const math::FVector3 p = Add(at, Scale(ray, d / length));
					if (world.BlockAt(voxel::BlockPos::FromWorld(p)).solid) {
						safe = Add(at, Scale(ray, std::max(0.f, d - 0.4f) / length));
						break;
					}
				}
			}
			m_camera.position = safe;
			m_camera.LookAt(at);
		}
	} else if (m_follow) {
		m_follow = false;
	}
	if (m_flying) {
		const bool *keys = SDL_GetKeyboardState(nullptr);
		const math::FVector3 forward = m_camera.Forward();
		const math::FVector3 right{std::cos(m_camera.yaw), 0.f, -std::sin(m_camera.yaw)};
		math::FVector3 move{0.f, 0.f, 0.f};
		if (keys[SDL_SCANCODE_W])
			move = Add(move, forward);
		if (keys[SDL_SCANCODE_S])
			move = Add(move, Scale(forward, -1.f));
		if (keys[SDL_SCANCODE_D])
			move = Add(move, Scale(right, -1.f));
		if (keys[SDL_SCANCODE_A])
			move = Add(move, right);
		if (keys[SDL_SCANCODE_E])
			move = Add(move, {0.f, 1.f, 0.f});
		if (keys[SDL_SCANCODE_Q])
			move = Add(move, {0.f, -1.f, 0.f});
		const float speed = FLY_SPEED * (keys[SDL_SCANCODE_LSHIFT] ? 3.f : 1.f);
		m_camera.position = Add(m_camera.position, Scale(move, speed * dt));
	}
	if (m_wheel != 0.f) {
		m_camera.position = Add(m_camera.position, Scale(m_camera.Forward(), m_wheel * 2.f));
		m_wheel = 0.f;
	}
}

void Studio::Update(float dt) {
	m_console.Update(dt);
	Session *session = m_shell.Game();
	voxel::Game *game = Game();
	if (game && m_selected.Valid() && !game->Registry().IsAlive(m_selected))
		Select(ecs::Entity{});
	UpdateCamera(dt);
	if (auto vp = m_viewport.Get<ui::Viewport3DState>(); vp.IsSome()) {
		const float aspect = vp.Unwrap()->textureHeight > 0
								 ? float(vp.Unwrap()->textureWidth) / float(vp.Unwrap()->textureHeight)
								 : 16.f / 9.f;
		if (game) {
			vp.Unwrap()->root = &session->Renderer().Root();
			vp.Unwrap()->camera = m_camera.ToCamera(aspect, session->Camera().farPlane);
		} else {
			vp.Unwrap()->root = &m_emptyScene;
		}
	}
	m_outlinerTimer += dt;
	if (m_outlinerTimer >= OUTLINER_PERIOD) {
		m_outlinerTimer = 0.f;
		RefreshOutliner();
	}
	RefreshInspector();
	m_blocks->Update(dt);
	RefreshConsole();
	RefreshToolbar();
}

void Studio::RefreshOutliner() {
	voxel::Game *game = Game();
	m_entries = game ? CollectEntities(*game, m_filter, m_camera.position) : std::vector<OutlinerEntry>{};
	if (m_report && game)
		m_report->Visit(String("outliner.refresh"));
	// Signature : l'ensemble affiché (pas les distances, qui bougent toujours).
	String signature = String::Format("%u|", unsigned(m_selected.id));
	for (const OutlinerEntry &e : m_entries)
		signature += String::Format("%u:%u:%d:%d,%d,%d;", unsigned(e.entity.id), unsigned(e.category), int(e.dead),
									e.chunk.x, e.chunk.y, e.chunk.z) +
					 e.name;
	if (signature == m_outlinerSignature)
		return;
	m_outlinerSignature = signature;
	RebuildOutliner();
}

void Studio::RebuildOutliner() {
	if (!m_outlinerList.Valid())
		return;
	m_outlinerList.Clear();
	if (!Game()) {
		m_outlinerCount.SetText(String("aucune partie en cours"));
		return;
	}
	m_outlinerCount.SetText(String::Format("%zu entité(s) — %s", m_entries.size(), Game()->CurrentDimension().CStr()));
	Option<EntityCategory> group = NONE;
	Option<voxel::ChunkPos> chunk = NONE;
	for (const OutlinerEntry &e : m_entries) {
		if (group.IsNone() || group.Value() != e.category) {
			group = Some(e.category);
			chunk = NONE;
			m_outlinerList.Append(ui::Label(String(EntityCategoryLabel(e.category))).Class("studio-group"));
		}
		if (chunk.IsNone() || !(chunk.Value() == e.chunk)) {
			chunk = Some(e.chunk);
			m_outlinerList.Append(ui::Label(String("  chunk ") + e.chunk.ToString()).Class("studio-status"));
		}
		const ecs::Entity entity = e.entity;
		ui::WidgetBuilder row = ui::Label(String("    ") + e.name + (e.dead ? String(" (mort)") : String()))
									.Class("studio-row")
									.Tooltip(e.id)
									.OnClick([this, entity] { Select(entity); })
									.OnDoubleClick([this, entity] {
										Select(entity);
										FocusOn(entity);
									});
		if (entity == m_selected)
			row.Class("selected");
		if (e.dead)
			row.Class("studio-dead");
		m_outlinerList.Append(row);
	}
}

void Studio::RefreshInspector() {
	voxel::Game *game = Game();
	const std::vector<InspectorSection> sections =
		game && m_selected.Valid() ? InspectEntity(*game, m_selected) : std::vector<InspectorSection>{};
	String signature;
	for (const InspectorSection &s : sections) {
		signature += s.title + String("{");
		for (const InspectorRow &r : s.rows)
			signature += r.label + String(";");
		signature += String("}");
	}
	if (signature != m_inspectorSignature) {
		// Structure changée (autre entité, composant ajouté) : reconstruction.
		m_inspectorSignature = signature;
		m_inspector.Clear();
		m_inspectorRows.clear();
		if (sections.empty())
			m_inspector.Append(ui::Label(String("Sélectionnez une entité dans l'outliner.")).Class("studio-status"));
		for (const InspectorSection &s : sections) {
			m_inspector.Append(ui::Label(s.title).Class("studio-section"));
			for (const InspectorRow &r : s.rows) {
				ui::Widget value;
				m_inspector.Append(ui::Row().Children({
					ui::Label(r.label).Class("studio-key"),
					ui::Label(r.value).Class("studio-value").Ref(value),
				}));
				m_inspectorRows.push_back({r.label, value});
			}
		}
		if (!sections.empty())
			Visit("inspector.show");
		return;
	}
	// Même structure : seules les valeurs changent (60 Hz, sans reconstruire).
	size_t i = 0;
	for (const InspectorSection &s : sections)
		for (const InspectorRow &r : s.rows) {
			if (i < m_inspectorRows.size() && m_inspectorRows[i].value.Text() != r.value)
				m_inspectorRows[i].value.SetText(r.value);
			++i;
		}
}

void Studio::RefreshConsole() {
	const auto &lines = m_console.Lines();
	if (m_consoleShown > lines.size())
		m_consoleShown = 0;
	for (size_t i = m_consoleShown; i < lines.size(); ++i)
		m_consoleLog.Append(ui::Label(lines[i].text).Class(String("studio-log-") + lines[i].kind));
	m_consoleShown = lines.size();
	// Code d'état → visibilité : fermée = seule la ligne de saisie reste.
	if (m_console.Visible())
		m_consoleLog.Show();
	else
		m_consoleLog.Hide();
}

void Studio::RefreshToolbar() {
	Session *session = m_shell.Game();
	m_pauseButton.SetText(String(m_shell.SimulationHeld() ? "Reprendre" : "Pause"));
	m_followButton.SetText(String(m_follow ? "Libérer" : "Suivre"));
	if (!session || !session->Ready()) {
		m_status.SetText(String("menu — aucune partie en cours"));
		return;
	}
	WorldRenderer &world = session->Renderer();
	const int threads = 1 + (world.Threaded() ? m_shell.Opts().meshThreads : 0) + (m_shell.Opts().threaded ? 1 : 0);
	if (m_report)
		m_report->Threads(threads);
	m_status.SetText(String::Format("%.0f img/s · %zu chunks en attente · simulation %.2f ms · maillage %.2f ms · %d fils%s",
									double(session->Fps()), world.PendingChunks(), double(session->TickMilliseconds()),
									double(world.MeshMilliseconds()), threads,
									m_shell.SimulationHeld() ? " · EN PAUSE" : ""));
	if (m_showStats) {
		const ui::FrameTimings &t = m_window.Gui().Timings();
		m_stats.SetText(String::Format("interface du studio : styles %.2f ms, mise en page %.2f ms, vue 3D %.2f ms, dessin %.2f ms · "
									   "%zu chunks maillés, %zu triangles, %zu en cours de maillage",
									   t.styleMs, t.layoutMs, t.viewport3dMs, t.drawMs, world.ChunkMeshCount(),
									   world.TriangleCount(), world.MeshingChunks()));
	}
}

} // namespace voxelia::studio
