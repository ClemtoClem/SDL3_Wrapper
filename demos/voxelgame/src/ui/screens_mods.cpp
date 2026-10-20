// Écrans des mods : navigateur, fiche, mods d'un monde, configuration,
// organisation, confirmation des changements.
#include "common.hpp"

#include <algorithm>

namespace voxelgame {

using namespace uiutil;

namespace {

String Join(const std::vector<String> &list, const char *sep = ", ") {
	String out;
	for (const String &s : list)
		out += (out.IsEmpty() ? String() : String(sep)) + s;
	return out;
}

/// Catégorie affichée (personnalisée par l'utilisateur, sinon celle du mod).
String CategoryOf(Shell &shell, const ModEntry &m) {
	auto it = shell.Prefs().modCategories.find(m.info.id.CStr());
	return it != shell.Prefs().modCategories.end() && !it->second.IsEmpty() ? it->second : m.Category();
}

/// Mondes qui utilisent un mod.
std::vector<String> WorldsUsing(Shell &shell, const String &id) {
	std::vector<String> out;
	for (const WorldEntry &w : shell.Worlds().List(shell.Mods().Installed())) {
		const bool listed = std::find(w.enabledMods.begin(), w.enabledMods.end(), id) != w.enabledMods.end();
		const bool implicit = w.enabledMods.empty() && std::any_of(w.mods.begin(), w.mods.end(), [&](const auto &m) { return m.first == id; });
		if (listed || implicit)
			out.push_back(w.name);
	}
	return out;
}

// ── Navigateur ───────────────────────────────────────────────────────────────

class ModBrowser final : public Screen {
public:
	explicit ModBrowser(Shell &shell) : Screen(shell) {}
	String Id() const override { return "mods"; }

	void Build(ecs::Entity root) override {
		const float s = kit.Scale();
		const Palette &p = kit.Colors();
		shell.Mods().Refresh();
		const ecs::Entity page = Page(kit, root, 1240.f);
		int installed = 0, available = 0, updates = 0;
		for (const ModEntry &m : shell.Mods().Entries()) {
			installed += m.installed;
			available += !m.installed;
			updates += m.UpdateAvailable();
		}
		const ecs::Entity header = kit.ScreenHeader(page, "Mods",
													String::Format("%d installé(s) · %d disponible(s) · %d mise(s) à jour", installed, available, updates),
													[this] { shell.Pop(); });
		kit.Button("Organiser", [this] { shell.Push(MakeModOrganize(shell)); }).Parent(header).Spawn();
		WidgetBuilder body = kit.Row(18.f);
		body.GrowW().GrowH().Align(ui::CrossAlign::Stretch).Parent(page);
		const ecs::Entity b = body.Spawn();
		// Barre latérale : catégories.
		WidgetBuilder side = kit.Column(2.f);
		side.W(Dimension::Px(250.f * s)).Parent(b);
		const ecs::Entity sd = side.Spawn();
		kit.Label("CATÉGORIES", Text::CAPTION).TextColor(p.textDim).Bold().Parent(sd).Spawn();
		auto count = [&](const String &cat) {
			int n = 0;
			for (const ModEntry &m : shell.Mods().Entries())
				n += cat.IsEmpty() || CategoryOf(shell, m) == cat;
			return n;
		};
		std::vector<std::pair<String, String>> cats{{String(), String("Toutes")}};
		for (const auto &c : ModCatalog::Categories())
			cats.push_back(c);
		for (const auto &[id, name] : cats) {
			const bool on = id == m_category;
			WidgetBuilder item = kit.Row(8.f);
			item.GrowW().H(Dimension::Px(32.f * s)).Pad(math::Sides(10.f * s, 0.f)).Radius(6.f);
			ui::UiStyle st;
			st.SetBg(on ? WithAlpha(p.primary, 0.18f) : WithAlpha(p.bg3, 0.f)).SetBgHovered(p.bg3);
			item.Bg(sdl3::FColor{}).Style(std::move(st)).OnClick([this, id] {
				m_category = id;
				shell.Rebuild();
			}).Parent(sd);
			const ecs::Entity it = item.Spawn();
			kit.Label(name, Text::SMALL).TextColor(on ? p.text : p.textMuted).GrowW().PointerThrough().Parent(it).Spawn();
			kit.Muted(String::Format("%d", count(id)), Text::CAPTION).PointerThrough().Parent(it).Spawn();
		}
		// Contenu : recherche, filtres, tri, cartes.
		WidgetBuilder main = kit.Column(10.f);
		main.GrowW().GrowH().Parent(b);
		const ecs::Entity mn = main.Spawn();
		WidgetBuilder tools = kit.Row(10.f);
		tools.GrowW().Parent(mn);
		const ecs::Entity t = tools.Spawn();
		kit.Icon(MaterialIcons::SEARCH, 20.f).Parent(t).Spawn();
		WidgetBuilder search = kit.Input("Rechercher (nom, auteur, mot-clé)…");
		search.OnTextChange([this](const String &q) {
			m_query = q.ToLower();
			Refresh();
		});
		const ecs::Entity se = search.Parent(t).Spawn();
		kit.SetInputText(se, m_query);
		kit.Combo({"Tous", "Installés", "Disponibles", "Mises à jour", "Compatibles"}, m_filter, [this](int i) {
			m_filter = i;
			Refresh();
		}).Parent(t).Spawn();
		kit.Combo({"Nom", "Catégorie", "Auteur", "Installés d'abord"}, m_sort, [this](int i) {
			m_sort = i;
			Refresh();
		}).Parent(t).Spawn();
		WidgetBuilder list = kit.Scroll(10.f);
		list.GrowW().GrowH().Parent(mn);
		m_list = list.Spawn();
		Refresh();
	}

private:
	void Refresh() {
		kit.ClearChildren(m_list);
		std::vector<const ModEntry *> shown;
		for (const ModEntry &m : shell.Mods().Entries()) {
			if (!m_category.IsEmpty() && CategoryOf(shell, m) != m_category)
				continue;
			if (m_filter == 1 && !m.installed)
				continue;
			if (m_filter == 2 && m.installed)
				continue;
			if (m_filter == 3 && !m.UpdateAvailable())
				continue;
			if (m_filter == 4 && !m.info.gameVersion.IsEmpty() && !shell.Version().StartsWith(m.info.gameVersion.CStr()))
				continue;
			const String hay = (m.info.name + String(" ") + m.info.author + String(" ") + m.info.description + String(" ") + Join(m.info.tags)).ToLower();
			if (!m_query.IsEmpty() && !hay.Contains(m_query))
				continue;
			shown.push_back(&m);
		}
		std::sort(shown.begin(), shown.end(), [this](const ModEntry *a, const ModEntry *b) {
			switch (m_sort) {
			case 1: return CategoryOf(shell, *a) < CategoryOf(shell, *b);
			case 2: return a->info.author < b->info.author;
			case 3: return a->installed > b->installed;
			default: return shell.Prefs().ModName(a->info.id, a->info.name).ToLower() < shell.Prefs().ModName(b->info.id, b->info.name).ToLower();
			}
		});
		if (shown.empty())
			kit.Muted("Aucun mod ne correspond à ces critères.", Text::BODY).Parent(m_list).Spawn();
		// Deux cartes par ligne.
		ecs::Entity row{};
		for (size_t i = 0; i < shown.size(); ++i) {
			if (i % 2 == 0) {
				WidgetBuilder r = kit.Row(10.f);
				r.GrowW().Align(ui::CrossAlign::Stretch).Parent(m_list);
				row = r.Spawn();
			}
			Card(row, *shown[i]);
		}
		if (shown.size() % 2 == 1)
			kit.Spacer().Parent(row).Spawn();
	}

	void Card(ecs::Entity parent, const ModEntry &m) {
		const Palette &p = kit.Colors();
		const float s = kit.Scale();
		WidgetBuilder card = kit.Column(8.f);
		card.GrowW().Pad(math::Sides(14.f * s, 12.f * s)).Radius(10.f * s).Parent(parent);
		ui::UiStyle st;
		st.SetBg(p.bg2).SetBgHovered(p.bg3).SetBorderColor(p.line).SetBorderHovered(p.lineStrong).SetBordersWidth(1.f);
		card.Bg(sdl3::FColor{}).Style(std::move(st)).OnDoubleClick([this, id = m.info.id] { shell.Push(MakeModDetails(shell, id)); });
		const ecs::Entity c = card.Spawn();
		WidgetBuilder top = kit.Row(12.f);
		top.GrowW().PointerThrough().Parent(c);
		const ecs::Entity t = top.Spawn();
		ModIcon(shell, m.info, 52.f).Parent(t).Spawn();
		WidgetBuilder head = kit.Column(2.f);
		head.GrowW().PointerThrough().Parent(t);
		const ecs::Entity h = head.Spawn();
		kit.Label(shell.Prefs().ModName(m.info.id, m.info.name), Text::H3).PointerThrough().Parent(h).Spawn();
		kit.Muted(String::Format("par %s · v%s%s", m.info.author.IsEmpty() ? "inconnu" : m.info.author.CStr(), m.info.version.CStr(),
								 m.UpdateAvailable() ? String::Format(" → v%s", m.available.Value().version.CStr()).CStr() : ""),
				  Text::CAPTION)
			.PointerThrough()
			.Parent(h)
			.Spawn();
		WidgetBuilder desc = kit.Paragraph(m.info.description, Text::SMALL);
		desc.H(Dimension::Px(36.f * s)).TextEllipsis().PointerThrough().Parent(c).Spawn();
		WidgetBuilder chips = kit.Row(6.f);
		chips.GrowW().PointerThrough().Parent(c);
		const ecs::Entity ch = chips.Spawn();
		kit.Chip(ModCatalog::CategoryName(CategoryOf(shell, m)), Tone::INFO).Parent(ch).Spawn();
		if (m.builtin)
			kit.Chip("Intégré", Tone::NEUTRAL).Parent(ch).Spawn();
		else if (m.installed)
			kit.Chip("Installé · activé", Tone::SUCCESS).Parent(ch).Spawn();
		else
			kit.Chip("Disponible", Tone::NEUTRAL).Parent(ch).Spawn();
		if (m.UpdateAvailable())
			kit.Chip("Mise à jour", Tone::ACCENT).Parent(ch).Spawn();
		if (!m.info.gameVersion.IsEmpty())
			kit.Chip(String("Jeu ") + m.info.gameVersion, shell.Version().StartsWith(m.info.gameVersion.CStr()) ? Tone::SUCCESS : Tone::WARNING)
				.Parent(ch)
				.Spawn();
		if (!m.info.dependencies.empty())
			kit.Muted(String("Requiert : ") + Join(m.info.dependencies), Text::CAPTION).PointerThrough().Parent(c).Spawn();
		if (!m.info.incompatibilities.empty())
			kit.Muted(String("Incompatible avec : ") + Join(m.info.incompatibilities), Text::CAPTION).TextColor(p.warning).PointerThrough().Parent(c).Spawn();
		WidgetBuilder actions = kit.Row(8.f);
		actions.GrowW().Justify(ui::Justify::End).Parent(c);
		const ecs::Entity a = actions.Spawn();
		kit.GhostButton("Détails", [this, id = m.info.id] { shell.Push(MakeModDetails(shell, id)); }).Parent(a).Spawn();
		if (m.UpdateAvailable())
			kit.PrimaryButton("Mettre à jour", [this, id = m.info.id] { Install(id); }).Parent(a).Spawn();
		if (!m.installed)
			kit.PrimaryButton("Installer", [this, id = m.info.id] { Install(id); }).Parent(a).Spawn();
		else if (!m.builtin)
			kit.DangerButton("Désinstaller", [this, id = m.info.id, name = m.info.name] { Uninstall(id, name); }).Parent(a).Spawn();
	}

	void Install(const String &id) {
		Result<bool, String> r = shell.Mods().Install(id);
		if (r.IsOk()) {
			shell.UiSound("ui_confirm");
			shell.Notify(Tone::SUCCESS, "Mod installé", id);
		} else {
			shell.Notify(Tone::DANGER, "Installation impossible", r.Error(), true);
		}
		shell.Rebuild();
	}
	void Uninstall(const String &id, const String &name) {
		const std::vector<String> worlds = WorldsUsing(shell, id);
		ConfirmSpec spec;
		spec.title = String::Format("Désinstaller « %s » ?", name.CStr());
		spec.message = "Le mod est retiré du dossier des mods ; il reste réinstallable depuis le catalogue.";
		if (!worlds.empty())
			spec.details.push_back(String::Format("Utilisé par %d monde(s) : %s", int(worlds.size()), Join(worlds).CStr()));
		spec.details.push_back("Les blocs de ce mod déjà posés deviendront des blocs inconnus jusqu'à sa réinstallation.");
		spec.confirmText = "Désinstaller";
		spec.icon = MaterialIcons::DELETE;
		spec.onConfirm = [this, id] {
			Result<bool, String> r = shell.Mods().Uninstall(id);
			if (r.IsOk())
				shell.Notify(Tone::SUCCESS, "Mod désinstallé", id);
			else
				shell.Notify(Tone::DANGER, "Désinstallation impossible", r.Error(), true);
			shell.Rebuild();
		};
		shell.Confirm(std::move(spec));
	}

	ecs::Entity m_list{};
	String m_query, m_category;
	int m_filter = 0, m_sort = 0;
};

// ── Fiche d'un mod ───────────────────────────────────────────────────────────

class ModDetails final : public Screen {
public:
	ModDetails(Shell &shell, String id) : Screen(shell), m_id(std::move(id)) {}
	String Id() const override { return "moddetails"; }

	void Build(ecs::Entity root) override {
		const Palette &p = kit.Colors();
		const float s = kit.Scale();
		const ecs::Entity page = Page(kit, root, 1100.f);
		const ModEntry *m = shell.Mods().Find(m_id);
		if (!m) {
			kit.ScreenHeader(page, "Mod introuvable", m_id, [this] { shell.Pop(); });
			return;
		}
		const voxel::ModInfo &info = m->info;
		// En-tête : illustration, nom, auteur, versions, actions.
		WidgetBuilder head = kit.Row(18.f);
		head.GrowW().Parent(page);
		const ecs::Entity h = head.Spawn();
		kit.IconButton(MaterialIcons::ARROW_BACK, "Retour", [this] { shell.Pop(); }).Parent(h).Spawn();
		ModIcon(shell, info, 84.f).Parent(h).Spawn();
		WidgetBuilder title = kit.Column(4.f);
		title.GrowW().Parent(h);
		const ecs::Entity tt = title.Spawn();
		kit.Label(shell.Prefs().ModName(info.id, info.name), Text::H1).Parent(tt).Spawn();
		kit.Muted(String::Format("par %s · identifiant « %s »", info.author.IsEmpty() ? "inconnu" : info.author.CStr(), info.id.CStr()), Text::SMALL)
			.Parent(tt)
			.Spawn();
		WidgetBuilder chips = kit.Row(6.f);
		chips.Parent(tt);
		const ecs::Entity ch = chips.Spawn();
		kit.Chip(ModCatalog::CategoryName(CategoryOf(shell, *m)), Tone::INFO).Parent(ch).Spawn();
		kit.Chip(m->installed ? String::Format("Installé : v%s", info.version.CStr()) : String("Non installé"),
				 m->installed ? Tone::SUCCESS : Tone::NEUTRAL)
			.Parent(ch)
			.Spawn();
		if (m->available.IsSome())
			kit.Chip(String::Format("Dernière version : v%s", m->available.Value().version.CStr()), m->UpdateAvailable() ? Tone::ACCENT : Tone::NEUTRAL)
				.Parent(ch)
				.Spawn();
		WidgetBuilder actions = kit.Column(8.f);
		actions.Parent(h);
		const ecs::Entity a = actions.Spawn();
		if (!m->installed)
			kit.PrimaryButton("Installer", [this] { Do(shell.Mods().Install(m_id), "Mod installé"); }).Parent(a).Spawn();
		if (m->UpdateAvailable())
			kit.PrimaryButton("Mettre à jour", [this] { Do(shell.Mods().Install(m_id), "Mod mis à jour"); }).Parent(a).Spawn();
		if (m->installed && json::At(info.manifest, "settings"))
			kit.Button("Configurer", [this] { shell.Push(MakeModConfig(shell, m_id, String())); }).Parent(a).Spawn();
		if (m->installed && !m->builtin)
			kit.DangerButton("Désinstaller", [this] { Do(shell.Mods().Uninstall(m_id), "Mod désinstallé"); }).Parent(a).Spawn();

		WidgetBuilder body = kit.Row(18.f);
		body.GrowW().GrowH().Align(ui::CrossAlign::Stretch).Parent(page);
		const ecs::Entity b = body.Spawn();
		WidgetBuilder left = kit.Scroll(10.f);
		left.GrowW().GrowH().Parent(b);
		const ecs::Entity l = left.Spawn();
		kit.SectionTitle(l, "Description", Some(MaterialIcons::INFO));
		kit.Paragraph(json::Str(info.manifest, "longDescription", info.description), Text::BODY).Parent(l).Spawn();
		if (!info.screenshots.empty()) {
			kit.SectionTitle(l, "Captures d'écran", NONE);
			WidgetBuilder shots = kit.Row(10.f);
			shots.GrowW().Parent(l);
			const ecs::Entity sh = shots.Spawn();
			for (const String &path : info.screenshots)
				if (const String key = ImageKey(shell, path); !key.IsEmpty()) {
					WidgetBuilder img = f.Image(key, 220.f * s, 124.f * s);
					img.Fit(ui::ImageFit::COVER).Radius(6.f).Parent(sh).Spawn();
				}
		}
		kit.SectionTitle(l, "Journal des modifications", Some(MaterialIcons::SCHEDULE));
		if (dataio::NodePtr log = json::At(info.manifest, "changelog"); log && log->IsArray() && log->GetSize() > 0) {
			for (size_t i = 0; i < log->GetSize(); ++i) {
				dataio::NodePtr e = log->At(i);
				WidgetBuilder entry = kit.Column(3.f);
				entry.GrowW().Pad(math::Sides(12.f, 8.f)).Radius(8.f).Bg(p.bg2).Parent(l);
				const ecs::Entity en = entry.Spawn();
				kit.Label(String::Format("v%s — %s", json::Str(e, "version").CStr(), json::Str(e, "date").CStr()), Text::SMALL).Bold().Parent(en).Spawn();
				for (const String &change : json::StrList(e, "changes"))
					kit.Paragraph(String("• ") + change, Text::SMALL).Parent(en).Spawn();
			}
		} else {
			kit.Muted("Aucun journal fourni par l'auteur.", Text::SMALL).Parent(l).Spawn();
		}
		WidgetBuilder right = kit.Scroll(8.f);
		right.W(Dimension::Px(360.f * s)).GrowH().Parent(b);
		const ecs::Entity r = right.Spawn();
		kit.SectionTitle(r, "Dépendances", Some(MaterialIcons::EXTENSION));
		if (info.dependencies.empty())
			kit.Muted("Aucune", Text::SMALL).Parent(r).Spawn();
		for (const String &dep : info.dependencies) {
			const ModEntry *d = shell.Mods().Find(dep);
			InfoRow(kit, r, dep, d && d->installed ? String("installée") : String("manquante"), Some(d && d->installed ? p.success : p.danger));
		}
		for (const String &dep : info.optionalDependencies)
			InfoRow(kit, r, dep, String("facultative"));
		kit.SectionTitle(r, "Incompatibilités connues", Some(MaterialIcons::WARNING));
		if (info.incompatibilities.empty())
			kit.Muted("Aucune", Text::SMALL).Parent(r).Spawn();
		for (const String &other : info.incompatibilities) {
			const ModEntry *o = shell.Mods().Find(other);
			InfoRow(kit, r, other, o && o->installed ? String("installé : conflit") : String("non installé"),
					Some(o && o->installed ? p.danger : p.textMuted));
		}
		kit.SectionTitle(r, "Mondes utilisant ce mod", Some(MaterialIcons::PUBLIC));
		const std::vector<String> worlds = WorldsUsing(shell, m_id);
		if (worlds.empty())
			kit.Muted("Aucun", Text::SMALL).Parent(r).Spawn();
		for (const String &w : worlds)
			kit.Label(w, Text::SMALL).Parent(r).Spawn();
		kit.SectionTitle(r, "Paramètres configurables", Some(MaterialIcons::TUNE));
		dataio::NodePtr settings = json::At(info.manifest, "settings");
		if (!settings || !settings->IsArray() || settings->GetSize() == 0)
			kit.Muted("Aucun", Text::SMALL).Parent(r).Spawn();
		for (size_t i = 0; settings && i < settings->GetSize(); ++i)
			kit.Muted(String("• ") + json::Str(settings->At(i), "label", json::Str(settings->At(i), "id")), Text::SMALL).Parent(r).Spawn();
		kit.SectionTitle(r, "Informations techniques", Some(MaterialIcons::BUILD));
		InfoRow(kit, r, "Identifiant", info.id);
		InfoRow(kit, r, "Version installée", m->installed ? info.version : String("—"));
		InfoRow(kit, r, "Version du jeu visée", info.gameVersion.IsEmpty() ? String("non précisée") : info.gameVersion);
		InfoRow(kit, r, "Scripts", String::Format("%d", int(info.scripts.size())));
		InfoRow(kit, r, "Dossier", info.path);
		if (!info.homepage.IsEmpty())
			InfoRow(kit, r, "Site", info.homepage);
	}

private:
	void Do(Result<bool, String> r, const char *ok) {
		if (r.IsOk())
			shell.Notify(Tone::SUCCESS, ok, m_id);
		else
			shell.Notify(Tone::DANGER, "Opération impossible", r.Error(), true);
		shell.Rebuild();
	}
	String m_id;
};

// ── Mods d'un monde ──────────────────────────────────────────────────────────

class WorldMods final : public Screen {
public:
	WorldMods(Shell &shell, String folder) : Screen(shell), m_folder(std::move(folder)) {
		shell.Mods().Refresh();
		m_entry = shell.Worlds().Find(m_folder, shell.Mods().Installed());
		if (m_entry.IsSome()) {
			m_enabled = m_entry.Value().enabledMods;
			if (m_enabled.empty())
				for (const voxel::ModInfo &m : shell.Mods().Installed())
					m_enabled.push_back(m.id);
			m_original = m_enabled;
		}
	}
	String Id() const override { return "worldmods"; }

	void Build(ecs::Entity root) override {
		const Palette &p = kit.Colors();
		const ecs::Entity page = Page(kit, root, 1180.f);
		if (m_entry.IsNone()) {
			kit.ScreenHeader(page, "Monde introuvable", m_folder, [this] { shell.Pop(); });
			return;
		}
		const WorldEntry &w = m_entry.Value();
		kit.ScreenHeader(page, String("Mods du monde — ") + w.name, "Choisissez les mods chargés avec ce monde.", [this] { Leave(); });
		// Avertissement : changements qui touchent la sauvegarde.
		WidgetBuilder banner = kit.Row(10.f);
		banner.GrowW().Pad(math::Sides(12.f, 10.f)).Radius(8.f).Bg(WithAlpha(p.warning, 0.12f)).BorderColor(WithAlpha(p.warning, 0.5f), 1.f);
		banner.Parent(page);
		const ecs::Entity bn = banner.Spawn();
		kit.Icon(MaterialIcons::WARNING, 20.f, Some(p.warning)).Parent(bn).Spawn();
		kit.Paragraph("Retirer un mod rend inconnus ses blocs et créatures déjà présents dans ce monde. Une sauvegarde de sécurité "
					  "vous sera proposée avant d'appliquer les changements.",
					  Text::SMALL)
			.Parent(bn)
			.Spawn();
		WidgetBuilder tools = kit.Row(10.f);
		tools.GrowW().Parent(page);
		const ecs::Entity t = tools.Spawn();
		kit.Icon(MaterialIcons::SEARCH, 20.f).Parent(t).Spawn();
		WidgetBuilder search = kit.Input("Rechercher un mod…");
		search.OnTextChange([this](const String &q) {
			m_query = q.ToLower();
			Refresh();
		});
		const ecs::Entity se = search.Parent(t).Spawn();
		kit.SetInputText(se, m_query);
		kit.Combo({"Tous", "Activés", "Désactivés", "Problèmes", "Mises à jour"}, m_filter, [this](int i) {
			m_filter = i;
			Refresh();
		}).Parent(t).Spawn();
		kit.Button("Ajouter un mod…", [this] { shell.Push(MakeModBrowser(shell)); }).Parent(t).Spawn();
		WidgetBuilder list = kit.Scroll(8.f);
		list.GrowW().GrowH().Parent(page);
		m_list = list.Spawn();
		Refresh();
		const ecs::Entity footer = kit.Footer(page);
		m_summary = kit.Muted(String(), Text::SMALL).GrowW().TextColor(p.warning).Parent(footer).Spawn();
		kit.Button("Annuler", [this] { shell.Pop(); }).Parent(footer).Spawn();
		kit.PrimaryButton("Enregistrer les modifications", [this] { Apply(); }).Parent(footer).Spawn();
		UpdateSummary();
	}
	bool OnBack() override {
		Leave();
		return true;
	}

private:
	bool Enabled(const String &id) const { return std::find(m_enabled.begin(), m_enabled.end(), id) != m_enabled.end(); }

	void Refresh() {
		const Palette &p = kit.Colors();
		kit.ClearChildren(m_list);
		const WorldEntry &w = m_entry.Value();
		// Mods attendus par le monde mais absents de l'installation.
		std::vector<String> missing;
		for (const WorldIssue &i : w.issues)
			if (i.kind == WorldIssue::Kind::MISSING_MOD)
				missing.push_back(i.modId);
		if (!missing.empty() && m_filter != 1 && m_filter != 2) {
			kit.SectionTitle(m_list, "Mods manquants", Some(MaterialIcons::ERROR));
			for (const String &id : missing) {
				const ModEntry *e = shell.Mods().Find(id);
				WidgetBuilder btn = e && e->available.IsSome() ? kit.PrimaryButton("Installer depuis le catalogue", [this, id] {
					(void)shell.Mods().Install(id);
					m_entry = shell.Worlds().Find(m_folder, shell.Mods().Installed());
					shell.Rebuild();
				})
																: kit.Chip("Introuvable", Tone::DANGER);
				kit.SettingRow(m_list, id, "Requis par la dernière session de ce monde, mais non installé.", std::move(btn));
			}
		}
		kit.SectionTitle(m_list, "Mods installés", Some(MaterialIcons::EXTENSION));
		std::vector<voxel::ModInfo> enabledInfos;
		for (const voxel::ModInfo &m : shell.Mods().Installed())
			if (Enabled(m.id))
				enabledInfos.push_back(m);
		const auto conflicts = voxel::ModLoader::Conflicts(enabledInfos);
		for (const ModEntry &m : shell.Mods().Entries()) {
			if (!m.installed)
				continue;
			const voxel::ModInfo &info = m.info;
			const bool on = Enabled(info.id) || m.builtin;
			std::vector<String> missingDeps;
			for (const String &d : info.dependencies)
				if (!Enabled(d) && d != "core")
					missingDeps.push_back(d);
			bool conflict = false;
			for (const auto &[a, b] : conflicts)
				conflict = conflict || a == info.id || b == info.id;
			const bool problem = (on && (!missingDeps.empty() || conflict));
			if (m_filter == 1 && !on)
				continue;
			if (m_filter == 2 && on)
				continue;
			if (m_filter == 3 && !problem)
				continue;
			if (m_filter == 4 && !m.UpdateAvailable())
				continue;
			if (!m_query.IsEmpty() && !(info.name + String(" ") + info.id).ToLower().Contains(m_query))
				continue;
			WidgetBuilder row = kit.Row(12.f);
			row.GrowW().Pad(math::Sides(12.f, 10.f)).Radius(8.f).Bg(WithAlpha(p.bg2, on ? 0.9f : 0.45f));
			row.BorderColor(problem ? WithAlpha(p.danger, 0.7f) : p.line, 1.f).Parent(m_list);
			const ecs::Entity r = row.Spawn();
			ModIcon(shell, info, 40.f).Parent(r).Spawn();
			WidgetBuilder text = kit.Column(3.f);
			text.GrowW().Parent(r);
			const ecs::Entity tx = text.Spawn();
			kit.Label(shell.Prefs().ModName(info.id, info.name), Text::BODY).Bold().TextColor(on ? p.text : p.textMuted).Parent(tx).Spawn();
			WidgetBuilder chips = kit.Row(6.f);
			chips.Parent(tx);
			const ecs::Entity ch = chips.Spawn();
			kit.Chip(String("v") + info.version, Tone::NEUTRAL).Parent(ch).Spawn();
			kit.Chip(on ? "Activé pour ce monde" : "Désactivé pour ce monde", on ? Tone::SUCCESS : Tone::NEUTRAL).Parent(ch).Spawn();
			if (m.UpdateAvailable())
				kit.Chip(String::Format("Mise à jour v%s", m.available.Value().version.CStr()), Tone::ACCENT).Parent(ch).Spawn();
			if (on && !missingDeps.empty())
				kit.Chip(String("Dépendance manquante : ") + Join(missingDeps), Tone::DANGER).Parent(ch).Spawn();
			if (on && conflict)
				kit.Chip("Incompatible avec un mod activé", Tone::DANGER).Parent(ch).Spawn();
			if (m_showDeps == info.id)
				kit.Muted(String("Dépendances : ") + (info.dependencies.empty() ? String("aucune") : Join(info.dependencies)) +
							  String("   ·   facultatives : ") + (info.optionalDependencies.empty() ? String("aucune") : Join(info.optionalDependencies)),
						  Text::CAPTION)
					.Parent(tx)
					.Spawn();
			kit.IconButton(MaterialIcons::ACCOUNT_TREE, "Dépendances", [this, id = info.id] {
				m_showDeps = m_showDeps == id ? String() : id;
				Refresh();
			}).Parent(r).Spawn();
			WidgetBuilder cfg = kit.IconButton(MaterialIcons::TUNE, "Configuration du mod pour ce monde", [this, id = info.id] {
				shell.Push(MakeModConfig(shell, id, m_folder));
			});
			if (!json::At(info.manifest, "settings") || !on)
				cfg.Disabled();
			cfg.Parent(r).Spawn();
			if (m.builtin)
				kit.Chip("Requis", Tone::INFO).Parent(r).Spawn();
			else if (on)
				kit.DangerButton("Retirer", [this, id = info.id] {
					std::erase(m_enabled, id);
					Refresh();
					UpdateSummary();
				}).Parent(r).Spawn();
			else
				kit.Button("Ajouter", [this, info] {
					m_enabled.push_back(info.id);
					for (const String &d : info.dependencies) // dépendances activées avec lui
						if (!Enabled(d))
							m_enabled.push_back(d);
					Refresh();
					UpdateSummary();
				}).Parent(r).Spawn();
		}
	}

	ModChanges Diff() const {
		ModChanges c;
		c.worldFolder = m_folder;
		c.worldName = m_entry.Value().name;
		for (const String &id : m_enabled)
			if (std::find(m_original.begin(), m_original.end(), id) == m_original.end())
				if (const ModEntry *e = shell.Mods().Find(id))
					c.added.push_back(String::Format("%s %s", e->info.name.CStr(), e->info.version.CStr()));
		for (const String &id : m_original)
			if (std::find(m_enabled.begin(), m_enabled.end(), id) == m_enabled.end()) {
				const ModEntry *e = shell.Mods().Find(id);
				c.removed.push_back(e ? e->info.name : id);
				c.risks.push_back(String::Format("Les blocs et créatures « %s:* » déjà présents deviendront inconnus", id.CStr()));
			}
		for (const WorldIssue &i : m_entry.Value().issues)
			if (i.kind == WorldIssue::Kind::VERSION_CHANGED)
				c.updated.push_back(i.modId + String(" ") + i.detail);
		for (const String &id : m_enabled)
			if (const ModEntry *e = shell.Mods().Find(id))
				for (const String &d : e->info.dependencies)
					c.affectedDependencies.push_back(String::Format("%s requiert %s : %s", e->info.name.CStr(), d.CStr(),
																	std::find(m_enabled.begin(), m_enabled.end(), d) != m_enabled.end() || d == "core"
																		? "actif"
																		: "MANQUANT"));
		return c;
	}

	void UpdateSummary() {
		const ModChanges c = Diff();
		kit.SetText(m_summary, c.added.empty() && c.removed.empty()
								   ? String()
								   : String::Format("%d ajout(s), %d retrait(s) en attente", int(c.added.size()), int(c.removed.size())));
	}

	void Apply() {
		ModChanges c = Diff();
		auto write = [this](bool backup) {
			if (backup)
				if (Result<String, String> b = shell.Worlds().Backup(m_folder); b.IsError()) {
					shell.Notify(Tone::DANGER, "Sauvegarde de sécurité impossible", b.Error(), true);
					return;
				}
			dataio::NodePtr settings = m_entry.Value().settings->Clone();
			dataio::NodePtr mods = json::At(settings, "mods");
			if (!mods || !mods->IsObject()) {
				mods = dataio::Node::MakeObject();
				settings->Set("mods", mods);
			}
			dataio::NodePtr list = dataio::Node::MakeArray();
			for (const String &id : m_enabled)
				list->Push(json::MakeStr(id));
			mods->Set("enabled", list);
			Result<bool, String> r = shell.Worlds().Update(m_folder, NONE, settings);
			if (r.IsError()) {
				shell.Notify(Tone::DANGER, "Échec de l'enregistrement", r.Error(), true);
				return;
			}
			m_original = m_enabled;
			shell.Notify(Tone::SUCCESS, "Mods du monde enregistrés", "Ils seront chargés à la prochaine ouverture du monde.");
			shell.Pop();
		};
		if (c.added.empty() && c.removed.empty()) {
			shell.Pop();
			return;
		}
		if (!shell.Prefs().confirmModChanges) {
			write(false);
			return;
		}
		shell.Push(MakeModChanges(shell, std::move(c), write));
	}

	void Leave() {
		const ModChanges c = Diff();
		if (c.added.empty() && c.removed.empty()) {
			shell.Pop();
			return;
		}
		ConfirmSpec spec;
		spec.title = "Abandonner les changements de mods ?";
		spec.message = "Les ajouts et retraits en attente ne seront pas appliqués à ce monde.";
		spec.confirmText = "Abandonner";
		spec.cancelText = "Continuer";
		spec.tone = Tone::WARNING;
		spec.onConfirm = [this] { shell.Pop(); };
		shell.Confirm(std::move(spec));
	}

	String m_folder, m_query, m_showDeps;
	Option<WorldEntry> m_entry = NONE;
	std::vector<String> m_enabled, m_original;
	ecs::Entity m_list{}, m_summary{};
	int m_filter = 0;
};

// ── Configuration d'un mod ───────────────────────────────────────────────────

class ModConfig final : public Screen {
public:
	ModConfig(Shell &shell, String id, String folder) : Screen(shell), m_id(std::move(id)), m_folder(std::move(folder)) {
		if (!m_folder.IsEmpty())
			if (Option<WorldEntry> w = shell.Worlds().Find(m_folder, {}); w.IsSome()) {
				m_worldName = w.Value().name;
				m_worldSettings = w.Value().settings;
			}
		dataio::NodePtr source = m_folder.IsEmpty() ? shell.Prefs().modSettings : json::At(m_worldSettings, "modSettings");
		m_values = json::At(source, m_id.CStr());
		m_values = m_values ? m_values->Clone() : dataio::Node::MakeObject();
	}
	String Id() const override { return "modconfig"; }

	void Build(ecs::Entity root) override {
		const Palette &p = kit.Colors();
		const ecs::Entity page = Page(kit, root, 940.f);
		const ModEntry *m = shell.Mods().Find(m_id);
		if (!m) {
			kit.ScreenHeader(page, "Mod introuvable", m_id, [this] { shell.Pop(); });
			return;
		}
		kit.ScreenHeader(page, String("Configuration — ") + shell.Prefs().ModName(m_id, m->info.name),
						 m_folder.IsEmpty() ? String("Valeurs par défaut pour tous les mondes")
											: String::Format("Pour le monde « %s » uniquement", m_worldName.CStr()),
						 [this] { shell.Pop(); });
		WidgetBuilder scroll = kit.Scroll(8.f);
		scroll.GrowW().GrowH().Parent(page);
		const ecs::Entity c = scroll.Spawn();
		dataio::NodePtr schema = json::At(m->info.manifest, "settings");
		if (!schema || !schema->IsArray()) {
			kit.Muted("Ce mod ne propose aucun réglage.", Text::BODY).Parent(c).Spawn();
		} else {
			// Groupes dans l'ordre d'apparition ; les réglages « advanced » dans un volet repliable.
			std::vector<String> groups;
			for (size_t i = 0; i < schema->GetSize(); ++i) {
				const String g = json::Str(schema->At(i), "group", "Général");
				if (std::find(groups.begin(), groups.end(), g) == groups.end())
					groups.push_back(g);
			}
			for (const String &g : groups) {
				kit.SectionTitle(c, g, NONE);
				for (size_t i = 0; i < schema->GetSize(); ++i)
					if (json::Str(schema->At(i), "group", "Général") == g && !json::Bool(schema->At(i), "advanced", false))
						Control(c, schema->At(i));
			}
			bool anyAdvanced = false;
			for (size_t i = 0; i < schema->GetSize(); ++i)
				anyAdvanced = anyAdvanced || json::Bool(schema->At(i), "advanced", false);
			if (anyAdvanced) {
				WidgetBuilder exp = f.Expander("Paramètres avancés", m_advanced);
				exp.GrowW().HAuto().OnToggle([this](bool v) { m_advanced = v; }).Parent(c);
				const ecs::Entity e = exp.Spawn();
				WidgetBuilder col = kit.Column(8.f);
				col.GrowW().Pad(math::Sides(0.f, 8.f)).Parent(e);
				const ecs::Entity ce = col.Spawn();
				for (size_t i = 0; i < schema->GetSize(); ++i)
					if (json::Bool(schema->At(i), "advanced", false))
						Control(ce, schema->At(i));
			}
		}
		const ecs::Entity footer = kit.Footer(page);
		kit.Muted("Les changements s'appliquent au prochain chargement du monde.", Text::CAPTION).GrowW().TextColor(p.textDim).Parent(footer).Spawn();
		kit.GhostButton("Valeurs par défaut", [this] {
			m_values = dataio::Node::MakeObject();
			shell.Rebuild();
		}).Parent(footer).Spawn();
		kit.Button("Annuler", [this] { shell.Pop(); }).Parent(footer).Spawn();
		kit.PrimaryButton("Enregistrer", [this] { Save(); }).Parent(footer).Spawn();
	}

private:
	dataio::NodePtr Value(const dataio::NodePtr &item) const {
		const String id = json::Str(item, "id");
		if (dataio::NodePtr v = json::At(m_values, id.CStr()))
			return v;
		return json::At(item, "default");
	}

	void Control(ecs::Entity parent, const dataio::NodePtr &item) {
		const String id = json::Str(item, "id");
		const String type = json::Str(item, "type", "bool");
		const String label = json::Str(item, "label", id);
		const String help = json::Str(item, "description");
		dataio::NodePtr value = Value(item);
		if (type == "bool" || type == "switch") {
			kit.ToggleRow(parent, label, help, value && value->IsBool() && value->boolValue, [this, id](bool v) { m_values->Set(id, json::MakeBool(v)); });
		} else if (type == "check") {
			kit.SettingRow(parent, label, help, kit.Check(value && value->IsBool() && value->boolValue, [this, id](bool v) {
				m_values->Set(id, json::MakeBool(v));
			}));
		} else if (type == "int" || type == "float" || type == "slider") {
			const float min = float(json::Num(item, "min", 0.0)), max = float(json::Num(item, "max", 100.0));
			const float step = float(json::Num(item, "step", type == "int" ? 1.0 : 0.0));
			const String unit = json::Str(item, "unit");
			const float v = float(json::Num(value, "", json::Num(item, "default", double(min))));
			kit.SliderRow(parent, label, help, min, max, v, step,
						  [type, unit](float x) {
							  return (type == "int" ? String::Format("%d", int(std::lround(x))) : String::Format("%.2f", double(x))) +
									 (unit.IsEmpty() ? String() : String(" ") + unit);
						  },
						  [this, id](float x) { m_values->Set(id, json::MakeNum(x)); });
		} else if (type == "choice") {
			const std::vector<String> options = json::StrList(item, "options");
			const String current = value && value->IsString() ? value->stringValue : String();
			int selected = 0;
			for (size_t i = 0; i < options.size(); ++i)
				if (options[i] == current)
					selected = int(i);
			kit.ComboRow(parent, label, help, options, selected, [this, id, options](int i) { m_values->Set(id, json::MakeStr(options[size_t(i)])); });
		} else if (type == "string") {
			WidgetBuilder in = kit.Input(json::Str(item, "placeholder"));
			in.W(Dimension::Px(300.f)).OnTextChange([this, id](const String &t) { m_values->Set(id, json::MakeStr(t)); });
			kit.SettingRow(parent, label, help, std::move(in));
			kit.SetInputText(kit.LastControl(), value && value->IsString() ? value->stringValue : String());
		} else if (type == "action") {
			// Préréglage : applique d'un coup plusieurs valeurs (« values »).
			dataio::NodePtr preset = json::At(item, "values");
			kit.SettingRow(parent, label, help, kit.Button(json::Str(item, "button", "Appliquer"), [this, preset] {
				if (preset && preset->IsObject())
					for (const String &k : preset->Keys())
						m_values->Set(k, preset->Get(k)->Clone());
				shell.Notify(Tone::INFO, "Préréglage appliqué");
				shell.Rebuild();
			}));
		}
	}

	void Save() {
		if (m_folder.IsEmpty()) {
			if (!shell.Prefs().modSettings || !shell.Prefs().modSettings->IsObject())
				shell.Prefs().modSettings = dataio::Node::MakeObject();
			shell.Prefs().modSettings->Set(m_id, m_values->Clone());
			shell.SaveSettings();
		} else {
			dataio::NodePtr settings = m_worldSettings ? m_worldSettings->Clone() : dataio::Node::MakeObject();
			dataio::NodePtr all = json::At(settings, "modSettings");
			if (!all || !all->IsObject()) {
				all = dataio::Node::MakeObject();
				settings->Set("modSettings", all);
			}
			all->Set(m_id, m_values->Clone());
			if (Result<bool, String> r = shell.Worlds().Update(m_folder, NONE, settings); r.IsError()) {
				shell.Notify(Tone::DANGER, "Échec de l'enregistrement", r.Error(), true);
				return;
			}
		}
		shell.UiSound("ui_save");
		shell.Notify(Tone::SUCCESS, "Configuration enregistrée", m_id);
		shell.Pop();
	}

	String m_id, m_folder, m_worldName;
	dataio::NodePtr m_worldSettings, m_values;
	bool m_advanced = false;
};

// ── Organisation des mods ────────────────────────────────────────────────────

class ModOrganize final : public Screen {
public:
	explicit ModOrganize(Shell &shell) : Screen(shell) {
		shell.Mods().Refresh();
		m_order = shell.Prefs().modOrder;
		for (const voxel::ModInfo &m : shell.Mods().Installed())
			if (std::find(m_order.begin(), m_order.end(), m.id) == m_order.end())
				m_order.push_back(m.id);
		std::erase_if(m_order, [&](const String &id) { return !shell.Mods().Find(id) || !shell.Mods().Find(id)->installed; });
		m_names = shell.Prefs().modNames;
		m_categories = shell.Prefs().modCategories;
	}
	String Id() const override { return "organize"; }

	void Build(ecs::Entity root) override {
		const Palette &p = kit.Colors();
		const ecs::Entity page = Page(kit, root, 1140.f);
		kit.ScreenHeader(page, "Organiser les mods",
						 "Nom d'affichage et catégorie personnalisés, ordre de chargement. L'identifiant interne d'un mod ne change jamais.",
						 [this] { shell.Pop(); });
		// Ordre effectif (les dépendances passent toujours d'abord).
		std::vector<voxel::ModInfo> infos;
		for (const String &id : m_order)
			if (const ModEntry *e = shell.Mods().Find(id))
				infos.push_back(e->info);
		std::vector<String> errors;
		const std::vector<voxel::ModInfo> effective = voxel::ModLoader::Order(infos, errors, m_order);
		const auto conflicts = voxel::ModLoader::Conflicts(infos);
		if (!conflicts.empty() || !errors.empty()) {
			WidgetBuilder banner = kit.Column(4.f);
			banner.GrowW().Pad(math::Sides(12.f, 10.f)).Radius(8.f).Bg(WithAlpha(p.danger, 0.12f)).BorderColor(WithAlpha(p.danger, 0.5f), 1.f);
			banner.Parent(page);
			const ecs::Entity bn = banner.Spawn();
			kit.Label("Conflits potentiels", Text::SMALL).Bold().TextColor(p.danger).Parent(bn).Spawn();
			for (const auto &[a, b] : conflicts)
				kit.Muted(String::Format("« %s » et « %s » sont déclarés incompatibles", a.CStr(), b.CStr()), Text::SMALL).Parent(bn).Spawn();
			for (const String &e : errors)
				kit.Muted(e, Text::SMALL).Parent(bn).Spawn();
		}
		WidgetBuilder head = kit.Row(10.f);
		head.GrowW().Pad(math::Sides(12.f, 0.f)).Parent(page);
		const ecs::Entity hd = head.Spawn();
		kit.Muted("ORDRE", Text::CAPTION).W(Dimension::Px(90.f)).Parent(hd).Spawn();
		kit.Muted("NOM D'AFFICHAGE  /  IDENTIFIANT INTERNE", Text::CAPTION).GrowW().Parent(hd).Spawn();
		kit.Muted("CATÉGORIE", Text::CAPTION).W(Dimension::Px(230.f)).Parent(hd).Spawn();
		WidgetBuilder list = kit.Scroll(6.f);
		list.GrowW().GrowH().Parent(page);
		const ecs::Entity l = list.Spawn();
		std::vector<String> catNames;
		for (const auto &[id, name] : ModCatalog::Categories())
			catNames.push_back(name);
		for (size_t i = 0; i < m_order.size(); ++i) {
			const ModEntry *e = shell.Mods().Find(m_order[i]);
			if (!e)
				continue;
			const voxel::ModInfo &info = e->info;
			int position = 0;
			for (size_t k = 0; k < effective.size(); ++k)
				if (effective[k].id == info.id)
					position = int(k) + 1;
			WidgetBuilder row = kit.Row(10.f);
			row.GrowW().Pad(math::Sides(12.f, 8.f)).Radius(8.f).Bg(p.bg2).BorderColor(p.line, 1.f).Parent(l);
			const ecs::Entity r = row.Spawn();
			WidgetBuilder order = kit.Row(2.f);
			order.W(Dimension::Px(90.f)).Parent(r);
			const ecs::Entity o = order.Spawn();
			kit.Label(String::Format("%d", position), Text::H3).W(Dimension::Px(26.f)).Parent(o).Spawn();
			WidgetBuilder up = kit.IconButton(MaterialIcons::ARROW_UPWARD, "Monter", [this, i] { Move(i, -1); });
			if (i == 0)
				up.Disabled();
			up.Parent(o).Spawn();
			WidgetBuilder down = kit.IconButton(MaterialIcons::ARROW_DOWNWARD, "Descendre", [this, i] { Move(i, 1); });
			if (i + 1 >= m_order.size())
				down.Disabled();
			down.Parent(o).Spawn();
			WidgetBuilder names = kit.Column(3.f);
			names.GrowW().Parent(r);
			const ecs::Entity n = names.Spawn();
			WidgetBuilder input = kit.Input(info.name);
			input.OnTextChange([this, id = info.id](const String &t) { m_names[id.CStr()] = t.Trim(); });
			const ecs::Entity in = input.Parent(n).Spawn();
			auto it = m_names.find(info.id.CStr());
			kit.SetInputText(in, it != m_names.end() ? it->second : String());
			String deps = info.dependencies.empty() ? String("aucune dépendance") : String("requiert ") + Join(info.dependencies);
			kit.Muted(String::Format("%s  ·  nom d'origine « %s »  ·  %s", info.id.CStr(), info.name.CStr(), deps.CStr()), Text::CAPTION)
				.Parent(n)
				.Spawn();
			int cat = 0;
			const String current = m_categories.contains(info.id.CStr()) ? m_categories[info.id.CStr()] : e->Category();
			for (size_t k = 0; k < ModCatalog::Categories().size(); ++k)
				if (ModCatalog::Categories()[k].first == current)
					cat = int(k);
			kit.Combo(catNames, cat, [this, id = info.id](int k) { m_categories[id.CStr()] = ModCatalog::Categories()[size_t(k)].first; })
				.Parent(r)
				.Spawn();
		}
		kit.Muted("L'ordre choisi s'applique aux nouveaux mondes et aux mondes sans ordre propre ; un mod est toujours chargé après ses dépendances.",
				  Text::CAPTION)
			.GrowW()
			.TextWrap()
			.Parent(page)
			.Spawn();
		const ecs::Entity footer = kit.Footer(page);
		kit.GhostButton("Restaurer les paramètres d'origine", [this] {
			m_names.clear();
			m_categories.clear();
			m_order.clear();
			for (const voxel::ModInfo &m : shell.Mods().Installed())
				m_order.push_back(m.id);
			shell.Rebuild();
		}).Parent(footer).Spawn();
		kit.Spacer().Parent(footer).Spawn();
		kit.Button("Annuler", [this] { shell.Pop(); }).Parent(footer).Spawn();
		kit.PrimaryButton("Enregistrer", [this] {
			shell.Prefs().modOrder = m_order;
			shell.Prefs().modNames = m_names;
			shell.Prefs().modCategories = m_categories;
			shell.SaveSettings();
			shell.Notify(Tone::SUCCESS, "Organisation des mods enregistrée");
			shell.Pop();
		}).Parent(footer).Spawn();
	}

private:
	void Move(size_t i, int step) {
		const size_t j = size_t(int(i) + step);
		if (j < m_order.size())
			std::swap(m_order[i], m_order[j]);
		shell.Rebuild();
	}
	std::vector<String> m_order;
	std::map<std::string, String> m_names, m_categories;
};

// ── Confirmation des changements de mods ────────────────────────────────────

class ModChangesScreen final : public Screen {
public:
	ModChangesScreen(Shell &shell, ModChanges changes, std::function<void(bool)> apply)
		: Screen(shell), m_changes(std::move(changes)), m_apply(std::move(apply)) {}
	String Id() const override { return "modchanges"; }
	bool Overlay() const override { return true; }

	void Build(ecs::Entity root) override {
		const Palette &p = kit.Colors();
		const ecs::Entity card = ModalCard(kit, root, 620.f);
		WidgetBuilder head = kit.Row(12.f);
		head.Parent(card);
		const ecs::Entity h = head.Spawn();
		kit.Icon(MaterialIcons::EXTENSION, 28.f, Some(p.primary)).Parent(h).Spawn();
		WidgetBuilder ht = kit.Column(2.f);
		ht.GrowW().Parent(h);
		const ecs::Entity htt = ht.Spawn();
		kit.Label("Appliquer les changements de mods ?", Text::H2).Parent(htt).Spawn();
		kit.Muted(String::Format("Monde « %s »", m_changes.worldName.CStr()), Text::SMALL).Parent(htt).Spawn();
		auto section = [&](const char *title, const std::vector<String> &items, MaterialIcons icon, sdl3::FColor color) {
			if (items.empty())
				return;
			WidgetBuilder box = kit.Column(4.f);
			box.GrowW().Pad(math::Sides(12.f, 8.f)).Radius(8.f).Bg(p.bg2).Parent(card);
			const ecs::Entity b = box.Spawn();
			WidgetBuilder t = kit.Row(6.f);
			t.Parent(b);
			const ecs::Entity tt = t.Spawn();
			kit.Icon(icon, 16.f, Some(color)).Parent(tt).Spawn();
			kit.Label(String::Format("%s (%d)", title, int(items.size())), Text::SMALL).Bold().TextColor(color).Parent(tt).Spawn();
			for (const String &i : items)
				kit.Label(String("• ") + i, Text::SMALL).TextColor(p.textMuted).Parent(b).Spawn();
		};
		section("Mods ajoutés", m_changes.added, MaterialIcons::ADD, p.success);
		section("Mods retirés", m_changes.removed, MaterialIcons::REMOVE, p.danger);
		section("Mods mis à jour", m_changes.updated, MaterialIcons::UPDATE, p.accent);
		section("Dépendances concernées", m_changes.affectedDependencies, MaterialIcons::ACCOUNT_TREE, p.info);
		section("Risques de compatibilité", m_changes.risks, MaterialIcons::WARNING, p.warning);
		kit.Paragraph("Conséquences sur la sauvegarde : le monde sera rechargé avec ces mods à sa prochaine ouverture. "
					  "Rien n'est effacé tant que vous n'enregistrez pas la partie ; une sauvegarde de sécurité permet de revenir en arrière.",
					  Text::SMALL)
			.Parent(card)
			.Spawn();
		WidgetBuilder row = kit.Row(10.f);
		row.Parent(card);
		const ecs::Entity r = row.Spawn();
		kit.Check(m_backup, [this](bool v) { m_backup = v; }).Parent(r).Spawn();
		kit.Label("Créer une sauvegarde de sécurité avant d'appliquer (recommandé)", Text::SMALL).Parent(r).Spawn();
		WidgetBuilder buttons = kit.Row(10.f);
		buttons.GrowW().Justify(ui::Justify::End).Parent(card);
		const ecs::Entity bt = buttons.Spawn();
		kit.Button("Annuler", [this] { shell.Pop(); }).Parent(bt).Spawn();
		kit.PrimaryButton("Appliquer les modifications", [this] {
			auto apply = m_apply;
			const bool backup = m_backup;
			shell.Pop();
			if (apply)
				apply(backup);
		}).Parent(bt).Spawn();
	}

private:
	ModChanges m_changes;
	std::function<void(bool)> m_apply;
	bool m_backup = true;
};

} // namespace

std::unique_ptr<Screen> MakeModBrowser(Shell &shell) { return std::make_unique<ModBrowser>(shell); }
std::unique_ptr<Screen> MakeModDetails(Shell &shell, String id) { return std::make_unique<ModDetails>(shell, std::move(id)); }
std::unique_ptr<Screen> MakeWorldMods(Shell &shell, String folder) { return std::make_unique<WorldMods>(shell, std::move(folder)); }
std::unique_ptr<Screen> MakeModConfig(Shell &shell, String modId, String worldFolder) {
	return std::make_unique<ModConfig>(shell, std::move(modId), std::move(worldFolder));
}
std::unique_ptr<Screen> MakeModOrganize(Shell &shell) { return std::make_unique<ModOrganize>(shell); }
std::unique_ptr<Screen> MakeModChanges(Shell &shell, ModChanges changes, std::function<void(bool)> apply) {
	return std::make_unique<ModChangesScreen>(shell, std::move(changes), std::move(apply));
}

} // namespace voxelgame
