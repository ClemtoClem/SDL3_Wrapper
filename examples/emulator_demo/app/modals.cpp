#include "modals.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <vector>

#include "sdl3/dialog.hpp"
#include "sdl3/filesystem.hpp"

#include "../emulator/hle/action_replay.hpp"
#include "../emulator/settings.hpp"
#include "application.hpp"
#include "config_location.hpp"
#include "rom_metadata.hpp"
#include "rom_source.hpp"

namespace emulator_demo::app {

namespace {

constexpr const char *ICON_TEXTURE_KEY = "emulator_demo.rom_icon";
constexpr const char *STATE_SUFFIX = ".state0";
/// Dossiers parcourus pour trouver des ROMs et des états.
constexpr const char *ROM_DIRECTORIES[] = {"assets/roms", "."};

[[nodiscard]] std::vector<String> FindFiles(std::initializer_list<const char *> patterns) {
	std::vector<String> out;
	for (const char *directory : ROM_DIRECTORIES)
		for (const char *pattern : patterns)
			for (const String &name : sdl3::filesystem::Glob(directory, pattern, true))
				out.push_back(String(directory) == "." ? name : String::Format("%s/%s", directory, name.CStr()));
	return out;
}

/// Masque toutes les pages d'un Tabview sauf `shown` — le widget ne gère la
/// visibilité qu'au CHANGEMENT d'onglet, pas pour les pages ajoutées après sa
/// création.
void ShowOnlyTab(ecs::ArchetypeRegistry &world, ecs::Entity tabview, size_t shown = 0) {
	auto children = world.GetComponent<ui::UiChildren>(tabview);
	if (children.IsNone())
		return;
	std::vector<ecs::Entity> pages = children.Unwrap()->list;
	for (size_t i = 0; i < pages.size(); ++i) {
		const bool hidden = world.HasComponent<ui::UiHidden>(pages[i]);
		if (i != shown && !hidden)
			world.AddComponent(pages[i], ui::UiHidden{});
		else if (i == shown && hidden)
			world.RemoveComponent<ui::UiHidden>(pages[i]);
	}
}

/// Onglet ouvert par la prochaine construction de la fenêtre de configuration,
/// et message à y afficher : changer de fichier de configuration la
/// reconstruit (ses autres onglets montraient les valeurs de l'ancien).
int g_configTab = 0;
String g_pathsNotice;
constexpr int PATHS_TAB = 3;

[[nodiscard]] ecs::Entity SpawnLabel(Application &app, ecs::Entity parent, const String &text, float size = 13.f,
									 bool muted = false) {
	ui::WidgetBuilder label = app.Factory().Label(text);
	label.FontSize(size).TextColor(muted ? Application::TEXT_MUTED() : Application::TEXT_PRIMARY()).WAuto().HAuto();
	label.Parent(parent);
	return label.Spawn();
}

[[nodiscard]] ecs::Entity SpawnRow(Application &app, ecs::Entity parent, float gap = 8.f) {
	ui::WidgetBuilder row = app.Factory().Row();
	row.GrowW().HAuto().Gap(gap).Align(ui::CrossAlign::Center).Parent(parent);
	return row.Spawn();
}

void SpawnButton(Application &app, ecs::Entity parent, const char *text, std::function<void()> action) {
	ui::WidgetBuilder button = app.Factory().Button(String(text));
	button.WAuto().H(ui::Dimension::Px(28.f)).FontSize(13.f).Parent(parent).OnClick(std::move(action));
	(void)button.Spawn();
}

[[nodiscard]] String InputText(Application &app, ecs::Entity input) {
	if (auto field = app.World().GetComponent<ui::UiInput>(input); field.IsSome())
		return field.Unwrap()->text;
	return String();
}

/// Texte explicatif sur toute la largeur, replié à la ligne (un libellé
/// ordinaire garde une seule ligne et déborde de la fenêtre).
void SpawnNote(Application &app, ecs::Entity page, const String &text) {
	ui::WidgetBuilder note = app.Factory().Label(text);
	note.FontSize(12.f).TextColor(Application::TEXT_MUTED()).GrowW().HAuto().TextWrap().Parent(page);
	(void)note.Spawn();
}

void SetInputText(Application &app, ecs::Entity input, const String &text) {
	if (auto field = app.World().GetComponent<ui::UiInput>(input); field.IsSome()) {
		field.Unwrap()->text = text;
		field.Unwrap()->cursor = field.Unwrap()->selectionAnchor = text.GetSize();
	}
}

// ── ROMs ─────────────────────────────────────────────────────────────────────

/// Informations calculées pour une ROM de la liste (une seule fois : une ROM
/// d'archive doit être décompressée pour lire son en-tête).
struct RomPreview {
	bool loaded = false;
	String error;
	Option<RomMetadata> metadata = NONE;
	Option<ArchiveOrigin> archive = NONE;
	String logicalPath;
};

[[nodiscard]] String BaseNameOf(const String &path) {
	size_t slash = path.Rfind('/');
	return slash == String::NPOS ? path : path.Substr(slash + 1);
}

[[nodiscard]] String FormatBytes(uint64_t bytes) {
	if (bytes >= 1024 * 1024)
		return String::Format("%.2f Mio", double(bytes) / (1024.0 * 1024.0));
	if (bytes >= 1024)
		return String::Format("%.1f Kio", double(bytes) / 1024.0);
	return String::Format("%llu octets", static_cast<unsigned long long>(bytes));
}

ecs::Entity BuildRomBrowser(Application &app) {
	Application::ModalFrame frame = app.BeginModal("ROMs", 940.f, 580.f);
	ui::UiFactory &f = app.Factory();

	std::vector<String> problems;
	auto roms = std::make_shared<std::vector<RomCandidate>>();
	for (const char *directory : ROM_DIRECTORIES)
		for (RomCandidate &candidate : FindRoms(String(directory), &problems))
			roms->push_back(std::move(candidate));
	auto previews = std::make_shared<std::vector<RomPreview>>(roms->size());
	auto selected = std::make_shared<int>(-1);

	ui::WidgetBuilder columns = f.Row();
	columns.GrowW().GrowH().Gap(12.f).Parent(frame.body);
	ecs::Entity columnsEntity = columns.Spawn();

	// ── Colonne gauche : liste ──────────────────────────────────────────────
	ui::WidgetBuilder left = f.Column();
	left.W(ui::Dimension::Px(400.f)).GrowH().Gap(8.f).Parent(columnsEntity);
	ecs::Entity leftEntity = left.Spawn();

	size_t archived = 0;
	std::vector<String> items;
	for (const RomCandidate &candidate : *roms) {
		archived += candidate.entry.IsEmpty() ? 0 : 1;
		items.push_back(candidate.entry.IsEmpty()
							? BaseNameOf(candidate.path)
							: String::Format("%s  ‹ %s", BaseNameOf(candidate.entry).CStr(),
											 BaseNameOf(candidate.path).CStr()));
	}
	(void)SpawnLabel(app, leftEntity,
					 String::Format("%d ROM(s), dont %d dans une archive (.zip .tar .tar.gz .gz)", int(roms->size()),
									int(archived)),
					 12.f, true);
	if (!problems.empty())
		(void)SpawnLabel(app, leftEntity, String::Format("%d archive(s) illisible(s) : %s", int(problems.size()),
														 problems.front().Truncate(60).CStr()),
						 12.f, true);
	if (items.empty())
		items.push_back(String("(aucune ROM dans assets/roms ni ici : utilisez Parcourir…)"));

	// ── Colonne droite : informations ───────────────────────────────────────
	ui::WidgetBuilder preview = f.Column();
	preview.GrowW().GrowH().Bg(Application::ROW_BG()).Radius(4.f).Pad(12.f).Gap(8.f).Scrollable().Clip();
	preview.Parent(columnsEntity);
	ecs::Entity previewEntity = preview.Spawn();

	ecs::Entity header = SpawnRow(app, previewEntity, 12.f);
	ui::WidgetBuilder icon = f.Image(String(ICON_TEXTURE_KEY), 64.f, 64.f);
	icon.Parent(header);
	(void)icon.Spawn();
	ecs::Entity title = SpawnLabel(app, header, "Sélectionnez une ROM", 16.f);
	ecs::Entity subtitle = SpawnLabel(app, previewEntity, "", 12.f, true);

	// Deux libellés multi-lignes côte à côte (clés / valeurs) : les lignes
	// restent alignées et le rappel de sélection ne fait que changer deux
	// textes — aucune entité créée ou détruite pendant le rappel.
	ui::WidgetBuilder table = f.Row();
	table.GrowW().HAuto().Gap(12.f).Parent(previewEntity);
	ecs::Entity tableEntity = table.Spawn();
	ui::WidgetBuilder keys = f.Label("");
	keys.FontSize(12.f).TextColor(Application::TEXT_MUTED()).W(ui::Dimension::Px(110.f)).HAuto().Parent(tableEntity);
	ecs::Entity keysEntity = keys.Spawn();
	ecs::Entity valuesEntity = SpawnLabel(app, tableEntity, "", 12.f);

	auto &textures = app.Gui().RenderSystem().textures;
	textures.erase(ICON_TEXTURE_KEY);

	auto show = [&app, roms, previews, keysEntity, valuesEntity, title, subtitle](int index) {
		auto &textures = app.Gui().RenderSystem().textures;
		textures.erase(ICON_TEXTURE_KEY);
		const RomCandidate &candidate = (*roms)[size_t(index)];
		RomPreview &info = (*previews)[size_t(index)];
		if (!info.loaded) {
			info.loaded = true;
			auto rom = LoadRom(candidate.path, candidate.entry);
			if (rom.IsError()) {
				info.error = rom.Error();
			} else {
				info.metadata = ReadRomMetadataFromBytes(rom.Value().bytes);
				info.archive = rom.Value().archive;
				info.logicalPath = rom.Value().LogicalPath();
				if (info.metadata.IsNone())
					info.error = String("en-tête de ROM non reconnu");
			}
		}

		std::vector<std::pair<String, String>> lines;
		if (info.metadata.IsSome()) {
			lines = DescribeRom(info.metadata.Value());
			if (!lines.empty() && lines.front().first == "Titre")
				lines.erase(lines.begin()); // déjà en titre
		}
		if (info.archive.IsSome()) {
			const ArchiveOrigin &origin = info.archive.Value();
			lines.emplace_back(String(" "), String());
			lines.emplace_back(String("ARCHIVE"), String());
			lines.emplace_back(String("Format"), String(data::archive::FormatName(origin.format)));
			lines.emplace_back(String("Entrée"), origin.entryName);
			lines.emplace_back(String("Taille archive"), FormatBytes(origin.archiveBytes));
			if (origin.storedBytes == 0)
				lines.emplace_back(String("Stockée"), String::Format("bloc compressé partagé (%s)", origin.method.CStr()));
			else if (info.metadata.IsSome() && info.metadata.Value().sizeBytes > 0)
				lines.emplace_back(String("Stockée"),
								   String::Format("%s (%.0f %% de la ROM, %s)", FormatBytes(origin.storedBytes).CStr(),
												  double(origin.storedBytes) * 100.0 /
													  double(info.metadata.Value().sizeBytes),
												  origin.method.CStr()));
			lines.emplace_back(String("ROMs incluses"), String::Format("%d", int(origin.romEntries)));
		}
		if (!info.logicalPath.IsEmpty()) {
			String state = Settings::stateFilePath(info.logicalPath);
			lines.emplace_back(String("État rapide"),
							   sdl3::filesystem::PathInfo(state).IsSome() ? BaseNameOf(state) : String("aucun"));
		}

		String heading = info.metadata.IsSome() && !info.metadata.Value().title.IsEmpty()
							 ? info.metadata.Value().title.Replace('\n', ' ')
							 : BaseNameOf(candidate.entry.IsEmpty() ? candidate.path : candidate.entry);
		app.SetLabel(title, heading);
		app.SetLabel(subtitle, info.error.IsEmpty() ? candidate.DisplayName()
													: String::Format("%s\n%s", candidate.DisplayName().CStr(),
																	 info.error.Truncate(90).CStr()));
		String keyText;
		String valueText;
		for (size_t i = 0; i < lines.size(); ++i) {
			if (i > 0) {
				keyText.Concat("\n");
				valueText.Concat("\n");
			}
			keyText.Concat(lines[i].first);
			valueText.Concat(lines[i].second.Truncate(70));
		}
		app.SetLabel(keysEntity, keyText);
		app.SetLabel(valuesEntity, valueText);

		if (info.metadata.IsSome() && info.metadata.Value().icon.IsSome()) {
			const RomIcon &romIcon = info.metadata.Value().icon.Value();
			auto surface = sdl3::Surface::CreateFromPixels(RomIcon::SIZE, RomIcon::SIZE, sdl3::PixelFormat::RGBA32,
														   romIcon.rgba.data(), RomIcon::SIZE * int(sizeof(uint32_t)));
			if (surface.IsOk()) {
				auto texture = sdl3::Texture::CreateFromSurface(app.Renderer(), surface.Value());
				if (texture.IsOk()) {
					(void)texture.Value().SetScaleMode(SDL_SCALEMODE_NEAREST);
					textures[String(ICON_TEXTURE_KEY)] = std::move(texture).Unwrap();
				}
			}
		}
	};

	ui::WidgetBuilder list = f.Listbox(items, roms->empty() ? -1 : 0);
	list.GrowW().GrowH().Parent(leftEntity);
	list.OnChange([roms, selected, show](float index) {
		int i = int(index);
		if (i < 0 || i >= int(roms->size()))
			return;
		*selected = i;
		show(i);
	});
	(void)list.Spawn();

	ecs::Entity buttons = SpawnRow(app, leftEntity);
	SpawnButton(app, buttons, "Jouer", [&app, roms, selected] {
		if (*selected < 0 || *selected >= int(roms->size()))
			return;
		RomCandidate candidate = (*roms)[size_t(*selected)];
		app.CloseModal();
		app.Post([&app, candidate] { (void)app.SwitchToGame(candidate.path, candidate.entry); });
	});
	SpawnButton(app, buttons, "Parcourir…", [&app] { OpenRomFileDialog(app); });
	SpawnButton(app, buttons, "Actualiser", [&app] { app.OpenModal("roms"); });

	// Première ROM présélectionnée : la boîte montre d'emblée ce qu'elle sait
	// lire (et `--open=roms` capture un panneau rempli).
	if (!roms->empty()) {
		*selected = 0;
		show(0);
	}
	return frame.root;
}

// ── États sauvegardés ───────────────────────────────────────────────────────

/// Un état sauvegardé trouvé sur le disque, et la ROM à laquelle il se
/// rapporte (chemin logique : à côté de son archive pour une ROM extraite).
struct StateSlot {
	String file;
	String rom;
};

/// États du dossier des états (cf. Settings::stateFilePath), puis ceux
/// rangés à côté des ROMs (réglage vide, ou états d'avant le dossier dédié).
[[nodiscard]] std::vector<StateSlot> FindStateSlots() {
	std::vector<StateSlot> slots;
	const String directory = Settings::getStateDirectory();
	if (!directory.IsEmpty()) {
		for (const String &name : sdl3::filesystem::Glob(directory, "*.state0", false)) {
			const String romName = BaseNameOf(name.Substr(0, name.GetSize() - String(STATE_SUFFIX).GetSize()));
			// Le fichier ne porte que le NOM de la ROM : on la cherche dans
			// les dossiers de ROMs.
			String rom = String::Format("%s/%s", ROM_DIRECTORIES[0], romName.CStr());
			for (const char *romDirectory : ROM_DIRECTORIES) {
				String candidate = String(romDirectory) == "." ? romName
															   : String::Format("%s/%s", romDirectory, romName.CStr());
				if (ResolveLogicalRom(candidate).IsSome()) {
					rom = candidate;
					break;
				}
			}
			const String file =
				directory.EndsWith("/") ? directory + name : String::Format("%s/%s", directory.CStr(), name.CStr());
			slots.push_back({file, rom});
		}
	}
	for (const String &file : FindFiles({"*.state0"})) {
		// Dossier des états confondu avec un dossier de ROMs : pas de doublon.
		auto normalized = [](String path) {
			while (path.StartsWith("./"))
				path = path.Substr(2);
			return path.Replace("//", "/");
		};
		const bool known = std::any_of(slots.begin(), slots.end(), [&](const StateSlot &slot) {
			return normalized(slot.file) == normalized(file);
		});
		if (!known)
			slots.push_back({file, file.Substr(0, file.GetSize() - String(STATE_SUFFIX).GetSize())});
	}
	return slots;
}

ecs::Entity BuildSaveManager(Application &app) {
	Application::ModalFrame frame = app.BeginModal("États sauvegardés", 600.f, 440.f);
	auto slots = std::make_shared<std::vector<StateSlot>>(FindStateSlots());
	auto selected = std::make_shared<int>(-1);

	const String directory = Settings::getStateDirectory();
	SpawnNote(app, frame.body,
			  String::Format("F5 en jeu pour sauvegarder, F9 pour recharger — dossier : %s",
							 directory.IsEmpty() ? "à côté des ROMs" : directory.CStr()));
	std::vector<String> items;
	for (const StateSlot &slot : *slots)
		items.push_back(BaseNameOf(slot.rom));
	if (items.empty())
		items.push_back(String("(aucun état sauvegardé)"));
	ui::WidgetBuilder list = app.Factory().Listbox(items, -1);
	list.GrowW().GrowH().Parent(frame.body).OnChange([selected](float index) { *selected = int(index); });
	(void)list.Spawn();

	ecs::Entity buttons = SpawnRow(app, frame.body);
	SpawnButton(app, buttons, "Reprendre", [&app, slots, selected] {
		if (*selected < 0 || *selected >= int(slots->size()))
			return;
		// L'état d'une ROM d'archive porte le nom de la ROM, à côté de
		// l'archive : on retrouve l'archive qui la contient.
		const StateSlot &slot = (*slots)[size_t(*selected)];
		Option<RomCandidate> rom = ResolveLogicalRom(slot.rom);
		if (rom.IsNone()) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ROM introuvable pour l'état %s", slot.file.CStr());
			return;
		}
		RomCandidate candidate = rom.Unwrap();
		app.CloseModal();
		app.Post([&app, candidate] {
			if (app.SwitchToGame(candidate.path, candidate.entry) && app.Session())
				app.Session()->RequestQuickLoad();
		});
	});
	SpawnButton(app, buttons, "Supprimer", [&app, slots, selected] {
		if (*selected < 0 || *selected >= int(slots->size()))
			return;
		(void)sdl3::filesystem::Remove((*slots)[size_t(*selected)].file);
		app.OpenModal("saves"); // reconstruit la liste
	});
	SpawnButton(app, buttons, "Changer de dossier…", [&app] {
		g_configTab = PATHS_TAB;
		app.OpenModal("config");
	});
	return frame.root;
}

// ── Configuration ───────────────────────────────────────────────────────────

String GamepadButtonName(sdl3::GamepadButton button) {
	using GB = sdl3::GamepadButton;
	switch (button) {
	case GB::SOUTH:
		return "Sud (A)";
	case GB::EAST:
		return "Est (B)";
	case GB::WEST:
		return "Ouest (X)";
	case GB::NORTH:
		return "Nord (Y)";
	case GB::BACK:
		return "Select";
	case GB::Start:
		return "Start";
	case GB::LEFT_SHOULDER:
		return "L";
	case GB::RIGHT_SHOULDER:
		return "R";
	case GB::D_PAD_UP:
		return "Croix haut";
	case GB::D_PAD_DOWN:
		return "Croix bas";
	case GB::D_PAD_LEFT:
		return "Croix gauche";
	case GB::D_PAD_RIGHT:
		return "Croix droite";
	default:
		return String::Format("Bouton %d", int(button));
	}
}

String BindingLabel(Application &app, int button) {
	if (app.Gamepads().IsGamepadBound(button))
		return GamepadButtonName(app.Gamepads().BoundGamepadButton(button));
	const char *name = sdl3::keyboard::KeyName(app.Gamepads().BoundKey(button));
	return (name && *name) ? String(name) : String("?");
}

struct ButtonSpot {
	int button;
	const char *label;
	float x, y, w, h;
};

// Schéma de manette 460x250 : croix, boutons de face en losange, gâchettes,
// Select/Start — repris de la maquette d'origine.
constexpr std::array<ButtonSpot, BTN_COUNT> PAD_LAYOUT = {{
	{BTN_L, "L", 20.f, 14.f, 50.f, 26.f},
	{BTN_R, "R", 390.f, 14.f, 50.f, 26.f},
	{BTN_UP, "^", 90.f, 90.f, 34.f, 30.f},
	{BTN_DOWN, "v", 90.f, 156.f, 34.f, 30.f},
	{BTN_LEFT, "<", 58.f, 123.f, 30.f, 34.f},
	{BTN_RIGHT, ">", 126.f, 123.f, 30.f, 34.f},
	{BTN_X, "X", 340.f, 76.f, 34.f, 30.f},
	{BTN_Y, "Y", 306.f, 110.f, 34.f, 30.f},
	{BTN_A, "A", 374.f, 110.f, 34.f, 30.f},
	{BTN_B, "B", 340.f, 144.f, 34.f, 30.f},
	{BTN_SELECT, "Select", 170.f, 205.f, 60.f, 26.f},
	{BTN_START, "Start", 240.f, 205.f, 60.f, 26.f},
}};

void BuildControlsPage(Application &app, ecs::Entity page) {
	ui::UiFactory &f = app.Factory();
	auto capturing = std::make_shared<int>(-1);

	ecs::Entity columns = SpawnRow(app, page, 16.f);
	ui::WidgetBuilder diagram = f.Column();
	diagram.Size(460.f, 250.f).Parent(columns);
	ecs::Entity diagramEntity = diagram.Spawn();
	ui::WidgetBuilder body = f.Canvas([](sdl3::Renderer &renderer, sdl3::FRect area) {
		(void)renderer.SetDrawColor(sdl3::FColor(sdl3::Color{200, 200, 205, 255}));
		(void)renderer.FillRoundedRect(sdl3::FRect{area.x, area.y + 20.f, area.w, area.h - 40.f}, sdl3::Corners(24.f));
		(void)renderer.SetDrawColor(sdl3::FColor(sdl3::Color{70, 74, 80, 255}));
		(void)renderer.FillRoundedRect(sdl3::FRect{area.x + 150.f, area.y + 60.f, 160.f, 110.f}, sdl3::Corners(6.f));
	});
	body.GrowW().GrowH().Parent(diagramEntity);
	(void)body.Spawn();
	for (const ButtonSpot &spot : PAD_LAYOUT) {
		ui::WidgetBuilder button = f.Button(String(spot.label));
		button.Absolute().Anchor(ui::Anchor::TopLeft).Offset(spot.x, spot.y).Size(spot.w, spot.h).FontSize(12.f);
		button.Parent(diagramEntity).OnClick([capturing, index = spot.button] { *capturing = index; });
		(void)button.Spawn();
	}

	ui::WidgetBuilder legend = f.Column();
	legend.GrowW().Gap(3.f).Parent(columns);
	ecs::Entity legendEntity = legend.Spawn();
	ecs::Entity prompt =
		SpawnLabel(app, legendEntity, "Cliquez un bouton du schéma, puis appuyez sur une touche ou un bouton de manette.",
				   12.f, true);
	auto bindingLabels = std::make_shared<std::array<ecs::Entity, BTN_COUNT>>();
	for (int button = 0; button < BTN_COUNT; ++button)
		(*bindingLabels)[size_t(button)] = SpawnLabel(
			app, legendEntity, String::Format("%s → %s", ButtonName(button), BindingLabel(app, button).CStr()), 12.f);

	app.AddModalTick([&app, capturing, bindingLabels, prompt] {
		for (int button = 0; button < BTN_COUNT; ++button)
			app.SetLabel((*bindingLabels)[size_t(button)],
						 String::Format("%s → %s", ButtonName(button), BindingLabel(app, button).CStr()));
		app.SetLabel(prompt, *capturing >= 0
								 ? String::Format("Appuyez sur la touche à affecter à %s…", ButtonName(*capturing))
								 : String("Cliquez un bouton du schéma, puis appuyez sur une touche ou un bouton de manette."));
	});
	// Capture AVANT l'interface : sinon la touche servirait aussi de raccourci.
	app.AddModalEventHandler([&app, capturing](const sdl3::Event &event) {
		if (*capturing < 0)
			return false;
		if (event.IsKeyDown()) {
			if (event.Keycode() != SDLK_ESCAPE)
				app.Gamepads().RebindKeyboard(*capturing, event.Keycode());
			*capturing = -1;
			return true;
		}
		if (event.IsGamepadButtonDown()) {
			app.Gamepads().RebindGamepad(*capturing, sdl3::GamepadButton(event.GamepadButton().button));
			*capturing = -1;
			return true;
		}
		return false;
	});
}

void BuildVideoPage(Application &app, ecs::Entity page) {
	ui::UiFactory &f = app.Factory();
	(void)SpawnLabel(app, page, "Disposition des écrans NDS", 14.f);
	(void)SpawnLabel(app, page, "Les sessions GBA et GBC n'ont qu'un écran. Raccourci : R.", 12.f, true);
	bool vertical = Settings::getScreenLayout() != 0;
	ecs::Entity row = SpawnRow(app, page, 24.f);
	ui::WidgetBuilder horizontal = f.Radio("layout", "Côte à côte (écran tactile à droite)", !vertical);
	horizontal.Parent(row).OnToggle([](bool checked) {
		if (checked)
			Settings::setScreenLayout(0);
	});
	(void)horizontal.Spawn();
	ui::WidgetBuilder stacked = f.Radio("layout", "Empilés (écran tactile en bas)", vertical);
	stacked.Parent(row).OnToggle([](bool checked) {
		if (checked)
			Settings::setScreenLayout(1);
	});
	(void)stacked.Spawn();

	ecs::Entity hudRow = SpawnRow(app, page);
	ui::WidgetBuilder hud = f.Toggle(app.HudVisible());
	hud.Parent(hudRow).OnToggle([&app](bool checked) { app.SetHudVisible(checked); });
	(void)hud.Spawn();
	(void)SpawnLabel(app, hudRow, "Bandeau de performances au-dessus du jeu");
}

void BuildSystemPage(Application &app, ecs::Entity page) {
	ui::UiFactory &f = app.Factory();
	(void)SpawnLabel(app, page, "Démarrage (pris en compte au prochain lancement d'une ROM)", 14.f);

	ecs::Entity bootRow = SpawnRow(app, page);
	ui::WidgetBuilder direct = f.Toggle(Settings::getDirectBoot() != 0);
	direct.Parent(bootRow).OnToggle([](bool checked) { Settings::setDirectBoot(checked ? 1 : 0); });
	(void)direct.Spawn();
	(void)SpawnLabel(app, bootRow, "Boot direct (saute l'écran du BIOS / firmware)");

	ecs::Entity hleRow = SpawnRow(app, page);
	ui::WidgetBuilder hle = f.Toggle(Settings::getArm7Hle() != 0);
	hle.Parent(hleRow).OnToggle([](bool checked) { Settings::setArm7Hle(checked ? 1 : 0); });
	(void)hle.Spawn();
	(void)SpawnLabel(app, hleRow, "ARM7 en HLE (NDS sans BIOS ARM7 ni firmware)");

	ecs::Entity verboseRow = SpawnRow(app, page);
	ui::WidgetBuilder verbose = f.Toggle(Settings::getVerboseLog() != 0);
	verbose.Parent(verboseRow).OnToggle([](bool checked) { Settings::setVerboseLog(checked ? 1 : 0); });
	(void)verbose.Spawn();
	(void)SpawnLabel(app, verboseRow, "Journal détaillé du cœur (très coûteux, pour le débogage)");

	(void)SpawnLabel(app, page, "Fichiers système", 14.f);
	for (const String &path : {Settings::getNdsBios9Path(), Settings::getNdsBios7Path(), Settings::getFirmwarePath(),
							   Settings::getGbaBiosPath()})
		(void)SpawnLabel(app, page,
						 String::Format("[%s] %s", sdl3::filesystem::PathInfo(path).IsSome() ? "présent" : "absent",
										path.CStr()),
						 12.f, true);

	(void)SpawnLabel(app, page, "Journal fichier (Entrée pour appliquer, vide = désactivé)", 14.f);
	ui::WidgetBuilder logPath = f.Input("(désactivé)");
	logPath.GrowW().H(ui::Dimension::Px(28.f)).Parent(page).OnSubmit([&app](const String &text) {
		String path = text;
		app.Post([&app, path] { app.SetLogFilePath(path); });
	});
	ecs::Entity logPathEntity = logPath.Spawn();
	if (auto field = app.World().GetComponent<ui::UiInput>(logPathEntity); field.IsSome())
		field.Unwrap()->text = Settings::getLogFilePath();
}

/// Champ de saisie d'un chemin, prérempli. Rend l'entité du champ.
[[nodiscard]] ecs::Entity SpawnPathField(Application &app, ecs::Entity page, const String &value,
										 const char *placeholder) {
	ui::WidgetBuilder input = app.Factory().Input(String(placeholder));
	input.GrowW().H(ui::Dimension::Px(28.f)).Parent(page);
	ecs::Entity entity = input.Spawn();
	SetInputText(app, entity, value);
	return entity;
}

void BuildPathsPage(Application &app, ecs::Entity page) {
	// ── Dossier des états ────────────────────────────────────────────────
	(void)SpawnLabel(app, page, "Dossier des états sauvegardés (F5 / F9)", 14.f);
	SpawnNote(app, page,
			  String::Format("Vide = à côté de chaque ROM. Défaut : %s. Pris en compte au prochain lancement "
							 "d'une ROM ; enregistré dans le fichier de configuration.",
							 Settings::DEFAULT_STATE_DIRECTORY));
	ecs::Entity stateInput = SpawnPathField(app, page, Settings::getStateDirectory(), "(à côté des ROMs)");
	ecs::Entity stateButtons = SpawnRow(app, page);
	ecs::Entity stateStatus = SpawnLabel(app, page, String(), 12.f, true);
	auto applyStateDirectory = [&app, stateStatus](const String &raw) {
		const String directory = raw.Trim();
		if (!directory.IsEmpty() && !sdl3::filesystem::CreateDirectory(directory)) {
			app.SetLabel(stateStatus, String::Format("Dossier impossible à créer : %s", directory.CStr()));
			return;
		}
		Settings::setStateDirectory(directory);
		app.SetLabel(stateStatus, directory.IsEmpty() ? String("États enregistrés à côté de chaque ROM.")
													  : String::Format("États enregistrés dans %s", directory.CStr()));
	};
	SpawnButton(app, stateButtons, "Appliquer",
				[&app, stateInput, applyStateDirectory] { applyStateDirectory(InputText(app, stateInput)); });
	SpawnButton(app, stateButtons, "Parcourir…", [&app, stateInput, applyStateDirectory] {
		sdl3::dialog::ShowOpenFolder(
			[&app, stateInput, applyStateDirectory](const sdl3::DialogResult &result, int) {
				if (!result.Ok())
					return;
				String path = result.First();
				app.Post([&app, stateInput, applyStateDirectory, path] {
					SetInputText(app, stateInput, path);
					applyStateDirectory(path);
				});
			},
			app.Window(), Settings::getStateDirectory());
	});
	SpawnButton(app, stateButtons, "Par défaut", [&app, stateInput, applyStateDirectory] {
		SetInputText(app, stateInput, String(Settings::DEFAULT_STATE_DIRECTORY));
		applyStateDirectory(String(Settings::DEFAULT_STATE_DIRECTORY));
	});
	SpawnButton(app, stateButtons, "Voir les états", [&app] { app.OpenModal("saves"); });

	// ── Fichier de configuration ─────────────────────────────────────────
	(void)SpawnLabel(app, page, "Fichier de configuration", 14.f);
	SpawnNote(app, page,
			  String::Format("Retenu pour les prochains lancements (--config=CHEMIN reste prioritaire). "
							 "Fichier existant : ses réglages sont chargés ; sinon, les réglages actuels y "
							 "sont écrits. Défaut : %s.",
							 Settings::DEFAULT_CONFIG_PATH));
	ecs::Entity configInput = SpawnPathField(app, page, Settings::getFilename(), Settings::DEFAULT_CONFIG_PATH);
	ecs::Entity configButtons = SpawnRow(app, page);
	ecs::Entity configStatus = SpawnLabel(app, page, g_pathsNotice, 12.f, true);
	g_pathsNotice = String();
	auto useConfigFile = [&app, configStatus](const String &raw) {
		const String path = raw.Trim().IsEmpty() ? String(Settings::DEFAULT_CONFIG_PATH) : raw.Trim();
		String message;
		if (sdl3::filesystem::PathInfo(path).IsSome()) {
			if (!Settings::load(path)) {
				app.SetLabel(configStatus, String::Format("Lecture impossible : %s", path.CStr()));
				return;
			}
			message = String::Format("Réglages chargés depuis %s", path.CStr());
		} else {
			Settings::setFilename(path);
			if (!Settings::save()) {
				app.SetLabel(configStatus, String::Format("Écriture impossible : %s", path.CStr()));
				return;
			}
			message = String::Format("Réglages actuels enregistrés dans %s", path.CStr());
		}
		if (!RememberConfigPath(path))
			message.Concat(" (emplacement non retenu : pas de dossier de préférences)");
		// Les autres onglets affichent les valeurs du fichier précédent :
		// la fenêtre est reconstruite, rouverte sur cet onglet.
		g_pathsNotice = message;
		g_configTab = PATHS_TAB;
		app.Post([&app] { app.OpenModal("config"); });
	};
	SpawnButton(app, configButtons, "Utiliser ce fichier",
				[&app, configInput, useConfigFile] { useConfigFile(InputText(app, configInput)); });
	SpawnButton(app, configButtons, "Parcourir…", [&app, configInput, useConfigFile] {
		sdl3::dialog::ShowSaveFile(
			[&app, configInput, useConfigFile](const sdl3::DialogResult &result, int) {
				if (!result.Ok())
					return;
				String path = result.First();
				app.Post([&app, configInput, useConfigFile, path] {
					SetInputText(app, configInput, path);
					useConfigFile(path);
				});
			},
			app.Window(), {sdl3::DialogFilter{"Réglages", "ini"}, sdl3::DialogFilter{"Tous les fichiers", "*"}},
			Settings::getFilename());
	});
	SpawnButton(app, configButtons, "Par défaut", [&app, configInput, useConfigFile] {
		SetInputText(app, configInput, String(Settings::DEFAULT_CONFIG_PATH));
		useConfigFile(String(Settings::DEFAULT_CONFIG_PATH));
	});
	SpawnButton(app, configButtons, "Enregistrer maintenant", [&app, configStatus] {
		app.SetLabel(configStatus, Settings::save() ? String::Format("Enregistré : %s", Settings::getFilename().CStr())
												   : String::Format("Écriture impossible : %s",
																	Settings::getFilename().CStr()));
	});
}

ecs::Entity BuildConfig(Application &app) {
	Application::ModalFrame frame = app.BeginModal("Configuration", 900.f, 540.f);
	const int initialTab = g_configTab;
	g_configTab = 0;
	ui::WidgetBuilder tabs = app.Factory().Tabview(
		{String("Contrôles"), String("Vidéo"), String("Système"), String("Chemins")}, initialTab);
	tabs.GrowW().GrowH().Parent(frame.body);
	ecs::Entity tabsEntity = tabs.Spawn();
	auto page = [&app, tabsEntity]() {
		ui::WidgetBuilder builder = app.Factory().Column();
		builder.GrowW().GrowH().Pad(8.f).Gap(10.f).Scrollable().Clip().Parent(tabsEntity);
		return builder.Spawn();
	};
	BuildControlsPage(app, page());
	BuildVideoPage(app, page());
	BuildSystemPage(app, page());
	BuildPathsPage(app, page());
	ShowOnlyTab(app.World(), tabsEntity, size_t(initialTab));
	return frame.root;
}

// ── Codes de triche ─────────────────────────────────────────────────────────

/// Mots hexadécimaux de 8 chiffres séparés par des espaces -> code AR (paires
/// de mots). Un mot final non apparié est ignoré.
std::vector<uint32_t> ParseCheatCode(const String &text) {
	std::vector<uint32_t> code;
	for (const String &word : text.Split(' ')) {
		if (word.IsEmpty())
			continue;
		code.push_back(uint32_t(std::strtoul(word.CStr(), nullptr, 16)));
	}
	if (code.size() % 2 != 0)
		code.pop_back();
	return code;
}

ecs::Entity BuildCheats(Application &app) {
	Application::ModalFrame frame = app.BeginModal("Codes de triche (Action Replay)", 520.f, 520.f);
	ui::UiFactory &f = app.Factory();
	ActionReplay *cheats = app.Session() ? app.Session()->Cheats() : nullptr;
	if (!cheats) {
		(void)SpawnLabel(app, frame.body,
						 app.Session() ? "Les codes Action Replay ne concernent que les jeux NDS."
									   : "Lancez d'abord une ROM NDS, puis rouvrez cette boîte.",
						 13.f, true);
		return frame.root;
	}

	ui::WidgetBuilder list = f.Column();
	list.GrowW().H(ui::Dimension::Px(250.f)).Gap(4.f).Scrollable().Clip().Parent(frame.body);
	ecs::Entity listEntity = list.Spawn();
	if (cheats->cheats.empty())
		(void)SpawnLabel(app, listEntity, "Aucun code pour cette ROM.", 12.f, true);
	for (size_t i = 0; i < cheats->cheats.size(); ++i) {
		ui::WidgetBuilder row = f.Row();
		row.GrowW().HAuto().Bg(Application::ROW_BG()).Radius(4.f).Pad(6.f).Gap(8.f).Align(ui::CrossAlign::Center);
		row.Parent(listEntity);
		ecs::Entity rowEntity = row.Spawn();
		ui::WidgetBuilder toggle = f.Toggle(cheats->cheats[i].enabled);
		toggle.Parent(rowEntity).OnToggle([cheats, i](bool checked) {
			if (i < cheats->cheats.size()) {
				cheats->cheats[i].enabled = checked;
				(void)cheats->saveCheats();
			}
		});
		(void)toggle.Spawn();
		ui::WidgetBuilder name = f.Label(cheats->cheats[i].name);
		name.FontSize(13.f).TextColor(Application::TEXT_PRIMARY()).GrowW().Parent(rowEntity);
		(void)name.Spawn();
		SpawnButton(app, rowEntity, "Supprimer", [&app, cheats, i] {
			if (i < cheats->cheats.size()) {
				cheats->cheats.erase(cheats->cheats.begin() + long(i));
				(void)cheats->saveCheats();
			}
			app.OpenModal("cheats");
		});
	}

	(void)SpawnLabel(app, frame.body, "Ajouter un code", 14.f);
	ui::WidgetBuilder nameInput = f.Input("Nom du code");
	nameInput.GrowW().H(ui::Dimension::Px(28.f)).Parent(frame.body);
	ecs::Entity nameEntity = nameInput.Spawn();
	ui::WidgetBuilder codeInput = f.Input("Mots hexadécimaux séparés par des espaces, ex. 94000130 FFFB0000 …");
	codeInput.GrowW().H(ui::Dimension::Px(28.f)).Parent(frame.body);
	ecs::Entity codeEntity = codeInput.Spawn();

	ecs::Entity buttons = SpawnRow(app, frame.body);
	SpawnButton(app, buttons, "Ajouter", [&app, cheats, nameEntity, codeEntity] {
		String name = InputText(app, nameEntity);
		std::vector<uint32_t> code = ParseCheatCode(InputText(app, codeEntity));
		if (name.IsEmpty() || code.empty())
			return;
		ARCheat cheat;
		cheat.name = name;
		cheat.code = code;
		cheat.enabled = true;
		cheats->cheats.push_back(cheat);
		(void)cheats->saveCheats();
		app.OpenModal("cheats");
	});
	SpawnButton(app, buttons, "Importer un fichier .cht…", [&app, cheats] {
		sdl3::dialog::ShowOpenFile(
			[&app, cheats](const sdl3::DialogResult &result, int) {
				if (!result.Ok())
					return;
				String path = result.First();
				app.Post([&app, cheats, path] {
					// Remplace la liste, puis l'écrit dans le .cht de la ROM
					// pour que l'import survive au prochain lancement.
					if (app.Session() && app.Session()->Cheats() == cheats && cheats->loadCheats(path))
						(void)cheats->saveCheats();
					app.OpenModal("cheats");
				});
			},
			app.Window(), {sdl3::DialogFilter{"Codes de triche", "cht;txt"}, sdl3::DialogFilter{"Tous les fichiers", "*"}});
	});
	return frame.root;
}

// ── Réseau ──────────────────────────────────────────────────────────────────

ecs::Entity BuildNetwork(Application &app) {
	Application::ModalFrame frame = app.BeginModal("Réseau local", 460.f, 300.f);
	ui::UiFactory &f = app.Factory();
	ui::WidgetBuilder address = f.Input("Adresse IP (ex. 127.0.0.1)");
	address.GrowW().H(ui::Dimension::Px(28.f)).Parent(frame.body);
	ecs::Entity addressEntity = address.Spawn();
	ui::WidgetBuilder port = f.Input("Port (ex. 7777)");
	port.GrowW().H(ui::Dimension::Px(28.f)).Parent(frame.body);
	ecs::Entity portEntity = port.Spawn();
	ecs::Entity status = SpawnLabel(app, frame.body, NetStatusText(app.Net().Status()));

	auto readPort = [&app, portEntity]() {
		Option<int64_t> value = InputText(app, portEntity).Trim().TryParseInt();
		return (value.IsSome() && value.Unwrap() > 0 && value.Unwrap() < 65536) ? uint16_t(value.Unwrap())
																				: uint16_t(7777);
	};
	ecs::Entity buttons = SpawnRow(app, frame.body);
	SpawnButton(app, buttons, "Héberger", [&app, readPort] { (void)app.Net().Host(readPort()); });
	SpawnButton(app, buttons, "Rejoindre", [&app, addressEntity, readPort] {
		String ip = InputText(app, addressEntity).Trim();
		(void)app.Net().Join(ip.IsEmpty() ? String("127.0.0.1") : ip, readPort());
	});
	SpawnButton(app, buttons, "Déconnecter", [&app] { app.Net().Disconnect(); });
	app.AddModalTick([&app, status] { app.SetLabel(status, NetStatusText(app.Net().Status())); });
	return frame.root;
}

// ── À propos ────────────────────────────────────────────────────────────────

ecs::Entity BuildAbout(Application &app) {
	Application::ModalFrame frame = app.BeginModal("À propos d'EmulOS", 520.f, 300.f);
	(void)SpawnLabel(app, frame.body, "EmulOS — emulator_demo", 18.f);
	// Les libellés ne passent pas à la ligne d'eux-mêmes : lignes explicites.
	(void)SpawnLabel(app, frame.body,
					 "Émulateur Nintendo DS et Game Boy Advance (cœur dérivé de NooDS)\n"
					 "et Game Boy / Game Boy Color, bâti sur le wrapper SDL3/C++23 :\n"
					 "sdl3:: (fenêtre, rendu, audio, réseau, manettes), ui:: (interface\n"
					 "retenue sur ECS) et data:: (rapport d'exécution JSON).",
					 13.f, true);
	(void)SpawnLabel(app, frame.body,
					 "F5 sauvegarde · F9 chargement · P pause · R disposition\nF12 capture · Ctrl+O ROMs · Échap quitter",
					 12.f, true);
	(void)SpawnLabel(app, frame.body, "Ligne de commande : emulator_demo --help", 12.f, true);
	return frame.root;
}

} // namespace

ecs::Entity BuildModal(Application &app, const String &name) {
	if (name == "roms")
		return BuildRomBrowser(app);
	if (name == "saves")
		return BuildSaveManager(app);
	if (name == "config")
		return BuildConfig(app);
	if (name == "cheats")
		return BuildCheats(app);
	if (name == "network")
		return BuildNetwork(app);
	if (name == "about")
		return BuildAbout(app);
	return ecs::Entity{};
}

void OpenRomFileDialog(Application &app) {
	sdl3::dialog::ShowOpenFile(
		[&app](const sdl3::DialogResult &result, int) {
			if (!result.Ok())
				return;
			String path = result.First();
			app.Post([&app, path] {
				app.CloseModal();
				(void)app.SwitchToGame(path);
			});
		},
		app.Window(),
		{sdl3::DialogFilter{"ROMs et archives", "nds;gba;gbc;gb;zip;tar;gz;tgz"},
		 sdl3::DialogFilter{"ROMs NDS/GBA/GBC", "nds;gba;gbc;gb"}, sdl3::DialogFilter{"Archives", "zip;tar;gz;tgz"},
		 sdl3::DialogFilter{"Tous les fichiers", "*"}});
}

} // namespace emulator_demo::app
