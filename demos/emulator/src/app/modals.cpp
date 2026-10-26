#include <emulator/app/modals.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <vector>

#include <sdl3/dialog.hpp>
#include <sdl3/filesystem.hpp>

#include <emulator/core/hle/action_replay.hpp>
#include <emulator/core/settings.hpp>
#include <emulator/app/application.hpp>
#include <emulator/app/config_location.hpp>
#include <emulator/app/rom_metadata.hpp>
#include <emulator/app/rom_source.hpp>

namespace emulator::app {

namespace {

constexpr const char *ICON_TEXTURE_KEY = "emulator.rom_icon";
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

/// Onglet ouvert par la prochaine construction de la fenêtre de configuration,
/// et message à y afficher : changer de fichier de configuration la
/// reconstruit (ses autres onglets montraient les valeurs de l'ancien).
int g_configTab = 0;
String g_pathsNotice;
constexpr int PATHS_TAB = 3;

ui::Widget SpawnLabel(ui::Widget parent, const String &text, float size = 13.f, bool muted = false) {
	ui::WidgetBuilder label = ui::Label(text);
	label.FontSize(size).Class(muted ? "muted" : "primary");
	return parent.Append(label);
}

ui::Widget SpawnRow(ui::Widget parent, float gap = 8.f) { return parent.Append(ui::Row().Class("dialog-row").Gap(gap)); }

void SpawnButton(ui::Widget parent, const char *text, std::function<void()> action) {
	parent.Append(ui::Button(text).OnClick(std::move(action)));
}

/// Texte explicatif sur toute la largeur, replié à la ligne.
void SpawnNote(ui::Widget page, const String &text) { page.Append(ui::Label(text).Class("note")); }

/// Champ de saisie d'un chemin, prérempli.
ui::Widget SpawnPathField(ui::Widget page, const String &value, const char *placeholder) {
	return page.Append(ui::Input(placeholder, value).Class("field"));
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

/// Titre d'un groupe de la liste des ROMs.
[[nodiscard]] const char *RomSystemTitle(Option<RomSystem> system) {
	if (system.IsNone())
		return "Console inconnue";
	switch (system.Value()) {
	case RomSystem::GBC:
		return "Game Boy / Game Boy Color";
	case RomSystem::GBA:
		return "Game Boy Advance";
	case RomSystem::NDS:
		return "Nintendo DS";
	}
	return "Console inconnue";
}

[[nodiscard]] String FormatBytes(uint64_t bytes) {
	if (bytes >= 1024 * 1024)
		return String::Format("%.2f Mio", double(bytes) / (1024.0 * 1024.0));
	if (bytes >= 1024)
		return String::Format("%.1f Kio", double(bytes) / 1024.0);
	return String::Format("%llu octets", static_cast<unsigned long long>(bytes));
}

ui::Widget BuildRomBrowser(Application &app) {
	Application::ModalFrame frame = app.BeginModal("ROMs", 940.f, 580.f);

	// assets/roms est parcouru avec tous ses sous-dossiers ; le dossier
	// courant seul (le parcourir récursivement balaierait tout le projet).
	std::vector<String> problems;
	auto roms = std::make_shared<std::vector<RomCandidate>>();
	for (const char *directory : ROM_DIRECTORIES)
		for (RomCandidate &candidate : FindRoms(String(directory), &problems, String(directory) != "."))
			roms->push_back(std::move(candidate));
	SortRomsBySystem(*roms);
	auto previews = std::make_shared<std::vector<RomPreview>>(roms->size());
	auto selected = std::make_shared<int>(-1);

	ui::Widget columns = frame.body.Append(ui::Row().Grow().FillWidth().Gap(12.f).AlignItems(ui::Align::Stretch));

	// ── Colonne gauche : liste ──────────────────────────────────────────────
	ui::Widget leftEntity = columns.Append(ui::Column().Width(400.f).Gap(8.f));

	// Liste groupée par console : une ligne d'en-tête par groupe, puis ses
	// ROMs (déjà triées par console puis par nom). `rowToRom` associe chaque
	// ligne à sa ROM, -1 pour un en-tête (non jouable).
	size_t archived = 0;
	int perSystem[4] = {0, 0, 0, 0};
	std::vector<String> items;
	auto rowToRom = std::make_shared<std::vector<int>>();
	int currentGroup = -1;
	for (size_t i = 0; i < roms->size(); ++i) {
		const RomCandidate &candidate = (*roms)[i];
		const int group = RomSystemOrder(candidate.System());
		++perSystem[group];
		if (group != currentGroup) {
			currentGroup = group;
			size_t count = 0;
			for (size_t j = i; j < roms->size() && RomSystemOrder((*roms)[j].System()) == group; ++j)
				++count;
			items.push_back(String::Format("▾ %s — %d ROM(s)", RomSystemTitle(candidate.System()), int(count)));
			rowToRom->push_back(-1);
		}
		archived += candidate.entry.IsEmpty() ? 0 : static_cast<size_t>(1);
		items.push_back(candidate.entry.IsEmpty() ? String::Format("    %s", candidate.ShortName().CStr())
												  : String::Format("    %s  ‹ %s", candidate.ShortName().CStr(),
																   BaseNameOf(candidate.path).CStr()));
		rowToRom->push_back(int(i));
	}
	(void)SpawnLabel(leftEntity,
					 String::Format("%d ROM(s) — GBC %d · GBA %d · NDS %d — dont %d dans une archive", int(roms->size()),
									perSystem[0], perSystem[1], perSystem[2], int(archived)),
					 12.f, true);
	if (!problems.empty())
		(void)SpawnLabel(leftEntity,
						 String::Format("%d archive(s) illisible(s) : %s", int(problems.size()),
										problems.front().Truncate(60).CStr()),
						 12.f, true);
	if (items.empty())
		items.push_back(String("(aucune ROM dans assets/roms et ses sous-dossiers : utilisez Parcourir…)"));

	// ── Colonne droite : informations ───────────────────────────────────────
	ui::Widget previewEntity =
		columns.Append(ui::Column().Class("card").Grow().Padding(12.f).Gap(8.f).Overflow(ui::Overflow::Auto));

	ui::Widget header = SpawnRow(previewEntity, 12.f);
	header.Append(ui::Image(ICON_TEXTURE_KEY).Size(64.f, 64.f));
	ui::Widget title = SpawnLabel(header, "Sélectionnez une ROM", 16.f);
	ui::Widget subtitle = SpawnLabel(previewEntity, "", 12.f, true);

	// Deux libellés multi-lignes côte à côte (clés / valeurs) : les lignes
	// restent alignées et le rappel de sélection ne fait que changer deux
	// textes — aucune entité créée ou détruite pendant le rappel.
	ui::Widget tableEntity = previewEntity.Append(ui::Row().FillWidth().Gap(12.f).AlignItems(ui::Align::Start));
	ui::Widget keysEntity = tableEntity.Append(ui::Label("").Class("muted").Width(110.f));
	ui::Widget valuesEntity = SpawnLabel(tableEntity, "", 12.f);

	app.Gui().GetTextures().Remove(ICON_TEXTURE_KEY);

	auto show = [&app, roms, previews, keysEntity, valuesEntity, title, subtitle](int index) {
		ui::Textures &pool = app.Gui().GetTextures();
		pool.Remove(ICON_TEXTURE_KEY);
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
			lines.emplace_back(String("Format"), String(archive::FormatName(origin.format)));
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
					pool.Add(ICON_TEXTURE_KEY, std::move(texture).Unwrap());
				}
			}
		}
	};

	// Ligne 0 = en-tête du premier groupe : la première ROM est en ligne 1.
	ui::WidgetBuilder list = ui::ListBox(items, roms->empty() ? -1 : 1);
	list.FillWidth().Grow().OnSelect([roms, rowToRom, selected, show](int row) {
		if (row < 0 || row >= int(rowToRom->size()))
			return;
		const int rom = (*rowToRom)[size_t(row)];
		if (rom < 0)
			return; // en-tête de groupe : la sélection précédente reste valable
		*selected = rom;
		show(rom);
	});
	leftEntity.Append(list);

	ui::Widget buttons = SpawnRow(leftEntity);
	SpawnButton(buttons, "Jouer", [&app, roms, selected] {
		if (*selected < 0 || *selected >= int(roms->size()))
			return;
		RomCandidate candidate = (*roms)[size_t(*selected)];
		app.CloseModal();
		app.Post([&app, candidate] { (void)app.SwitchToGame(candidate.path, candidate.entry); });
	});
	SpawnButton(buttons, "Parcourir…", [&app] { OpenRomFileDialog(app); });
	SpawnButton(buttons, "Actualiser", [&app] { app.OpenModal("roms"); });

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

ui::Widget BuildSaveManager(Application &app) {
	Application::ModalFrame frame = app.BeginModal("États sauvegardés", 600.f, 440.f);
	auto slots = std::make_shared<std::vector<StateSlot>>(FindStateSlots());
	auto selected = std::make_shared<int>(-1);

	const String directory = Settings::getStateDirectory();
	SpawnNote(frame.body,
			  String::Format("F5 en jeu pour sauvegarder, F9 pour recharger — dossier : %s",
							 directory.IsEmpty() ? "à côté des ROMs" : directory.CStr()));
	std::vector<String> items;
	for (const StateSlot &slot : *slots)
		items.push_back(BaseNameOf(slot.rom));
	if (items.empty())
		items.push_back(String("(aucun état sauvegardé)"));
	frame.body.Append(ui::ListBox(items, -1).FillWidth().Grow().OnSelect([selected](int index) { *selected = index; }));

	ui::Widget buttons = SpawnRow(frame.body);
	SpawnButton(buttons, "Reprendre", [&app, slots, selected] {
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
	SpawnButton(buttons, "Supprimer", [&app, slots, selected] {
		if (*selected < 0 || *selected >= int(slots->size()))
			return;
		(void)sdl3::filesystem::Remove((*slots)[size_t(*selected)].file);
		app.OpenModal("saves"); // reconstruit la liste
	});
	SpawnButton(buttons, "Changer de dossier…", [&app] {
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
	case GB::START:
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

void BuildControlsPage(Application &app, ui::Widget page) {
	auto capturing = std::make_shared<int>(-1);

	ui::Widget columns = SpawnRow(page, 16.f);
	ui::Widget diagramEntity = columns.Append(ui::Stack().Size(460.f, 250.f));
	diagramEntity.Append(ui::Canvas([](ui::Painter &painter, const sdl3::FRect &area) {
		painter.FillRoundedRect(sdl3::FRect{area.x, area.y + 20.f, area.w, area.h - 40.f}, math::Corners(24.f),
								ui::Rgba(200, 200, 205));
		painter.FillRoundedRect(sdl3::FRect{area.x + 150.f, area.y + 60.f, 160.f, 110.f}, math::Corners(6.f),
								ui::Rgba(70, 74, 80));
	}).Fill());
	for (const ButtonSpot &spot : PAD_LAYOUT)
		diagramEntity.Append(ui::Button(spot.label)
								 .Absolute()
								 .Offset(spot.x, spot.y)
								 .Size(spot.w, spot.h)
								 .FontSize(12.f)
								 .Padding(0.f)
								 .OnClick([capturing, index = spot.button] { *capturing = index; }));

	ui::Widget legendEntity = columns.Append(ui::Column().Grow().Gap(3.f));
	ui::Widget prompt = legendEntity.Append(
		ui::Label("Cliquez un bouton du schéma, puis appuyez sur une touche ou un bouton de manette.").Class("note"));
	auto bindingLabels = std::make_shared<std::array<ui::Widget, BTN_COUNT>>();
	for (int button = 0; button < BTN_COUNT; ++button)
		(*bindingLabels)[size_t(button)] =
			SpawnLabel(legendEntity, String::Format("%s → %s", ButtonName(button), BindingLabel(app, button).CStr()), 12.f);

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

void BuildVideoPage(Application &app, ui::Widget page) {
	(void)SpawnLabel(page, "Disposition des écrans NDS", 14.f);
	(void)SpawnLabel(page, "Les sessions GBA et GBC n'ont qu'un écran. Raccourci : R.", 12.f, true);
	const bool vertical = Settings::getScreenLayout() != 0;
	ui::Widget row = SpawnRow(page, 24.f);
	row.Append(ui::Radio("Côte à côte (écran tactile à droite)", "layout", !vertical).OnToggle([](bool checked) {
		if (checked)
			Settings::setScreenLayout(0);
	}));
	row.Append(ui::Radio("Empilés (écran tactile en bas)", "layout", vertical).OnToggle([](bool checked) {
		if (checked)
			Settings::setScreenLayout(1);
	}));

	ui::Widget hudRow = SpawnRow(page);
	hudRow.Append(ui::Switch(app.HudVisible()).OnToggle([&app](bool checked) { app.SetHudVisible(checked); }));
	(void)SpawnLabel(hudRow, "Bandeau de performances au-dessus du jeu");
}

void BuildSystemPage(Application &app, ui::Widget page) {
	(void)SpawnLabel(page, "Démarrage (pris en compte au prochain lancement d'une ROM)", 14.f);
	auto option = [&page](bool on, const char *text, std::function<void(bool)> apply) {
		ui::Widget row = SpawnRow(page);
		row.Append(ui::Switch(on).OnToggle(std::move(apply)));
		(void)SpawnLabel(row, text);
	};
	option(Settings::getDirectBoot() != 0, "Boot direct (saute l'écran du BIOS / firmware)",
		   [](bool checked) { Settings::setDirectBoot(checked ? 1 : 0); });
	option(Settings::getArm7Hle() != 0,
		   "ARM7 en HLE — expérimental : ni son ni jeux du commerce (inutile sans BIOS : le BIOS HLE suffit)",
		   [](bool checked) { Settings::setArm7Hle(checked ? 1 : 0); });
	option(Settings::getVerboseLog() != 0, "Journal détaillé du cœur (très coûteux, pour le débogage)",
		   [](bool checked) { Settings::setVerboseLog(checked ? 1 : 0); });

	(void)SpawnLabel(page, "Fichiers système", 14.f);
	for (const String &path : {Settings::getNdsBios9Path(), Settings::getNdsBios7Path(), Settings::getFirmwarePath(),
							   Settings::getGbaBiosPath()})
		(void)SpawnLabel(page,
						 String::Format("[%s] %s", sdl3::filesystem::PathInfo(path).IsSome() ? "présent" : "absent",
										path.CStr()),
						 12.f, true);

	(void)SpawnLabel(page, "Journal fichier (Entrée pour appliquer, vide = désactivé)", 14.f);
	page.Append(ui::Input("(désactivé)", Settings::getLogFilePath()).Class("field").OnSubmit([&app](const String &text) {
		String path = text;
		app.Post([&app, path] { app.SetLogFilePath(path); });
	}));
}

void BuildPathsPage(Application &app, ui::Widget page) {
	// ── Dossier des états ────────────────────────────────────────────────
	(void)SpawnLabel(page, "Dossier des états sauvegardés (F5 / F9)", 14.f);
	SpawnNote(page,
			  String::Format("Vide = à côté de chaque ROM. Défaut : %s. Pris en compte au prochain lancement "
							 "d'une ROM ; enregistré dans le fichier de configuration.",
							 Settings::DEFAULT_STATE_DIRECTORY));
	ui::Widget stateInput = SpawnPathField(page, Settings::getStateDirectory(), "(à côté des ROMs)");
	ui::Widget stateButtons = SpawnRow(page);
	ui::Widget stateStatus = SpawnLabel(page, String(), 12.f, true);
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
	SpawnButton(stateButtons, "Appliquer", [stateInput, applyStateDirectory] { applyStateDirectory(stateInput.Text()); });
	SpawnButton(stateButtons, "Parcourir…", [&app, stateInput, applyStateDirectory] {
		sdl3::dialog::ShowOpenFolder(
			[&app, stateInput, applyStateDirectory](const sdl3::DialogResult &result, int) {
				if (!result.Ok())
					return;
				String path = result.First();
				app.Post([stateInput, applyStateDirectory, path]() mutable {
					stateInput.SetText(path);
					applyStateDirectory(path);
				});
			},
			app.Window(), Settings::getStateDirectory());
	});
	SpawnButton(stateButtons, "Par défaut", [stateInput, applyStateDirectory]() mutable {
		stateInput.SetText(String(Settings::DEFAULT_STATE_DIRECTORY));
		applyStateDirectory(String(Settings::DEFAULT_STATE_DIRECTORY));
	});
	SpawnButton(stateButtons, "Voir les états", [&app] { app.OpenModal("saves"); });

	// ── Fichier de configuration ─────────────────────────────────────────
	(void)SpawnLabel(page, "Fichier de configuration", 14.f);
	SpawnNote(page,
			  String::Format("Retenu pour les prochains lancements (--config=CHEMIN reste prioritaire). "
							 "Fichier existant : ses réglages sont chargés ; sinon, les réglages actuels y "
							 "sont écrits. Défaut : %s.",
							 Settings::DEFAULT_CONFIG_PATH));
	ui::Widget configInput = SpawnPathField(page, Settings::getFilename(), Settings::DEFAULT_CONFIG_PATH);
	ui::Widget configButtons = SpawnRow(page);
	ui::Widget configStatus = SpawnLabel(page, g_pathsNotice, 12.f, true);
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
	SpawnButton(configButtons, "Utiliser ce fichier", [configInput, useConfigFile] { useConfigFile(configInput.Text()); });
	SpawnButton(configButtons, "Parcourir…", [&app, configInput, useConfigFile] {
		sdl3::dialog::ShowSaveFile(
			[&app, configInput, useConfigFile](const sdl3::DialogResult &result, int) {
				if (!result.Ok())
					return;
				String path = result.First();
				app.Post([configInput, useConfigFile, path]() mutable {
					configInput.SetText(path);
					useConfigFile(path);
				});
			},
			app.Window(), {sdl3::DialogFilter{"Réglages", "ini"}, sdl3::DialogFilter{"Tous les fichiers", "*"}},
			Settings::getFilename());
	});
	SpawnButton(configButtons, "Par défaut", [configInput, useConfigFile]() mutable {
		configInput.SetText(String(Settings::DEFAULT_CONFIG_PATH));
		useConfigFile(String(Settings::DEFAULT_CONFIG_PATH));
	});
	SpawnButton(configButtons, "Enregistrer maintenant", [&app, configStatus] {
		app.SetLabel(configStatus, Settings::save() ? String::Format("Enregistré : %s", Settings::getFilename().CStr())
												   : String::Format("Écriture impossible : %s",
																	Settings::getFilename().CStr()));
	});
}

ui::Widget BuildConfig(Application &app) {
	Application::ModalFrame frame = app.BeginModal("Configuration", 900.f, 540.f);
	const int initialTab = g_configTab;
	g_configTab = 0;
	// Quatre pages vides, remplies une fois l'onglet construit.
	std::vector<ui::AnyWidget> pages;
	for (int i = 0; i < 4; ++i)
		pages.push_back(ui::Column().Class("config-page").Fill());
	ui::Widget tabs =
		frame.body.Append(ui::TabView({"Contrôles", "Vidéo", "Système", "Chemins"}, initialTab).Grow().FillWidth().Children(
			std::move(pages)));
	const std::vector<ui::Widget> page = tabs.FindAll(".config-page");
	if (page.size() == 4) {
		BuildControlsPage(app, page[0]);
		BuildVideoPage(app, page[1]);
		BuildSystemPage(app, page[2]);
		BuildPathsPage(app, page[3]);
	}
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

ui::Widget BuildCheats(Application &app) {
	Application::ModalFrame frame = app.BeginModal("Codes de triche (Action Replay)", 520.f, 520.f);
	ActionReplay *cheats = app.Session() ? app.Session()->Cheats() : nullptr;
	if (!cheats) {
		(void)SpawnLabel(frame.body,
						 app.Session() ? "Les codes Action Replay ne concernent que les jeux NDS."
									   : "Lancez d'abord une ROM NDS, puis rouvrez cette boîte.",
						 13.f, true);
		return frame.root;
	}

	ui::Widget listEntity = frame.body.Append(ui::ScrollView().FillWidth().Height(250.f).Gap(4.f));
	if (cheats->cheats.empty())
		(void)SpawnLabel(listEntity, "Aucun code pour cette ROM.", 12.f, true);
	for (size_t i = 0; i < cheats->cheats.size(); ++i) {
		ui::Widget rowEntity = listEntity.Append(ui::Row().Class("card").FillWidth().Gap(8.f));
		rowEntity.Append(ui::Switch(cheats->cheats[i].enabled).OnToggle([cheats, i](bool checked) {
			if (i < cheats->cheats.size()) {
				cheats->cheats[i].enabled = checked;
				(void)cheats->saveCheats();
			}
		}));
		rowEntity.Append(ui::Label(cheats->cheats[i].name).Class("primary").Grow());
		SpawnButton(rowEntity, "Supprimer", [&app, cheats, i] {
			if (i < cheats->cheats.size()) {
				cheats->cheats.erase(cheats->cheats.begin() + long(i));
				(void)cheats->saveCheats();
			}
			app.OpenModal("cheats");
		});
	}

	(void)SpawnLabel(frame.body, "Ajouter un code", 14.f);
	ui::Widget nameEntity = frame.body.Append(ui::Input("Nom du code").Class("field"));
	ui::Widget codeEntity =
		frame.body.Append(ui::Input("Mots hexadécimaux séparés par des espaces, ex. 94000130 FFFB0000 …").Class("field"));

	ui::Widget buttons = SpawnRow(frame.body);
	SpawnButton(buttons, "Ajouter", [&app, cheats, nameEntity, codeEntity] {
		String name = nameEntity.Text();
		std::vector<uint32_t> code = ParseCheatCode(codeEntity.Text());
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
	SpawnButton(buttons, "Importer un fichier .cht…", [&app, cheats] {
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

ui::Widget BuildNetwork(Application &app) {
	Application::ModalFrame frame = app.BeginModal("Réseau local", 460.f, 300.f);
	ui::Widget addressEntity = frame.body.Append(ui::Input("Adresse IP (ex. 127.0.0.1)").Class("field"));
	ui::Widget portEntity = frame.body.Append(ui::Input("Port (ex. 7777)").Class("field"));
	ui::Widget status = SpawnLabel(frame.body, NetStatusText(app.Net().Status()));

	auto readPort = [portEntity]() {
		Option<int64_t> value = portEntity.Text().Trim().TryParseInt();
		return (value.IsSome() && value.Unwrap() > 0 && value.Unwrap() < 65536) ? uint16_t(value.Unwrap())
																				: uint16_t(7777);
	};
	ui::Widget buttons = SpawnRow(frame.body);
	SpawnButton(buttons, "Héberger", [&app, readPort] { (void)app.Net().Host(readPort()); });
	SpawnButton(buttons, "Rejoindre", [&app, addressEntity, readPort] {
		String ip = addressEntity.Text().Trim();
		(void)app.Net().Join(ip.IsEmpty() ? String("127.0.0.1") : ip, readPort());
	});
	SpawnButton(buttons, "Déconnecter", [&app] { app.Net().Disconnect(); });
	app.AddModalTick([&app, status] { app.SetLabel(status, NetStatusText(app.Net().Status())); });
	return frame.root;
}

// ── À propos ────────────────────────────────────────────────────────────────

ui::Widget BuildAbout(Application &app) {
	Application::ModalFrame frame = app.BeginModal("À propos d'EmulOS", 520.f, 300.f);
	(void)SpawnLabel(frame.body, "EmulOS — emulator", 18.f);
	// Les libellés ne passent pas à la ligne d'eux-mêmes : lignes explicites.
	(void)SpawnLabel(frame.body,
					 "Émulateur Nintendo DS et Game Boy Advance (cœur dérivé de NooDS)\n"
					 "et Game Boy / Game Boy Color, bâti sur le wrapper SDL3/C++23 :\n"
					 "sdl3:: (fenêtre, rendu, audio, réseau, manettes), ui:: (interface\n"
					 "retenue sur ECS) et dataio:: (rapport d'exécution JSON).",
					 13.f, true);
	(void)SpawnLabel(frame.body,
					 "F5 sauvegarde · F9 chargement · P pause · R disposition\nF12 capture · Ctrl+O ROMs · Échap quitter",
					 12.f, true);
	(void)SpawnLabel(frame.body, "Ligne de commande : emulator --help", 12.f, true);
	return frame.root;
}

} // namespace

ui::Widget BuildModal(Application &app, const String &name) {
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
	return ui::Widget();
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

} // namespace emulator::app
