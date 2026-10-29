// Écrans des mods : navigateur, fiche, mods d'un monde, configuration,
// organisation, confirmation des changements.
#include "common.hpp"

#include <algorithm>

namespace voxelia {

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
	explicit ModBrowser(Shell &shell_) : Screen(shell_) {}
	String Id() const override { return "mods"; }

	void Build(ui::Widget root) override {
		const float s = kit.Scale();
		const Palette &p = kit.Colors();
		shell.Mods().Refresh();
		ui::Widget page = Page(kit, root, 1240.f);
		int installed = 0, available = 0, updates = 0;
		for (const ModEntry &m : shell.Mods().Entries()) {
			installed += m.installed;
			available += !m.installed;
			updates += m.UpdateAvailable();
		}
		ui::Widget header = kit.ScreenHeader(page, "Mods",
													String::Format("%d installé(s) · %d disponible(s) · %d mise(s) à jour", installed, available, updates),
													[this] { shell.Pop(); });
		header.Append(kit.Button("Organiser", [this] { shell.Push(MakeModOrganize(shell)); }));
		WidgetBuilder body = kit.Row(18.f);
		body.GrowWidth().GrowHeight().AlignItems(ui::Align::Stretch);
		ui::Widget b = page.Append(body);
		// Barre latérale : catégories.
		WidgetBuilder side = kit.Column(2.f);
		side.Width(250.f * s);
		ui::Widget sd = b.Append(side);
		sd.Append(kit.Label("CATÉGORIES", Text::CAPTION).Color(p.textDim).Bold());
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
			item.GrowWidth().Height(32.f * s).Padding(math::Sides(10.f * s, 0.f)).BorderRadius(6.f);
			item.Background(on ? WithAlpha(p.primary, 0.18f) : WithAlpha(p.bg3, 0.f)).HoverStyle(ui::Style().Background(p.bg3));
			item.OnClick([this, id] {
				m_category = id;
				shell.Rebuild();
			});
			ui::Widget it = sd.Append(item);
			it.Append(kit.Label(name, Text::SMALL).Color(on ? p.text : p.textMuted).GrowWidth().PointerThrough());
			it.Append(kit.Muted(String::Format("%d", count(id)), Text::CAPTION).PointerThrough());
		}
		// Contenu : recherche, filtres, tri, cartes.
		WidgetBuilder main = kit.Column(10.f);
		main.GrowWidth().GrowHeight();
		ui::Widget mn = b.Append(main);
		WidgetBuilder tools = kit.Row(10.f);
		tools.GrowWidth();
		ui::Widget t = mn.Append(tools);
		t.Append(kit.Icon(MaterialIcons::SEARCH, 20.f));
		WidgetBuilder search = kit.Input("Rechercher (nom, auteur, mot-clé)…");
		search.OnTextChange([this](const String &q) {
			m_query = q.ToLower();
			Refresh();
		});
		ui::Widget se = t.Append(search);
		kit.SetInputText(se, m_query);
		t.Append(kit.Combo({"Tous", "Installés", "Disponibles", "Mises à jour", "Compatibles"}, m_filter, [this](int i) {
			m_filter = i;
			Refresh();
		}));
		t.Append(kit.Combo({"Nom", "Catégorie", "Auteur", "Installés d'abord"}, m_sort, [this](int i) {
			m_sort = i;
			Refresh();
		}));
		WidgetBuilder list = kit.Scroll(10.f);
		list.GrowWidth().GrowHeight();
		m_list = mn.Append(list);
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
			m_list.Append(kit.Muted("Aucun mod ne correspond à ces critères.", Text::BODY));
		// Deux cartes par ligne.
		ui::Widget row{};
		for (size_t i = 0; i < shown.size(); ++i) {
			if (i % 2 == 0) {
				WidgetBuilder r = kit.Row(10.f);
				r.GrowWidth().AlignItems(ui::Align::Stretch);
				row = m_list.Append(r);
			}
			Card(row, *shown[i]);
		}
		if (shown.size() % 2 == 1)
			row.Append(kit.Spacer());
	}

	void Card(ui::Widget parent, const ModEntry &m) {
		const Palette &p = kit.Colors();
		const float s = kit.Scale();
		WidgetBuilder card = kit.Column(8.f);
		card.GrowWidth().Padding(math::Sides(14.f * s, 12.f * s)).BorderRadius(10.f * s);
		card.Background(p.bg2).BorderColor(p.line).BorderWidth(1.f);
		card.HoverStyle(ui::Style().Background(p.bg3).BorderColor(p.lineStrong));
		card.OnDoubleClick([this, id = m.info.id] { shell.Push(MakeModDetails(shell, id)); });
		ui::Widget c = parent.Append(card);
		WidgetBuilder top = kit.Row(12.f);
		top.GrowWidth().PointerThrough();
		ui::Widget t = c.Append(top);
		t.Append(ModIcon(shell, m.info, 52.f));
		WidgetBuilder head = kit.Column(2.f);
		head.GrowWidth().PointerThrough();
		ui::Widget h = t.Append(head);
		h.Append(kit.Label(shell.Prefs().ModName(m.info.id, m.info.name), Text::H3).PointerThrough());
		h.Append(kit.Muted(String::Format("par %s · v%s%s", m.info.author.IsEmpty() ? "inconnu" : m.info.author.CStr(), m.info.version.CStr(),
								 m.UpdateAvailable() ? String::Format(" → v%s", m.available.Value().version.CStr()).CStr() : ""),
				  Text::CAPTION)
			.PointerThrough());
		WidgetBuilder desc = kit.Paragraph(m.info.description, Text::SMALL);
		c.Append(desc.Height(36.f * s).Ellipsis().PointerThrough());
		WidgetBuilder chips = kit.Row(6.f);
		chips.GrowWidth().PointerThrough();
		ui::Widget ch = c.Append(chips);
		ch.Append(kit.Chip(ModCatalog::CategoryName(CategoryOf(shell, m)), Tone::INFO));
		if (m.builtin)
			ch.Append(kit.Chip("Intégré", Tone::NEUTRAL));
		else if (m.installed)
			ch.Append(kit.Chip("Installé · activé", Tone::SUCCESS));
		else
			ch.Append(kit.Chip("Disponible", Tone::NEUTRAL));
		if (m.UpdateAvailable())
			ch.Append(kit.Chip("Mise à jour", Tone::ACCENT));
		if (!m.info.gameVersion.IsEmpty())
			ch.Append(kit.Chip(String("Jeu ") + m.info.gameVersion, shell.Version().StartsWith(m.info.gameVersion.CStr()) ? Tone::SUCCESS : Tone::WARNING));
		if (!m.info.dependencies.empty())
			c.Append(kit.Muted(String("Requiert : ") + Join(m.info.dependencies), Text::CAPTION).PointerThrough());
		if (!m.info.incompatibilities.empty())
			c.Append(kit.Muted(String("Incompatible avec : ") + Join(m.info.incompatibilities), Text::CAPTION).Color(p.warning).PointerThrough());
		WidgetBuilder actions = kit.Row(8.f);
		actions.GrowWidth().JustifyContent(ui::Justify::End);
		ui::Widget a = c.Append(actions);
		a.Append(kit.GhostButton("Détails", [this, id = m.info.id] { shell.Push(MakeModDetails(shell, id)); }));
		if (m.UpdateAvailable())
			a.Append(kit.PrimaryButton("Mettre à jour", [this, id = m.info.id] { Install(id); }));
		if (!m.installed)
			a.Append(kit.PrimaryButton("Installer", [this, id = m.info.id] { Install(id); }));
		else if (!m.builtin)
			a.Append(kit.DangerButton("Désinstaller", [this, id = m.info.id, name = m.info.name] { Uninstall(id, name); }));
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

	ui::Widget m_list{};
	String m_query, m_category;
	int m_filter = 0, m_sort = 0;
};

// ── Fiche d'un mod ───────────────────────────────────────────────────────────

class ModDetails final : public Screen {
public:
	ModDetails(Shell &shell_, String id) : Screen(shell_), m_id(std::move(id)) {}
	String Id() const override { return "moddetails"; }

	void Build(ui::Widget root) override {
		const Palette &p = kit.Colors();
		const float s = kit.Scale();
		ui::Widget page = Page(kit, root, 1100.f);
		const ModEntry *m = shell.Mods().Find(m_id);
		if (!m) {
			kit.ScreenHeader(page, "Mod introuvable", m_id, [this] { shell.Pop(); });
			return;
		}
		const voxel::ModInfo &info = m->info;
		// En-tête : illustration, nom, auteur, versions, actions.
		WidgetBuilder head = kit.Row(18.f);
		head.GrowWidth();
		ui::Widget h = page.Append(head);
		h.Append(kit.IconButton(MaterialIcons::ARROW_BACK, "Retour", [this] { shell.Pop(); }));
		h.Append(ModIcon(shell, info, 84.f));
		WidgetBuilder title = kit.Column(4.f);
		title.GrowWidth();
		ui::Widget tt = h.Append(title);
		tt.Append(kit.Label(shell.Prefs().ModName(info.id, info.name), Text::H1));
		tt.Append(kit.Muted(String::Format("par %s · identifiant « %s »", info.author.IsEmpty() ? "inconnu" : info.author.CStr(), info.id.CStr()), Text::SMALL));
		WidgetBuilder chips = kit.Row(6.f);
		ui::Widget ch = tt.Append(chips);
		ch.Append(kit.Chip(ModCatalog::CategoryName(CategoryOf(shell, *m)), Tone::INFO));
		ch.Append(kit.Chip(m->installed ? String::Format("Installé : v%s", info.version.CStr()) : String("Non installé"),
				 m->installed ? Tone::SUCCESS : Tone::NEUTRAL));
		if (m->available.IsSome())
			ch.Append(kit.Chip(String::Format("Dernière version : v%s", m->available.Value().version.CStr()), m->UpdateAvailable() ? Tone::ACCENT : Tone::NEUTRAL));
		WidgetBuilder actions = kit.Column(8.f);
		ui::Widget a = h.Append(actions);
		if (!m->installed)
			a.Append(kit.PrimaryButton("Installer", [this] { Do(shell.Mods().Install(m_id), "Mod installé"); }));
		if (m->UpdateAvailable())
			a.Append(kit.PrimaryButton("Mettre à jour", [this] { Do(shell.Mods().Install(m_id), "Mod mis à jour"); }));
		if (m->installed && json::At(info.manifest, "settings"))
			a.Append(kit.Button("Configurer", [this] { shell.Push(MakeModConfig(shell, m_id, String())); }));
		if (m->installed && !m->builtin)
			a.Append(kit.DangerButton("Désinstaller", [this] { Do(shell.Mods().Uninstall(m_id), "Mod désinstallé"); }));

		WidgetBuilder body = kit.Row(18.f);
		body.GrowWidth().GrowHeight().AlignItems(ui::Align::Stretch);
		ui::Widget b = page.Append(body);
		WidgetBuilder left = kit.Scroll(10.f);
		left.GrowWidth().GrowHeight();
		ui::Widget l = b.Append(left);
		kit.SectionTitle(l, "Description", Some(MaterialIcons::INFO));
		l.Append(kit.Paragraph(json::Str(info.manifest, "longDescription", info.description), Text::BODY));
		if (!info.screenshots.empty()) {
			kit.SectionTitle(l, "Captures d'écran", NONE);
			WidgetBuilder shots = kit.Row(10.f);
			shots.GrowWidth();
			ui::Widget sh = l.Append(shots);
			for (const String &path : info.screenshots)
				if (const String key = ImageKey(shell, path); !key.IsEmpty()) {
					WidgetBuilder img = ui::Image(key);
					img.Size(220.f * s, 124.f * s);
					sh.Append(img.ObjectFit(ui::ObjectFit::Cover).BorderRadius(6.f));
				}
		}
		kit.SectionTitle(l, "Journal des modifications", Some(MaterialIcons::SCHEDULE));
		if (dataio::NodePtr log = json::At(info.manifest, "changelog"); log && log->IsArray() && log->GetSize() > 0) {
			for (size_t i = 0; i < log->GetSize(); ++i) {
				dataio::NodePtr e = log->At(i);
				WidgetBuilder entry = kit.Column(3.f);
				entry.GrowWidth().Padding(math::Sides(12.f, 8.f)).BorderRadius(8.f).Background(p.bg2);
				ui::Widget en = l.Append(entry);
				en.Append(kit.Label(String::Format("v%s — %s", json::Str(e, "version").CStr(), json::Str(e, "date").CStr()), Text::SMALL).Bold());
				for (const String &change : json::StrList(e, "changes"))
					en.Append(kit.Paragraph(String("• ") + change, Text::SMALL));
			}
		} else {
			l.Append(kit.Muted("Aucun journal fourni par l'auteur.", Text::SMALL));
		}
		WidgetBuilder right = kit.Scroll(8.f);
		right.Width(360.f * s).GrowHeight();
		ui::Widget r = b.Append(right);
		kit.SectionTitle(r, "Dépendances", Some(MaterialIcons::EXTENSION));
		if (info.dependencies.empty())
			r.Append(kit.Muted("Aucune", Text::SMALL));
		for (const String &dep : info.dependencies) {
			const ModEntry *d = shell.Mods().Find(dep);
			InfoRow(kit, r, dep, d && d->installed ? String("installée") : String("manquante"), Some(d && d->installed ? p.success : p.danger));
		}
		for (const String &dep : info.optionalDependencies)
			InfoRow(kit, r, dep, String("facultative"));
		kit.SectionTitle(r, "Incompatibilités connues", Some(MaterialIcons::WARNING));
		if (info.incompatibilities.empty())
			r.Append(kit.Muted("Aucune", Text::SMALL));
		for (const String &other : info.incompatibilities) {
			const ModEntry *o = shell.Mods().Find(other);
			InfoRow(kit, r, other, o && o->installed ? String("installé : conflit") : String("non installé"),
					Some(o && o->installed ? p.danger : p.textMuted));
		}
		kit.SectionTitle(r, "Mondes utilisant ce mod", Some(MaterialIcons::PUBLIC));
		const std::vector<String> worlds = WorldsUsing(shell, m_id);
		if (worlds.empty())
			r.Append(kit.Muted("Aucun", Text::SMALL));
		for (const String &w : worlds)
			r.Append(kit.Label(w, Text::SMALL));
		kit.SectionTitle(r, "Paramètres configurables", Some(MaterialIcons::TUNE));
		dataio::NodePtr settings = json::At(info.manifest, "settings");
		if (!settings || !settings->IsArray() || settings->GetSize() == 0)
			r.Append(kit.Muted("Aucun", Text::SMALL));
		for (size_t i = 0; settings && i < settings->GetSize(); ++i)
			r.Append(kit.Muted(String("• ") + json::Str(settings->At(i), "label", json::Str(settings->At(i), "id")), Text::SMALL));
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
	WorldMods(Shell &shell_, String folder) : Screen(shell_), m_folder(std::move(folder)) {
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

	void Build(ui::Widget root) override {
		const Palette &p = kit.Colors();
		ui::Widget page = Page(kit, root, 1180.f);
		if (m_entry.IsNone()) {
			kit.ScreenHeader(page, "Monde introuvable", m_folder, [this] { shell.Pop(); });
			return;
		}
		const WorldEntry &w = m_entry.Value();
		kit.ScreenHeader(page, String("Mods du monde — ") + w.name, "Choisissez les mods chargés avec ce monde.", [this] { Leave(); });
		// Avertissement : changements qui touchent la sauvegarde.
		WidgetBuilder banner = kit.Row(10.f);
		banner.GrowWidth().Padding(math::Sides(12.f, 10.f)).BorderRadius(8.f).Background(WithAlpha(p.warning, 0.12f)).BorderColor(WithAlpha(p.warning, 0.5f)).BorderWidth(1.f);
		ui::Widget bn = page.Append(banner);
		bn.Append(kit.Icon(MaterialIcons::WARNING, 20.f, Some(p.warning)));
		bn.Append(kit.Paragraph("Retirer un mod rend inconnus ses blocs et créatures déjà présents dans ce monde. Une sauvegarde de sécurité "
					  "vous sera proposée avant d'appliquer les changements.",
					  Text::SMALL));
		WidgetBuilder tools = kit.Row(10.f);
		tools.GrowWidth();
		ui::Widget t = page.Append(tools);
		t.Append(kit.Icon(MaterialIcons::SEARCH, 20.f));
		WidgetBuilder search = kit.Input("Rechercher un mod…");
		search.OnTextChange([this](const String &q) {
			m_query = q.ToLower();
			Refresh();
		});
		ui::Widget se = t.Append(search);
		kit.SetInputText(se, m_query);
		t.Append(kit.Combo({"Tous", "Activés", "Désactivés", "Problèmes", "Mises à jour"}, m_filter, [this](int i) {
			m_filter = i;
			Refresh();
		}));
		t.Append(kit.Button("Ajouter un mod…", [this] { shell.Push(MakeModBrowser(shell)); }));
		WidgetBuilder list = kit.Scroll(8.f);
		list.GrowWidth().GrowHeight();
		m_list = page.Append(list);
		Refresh();
		ui::Widget footer = kit.Footer(page);
		m_summary = footer.Append(kit.Muted(String(), Text::SMALL).GrowWidth().Color(p.warning));
		footer.Append(kit.Button("Annuler", [this] { shell.Pop(); }));
		footer.Append(kit.PrimaryButton("Enregistrer les modifications", [this] { Apply(); }));
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
			row.GrowWidth().Padding(math::Sides(12.f, 10.f)).BorderRadius(8.f).Background(WithAlpha(p.bg2, on ? 0.9f : 0.45f));
			row.BorderColor(problem ? WithAlpha(p.danger, 0.7f) : p.line).BorderWidth(1.f);
			ui::Widget r = m_list.Append(row);
			r.Append(ModIcon(shell, info, 40.f));
			WidgetBuilder text = kit.Column(3.f);
			text.GrowWidth();
			ui::Widget tx = r.Append(text);
			tx.Append(kit.Label(shell.Prefs().ModName(info.id, info.name), Text::BODY).Bold().Color(on ? p.text : p.textMuted));
			WidgetBuilder chips = kit.Row(6.f);
			ui::Widget ch = tx.Append(chips);
			ch.Append(kit.Chip(String("v") + info.version, Tone::NEUTRAL));
			ch.Append(kit.Chip(on ? "Activé pour ce monde" : "Désactivé pour ce monde", on ? Tone::SUCCESS : Tone::NEUTRAL));
			if (m.UpdateAvailable())
				ch.Append(kit.Chip(String::Format("Mise à jour v%s", m.available.Value().version.CStr()), Tone::ACCENT));
			if (on && !missingDeps.empty())
				ch.Append(kit.Chip(String("Dépendance manquante : ") + Join(missingDeps), Tone::DANGER));
			if (on && conflict)
				ch.Append(kit.Chip("Incompatible avec un mod activé", Tone::DANGER));
			if (m_showDeps == info.id)
				tx.Append(kit.Muted(String("Dépendances : ") + (info.dependencies.empty() ? String("aucune") : Join(info.dependencies)) +
							  String("   ·   facultatives : ") + (info.optionalDependencies.empty() ? String("aucune") : Join(info.optionalDependencies)),
						  Text::CAPTION));
			r.Append(kit.IconButton(MaterialIcons::ACCOUNT_TREE, "Dépendances", [this, id = info.id] {
				m_showDeps = m_showDeps == id ? String() : id;
				Refresh();
			}));
			WidgetBuilder cfg = kit.IconButton(MaterialIcons::TUNE, "Configuration du mod pour ce monde", [this, id = info.id] {
				shell.Push(MakeModConfig(shell, id, m_folder));
			});
			if (!json::At(info.manifest, "settings") || !on)
				cfg.Disabled();
			r.Append(cfg);
			if (m.builtin)
				r.Append(kit.Chip("Requis", Tone::INFO));
			else if (on)
				r.Append(kit.DangerButton("Retirer", [this, id = info.id] {
					std::erase(m_enabled, id);
					Refresh();
					UpdateSummary();
				}));
			else
				r.Append(kit.Button("Ajouter", [this, info] {
					m_enabled.push_back(info.id);
					for (const String &d : info.dependencies) // dépendances activées avec lui
						if (!Enabled(d))
							m_enabled.push_back(d);
					Refresh();
					UpdateSummary();
				}));
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
	ui::Widget m_list{}, m_summary{};
	int m_filter = 0;
};

// ── Configuration d'un mod ───────────────────────────────────────────────────

class ModConfig final : public Screen {
public:
	ModConfig(Shell &shell_, String id, String folder) : Screen(shell_), m_id(std::move(id)), m_folder(std::move(folder)) {
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

	void Build(ui::Widget root) override {
		const Palette &p = kit.Colors();
		ui::Widget page = Page(kit, root, 940.f);
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
		scroll.GrowWidth().GrowHeight();
		ui::Widget c = page.Append(scroll);
		dataio::NodePtr schema = json::At(m->info.manifest, "settings");
		if (!schema || !schema->IsArray()) {
			c.Append(kit.Muted("Ce mod ne propose aucun réglage.", Text::BODY));
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
				WidgetBuilder exp = ui::Expander("Paramètres avancés", m_advanced);
				exp.GrowWidth().OnToggle([this](bool v) { m_advanced = v; });
				ui::Widget e = c.Append(exp);
				WidgetBuilder col = kit.Column(8.f);
				col.GrowWidth().Padding(math::Sides(0.f, 8.f));
				ui::Widget ce = e.Append(col);
				for (size_t i = 0; i < schema->GetSize(); ++i)
					if (json::Bool(schema->At(i), "advanced", false))
						Control(ce, schema->At(i));
			}
		}
		ui::Widget footer = kit.Footer(page);
		footer.Append(kit.Muted("Les changements s'appliquent au prochain chargement du monde.", Text::CAPTION).GrowWidth().Color(p.textDim));
		footer.Append(kit.GhostButton("Valeurs par défaut", [this] {
			m_values = dataio::Node::MakeObject();
			shell.Rebuild();
		}));
		footer.Append(kit.Button("Annuler", [this] { shell.Pop(); }));
		footer.Append(kit.PrimaryButton("Enregistrer", [this] { Save(); }));
	}

private:
	dataio::NodePtr Value(const dataio::NodePtr &item) const {
		const String id = json::Str(item, "id");
		if (dataio::NodePtr v = json::At(m_values, id.CStr()))
			return v;
		return json::At(item, "default");
	}

	void Control(ui::Widget parent, const dataio::NodePtr &item) {
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
			in.Width(300.f).OnTextChange([this, id](const String &t) { m_values->Set(id, json::MakeStr(t)); });
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
		// Partie en cours (ce monde, ou réglage pour tous) : appliqué tout de suite.
		const bool applied = shell.Game() && (m_folder.IsEmpty() || m_folder == shell.Game()->Folder());
		if (applied)
			shell.Game()->Game().UpdateModSettings(m_id, m_values, !m_folder.IsEmpty());
		shell.UiSound("ui_save");
		shell.Notify(Tone::SUCCESS, "Configuration enregistrée", applied ? m_id + String(" — appliquée à la partie en cours") : m_id);
		shell.Pop();
	}

	String m_id, m_folder, m_worldName;
	dataio::NodePtr m_worldSettings, m_values;
	bool m_advanced = false;
};

// ── Organisation des mods ────────────────────────────────────────────────────

class ModOrganize final : public Screen {
public:
	explicit ModOrganize(Shell &shell_) : Screen(shell_) {
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

	void Build(ui::Widget root) override {
		const Palette &p = kit.Colors();
		ui::Widget page = Page(kit, root, 1140.f);
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
			banner.GrowWidth().Padding(math::Sides(12.f, 10.f)).BorderRadius(8.f).Background(WithAlpha(p.danger, 0.12f)).BorderColor(WithAlpha(p.danger, 0.5f)).BorderWidth(1.f);
			ui::Widget bn = page.Append(banner);
			bn.Append(kit.Label("Conflits potentiels", Text::SMALL).Bold().Color(p.danger));
			for (const auto &[a, b] : conflicts)
				bn.Append(kit.Muted(String::Format("« %s » et « %s » sont déclarés incompatibles", a.CStr(), b.CStr()), Text::SMALL));
			for (const String &e : errors)
				bn.Append(kit.Muted(e, Text::SMALL));
		}
		WidgetBuilder head = kit.Row(10.f);
		head.GrowWidth().Padding(math::Sides(12.f, 0.f));
		ui::Widget hd = page.Append(head);
		hd.Append(kit.Muted("ORDRE", Text::CAPTION).Width(90.f));
		hd.Append(kit.Muted("NOM D'AFFICHAGE  /  IDENTIFIANT INTERNE", Text::CAPTION).GrowWidth());
		hd.Append(kit.Muted("CATÉGORIE", Text::CAPTION).Width(230.f));
		WidgetBuilder list = kit.Scroll(6.f);
		list.GrowWidth().GrowHeight();
		ui::Widget l = page.Append(list);
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
			row.GrowWidth().Padding(math::Sides(12.f, 8.f)).BorderRadius(8.f).Background(p.bg2).BorderColor(p.line).BorderWidth(1.f);
			ui::Widget r = l.Append(row);
			WidgetBuilder order = kit.Row(2.f);
			order.Width(90.f);
			ui::Widget o = r.Append(order);
			o.Append(kit.Label(String::Format("%d", position), Text::H3).Width(26.f));
			WidgetBuilder up = kit.IconButton(MaterialIcons::ARROW_UPWARD, "Monter", [this, i] { Move(i, -1); });
			if (i == 0)
				up.Disabled();
			o.Append(up);
			WidgetBuilder down = kit.IconButton(MaterialIcons::ARROW_DOWNWARD, "Descendre", [this, i] { Move(i, 1); });
			if (i + 1 >= m_order.size())
				down.Disabled();
			o.Append(down);
			WidgetBuilder names = kit.Column(3.f);
			names.GrowWidth();
			ui::Widget n = r.Append(names);
			WidgetBuilder input = kit.Input(info.name);
			input.OnTextChange([this, id = info.id](const String &t) { m_names[id.CStr()] = t.Trim(); });
			ui::Widget in = n.Append(input);
			auto it = m_names.find(info.id.CStr());
			kit.SetInputText(in, it != m_names.end() ? it->second : String());
			String deps = info.dependencies.empty() ? String("aucune dépendance") : String("requiert ") + Join(info.dependencies);
			n.Append(kit.Muted(String::Format("%s  ·  nom d'origine « %s »  ·  %s", info.id.CStr(), info.name.CStr(), deps.CStr()), Text::CAPTION));
			int cat = 0;
			const String current = m_categories.contains(info.id.CStr()) ? m_categories[info.id.CStr()] : e->Category();
			for (size_t k = 0; k < ModCatalog::Categories().size(); ++k)
				if (ModCatalog::Categories()[k].first == current)
					cat = int(k);
			r.Append(kit.Combo(catNames, cat, [this, id = info.id](int k) { m_categories[id.CStr()] = ModCatalog::Categories()[size_t(k)].first; }));
		}
		page.Append(kit.Muted("L'ordre choisi s'applique aux nouveaux mondes et aux mondes sans ordre propre ; un mod est toujours chargé après ses dépendances.",
				  Text::CAPTION)
			.GrowWidth()
			.TextWrap());
		ui::Widget footer = kit.Footer(page);
		footer.Append(kit.GhostButton("Restaurer les paramètres d'origine", [this] {
			m_names.clear();
			m_categories.clear();
			m_order.clear();
			for (const voxel::ModInfo &m : shell.Mods().Installed())
				m_order.push_back(m.id);
			shell.Rebuild();
		}));
		footer.Append(kit.Spacer());
		footer.Append(kit.Button("Annuler", [this] { shell.Pop(); }));
		footer.Append(kit.PrimaryButton("Enregistrer", [this] {
			shell.Prefs().modOrder = m_order;
			shell.Prefs().modNames = m_names;
			shell.Prefs().modCategories = m_categories;
			shell.SaveSettings();
			shell.Notify(Tone::SUCCESS, "Organisation des mods enregistrée");
			shell.Pop();
		}));
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
	ModChangesScreen(Shell &shell_, ModChanges changes, std::function<void(bool)> apply)
		: Screen(shell_), m_changes(std::move(changes)), m_apply(std::move(apply)) {}
	String Id() const override { return "modchanges"; }
	bool Overlay() const override { return true; }

	void Build(ui::Widget root) override {
		const Palette &p = kit.Colors();
		ui::Widget card = ModalCard(kit, root, 620.f);
		WidgetBuilder head = kit.Row(12.f);
		ui::Widget h = card.Append(head);
		h.Append(kit.Icon(MaterialIcons::EXTENSION, 28.f, Some(p.primary)));
		WidgetBuilder ht = kit.Column(2.f);
		ht.GrowWidth();
		ui::Widget htt = h.Append(ht);
		htt.Append(kit.Label("Appliquer les changements de mods ?", Text::H2));
		htt.Append(kit.Muted(String::Format("Monde « %s »", m_changes.worldName.CStr()), Text::SMALL));
		auto section = [&](const char *title, const std::vector<String> &items, MaterialIcons icon, sdl3::FColor color) {
			if (items.empty())
				return;
			WidgetBuilder box = kit.Column(4.f);
			box.GrowWidth().Padding(math::Sides(12.f, 8.f)).BorderRadius(8.f).Background(p.bg2);
			ui::Widget b = card.Append(box);
			WidgetBuilder t = kit.Row(6.f);
			ui::Widget tt = b.Append(t);
			tt.Append(kit.Icon(icon, 16.f, Some(color)));
			tt.Append(kit.Label(String::Format("%s (%d)", title, int(items.size())), Text::SMALL).Bold().Color(color));
			for (const String &i : items)
				b.Append(kit.Label(String("• ") + i, Text::SMALL).Color(p.textMuted));
		};
		section("Mods ajoutés", m_changes.added, MaterialIcons::ADD, p.success);
		section("Mods retirés", m_changes.removed, MaterialIcons::REMOVE, p.danger);
		section("Mods mis à jour", m_changes.updated, MaterialIcons::UPDATE, p.accent);
		section("Dépendances concernées", m_changes.affectedDependencies, MaterialIcons::ACCOUNT_TREE, p.info);
		section("Risques de compatibilité", m_changes.risks, MaterialIcons::WARNING, p.warning);
		card.Append(kit.Paragraph("Conséquences sur la sauvegarde : le monde sera rechargé avec ces mods à sa prochaine ouverture. "
					  "Rien n'est effacé tant que vous n'enregistrez pas la partie ; une sauvegarde de sécurité permet de revenir en arrière.",
					  Text::SMALL));
		WidgetBuilder row = kit.Row(10.f);
		ui::Widget r = card.Append(row);
		r.Append(kit.Check(m_backup, [this](bool v) { m_backup = v; }));
		r.Append(kit.Label("Créer une sauvegarde de sécurité avant d'appliquer (recommandé)", Text::SMALL));
		WidgetBuilder buttons = kit.Row(10.f);
		buttons.GrowWidth().JustifyContent(ui::Justify::End);
		ui::Widget bt = card.Append(buttons);
		bt.Append(kit.Button("Annuler", [this] { shell.Pop(); }));
		bt.Append(kit.PrimaryButton("Appliquer les modifications", [this] {
			auto apply = m_apply;
			const bool backup = m_backup;
			shell.Pop();
			if (apply)
				apply(backup);
		}));
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

} // namespace voxelia
