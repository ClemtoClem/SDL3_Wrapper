/**
 * ui_showcase — vitrine interactive de TOUS les widgets du module ui::.
 *
 * Une barre d'onglets en haut, une page (scène retenue, ui::SceneManager)
 * par famille de widgets. Changer de page ne détruit rien : l'état des
 * widgets (curseurs, saisie, défilement…) survit.
 *
 * Pages :
 *   1. Base       — Label, Button (+désactivé), Switch, Checkbox, Radio,
 *                   Separator, Badge, icônes
 *   2. Contrôles  — Slider H/V (pas + molette), Knob, Progress animé, Spinner
 *   3. Saisie     — Input / Password / TextArea, ScrollBar H/V, ScrollView
 *   4. Listes     — Combo, ListBox, Expander, TabView
 *   5. Image      — Image (fill/contain/cover/none), Canvas animé
 *   6. Dynamique  — styles en direct (DragValue), visibilité, Splitter,
 *                   menus, sélection, arbre, Table, graphe temps réel,
 *                   couleurs, dates, modale, texte enrichi
 *
 * Le style vient du thème sombre et d'une petite feuille CSS propre à
 * l'exemple (onglet actif, titres de section, couleurs sémantiques).
 *
 *   cpp-maker run ui_showcase
 */
#include <cmath>
#include <format>
#include <iostream>
#include <vector>

#include <core/core.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

static constexpr int WIN_W = 1280;
static constexpr int WIN_H = 820;

// Feuille de l'exemple : sélecteurs de type, de classe, d'id et d'état.
static constexpr const char *SHOWCASE_CSS = R"css(
#header { background-color: #181a26; padding: 8px 12px; gap: 8px; height: 52px; }
#app-title { font-size: 18px; color: var(--accent); flex-grow: 1; }
.nav { width: 110px; height: 32px; }
.nav.active { background-color: #3773d2; color: #ffffff; border-color: #3773d2; }
.page { padding: 12px; gap: 12px; align-items: flex-start; }
.page > panel { padding: 14px; gap: 8px; }
.section { font-size: 18px; }
.muted { color: #969cb2; }
.accent { color: var(--accent); }
.ok { color: #50c878; }
.warn { color: #eb9b3c; }
.error { color: #e15a50; }
.field-label { width: 90px; }
.dark-well { background-color: #161824; border-radius: 6px; padding: 8px; gap: 4px; }
)css";

static constexpr const char *PAGES[] = {"base", "controles", "saisie", "listes", "image", "dynamique"};
static constexpr const char *PAGE_LABELS[] = {"Base", "Contrôles", "Saisie", "Listes", "Image", "Dynamique"};
static constexpr int PAGE_COUNT = 6;

// État applicatif partagé entre les callbacks et la boucle.
struct App {
	ui::Widget lblClick, lblRadio, lblToggles, badge;
	ui::Widget lblSlider, lblKnob, progress, lblProgress;
	ui::Widget lblEcho, lblScroll;
	ui::Widget lblCombo, lblList, lblTab;
	ui::Widget sampleText, plotLive, modal, swatch, colorPopup;
	std::vector<ui::Widget> dynItems;
	std::vector<ui::Widget> navButtons;

	float progressT = 0.f;
	bool animRunning = true;
	float animSpeed = 0.25f;
	float canvasAngle = 0.f;
	int badgeCount = 0;
	std::vector<float> plotBuffer = std::vector<float>(40, 0.f);
	float plotPhase = 0.f;
	float plotClock = 0.f;
};

static ui::WidgetBuilder Section(String text) { return ui::Label(std::move(text)).Class("section"); }
static ui::WidgetBuilder Muted(String text) { return ui::Label(std::move(text)).Class("muted"); }

// ═════════════════════════════════════════════════════════════════════════════
// Pages
// ═════════════════════════════════════════════════════════════════════════════

static ui::AnyWidget PageBase(App &app) {
	return ui::Row().Class("page").Children({
		ui::Panel().Width(420.f).Children({
			Section("Étiquettes"),
			ui::Label("Texte normal"),
			ui::Label("Accent").Class("accent"),
			ui::Label("Succès").Class("ok"),
			ui::Label("Avertissement").Class("warn"),
			ui::Label("Erreur").Class("error"),
			ui::Separator(),
			Section("Boutons"),
			ui::Button("Cliquez ici").Tooltip("Un bouton ordinaire").OnClick([&app] {
				app.lblClick.SetText("Bouton cliqué !");
			}),
			ui::Button("Compteur badge +1").Tooltip("Incrémente la pastille de l'en-tête").OnClick([&app] {
				app.badge.SetText(String::From(++app.badgeCount));
			}),
			// Clic gauche quadruple OU bouton EAST de la manette (OnBind générique).
			ui::Button("Quadruple clic / EAST")
				.OnBind({sdl3::MouseButton::Left, sdl3::ButtonEvent::MultiClicks, 4},
						{sdl3::GamepadButton::EAST, sdl3::ButtonEvent::Clic, 0},
						[&app](ui::WidgetEvent) { app.lblClick.SetText("Quadruple clic !"); }),
			ui::Button("Désactivé").Disabled().Tooltip("Jamais affichée : widget inerte"),
			Muted("Cliquez sur un bouton...").Ref(app.lblClick),
		}),
		ui::Panel().Width(420.f).Children({
			Section("Interrupteurs"),
			ui::Row().Gap(10.f).Children({
				ui::Switch(true).Tooltip("Fonction A").OnToggle([&app](bool on) {
					app.lblToggles.SetText(on ? "A activé" : "A désactivé");
				}),
				ui::Switch(false).Tooltip("Fonction B"),
				ui::Checkbox("Case", true).Tooltip("Case à cocher"),
				ui::Switch(false).Disabled().Tooltip("Interrupteur désactivé"),
			}),
			Muted("—").Ref(app.lblToggles),
			ui::Separator(),
			Section("Boutons radio (groupe unique)"),
			ui::Radio("Option A", "grp", true).OnToggle([&app](bool on) {
				if (on)
					app.lblRadio.SetText("Choix : A");
			}),
			ui::Radio("Option B", "grp").OnToggle([&app](bool on) {
				if (on)
					app.lblRadio.SetText("Choix : B");
			}),
			ui::Radio("Option C", "grp").OnToggle([&app](bool on) {
				if (on)
					app.lblRadio.SetText("Choix : C");
			}),
			ui::Label("Choix : A").Class("accent").Ref(app.lblRadio),
			ui::Separator(),
			Section("Icônes (Cupertino / Material)"),
			ui::Row().Gap(14.f).Children({
				ui::Icon(ui::CupertinoIcons::HEART, 22.f).Class("error").Tooltip("CupertinoIcons::HEART"),
				ui::Icon(ui::CupertinoIcons::SEARCH, 22.f).Tooltip("CupertinoIcons::SEARCH"),
				ui::Icon(ui::CupertinoIcons::SETTINGS, 22.f).Tooltip("CupertinoIcons::SETTINGS"),
				ui::Icon(ui::CupertinoIcons::BELL, 22.f).Class("warn").Tooltip("CupertinoIcons::BELL"),
				ui::Separator(ui::Orientation::Vertical).Height(22.f),
				ui::Icon(ui::MaterialIcons::HOME, 22.f).Class("accent").Tooltip("MaterialIcons::HOME"),
				ui::Icon(ui::MaterialIcons::FAVORITE, 22.f).Class("error").Tooltip("MaterialIcons::FAVORITE"),
				ui::IconButton(ui::MaterialIcons::SETTINGS, "Bouton à icône"),
				ui::IconButton(ui::MaterialIcons::SEARCH, "Rechercher"),
			}),
		}),
	});
}

static ui::AnyWidget PageControls(App &app) {
	auto knob = [](ui::WidgetBuilder k, const char *caption) {
		return ui::Column().AlignItems(ui::Align::Center).Gap(4.f).Children({k.Size(64.f, 64.f), Muted(caption)});
	};
	return ui::Row().Class("page").Children({
		ui::Panel().Width(460.f).Gap(10.f).Children({
			Section("Curseurs (glisser + molette)"),
			ui::Row().Children({
				ui::Label("Volume").Class("field-label"),
				ui::Slider(0.f, 1.f, 0.7f)
					.Grow()
					.Tooltip("Continu [0-1]")
					.OnChange([&app](float v) { app.lblSlider.SetText(std::format("Volume : {:.2f}", v)); }),
			}),
			ui::Row().Children({
				ui::Label("Pas de 5").Class("field-label"),
				ui::Slider(0.f, 100.f, 40.f, 5.f).Grow().Tooltip("Quantifié : pas de 5"),
			}),
			Muted("Volume : 0.70").Ref(app.lblSlider),
			ui::Separator(),
			Section("Curseur vertical + potentiomètres"),
			ui::Row().Gap(20.f).Children({
				ui::Slider(0.f, 1.f, 0.4f, 0.f, ui::Orientation::Vertical).Size(22.f, 130.f).Tooltip("Vertical"),
				knob(ui::Knob(0.f, 1.f, 0.5f).Tooltip("Continu [0-1]").OnChange([&app](float v) {
					app.lblKnob.SetText(std::format("Knob : {:.2f}", v));
				}),
					 "continu"),
				knob(ui::Knob(0.f, 100.f, 50.f, 5.f).Tooltip("[0-100], pas de 5"), "0-100, pas 5"),
				knob(ui::Knob(0.f, 1.f, 0.3f).Disabled().Tooltip("Désactivé"), "désactivé"),
			}),
			Muted("Knob : 0.50").Ref(app.lblKnob),
		}),
		ui::Panel().Width(420.f).Gap(10.f).Children({
			Section("Barres de progression"),
			ui::Progress(0.f, 1.f, 0.25f).FillWidth(),
			ui::Progress(0.f, 1.f, 0.60f).FillWidth(),
			ui::Separator(),
			Section("Progression animée"),
			ui::Row().Children({
				ui::Progress(0.f, 1.f, 0.f).Grow().Height(12.f).Ref(app.progress),
				ui::Label("0 %").Width(56.f).Ref(app.lblProgress),
				ui::Button("Pause").Tooltip("Suspend / reprend l'animation").OnClick([&app] {
					app.animRunning = !app.animRunning;
				}),
			}),
			ui::Row().Children({
				ui::Label("Vitesse").Class("field-label"),
				ui::Slider(0.05f, 2.f, 0.25f).Grow().OnChange([&app](float v) { app.animSpeed = v; }),
			}),
			ui::Separator(),
			ui::Row().Gap(12.f).Children({
				ui::Spinner().Size(36.f, 36.f).Tooltip("Indicateur de chargement"),
				Muted("Spinner (tourne en continu)"),
			}),
		}),
	});
}

static ui::AnyWidget PageInput(App &app) {
	std::vector<ui::AnyWidget> items;
	for (int i = 1; i <= 25; ++i) {
		ui::WidgetBuilder item = ui::Label(std::format("{:02d}. Élément défilable", i));
		if (i % 3 == 0)
			item.Class("accent");
		items.push_back(item);
	}
	return ui::Row().Class("page").Children({
		ui::Panel().Width(420.f).Children({
			Section("Champs de saisie"),
			ui::Input("Votre nom...").FillWidth().Tooltip("Le texte est répété dessous").OnTextChange(
				[&app](const String &t) { app.lblEcho.SetText(String("Écho : ") + t); }),
			ui::Label("Écho : ").Class("accent").Ref(app.lblEcho),
			ui::Input("email@exemple.com").FillWidth(),
			ui::Password("Mot de passe").FillWidth(),
			ui::Input("Non éditable...").FillWidth().Disabled(),
			ui::TextArea("Plusieurs lignes...").FillWidth().Height(80.f),
			ui::Separator(),
			Section("Barres de défilement autonomes"),
			ui::ScrollBar(300.f, 100.f).FillWidth().Height(12.f).OnScroll([&app](float off) {
				app.lblScroll.SetText(std::format("Offset : {:.0f}", off));
			}),
			ui::Row().Gap(12.f).Children({
				ui::ScrollBar(200.f, 60.f, ui::Orientation::Vertical).Size(12.f, 120.f),
				Muted("Offset : 0").Ref(app.lblScroll),
			}),
		}),
		ui::Panel().Width(420.f).Children({
			Section("Conteneur défilant (molette)"),
			ui::ScrollView().Class("dark-well").FillWidth().Height(320.f).Children(std::move(items)),
			Muted("Molette au-dessus de la liste"),
		}),
	});
}

static ui::AnyWidget PageLists(App &app) {
	std::vector<String> fruits{"Pomme", "Banane", "Cerise", "Datte",  "Figue",  "Grenade",
							   "Kiwi",  "Litchi", "Mangue", "Orange", "Papaye", "Raisin"};
	return ui::Row().Class("page").Children({
		ui::Panel().Width(420.f).Children({
			Section("Menu déroulant"),
			ui::Combo(std::vector<String>{"Français", "English", "Deutsch", "Español"}, 0)
				.FillWidth()
				.OnSelect([&app](int i) { app.lblCombo.SetText(std::format("Langue n° {}", i)); }),
			Muted("Langue n° 0").Ref(app.lblCombo),
			ui::Combo(std::vector<ui::ComboCategory>{{"Fruits", {"Pomme", "Poire"}}, {"Légumes", {"Carotte", "Poireau"}}},
					  0)
				.FillWidth()
				.Tooltip("Catégories en sous-menus"),
			ui::Separator(),
			Section("Liste (molette + clic)"),
			ui::ListBox(fruits, 2).FillWidth().Height(180.f).OnSelect([&app](int i) {
				app.lblList.SetText(std::format("Fruit n° {}", i));
			}),
			Muted("Fruit n° 2").Ref(app.lblList),
		}),
		ui::Panel().Width(460.f).Gap(10.f).Children({
			Section("Sections repliables"),
			ui::Expander("Réglages avancés", true)
				.FillWidth()
				.Children({ui::Label("Contenu de la section A"), ui::Switch(true), ui::Slider(0.f, 1.f, 0.3f)}),
			ui::Expander("Section repliée au départ", false)
				.FillWidth()
				.Children({ui::Label("Surprise ! Contenu caché."), ui::Checkbox("Coché ?")}),
			ui::Separator(),
			Section("Onglets"),
			ui::TabView({"Un", "Deux", "Trois"}, 0)
				.FillWidth()
				.Height(150.f)
				.OnSelect([&app](int i) { app.lblTab.SetText(std::format("Onglet actif : {}", i)); })
				.Children({
					ui::Column().Children({ui::Label("Contenu du premier onglet").Class("accent"),
										   ui::Label("Chaque onglet affiche son enfant.")}),
					ui::Column().Children({ui::Label("Deuxième onglet").Class("ok"), ui::Switch(false)}),
					ui::Column().Children({ui::Label("Troisième onglet").Class("warn"), ui::Slider(0.f, 1.f, 0.5f)}),
				}),
			Muted("Onglet actif : 0").Ref(app.lblTab),
		}),
	});
}

static ui::AnyWidget PageImage(App &app) {
	std::vector<ui::AnyWidget> fits;
	for (auto [name, fit] : {std::pair{"Fill", ui::ObjectFit::Fill}, std::pair{"Contain", ui::ObjectFit::Contain},
							 std::pair{"Cover", ui::ObjectFit::Cover}, std::pair{"None", ui::ObjectFit::None}})
		fits.push_back(ui::Column().AlignItems(ui::Align::Center).Gap(4.f).Children({
			ui::Image("demos/voxelia/mods/core/textures/blocks/mineral_stone_block.png")
				.Size(100.f, 100.f)
				.ObjectFit(fit)
				.Tooltip(std::format("object-fit: {}", name)),
			Muted(name),
		}));
	return ui::Row().Class("page").Children({
		ui::Panel().Width(480.f).Children({
			Section("Image — modes d'ajustement"),
			ui::Row().Gap(10.f).Children(std::move(fits)),
			Muted("Texture chargée à la demande depuis son chemin"),
			ui::Separator(),
			ui::Image("demos/voxelia/mods/core/textures/blocks/logo.png").Size(128.f, 128.f).Tooltip("Logo — fill"),
		}),
		ui::Panel().Width(420.f).Children({
			Section("Canvas — dessin libre"),
			ui::Canvas([&app](ui::Painter &p, const sdl3::FRect &r) {
				p.Checkerboard(r, 20.f);
				const sdl3::FPoint c{r.x + r.w * 0.5f, r.y + r.h * 0.5f};
				const float size = std::min(r.w, r.h) * 0.32f;
				const float a = app.canvasAngle * float(M_PI) / 180.f;
				sdl3::FPoint pts[4];
				for (int i = 0; i < 4; ++i) {
					const float ang = a + float(i) * float(M_PI) * 0.5f;
					pts[i] = {c.x + std::cos(ang) * size, c.y + std::sin(ang) * size};
				}
				p.Polyline(pts, 2.f, ui::Rgba(90, 160, 255), true);
				p.FillCircle(c, std::fabs(std::sin(a * 2.f)) * 10.f + 5.f, ui::Rgba(155, 75, 220));
				p.Arc(c, size * 1.2f, app.canvasAngle, 120.f, 4.f, ui::Rgba(80, 200, 120));
			})
				.FillWidth()
				.Height(240.f)
				.Tooltip("Rappel de dessin appelé à chaque image"),
			Muted("Painter : formes, arcs, dégradés, clip appliqué"),
		}),
	});
}

static ui::AnyWidget PageDynamic(App &app) {
	// ── Colonne 1 : propriétés en direct, visibilité, splitter ──
	std::vector<ui::AnyWidget> dynItems;
	app.dynItems.assign(20, ui::Widget());
	for (int i = 0; i < 20; ++i)
		dynItems.push_back(ui::Label(std::format("Élément {:02d}", i + 1)).Hidden(i >= 5).Ref(app.dynItems[size_t(i)]));

	auto col1 = ui::Panel().Width(340.f).Gap(10.f).Children({
		Section("Taille de police en direct"),
		ui::Row().Children({
			ui::Label("Taille :").Width(70.f),
			ui::DragValue(8.f, 32.f, 16.f, 1.f, 0.3f, 0).Tooltip("Glisser, ou double-clic pour taper").OnChange(
				[&app](float v) { app.sampleText.FontSize(v); }),
		}),
		ui::Label("Ce texte change de taille en direct").FontSize(16.f).Ref(app.sampleText),
		ui::Separator(),
		Section("Défilement automatique"),
		ui::Row().Children({
			ui::Label("Visibles :").Width(70.f),
			ui::DragValue(0.f, 20.f, 5.f, 1.f, 0.3f, 0).OnChange([&app](float v) {
				const int n = int(v + 0.5f);
				for (size_t i = 0; i < app.dynItems.size(); ++i)
					app.dynItems[i].SetVisible(int(i) < n);
			}),
		}),
		ui::ScrollView().Class("dark-well").FillWidth().Height(120.f).Children(std::move(dynItems)),
		Muted("La barre apparaît selon le nombre visible"),
		ui::Separator(),
		Section("Splitter"),
		ui::Splitter(ui::Panel().Padding(8.f).Children({ui::Label("A").Class("accent")}),
					 ui::Panel().Padding(8.f).Children({ui::Label("B").Class("ok")}), ui::Orientation::Horizontal,
					 100.f, 40.f, 40.f)
			.FillWidth()
			.Height(120.f),
	});

	// ── Colonne 2 : menus, valeurs, sélection, table, graphe ──
	auto col2 = ui::Column().Width(360.f).Gap(10.f).Children({
		Section("Barre de menus"),
		ui::MenuBar().Children({
			ui::Menu("Fichier").Children({
				ui::MenuItem("Nouveau", "Ctrl+N"),
				ui::MenuItem("Ouvrir", "Ctrl+O"),
				ui::SubMenu("Récents").Children({ui::MenuItem("projet.json"), ui::MenuItem("scene.json")}),
				ui::MenuSeparator(),
				ui::MenuItem("Quitter").OnClick([] { std::cout << "Quitter (démo, sans effet)\n"; }),
			}),
			ui::Menu("Aide").Children({ui::MenuItem("À propos")}),
		}),
		ui::Separator(),
		Section("Édition de valeurs"),
		ui::Row().Children({ui::Label("Quantité :").Width(100.f), ui::NumberInput(0.f, 10.f, 3.f, 1.f, 0)}),
		ui::Row().Children({
			ui::Label("Couleur :").Width(100.f),
			ui::ColorSwatch(ui::Rgba(90, 180, 255)).Size(28.f, 28.f).Ref(app.swatch).OnClick([&app] {
				app.colorPopup.OpenBelow(app.swatch);
			}),
			ui::Popup().Ref(app.colorPopup).Children({
				ui::ColorPicker(ui::Rgba(90, 180, 255), [&app](ui::Color c) { app.swatch.SetColorValue(c); }),
			}),
			ui::DatePicker(2026, 8, 10, [](int y, int m, int d) { std::cout << std::format("{}-{}-{}\n", y, m, d); }),
		}),
		ui::Separator(),
		Section("Sélection & arbre"),
		ui::Row().Gap(10.f).Children({
			ui::SelectionList()
				.Class("dark-well")
				.Grow()
				.Height(90.f)
				.Children({ui::Selectable("Pomme", 0), ui::Selectable("Banane", 1), ui::Selectable("Cerise", 2)}),
			ui::SelectionList(false)
				.Class("dark-well")
				.Grow()
				.Height(90.f)
				.Children({ui::TreeNode("Catégorie", 0).Children(
					{ui::TreeNode("Sous-item 1", 1, false), ui::TreeNode("Sous-item 2", 2, false)})}),
		}),
		ui::Separator(),
		Section("Table (tri, redimension, réordonnement)"),
		ui::Table({{"Nom", 120.f, 50.f, true}, {"Score", 80.f, 40.f, true}},
				  {{"Alice", "87"}, {"Bob", "64"}, {"Chloé", "95"}}, true, true)
			.FillWidth()
			.Height(120.f),
		Section("Graphe temps réel"),
		ui::PlotLines(app.plotBuffer, "Temps réel").FillWidth().Height(90.f).YRange(-1.2f, 1.2f).Ref(app.plotLive),
		ui::Button("Ouvrir la modale").OnClick([&app] { app.modal.Open(); }),
	});

	// ── Colonne 3 : texte enrichi ──
	static constexpr const char *LOREM =
		"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et "
		"dolore magna aliqua.\n\nUt enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex "
		"ea commodo consequat.\n\nDuis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu "
		"fugiat nulla pariatur.";
	auto col3 = ui::Panel().Width(360.f).Children({
		Section("Texte enrichi").Bold(),
		Muted("Gras / italique / souligné / barré / surligné"),
		ui::Text(LOREM).TextWrap(ui::TextAlign::Justify).Italic().FillWidth(),
		ui::Separator(),
		ui::Wrap().Gap(10.f).Children({
			ui::Label("gras").Bold(),
			ui::Label("souligné").Underline(),
			ui::Label("barré").Strikethrough(),
			ui::Label("surligné").Background(ui::Rgba(255, 220, 60, 140)).Padding(2.f, 0.f),
			ui::Label("défilant si trop long pour sa boîte").Width(120.f).Marquee(),
			ui::Label("tronqué avec des points de suspension").Width(140.f).Ellipsis(),
		}),
	});

	return ui::Stack().Children({
		ui::Row().Class("page").Children({col1, col2, col3}),
		ui::Modal(ui::Panel().Width(320.f).Gap(10.f).Padding(16.f).Children({
					  Section("Boîte de dialogue modale"),
					  Muted("Bloque le reste de l'interface (Échap ou le bouton ferme).").TextWrap().FillWidth(),
					  ui::Button("Fermer").AlignSelf(ui::Align::End).OnClick([&app] { app.modal.Close(); }),
				  }))
			.Ref(app.modal),
	});
}

// ═════════════════════════════════════════════════════════════════════════════

int main() {
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO | sdl3::init_flags::EVENTS | sdl3::init_flags::GAMEPAD);
	if (!sdl) {
		std::cerr << "SDL init: " << sdl.Error().CStr() << "\n";
		return 1;
	}
	auto ttf = sdl3::TtfContext::Create();
	if (!ttf) {
		std::cerr << "TTF init: " << ttf.Error().CStr() << "\n";
		return 1;
	}
	auto winRes = sdl3::Window::Create(u8"ui:: - vitrine des widgets", WIN_W, WIN_H, ui::WindowFrame::WINDOW_FLAGS);
	if (!winRes) {
		std::cerr << "Window: " << winRes.Error().CStr() << "\n";
		return 1;
	}
	auto &window = winRes.Value();
	auto renRes = sdl3::Renderer::Create(window);
	if (!renRes) {
		std::cerr << "Renderer: " << renRes.Error().CStr() << "\n";
		return 1;
	}
	auto &ren = renRes.Value();

	ecs::ArchetypeRegistry world;
	ui::Ui gui(world, window, ren);
	gui.AddStyleSheet(SHOWCASE_CSS);

	ui::WindowFrame frame;
	frame.Build(gui, window,
				{.title = "ui:: - vitrine des widgets", .appIcon = Some(ui::MaterialIcons::WIDGETS),
				 .status = "Six pages : naviguer avec les onglets de l'en-tête."});

	App app;

	// ── En-tête : titre, navigation, badge ──────────────────────────────────
	ui::SceneManager scenes(ui::Widget{});
	auto switchPage = [&](int index) {
		scenes.SwitchTo(PAGES[index]);
		for (int i = 0; i < int(app.navButtons.size()); ++i)
			if (i == index)
				app.navButtons[size_t(i)].AddClass("active");
			else
				app.navButtons[size_t(i)].RemoveClass("active");
	};
	ui::WidgetBuilder header = ui::Row().Id("header");
	header.Child(ui::Label("ui:: showcase").Id("app-title"));
	app.navButtons.assign(PAGE_COUNT, ui::Widget());
	for (int i = 0; i < PAGE_COUNT; ++i)
		header.Child(ui::Button(PAGE_LABELS[i])
						 .Class("nav")
						 .Tooltip(std::format("Aller à la page {}", PAGE_LABELS[i]))
						 .Ref(app.navButtons[size_t(i)])
						 .OnClick([&switchPage, i] { switchPage(i); }));
	header.Child(ui::Spinner().Size(24.f, 24.f).Tooltip("Toujours animé"));
	header.Child(ui::Badge("0").Tooltip("Compteur (page Base)").Ref(app.badge));
	frame.Content().Append(header);

	// ── Pages (construites à la première visite) ────────────────────────────
	ui::Widget pages = frame.Content().Append(ui::Stack().Grow().FillWidth());
	scenes = ui::SceneManager(pages);
	scenes.Add("base", [&] { return PageBase(app); });
	scenes.Add("controles", [&] { return PageControls(app); });
	scenes.Add("saisie", [&] { return PageInput(app); });
	scenes.Add("listes", [&] { return PageLists(app); });
	scenes.Add("image", [&] { return PageImage(app); });
	scenes.Add("dynamique", [&] { return PageDynamic(app); });
	switchPage(0);

	// ── Boucle ───────────────────────────────────────────────────────────────
	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();
	while (running) {
		const uint64_t now = sdl3::GetTicksMS();
		const float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit() || (e.IsKeyDown(SDLK_ESCAPE) && !gui.HasOpenModal())) {
				running = false;
				break;
			}
			gui.HandleEvent(e);
		}

		// Animations applicatives.
		app.canvasAngle = std::fmod(app.canvasAngle + dt * 90.f, 360.f);
		if (app.animRunning) {
			app.progressT += dt * app.animSpeed;
			if (app.progressT > 1.f)
				app.progressT -= 1.f;
		}
		if (app.progress) {
			app.progress.SetValue(app.progressT);
			app.lblProgress.SetText(std::format("{:.0f} %", app.progressT * 100.f));
		}
		// Graphe temps réel : tampon glissant, un échantillon toutes les 30 ms.
		app.plotClock += dt;
		if (app.plotLive && app.plotClock >= 0.03f) {
			app.plotPhase += app.plotClock * 4.f;
			app.plotClock = 0.f;
			app.plotBuffer.erase(app.plotBuffer.begin());
			app.plotBuffer.push_back(std::sin(app.plotPhase) * 0.8f + std::sin(app.plotPhase * 2.7f) * 0.2f);
			if (auto plot = app.plotLive.Get<ui::PlotModel>(); plot.IsSome() && !plot.Unwrap()->series.empty())
				plot.Unwrap()->series[0].SetY(app.plotBuffer);
		}

		gui.Update(dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		ren.SetDrawColor(gui.CurrentPalette().appBg);
		ren.Clear();
		gui.Render();
		ren.Present();
	}
	return 0;
}
