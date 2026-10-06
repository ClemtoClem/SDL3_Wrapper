/**
 * ui_showcase — vitrine interactive de TOUS les widgets du module ui::.
 *
 * Inspiré de SDL3pp examples/renderer/13_ui_v1.cpp : une barre d'onglets en
 * haut, une page (scène retenue) par famille de widgets. Changer de page ne
 * détruit rien — l'état des widgets (sliders, saisie, scroll...) survit.
 *
 * Pages :
 *   1. Base       — Label, Button (+désactivé), Toggle, Checkbox, Radio,
 *                   Separator, Badge
 *   2. Contrôles  — Slider H/V (pas + molette), Knob (plage + pas), Progress
 *                   animé, Spinner
 *   3. Saisie     — Input (+placeholder/désactivé), ScrollBar H/V, conteneur
 *                   scrollable à la molette
 *   4. Listes     — ComboBox, ListBox, Expander, TabView
 *   5. Image      — Image (Fill/Contain/Cover/NONE), Canvas animé
 *   6. Dynamique  — dynamisme de l'UI : fontSize modifié en direct
 *                   (DragValue), affichage/masquage automatique du pouce de
 *                   défilement selon le contenu, Splitter, et les widgets
 *                   des Phases 1/3/4/5/6/7/8 (popup/modale, ColorPicker,
 *                   Selectable/TreeNode, Menu, Table, PlotLines temps réel,
 *                   DatePicker) + un bloc de texte multi-paragraphe composé
 *                   de widgets « span » (un UiLabel par mot, enveloppé à une
 *                   largeur fixe, hauteur automatique) démontrant le
 *                   formatage de texte (bold/italique/souligné/barré/
 *                   surligné).
 *
 * Toutes les infobulles apparaissent après ~0.6 s de survol.
 *
 *   make examples && ./build/examples/ui_showcase
 */
#include <algorithm>
#include <format>
#include <iostream>
#include <vector>

#include "core/core.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

static constexpr int WIN_W = 1280;
static constexpr int WIN_H = 820; // 760 de contenu + barres de titre et d'état
/// Hauteur de la barre de titre (ui::WindowFrame) : l'en-tête et les pages,
/// placés en absolu, commencent dessous.
static constexpr float TITLE_H = 36.f;
static constexpr float FONT_PT = 14.f;

namespace pal {
constexpr sdl3::FColor BG = sdl3::FColor::UI_APP_BG();
constexpr sdl3::FColor ACCENT = sdl3::FColor::UI_ACCENT_BLUE_BRIGHT();
constexpr sdl3::FColor GREEN{80 / 255.f, 200 / 255.f, 120 / 255.f, 1.f};
constexpr sdl3::FColor ORANGE{235 / 255.f, 155 / 255.f, 60 / 255.f, 1.f};
constexpr sdl3::FColor RED{225 / 255.f, 90 / 255.f, 80 / 255.f, 1.f};
constexpr sdl3::FColor MUTED{150 / 255.f, 156 / 255.f, 178 / 255.f, 1.f};
constexpr sdl3::FColor TAB_ON{55 / 255.f, 115 / 255.f, 210 / 255.f, 1.f};
constexpr sdl3::FColor TAB_OFF{35 / 255.f, 38 / 255.f, 54 / 255.f, 1.f};
} // namespace pal

// État applicatif partagé entre les callbacks et la boucle.
struct App {
	// Entités retrouvées par nom après construction des scènes
	ecs::Entity lblClick{}, lblRadio{}, lblToggles{};
	ecs::Entity lblSlider{}, lblKnob{}, progress{}, lblProgress{};
	ecs::Entity lblEcho{}, lblScroll{};
	ecs::Entity lblCombo{}, lblList{}, lblTab{};
	ecs::Entity badge{};
	std::vector<ecs::Entity> navButtons;

	// Animation
	float progressT = 0.f;
	bool animRunning = true;
	float animSpeed = 0.25f;
	float canvasAngle = 0.f;
	int badgeCount = 0;

	// Page "Dynamique" (dynamisme de l'interface : propriétés live, auto-
	// scrollbar, graphe temps réel) — cf. scenes.Add("dynamique", ...).
	ecs::Entity sampleTextEntity{};              ///< fontSize piloté en direct
	std::vector<ecs::Entity> dynListItems;       ///< pool fixe, visibilité togglée (pas de despawn/respawn)
	ecs::Entity plotLiveEntity{};
	std::vector<float> plotLiveBuffer;
	float plotLivePhase = 0.f;
};

static const char *kPages[] = {"base", "controles", "saisie", "listes", "image", "dynamique"};
static const char *kPageLabels[] = {"Base", "Contrôles", "Saisie", "Listes", "Image", "Dynamique"};
static constexpr int K_PAGE_COUNT = 6;

/// Enveloppe `paragraph` (mots séparés par des espaces simples) en lignes
/// dont la largeur cumulée (mesurée via `measureWord`, en pixels) ne dépasse
/// pas `maxWidth` — utilisé pour composer un bloc de texte multi-paragraphe
/// à largeur FIXE avec un widget par MOT (« span », cf. la page Dynamique) :
/// UiFlow ne sait pas encore faire d'enveloppement de ligne lui-même (pas de
/// direction "Wrap"), donc l'enveloppement est calculé ici, une fois, à la
/// construction — chaque ligne résultante devient un `.Row()` de labels.
static std::vector<std::vector<String>> WrapWords(const String &paragraph, float maxWidth,
												   const std::function<float(const String &)> &measureWord,
												   float spaceWidth) {
	std::vector<std::vector<String>> lines;
	std::vector<String> current;
	float curWidth = 0.f;
	size_t start = 0;
	while (start < paragraph.size()) {
		size_t sp = paragraph.Find(' ', start);
		String word = sp == String::npos ? paragraph.Substr(start) : paragraph.Substr(start, sp - start);
		start = sp == String::npos ? paragraph.size() : sp + 1;
		if (word.IsEmpty())
			continue;
		float w = measureWord(word);
		float extra = current.empty() ? w : curWidth + spaceWidth + w;
		if (!current.empty() && extra > maxWidth) {
			lines.push_back(current);
			current.clear();
			curWidth = 0.f;
		}
		current.push_back(word);
		curWidth = current.size() == 1 ? w : curWidth + spaceWidth + w;
	}
	if (!current.empty())
		lines.push_back(current);
	return lines;
}

int main() {
	// ── Init SDL / TTF / fenêtre ─────────────────────────────────────────────
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO | sdl3::init_flags::EVENTS);
	if (!sdl) {
		std::cerr << "SDL init: " << sdl.Error().CStr() << "\n";
		return 1;
	}

	auto ttf = sdl3::TtfContext::Create();
	if (!ttf) {
		std::cerr << "TTF init: " << ttf.Error().CStr() << "\n";
		return 1;
	}

	auto fontRes = sdl3::Font::FindLocal({"DejaVuSans", "DejaVuSans-Bold", "FreeSans", "Arial", "Helvetica"}, FONT_PT);
	if (!fontRes) {
		std::cerr << "Font: " << fontRes.Error().CStr() << "\n";
		return 1;
	}
	auto &font = fontRes.Value();

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
	ui::SdlRendererBackend renBackend(ren);

	auto engRes = sdl3::TextEngine::Create(ren);
	if (!engRes) {
		std::cerr << "TextEngine: " << engRes.Error().CStr() << "\n";
		return 1;
	}
	auto &eng = engRes.Value();

	window.StartTextInput(); // pour les UiInput

	// ── Pipeline UI ──────────────────────────────────────────────────────────
	ecs::ArchetypeRegistry ar;
	ui::LayoutSystem layout;
	ui::InputSystem input;
	ui::RenderSystem render;
	ui::StyleSystem style;
	ui::UiFactory f(ar, layout, ui::UiTheme::Dark());
	style.sheet = &f.sheet;
	ui::SceneManager scenes(f);

	render.SetTextEngine(&eng, &font);
	layout.measureText = [&font](const String &s, float fs) -> sdl3::FPoint {
		if (auto sz = font.Measure(s); sz.IsSome())
			return {float(sz.Unwrap().x) * (fs / FONT_PT), fs * 1.35f};
		return {float(s.size()) * fs * 0.55f, fs * 1.3f};
	};

	// Polices d'icônes (cf. ui/glyphs.hpp) : chargées depuis assets/fonts/ et
	// enregistrées sous leur nom de famille pour que fac.Icon<E>(...) s'y
	// résolve au rendu (cf. ui::Glyphs::FontFamily<E>() / RenderSystem::RegisterFont).
	auto cupertinoFontRes = sdl3::Font::Open(String("assets/fonts/") + ui::CUPERTINOICONS_FILENAME, 22.f);
	if (cupertinoFontRes)
		render.RegisterFont(ui::Glyphs::FontFamily<ui::CupertinoIcons>(), cupertinoFontRes.Value());
	else
		std::cerr << "[warn] police CupertinoIcons : " << cupertinoFontRes.Error().CStr() << "\n";

	auto materialFontRes = sdl3::Font::Open(String("assets/fonts/") + ui::MATERIALICONS_FILENAME, 22.f);
	if (materialFontRes)
		render.RegisterFont(ui::Glyphs::FontFamily<ui::MaterialIcons>(), materialFontRes.Value());
	else
		std::cerr << "[warn] police MaterialIcons : " << materialFontRes.Error().CStr() << "\n";

	// Variantes gras/italique pour prop::Bold/prop::Italic (cf. styles.hpp) —
	// des objets sdl3::Font DISTINCTS de `font` (TTF_SetFontStyle est propre
	// à CHAQUE Font, pas au texte dessiné : le mettre sur `font` directement
	// aurait basculé TOUT le texte de l'appli en italique/gras). Conservés
	// en vie comme `fontRes` ci-dessus (référence dans le Result, pas de
	// déplacement — même convention que le reste du fichier). "Gras" tente
	// d'abord un fichier BOLD dédié (meilleur rendu qu'un style synthétique
	// sur la police normale), avec repli sur setStyle(Bold) sinon.
	auto italicFontRes = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, FONT_PT);
	if (italicFontRes) {
		italicFontRes.Value().SetStyle(sdl3::FontStyle::ITALIC);
		render.RegisterItalicFont(italicFontRes.Value());
	} else {
		std::cerr << "[warn] police italique : " << italicFontRes.Error().CStr() << "\n";
	}
	auto boldFontRes = sdl3::Font::FindLocal({"DejaVuSans-Bold", "FreeSansBold", "Arial-Bold"}, FONT_PT);
	bool boldIsSynthetic = false;
	if (!boldFontRes) {
		boldFontRes = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, FONT_PT);
		boldIsSynthetic = true;
	}
	if (boldFontRes) {
		if (boldIsSynthetic) // pas de fichier BOLD dédié trouvé : style synthétique sur la police normale
			boldFontRes.Value().SetStyle(sdl3::FontStyle::BOLD);
		render.RegisterBoldFont(boldFontRes.Value());
	} else {
		std::cerr << "[warn] police grasse : " << boldFontRes.Error().CStr() << "\n";
	}

	// Textures pour la page Image (chemins relatifs à la racine du projet).
	for (auto [key, path] : {std::pair{"logo", "assets/textures/default_logo.png"},
							 std::pair{"pierre", "assets/textures/default_mineral_stone_block.png"}}) {
		if (auto tex = sdl3::Texture::CreateFromFile(ren, path))
			render.textures.insert_or_assign(String(key), std::move(tex.Value()));
		else
			std::cerr << "[warn] texture " << path << ": " << tex.Error().CStr() << "\n";
	}

	App app;

	auto named = [&ar](const char *n) -> ecs::Entity {
		auto o = ui::FindByName(ar, String(n));
		return o.IsSome() ? o.Unwrap() : ecs::Entity{};
	};
	auto setLabel = [&](ecs::Entity e, String text) {
		if (!e.Valid())
			return;
		if (auto l = ar.GetComponent<ui::UiLabel>(e); l.IsSome()) {
			if (l.Unwrap()->text == text)
				return;
			l.Unwrap()->text = std::move(text);
			layout.MarkDirty();
		}
	};

	// ═════════════════════════════════════════════════════════════════════════
	// Page 1 — Base
	// ═════════════════════════════════════════════════════════════════════════
	scenes.Add("base", [&](ui::UiFactory &fac) {
		auto page = fac.Row();
		page.Name("page_base").Anchor(ui::Anchor::TopLeft).Offset(12.f, 64.f + TITLE_H).Gap(12.f).Pad(0.f);

		// Colonne gauche : labels + boutons
		auto left = fac.Panel();
		left.W(ui::Dimension::Px(420))
			.Gap(8.f)
			.Pad(14.f)
			.Children(fac.Label("Étiquettes").FontSize(18.f), fac.Label("Texte normal"),
					  fac.Label("Accent").TextColor(pal::ACCENT), fac.Label("Succès").TextColor(pal::GREEN),
					  fac.Label("Avertissement").TextColor(pal::ORANGE), fac.Label("Erreur").TextColor(pal::RED),
					  fac.Separator(), fac.Label("Boutons").FontSize(18.f),
					  fac.Button("Cliquez ici").Tooltip("Un bouton ordinaire").OnClick([&app, &setLabel] {
						  setLabel(app.lblClick, "Bouton cliqué !");
					  }),
					  fac.Button("Compteur badge +1")
						  .Tooltip("Incrémente la pastille de l'en-tête")
						  .OnClick([&app, &ar, &layout] {
							  ++app.badgeCount;
							  if (auto b = ar.GetComponent<ui::UiBadge>(app.badge); b.IsSome()) {
								  b.Unwrap()->text = String(String::From(app.badgeCount).c_str());
								  layout.MarkDirty();
							  }
						  }),
					  fac.Button("Désactivé").Disabled().Tooltip("Jamais affichée : widget inerte"),
					  fac.Label("Cliquez sur un bouton...").Name("lbl_click").TextColor(pal::MUTED));

		// Colonne droite : toggles + radios
		auto right = fac.Panel();
		right.W(ui::Dimension::Px(420))
			.Gap(8.f)
			.Pad(14.f)
			.Children(fac.Label("Interrupteurs").FontSize(18.f),
					  fac.Row().Gap(10.f).Children(
						  fac.Toggle(true).Name("tog_a").Tooltip("Fonction A").OnToggle([&app, &setLabel](bool) {
							  setLabel(app.lblToggles, "Toggles modifiés");
						  }),
						  fac.Toggle(false).Name("tog_b").Tooltip("Fonction B"),
						  fac.Checkbox(true).Tooltip("Case à cocher"),
						  fac.Toggle(false).Disabled().Tooltip("Interrupteur désactivé")),
					  fac.Label("—").Name("lbl_toggles").TextColor(pal::MUTED), fac.Separator(),
					  fac.Label("Boutons radio (groupe unique)").FontSize(18.f),
					  fac.Radio("grp", "Option A", true).OnToggle([&app, &setLabel](bool on) {
						  if (on)
							  setLabel(app.lblRadio, "Choix : A");
					  }),
					  fac.Radio("grp", "Option B").OnToggle([&app, &setLabel](bool on) {
						  if (on)
							  setLabel(app.lblRadio, "Choix : B");
					  }),
					  fac.Radio("grp", "Option C").OnToggle([&app, &setLabel](bool on) {
						  if (on)
							  setLabel(app.lblRadio, "Choix : C");
					  }),
					  fac.Label("Choix : A").Name("lbl_radio").TextColor(pal::ACCENT), fac.Separator(),
					  fac.Label("Icônes (ui::Glyphs — Cupertino / Material)").FontSize(18.f),
					  fac.Row().Gap(14.f).Align(ui::CrossAlign::Center).Children(
						  fac.Icon(ui::CupertinoIcons::HEART, 22.f)
							  .TextColor(pal::RED)
							  .Tooltip("CupertinoIcons::HEART"),
						  fac.Icon(ui::CupertinoIcons::SEARCH, 22.f).Tooltip("CupertinoIcons::SEARCH"),
						  fac.Icon(ui::CupertinoIcons::SETTINGS, 22.f).Tooltip("CupertinoIcons::SETTINGS"),
						  fac.Icon(ui::CupertinoIcons::BELL, 22.f)
							  .TextColor(pal::ORANGE)
							  .Tooltip("CupertinoIcons::BELL"),
						  fac.Separator(ui::Orientation::Vertical),
						  fac.Icon(ui::MaterialIcons::HOME, 22.f).TextColor(pal::ACCENT).Tooltip("MaterialIcons::HOME"),
						  fac.Icon(ui::MaterialIcons::FAVORITE, 22.f)
							  .TextColor(pal::RED)
							  .Tooltip("MaterialIcons::FAVORITE"),
						  fac.Icon(ui::MaterialIcons::SETTINGS, 22.f).Tooltip("MaterialIcons::SETTINGS"),
						  fac.Icon(ui::MaterialIcons::SEARCH, 22.f).Tooltip("MaterialIcons::SEARCH")));

		page.Children(std::move(left), std::move(right));
		ecs::Entity root = page.Spawn();
		app.lblClick = named("lbl_click");
		app.lblToggles = named("lbl_toggles");
		app.lblRadio = named("lbl_radio");
		return std::vector{root};
	});

	// ═════════════════════════════════════════════════════════════════════════
	// Page 2 — Contrôles
	// ═════════════════════════════════════════════════════════════════════════
	scenes.Add("controles", [&](ui::UiFactory &fac) {
		auto page = fac.Row();
		page.Name("page_ctrl").Anchor(ui::Anchor::TopLeft).Offset(12.f, 64.f + TITLE_H).Gap(12.f).Pad(0.f);

		auto left = fac.Panel();
		left.W(ui::Dimension::Px(460))
			.Gap(10.f)
			.Pad(14.f)
			.Children(
				fac.Label("Sliders (drag + molette)").FontSize(18.f),
				fac.Row().Gap(8.f).Children(fac.Label("Volume").W(ui::Dimension::Px(90)),
											fac.Slider(0.f, 1.f, 0.7f)
												.GrowW()
												.Tooltip("Continu [0-1] — molette : ±2 %")
												.OnChange([&app, &setLabel](float v) {
													setLabel(app.lblSlider,
															 String(std::format("Volume : {:.2f}", v).c_str()));
												})),
				fac.Row().Gap(8.f).Children(
					fac.Label("Pas de 5").W(ui::Dimension::Px(90)),
					fac.Slider(0.f, 100.f, 40.f, 5.f).GrowW().Tooltip("Quantifié : pas de 5 (drag comme molette)")),
				fac.Label("Volume : 0.70").Name("lbl_slider").TextColor(pal::MUTED), fac.Separator(),
				fac.Label("Slider vertical + potentiomètres").FontSize(18.f),
				fac.Row()
					.Gap(20.f)
					.Align(ui::CrossAlign::Center)
					.Children(fac.Slider(0.f, 1.f, 0.4f, 0.f, ui::Orientation::Vertical)
								  .Size(22.f, 130.f)
								  .Tooltip("Slider vertical"),
							  fac.Column()
								  .Gap(4.f)
								  .Align(ui::CrossAlign::Center)
								  .Children(fac.Knob(0.5f)
												.Size(64.f, 64.f)
												.Tooltip("Knob continu [0-1]")
												.OnChange([&app, &setLabel](float v) {
													setLabel(app.lblKnob,
															 String(std::format("Knob : {:.2f}", v).c_str()));
												}),
											fac.Label("continu").TextColor(pal::MUTED)),
							  fac.Column()
								  .Gap(4.f)
								  .Align(ui::CrossAlign::Center)
								  .Children(fac.Knob(0.f, 100.f, 50.f, 5.f)
												.Size(64.f, 64.f)
												.Tooltip("Knob [0-100], pas de 5 — molette aussi"),
											fac.Label("0-100, pas 5").TextColor(pal::MUTED)),
							  fac.Column()
								  .Gap(4.f)
								  .Align(ui::CrossAlign::Center)
								  .Children(fac.Knob(0.3f).Size(64.f, 64.f).Disabled().Tooltip("Knob désactivé"),
											fac.Label("désactivé").TextColor(pal::MUTED))),
				fac.Label("Knob : 0.50").Name("lbl_knob").TextColor(pal::MUTED));

		auto right = fac.Panel();
		right.W(ui::Dimension::Px(420))
			.Gap(10.f)
			.Pad(14.f)
			.Children(
				fac.Label("Barres de progression").FontSize(18.f),
				fac.Progress(0.f, 1.f, 0.25f).GrowW().H(ui::Dimension::Px(10)),
				fac.Progress(0.f, 1.f, 0.60f).GrowW().H(ui::Dimension::Px(10)), fac.Separator(),
				fac.Label("Progression animée").FontSize(18.f),
				fac.Row()
					.Gap(8.f)
					.Align(ui::CrossAlign::Center)
					.Children(fac.Progress(0.f, 1.f, 0.f).Name("prog_anim").GrowW().H(ui::Dimension::Px(12)),
							  fac.Label("0 %").Name("lbl_prog").W(ui::Dimension::Px(56)),
							  fac.Button("Pause").Tooltip("Suspend / reprend l'animation").OnClick([&app] {
								  app.animRunning = !app.animRunning;
							  })),
				fac.Row().Gap(8.f).Children(
					fac.Label("Vitesse").W(ui::Dimension::Px(90)),
					fac.Slider(0.05f, 2.f, 0.25f).GrowW().Tooltip("Vitesse de l'animation").OnChange([&app](float v) {
						app.animSpeed = v;
					})),
				fac.Separator(),
				fac.Row()
					.Gap(12.f)
					.Align(ui::CrossAlign::Center)
					.Children(fac.Spinner().Size(36.f, 36.f).Tooltip("Indicateur de chargement animé"),
							  fac.Label("Spinner (tourne en continu)").TextColor(pal::MUTED)));

		page.Children(std::move(left), std::move(right));
		ecs::Entity root = page.Spawn();
		app.lblSlider = named("lbl_slider");
		app.lblKnob = named("lbl_knob");
		app.progress = named("prog_anim");
		app.lblProgress = named("lbl_prog");
		return std::vector{root};
	});

	// ═════════════════════════════════════════════════════════════════════════
	// Page 3 — Saisie & scroll
	// ═════════════════════════════════════════════════════════════════════════
	scenes.Add("saisie", [&](ui::UiFactory &fac) {
		auto page = fac.Row();
		page.Name("page_saisie").Anchor(ui::Anchor::TopLeft).Offset(12.f, 64.f + TITLE_H).Gap(12.f).Pad(0.f);

		auto left = fac.Panel();
		left.W(ui::Dimension::Px(420))
			.Gap(8.f)
			.Pad(14.f)
			.Children(fac.Label("Champs de saisie").FontSize(18.f),
					  fac.Input("Votre nom...")
						  .GrowW()
						  .Tooltip("Tapez : le texte est répété dessous")
						  .onTextChange([&app, &setLabel](const String &t) {
							  String echo("Écho : ");
							  echo += t;
							  setLabel(app.lblEcho, std::move(echo));
						  }),
					  fac.Label("Écho : ").Name("lbl_echo").TextColor(pal::ACCENT),
					  fac.Input("email@exemple.com").GrowW().Tooltip("Un autre champ"),
					  fac.Input("Non éditable...").GrowW().Disabled().Tooltip("Champ désactivé"), fac.Separator(),
					  fac.Label("Barres de défilement autonomes").FontSize(18.f),
					  fac.Scrollbar(300.f, 100.f)
						  .GrowW()
						  .H(ui::Dimension::Px(12))
						  .Tooltip("Glissez le pouce — l'offset s'affiche dessous")
						  .onScroll([&app, &setLabel](float off) {
							  setLabel(app.lblScroll, String(std::format("Offset : {:.0f}", off).c_str()));
						  }),
					  fac.Row().Gap(12.f).Children(
						  fac.Scrollbar(200.f, 60.f, ui::Orientation::Vertical).Size(12.f, 120.f).Tooltip("Verticale"),
						  fac.Label("Offset : 0").Name("lbl_scroll").TextColor(pal::MUTED)));

		auto right = fac.Panel();
		right.W(ui::Dimension::Px(420))
			.Gap(8.f)
			.Pad(14.f)
			.Children(fac.Label("Conteneur scrollable (molette)").FontSize(18.f));
		auto list = fac.Column();
		list.Name("liste_scroll")
			.Scrollable()
			.GrowW()
			.H(ui::Dimension::Px(320))
			.Gap(4.f)
			.Pad(8.f)
			.Bg(sdl3::Color{22, 24, 36, 255})
			.Radius(6.f);
		for (int i = 1; i <= 25; ++i) {
			auto item = fac.Label(String(std::format("{:02d}. Élément défilable", i).c_str()));
			if (i % 3 == 0)
				item.TextColor(pal::ACCENT);
			list.Children(std::move(item));
		}
		right.Children(std::move(list), fac.Label("Molette au-dessus de la liste").TextColor(pal::MUTED));

		page.Children(std::move(left), std::move(right));
		ecs::Entity root = page.Spawn();
		app.lblEcho = named("lbl_echo");
		app.lblScroll = named("lbl_scroll");
		return std::vector{root};
	});

	// ═════════════════════════════════════════════════════════════════════════
	// Page 4 — Listes & conteneurs
	// ═════════════════════════════════════════════════════════════════════════
	scenes.Add("listes", [&](ui::UiFactory &fac) {
		auto page = fac.Row();
		page.Name("page_listes").Anchor(ui::Anchor::TopLeft).Offset(12.f, 64.f + TITLE_H).Gap(12.f).Pad(0.f);

		std::vector<String> fruits{"Pomme", "Banane", "Cerise", "Datte",  "Figue",  "Grenade",
								   "Kiwi",  "Litchi", "Mangue", "Orange", "Papaye", "Raisin"};

		auto left = fac.Panel();
		left.W(ui::Dimension::Px(420))
			.Gap(8.f)
			.Pad(14.f)
			.Children(fac.Label("ComboBox").FontSize(18.f),
					  fac.Combo({String("Français"), String("English"), String("Deutsch"), String("Español")}, 0)
						  .GrowW()
						  .Tooltip("Cliquez pour dérouler")
						  .OnChange([&app, &setLabel](float idx) {
							  setLabel(app.lblCombo, String(std::format("Langue n° {}", int(idx)).c_str()));
						  }),
					  fac.Label("Langue n° 0").Name("lbl_combo").TextColor(pal::MUTED), fac.Separator(),
					  fac.Label("ListBox (molette + clic)").FontSize(18.f),
					  fac.Listbox(fruits, 2)
						  .GrowW()
						  .H(ui::Dimension::Px(180))
						  .Tooltip("Sélection au clic, scroll interne à la molette")
						  .OnChange([&app, &setLabel](float idx) {
							  setLabel(app.lblList, String(std::format("Fruit n° {}", int(idx)).c_str()));
						  }),
					  fac.Label("Fruit n° 2").Name("lbl_list").TextColor(pal::MUTED));

		auto right = fac.Panel();
		right.W(ui::Dimension::Px(460))
			.Gap(10.f)
			.Pad(14.f)
			.Children(fac.Label("Expander (sections repliables)").FontSize(18.f),
					  fac.Expander("Réglages avancés", true)
						  .GrowW()
						  .Tooltip("Cliquez sur l'en-tête pour replier")
						  .Children(fac.Label("Contenu de la section A"), fac.Toggle(true),
									fac.Slider(0.f, 1.f, 0.3f).GrowW()),
					  fac.Expander("Section repliée au départ", false)
						  .GrowW()
						  .Children(fac.Label("Surprise ! Contenu caché."), fac.Checkbox(false)),
					  fac.Separator(), fac.Label("TabView (onglets)").FontSize(18.f),
					  fac.Tabview({String("Un"), String("Deux"), String("Trois")}, 0)
						  .GrowW()
						  .H(ui::Dimension::Px(150))
						  .Tooltip("Cliquez sur un onglet")
						  .OnChange([&app, &setLabel](float idx) {
							  setLabel(app.lblTab, String(std::format("Onglet actif : {}", int(idx)).c_str()));
						  })
						  .Children(fac.Column().Gap(4.f).Children(
										fac.Label("Contenu du premier onglet").TextColor(pal::ACCENT),
										fac.Label("Chaque onglet affiche son enfant.")),
									fac.Column().Gap(4.f).Children(fac.Label("Deuxième onglet").TextColor(pal::GREEN),
																   fac.Toggle(false)),
									fac.Column().Gap(4.f).Children(fac.Label("Troisième onglet").TextColor(pal::ORANGE),
																   fac.Slider(0.f, 1.f, 0.5f).GrowW())),
					  fac.Label("Onglet actif : 0").Name("lbl_tab").TextColor(pal::MUTED));

		page.Children(std::move(left), std::move(right));
		ecs::Entity root = page.Spawn();
		app.lblCombo = named("lbl_combo");
		app.lblList = named("lbl_list");
		app.lblTab = named("lbl_tab");
		return std::vector{root};
	});

	// ═════════════════════════════════════════════════════════════════════════
	// Page 5 — Image & Canvas
	// ═════════════════════════════════════════════════════════════════════════
	scenes.Add("image", [&](ui::UiFactory &fac) {
		auto page = fac.Row();
		page.Name("page_image").Anchor(ui::Anchor::TopLeft).Offset(12.f, 64.f + TITLE_H).Gap(12.f).Pad(0.f);

		auto left = fac.Panel();
		left.W(ui::Dimension::Px(480))
			.Gap(8.f)
			.Pad(14.f)
			.Children(fac.Label("Image — modes d'ajustement").FontSize(18.f));
		auto imgRow = fac.Row();
		imgRow.Gap(10.f);
		struct FitSpec {
			const char *lbl;
			ui::ImageFit fit;
		};
		for (auto [lbl, fit] : {FitSpec{"Fill", ui::ImageFit::FILL}, FitSpec{"Contain", ui::ImageFit::CONTAIN},
								FitSpec{"Cover", ui::ImageFit::COVER}, FitSpec{"None", ui::ImageFit::NONE}}) {
			auto img = fac.Image(String("pierre"), 100.f, 100.f);
			img.Fit(fit).Tooltip(String(std::format("ImageFit::{}", lbl).c_str()));
			auto col = fac.Column();
			col.Gap(4.f)
				.Align(ui::CrossAlign::Center)
				.Children(std::move(img), fac.Label(String(lbl)).TextColor(pal::MUTED));
			imgRow.Children(std::move(col));
		}
		left.Children(std::move(imgRow),
					  fac.Label("Texture assets/textures/... (placeholder si absente)").TextColor(pal::MUTED),
					  fac.Separator(), fac.Image(String("logo"), 128.f, 128.f).Tooltip("Logo — Fill"));

		auto right = fac.Panel();
		right.W(ui::Dimension::Px(420))
			.Gap(8.f)
			.Pad(14.f)
			.Children(fac.Label("Canvas — dessin SDL libre").FontSize(18.f),
					  fac.Canvas([&app](sdl3::Renderer &r, sdl3::FRect rect) {
							 // Damier de fond
							 r.SetDrawColor(sdl3::FColor{28 / 255.f, 32 / 255.f, 48 / 255.f, 1.f});
							 r.FillRect(rect);
							 const float CELL = 20.f;
							 r.SetDrawColor(sdl3::FColor{44 / 255.f, 50 / 255.f, 72 / 255.f, 120 / 255.f});
							 for (int ix = 0; ix * CELL < rect.w; ++ix)
								 for (int iy = 0; iy * CELL < rect.h; ++iy)
									 if ((ix + iy) % 2 == 0)
										 r.FillRect({rect.x + ix * CELL, rect.y + iy * CELL,
													 sdl3::Min(CELL, rect.w - ix * CELL),
													 sdl3::Min(CELL, rect.h - iy * CELL)});
							 // Carré tournant
							 float cx = rect.x + rect.w * 0.5f, cy = rect.y + rect.h * 0.5f;
							 float sz = sdl3::Min(rect.w, rect.h) * 0.32f;
							 float a = sdl3::DegToRad(app.canvasAngle);
							 sdl3::FPoint pts[5];
							 for (int i = 0; i < 4; ++i) {
								 float ang = a + float(i) * sdl3::PI_F * 0.5f;
								 pts[i] = {cx + sdl3::Cos(ang) * sz, cy + sdl3::Sin(ang) * sz};
							 }
							 pts[4] = pts[0];
							 r.SetDrawColor(pal::ACCENT);
							 for (int i = 0; i < 4; ++i)
								 r.DrawLine(pts[i], pts[i + 1]);
							 // Cercle pulsant
							 float pulse = sdl3::Abs(sdl3::Sin(a * 2.f)) * 10.f + 5.f;
							 r.SetDrawColor(sdl3::FColor{155 / 255.f, 75 / 255.f, 220 / 255.f, 1.f});
							 r.FillCircle({cx, cy}, pulse);
						 })
						  .GrowW()
						  .H(ui::Dimension::Px(240))
						  .Tooltip("Callback de dessin appelé à chaque frame"),
					  fac.Label("Appels SDL bruts, clip déjà appliqué").TextColor(pal::MUTED));

		page.Children(std::move(left), std::move(right));
		return std::vector{page.Spawn()};
	});

	// ═════════════════════════════════════════════════════════════════════════
	// Page 6 — Dynamique : propriétés live (fontSize), défilement automatique
	// (affichage/masquage du pouce selon le contenu), Splitter, et les widgets
	// des Phases 1-9 (DragValue/inputNumber/ColorPicker, Selectable/TreeNode,
	// Menu, Table, PlotLines temps réel, DatePicker, modale) + un bloc de
	// texte enrichi composé de spans (un UiLabel par mot, cf. wrapWords) —
	// largeur fixe, hauteur automatique (somme des lignes enveloppées).
	// ═════════════════════════════════════════════════════════════════════════
	scenes.Add("dynamique", [&](ui::UiFactory &fac) {
		auto page = fac.Row();
		page.Name("page_dyn").Anchor(ui::Anchor::TopLeft).Offset(12.f, 64.f + TITLE_H).Gap(12.f).Pad(0.f);

		// Popups/modale : entités racines indépendantes (cf. UiFactory::
		// popup()/modal()) — à ajouter au vecteur retourné pour que
		// SceneManager les cache/affiche avec le reste de la page (sinon
		// elles resteraient visibles par-dessus une AUTRE page après un
		// changement d'onglet si laissées ouvertes).
		std::vector<ecs::Entity> extraRoots;

		// ── Colonne 1 : dynamisme (fontSize live, auto-scrollbar, splitter) ──
		auto col1 = fac.Panel();
		col1.W(ui::Dimension::Px(340)).Gap(10.f).Pad(14.f);
		col1.Children(
			fac.Label("Taille de police en direct").FontSize(18.f),
			fac.Row().Gap(8.f).Align(ui::CrossAlign::Center).Children(
				fac.Label("Taille :").W(ui::Dimension::Px(70)),
				fac.DragValue(8.f, 32.f, 16.f, 1.f, 0.3f, 0).Tooltip("Glisser, ou double-clic pour taper")),
			fac.Label("Ce texte change de taille en direct").Name("lbl_sample_font").FontSize(16.f), fac.Separator(),
			fac.Label("Défilement automatique").FontSize(18.f),
			fac.Row().Gap(8.f).Align(ui::CrossAlign::Center).Children(
				fac.Label("Visibles :").W(ui::Dimension::Px(70)),
				fac.DragValue(0.f, 20.f, 5.f, 1.f, 0.3f, 0).Tooltip("0-20 éléments visibles dans la liste ci-dessous")));
		auto dynList = fac.Column();
		dynList.Name("dyn_list")
			.Scrollable()
			.GrowW()
			.H(ui::Dimension::Px(120.f))
			.Gap(2.f)
			.Pad(6.f)
			.Bg(sdl3::Color{22, 24, 36, 255})
			.Radius(6.f);
		for (int i = 0; i < 20; ++i) {
			auto item = fac.Label(String(std::format("Élément {:02d}", i + 1).c_str()));
			item.Name(String(std::format("dyn_ite{}", i).c_str())).Hidden();
			dynList.Children(std::move(item));
		}
		col1.Children(std::move(dynList),
					  fac.Label("Le pouce apparaît/disparaît selon le nombre visible").TextColor(pal::MUTED),
					  fac.Separator(), fac.Label("Splitter (glisser pour redimensionner)").FontSize(18.f));
		ecs::Entity col1E = col1.Spawn();

		// Splitter : eagerly-spawn (cf. UiFactory::Splitter()), attaché
		// manuellement dans le flow de col1 APRÈS son propre spawn (a besoin
		// de l'Entity du panneau avant/après déjà connue — même motif que
		// examples/ui_aero_windows.cpp).
		{
			auto splitLeft = fac.Panel();
			splitLeft.Pad(8.f).Children(fac.Label("A").TextColor(pal::ACCENT));
			auto splitRight = fac.Panel();
			splitRight.Pad(8.f).Children(fac.Label("B").TextColor(pal::GREEN));
			ecs::Entity splitContainer =
				fac.Splitter(std::move(splitLeft), std::move(splitRight), ui::Orientation::Horizontal, 100.f, 40.f, 40.f);
			ar.AddComponent(splitContainer, ui::UiParent{col1E});
			if (auto ch = ar.GetOrAddComponent<ui::UiChildren>(col1E); ch.IsSome())
				ch.Unwrap()->list.push_back(splitContainer);
			if (auto it = ar.GetOrAddComponent<ui::UiItem>(splitContainer); it.IsSome())
				it.Unwrap()->height = ui::Dimension::Px(120.f);
			layout.MarkDirty();
		}

		// ── Colonne 2 : nouveaux widgets (Phases 3, 4, 5, 6, 7, 8) ──────────
		auto col2 = fac.Column();
		col2.W(ui::Dimension::Px(360)).Gap(10.f).Pad(14.f);

		auto menuBarRow = fac.MenuBar();
		menuBarRow.Name("dyn_menubar").Bg(sdl3::Color{22, 24, 36, 255}).Radius(4.f);

		std::vector<String> fruitsSel{"Pomme", "Banane", "Cerise", "Datte"};
		auto selList = fac.Column();
		selList.SelectionRoot(true).Gap(2.f).Pad(6.f).GrowW().H(ui::Dimension::Px(90.f));
		for (int i = 0; i < int(fruitsSel.size()); ++i)
			selList.Children(fac.Selectable(fruitsSel[size_t(i)], i));

		auto tree = fac.Column();
		tree.SelectionRoot(true).Gap(2.f).Pad(6.f).GrowW().H(ui::Dimension::Px(90.f));
		auto treeCat = fac.TreeNode("Catégorie", 0, 0);
		treeCat.Children(fac.Selectable("Sous-item 1", 1), fac.Selectable("Sous-item 2", 2));
		tree.Children(std::move(treeCat));

		std::vector<ui::UiTableColumn> tblCols = {{"Nom", 100.f, 50.f, true}, {"Score", 80.f, 40.f, true}};
		std::vector<std::vector<String>> tblRows = {{"Alice", "87"}, {"Bob", "64"}, {"Chloé", "95"}};
		auto table = fac.Table(tblCols, tblRows, true, true);
		table.GrowW().H(ui::Dimension::Px(120.f));

		std::vector<float> plotInit(40, 0.f);
		auto plotLive = fac.PlotLines(plotInit, "Temps réel");
		plotLive.Name("dyn_plot").GrowW().H(ui::Dimension::Px(90.f));

		auto colorSwatch = fac.ColorSwatch(sdl3::FColor::UI_ACCENT_SKY_BRIGHT());
		colorSwatch.Size(28.f, 28.f).Name("dyn_swatch");

		col2.Children(
			fac.Label("Barre de menu (Phase 5)").FontSize(18.f), std::move(menuBarRow), fac.Separator(),
			fac.Label("Édition de valeurs (Phase 3)").FontSize(18.f),
			fac.Row().Gap(8.f).Align(ui::CrossAlign::Center).Children(
				fac.Label("Quantité :").W(ui::Dimension::Px(100)), fac.InputNumber(0.f, 10.f, 3.f, 1.f, 0)),
			fac.Row().Gap(8.f).Align(ui::CrossAlign::Center).Children(
				fac.Label("Couleur :").W(ui::Dimension::Px(100)), std::move(colorSwatch)),
			fac.Separator(), fac.Label("Sélection & arbre (Phase 4)").FontSize(18.f),
			fac.Row().Gap(10.f).Children(std::move(selList), std::move(tree)), fac.Separator(),
			fac.Label("Table (Phase 6)").FontSize(18.f), std::move(table),
			fac.Label("Graphe temps réel (Phase 7)").FontSize(18.f), std::move(plotLive), fac.Separator(),
			fac.Row().Gap(10.f).Children(
				fac.Button("Choisir une date...").Name("dyn_datebtn").Tooltip("Ouvre un DatePicker (Phase 8)"),
				fac.Button("Ouvrir la modale").Name("dyn_modalbtn").Tooltip("Boîte de dialogue (Phase 1)")));
		ecs::Entity col2E = col2.Spawn();
		(void)col2E;

		// ── Colonne 3 : texte enrichi — spans (Phase style « formatage ») ───
		auto col3 = fac.Panel();
		col3.W(ui::Dimension::Px(360)).Gap(8.f).Pad(14.f);
		col3.Children(fac.Label("Texte enrichi (widgets « span »)").FontSize(18.f).Bold(),
					  fac.Label("Bold / italique / souligné / barré / surligné").TextColor(pal::MUTED));

		constexpr float K_LOREM_WIDTH = 320.f;
		auto measureWord = [&font](const String &w) -> float {
			if (auto sz = font.Measure(w); sz.IsSome())
				return float(sz.Unwrap().x);
			return float(w.size()) * FONT_PT * 0.55f;
		};
		float spaceW = measureWord(String(" "));
		static const char *kLorem[] = {
			"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut "
			"labore et dolore magna aliqua.",
			"Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
			"commodo consequat.",
			"Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
			"pariatur."};
		auto loremBlock = fac.Column();
		loremBlock.Name("loreblock").W(ui::Dimension::Px(K_LOREM_WIDTH)).HAuto().Gap(3.f);
		for (const char *para : kLorem) {
			auto lines = WrapWords(String(para), K_LOREM_WIDTH, measureWord, spaceW);
			for (auto &lineWords : lines) {
				auto lineRow = fac.Row();
				lineRow.Gap(4.f);
				for (auto &word : lineWords)
					lineRow.Children(fac.Label(word).Italic());
				loremBlock.Children(std::move(lineRow));
			}
			loremBlock.Children(fac.Label(" ")); // espaceur entre paragraphes
		}
		col3.Children(std::move(loremBlock), fac.Separator(),
					  fac.Row().Gap(10.f).Align(ui::CrossAlign::Center).Children(
						  fac.Label("souligné").Underline(), fac.Label("barré").Strikethrough(),
						  fac.Label("surligné").Highlight().HighlightColor(sdl3::FColor{255 / 255.f, 220 / 255.f, 60 / 255.f, 140 / 255.f})));

		page.Children(std::move(col1), std::move(col2), std::move(col3));
		ecs::Entity root = page.Spawn();

		// ── Câblage post-spawn : entités retrouvées par nom, popups (Phase
		// 1/3/5/8), callbacks — même contrainte que partout ailleurs cette
		// session (une entité n'existe qu'après le spawn de son parent).
		app.sampleTextEntity = named("lbl_sample_font");
		for (int i = 0; i < 20; ++i)
			app.dynListItems.push_back(named(std::format("dyn_ite{}", i).c_str()));
		app.plotLiveEntity = named("dyn_plot");
		app.plotLiveBuffer.assign(40, 0.f);

		auto colorPicker = fac.ColorPicker(sdl3::FColor::UI_ACCENT_SKY_BRIGHT(), [](sdl3::FColor) {});
		ecs::Entity colorPickerE = colorPicker.Spawn();
		extraRoots.push_back(colorPickerE);
		if (auto e = ui::FindByName(ar, "dyn_swatch"); e.IsSome()) {
			ecs::Entity swatchE = e.Unwrap();
			if (auto cb = ar.GetOrAddComponent<ui::UiCallbacks>(swatchE); cb.IsSome())
				cb.Unwrap()->onClick = [&ar, &layout, swatchE, colorPickerE] {
					ui::OpenPopup(ar, layout, colorPickerE, swatchE);
				};
		}

		auto datePicker = fac.DatePicker(2026, 8, 10);
		ecs::Entity datePickerE = datePicker.Spawn();
		extraRoots.push_back(datePickerE);
		if (auto e = ui::FindByName(ar, "dyn_datebtn"); e.IsSome()) {
			ecs::Entity btnE = e.Unwrap();
			if (auto cb = ar.GetOrAddComponent<ui::UiCallbacks>(btnE); cb.IsSome())
				cb.Unwrap()->onClick = [&ar, &layout, btnE, datePickerE] {
					ui::OpenPopup(ar, layout, datePickerE, btnE);
				};
		}

		auto modalContent = fac.Panel();
		modalContent.Size(320.f, 140.f).Gap(10.f).Pad(16.f);
		modalContent.Children(fac.Label("Boîte de dialogue modale").FontSize(18.f),
							  fac.Label("Bloque le reste de l'UI (Échap ou le bouton ferme).").TextColor(pal::MUTED),
							  fac.Button("Fermer").Name("dyn_modalclose").AlignSelf(ui::CrossAlign::End));
		auto modalW = fac.Modal(std::move(modalContent));
		ecs::Entity modalE = modalW.Spawn();
		extraRoots.push_back(modalE);
		if (auto e = ui::FindByName(ar, "dyn_modalclose"); e.IsSome()) {
			ecs::Entity closeE = e.Unwrap();
			if (auto cb = ar.GetOrAddComponent<ui::UiCallbacks>(closeE); cb.IsSome())
				cb.Unwrap()->onClick = [&input, &ar, &layout, modalE] { input.CloseModal(ar, layout, modalE); };
		}
		if (auto e = ui::FindByName(ar, "dyn_modalbtn"); e.IsSome()) {
			ecs::Entity btnE = e.Unwrap();
			if (auto cb = ar.GetOrAddComponent<ui::UiCallbacks>(btnE); cb.IsSome())
				cb.Unwrap()->onClick = [&input, &ar, &layout, modalE] { input.OpenModal(ar, layout, modalE); };
		}

		// Petit menu "Fichier" (Phase 5) — deux entrées informatives.
		if (auto e = ui::FindByName(ar, "dyn_menubar"); e.IsSome()) {
			ecs::Entity barE = e.Unwrap();
			fac.Menu(barE, "Fichier", fac.MenuItem("Nouveau"), fac.MenuItem("Ouvrir"),
					fac.MenuItem("Quitter").OnClick([] { std::cout << "Quitter (démo, sans effet)\n"; }));
			fac.Menu(barE, "Aide", fac.MenuItem("À propos"));
		}

		// fontSize live et compteur d'éléments visibles : les 2 UiDragValue de
		// col1, distingués par position Y (le premier posé = fontSize, plus
		// haut dans la page). Câblés ici (après spawn) plutôt qu'en ligne
		// via .OnChange() lors de la construction, pour rester cohérent avec
		// le reste du câblage post-spawn de cette page.
		{
			std::vector<std::pair<float, ecs::Entity>> dragValues;
			ar.Query<ui::UiDragValue, ui::UiComputed>(
				[&](ecs::Entity e, ui::UiDragValue &, ui::UiComputed &c) { dragValues.push_back({c.screen.y, e}); });
			std::sort(dragValues.begin(), dragValues.end(),
					 [](auto &a, auto &b) { return a.first < b.first; });
			if (!dragValues.empty()) {
				ecs::Entity fontDrag = dragValues.front().second;
				if (auto cb = ar.GetOrAddComponent<ui::UiCallbacks>(fontDrag); cb.IsSome())
					cb.Unwrap()->onChange = [&ar, &app](float v) {
						if (app.sampleTextEntity.Valid())
							ui::EditInlineStyle(ar, app.sampleTextEntity, [&](ui::UiStyle &s) { s.SetFontSize(v); });
					};
			}
			if (dragValues.size() >= 2) {
				ecs::Entity visDrag = dragValues[1].second;
				if (auto cb = ar.GetOrAddComponent<ui::UiCallbacks>(visDrag); cb.IsSome())
					cb.Unwrap()->onChange = [&ar, &layout, &app](float v) {
						int n = int(v + 0.5f);
						for (int i = 0; i < int(app.dynListItems.size()); ++i) {
							ecs::Entity item = app.dynListItems[size_t(i)];
							if (!item.Valid())
								continue;
							if (i < n)
								ar.RemoveComponent<ui::UiHidden>(item);
							else if (!ar.HasComponent<ui::UiHidden>(item))
								ar.AddComponent(item, ui::UiHidden{});
						}
						layout.MarkDirty();
					};
			}
		}

		extraRoots.push_back(root);
		return extraRoots;
	});

	// ═════════════════════════════════════════════════════════════════════════
	// En-tête : titre + navigation + badge (hors scènes, toujours visible)
	// ═════════════════════════════════════════════════════════════════════════
	auto switchPage = [&](int idx) {
		scenes.SwitchTo(String(kPages[idx]));
		for (int i = 0; i < int(app.navButtons.size()); ++i) {
			ui::EditInlineStyle(ar, app.navButtons[i],
								[&](ui::UiStyle &s) { s.SetBg((i == idx) ? pal::TAB_ON : pal::TAB_OFF); });
		}
	};

	// Encadrement de fenêtre (barre de titre, barre d'état, redimensionnement)
	// — après l'enregistrement des polices d'icônes, qu'il réutilise.
	ui::WindowFrame frame;
	frame.Build(f, layout, render, window,
				{.title = "ui:: - vitrine des widgets", .appIcon = Some(ui::MaterialIcons::WIDGETS),
				 .status = "Six pages : naviguer avec les onglets de l'en-tête.", .titleHeight = TITLE_H});

	{
		auto header = f.Row();
		header.Name("header")
			.Anchor(ui::Anchor::TopLeft)
			.Offset(0.f, TITLE_H)
			.W(ui::Dimension::Rpct(100.f))
			.H(ui::Dimension::Px(52))
			.Gap(8.f)
			.Pad(math::Sides{12.f, 8.f, 12.f, 8.f})
			.Align(ui::CrossAlign::Center)
			.Bg(sdl3::Color{24, 26, 38, 255});
		header.Children(f.Label("ui:: showcase").FontSize(18.f).TextColor(pal::ACCENT).GrowW());
		for (int i = 0; i < K_PAGE_COUNT; ++i) {
			auto btn = f.Button(String(kPageLabels[i]));
			btn.Name(String(std::format("nav_{}", i).c_str()))
				.W(ui::Dimension::Px(110))
				.H(ui::Dimension::Px(32))
				.Tooltip(String(std::format("Aller à la page {}", kPageLabels[i]).c_str()))
				.OnClick([&switchPage, i] { switchPage(i); });
			header.Children(std::move(btn));
		}
		header.Children(f.Spinner().Size(24.f, 24.f).Tooltip("Toujours animé"),
						f.Badge(String("0")).Name("badge").Tooltip("Compteur (page Base)"));
		header.Spawn();
		app.badge = named("badge");
		for (int i = 0; i < K_PAGE_COUNT; ++i)
			app.navButtons.push_back(named(std::format("nav_{}", i).c_str()));
	}

	switchPage(0);

	// ═════════════════════════════════════════════════════════════════════════
	// Boucle principale
	// ═════════════════════════════════════════════════════════════════════════
	// Chaque évènement SDL est transmis directement à input.HandleEvent() —
	// aucune structure agrégée par frame. C'est important : agréger plusieurs
	// évènements dans un seul struct par frame écrasait des positions de
	// souris (une MOTION après un DOWN perdait les coordonnées du clic) et
	// pouvait faire cohabiter pressed+released d'un clic rapide dans la même
	// passe, d'où des interactions "corrompues". tick() gère séparément les
	// mises à jour continues (animations, infobulle) qui ne dépendent pas
	// d'un évènement discret.
	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();

	while (running) {
		uint64_t now = sdl3::GetTicksMS();
		float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		// Animations applicatives.
		app.canvasAngle = sdl3::Fmod(app.canvasAngle + dt * 90.f, 360.f);
		if (app.animRunning) {
			app.progressT += dt * app.animSpeed;
			if (app.progressT > 1.f)
				app.progressT -= 1.f;
		}
		if (app.progress.Valid()) {
			if (auto p = ar.GetComponent<ui::UiProgress>(app.progress); p.IsSome())
				p.Unwrap()->value = app.progressT;
			setLabel(app.lblProgress, String(std::format("{:.0f} %", app.progressT * 100.f).c_str()));
		}
		// Graphe temps réel (page Dynamique) : fait glisser un buffer circulaire
		// et pousse un nouvel échantillon (onde + un peu de bruit) — démontre
		// qu'un Plot déjà spawné réagit à PlotSeries::setY() appelé en
		// continu, pas seulement à la construction.
		if (app.plotLiveEntity.Valid()) {
			app.plotLivePhase += dt * 2.f;
			if (!app.plotLiveBuffer.empty())
				app.plotLiveBuffer.erase(app.plotLiveBuffer.begin());
			app.plotLiveBuffer.push_back(sdl3::Sin(app.plotLivePhase) * 0.8f + sdl3::Sin(app.plotLivePhase * 2.7f) * 0.2f);
			if (auto p = ar.GetComponent<ui::UiPlot>(app.plotLiveEntity); p.IsSome() && !p.Unwrap()->series.empty())
				p.Unwrap()->series[0].SetY(app.plotLiveBuffer);
		}

		auto ws = window.GetSize();
		layout.RunIfNeeded(ar, float(ws.x), float(ws.y));

		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE)) {
				running = false;
				break;
			}
			input.HandleEvent(ar, e, layout);
		}
		input.Tick(ar, layout, dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		// Un évènement (scroll, changement de page, repli d'un expander...)
		// a pu marquer le layout dirty : on le rejoue avant de dessiner.
		// Cascade de styles avant le layout (fontSize effectif pour les
		// unités em) — cf. StyleSystem::Resolve().
		style.Resolve(ar);
		layout.RunIfNeeded(ar, float(ws.x), float(ws.y));

		ren.SetDrawColor(pal::BG);
		ren.Clear();
		render.Run(ar, renBackend, &input.tooltip);
		ren.Present();
	}

	return 0;
}
