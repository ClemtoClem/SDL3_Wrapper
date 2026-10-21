// Écrans des menus : accueil et gestion des mondes.
#include "common.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace voxelgame {

using namespace uiutil;

namespace {

// ── Accueil ──────────────────────────────────────────────────────────────────

class MainMenu final : public Screen {
public:
	explicit MainMenu(Shell &shell_) : Screen(shell_) {}
	String Id() const override { return "mainmenu"; }

	void Build(ecs::Entity root) override {
		const Palette &p = kit.Colors();
		const float s = kit.Scale();
		shell.Mods().Refresh();
		const std::vector<WorldEntry> worlds = shell.Worlds().List(shell.Mods().Installed());
		// Colonne gauche : logo et actions, sur un dégradé sombre.
		WidgetBuilder side = f.Column();
		side.Absolute().Anchor(ui::Anchor::TopLeft).W(Dimension::Px(470.f * s)).H(Dimension::Rpct(100.f));
		side.Pad(math::Sides(48.f * s, 54.f * s, 40.f * s, 32.f * s)).Gap(10.f * s).Bg(WithAlpha(p.bg0, 0.82f)).Parent(root);
		const ecs::Entity col = side.Spawn();
		kit.PixelTitle("VOXELIA", 50.f, p.text).Parent(col).Spawn();
		WidgetBuilder tagline = kit.Row(8.f);
		tagline.Parent(col);
		const ecs::Entity tl = tagline.Spawn();
		WidgetBuilder bar = f.Column();
		bar.Size(28.f * s, 4.f * s).Bg(p.primary).Radius(2.f).Parent(tl).Spawn();
		kit.Muted("Construire, explorer, survivre", Text::SMALL).Parent(tl).Spawn();
		WidgetBuilder gap = f.Column();
		gap.Size(10.f, 28.f * s).Parent(col).Spawn();
		// Jouer : reprend le dernier monde, sinon en crée un.
		if (!worlds.empty()) {
			const WorldEntry &last = worlds.front();
			kit.MenuButton(col, MaterialIcons::PLAY_ARROW, "Jouer",
						   String::Format("Continuer « %s » — %s, %s", last.name.CStr(), GameModeName(last.gameMode).CStr(),
										  FormatDate(last.lastPlayed).CStr()),
						   [this, folder = last.folder] {
							   shell.UiSound("ui_confirm");
							   shell.PlayWorld(folder);
						   },
						   true);
		} else {
			kit.MenuButton(col, MaterialIcons::PLAY_ARROW, "Jouer", "Créer votre premier monde", [this] {
				shell.UiSound("ui_confirm");
				shell.Push(MakeCreateWorld(shell));
			}, true);
		}
		kit.MenuButton(col, MaterialIcons::ADD, "Nouveau monde", "Choisir le mode, la génération et les mods", [this] {
			shell.UiSound("ui_click");
			shell.Push(MakeCreateWorld(shell));
		});
		kit.MenuButton(col, MaterialIcons::PUBLIC, "Mondes", String::Format("%d monde(s) enregistré(s)", int(worlds.size())), [this] {
			shell.UiSound("ui_click");
			shell.Push(MakeWorldSelect(shell));
		});
		int updates = 0, installed = 0;
		for (const ModEntry &m : shell.Mods().Entries()) {
			updates += m.UpdateAvailable();
			installed += m.installed;
		}
		kit.MenuButton(col, MaterialIcons::EXTENSION, "Mods",
					   updates > 0 ? String::Format("%d installé(s) — %d mise(s) à jour disponible(s)", installed, updates)
								   : String::Format("%d installé(s) — parcourir le catalogue", installed),
					   [this] {
						   shell.UiSound("ui_click");
						   shell.Push(MakeModBrowser(shell));
					   });
		kit.MenuButton(col, MaterialIcons::SETTINGS, "Paramètres", "Graphismes, commandes, audio, accessibilité", [this] {
			shell.UiSound("ui_click");
			shell.Push(MakeSettings(shell));
		});
		kit.Spacer().GrowH().Parent(col).Spawn();
		kit.Button("Quitter le jeu", [this] { shell.Quit(); }).Parent(col).Spawn();
		kit.Muted(String::Format("Voxelia %s · moteur voxel ECS · %d mod(s) · SDL3", shell.Version().CStr(), installed), Text::CAPTION)
			.Parent(col)
			.Spawn();
		// En haut à droite : profil (nom et skin).
		WidgetBuilder profile = kit.Row(10.f);
		profile.Absolute().Anchor(ui::Anchor::TopRight).Offset(-24.f * s, 24.f * s).Pad(math::Sides(12.f * s, 8.f * s)).Radius(10.f * s);
		profile.Bg(WithAlpha(p.bg1, 0.88f)).BorderColor(p.line, 1.f).Parent(root);
		profile.OnClick([this] { shell.Push(MakeSkinPicker(shell)); });
		ui::UiStyle st;
		st.SetBg(WithAlpha(p.bg1, 0.88f)).SetBgHovered(p.bg3);
		profile.Bg(sdl3::FColor{}).Style(std::move(st));
		const ecs::Entity pr = profile.Spawn();
		String skin = shell.Prefs().skin;
		if (skin.IsEmpty() && shell.MenuGame())
			skin = shell.MenuGame()->Content().ConfigStr("player.skin");
		SkinFront(shell, skin, 56.f * s).Parent(pr).Spawn();
		WidgetBuilder who = kit.Column(2.f);
		who.PointerThrough().Parent(pr);
		const ecs::Entity w = who.Spawn();
		kit.Label(shell.Prefs().playerName, Text::H3).PointerThrough().Parent(w).Spawn();
		kit.Muted("Changer de skin", Text::CAPTION).PointerThrough().Parent(w).Spawn();
		// En bas à droite : astuce du jour.
		if (shell.Prefs().showTips) {
			const auto &tips = Tips();
			const auto day = std::chrono::duration_cast<std::chrono::hours>(std::chrono::system_clock::now().time_since_epoch()).count();
			WidgetBuilder tip = kit.Row(12.f);
			tip.Absolute().Anchor(ui::Anchor::BottomRight).Offset(-24.f * s, -24.f * s).W(Dimension::Px(400.f * s)).HAuto();
			tip.Pad(math::Sides(14.f * s, 12.f * s)).Radius(10.f * s).Bg(WithAlpha(p.bg1, 0.88f)).BorderColor(p.line, 1.f);
			tip.Align(ui::CrossAlign::Start).Parent(root);
			const ecs::Entity t = tip.Spawn();
			kit.Icon(MaterialIcons::LIGHTBULB, 22.f, Some(p.accent)).Parent(t).Spawn();
			WidgetBuilder tc = kit.Column(3.f);
			tc.GrowW().Parent(t);
			const ecs::Entity tcc = tc.Spawn();
			kit.Label("Astuce", Text::SMALL).TextColor(p.accent).Bold().Parent(tcc).Spawn();
			kit.Paragraph(tips[size_t(day) % tips.size()], Text::SMALL).Parent(tcc).Spawn();
		}
	}
	bool OnBack() override { return true; }
};

// ── Sélection des mondes ─────────────────────────────────────────────────────

class WorldSelect final : public Screen {
public:
	explicit WorldSelect(Shell &shell_) : Screen(shell_) {}
	String Id() const override { return "worlds"; }

	void Build(ecs::Entity root) override {
		const float s = kit.Scale();
		m_worlds = shell.Worlds().List(shell.Mods().Installed());
		const ecs::Entity page = Page(kit, root);
		const ecs::Entity header = kit.ScreenHeader(page, "Mondes",
													String::Format("%d monde(s) — %s", int(m_worlds.size()), shell.Worlds().Directory().CStr()),
													[this] { shell.Pop(); });
		kit.PrimaryButton("Nouveau monde", [this] { shell.Push(MakeCreateWorld(shell)); }).Parent(header).Spawn();
		// Recherche, tri, filtre.
		WidgetBuilder tools = kit.Row(10.f);
		tools.GrowW().Parent(page);
		const ecs::Entity t = tools.Spawn();
		kit.Icon(MaterialIcons::SEARCH, 20.f).Parent(t).Spawn();
		WidgetBuilder search = kit.Input("Rechercher un monde (nom, description, mode)…");
		search.OnTextChange([this](const String &text) {
			m_query = text.ToLower();
			Refresh();
		});
		m_search = search.Parent(t).Spawn();
		kit.SetInputText(m_search, m_query);
		kit.Muted("Trier :", Text::SMALL).Parent(t).Spawn();
		kit.Combo({"Dernière session", "Nom", "Temps de jeu", "Taille"}, m_sort, [this](int i) {
			m_sort = i;
			Refresh();
		}).Parent(t).Spawn();
		kit.Combo({"Tous les modes", "Survie", "Créatif", "Aventure", "Spectateur"}, m_mode, [this](int i) {
			m_mode = i;
			Refresh();
		}).Parent(t).Spawn();
		WidgetBuilder list = kit.Scroll(10.f);
		list.GrowW().GrowH().Parent(page);
		m_list = list.Spawn();
		Refresh();
		(void)s;
	}

private:
	void Refresh() {
		kit.ClearChildren(m_list);
		std::vector<const WorldEntry *> shown;
		static const char *modes[] = {"", "survival", "creative", "adventure", "spectator"};
		for (const WorldEntry &w : m_worlds) {
			if (m_mode > 0 && w.gameMode != modes[m_mode])
				continue;
			if (!m_query.IsEmpty() && !w.name.ToLower().Contains(m_query) && !w.description.ToLower().Contains(m_query) &&
				!GameModeName(w.gameMode).ToLower().Contains(m_query))
				continue;
			shown.push_back(&w);
		}
		std::sort(shown.begin(), shown.end(), [this](const WorldEntry *a, const WorldEntry *b) {
			switch (m_sort) {
			case 1: return a->name.ToLower() < b->name.ToLower();
			case 2: return a->playTime > b->playTime;
			case 3: return a->sizeBytes > b->sizeBytes;
			default: return a->lastPlayed > b->lastPlayed;
			}
		});
		if (shown.empty()) {
			WidgetBuilder empty = kit.Card(32.f);
			empty.GrowW().Align(ui::CrossAlign::Center).Parent(m_list);
			const ecs::Entity e = empty.Spawn();
			kit.Icon(MaterialIcons::PUBLIC, 40.f).Parent(e).Spawn();
			kit.Label(m_worlds.empty() ? "Aucun monde pour l'instant" : "Aucun monde ne correspond à la recherche", Text::H3).Parent(e).Spawn();
			if (m_worlds.empty())
				kit.PrimaryButton("Créer un monde", [this] { shell.Push(MakeCreateWorld(shell)); }).Parent(e).Spawn();
			return;
		}
		for (const WorldEntry *w : shown)
			Card(*w);
	}

	void Card(const WorldEntry &w) {
		const Palette &p = kit.Colors();
		const float s = kit.Scale();
		WidgetBuilder card = kit.Row(16.f);
		card.GrowW().Pad(math::Sides(12.f * s, 12.f * s)).Radius(10.f * s).Align(ui::CrossAlign::Center).Parent(m_list);
		ui::UiStyle st;
		st.SetBg(p.bg2).SetBgHovered(p.bg3).SetBorderColor(p.line).SetBorderHovered(p.lineStrong).SetBordersWidth(1.f);
		card.Bg(sdl3::FColor{}).Style(std::move(st)).OnDoubleClick([this, folder = w.folder] { shell.PlayWorld(folder); });
		const ecs::Entity c = card.Spawn();
		// Vignette.
		const String thumb = ImageKey(shell, w.thumbnail);
		if (!thumb.IsEmpty()) {
			WidgetBuilder img = f.Image(thumb, 176.f * s, 99.f * s);
			img.Fit(ui::ImageFit::COVER).Radius(6.f).PointerThrough().Parent(c).Spawn();
		} else {
			WidgetBuilder ph = f.Column();
			ph.Size(176.f * s, 99.f * s).Radius(6.f).Bg(p.bg1).Justify(ui::Justify::Center).Align(ui::CrossAlign::Center).PointerThrough();
			ph.Parent(c);
			const ecs::Entity e = ph.Spawn();
			kit.Icon(MaterialIcons::TERRAIN, 34.f, Some(p.textDim)).Parent(e).Spawn();
		}
		// Informations.
		WidgetBuilder info = kit.Column(5.f);
		info.GrowW().PointerThrough().Parent(c);
		const ecs::Entity i = info.Spawn();
		WidgetBuilder title = kit.Row(8.f);
		title.Parent(i);
		const ecs::Entity tr = title.Spawn();
		kit.Label(w.name, Text::H3).Parent(tr).Spawn();
		kit.Chip(GameModeName(w.gameMode), w.gameMode == "creative" ? Tone::INFO : Tone::PRIMARY).Parent(tr).Spawn();
		kit.Chip(DifficultyName(w.difficulty), w.difficulty == "hard" ? Tone::DANGER : w.difficulty == "peaceful" ? Tone::SUCCESS : Tone::NEUTRAL)
			.Parent(tr)
			.Spawn();
		if (!w.description.IsEmpty())
			kit.Paragraph(w.description, Text::SMALL).Parent(i).Spawn();
		kit.Muted(String::Format("Dernière session : %s   ·   Temps de jeu : %s   ·   %s   ·   Graine %u", FormatDate(w.lastPlayed).CStr(),
								 FormatDuration(w.playTime).CStr(), FormatBytes(w.sizeBytes).CStr(), w.seed),
				  Text::CAPTION)
			.Parent(i)
			.Spawn();
		// Compatibilité.
		WidgetBuilder compat = kit.Row(6.f);
		compat.Parent(i);
		const ecs::Entity cr = compat.Spawn();
		int missing = 0, changed = 0, newer = 0;
		for (const WorldIssue &issue : w.issues) {
			missing += issue.kind == WorldIssue::Kind::MISSING_MOD;
			changed += issue.kind == WorldIssue::Kind::VERSION_CHANGED;
			newer += issue.kind == WorldIssue::Kind::NEWER_SAVE;
		}
		if (w.issues.empty())
			kit.Chip("Compatible", Tone::SUCCESS).Parent(cr).Spawn();
		if (missing)
			kit.Chip(String::Format("%d mod(s) manquant(s)", missing), Tone::DANGER).Parent(cr).Spawn();
		if (changed)
			kit.Chip(String::Format("%d mod(s) mis à jour depuis", changed), Tone::WARNING).Parent(cr).Spawn();
		if (newer)
			kit.Chip("Sauvegarde plus récente que le jeu", Tone::DANGER).Parent(cr).Spawn();
		kit.Chip(String::Format("%d mod(s)", int(w.enabledMods.empty() ? w.mods.size() : w.enabledMods.size())), Tone::NEUTRAL)
			.Parent(cr)
			.Spawn();
		// Actions.
		WidgetBuilder actions = kit.Row(4.f);
		actions.Parent(c);
		const ecs::Entity a = actions.Spawn();
		kit.IconButton(MaterialIcons::TUNE, "Paramètres du monde", [this, folder = w.folder] { shell.Push(MakeWorldSettings(shell, folder)); })
			.Parent(a)
			.Spawn();
		kit.IconButton(MaterialIcons::EXTENSION, "Mods du monde", [this, folder = w.folder] { shell.Push(MakeWorldMods(shell, folder)); })
			.Parent(a)
			.Spawn();
		kit.IconButton(MaterialIcons::EDIT, "Renommer", [this, folder = w.folder] { shell.Push(MakeRenameWorld(shell, folder)); })
			.Parent(a)
			.Spawn();
		kit.IconButton(MaterialIcons::CONTENT_COPY, "Dupliquer", [this, folder = w.folder] {
			Result<String, String> r = shell.Worlds().Duplicate(folder);
			if (r.IsOk())
				shell.Notify(Tone::SUCCESS, "Monde dupliqué", r.Value());
			else
				shell.Notify(Tone::DANGER, "Duplication impossible", r.Error(), true);
			shell.Rebuild();
		}).Parent(a).Spawn();
		kit.IconButton(MaterialIcons::DELETE, "Supprimer", [this, w] { AskDelete(w); }, Tone::DANGER).Parent(a).Spawn();
		WidgetBuilder play = kit.PrimaryButton("Jouer", [this, folder = w.folder] {
			shell.UiSound("ui_confirm");
			shell.PlayWorld(folder);
		});
		if (w.issues.size() > 0 && missing + newer > 0)
			play.Tooltip("Des mods manquent : le monde s'ouvrira sans eux (blocs inconnus remplacés).");
		play.Parent(a).Spawn();
	}

	void AskDelete(const WorldEntry &w) {
		auto remove = [this, folder = w.folder, name = w.name](bool backup) {
			if (backup) {
				Result<String, String> b = shell.Worlds().Backup(folder);
				if (b.IsError()) {
					shell.Notify(Tone::DANGER, "Sauvegarde de sécurité impossible", b.Error(), true);
					return;
				}
			}
			Result<bool, String> r = shell.Worlds().Remove(folder);
			if (r.IsOk())
				shell.Notify(Tone::SUCCESS, "Monde supprimé", name);
			else
				shell.Notify(Tone::DANGER, "Suppression impossible", r.Error(), true);
			shell.Rebuild();
		};
		if (!shell.Prefs().confirmDelete) {
			remove(false);
			return;
		}
		auto backup = std::make_shared<bool>(true);
		ConfirmSpec spec;
		spec.title = String::Format("Supprimer « %s » ?", w.name.CStr());
		spec.message = "Le monde, ses constructions, ses inventaires et ses réglages seront définitivement effacés.";
		spec.details = {String::Format("Taille : %s — temps de jeu : %s", FormatBytes(w.sizeBytes).CStr(), FormatDuration(w.playTime).CStr()),
						"Cette action est irréversible"};
		spec.confirmText = "Supprimer définitivement";
		spec.tone = Tone::DANGER;
		spec.icon = MaterialIcons::DELETE;
		spec.checkboxText = "Créer d'abord une sauvegarde de sécurité (dossier backups)";
		spec.checkboxValue = true;
		spec.onCheckbox = [backup](bool v) { *backup = v; };
		spec.onConfirm = [remove, backup] { remove(*backup); };
		shell.Confirm(std::move(spec));
	}

	std::vector<WorldEntry> m_worlds;
	ecs::Entity m_list{}, m_search{};
	String m_query;
	int m_sort = 0, m_mode = 0;
};

// ── Paramètres communs : création et configuration ──────────────────────────

/// Réglages de monde en cours d'édition (copie de world.json « settings »).
struct WorldForm {
	String name, description, seed, generator;
	String gameMode = "survival", difficulty = "normal";
	std::map<std::string, bool> rules;
	double dayLength = 600.0;
	float relief = 1.f, caves = 1.f, ores = 1.f, vegetation = 1.f;
	bool structures = true;
	int seaLevel = 0;
	std::vector<String> mods;

	void From(const dataio::NodePtr &s) {
		description = json::Str(s, "description");
		gameMode = json::Str(s, "gameMode", "survival");
		difficulty = json::Str(s, "difficulty", "normal");
		for (const auto &[rule, value] : voxel::Game::DefaultRules())
			rules[rule] = json::Bool(s, (String("rules.") + rule).CStr(), value);
		dayLength = json::Num(s, "dayLength", 600.0);
		relief = float(json::Num(s, "generation.relief", 1.0));
		caves = float(json::Num(s, "generation.caves", 1.0));
		ores = float(json::Num(s, "generation.ores", 1.0));
		vegetation = float(json::Num(s, "generation.vegetation", 1.0));
		structures = json::Bool(s, "generation.structures", true);
		seaLevel = json::Int(s, "generation.seaLevel", 0);
		mods = json::StrList(s, "mods.enabled");
	}
	dataio::NodePtr To(dataio::NodePtr base) const {
		dataio::NodePtr s = base ? base->Clone() : dataio::Node::MakeObject();
		s->Set("description", json::MakeStr(description));
		s->Set("gameMode", json::MakeStr(gameMode));
		s->Set("difficulty", json::MakeStr(difficulty));
		s->Set("dayLength", json::MakeNum(dayLength));
		dataio::NodePtr r = dataio::Node::MakeObject();
		for (const auto &[rule, value] : rules)
			r->Set(String(rule.c_str()), json::MakeBool(value));
		s->Set("rules", r);
		dataio::NodePtr g = dataio::Node::MakeObject();
		g->Set("relief", json::MakeNum(relief));
		g->Set("caves", json::MakeNum(caves));
		g->Set("ores", json::MakeNum(ores));
		g->Set("vegetation", json::MakeNum(vegetation));
		g->Set("structures", json::MakeBool(structures));
		g->Set("seaLevel", json::MakeInt(seaLevel));
		s->Set("generation", g);
		dataio::NodePtr m = json::At(s, "mods");
		if (!m || !m->IsObject()) {
			m = dataio::Node::MakeObject();
			s->Set("mods", m);
		}
		dataio::NodePtr list = dataio::Node::MakeArray();
		for (const String &id : mods)
			list->Push(json::MakeStr(id));
		m->Set("enabled", list);
		return s;
	}
};

const std::vector<String> MODE_IDS{"survival", "creative", "adventure", "spectator"};
const std::vector<String> MODE_NAMES{"Survie", "Créatif", "Aventure", "Spectateur"};
const std::vector<String> DIFF_IDS{"peaceful", "easy", "normal", "hard"};
const std::vector<String> DIFF_NAMES{"Paisible", "Facile", "Normale", "Difficile"};

int IndexOf(const std::vector<String> &list, const String &v, int fallback = 0) {
	for (size_t i = 0; i < list.size(); ++i)
		if (list[i] == v)
			return int(i);
	return fallback;
}

/// Libellés et descriptions des règles.
const std::vector<std::tuple<const char *, const char *, const char *>> &RuleTexts() {
	static const std::vector<std::tuple<const char *, const char *, const char *>> list{
		{"daylightCycle", "Cycle jour / nuit", "Le soleil avance ; désactivé, l'heure reste figée."},
		{"mobSpawning", "Apparition des créatures", "Animaux, monstres et villageois apparaissent naturellement."},
		{"keepInventory", "Conserver l'inventaire", "À la mort, l'inventaire et l'expérience sont conservés."},
		{"naturalRegeneration", "Régénération naturelle", "La santé remonte quand la faim est rassasiée."},
		{"hunger", "Faim", "La course, les sauts et le minage creusent l'appétit."},
		{"fallDamage", "Dégâts de chute", "Une chute trop haute blesse."},
		{"mobGriefing", "Dégâts des créatures", "Les explosions de creepers détruisent les blocs."},
		{"fireSpread", "Propagation du feu", "Le feu peut gagner les blocs voisins inflammables."},
		{"weather", "Météo", "Pluie et neige selon les biomes."},
		{"commands", "Commandes (triche)", "Autorise /give, /tp, /time… dans le terminal."},
		{"showCoordinates", "Afficher les coordonnées", "Le HUD affiche la position du joueur."},
		{"announceDeaths", "Annoncer les morts", "Un message signale la mort d'un joueur."},
	};
	return list;
}

class CreateWorld final : public Screen {
public:
	explicit CreateWorld(Shell &shell_) : Screen(shell_) {
		m_form.name = "Nouveau monde";
		for (const auto &[rule, value] : voxel::Game::DefaultRules())
			m_form.rules[rule] = value;
		m_form.seed = String::Format("%u", uint32_t(std::chrono::system_clock::now().time_since_epoch().count() % 2000000000));
		if (voxel::Game *g = shell.MenuGame())
			m_form.generator = g->Content().ConfigStr("world.generator");
		for (const voxel::ModInfo &m : shell.Mods().Installed())
			m_form.mods.push_back(m.id);
	}
	String Id() const override { return "createworld"; }

	void Build(ecs::Entity root) override {
		const float s = kit.Scale();
		const Palette &p = kit.Colors();
		const ecs::Entity page = Page(kit, root, 1180.f);
		kit.ScreenHeader(page, "Créer un nouveau monde", "Toutes les options se modifient plus tard, sauf la génération du terrain.",
						 [this] { shell.Pop(); });
		WidgetBuilder body = kit.Row(18.f);
		body.GrowW().GrowH().Align(ui::CrossAlign::Stretch).Parent(page);
		const ecs::Entity b = body.Spawn();
		// Colonne gauche : général, jeu, génération.
		WidgetBuilder left = kit.Scroll(10.f);
		left.GrowW().GrowH().Parent(b);
		const ecs::Entity l = left.Spawn();
		kit.SectionTitle(l, "Général", Some(MaterialIcons::EDIT));
		WidgetBuilder nameRow = kit.Column(6.f);
		nameRow.GrowW().Parent(l);
		const ecs::Entity nr = nameRow.Spawn();
		kit.Label("Nom du monde", Text::SMALL).Parent(nr).Spawn();
		WidgetBuilder name = kit.Input("Mon monde");
		name.OnTextChange([this](const String &t) {
			m_form.name = t;
			ValidateName();
		});
		const ecs::Entity ne = name.Parent(nr).Spawn();
		kit.SetInputText(ne, m_form.name);
		m_nameError = kit.Label(String(), Text::CAPTION).TextColor(p.danger).Parent(nr).Spawn();
		kit.Label("Description (facultative)", Text::SMALL).Parent(nr).Spawn();
		WidgetBuilder desc = kit.Input("Une vallée paisible, un défi de survie…");
		desc.OnTextChange([this](const String &t) { m_form.description = t; });
		const ecs::Entity de = desc.Parent(nr).Spawn();
		kit.SetInputText(de, m_form.description);

		kit.SectionTitle(l, "Mode de jeu et difficulté", Some(MaterialIcons::BOLT));
		kit.ComboRow(l, "Mode de jeu", "Survie : ressources et dangers. Créatif : blocs illimités, vol. Aventure : sans casser ni poser.",
					 MODE_NAMES, IndexOf(MODE_IDS, m_form.gameMode), [this](int i) { m_form.gameMode = MODE_IDS[size_t(i)]; });
		kit.ComboRow(l, "Difficulté", "Paisible : aucun monstre. Difficile : coups plus forts, famine mortelle.", DIFF_NAMES,
					 IndexOf(DIFF_IDS, m_form.difficulty, 2), [this](int i) { m_form.difficulty = DIFF_IDS[size_t(i)]; });
		kit.ToggleRow(l, "Commandes et triche", "Autorise /give, /tp, /time… (modifiable plus tard).", m_form.rules["commands"],
					  [this](bool v) { m_form.rules["commands"] = v; });

		kit.SectionTitle(l, "Génération du terrain", Some(MaterialIcons::TERRAIN));
		std::vector<String> genNames;
		m_genIds.clear();
		if (voxel::Game *g = shell.MenuGame())
			for (const voxel::GeneratorDef *gen : g->Content().Generators()) {
				m_genIds.push_back(gen->id);
				genNames.push_back(gen->id == "core:overworld" ? String("Monde normal (biomes)")
								   : gen->id == "core:flat"	   ? String("Monde plat")
								   : gen->id == "core:islands" ? String("Archipel (script)")
															   : gen->id);
			}
		kit.ComboRow(l, "Type de génération", "Biomes et relief, monde plat, ou générateur fourni par un mod.", genNames,
					 IndexOf(m_genIds, m_form.generator), [this](int i) { m_form.generator = m_genIds[size_t(i)]; });
		WidgetBuilder seedRow = kit.Row(8.f);
		const ecs::Entity sr = kit.SettingRow(l, "Graine", "Un même nombre redonne le même monde. Laissez-la aléatoire ou saisissez-la.",
											  std::move(seedRow));
		(void)sr;
		const ecs::Entity group = kit.LastControl();
		WidgetBuilder seed = kit.Input("Aléatoire");
		seed.W(Dimension::Px(170.f * s)).OnTextChange([this](const String &t) { m_form.seed = t; });
		m_seedInput = seed.Parent(group).Spawn();
		kit.SetInputText(m_seedInput, m_form.seed);
		kit.IconButton(MaterialIcons::REFRESH, "Nouvelle graine aléatoire", [this] {
			m_form.seed = String::Format("%u", uint32_t(std::chrono::steady_clock::now().time_since_epoch().count() % 2000000000));
			kit.SetInputText(m_seedInput, m_form.seed);
		}).Parent(group).Spawn();
		auto percent = [](float v) { return String::Format("%d %%", int(std::lround(v * 100.f))); };
		kit.SliderRow(l, "Relief", "Amplitude des collines et montagnes.", 0.3f, 2.f, m_form.relief, 0.1f, percent,
					  [this](float v) { m_form.relief = v; });
		kit.SliderRow(l, "Grottes", "Densité des réseaux souterrains.", 0.f, 2.f, m_form.caves, 0.1f, percent, [this](float v) { m_form.caves = v; });
		kit.SliderRow(l, "Minerais", "Abondance du charbon, du fer, des gemmes…", 0.2f, 3.f, m_form.ores, 0.1f, percent,
					  [this](float v) { m_form.ores = v; });
		kit.SliderRow(l, "Végétation", "Arbres, fleurs et herbes générés.", 0.f, 2.f, m_form.vegetation, 0.1f, percent,
					  [this](float v) { m_form.vegetation = v; });
		kit.SliderRow(l, "Niveau de la mer", "Décalage du niveau des océans (en blocs).", -12.f, 12.f, float(m_form.seaLevel), 1.f,
					  [](float v) { return String::Format("%+d", int(v)); }, [this](float v) { m_form.seaLevel = int(v); });
		kit.ToggleRow(l, "Structures", "Maisons, puits, ruines et rochers.", m_form.structures, [this](bool v) { m_form.structures = v; });

		// Colonne droite : créatures et événements, règles, mods.
		WidgetBuilder right = kit.Scroll(10.f);
		right.W(Dimension::Px(450.f * s)).GrowH().Parent(b);
		const ecs::Entity r = right.Spawn();
		kit.SectionTitle(r, "Créatures et événements", Some(MaterialIcons::PETS));
		for (const char *rule : {"mobSpawning", "daylightCycle", "mobGriefing", "weather"})
			for (const auto &[id, label, help] : RuleTexts())
				if (std::string(id) == rule)
					kit.ToggleRow(r, label, help, m_form.rules[rule], [this, rule](bool v) { m_form.rules[rule] = v; });
		kit.ComboRow(r, "Durée d'une journée", "Durée réelle d'un cycle jour / nuit.", {"5 min", "10 min", "20 min", "40 min"},
					 m_form.dayLength <= 300 ? 0 : m_form.dayLength <= 600 ? 1 : m_form.dayLength <= 1200 ? 2 : 3,
					 [this](int i) { m_form.dayLength = std::array<double, 4>{300, 600, 1200, 2400}[size_t(i)]; });
		kit.SectionTitle(r, "Règles de survie", Some(MaterialIcons::FAVORITE));
		for (const char *rule : {"keepInventory", "naturalRegeneration", "hunger", "fallDamage"})
			for (const auto &[id, label, help] : RuleTexts())
				if (std::string(id) == rule)
					kit.ToggleRow(r, label, help, m_form.rules[rule], [this, rule](bool v) { m_form.rules[rule] = v; });
		kit.SectionTitle(r, "Mods de ce monde", Some(MaterialIcons::EXTENSION));
		for (const voxel::ModInfo &m : shell.Mods().Installed()) {
			const bool on = std::find(m_form.mods.begin(), m_form.mods.end(), m.id) != m_form.mods.end();
			String help = m.description;
			if (!m.dependencies.empty()) {
				String deps;
				for (const String &d : m.dependencies)
					deps += (deps.IsEmpty() ? String() : String(", ")) + d;
				help = String("Requiert : ") + deps;
			}
			kit.ToggleRow(r, shell.Prefs().ModName(m.id, m.name), help, on || m.id == "core",
						  [this, id = m.id](bool v) {
							  std::erase(m_form.mods, id);
							  if (v)
								  m_form.mods.push_back(id);
						  },
						  m.id == "core");
		}
		// Pied : retour et création.
		const ecs::Entity footer = kit.Footer(page);
		kit.Muted("Astuce : Échap revient en arrière sans rien créer.", Text::CAPTION).GrowW().Parent(footer).Spawn();
		kit.Button("Retour", [this] { shell.Pop(); }).Parent(footer).Spawn();
		kit.PrimaryButton("Créer le monde", [this] { Create(); }).Parent(footer).Spawn();
		ValidateName();
	}

private:
	bool ValidateName() {
		String error;
		const String n = m_form.name.Trim();
		if (n.IsEmpty())
			error = "Le nom ne peut pas être vide.";
		else if (n.GetSize() > 48)
			error = "Le nom est trop long (48 caractères au plus).";
		kit.SetText(m_nameError, error);
		kit.SetHidden(m_nameError, error.IsEmpty());
		return error.IsEmpty();
	}

	void Create() {
		if (!ValidateName()) {
			shell.UiSound("ui_error");
			return;
		}
		uint32_t seed = 0;
		const String text = m_form.seed.Trim();
		bool numeric = !text.IsEmpty();
		for (size_t i = 0; i < text.GetSize(); ++i)
			numeric = numeric && text.CharAt(i) >= '0' && text.CharAt(i) <= '9';
		if (numeric)
			seed = uint32_t(std::strtoul(text.CStr(), nullptr, 10));
		else if (!text.IsEmpty()) {
			uint32_t h = 2166136261u; // graine textuelle → nombre
			for (size_t i = 0; i < text.GetSize(); ++i)
				h = (h ^ uint8_t(text.CharAt(i))) * 16777619u;
			seed = h;
		} else {
			seed = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());
		}
		if (std::find(m_form.mods.begin(), m_form.mods.end(), String("core")) == m_form.mods.end())
			m_form.mods.push_back("core");
		Result<String, String> r = shell.Worlds().Create(m_form.name.Trim(), seed, m_form.generator, m_form.To(nullptr));
		if (r.IsError()) {
			shell.ShowError("Création du monde impossible", "Le dossier de sauvegarde n'a pas pu être écrit.", r.Error());
			return;
		}
		shell.UiSound("ui_confirm");
		shell.PlayWorld(r.Value());
	}

	WorldForm m_form;
	std::vector<String> m_genIds;
	ecs::Entity m_nameError{}, m_seedInput{};
};

// ── Configuration avancée d'un monde ─────────────────────────────────────────

class WorldSettingsScreen final : public Screen {
public:
	WorldSettingsScreen(Shell &shell_, String folder) : Screen(shell_), m_folder(std::move(folder)) {
		if (m_folder.IsEmpty() && shell.Game())
			m_folder = shell.Game()->Folder();
		Load();
	}
	String Id() const override { return "worldsettings"; }

	void Build(ecs::Entity root) override {
		const Palette &p = kit.Colors();
		const ecs::Entity page = Page(kit, root, 1180.f);
		if (m_entry.IsNone()) {
			kit.ScreenHeader(page, "Monde introuvable", m_folder, [this] { shell.Pop(); });
			return;
		}
		const WorldEntry &w = m_entry.Value();
		const bool live = shell.Game() && shell.Game()->Folder() == m_folder;
		kit.ScreenHeader(page, String("Configuration — ") + w.name,
						 live ? String("Partie en cours : les changements s'appliquent immédiatement à l'enregistrement.")
							  : String::Format("Dossier « %s » — créé le %s", w.folder.CStr(), FormatDate(w.created).CStr()),
						 [this] { Leave(); });
		kit.Tabs(page, {"Général", "Règles", "Génération", "Sauvegarde", "Mods", "Compatibilité"}, m_tab, [this](int i) {
			m_tab = i;
			shell.Rebuild();
		});
		WidgetBuilder scroll = kit.Scroll(10.f);
		scroll.GrowW().GrowH().Parent(page);
		const ecs::Entity c = scroll.Spawn();
		auto percent = [](float v) { return String::Format("%d %%", int(std::lround(v * 100.f))); };
		switch (m_tab) {
		case 0: {
			kit.SectionTitle(c, "Paramètres généraux", Some(MaterialIcons::EDIT));
			WidgetBuilder nameInput = kit.Input("Nom du monde");
			nameInput.W(Dimension::Px(320.f)).OnTextChange([this](const String &t) {
				m_name = t;
				m_dirty = true;
			});
			const ecs::Entity row = kit.SettingRow(c, "Nom affiché", "Le dossier de sauvegarde ne change pas.", std::move(nameInput));
			SetFirstInput(row, m_name);
			WidgetBuilder descInput = kit.Input("Description");
			descInput.W(Dimension::Px(420.f)).OnTextChange([this](const String &t) {
				m_form.description = t;
				m_dirty = true;
			});
			const ecs::Entity d = kit.SettingRow(c, "Description", "Affichée dans la liste des mondes.", std::move(descInput));
			SetFirstInput(d, m_form.description);
			kit.SectionTitle(c, "Mode de jeu et difficulté", Some(MaterialIcons::BOLT));
			kit.ComboRow(c, "Mode de jeu", "S'applique au joueur dès l'enregistrement.", MODE_NAMES, IndexOf(MODE_IDS, m_form.gameMode),
						 [this](int i) {
							 m_form.gameMode = MODE_IDS[size_t(i)];
							 m_dirty = true;
						 });
			kit.ComboRow(c, "Difficulté", "Force des créatures, faim et régénération.", DIFF_NAMES, IndexOf(DIFF_IDS, m_form.difficulty, 2),
						 [this](int i) {
							 m_form.difficulty = DIFF_IDS[size_t(i)];
							 m_dirty = true;
						 });
			kit.SectionTitle(c, "Simulation", Some(MaterialIcons::SCHEDULE));
			kit.ComboRow(c, "Durée d'une journée", "Cycle jour / nuit complet.", {"5 min", "10 min", "20 min", "40 min"},
						 m_form.dayLength <= 300 ? 0 : m_form.dayLength <= 600 ? 1 : m_form.dayLength <= 1200 ? 2 : 3, [this](int i) {
							 m_form.dayLength = std::array<double, 4>{300, 600, 1200, 2400}[size_t(i)];
							 m_dirty = true;
						 });
			kit.ToggleRow(c, "Cycle jour / nuit", "Désactivé : l'heure reste figée.", m_form.rules["daylightCycle"], [this](bool v) {
				m_form.rules["daylightCycle"] = v;
				m_dirty = true;
			});
			break;
		}
		case 1: {
			kit.SectionTitle(c, "Règles du monde", Some(MaterialIcons::ADMIN_PANEL_SETTINGS));
			for (const auto &[id, label, help] : RuleTexts()) {
				const std::string rule = id;
				kit.ToggleRow(c, label, help, m_form.rules[rule], [this, rule](bool v) {
					m_form.rules[rule] = v;
					m_dirty = true;
				});
			}
			break;
		}
		case 2: {
			kit.SectionTitle(c, "Génération (verrouillée après la création)", Some(MaterialIcons::LOCK));
			WidgetBuilder banner = kit.Row(10.f);
			banner.GrowW().Pad(math::Sides(12.f, 10.f)).Radius(8.f).Bg(WithAlpha(p.info, 0.12f)).BorderColor(WithAlpha(p.info, 0.5f), 1.f);
			banner.Parent(c);
			const ecs::Entity bn = banner.Spawn();
			kit.Icon(MaterialIcons::INFO, 20.f, Some(p.info)).Parent(bn).Spawn();
			kit.Paragraph("Le terrain déjà généré ne change pas : ces réglages ne s'appliquent qu'aux zones encore inexplorées. "
						  "La graine et le type de génération sont fixés à la création.",
						  Text::SMALL)
				.Parent(bn)
				.Spawn();
			kit.SettingRow(c, "Graine", "Fixée à la création.", kit.Label(String::Format("%u", w.seed), Text::BODY), true);
			kit.SettingRow(c, "Type de génération", "Fixé à la création.", kit.Label(w.generator.IsEmpty() ? String("par défaut") : w.generator), true);
			kit.SliderRow(c, "Grottes (nouvelles zones)", "Densité des réseaux souterrains.", 0.f, 2.f, m_form.caves, 0.1f, percent, [this](float v) {
				m_form.caves = v;
				m_dirty = true;
			});
			kit.SliderRow(c, "Minerais (nouvelles zones)", "Abondance des minerais.", 0.2f, 3.f, m_form.ores, 0.1f, percent, [this](float v) {
				m_form.ores = v;
				m_dirty = true;
			});
			kit.SliderRow(c, "Végétation (nouvelles zones)", "Arbres et plantes.", 0.f, 2.f, m_form.vegetation, 0.1f, percent, [this](float v) {
				m_form.vegetation = v;
				m_dirty = true;
			});
			kit.SettingRow(c, "Relief", "Fixé à la création (raccords impossibles).", kit.Label(percent(m_form.relief)), true);
			kit.SettingRow(c, "Niveau de la mer", "Fixé à la création.", kit.Label(String::Format("%+d", m_form.seaLevel)), true);
			kit.ToggleRow(c, "Structures (nouvelles zones)", "Maisons, puits, ruines.", m_form.structures, [this](bool v) {
				m_form.structures = v;
				m_dirty = true;
			});
			break;
		}
		case 3: {
			kit.SectionTitle(c, "Sauvegarde", Some(MaterialIcons::SAVE));
			InfoRow(kit, c, "Dossier", w.path);
			InfoRow(kit, c, "Taille", FormatBytes(w.sizeBytes));
			InfoRow(kit, c, "Créé le", FormatDate(w.created));
			InfoRow(kit, c, "Dernière session", FormatDate(w.lastPlayed));
			InfoRow(kit, c, "Temps de jeu", FormatDuration(w.playTime));
			InfoRow(kit, c, "Format", String::Format("sauvegarde v%d (jeu : v%d)", w.version, voxel::SAVE_VERSION));
			kit.SliderRow(c, "Sauvegarde automatique", "Préférence globale (Paramètres > Général).", 0.f, 15.f,
						  float(shell.Prefs().autosaveMinutes), 1.f,
						  [](float v) { return v < 0.5f ? String("jamais") : String::Format("%d min", int(v)); },
						  [this](float v) {
							  shell.Prefs().autosaveMinutes = int(v);
							  shell.SaveSettings();
						  });
			WidgetBuilder actions = kit.Row(10.f);
			actions.Parent(c);
			const ecs::Entity a = actions.Spawn();
			kit.Button("Créer une sauvegarde de sécurité", [this] {
				Result<String, String> r = shell.Worlds().Backup(m_folder);
				if (r.IsOk())
					shell.Notify(Tone::SUCCESS, "Sauvegarde de sécurité créée", r.Value());
				else
					shell.Notify(Tone::DANGER, "Échec de la sauvegarde de sécurité", r.Error(), true);
			}).Parent(a).Spawn();
			if (live)
				kit.Button("Enregistrer maintenant", [this] { shell.Game()->Save(true); }).Parent(a).Spawn();
			break;
		}
		case 4: {
			kit.SectionTitle(c, "Paramètres des mods de ce monde", Some(MaterialIcons::EXTENSION));
			for (const voxel::ModInfo &m : shell.Mods().Installed()) {
				const bool enabled = w.enabledMods.empty() || std::find(w.enabledMods.begin(), w.enabledMods.end(), m.id) != w.enabledMods.end();
				const bool configurable = json::At(m.manifest, "settings") != nullptr;
				WidgetBuilder btn = kit.Button(configurable ? "Configurer" : "Aucun réglage", [this, id = m.id] {
					shell.Push(MakeModConfig(shell, id, m_folder));
				});
				if (!configurable || !enabled)
					btn.Disabled();
				kit.SettingRow(c, shell.Prefs().ModName(m.id, m.name) + (enabled ? String() : String(" (désactivé)")),
							   String::Format("%s — version %s", m.id.CStr(), m.version.CStr()), std::move(btn));
			}
			kit.Button("Gérer les mods du monde…", [this] { shell.Push(MakeWorldMods(shell, m_folder)); }).Parent(c).Spawn();
			break;
		}
		default: {
			kit.SectionTitle(c, "Informations de compatibilité", Some(MaterialIcons::CHECK_CIRCLE));
			if (w.issues.empty())
				kit.SettingRow(c, "Compatible", "Tous les mods attendus sont installés dans la bonne version.", kit.Chip("OK", Tone::SUCCESS));
			for (const WorldIssue &issue : w.issues) {
				const bool danger = issue.kind != WorldIssue::Kind::VERSION_CHANGED;
				kit.SettingRow(c, issue.kind == WorldIssue::Kind::MISSING_MOD	   ? String::Format("Mod manquant : %s", issue.modId.CStr())
								  : issue.kind == WorldIssue::Kind::VERSION_CHANGED ? String::Format("Version changée : %s", issue.modId.CStr())
																					  : String("Sauvegarde plus récente"),
							   issue.detail, kit.Chip(danger ? "Attention" : "À vérifier", danger ? Tone::DANGER : Tone::WARNING));
			}
			kit.SectionTitle(c, "Mods de la dernière session", NONE);
			for (const auto &[id, version] : w.mods)
				InfoRow(kit, c, id, version);
			break;
		}
		}
		const ecs::Entity footer = kit.Footer(page);
		WidgetBuilder status = kit.Muted(m_dirty ? "Modifications non enregistrées" : "", Text::SMALL);
		status.GrowW().TextColor(p.warning).Parent(footer).Spawn();
		kit.GhostButton("Réinitialiser", [this] {
			ConfirmSpec spec;
			spec.title = "Réinitialiser les paramètres du monde ?";
			spec.message = "Mode, difficulté et règles reviennent aux valeurs par défaut. Le terrain n'est pas touché.";
			spec.confirmText = "Réinitialiser";
			spec.tone = Tone::WARNING;
			spec.icon = MaterialIcons::REFRESH;
			spec.onConfirm = [this] {
				m_form.gameMode = "survival";
				m_form.difficulty = "normal";
				m_form.dayLength = 600.0;
				for (const auto &[rule, value] : voxel::Game::DefaultRules())
					m_form.rules[rule] = value;
				m_dirty = true;
			};
			shell.Confirm(std::move(spec));
		}).Parent(footer).Spawn();
		kit.Button("Annuler", [this] { shell.Pop(); }).Parent(footer).Spawn();
		kit.PrimaryButton("Enregistrer", [this] { Save(); }).Parent(footer).Spawn();
	}
	bool OnBack() override {
		Leave();
		return true;
	}

private:
	void Load() {
		m_entry = shell.Worlds().Find(m_folder, shell.Mods().Installed());
		if (m_entry.IsSome()) {
			m_form.From(m_entry.Value().settings);
			m_name = m_entry.Value().name;
		}
	}
	void SetFirstInput(ecs::Entity row, const String &text) {
		(void)row;
		kit.SetInputText(kit.LastControl(), text);
	}
	void Save() {
		if (m_entry.IsNone())
			return;
		dataio::NodePtr settings = m_form.To(m_entry.Value().settings);
		if (Session *s = shell.Game(); s && s->Folder() == m_folder) {
			// Partie en cours : les règles s'appliquent au jeu, qui les enregistre.
			voxel::Game &g = s->Game();
			g.Metadata().settings = settings;
			g.Metadata().name = m_name.Trim().IsEmpty() ? g.Metadata().name : m_name.Trim();
			g.SetMode(voxel::GameModeFromId(m_form.gameMode));
			g.SetDifficulty(voxel::DifficultyFromId(m_form.difficulty));
			s->Save(false);
		} else {
			Result<bool, String> r = shell.Worlds().Update(m_folder, m_name.Trim().IsEmpty() ? Option<String>(NONE) : Some(m_name.Trim()),
														   settings);
			if (r.IsError()) {
				shell.Notify(Tone::DANGER, "Échec de l'enregistrement", r.Error(), true);
				return;
			}
		}
		m_dirty = false;
		shell.UiSound("ui_save");
		shell.Notify(Tone::SUCCESS, "Paramètres du monde enregistrés", m_name);
		shell.Pop();
	}
	void Leave() {
		if (!m_dirty) {
			shell.Pop();
			return;
		}
		ConfirmSpec spec;
		spec.title = "Abandonner les modifications ?";
		spec.message = "Les changements apportés à ce monde ne seront pas enregistrés.";
		spec.confirmText = "Abandonner";
		spec.cancelText = "Continuer l'édition";
		spec.tone = Tone::WARNING;
		spec.onConfirm = [this] {
			m_dirty = false;
			shell.Pop();
		};
		shell.Confirm(std::move(spec));
	}

	String m_folder, m_name;
	Option<WorldEntry> m_entry = NONE;
	WorldForm m_form;
	int m_tab = 0;
	bool m_dirty = false;
};

// ── Renommage ────────────────────────────────────────────────────────────────

class RenameWorld final : public Screen {
public:
	RenameWorld(Shell &shell_, String folder) : Screen(shell_), m_folder(std::move(folder)) {
		if (Option<WorldEntry> e = shell.Worlds().Find(m_folder, {}); e.IsSome())
			m_current = m_value = e.Value().name;
	}
	String Id() const override { return "rename"; }
	bool Overlay() const override { return true; }

	void Build(ecs::Entity root) override {
		const Palette &p = kit.Colors();
		const ecs::Entity card = ModalCard(kit, root, 460.f);
		WidgetBuilder head = kit.Row(10.f);
		head.Parent(card);
		const ecs::Entity h = head.Spawn();
		kit.Icon(MaterialIcons::EDIT, 24.f, Some(p.primary)).Parent(h).Spawn();
		kit.Label("Renommer le monde", Text::H2).Parent(h).Spawn();
		WidgetBuilder cur = kit.Column(2.f);
		cur.GrowW().Pad(math::Sides(12.f, 8.f)).Radius(8.f).Bg(p.bg2).Parent(card);
		const ecs::Entity c = cur.Spawn();
		kit.Muted("Nom actuel", Text::CAPTION).Parent(c).Spawn();
		kit.Label(m_current, Text::BODY).Bold().Parent(c).Spawn();
		kit.Label("Nouveau nom", Text::SMALL).Parent(card).Spawn();
		WidgetBuilder input = kit.Input("Nouveau nom");
		input.OnTextChange([this](const String &t) {
			m_value = t;
			Validate();
		});
		input.OnSubmit([this](const String &) { Apply(); });
		m_input = input.Parent(card).Spawn();
		kit.SetInputText(m_input, m_value);
		m_error = kit.Label(String(), Text::CAPTION).TextColor(p.danger).Parent(card).Spawn();
		kit.Muted(String::Format("Le dossier « %s » est conservé : les sauvegardes et vignettes restent associées.", m_folder.CStr()),
				  Text::CAPTION)
			.GrowW()
			.TextWrap()
			.Parent(card)
			.Spawn();
		WidgetBuilder buttons = kit.Row(10.f);
		buttons.GrowW().Justify(ui::Justify::End).Parent(card);
		const ecs::Entity b = buttons.Spawn();
		kit.Button("Annuler", [this] { shell.Pop(); }).Parent(b).Spawn();
		kit.PrimaryButton("Renommer", [this] { Apply(); }).Parent(b).Spawn();
		Validate();
	}

private:
	bool Validate() {
		const String n = m_value.Trim();
		String e;
		if (n.IsEmpty())
			e = "Le nom ne peut pas être vide.";
		else if (n.GetSize() > 48)
			e = "48 caractères au plus.";
		else if (n == m_current)
			e = "C'est déjà le nom de ce monde.";
		kit.SetText(m_error, e);
		kit.SetHidden(m_error, e.IsEmpty());
		return e.IsEmpty();
	}
	void Apply() {
		if (!Validate()) {
			shell.UiSound("ui_error");
			return;
		}
		Result<bool, String> r = shell.Worlds().Update(m_folder, Some(m_value.Trim()), dataio::NodePtr());
		if (r.IsError()) {
			shell.Notify(Tone::DANGER, "Renommage impossible", r.Error(), true);
			return;
		}
		shell.Notify(Tone::SUCCESS, "Monde renommé", String::Format("« %s » → « %s »", m_current.CStr(), m_value.Trim().CStr()));
		shell.Pop();
		shell.Rebuild();
	}
	String m_folder, m_current, m_value;
	ecs::Entity m_input{}, m_error{};
};

} // namespace

std::unique_ptr<Screen> MakeMainMenu(Shell &shell) { return std::make_unique<MainMenu>(shell); }
std::unique_ptr<Screen> MakeWorldSelect(Shell &shell) { return std::make_unique<WorldSelect>(shell); }
std::unique_ptr<Screen> MakeCreateWorld(Shell &shell) { return std::make_unique<CreateWorld>(shell); }
std::unique_ptr<Screen> MakeWorldSettings(Shell &shell, String folder) { return std::make_unique<WorldSettingsScreen>(shell, std::move(folder)); }
std::unique_ptr<Screen> MakeRenameWorld(Shell &shell, String folder) { return std::make_unique<RenameWorld>(shell, std::move(folder)); }

} // namespace voxelgame
