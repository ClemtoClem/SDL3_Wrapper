// Écrans des réglages : six onglets, saisie d'une touche, choix du skin.
#include "common.hpp"

#include <algorithm>
#include <cmath>

namespace voxelia {

using namespace uiutil;

namespace {

String Percent(float v) { return String::Format("%d %%", int(std::lround(v * 100.f))); }

// ── Paramètres ───────────────────────────────────────────────────────────────

class SettingsScreen final : public Screen {
public:
	SettingsScreen(Shell &shell_, SettingsTab tab) : Screen(shell_), m_tab(int(tab)), m_draft(shell_.Prefs()) {}
	String Id() const override { return "settings"; }

	void Build(ui::Widget root) override {
		const Palette &p = kit.Colors();
		const float s = kit.Scale();
		ui::Widget page = Page(kit, root, 1180.f);
		kit.ScreenHeader(page, "Paramètres", shell.InGame() ? String("Les réglages du monde en cours sont signalés par une icône de globe.")
															: String("Préférences du joueur, communes à tous les mondes."),
						 [this] { Leave(); });
		WidgetBuilder body = kit.Row(20.f);
		body.GrowWidth().GrowHeight().AlignItems(ui::Align::Stretch);
		ui::Widget b = page.Append(body);
		// Navigation latérale.
		WidgetBuilder nav = kit.Column(4.f);
		nav.Width(230.f * s);
		ui::Widget n = b.Append(nav);
		const std::pair<MaterialIcons, const char *> tabs[] = {
			{MaterialIcons::TUNE, "Général"},		{MaterialIcons::KEYBOARD, "Commandes"}, {MaterialIcons::DISPLAY_SETTINGS, "Graphismes"},
			{MaterialIcons::MUSIC_NOTE, "Audio"},	{MaterialIcons::BOLT, "Jeu"},			{MaterialIcons::ACCESSIBILITY, "Accessibilité"}};
		for (int i = 0; i < 6; ++i) {
			const bool on = i == m_tab;
			WidgetBuilder item = kit.Row(10.f);
			item.GrowWidth().Height(42.f * s).Padding(math::Sides(12.f * s, 0.f)).BorderRadius(8.f);
			item.Background(on ? WithAlpha(p.primary, 0.16f) : WithAlpha(p.bg3, 0.f)).HoverStyle(ui::Style().Background(p.bg3));
			item.BorderColor(on ? p.primary : WithAlpha(p.line, 0.f)).BorderWidth(math::Sides(3.f, 0.f, 0.f, 0.f)).OnClick([this, i] {
				m_tab = i;
				shell.UiSound("ui_click");
				shell.Rebuild();
			});
			ui::Widget it = n.Append(item);
			it.Append(kit.Icon(tabs[i].first, 20.f, Some(on ? p.primary : p.textMuted)).PointerThrough());
			it.Append(kit.Label(tabs[i].second, Text::BODY).Color(on ? p.text : p.textMuted).PointerThrough());
		}
		WidgetBuilder content = kit.Scroll(8.f);
		content.GrowWidth().GrowHeight();
		ui::Widget c = b.Append(content);
		switch (m_tab) {
		case 0: General(c); break;
		case 1: Controls(c); break;
		case 2: Graphics(c); break;
		case 3: Audio(c); break;
		case 4: Gameplay(c); break;
		default: Accessibility(c); break;
		}
		ui::Widget footer = kit.Footer(page);
		m_status = footer.Append(kit.Muted(String(), Text::SMALL).GrowWidth().Color(p.warning));
		footer.Append(kit.GhostButton("Valeurs par défaut de l'onglet", [this] { ResetTab(); }));
		footer.Append(kit.Button("Annuler", [this] {
			m_draft = shell.Prefs();
			shell.Pop();
		}));
		footer.Append(kit.PrimaryButton("Appliquer", [this] { Apply(); }));
		Changed();
	}
	bool OnBack() override {
		Leave();
		return true;
	}

private:
	void Changed() {
		const bool dirty = json::Encode(m_draft.ToJson()) != json::Encode(shell.Prefs().ToJson());
		kit.SetText(m_status, dirty ? String("Modifications non appliquées") : String());
	}
	template <typename T> std::function<void(T)> Set(T Settings::*field) {
		return [this, field](T v) {
			m_draft.*field = v;
			Changed();
		};
	}
	std::function<void(float)> SetInt(int Settings::*field) {
		return [this, field](float v) {
			m_draft.*field = int(std::lround(v));
			Changed();
		};
	}
	std::function<void(int)> SetChoice(int Settings::*field) {
		return [this, field](int v) {
			m_draft.*field = v;
			Changed();
		};
	}

	void General(ui::Widget c) {
		kit.SectionTitle(c, "Profil", Some(MaterialIcons::PERSON));
		WidgetBuilder name = kit.Input("Joueur");
		name.Width(260.f).MaxLength(24).OnTextChange([this](const String &t) {
			m_draft.playerName = t.Trim().IsEmpty() ? String("Joueur") : t.Trim();
			Changed();
		});
		kit.SettingRow(c, "Nom du joueur", "Affiché dans la discussion et enregistré avec le personnage.", std::move(name));
		kit.SetInputText(kit.LastControl(), m_draft.playerName);
		WidgetBuilder skinRow = kit.Row(10.f);
		kit.SettingRow(c, "Skin", "Apparence du personnage (vue à la troisième personne : F5).", std::move(skinRow));
		ui::Widget sk = kit.LastControl();
		String skin = m_draft.skin;
		if (skin.IsEmpty() && shell.MenuGame())
			skin = shell.MenuGame()->Content().ConfigStr("player.skin");
		sk.Append(SkinFront(shell, skin, 64.f));
		sk.Append(kit.Button("Choisir…", [this] { shell.Push(MakeSkinPicker(shell)); }));
		kit.SectionTitle(c, "Langue et interface", Some(MaterialIcons::LANGUAGE));
		kit.ComboRow(c, "Langue", "Les textes du jeu et des mods sont en français.", {"Français"}, 0, [](int) {});
		kit.SliderRow(c, "Échelle de l'interface", "Taille des menus et des textes.", 0.75f, 1.5f, m_draft.uiScale, 0.05f, Percent,
					  Set(&Settings::uiScale));
		kit.ToggleRow(c, "Astuces", "Conseils à l'accueil et pendant les chargements.", m_draft.showTips, Set(&Settings::showTips));
		kit.SectionTitle(c, "Notifications", Some(MaterialIcons::NOTIFICATION_ADD));
		kit.ComboRow(c, "Notifications affichées", "Les erreurs restent toujours affichées.", {"Aucune", "Importantes", "Toutes"},
					 m_draft.notifications, SetChoice(&Settings::notifications));
		kit.SliderRow(c, "Durée d'affichage", "Temps pendant lequel une notification reste visible.", 2.f, 10.f, m_draft.notificationSeconds, 0.5f,
					  [](float v) { return String::Format("%.1f s", double(v)); }, Set(&Settings::notificationSeconds));
		kit.SectionTitle(c, "Confirmations", Some(MaterialIcons::CHECK_CIRCLE));
		kit.ToggleRow(c, "Avant de quitter un monde", "Demande confirmation au retour au menu.", m_draft.confirmQuit, Set(&Settings::confirmQuit));
		kit.ToggleRow(c, "Avant de supprimer", "Mondes, mods.", m_draft.confirmDelete, Set(&Settings::confirmDelete));
		kit.ToggleRow(c, "Avant de changer les mods d'un monde", "Récapitulatif et sauvegarde de sécurité.", m_draft.confirmModChanges,
					  Set(&Settings::confirmModChanges));
		kit.SectionTitle(c, "Sauvegarde", Some(MaterialIcons::SAVE));
		kit.SliderRow(c, "Sauvegarde automatique", "Enregistre la partie à intervalle régulier (0 : jamais).", 0.f, 15.f, float(m_draft.autosaveMinutes),
					  1.f, [](float v) { return v < 0.5f ? String("jamais") : String::Format("%d min", int(v)); }, SetInt(&Settings::autosaveMinutes));
		kit.ToggleRow(c, "Pause si la fenêtre perd le focus", "Ouvre le menu pause quand on change de fenêtre.", m_draft.pauseOnFocusLoss,
					  Set(&Settings::pauseOnFocusLoss));
	}

	void Controls(ui::Widget c) {
		const Palette &p = kit.Colors();
		kit.SectionTitle(c, "Souris et caméra", Some(MaterialIcons::MOUSE));
		kit.SliderRow(c, "Sensibilité de la souris", "", 0.2f, 3.f, m_draft.mouseSensitivity, 0.05f, Percent, Set(&Settings::mouseSensitivity));
		kit.ToggleRow(c, "Inverser l'axe vertical", "Pousser la souris vers l'avant fait regarder vers le bas.", m_draft.invertY, Set(&Settings::invertY));
		kit.SliderRow(c, "Champ de vision", "", 50.f, 110.f, m_draft.fov, 1.f, [](float v) { return String::Format("%d°", int(v)); },
					  Set(&Settings::fov));
		kit.ToggleRow(c, "Balancement de la vue", "La caméra suit le pas du personnage.", m_draft.viewBobbing, Set(&Settings::viewBobbing));
		kit.SectionTitle(c, "Comportement des touches", Some(MaterialIcons::KEYBOARD));
		kit.ToggleRow(c, "Course en bascule", "Un appui active la course, un second l'arrête.", m_draft.toggleSprint, Set(&Settings::toggleSprint));
		kit.ToggleRow(c, "Accroupi en bascule", "", m_draft.toggleSneak, Set(&Settings::toggleSneak));
		kit.ToggleRow(c, "Double attribution", "Autorise une même touche pour plusieurs actions.", m_draft.allowDualBinding,
					  Set(&Settings::allowDualBinding));
		// Conflits.
		const auto conflicts = m_draft.Conflicts();
		if (!conflicts.empty()) {
			WidgetBuilder banner = kit.Column(3.f);
			banner.GrowWidth().Padding(math::Sides(12.f, 10.f)).BorderRadius(8.f).Background(WithAlpha(p.warning, 0.12f)).BorderColor(WithAlpha(p.warning, 0.5f)).BorderWidth(1.f);
			ui::Widget bn = c.Append(banner);
			bn.Append(kit.Label(String::Format("%d conflit(s) de raccourcis", int(conflicts.size())), Text::SMALL).Bold().Color(p.warning));
			for (const auto &[a, b] : conflicts)
				bn.Append(kit.Muted(String::Format("« %s » et « %s » partagent une touche", m_draft.Binding(a)->label.CStr(), m_draft.Binding(b)->label.CStr()),
						  Text::SMALL));
		}
		String category;
		for (size_t i = 0; i < m_draft.bindings.size(); ++i) {
			const KeyBinding &kb = m_draft.bindings[i];
			if (kb.category != category) {
				category = kb.category;
				kit.SectionTitle(c, category, NONE);
			}
			bool conflict = false;
			for (const auto &[a, b] : conflicts)
				conflict = conflict || a == kb.action || b == kb.action;
			WidgetBuilder row = kit.Row(10.f);
			row.GrowWidth().Padding(math::Sides(12.f, 6.f)).BorderRadius(8.f).Background(WithAlpha(p.bg2, 0.6f));
			ui::Widget r = c.Append(row);
			r.Append(kit.Label(kb.label, Text::BODY).Color(conflict ? p.warning : p.text).GrowWidth());
			for (int slot = 0; slot < 2; ++slot) {
				WidgetBuilder key = kit.Button(InputName(kb.keys[size_t(slot)]), [this, i, slot] {
					shell.Push(MakeKeyCapture(shell, m_draft.bindings[i].label, [this, i, slot](int code) {
						if (!m_draft.allowDualBinding && code > 0)
							for (KeyBinding &other : m_draft.bindings)
								for (int &k : other.keys)
									if (k == code && &other != &m_draft.bindings[i])
										k = 0; // la touche est retirée de l'autre action
						m_draft.bindings[i].keys[size_t(slot)] = code;
						Changed();
						shell.Rebuild();
					}));
				});
				key.Width(150.f);
				if (slot == 1 && kb.keys[1] == 0)
					key.Tooltip("Touche secondaire (facultative)");
				r.Append(key);
			}
			WidgetBuilder reset = kit.IconButton(MaterialIcons::REFRESH, "Touche par défaut", [this, i] {
				m_draft.bindings[i].keys = m_draft.bindings[i].defaults;
				Changed();
				shell.Rebuild();
			});
			if (kb.keys == kb.defaults)
				reset.Disabled();
			r.Append(reset);
		}
		c.Append(kit.Button("Restaurer toutes les commandes par défaut", [this] {
			m_draft.ResetBindings();
			Changed();
			shell.Rebuild();
		}));
	}

	void Graphics(ui::Widget c) {
		kit.SectionTitle(c, "Affichage", Some(MaterialIcons::DISPLAY_SETTINGS));
		kit.ComboRow(c, "Mode d'affichage", "Le plein écran utilise la résolution du bureau.", {"Fenêtre", "Plein écran"}, m_draft.displayMode,
					 SetChoice(&Settings::displayMode));
		static const std::vector<std::pair<int, int>> sizes{{1024, 640}, {1280, 720}, {1280, 760}, {1366, 768}, {1600, 900}, {1920, 1080}};
		std::vector<String> names;
		int current = 0;
		for (size_t i = 0; i < sizes.size(); ++i) {
			names.push_back(String::Format("%d × %d", sizes[i].first, sizes[i].second));
			if (sizes[i].first == m_draft.width && sizes[i].second == m_draft.height)
				current = int(i);
		}
		kit.ComboRow(c, "Résolution", "Taille de la fenêtre (mode fenêtré).", names, current, [this](int i) {
			m_draft.width = sizes[size_t(i)].first;
			m_draft.height = sizes[size_t(i)].second;
			Changed();
		});
		int hz = 60;
		if (const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay()))
			hz = int(mode->refresh_rate);
		kit.SettingRow(c, "Fréquence de rafraîchissement", "Imposée par l'écran.", kit.Label(String::Format("%d Hz", hz)), true);
		kit.ToggleRow(c, "Synchronisation verticale", "Évite les déchirures ; limite les images à la fréquence de l'écran.", m_draft.vsync,
					  Set(&Settings::vsync));
		kit.ComboRow(c, "Limite d'images", "Économise l'énergie quand la synchronisation est désactivée.", {"Illimitée", "30", "60", "120", "144"},
					 m_draft.fpsLimit == 30 ? 1 : m_draft.fpsLimit == 60 ? 2 : m_draft.fpsLimit == 120 ? 3 : m_draft.fpsLimit == 144 ? 4 : 0,
					 [this](int i) {
						 m_draft.fpsLimit = std::array<int, 5>{0, 30, 60, 120, 144}[size_t(i)];
						 Changed();
					 });
		kit.SectionTitle(c, "Qualité", Some(MaterialIcons::LIGHTBULB));
		kit.SliderRow(c, "Distance d'affichage", "Chunks de 16 blocs autour du joueur.", 2.f, 16.f, float(m_draft.viewDistance), 1.f,
					  [](float v) { return String::Format("%d chunks", int(v)); }, SetInt(&Settings::viewDistance));
		kit.ComboRow(c, "Ombres", "Ombres portées des créatures au sol ; « marquées » accentue aussi l'ombrage des faces.",
					 {"Désactivées", "Créatures", "Créatures + faces marquées"}, m_draft.shadows, SetChoice(&Settings::shadows));
		kit.ComboRow(c, "Éclairage dynamique", "Les torches, lanternes et blocs lumineux proches éclairent les alentours.",
					 {"Désactivé", "4 lumières", "8 lumières", "12 lumières"}, m_draft.dynamicLights / 4, [this](int i) {
						 m_draft.dynamicLights = i * 4;
						 Changed();
					 });
		kit.SliderRow(c, "Luminosité", "Lumière ambiante (surtout la nuit et sous terre).", 0.f, 1.f, m_draft.brightness, 0.05f, Percent,
					  Set(&Settings::brightness));
		kit.ToggleRow(c, "Filtrage lissé des textures", "Désactivé : pixels nets, style voxel.", m_draft.smoothTextures, Set(&Settings::smoothTextures));
		kit.SectionTitle(c, "Effets", Some(MaterialIcons::BOLT));
		kit.ComboRow(c, "Particules", "Éclats des blocs cassés.", {"Aucune", "Réduites", "Toutes"}, m_draft.particles, SetChoice(&Settings::particles));
		kit.ToggleRow(c, "Nuages", "Couche de nuages dérivant avec le vent.", m_draft.clouds, Set(&Settings::clouds));
		kit.ToggleRow(c, "Vignettage", "Assombrit légèrement les bords de l'écran.", m_draft.vignette, Set(&Settings::vignette));
		kit.ToggleRow(c, "Effets d'écran", "Teinte sous l'eau, éclair rouge des blessures.", m_draft.screenEffects, Set(&Settings::screenEffects));
		kit.SectionTitle(c, "Rendu voxel", Some(MaterialIcons::TERRAIN));
		kit.ToggleRow(c, "Occlusion ambiante", "Assombrit les coins entre blocs (relief plus lisible).", m_draft.ambientOcclusion,
					  Set(&Settings::ambientOcclusion));
		kit.SliderRow(c, "Densité de végétation", "Part des herbes et fleurs affichées.", 0.f, 1.f, m_draft.vegetationDensity, 0.05f, Percent,
					  Set(&Settings::vegetationDensity));
		kit.SliderRow(c, "Distance des créatures", "Au-delà, les créatures ne sont plus dessinées.", 16.f, 128.f, float(m_draft.entityDistance), 4.f,
					  [](float v) { return String::Format("%d blocs", int(v)); }, SetInt(&Settings::entityDistance));
		WidgetBuilder exp = ui::Expander("Paramètres avancés", false);
		exp.GrowWidth();
		ui::Widget e = c.Append(exp);
		WidgetBuilder col = kit.Column(8.f);
		col.GrowWidth().Padding(math::Sides(0.f, 8.f));
		ui::Widget ce = e.Append(col);
		kit.SliderRow(ce, "Mises à jour des chunks par image", "Plus élevé : le terrain apparaît plus vite, au prix de saccades.", 1.f, 32.f,
					  float(m_draft.chunkUpdates), 1.f, [](float v) { return String::Format("%d", int(v)); }, SetInt(&Settings::chunkUpdates));
	}

	void Audio(ui::Widget c) {
		kit.SectionTitle(c, "Volumes", Some(MaterialIcons::MUSIC_NOTE));
		kit.SliderRow(c, "Volume général", "", 0.f, 1.f, m_draft.masterVolume, 0.01f, Percent, Set(&Settings::masterVolume));
		kit.SliderRow(c, "Musique", "Thèmes du menu et des biomes.", 0.f, 1.f, m_draft.musicVolume, 0.01f, Percent, Set(&Settings::musicVolume));
		kit.SliderRow(c, "Effets sonores", "Blocs, pas, combats, créatures.", 0.f, 1.f, m_draft.effectsVolume, 0.01f, Percent, Set(&Settings::effectsVolume));
		kit.SliderRow(c, "Ambiance", "Grottes, vent, grillons…", 0.f, 1.f, m_draft.ambienceVolume, 0.01f, Percent, Set(&Settings::ambienceVolume));
		kit.SliderRow(c, "Interface", "Clics, notifications.", 0.f, 1.f, m_draft.uiVolume, 0.01f, Percent, Set(&Settings::uiVolume));
		kit.SliderRow(c, "Voix", "Paroles des villageois et des gardes.", 0.f, 1.f, m_draft.voiceVolume, 0.01f, Percent, Set(&Settings::voiceVolume));
		kit.SectionTitle(c, "Avancé", Some(MaterialIcons::TUNE));
		kit.ToggleRow(c, "Son spatialisé", "Les sons viennent de la direction de leur source.", m_draft.spatialAudio, Set(&Settings::spatialAudio));
		kit.ToggleRow(c, "Couper le son hors de la fenêtre", "", m_draft.muteWhenUnfocused, Set(&Settings::muteWhenUnfocused));
		c.Append(kit.Button("Écouter un son d'essai", [this] { shell.UiSound("ui_confirm"); }));
	}

	void Gameplay(ui::Widget c) {
		const Palette &p = kit.Colors();
		kit.SectionTitle(c, "Caméra", Some(MaterialIcons::MOUSE));
		kit.ToggleRow(c, "Vue à la 3e personne douce", "Recul de la caméra limité par les murs.", m_draft.thirdPersonSmooth,
					  Set(&Settings::thirdPersonSmooth));
		kit.SectionTitle(c, "Interface en jeu (HUD)", Some(MaterialIcons::INFO));
		kit.SliderRow(c, "Opacité du HUD", "", 0.3f, 1.f, m_draft.hudOpacity, 0.05f, Percent, Set(&Settings::hudOpacity));
		kit.SliderRow(c, "Taille du HUD", "", 0.75f, 1.5f, m_draft.hudScale, 0.05f, Percent, Set(&Settings::hudScale));
		kit.ToggleRow(c, "Coordonnées", "", m_draft.showCoordinates, Set(&Settings::showCoordinates));
		kit.ToggleRow(c, "Images par seconde", "", m_draft.showFps, Set(&Settings::showFps));
		kit.ToggleRow(c, "Biome et mode de jeu", "", m_draft.showBiome, Set(&Settings::showBiome));
		kit.ToggleRow(c, "Heure du monde", "", m_draft.showClock, Set(&Settings::showClock));
		kit.ToggleRow(c, "Indications de commandes", "Rappels des touches selon ce que l'on vise.", m_draft.showHints, Set(&Settings::showHints));
		kit.SliderRow(c, "Opacité de la discussion", "", 0.f, 1.f, m_draft.chatOpacity, 0.05f, Percent, Set(&Settings::chatOpacity));
		kit.SectionTitle(c, "Interaction et construction", Some(MaterialIcons::BUILD));
		kit.ToggleRow(c, "Saut automatique", "Monte seul les marches d'un bloc.", m_draft.autoJump, Set(&Settings::autoJump));
		kit.ToggleRow(c, "Contour du bloc visé", "Aide à la pose et au minage.", m_draft.placeAssist, Set(&Settings::placeAssist));
		kit.ComboRow(c, "Réticule", "", {"Croix", "Point", "Cercle"}, m_draft.crosshair, SetChoice(&Settings::crosshair));
		// Réglages du monde en cours : distingués des préférences.
		if (Session *s = shell.Game(); s && s->Ready()) {
			kit.SectionTitle(c, "Monde en cours", Some(MaterialIcons::PUBLIC));
			WidgetBuilder banner = kit.Row(10.f);
			banner.GrowWidth().Padding(math::Sides(12.f, 8.f)).BorderRadius(8.f).Background(WithAlpha(p.info, 0.1f)).BorderColor(WithAlpha(p.info, 0.4f)).BorderWidth(1.f);
			ui::Widget bn = c.Append(banner);
			bn.Append(kit.Icon(MaterialIcons::PUBLIC, 18.f, Some(p.info)));
			bn.Append(kit.Paragraph("Ces réglages appartiennent au monde « " + s->Game().Metadata().name +
							  " » et s'appliquent immédiatement (ils sont enregistrés avec lui).",
						  Text::SMALL));
			voxel::Game &g = s->Game();
			kit.ComboRow(c, "Difficulté (monde)", "", {"Paisible", "Facile", "Normale", "Difficile"}, int(g.GetDifficulty()),
						 [&g](int i) { g.SetDifficulty(voxel::Difficulty(i)); });
			kit.ToggleRow(c, "Faim (monde)", "", g.Rule("hunger"), [&g](bool v) { g.SetRule("hunger", v); });
			kit.ToggleRow(c, "Conserver l'inventaire (monde)", "", g.Rule("keepInventory"), [&g](bool v) { g.SetRule("keepInventory", v); });
			c.Append(kit.Button("Tous les paramètres du monde…", [this] { shell.Push(MakeWorldSettings(shell, String())); }));
		}
	}

	void Accessibility(ui::Widget c) {
		kit.SectionTitle(c, "Lisibilité", Some(MaterialIcons::ACCESSIBILITY));
		kit.SliderRow(c, "Échelle de l'interface", "", 0.75f, 1.5f, m_draft.uiScale, 0.05f, Percent, Set(&Settings::uiScale));
		kit.SliderRow(c, "Taille du texte", "", 0.8f, 1.6f, m_draft.textScale, 0.05f, Percent, Set(&Settings::textScale));
		kit.ToggleRow(c, "Contraste élevé", "Fonds plus sombres, textes et bordures plus marqués.", m_draft.highContrast, Set(&Settings::highContrast));
		kit.ComboRow(c, "Mode daltonien", "Adapte les couleurs d'action, de danger et de succès.",
					 {"Aucun", "Protanopie", "Deutéranopie", "Tritanopie"}, m_draft.colorBlind, SetChoice(&Settings::colorBlind));
		kit.SliderRow(c, "Fond des textes du HUD", "Opacité derrière les informations en jeu.", 0.f, 0.9f, m_draft.hudBackground, 0.05f, Percent,
					  Set(&Settings::hudBackground));
		kit.SectionTitle(c, "Sons", Some(MaterialIcons::MUSIC_NOTE));
		kit.ToggleRow(c, "Sous-titres des sons", "Les sons importants (créatures, explosions) sont écrits dans la discussion.", m_draft.subtitles,
					  Set(&Settings::subtitles));
		kit.SectionTitle(c, "Confort visuel", Some(MaterialIcons::VISIBILITY));
		kit.ToggleRow(c, "Réduire les animations", "Fond des menus immobile, pas de balancement ni d'effet de vitesse.", m_draft.reduceMotion,
					  Set(&Settings::reduceMotion));
		kit.ToggleRow(c, "Réduire les flashs", "Atténue l'éclair rouge des blessures.", m_draft.reduceFlashes, Set(&Settings::reduceFlashes));
		kit.SectionTitle(c, "Indicateurs visuels", Some(MaterialIcons::ADD));
		kit.ComboRow(c, "Couleur du réticule", "", {"Blanc", "Vert", "Jaune", "Rouge", "Cyan"}, m_draft.crosshairColor,
					 SetChoice(&Settings::crosshairColor));
		kit.SliderRow(c, "Taille du réticule", "", 0.5f, 2.5f, m_draft.crosshairSize, 0.1f, Percent, Set(&Settings::crosshairSize));
	}

	void ResetTab() {
		Settings d;
		d.ResetBindings();
		switch (m_tab) {
		case 0:
			m_draft.uiScale = d.uiScale, m_draft.showTips = d.showTips, m_draft.notifications = d.notifications;
			m_draft.notificationSeconds = d.notificationSeconds, m_draft.confirmQuit = d.confirmQuit, m_draft.confirmDelete = d.confirmDelete;
			m_draft.confirmModChanges = d.confirmModChanges, m_draft.autosaveMinutes = d.autosaveMinutes, m_draft.pauseOnFocusLoss = d.pauseOnFocusLoss;
			break;
		case 1:
			m_draft.bindings = d.bindings, m_draft.mouseSensitivity = d.mouseSensitivity, m_draft.invertY = d.invertY, m_draft.fov = d.fov;
			m_draft.viewBobbing = d.viewBobbing, m_draft.toggleSprint = d.toggleSprint, m_draft.toggleSneak = d.toggleSneak;
			m_draft.allowDualBinding = d.allowDualBinding;
			break;
		case 2:
			m_draft.displayMode = d.displayMode, m_draft.vsync = d.vsync, m_draft.fpsLimit = d.fpsLimit, m_draft.viewDistance = d.viewDistance;
			m_draft.shadows = d.shadows, m_draft.dynamicLights = d.dynamicLights, m_draft.brightness = d.brightness;
			m_draft.ambientOcclusion = d.ambientOcclusion, m_draft.clouds = d.clouds, m_draft.vignette = d.vignette;
			m_draft.screenEffects = d.screenEffects, m_draft.particles = d.particles, m_draft.vegetationDensity = d.vegetationDensity;
			m_draft.entityDistance = d.entityDistance, m_draft.chunkUpdates = d.chunkUpdates, m_draft.smoothTextures = d.smoothTextures;
			break;
		case 3:
			m_draft.masterVolume = d.masterVolume, m_draft.musicVolume = d.musicVolume, m_draft.effectsVolume = d.effectsVolume;
			m_draft.ambienceVolume = d.ambienceVolume, m_draft.uiVolume = d.uiVolume, m_draft.voiceVolume = d.voiceVolume;
			m_draft.spatialAudio = d.spatialAudio, m_draft.muteWhenUnfocused = d.muteWhenUnfocused;
			break;
		case 4:
			m_draft.hudOpacity = d.hudOpacity, m_draft.hudScale = d.hudScale, m_draft.showCoordinates = d.showCoordinates;
			m_draft.showFps = d.showFps, m_draft.showBiome = d.showBiome, m_draft.showClock = d.showClock, m_draft.showHints = d.showHints;
			m_draft.chatOpacity = d.chatOpacity, m_draft.autoJump = d.autoJump, m_draft.placeAssist = d.placeAssist, m_draft.crosshair = d.crosshair;
			break;
		default:
			m_draft.uiScale = d.uiScale, m_draft.textScale = d.textScale, m_draft.highContrast = d.highContrast, m_draft.colorBlind = d.colorBlind;
			m_draft.hudBackground = d.hudBackground, m_draft.subtitles = d.subtitles, m_draft.reduceMotion = d.reduceMotion;
			m_draft.reduceFlashes = d.reduceFlashes, m_draft.crosshairColor = d.crosshairColor, m_draft.crosshairSize = d.crosshairSize;
			break;
		}
		shell.Rebuild();
	}

	void Apply() {
		const Settings before = shell.Prefs();
		const bool display = before.displayMode != m_draft.displayMode || before.width != m_draft.width || before.height != m_draft.height;
		shell.Prefs() = m_draft;
		shell.ApplySettings(true);
		shell.UiSound("ui_save");
		if (display) {
			// Changement d'affichage : à confirmer, sinon retour automatique.
			ConfirmSpec spec;
			spec.title = "Conserver ces paramètres d'affichage ?";
			spec.message = "Si l'image est illisible, appuyez sur Échap : l'affichage précédent sera rétabli.";
			spec.confirmText = "Conserver";
			spec.cancelText = "Rétablir";
			spec.tone = Tone::PRIMARY;
			spec.icon = MaterialIcons::DISPLAY_SETTINGS;
			spec.onCancel = [this, before] {
				shell.Prefs().displayMode = before.displayMode;
				shell.Prefs().width = before.width;
				shell.Prefs().height = before.height;
				m_draft = shell.Prefs();
				shell.ApplySettings(true);
			};
			shell.Confirm(std::move(spec));
		} else {
			shell.Notify(Tone::SUCCESS, "Paramètres appliqués");
		}
	}

	void Leave() {
		if (json::Encode(m_draft.ToJson()) == json::Encode(shell.Prefs().ToJson())) {
			shell.Pop();
			return;
		}
		ConfirmSpec spec;
		spec.title = "Appliquer les modifications ?";
		spec.message = "Des réglages ont été modifiés sans être appliqués.";
		spec.confirmText = "Appliquer";
		spec.cancelText = "Abandonner";
		spec.tone = Tone::PRIMARY;
		spec.icon = MaterialIcons::SETTINGS;
		spec.onConfirm = [this] {
			Apply();
			shell.Pop();
		};
		spec.onCancel = [this] { shell.Pop(); };
		shell.Confirm(std::move(spec));
	}

	int m_tab = 0;
	Settings m_draft;
	ui::Widget m_status{};
};

// ── Saisie d'une touche ──────────────────────────────────────────────────────

class KeyCapture final : public Screen {
public:
	KeyCapture(Shell &shell_, String label, std::function<void(int)> done) : Screen(shell_), m_label(std::move(label)), m_done(std::move(done)) {}
	String Id() const override { return "keycapture"; }
	bool Overlay() const override { return true; }

	void Build(ui::Widget root) override {
		const Palette &p = kit.Colors();
		ui::Widget card = ModalCard(kit, root, 440.f);
		WidgetBuilder col = kit.Column(12.f);
		col.GrowWidth().AlignItems(ui::Align::Center);
		ui::Widget c = card.Append(col);
		c.Append(kit.Icon(MaterialIcons::KEYBOARD, 40.f, Some(p.primary)));
		c.Append(kit.Label(String::Format("Nouvelle touche pour « %s »", m_label.CStr()), Text::H3));
		WidgetBuilder key = ui::Column();
		key.Size(220.f, 56.f).BorderRadius(10.f).Background(p.bg0).BorderColor(p.primary).BorderWidth(2.f).JustifyContent(ui::Justify::Center).AlignItems(ui::Align::Center);
		ui::Widget k = c.Append(key);
		k.Append(kit.Label("Appuyez sur une touche…", Text::BODY).Color(p.textMuted));
		c.Append(kit.Muted("Clavier, bouton de souris ou molette. Échap : annuler — Retour arrière : aucune touche.", Text::CAPTION));
	}
	bool OnKey(int code) override {
		if (code == SDL_SCANCODE_ESCAPE) {
			shell.Pop();
			return true;
		}
		const int value = code == SDL_SCANCODE_BACKSPACE ? 0 : code;
		auto done = m_done;
		shell.UiSound("ui_confirm");
		shell.Pop();
		if (done)
			done(value);
		return true;
	}

private:
	String m_label;
	std::function<void(int)> m_done;
};

// ── Choix du skin ────────────────────────────────────────────────────────────

class SkinPicker final : public Screen {
public:
	explicit SkinPicker(Shell &shell_) : Screen(shell_) {
		m_selected = shell.Prefs().skin;
		if (voxel::Game *g = ContentGame(shell)) {
			if (m_selected.IsEmpty())
				m_selected = g->Content().ConfigStr("player.skin");
			for (const String &s : json::StrList(g->Content().Config(), "skins.featured"))
				m_skins.push_back(s);
			const String folder = json::Str(g->Content().Config(), "skins.folder", "core:skins/");
			std::vector<String> numbered;
			for (const voxel::TextureDef *t : g->Content().Textures())
				if (t->id.StartsWith(folder.CStr()) && std::find(m_skins.begin(), m_skins.end(), t->id) == m_skins.end() &&
					!t->id.Contains("mask") && !t->id.Contains("button") && !t->id.Contains("transform"))
					numbered.push_back(t->id);
			std::sort(numbered.begin(), numbered.end(), [](const String &a, const String &b) {
				const long na = std::atol(a.CStr() + a.Find(".") + 1), nb = std::atol(b.CStr() + b.Find(".") + 1);
				return na != nb ? na < nb : a < b;
			});
			m_skins.insert(m_skins.end(), numbered.begin(), numbered.end());
		}
	}
	String Id() const override { return "skins"; }

	void Build(ui::Widget root) override {
		const Palette &p = kit.Colors();
		const float s = kit.Scale();
		ui::Widget page = Page(kit, root, 1180.f);
		kit.ScreenHeader(page, "Skin du personnage", String::Format("%d skins disponibles (textures/skins du mod core)", int(m_skins.size())),
						 [this] { shell.Pop(); });
		WidgetBuilder body = kit.Row(20.f);
		body.GrowWidth().GrowHeight().AlignItems(ui::Align::Stretch);
		ui::Widget b = page.Append(body);
		// Aperçu en grand.
		WidgetBuilder preview = kit.Card(18.f);
		preview.Width(280.f * s).AlignItems(ui::Align::Center);
		ui::Widget pv = b.Append(preview);
		pv.Append(SkinFront(shell, m_selected, 320.f * s));
		pv.Append(kit.Label(m_selected.Substring(m_selected.Find(":") + 1), Text::SMALL));
		pv.Append(kit.PrimaryButton("Utiliser ce skin", [this] { Apply(); }));
		// Grille paginée.
		WidgetBuilder right = kit.Column(10.f);
		right.GrowWidth().GrowHeight();
		ui::Widget r = b.Append(right);
		WidgetBuilder tools = kit.Row(10.f);
		tools.GrowWidth();
		ui::Widget t = r.Append(tools);
		t.Append(kit.Icon(MaterialIcons::SEARCH, 20.f));
		WidgetBuilder search = kit.Input("Numéro ou nom (prince, rogue…)");
		search.OnTextChange([this](const String &q) {
			m_query = q.ToLower();
			m_page = 0;
			Refresh();
		});
		ui::Widget se = t.Append(search);
		kit.SetInputText(se, m_query);
		t.Append(kit.IconButton(MaterialIcons::ARROW_BACK, "Page précédente", [this] {
			m_page = std::max(0, m_page - 1);
			Refresh();
		}));
		m_pageLabel = t.Append(kit.Label(String(), Text::SMALL));
		t.Append(kit.IconButton(MaterialIcons::ARROW_FORWARD, "Page suivante", [this] {
			++m_page;
			Refresh();
		}));
		WidgetBuilder grid = kit.Column(8.f);
		grid.GrowWidth().GrowHeight();
		m_grid = r.Append(grid);
		Refresh();
		(void)p;
	}

private:
	void Refresh() {
		const Palette &p = kit.Colors();
		kit.ClearChildren(m_grid);
		std::vector<const String *> shown;
		for (const String &id : m_skins)
			if (m_query.IsEmpty() || id.ToLower().Contains(m_query))
				shown.push_back(&id);
		const int perPage = 30, pages = std::max(1, int((shown.size() + perPage - 1) / perPage));
		m_page = std::clamp(m_page, 0, pages - 1);
		kit.SetText(m_pageLabel, String::Format("Page %d / %d", m_page + 1, pages));
		ui::Widget row{};
		for (int i = 0; i < perPage && size_t(m_page * perPage + i) < shown.size(); ++i) {
			if (i % 10 == 0) {
				WidgetBuilder rw = kit.Row(8.f);
				rw.GrowWidth();
				row = m_grid.Append(rw);
			}
			const String id = *shown[size_t(m_page * perPage + i)];
			const bool on = id == m_selected;
			WidgetBuilder cell = ui::Column();
			cell.Size(70.f, 120.f).BorderRadius(8.f).Padding(4.f).AlignItems(ui::Align::Center).JustifyContent(ui::Justify::Center);
			cell.Background(on ? WithAlpha(p.primary, 0.25f) : p.bg2).HoverStyle(ui::Style().Background(p.bg3));
			cell.BorderColor(on ? p.primary : p.line).BorderWidth(on ? 2.f : 1.f).Tooltip(id).OnClick([this, id] {
				m_selected = id;
				shell.UiSound("ui_click");
				shell.Rebuild();
			});
			ui::Widget c = row.Append(cell);
			c.Append(SkinFront(shell, id, 108.f));
		}
	}
	void Apply() {
		shell.Prefs().skin = m_selected;
		shell.SaveSettings();
		if (Session *s = shell.Game(); s && s->Ready())
			if (auto v = s->Game().Registry().GetComponent<voxel::Visual>(s->Player()); v.IsSome())
				v.Unwrap()->skin = m_selected;
		shell.Notify(Tone::SUCCESS, "Skin appliqué", m_selected);
		shell.Pop();
	}
	std::vector<String> m_skins;
	String m_selected, m_query;
	int m_page = 0;
	ui::Widget m_grid{}, m_pageLabel{};
};

} // namespace

std::unique_ptr<Screen> MakeSettings(Shell &shell, SettingsTab tab) { return std::make_unique<SettingsScreen>(shell, tab); }
std::unique_ptr<Screen> MakeKeyCapture(Shell &shell, String label, std::function<void(int)> done) {
	return std::make_unique<KeyCapture>(shell, std::move(label), std::move(done));
}
std::unique_ptr<Screen> MakeSkinPicker(Shell &shell) { return std::make_unique<SkinPicker>(shell); }

} // namespace voxelia
