// Écrans de la partie : pause, inventaire, fabrication, discussion, terminal,
// informations du monde, commerce, rangement, dialogue.
#include "common.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <voxel/inventory.hpp>

namespace voxelia {

using namespace uiutil;
using namespace voxel;

namespace {

constexpr int HOTBAR = 9;

/// Écran de partie : la session doit être prête.
Session *Live(Shell &shell) {
	Session *s = shell.Game();
	return s && s->Ready() ? s : nullptr;
}

/// Catégorie d'un objet (filtres de l'inventaire).
int ItemCategory(Game &game, const String &id) {
	const ItemDef *item = game.Content().FindItem(id);
	if (!item)
		return 0;
	if (item->HasTag("armor") || item->HasTag("weapons") || item->HasTag("swords"))
		return 3;
	if (item->tool)
		return 2;
	if (item->food > 0.f)
		return 4;
	if (!item->placesBlock.IsEmpty())
		return 1;
	return 5;
}
const std::vector<String> ITEM_CATEGORIES{"Tout", "Blocs", "Outils", "Combat", "Nourriture", "Matériaux"};

String Describe(Game &game, const String &id) {
	const ItemDef *item = game.Content().FindItem(id);
	if (!item)
		return String();
	std::vector<String> lines;
	if (item->food > 0.f)
		lines.push_back(String::Format("Nourriture : +%.0f faim", double(item->food)));
	if (item->tool) {
		if (item->tool->damage > 0.f)
			lines.push_back(String::Format("Dégâts : %.0f", double(item->tool->damage)));
		if (item->tool->durability > 0)
			lines.push_back(String::Format("Durabilité : %d", item->tool->durability));
		if (item->tool->speed > 1.f)
			lines.push_back(String::Format("Vitesse de minage : ×%.1f (%s)", double(item->tool->speed), item->tool->kind.CStr()));
	}
	if (json::Has(item->raw, "armor"))
		lines.push_back(String::Format("Armure : +%d (%s)", json::Int(item->raw, "armor.points"), json::Str(item->raw, "armor.slot").CStr()));
	if (!item->placesBlock.IsEmpty())
		lines.push_back("Bloc à poser");
	if (NodePtr fx = json::At(item->raw, "effects"); fx && fx->IsArray())
		for (size_t i = 0; i < fx->GetSize(); ++i)
			lines.push_back(String::Format("Effet : %s (%d s)", json::Str(fx->At(i), "id").CStr(), json::Int(fx->At(i), "seconds", 10)));
	lines.push_back(String::Format("Pile maximale : %d", item->maxStack));
	String out;
	for (const String &l : lines)
		out += (out.IsEmpty() ? String() : String("\n")) + l;
	return out;
}

// ── Pause ────────────────────────────────────────────────────────────────────

class Pause final : public Screen {
public:
	explicit Pause(Shell &shell_) : Screen(shell_) {}
	String Id() const override { return "pause"; }
	bool Overlay() const override { return true; }

	void Build(ui::Widget root) override {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		ui::Widget card = ModalCard(kit, root, 420.f);
		card.Append(kit.PixelTitle(s->Game().Metadata().name, 22.f, p.text, true));
		card.Append(kit.Muted(String::Format("%s · %s · %s de jeu", GameModeName(GameModeId(s->Game().Mode())).CStr(),
								 DifficultyName(DifficultyId(s->Game().GetDifficulty())).CStr(),
								 FormatDuration(s->Game().Metadata().playTime).CStr()),
				  Text::SMALL)
			.AlignSelf(ui::Align::Center));
		card.Append(kit.Divider());
		kit.MenuButton(card, MaterialIcons::PLAY_ARROW, "Reprendre la partie", "Échap", [this] { shell.ClearScreens(); }, true);
		kit.MenuButton(card, MaterialIcons::BACKPACK, "Inventaire", String(), [this] {
			shell.Pop();
			shell.Push(MakeInventory(shell));
		});
		kit.MenuButton(card, MaterialIcons::SETTINGS, "Paramètres", String(), [this] { shell.Push(MakeSettings(shell)); });
		kit.MenuButton(card, MaterialIcons::INFO, "Informations du monde", String(), [this] { shell.Push(MakeWorldInfo(shell)); });
		kit.MenuButton(card, MaterialIcons::TUNE, "Configuration du monde", String(), [this] { shell.Push(MakeWorldSettings(shell, String())); });
		kit.MenuButton(card, MaterialIcons::SAVE, "Enregistrer", "F6", [this] {
			if (Session *live = Live(shell))
				live->Save(true);
		});
		card.Append(kit.Divider());
		card.Append(kit.DangerButton("Enregistrer et quitter le monde", [this] { AskQuit(); }).GrowWidth());
	}

private:
	void AskQuit() {
		if (!shell.Prefs().confirmQuit) {
			shell.LeaveWorld(true);
			return;
		}
		ConfirmSpec spec;
		spec.title = "Quitter le monde ?";
		spec.message = "La partie est enregistrée avant le retour au menu principal.";
		if (Session *s = Live(shell))
			spec.details = {String::Format("Durée de la session : %s", FormatDuration(s->Stats().sessionTime).CStr()),
							"Progression, inventaire et constructions conservés"};
		spec.confirmText = "Quitter";
		spec.tone = Tone::PRIMARY;
		spec.icon = MaterialIcons::EXIT_TO_APP;
		spec.onConfirm = [this] { shell.LeaveWorld(true); };
		shell.Confirm(std::move(spec));
	}
};

// ── Inventaire ───────────────────────────────────────────────────────────────

class InventoryScreen final : public Screen {
public:
	explicit InventoryScreen(Shell &shell_) : Screen(shell_) {}
	String Id() const override { return "inventory"; }
	bool Overlay() const override { return true; }
	~InventoryScreen() override { ReturnHeld(); }

	void Build(ui::Widget root) override {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		const float sc = kit.Scale();
		Game &game = s->Game();
		const bool creative = game.Mode() == GameMode::CREATIVE;
		ui::Widget card = ModalCard(kit, root, creative ? 1180.f : 1040.f);
		// En-tête : titre, recherche, catégories, capacité.
		WidgetBuilder head = kit.Row(10.f);
		head.GrowWidth();
		ui::Widget h = card.Append(head);
		h.Append(kit.Icon(MaterialIcons::BACKPACK, 26.f, Some(p.primary)));
		h.Append(kit.Label("Inventaire", Text::H2));
		WidgetBuilder search = kit.Input("Rechercher un objet…");
		search.Width(240.f * sc).OnTextChange([this](const String &q) { m_query = q.ToLower(); });
		ui::Widget se = h.Append(search);
		kit.SetInputText(se, m_query);
		h.Append(kit.Combo(ITEM_CATEGORIES, m_category, [this](int i) { m_category = i; }));
		int used = 0;
		if (Inventory *inv = s->Inventory())
			for (int i = 0; i < inv->Size(); ++i)
				used += !inv->Slot(i).IsEmpty();
		h.Append(kit.Spacer());
		h.Append(kit.Chip(String::Format("%d / %d emplacements", used, s->Inventory() ? s->Inventory()->Size() : 0), used > 32 ? Tone::WARNING : Tone::NEUTRAL));
		h.Append(kit.IconButton(MaterialIcons::CLOSE, "Fermer (E)", [this] { shell.Pop(); }));

		WidgetBuilder body = kit.Row(18.f);
		body.GrowWidth().AlignItems(ui::Align::Start);
		ui::Widget b = card.Append(body);
		// Équipement : personnage + 4 emplacements d'armure.
		WidgetBuilder equip = kit.Card(12.f);
		equip.Width(170.f * sc).AlignItems(ui::Align::Center);
		ui::Widget eq = b.Append(equip);
		eq.Append(kit.Label("Équipement", Text::SMALL).Bold());
		WidgetBuilder row = kit.Row(8.f);
		ui::Widget r = eq.Append(row);
		String skin;
		if (auto v = game.Registry().GetComponent<Visual>(s->Player()); v.IsSome())
			skin = v.Unwrap()->skin;
		r.Append(SkinFront(shell, skin, 150.f * sc));
		WidgetBuilder armor = ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &rect) {
			if (painter.Renderer())
				DrawArmor(*painter.Renderer(), rect);
		});
		armor.Size(46.f * sc, 4.f * 48.f * sc).OnClick([this] { ClickArmor(); });
		r.Append(armor);
		m_armorPoints = eq.Append(kit.Muted(String(), Text::CAPTION));
		// Grille.
		WidgetBuilder center = kit.Column(8.f);
		ui::Widget c = b.Append(center);
		WidgetBuilder grid = ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &rect) {
			if (painter.Renderer())
				DrawGrid(*painter.Renderer(), rect);
		});
		const float cell = 50.f * sc;
		grid.Size(cell * 9.f, cell * 4.f + 12.f).OnClick([this] { ClickGrid(false); }).OnContextMenu([this](sdl3::FPoint) { ClickGrid(true); });
		c.Append(grid);
		c.Append(kit.Muted("Clic : prendre / poser · Clic droit : moitié / un seul · Maj + clic : déplacer vers la barre d'objets", Text::CAPTION));
		if (creative) {
			c.Append(kit.Label("Catalogue créatif — cliquez pour prendre une pile", Text::SMALL).Bold());
			WidgetBuilder catalog = ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &rect) {
			if (painter.Renderer())
				DrawCatalog(*painter.Renderer(), rect);
		});
			c.Append(catalog.Size(cell * 9.f, cell * 4.f).OnClick([this] { ClickCatalog(); }));
			WidgetBuilder pages = kit.Row(8.f);
			ui::Widget pg = c.Append(pages);
			pg.Append(kit.IconButton(MaterialIcons::ARROW_BACK, "Page précédente", [this] { m_catalogPage = std::max(0, m_catalogPage - 1); }));
			m_catalogLabel = pg.Append(kit.Muted(String(), Text::CAPTION));
			pg.Append(kit.IconButton(MaterialIcons::ARROW_FORWARD, "Page suivante", [this] { ++m_catalogPage; }));
		}
		// Détail de l'objet survolé ou sélectionné.
		WidgetBuilder detail = kit.Card(14.f);
		detail.Width(240.f * sc);
		ui::Widget d = b.Append(detail);
		WidgetBuilder icon = ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &rect) {
			if (Session *live = Live(shell); live && !m_detail.IsEmpty() && painter.Renderer())
				live->DrawItem(*painter.Renderer(), m_detail, rect, false);
		});
		d.Append(icon.Size(64.f * sc, 64.f * sc));
		m_detailName = d.Append(kit.Label(String(), Text::H3).GrowWidth().TextWrap());
		m_detailInfo = d.Append(kit.Paragraph(String(), Text::SMALL));
		WidgetBuilder actions = kit.Column(6.f);
		actions.GrowWidth();
		m_actions = d.Append(actions);
		m_actions.Append(kit.Button("Utiliser / équiper", [this] { UseSelected(); }).GrowWidth());
		m_actions.Append(kit.Button("Jeter un", [this] { DropSelected(false); }).GrowWidth());
		m_actions.Append(kit.DangerButton("Jeter la pile", [this] { DropSelected(true); }).GrowWidth());
		d.Append(kit.Button("Fabrication…", [this] {
			shell.Pop();
			shell.Push(MakeCrafting(shell));
		}).GrowWidth());
		UpdateDetail();
	}

	void Update(float) override {
		UpdateHover();
		UpdateDetail();
		if (Session *s = Live(shell))
			kit.SetText(m_armorPoints, String::Format("Armure : %d / 20", s->Game().ArmorPoints(s->Player())));
		if (m_catalogLabel.Valid())
			kit.SetText(m_catalogLabel, String::Format("Page %d / %d", m_catalogPage + 1, std::max(1, int((CatalogItems().size() + 35) / 36))));
	}
	bool OnKey(int code) override {
		if (shell.Prefs().Matches("inventory", code) && !kit.Ui().Focused()) {
			shell.Pop();
			return true;
		}
		return false;
	}

private:
	/// Case de la grille sous la souris (-1 : aucune).
	int SlotAt(float mx, float my) const {
		const float cell = m_grid.w / 9.f;
		if (mx < m_grid.x || mx >= m_grid.x + m_grid.w || my < m_grid.y || my >= m_grid.y + m_grid.h)
			return -1;
		const int col = int((mx - m_grid.x) / cell);
		const float ry = my - m_grid.y;
		if (ry >= cell * 3.f + 12.f)
			return col; // barre d'objets (en bas)
		if (ry < cell * 3.f)
			return HOTBAR + int(ry / cell) * HOTBAR + col;
		return -1;
	}
	sdl3::FRect CellRect(int slot) const {
		const float cell = m_grid.w / 9.f;
		const int col = slot % HOTBAR;
		const float y = slot < HOTBAR ? m_grid.y + cell * 3.f + 12.f : m_grid.y + float((slot - HOTBAR) / HOTBAR) * cell;
		return sdl3::FRect{m_grid.x + float(col) * cell + 2.f, y + 2.f, cell - 4.f, cell - 4.f};
	}
	bool Matches(Game &game, const ItemStack &st) const {
		if (st.IsEmpty())
			return true;
		if (m_category > 0 && ItemCategory(game, st.item) != m_category)
			return false;
		if (!m_query.IsEmpty()) {
			const ItemDef *item = game.Content().FindItem(st.item);
			if (!(item ? item->displayName : st.item).ToLower().Contains(m_query) && !st.item.ToLower().Contains(m_query))
				return false;
		}
		return true;
	}
	void DrawSlot(sdl3::Renderer &ren, sdl3::FRect cell, bool selected, bool hovered, bool dim) {
		const Palette &p = kit.Colors();
		ren.SetDrawColor(dim ? WithAlpha(p.bg0, 0.6f) : hovered ? p.bg3 : p.bg1);
		(void)ren.FillRect(cell);
		ren.SetDrawColor(selected ? p.primary : p.line);
		(void)ren.DrawRect(cell);
		if (selected)
			(void)ren.DrawRect(sdl3::FRect{cell.x + 1.f, cell.y + 1.f, cell.w - 2.f, cell.h - 2.f});
	}
	void DrawGrid(sdl3::Renderer &ren, sdl3::FRect r) {
		Session *s = Live(shell);
		if (!s)
			return;
		m_grid = r;
		Inventory *inv = s->Inventory();
		if (!inv)
			return;
		(void)ren.SetBlendMode(sdl3::BlendMode::BLEND);
		for (int i = 0; i < std::min(inv->Size(), 36); ++i) {
			const sdl3::FRect cell = CellRect(i);
			const bool dim = !Matches(s->Game(), inv->Slot(i));
			DrawSlot(ren, cell, i == m_selected || (i == s->SelectedSlot() && i < HOTBAR), i == m_hover, dim);
			s->DrawItem(ren, inv->Slot(i), cell);
			if (dim) {
				ren.SetDrawColor(Rgb(10, 12, 16, 170));
				(void)ren.FillRect(cell);
			}
		}
		// Pile tenue : suit la souris.
		if (!m_held.IsEmpty()) {
			float mx = 0.f, my = 0.f;
			(void)SDL_GetMouseState(&mx, &my);
			const float size = m_grid.w / 9.f;
			s->DrawItem(ren, m_held, sdl3::FRect{mx - size * 0.4f, my - size * 0.4f, size * 0.8f, size * 0.8f});
		}
	}
	void DrawArmor(sdl3::Renderer &ren, sdl3::FRect r) {
		Session *s = Live(shell);
		if (!s)
			return;
		m_armor = r;
		auto eq = s->Game().Registry().GetComponent<Equipment>(s->Player());
		const char *names[4] = {"T", "C", "J", "P"};
		for (int i = 0; i < ARMOR_SLOTS; ++i) {
			const sdl3::FRect cell{r.x, r.y + float(i) * (r.h / 4.f) + 1.f, r.w - 2.f, r.h / 4.f - 4.f};
			DrawSlot(ren, cell, false, false, false);
			if (eq.IsSome() && !eq.Unwrap()->armor[size_t(i)].IsEmpty())
				s->DrawItem(ren, eq.Unwrap()->armor[size_t(i)], cell);
			else
				kit.Pixel().Draw(ren, names[i], cell.x + cell.w * 0.5f - 4.f, cell.y + cell.h * 0.5f - 6.f, 1.5f, kit.Colors().textDim, 0.f);
		}
	}
	std::vector<const ItemDef *> CatalogItems() {
		std::vector<const ItemDef *> out;
		if (Session *s = Live(shell))
			for (const ItemDef *item : s->Game().Content().Items())
				if (Matches(s->Game(), ItemStack{item->id, 1, 0, nullptr}))
					out.push_back(item);
		return out;
	}
	void DrawCatalog(sdl3::Renderer &ren, sdl3::FRect r) {
		Session *s = Live(shell);
		if (!s)
			return;
		m_catalog = r;
		const auto items = CatalogItems();
		const float cell = r.w / 9.f;
		for (int i = 0; i < 36; ++i) {
			const size_t index = size_t(m_catalogPage * 36 + i);
			const sdl3::FRect c{r.x + float(i % 9) * cell + 2.f, r.y + float(i / 9) * cell + 2.f, cell - 4.f, cell - 4.f};
			DrawSlot(ren, c, false, false, index >= items.size());
			if (index < items.size())
				s->DrawItem(ren, ItemStack{items[index]->id, 1, 0, nullptr}, c, false);
		}
	}
	void UpdateHover() {
		float mx = 0.f, my = 0.f;
		(void)SDL_GetMouseState(&mx, &my);
		m_hover = SlotAt(mx, my);
	}
	void UpdateDetail() {
		Session *s = Live(shell);
		if (!s)
			return;
		Inventory *inv = s->Inventory();
		const int slot = m_hover >= 0 ? m_hover : m_selected;
		m_detail = inv && slot >= 0 && slot < inv->Size() ? inv->Slot(slot) : ItemStack{};
		if (!m_held.IsEmpty())
			m_detail = m_held;
		const bool any = !m_detail.IsEmpty();
		kit.SetText(m_detailName, any ? String::Format("%s%s", s->ItemName(m_detail.item).CStr(),
													  m_detail.count > 1 ? String::Format(" × %d", m_detail.count).CStr() : "")
									  : String("Aucun objet"));
		kit.SetText(m_detailInfo, any ? Describe(s->Game(), m_detail.item) + String("\n") + m_detail.item
									  : String("Survolez ou sélectionnez un objet pour voir ses propriétés."));
		kit.SetHidden(m_actions, !any || m_selected < 0);
	}
	void ClickGrid(bool right) {
		Session *s = Live(shell);
		if (!s)
			return;
		Inventory *inv = s->Inventory();
		float mx = 0.f, my = 0.f;
		(void)SDL_GetMouseState(&mx, &my);
		const int slot = SlotAt(mx, my);
		if (!inv || slot < 0 || slot >= inv->Size())
			return;
		ItemStack &target = inv->Slot(slot);
		shell.UiSound("ui_click");
		if ((SDL_GetModState() & SDL_KMOD_SHIFT) && m_held.IsEmpty() && !target.IsEmpty()) {
			// Déplacement rapide barre d'objets ↔ sac.
			const int from = slot < HOTBAR ? HOTBAR : 0, to = slot < HOTBAR ? inv->Size() : HOTBAR;
			for (int i = from; i < to; ++i)
				if (inv->Slot(i).IsEmpty()) {
					std::swap(inv->Slot(i), target);
					return;
				}
			return;
		}
		m_selected = slot;
		const ItemDef *def = s->Game().Content().FindItem(m_held.IsEmpty() ? target.item : m_held.item);
		const int maxStack = def ? def->maxStack : 64;
		if (m_held.IsEmpty()) {
			if (target.IsEmpty())
				return;
			if (right) { // la moitié
				const int half = (target.count + 1) / 2;
				m_held = ItemStack{target.item, half, target.wear, target.meta};
				target.count -= half;
				if (target.count <= 0)
					target = ItemStack{};
			} else {
				m_held = target;
				target = ItemStack{};
			}
			return;
		}
		if (target.IsEmpty() || target.item == m_held.item) {
			const int room = maxStack - (target.IsEmpty() ? 0 : target.count);
			const int moved = std::min(right ? 1 : m_held.count, room);
			if (moved <= 0)
				return;
			if (target.IsEmpty())
				target = ItemStack{m_held.item, 0, m_held.wear, m_held.meta};
			target.count += moved;
			m_held.count -= moved;
			if (m_held.count <= 0)
				m_held = ItemStack{};
		} else {
			std::swap(target, m_held);
		}
	}
	void ClickArmor() {
		Session *s = Live(shell);
		if (!s)
			return;
		float mx = 0.f, my = 0.f;
		(void)SDL_GetMouseState(&mx, &my);
		const int slot = int((my - m_armor.y) / (m_armor.h / 4.f));
		auto eq = s->Game().Registry().GetComponent<Equipment>(s->Player());
		if (slot < 0 || slot >= ARMOR_SLOTS || eq.IsNone())
			return;
		static const char *slots[4] = {"head", "chest", "legs", "feet"};
		if (!m_held.IsEmpty()) {
			const ItemDef *item = s->Game().Content().FindItem(m_held.item);
			if (!item || json::Str(item->raw, "armor.slot") != slots[slot]) {
				shell.UiSound("ui_error");
				return;
			}
		}
		std::swap(eq.Unwrap()->armor[size_t(slot)], m_held);
		shell.UiSound("ui_click");
	}
	void ClickCatalog() {
		const auto items = CatalogItems();
		float mx = 0.f, my = 0.f;
		(void)SDL_GetMouseState(&mx, &my);
		const float cell = m_catalog.w / 9.f;
		const int col = int((mx - m_catalog.x) / cell), row = int((my - m_catalog.y) / cell);
		const size_t index = size_t(m_catalogPage * 36 + row * 9 + col);
		if (col < 0 || col >= 9 || row < 0 || row >= 4 || index >= items.size())
			return;
		m_held = ItemStack{items[index]->id, items[index]->maxStack, 0, nullptr};
		shell.UiSound("ui_click");
	}
	void UseSelected() {
		Session *s = Live(shell);
		if (!s || m_selected < 0)
			return;
		if (m_selected < HOTBAR)
			s->SelectSlot(m_selected);
		else if (Inventory *inv = s->Inventory()) {
			std::swap(inv->Slot(m_selected), inv->Slot(s->SelectedSlot()));
			m_selected = s->SelectedSlot();
		}
		if (!s->Game().EquipHeld(s->Player()))
			(void)s->Game().Eat(s->Player());
	}
	void DropSelected(bool all) {
		Session *s = Live(shell);
		if (!s || m_selected < 0)
			return;
		if (Inventory *inv = s->Inventory(); inv && m_selected < inv->Size()) {
			const int keep = s->SelectedSlot();
			std::swap(inv->Slot(m_selected), inv->Slot(keep));
			s->DropSelected(all);
			std::swap(inv->Slot(m_selected), inv->Slot(keep));
		}
	}
	void ReturnHeld() {
		Session *s = Live(shell);
		if (!s || m_held.IsEmpty())
			return;
		if (Inventory *inv = s->Inventory()) {
			const int left = inv->Add(s->Game().Content(), m_held.item, m_held.count, m_held.meta);
			if (left > 0)
				if (auto t = s->Game().Registry().GetComponent<Transform>(s->Player()); t.IsSome())
					(void)s->Game().DropItem(ItemStack{m_held.item, left, m_held.wear, m_held.meta}, t.Unwrap()->position + math::FVector3{0.f, 1.f, 0.f});
		}
		m_held = ItemStack{};
	}

	ItemStack m_held, m_detail;
	sdl3::FRect m_grid{}, m_armor{}, m_catalog{};
	int m_hover = -1, m_selected = -1, m_category = 0, m_catalogPage = 0;
	String m_query;
	ui::Widget m_detailName{}, m_detailInfo{}, m_actions{}, m_armorPoints{}, m_catalogLabel{};
};

// ── Fabrication ──────────────────────────────────────────────────────────────

class Crafting final : public Screen {
public:
	explicit Crafting(Shell &shell_) : Screen(shell_) {}
	String Id() const override { return "crafting"; }
	bool Overlay() const override { return true; }

	void Build(ui::Widget root) override {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		const float sc = kit.Scale();
		Game &game = s->Game();
		m_stations = game.NearbyStations(s->Player());
		ui::Widget card = ModalCard(kit, root, 1160.f);
		WidgetBuilder head = kit.Row(10.f);
		head.GrowWidth();
		ui::Widget h = card.Append(head);
		h.Append(kit.Icon(MaterialIcons::BUILD, 26.f, Some(p.primary)));
		h.Append(kit.Label("Fabrication", Text::H2));
		// Ateliers : à la main + ceux qui sont à proximité (extensible par les mods).
		WidgetBuilder stations = kit.Row(6.f);
		stations.GrowWidth();
		ui::Widget st = h.Append(stations);
		st.Append(kit.Chip("À la main", Tone::SUCCESS));
		std::vector<String> known;
		for (const RecipeDef *r : game.Content().Recipes())
			if (const String station = game.RecipeStation(*r); !station.IsEmpty() && std::find(known.begin(), known.end(), station) == known.end())
				known.push_back(station);
		for (const String &station : known) {
			const BlockDef *def = game.Content().FindBlock(station);
			const bool near = std::find(m_stations.begin(), m_stations.end(), station) != m_stations.end();
			st.Append(kit.Chip((def ? def->displayName : station) + (near ? String(" · à proximité") : String(" · absent")), near ? Tone::SUCCESS : Tone::NEUTRAL));
		}
		h.Append(kit.IconButton(MaterialIcons::CLOSE, "Fermer (C)", [this] { shell.Pop(); }));
		WidgetBuilder body = kit.Row(16.f);
		body.GrowWidth().Height(560.f * sc).AlignItems(ui::Align::Stretch);
		ui::Widget b = card.Append(body);
		// Catégories.
		WidgetBuilder cats = kit.Column(2.f);
		cats.Width(180.f * sc);
		ui::Widget cs = b.Append(cats);
		NodePtr list = json::At(game.Content().Config(), "crafting.categories");
		for (size_t i = 0; list && i < list->GetSize(); ++i) {
			const String id = json::Str(list->At(i), "id"), name = json::Str(list->At(i), "name", id);
			const bool on = id == m_category;
			WidgetBuilder item = kit.Row(6.f);
			item.GrowWidth().Height(30.f * sc).Padding(math::Sides(10.f, 0.f)).BorderRadius(6.f);
			item.Background(on ? WithAlpha(p.primary, 0.18f) : WithAlpha(p.bg3, 0.f)).HoverStyle(ui::Style().Background(p.bg3));
			item.OnClick([this, id] {
				m_category = id;
				Refresh();
				shell.Rebuild();
			});
			ui::Widget it = cs.Append(item);
			it.Append(kit.Label(name, Text::SMALL).Color(on ? p.text : p.textMuted).PointerThrough());
		}
		// Liste des recettes.
		WidgetBuilder mid = kit.Column(8.f);
		mid.GrowWidth();
		ui::Widget m = b.Append(mid);
		WidgetBuilder tools = kit.Row(8.f);
		tools.GrowWidth();
		ui::Widget t = m.Append(tools);
		t.Append(kit.Icon(MaterialIcons::SEARCH, 18.f));
		WidgetBuilder search = kit.Input("Rechercher une recette…");
		search.OnTextChange([this](const String &q) {
			m_query = q.ToLower();
			Refresh();
		});
		ui::Widget se = t.Append(search);
		kit.SetInputText(se, m_query);
		t.Append(kit.Check(m_onlyCraftable, [this](bool v) {
			m_onlyCraftable = v;
			Refresh();
		}));
		t.Append(kit.Muted("Réalisables", Text::SMALL));
		WidgetBuilder scroll = kit.Scroll(4.f);
		scroll.GrowWidth().GrowHeight();
		m_list = m.Append(scroll);
		// Détail.
		WidgetBuilder right = kit.Card(14.f);
		right.Width(330.f * sc);
		m_detail = b.Append(right);
		Refresh();
	}
	bool OnKey(int code) override {
		if (shell.Prefs().Matches("crafting", code) && !kit.Ui().Focused()) {
			shell.Pop();
			return true;
		}
		return false;
	}

private:
	void Refresh() {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		Game &game = s->Game();
		kit.ClearChildren(m_list);
		int shown = 0;
		for (const RecipeDef *r : game.Content().Recipes()) {
			if (!m_category.IsEmpty() && m_category != "all" && r->category != m_category)
				continue;
			const String name = s->ItemName(r->result);
			if (!m_query.IsEmpty() && !name.ToLower().Contains(m_query) && !r->id.ToLower().Contains(m_query))
				continue;
			const String blocker = game.CraftBlocker(s->Player(), *r, m_stations);
			if (m_onlyCraftable && !blocker.IsEmpty())
				continue;
			if (++shown > 160)
				break;
			const bool locked = blocker.StartsWith("niveau");
			const bool on = r->id == m_selected;
			WidgetBuilder row = kit.Row(10.f);
			row.GrowWidth().Height(44.f).Padding(math::Sides(8.f, 0.f)).BorderRadius(6.f);
			row.Background(on ? WithAlpha(p.primary, 0.2f) : p.bg2).HoverStyle(ui::Style().Background(p.bg3));
			row.BorderColor(on ? p.primary : p.line).BorderWidth(1.f).OnClick([this, id = r->id] {
				m_selected = id;
				Refresh();
			}).OnDoubleClick([this, id = r->id] {
				if (Session *live = Live(shell))
					live->Craft(id, 1);
				Refresh();
			});
			ui::Widget e = m_list.Append(row);
			e.Append(ItemIcon(shell, ItemStack{r->result, r->resultCount, 0, nullptr}, 34.f));
			WidgetBuilder text = kit.Column(1.f);
			text.GrowWidth().PointerThrough();
			ui::Widget tx = e.Append(text);
			tx.Append(kit.Label(name, Text::SMALL).Bold().Color(blocker.IsEmpty() ? p.text : p.textMuted).PointerThrough());
			tx.Append(kit.Muted(blocker.IsEmpty() ? String("Prête à fabriquer") : blocker, Text::CAPTION)
				.Color(blocker.IsEmpty() ? p.success : locked ? p.accent : p.textDim)
				.PointerThrough());
			if (locked)
				e.Append(kit.Icon(MaterialIcons::LOCK, 16.f, Some(p.accent)).PointerThrough());
		}
		if (shown == 0)
			m_list.Append(kit.Muted("Aucune recette ici.", Text::SMALL));
		Detail();
	}

	void Detail() {
		Session *s = Live(shell);
		kit.ClearChildren(m_detail);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		Game &game = s->Game();
		const RecipeDef *r = game.Content().FindRecipe(m_selected);
		if (!r) {
			m_detail.Append(kit.Muted("Choisissez une recette pour voir ses ingrédients.", Text::SMALL));
			return;
		}
		WidgetBuilder head = kit.Row(10.f);
		ui::Widget h = m_detail.Append(head);
		h.Append(ItemIcon(shell, ItemStack{r->result, r->resultCount, 0, nullptr}, 64.f));
		WidgetBuilder ht = kit.Column(2.f);
		ht.GrowWidth();
		ui::Widget htt = h.Append(ht);
		htt.Append(kit.Label(s->ItemName(r->result), Text::H3).GrowWidth().TextWrap());
		htt.Append(kit.Muted(String::Format("Produit : %d", r->resultCount), Text::SMALL));
		m_detail.Append(kit.Label("Ingrédients", Text::SMALL).Bold());
		Inventory *inv = s->Inventory();
		for (const Ingredient &ing : RecipeNeeds(*r)) {
			const int have = inv ? inv->CountMatching(game.Content(), ing) : 0;
			WidgetBuilder row = kit.Row(8.f);
			row.GrowWidth();
			ui::Widget e = m_detail.Append(row);
			if (!ing.IsTag())
				e.Append(ItemIcon(shell, ItemStack{ing.item, 1, 0, nullptr}, 28.f, false));
			else
				e.Append(kit.Icon(MaterialIcons::CATEGORY, 22.f));
			e.Append(kit.Label(ing.IsTag() ? String("Tout « ") + ing.item.Substring(1) + String(" »") : s->ItemName(ing.item), Text::SMALL)
				.GrowWidth());
			e.Append(kit.Label(String::Format("%d / %d", have, ing.count), Text::SMALL).Bold().Color(have >= ing.count ? p.success : p.danger));
		}
		const String station = game.RecipeStation(*r);
		if (!station.IsEmpty()) {
			const BlockDef *def = game.Content().FindBlock(station);
			const bool near = std::find(m_stations.begin(), m_stations.end(), station) != m_stations.end();
			InfoRow(kit, m_detail, "Atelier", (def ? def->displayName : station) + (near ? String(" (à proximité)") : String(" (absent)")),
					Some(near ? p.success : p.danger));
		}
		if (r->kind == RecipeKind::COOKING)
			InfoRow(kit, m_detail, "Combustible", "1 par cuisson (charbon, bois, bâtons…)");
		if (r->requiresLevel > 0)
			InfoRow(kit, m_detail, "Niveau requis", String::Format("%d", r->requiresLevel), Some(p.accent));
		const String blocker = game.CraftBlocker(s->Player(), *r, m_stations);
		WidgetBuilder buttons = kit.Row(8.f);
		buttons.GrowWidth().Margin(math::Sides(0.f, 8.f, 0.f, 0.f));
		ui::Widget bt = m_detail.Append(buttons);
		for (const auto &[label, times] : std::vector<std::pair<const char *, int>>{{"Fabriquer", 1}, {"× 5", 5}, {"Tout", 999}}) {
			WidgetBuilder btn = times == 1 ? kit.PrimaryButton(label, [this, id = r->id, times] { Do(id, times); })
										   : kit.Button(label, [this, id = r->id, times] { Do(id, times); });
			if (!blocker.IsEmpty())
				btn.Disabled();
			bt.Append(btn);
		}
		if (!blocker.IsEmpty())
			m_detail.Append(kit.Muted(blocker, Text::CAPTION).Color(p.danger));
	}
	void Do(const String &id, int times) {
		if (Session *s = Live(shell))
			s->Craft(id, times);
		shell.UiSound("ui_confirm");
		Refresh();
	}

	std::vector<String> m_stations;
	String m_category = "all", m_query, m_selected;
	bool m_onlyCraftable = false;
	ui::Widget m_list{}, m_detail{};
};

// ── Discussion ───────────────────────────────────────────────────────────────

class Chat final : public Screen {
public:
	Chat(Shell &shell_, String prefix) : Screen(shell_), m_prefix(std::move(prefix)) {}
	String Id() const override { return "chat"; }
	bool Overlay() const override { return true; }

	void Build(ui::Widget root) override {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		const float sc = kit.Scale();
		root.Background(WithAlpha(p.bg0, 0.f));
		WidgetBuilder panel = kit.Column(6.f);
		panel.Absolute().Anchor(ui::Anchor::BottomLeft).Offset(14.f, -14.f).Width(600.f * sc);
		panel.Padding(math::Sides(10.f, 10.f)).BorderRadius(10.f).Background(WithAlpha(p.bg0, std::max(0.35f, shell.Prefs().chatOpacity)));
		ui::Widget pn = root.Append(panel);
		WidgetBuilder head = kit.Row(6.f);
		head.GrowWidth();
		ui::Widget h = pn.Append(head);
		h.Append(kit.Icon(MaterialIcons::CHAT, 18.f, Some(p.primary)));
		int i = 0;
		for (const char *label : {"Tout", "Joueurs", "Système", "Événements"}) {
			const bool on = i == m_filter;
			WidgetBuilder chip = kit.GhostButton(label, [this, i] {
				m_filter = i;
				Refresh();
			});
			chip.Height(26.f);
			if (on)
				chip.Color(p.primary);
			h.Append(chip);
			++i;
		}
		h.Append(kit.Spacer());
		h.Append(kit.IconButton(m_collapsed ? MaterialIcons::ARROW_UPWARD : MaterialIcons::ARROW_DOWNWARD, m_collapsed ? "Agrandir" : "Réduire", [this] {
			m_collapsed = !m_collapsed;
			shell.Rebuild();
		}));
		h.Append(kit.IconButton(MaterialIcons::CLOSE, "Masquer (Échap)", [this] { shell.Pop(); }));
		WidgetBuilder log = kit.Scroll(2.f);
		log.GrowWidth().Height((m_collapsed ? 80.f : 300.f) * sc);
		m_log = pn.Append(log);
		m_suggest = pn.Append(kit.Muted(String(), Text::CAPTION).Color(p.info));
		WidgetBuilder input = kit.Input("Message, ou / pour une commande (Tab : compléter, ↑ ↓ : historique)");
		input.MaxLength(256).OnSubmit([this](const String &text) {
			if (Session *live = Live(shell))
				live->Submit(text);
			shell.Pop();
		}).OnTextChange([this](const String &t) { Suggest(t); });
		m_input = pn.Append(input);
		kit.SetInputText(m_input, m_prefix);
		m_input.Focus();
		m_history = int(s->History().size());
		Refresh();
		Suggest(m_prefix);
	}
	void Update(float) override {
		if (Session *s = Live(shell); s && s->Log().size() != m_seen)
			Refresh();
	}
	bool OnKey(int code) override {
		Session *s = Live(shell);
		if (!s)
			return false;
		if (code == SDL_SCANCODE_TAB) {
			Complete();
			return true;
		}
		if (code == SDL_SCANCODE_UP || code == SDL_SCANCODE_DOWN) {
			auto &h = s->History();
			if (h.empty())
				return true;
			m_history = std::clamp(m_history + (code == SDL_SCANCODE_UP ? -1 : 1), 0, int(h.size()));
			kit.SetInputText(m_input, m_history < int(h.size()) ? h[size_t(m_history)] : String());
			return true;
		}
		return false;
	}

private:
	void Refresh() {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		kit.ClearChildren(m_log);
		m_seen = s->Log().size();
		std::vector<const LogLine *> lines;
		for (const LogLine &l : s->Log()) {
			const bool player = l.channel == "chat", system = l.channel == "system" || l.channel == "error" || l.channel == "result" ||
																 l.channel == "command",
					   event = l.channel == "event";
			if ((m_filter == 1 && !player) || (m_filter == 2 && !system) || (m_filter == 3 && !event))
				continue;
			lines.push_back(&l);
		}
		const size_t first = lines.size() > 60 ? lines.size() - 60 : 0;
		for (size_t i = first; i < lines.size(); ++i) {
			const LogLine &l = *lines[i];
			WidgetBuilder row = kit.Row(6.f);
			row.GrowWidth();
			ui::Widget r = m_log.Append(row);
			if (shell.Prefs().chatTimestamps) {
				const std::time_t t = std::time_t(l.time);
				char buf[16];
				std::strftime(buf, sizeof(buf), "%H:%M", std::localtime(&t));
				r.Append(kit.Muted(buf, Text::CAPTION));
			}
			const sdl3::FColor c = l.channel == "event"	  ? p.accent
								   : l.channel == "error" ? p.danger
								   : l.channel == "system" ? p.info
								   : l.channel == "command" ? p.textDim
															: p.text;
			if (!l.author.IsEmpty())
				r.Append(kit.Label(String("<") + l.author + String(">"), Text::SMALL).Bold().Color(p.primary));
			r.Append(kit.Label(l.text, Text::SMALL).Color(c).GrowWidth().TextWrap());
		}
	}
	void Suggest(const String &text) {
		Session *s = Live(shell);
		if (!s || !text.StartsWith("/")) {
			kit.SetText(m_suggest, String());
			kit.SetHidden(m_suggest, true);
			return;
		}
		const std::vector<String> options = s->Game().Complete(text);
		String list;
		for (size_t i = 0; i < options.size() && i < 10; ++i)
			list += (i ? String("   ") : String()) + options[i];
		kit.SetText(m_suggest, options.empty() ? String("Aucune suggestion") : list);
		kit.SetHidden(m_suggest, false);
	}
	void Complete() {
		Session *s = Live(shell);
		const String text = kit.InputText(m_input);
		const std::vector<String> options = s ? s->Game().Complete(text) : std::vector<String>{};
		if (options.size() != 1)
			return Suggest(text);
		size_t last = String::NPOS;
		for (size_t i = 0; i < text.GetSize(); ++i)
			if (text.CharAt(i) == ' ')
				last = i;
		kit.SetInputText(m_input, (last == String::NPOS ? (text.StartsWith("/") ? String("/") : String()) : text.Substring(0, last + 1)) + options[0] +
									  String(" "));
	}
	String m_prefix;
	ui::Widget m_log{}, m_input{}, m_suggest{};
	size_t m_seen = 0;
	int m_filter = 0, m_history = 0;
	bool m_collapsed = false;
};

// ── Terminal de commandes ────────────────────────────────────────────────────

class Console final : public Screen {
public:
	Console(Shell &shell_, String prefix) : Screen(shell_), m_prefix(std::move(prefix)) {}
	String Id() const override { return "console"; }
	bool Overlay() const override { return true; }

	void Build(ui::Widget root) override {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		const float sc = kit.Scale();
		WidgetBuilder panel = kit.Row(12.f);
		panel.Absolute().Anchor(ui::Anchor::Top).Offset(0.f, 16.f).Width(1100.f * sc).Height(470.f * sc);
		panel.Padding(math::Sides(14.f, 12.f)).BorderRadius(10.f).Background(WithAlpha(Rgb(6, 8, 12), 0.95f)).BorderColor(p.lineStrong).BorderWidth(1.f);
		panel.AlignItems(ui::Align::Stretch);
		ui::Widget pn = root.Append(panel);
		WidgetBuilder main = kit.Column(6.f);
		main.GrowWidth();
		ui::Widget m = pn.Append(main);
		WidgetBuilder head = kit.Row(8.f);
		head.GrowWidth();
		ui::Widget h = m.Append(head);
		h.Append(kit.Icon(MaterialIcons::KEYBOARD, 18.f, Some(p.primary)));
		h.Append(kit.Label("Terminal", Text::BODY).Bold());
		h.Append(kit.Muted(s->Game().Rule("commands") ? "commandes autorisées" : "commandes de triche désactivées dans ce monde", Text::CAPTION)
			.GrowWidth());
		h.Append(kit.IconButton(MaterialIcons::CLOSE, "Fermer (Échap)", [this] { shell.Pop(); }));
		WidgetBuilder out = kit.Scroll(1.f);
		out.GrowWidth().GrowHeight().Background(WithAlpha(Rgb(0, 0, 0), 0.35f)).BorderRadius(6.f).Padding(math::Sides(8.f, 6.f));
		m_output = m.Append(out);
		m_preview = m.Append(kit.Label(String(), Text::SMALL));
		WidgetBuilder inputRow = kit.Row(8.f);
		inputRow.GrowWidth();
		ui::Widget ir = m.Append(inputRow);
		ir.Append(kit.PixelTitle("›", 16.f, p.primary));
		WidgetBuilder input = kit.Input("/help — Tab : compléter · ↑ ↓ : historique");
		input.MaxLength(256).OnSubmit([this](const String &text) { Run(text); }).OnTextChange([this](const String &t) { Analyze(t); });
		m_input = ir.Append(input);
		kit.SetInputText(m_input, m_prefix);
		m_input.Focus();
		m_hint = m.Append(kit.Muted(String(), Text::CAPTION));
		m_suggestions = m.Append(kit.Row(6.f));
		// Commandes récentes.
		WidgetBuilder side = kit.Column(4.f);
		side.Width(230.f * sc);
		m_recent = pn.Append(side);
		m_history = int(s->History().size());
		Refresh();
		Analyze(m_prefix);
	}
	void Update(float) override {
		if (Session *s = Live(shell); s && s->Log().size() != m_seen)
			Refresh();
	}
	bool OnKey(int code) override {
		Session *s = Live(shell);
		if (!s)
			return false;
		if (code == SDL_SCANCODE_TAB) {
			const String text = kit.InputText(m_input);
			const std::vector<String> options = s->Game().Complete(text);
			if (options.size() == 1) {
				size_t last = String::NPOS;
				for (size_t i = 0; i < text.GetSize(); ++i)
					if (text.CharAt(i) == ' ')
						last = i;
				kit.SetInputText(m_input, (last == String::NPOS ? String("/") : text.Substring(0, last + 1)) + options[0] + String(" "));
				Analyze(kit.InputText(m_input));
			}
			return true;
		}
		if (code == SDL_SCANCODE_UP || code == SDL_SCANCODE_DOWN) {
			auto &h = s->History();
			if (h.empty())
				return true;
			m_history = std::clamp(m_history + (code == SDL_SCANCODE_UP ? -1 : 1), 0, int(h.size()));
			kit.SetInputText(m_input, m_history < int(h.size()) ? h[size_t(m_history)] : String("/"));
			Analyze(kit.InputText(m_input));
			return true;
		}
		return false;
	}

private:
	void Run(const String &text) {
		Session *s = Live(shell);
		if (!s || text.Trim().IsEmpty())
			return;
		s->History().push_back(text.Trim());
		(void)s->Command(text);
		m_history = int(s->History().size());
		kit.SetInputText(m_input, String("/"));
		m_input.Focus();
		Analyze(String("/"));
		Refresh();
	}
	void Refresh() {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		kit.ClearChildren(m_output);
		m_seen = s->Log().size();
		std::vector<const LogLine *> lines;
		for (const LogLine &l : s->Log())
			if (l.channel == "command" || l.channel == "result" || l.channel == "error")
				lines.push_back(&l);
		if (lines.empty())
			m_output.Append(kit.Muted("Tapez /help pour la liste des commandes.", Text::SMALL));
		for (size_t i = lines.size() > 80 ? lines.size() - 80 : 0; i < lines.size(); ++i) {
			const LogLine &l = *lines[i];
			WidgetBuilder lb = kit.Label(l.channel == "command" ? String("› ") + l.text : (l.channel == "error" ? String("✖ ") : String("  ")) + l.text,
										 Text::SMALL);
			lb.GrowWidth().TextWrap().Color(l.channel == "command" ? p.secondary : l.channel == "error" ? p.danger : p.text);
			if (l.channel == "command")
				lb.Bold();
			m_output.Append(lb);
		}
		kit.ClearChildren(m_recent);
		m_recent.Append(kit.Label("Commandes récentes", Text::SMALL).Bold());
		std::vector<String> recent;
		for (auto it = s->History().rbegin(); it != s->History().rend() && recent.size() < 12; ++it)
			if (it->StartsWith("/") && std::find(recent.begin(), recent.end(), *it) == recent.end())
				recent.push_back(*it);
		if (recent.empty())
			m_recent.Append(kit.Muted("Aucune pour l'instant", Text::CAPTION));
		for (const String &r : recent)
			m_recent.Append(kit.GhostButton(r, [this, r] {
				kit.SetInputText(m_input, r);
				Analyze(r);
			}).GrowWidth());
	}
	/// Coloration syntaxique (aperçu), suggestions et paramètres attendus.
	void Analyze(const String &text) {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		String name = text.StartsWith("/") ? text.Substring(1) : text;
		const size_t space = name.Find(" ");
		const String args = space == String::NPOS ? String() : name.Substring(space + 1);
		name = space == String::NPOS ? name : name.Substring(0, space);
		const Game::Command *command = nullptr;
		for (const Game::Command *c : s->Game().Commands())
			if (c->name == name)
				command = c;
		kit.SetText(m_preview, text.IsEmpty() ? String() : (String("/") + name + (args.IsEmpty() ? String() : String("  ") + args)));
		kit.SetColor(m_preview, name.IsEmpty() ? p.textDim : command ? p.success : p.danger);
		kit.SetText(m_hint, command ? command->usage + String(" — ") + command->help
									: name.IsEmpty() ? String("Saisissez une commande") : String("Commande inconnue"));
		kit.ClearChildren(m_suggestions);
		const std::vector<String> options = s->Game().Complete(text);
		for (size_t i = 0; i < options.size() && i < 8; ++i)
			m_suggestions.Append(kit.GhostButton(options[i], [this, text, o = options[i]] {
				size_t last = String::NPOS;
				for (size_t k = 0; k < text.GetSize(); ++k)
					if (text.CharAt(k) == ' ')
						last = k;
				const String done = (last == String::NPOS ? String("/") : text.Substring(0, last + 1)) + o + String(" ");
				kit.SetInputText(m_input, done);
				Analyze(done);
			}));
	}
	String m_prefix;
	ui::Widget m_output{}, m_input{}, m_preview{}, m_hint{}, m_suggestions{}, m_recent{};
	size_t m_seen = 0;
	int m_history = 0;
};

// ── Informations du monde ────────────────────────────────────────────────────

class WorldInfo final : public Screen {
public:
	explicit WorldInfo(Shell &shell_) : Screen(shell_) {}
	String Id() const override { return "worldinfo"; }
	bool Overlay() const override { return true; }

	void Build(ui::Widget root) override {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		Game &game = s->Game();
		ui::Widget card = ModalCard(kit, root, 1080.f);
		WidgetBuilder head = kit.Row(10.f);
		head.GrowWidth();
		ui::Widget h = card.Append(head);
		h.Append(kit.Icon(MaterialIcons::PUBLIC, 28.f, Some(p.primary)));
		WidgetBuilder ht = kit.Column(2.f);
		ht.GrowWidth();
		ui::Widget htt = h.Append(ht);
		htt.Append(kit.Label(game.Metadata().name, Text::H2));
		htt.Append(kit.Muted(String::Format("%s · %s · graine %u · générateur %s", GameModeName(GameModeId(game.Mode())).CStr(),
								 DifficultyName(DifficultyId(game.GetDifficulty())).CStr(), game.Metadata().seed, game.Metadata().generator.CStr()),
				  Text::SMALL));
		h.Append(kit.IconButton(MaterialIcons::CLOSE, "Fermer (Tab)", [this] { shell.Pop(); }));
		auto t = game.Registry().GetComponent<Transform>(s->Player());
		const math::FVector3 pos = t.IsSome() ? t.Unwrap()->position : math::FVector3{0.f, 0.f, 0.f};
		const BlockPos b = BlockPos::FromWorld(pos);
		const BiomeDef *biome = game.GetWorld().Generator() ? game.GetWorld().Generator()->BiomeAt(b.x, b.z) : nullptr;
		WidgetBuilder grid = kit.Row(12.f);
		grid.GrowWidth().AlignItems(ui::Align::Start);
		ui::Widget g = card.Append(grid);
		auto column = [&] {
			WidgetBuilder c = kit.Column(10.f);
			c.GrowWidth();
			return g.Append(c);
		};
		auto section = [&](ui::Widget parent, const char *title, MaterialIcons icon) {
			WidgetBuilder c = kit.Card(12.f);
			c.GrowWidth().Gap(4.f);
			ui::Widget e = parent.Append(c);
			WidgetBuilder tr = kit.Row(6.f);
			ui::Widget tt = e.Append(tr);
			tt.Append(kit.Icon(icon, 16.f, Some(p.primary)));
			tt.Append(kit.Label(title, Text::SMALL).Bold().Color(p.primary));
			return e;
		};
		ui::Widget c1 = column(), c2 = column(), c3 = column();
		const float day = game.DayFraction();
		const int hours = int(day * 24.f), minutes = int((day * 24.f - float(hours)) * 60.f);
		ui::Widget e = section(c1, "Temps", MaterialIcons::SCHEDULE);
		InfoRow(kit, e, "Temps de jeu total", FormatDuration(game.Metadata().playTime));
		InfoRow(kit, e, "Cette session", FormatDuration(s->Stats().sessionTime));
		InfoRow(kit, e, "Jour", String::Format("%d — %02d:%02d", int(game.GameTime() / double(game.Options().dayLength)) + 1, hours, minutes));
		InfoRow(kit, e, "Cycle jour / nuit", game.Rule("daylightCycle") ? String("actif") : String("figé"));
		e = section(c1, "Position", MaterialIcons::PERSON);
		InfoRow(kit, e, "Coordonnées", String::Format("X %d   Y %d   Z %d", b.x, b.y, b.z));
		InfoRow(kit, e, "Chunk", ChunkOf(b).ToString());
		InfoRow(kit, e, "Point d'apparition", String::Format("%.0f %.0f %.0f", double(game.Metadata().spawn.x), double(game.Metadata().spawn.y),
															 double(game.Metadata().spawn.z)));
		e = section(c2, "Région actuelle", MaterialIcons::TERRAIN);
		InfoRow(kit, e, "Biome", biome ? biome->displayName : String("—"));
		InfoRow(kit, e, "Climat", biome ? String::Format("température %.2f · humidité %.2f", double(biome->temperature), double(biome->humidity))
										: String("—"));
		int creatures = 0, hostile = 0;
		game.Registry().Query<Transform, EntityKind>([&](ecs::Entity, Transform &et, EntityKind &k) {
			if (k.def && (et.position - pos).Length() < 48.f) {
				++creatures;
				hostile += std::find(k.def->tags.begin(), k.def->tags.end(), String("hostile")) != k.def->tags.end();
			}
		});
		InfoRow(kit, e, "Créatures proches", String::Format("%d (dont %d hostiles)", creatures, hostile), Some(hostile > 0 ? p.warning : p.text));
		InfoRow(kit, e, "Chunks chargés", String::Format("%d", int(game.GetWorld().LoadedChunkCount())));
		e = section(c2, "Statistiques", MaterialIcons::BOLT);
		const SessionStats &st = s->Stats();
		InfoRow(kit, e, "Blocs cassés", String::Format("%d", st.blocksBroken));
		InfoRow(kit, e, "Blocs posés", String::Format("%d", st.blocksPlaced));
		InfoRow(kit, e, "Créatures vaincues", String::Format("%d", st.creaturesKilled));
		InfoRow(kit, e, "Objets fabriqués", String::Format("%d", st.itemsCrafted));
		InfoRow(kit, e, "Distance parcourue", String::Format("%.0f blocs", st.distance));
		InfoRow(kit, e, "Morts", String::Format("%d", st.deaths));
		if (auto sv = game.Registry().GetComponent<Survival>(s->Player()); sv.IsSome())
			InfoRow(kit, e, "Niveau d'expérience", String::Format("%d (%d points)", XpLevel(sv.Unwrap()->xp), sv.Unwrap()->xp));
		e = section(c3, "Mods actifs", MaterialIcons::EXTENSION);
		for (const ModInfo &m : game.Mods().mods)
			InfoRow(kit, e, shell.Prefs().ModName(m.id, m.name), String::Format("v%s · %d déf.", m.version.CStr(), m.definitions));
		e = section(c3, "Sauvegarde", MaterialIcons::SAVE);
		InfoRow(kit, e, "Dossier", s->Folder());
		InfoRow(kit, e, "Créé le", FormatDate(game.Metadata().created));
		InfoRow(kit, e, "Sauvegarde auto", shell.Prefs().autosaveMinutes > 0 ? String::Format("toutes les %d min", shell.Prefs().autosaveMinutes)
																			 : String("désactivée"));
		e.Append(kit.Button("Enregistrer maintenant", [this] {
			if (Session *live = Live(shell))
				live->Save(true);
		}));
	}
	bool OnKey(int code) override {
		if (shell.Prefs().Matches("worldInfo", code)) {
			shell.Pop();
			return true;
		}
		return false;
	}
};

// ── Commerce avec un PNJ ─────────────────────────────────────────────────────

class Trade final : public Screen {
public:
	Trade(Shell &shell_, UiRequest request) : Screen(shell_), m_request(std::move(request)) {}
	String Id() const override { return "trade"; }
	bool Overlay() const override { return true; }

	void Build(ui::Widget root) override {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		ui::Widget card = ModalCard(kit, root, 620.f);
		WidgetBuilder head = kit.Row(12.f);
		head.GrowWidth();
		ui::Widget h = card.Append(head);
		h.Append(kit.Icon(MaterialIcons::STOREFRONT, 30.f, Some(p.accent)));
		WidgetBuilder ht = kit.Column(2.f);
		ht.GrowWidth();
		ui::Widget htt = h.Append(ht);
		htt.Append(kit.Label(json::Str(m_request.data, "title", "Marchand"), Text::H2));
		htt.Append(kit.Muted(json::Str(m_request.data, "profession", "Échanges"), Text::SMALL));
		h.Append(kit.IconButton(MaterialIcons::CLOSE, "Fermer", [this] { shell.Pop(); }));
		Inventory *inv = s->Inventory();
		NodePtr offers = json::At(m_request.data, "offers");
		for (size_t i = 0; offers && i < offers->GetSize(); ++i) {
			NodePtr o = offers->At(i);
			const String giveId = json::Str(json::At(o, "give")->At(0), ""), getId = json::Str(json::At(o, "get")->At(0), "");
			const int giveN = json::Int(json::At(o, "give")->At(1), "", 1), getN = json::Int(json::At(o, "get")->At(1), "", 1);
			const int have = inv ? inv->Count(giveId) : 0;
			WidgetBuilder row = kit.Row(10.f);
			row.GrowWidth().Padding(math::Sides(10.f, 8.f)).BorderRadius(8.f).Background(p.bg2).BorderColor(p.line).BorderWidth(1.f);
			ui::Widget r = card.Append(row);
			r.Append(ItemIcon(shell, ItemStack{giveId, giveN, 0, nullptr}, 40.f));
			WidgetBuilder gc = kit.Column(1.f);
			gc.Width(150.f);
			ui::Widget g = r.Append(gc);
			g.Append(kit.Label(s->ItemName(giveId), Text::SMALL));
			g.Append(kit.Label(String::Format("vous avez %d / %d", have, giveN), Text::CAPTION).Color(have >= giveN ? p.success : p.danger));
			r.Append(kit.Icon(MaterialIcons::ARROW_FORWARD, 22.f, Some(p.textMuted)));
			r.Append(ItemIcon(shell, ItemStack{getId, getN, 0, nullptr}, 40.f));
			r.Append(kit.Label(String::Format("%d × %s", getN, s->ItemName(getId).CStr()), Text::SMALL).GrowWidth());
			WidgetBuilder btn = kit.PrimaryButton("Échanger", [this, giveId, giveN, getId, getN] {
				if (Session *live = Live(shell)) {
					Result<bool, String> res = live->Game().Trade(live->Player(), giveId, giveN, getId, getN);
					if (res.IsError())
						shell.Notify(Tone::WARNING, "Échange impossible", res.Error());
				}
				shell.Rebuild();
			});
			if (have < giveN)
				btn.Disabled();
			r.Append(btn);
		}
	}

private:
	UiRequest m_request;
};

// ── Rangement (coffre, armoire…) ─────────────────────────────────────────────

/// Contenu d'un bloc rangement (en haut) et inventaire du joueur (en bas) :
/// clic pour prendre / poser, clic droit pour la moitié / un seul, Maj + clic
/// pour passer d'un côté à l'autre. Fermé si le bloc disparaît ou s'éloigne.
class ContainerScreen final : public Screen {
public:
	ContainerScreen(Shell &shell_, UiRequest request)
		: Screen(shell_), m_request(std::move(request)),
		  m_pos{json::Int(m_request.data, "x", 0), json::Int(m_request.data, "y", 0), json::Int(m_request.data, "z", 0)},
		  m_columns(std::clamp(json::Int(m_request.data, "columns", 9), 1, 12)) {}
	String Id() const override { return "container"; }
	bool Overlay() const override { return true; }
	~ContainerScreen() override {
		ReturnHeld();
		if (Session *s = Live(shell))
			s->Game().PlaySound(json::Str(m_request.data, "closeSound"), m_pos.Center());
	}

	void Build(ui::Widget root) override {
		Session *s = Live(shell);
		Inventory *box = Box();
		if (!s || !box)
			return;
		const Palette &p = kit.Colors();
		const float sc = kit.Scale();
		const float cell = 50.f * sc;
		ui::Widget card = ModalCard(kit, root, std::max(float(m_columns), 9.f) * cell + 80.f * sc);
		WidgetBuilder head = kit.Row(10.f);
		head.GrowWidth();
		ui::Widget h = card.Append(head);
		h.Append(kit.Icon(MaterialIcons::INVENTORY2, 26.f, Some(p.primary)));
		h.Append(kit.Label(json::Str(m_request.data, "title", "Rangement"), Text::H2));
		h.Append(kit.Spacer());
		m_fill = h.Append(kit.Chip(String(), Tone::NEUTRAL));
		h.Append(kit.IconButton(MaterialIcons::CLOSE, "Fermer (E)", [this] { shell.Pop(); }));
		const int rows = (box->Size() + m_columns - 1) / m_columns;
		WidgetBuilder top = ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &rect) {
			if (painter.Renderer())
				DrawBox(*painter.Renderer(), rect);
		});
		top.Size(cell * float(m_columns), cell * float(rows)).OnClick([this] { Click(false); }).OnContextMenu([this](sdl3::FPoint) { Click(true); });
		card.Append(top);
		WidgetBuilder actions = kit.Row(8.f);
		ui::Widget a = card.Append(actions);
		a.Append(kit.Button("Tout ranger", [this] { MoveAll(true); }));
		a.Append(kit.Button("Tout prendre", [this] { MoveAll(false); }));
		a.Append(kit.Label("Inventaire", Text::SMALL).Bold());
		WidgetBuilder bottom = ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &rect) {
			if (painter.Renderer())
				DrawPlayer(*painter.Renderer(), rect);
		});
		bottom.Size(cell * 9.f, cell * 4.f + 12.f).OnClick([this] { Click(false); }).OnContextMenu([this](sdl3::FPoint) { Click(true); });
		card.Append(bottom);
		card.Append(kit.Muted("Clic : prendre / poser · Clic droit : moitié / un seul · Maj + clic : vers l'autre côté", Text::CAPTION));
	}

	void Update(float) override {
		Session *s = Live(shell);
		Inventory *box = Box();
		// Rangement cassé ou joueur trop loin : on ferme.
		bool far = true;
		if (s)
			if (auto t = s->Game().Registry().GetComponent<Transform>(s->Player()); t.IsSome())
				far = (t.Unwrap()->position - m_pos.Center()).Length() > 8.f;
		if (!s || !box || far) {
			if (shell.Top() == this) // fermé seulement au premier plan (pas un autre écran)
				shell.Pop();
			return; // écran détruit : plus rien à toucher
		}
		int used = 0;
		for (int i = 0; i < box->Size(); ++i)
			used += !box->Slot(i).IsEmpty();
		kit.SetText(m_fill, String::Format("%d / %d emplacements", used, box->Size()));
	}
	bool OnKey(int code) override {
		if (shell.Prefs().Matches("inventory", code) && !kit.Ui().Focused()) {
			shell.Pop();
			return true;
		}
		return false;
	}

private:
	Inventory *Box() {
		Session *s = Live(shell);
		Inventory *box = s && s->Game().HasWorld() ? s->Game().BlockInventory(m_pos) : nullptr;
		return box && box->Size() == json::Int(m_request.data, "slots", box->Size()) ? box : nullptr;
	}
	/// Case sous la souris : (rangement ?, index) ; index -1 : aucune.
	std::pair<bool, int> SlotAt(float mx, float my) const {
		auto inside = [&](const sdl3::FRect &r) { return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h; };
		if (inside(m_boxRect)) {
			const float cell = m_boxRect.w / float(m_columns);
			return {true, int((my - m_boxRect.y) / cell) * m_columns + int((mx - m_boxRect.x) / cell)};
		}
		if (inside(m_playerRect)) {
			const float cell = m_playerRect.w / 9.f;
			const int col = int((mx - m_playerRect.x) / cell);
			const float ry = my - m_playerRect.y;
			if (ry >= cell * 3.f + 12.f)
				return {false, col}; // barre d'objets
			if (ry < cell * 3.f)
				return {false, HOTBAR + int(ry / cell) * HOTBAR + col};
		}
		return {false, -1};
	}
	void DrawSlot(sdl3::Renderer &ren, const sdl3::FRect &cell, const ItemStack &stack, bool hovered) {
		const Palette &p = kit.Colors();
		ren.SetDrawColor(hovered ? p.bg3 : p.bg1);
		(void)ren.FillRect(cell);
		ren.SetDrawColor(p.line);
		(void)ren.DrawRect(cell);
		if (Session *s = Live(shell))
			s->DrawItem(ren, stack, cell);
	}
	void DrawBox(sdl3::Renderer &ren, sdl3::FRect r) {
		m_boxRect = r;
		Inventory *box = Box();
		if (!box)
			return;
		(void)ren.SetBlendMode(sdl3::BlendMode::BLEND);
		float mx = 0.f, my = 0.f;
		(void)SDL_GetMouseState(&mx, &my);
		const auto [inBox, hover] = SlotAt(mx, my);
		const float cell = r.w / float(m_columns);
		for (int i = 0; i < box->Size(); ++i) {
			const sdl3::FRect c{r.x + float(i % m_columns) * cell + 2.f, r.y + float(i / m_columns) * cell + 2.f, cell - 4.f, cell - 4.f};
			DrawSlot(ren, c, box->Slot(i), inBox && hover == i);
		}
	}
	void DrawPlayer(sdl3::Renderer &ren, sdl3::FRect r) {
		Session *s = Live(shell);
		m_playerRect = r;
		Inventory *inv = s ? s->Inventory() : nullptr;
		if (!inv)
			return;
		(void)ren.SetBlendMode(sdl3::BlendMode::BLEND);
		float mx = 0.f, my = 0.f;
		(void)SDL_GetMouseState(&mx, &my);
		const auto [inBox, hover] = SlotAt(mx, my);
		const float cell = r.w / 9.f;
		for (int i = 0; i < std::min(inv->Size(), 36); ++i) {
			const float y = i < HOTBAR ? r.y + cell * 3.f + 12.f : r.y + float((i - HOTBAR) / HOTBAR) * cell;
			const sdl3::FRect c{r.x + float(i % HOTBAR) * cell + 2.f, y + 2.f, cell - 4.f, cell - 4.f};
			DrawSlot(ren, c, inv->Slot(i), !inBox && hover == i);
		}
		// Pile tenue : suit la souris (dessinée en dernier, par-dessus les deux grilles).
		if (!m_held.IsEmpty())
			s->DrawItem(ren, m_held, sdl3::FRect{mx - cell * 0.4f, my - cell * 0.4f, cell * 0.8f, cell * 0.8f});
	}
	void Click(bool right) {
		Session *s = Live(shell);
		Inventory *box = Box();
		Inventory *inv = s ? s->Inventory() : nullptr;
		if (!s || !box || !inv)
			return;
		float mx = 0.f, my = 0.f;
		(void)SDL_GetMouseState(&mx, &my);
		const auto [inBox, slot] = SlotAt(mx, my);
		Inventory &from = inBox ? *box : *inv;
		Inventory &other = inBox ? *inv : *box;
		if (slot < 0 || slot >= from.Size())
			return;
		ItemStack &target = from.Slot(slot);
		shell.UiSound("ui_click");
		Game &game = s->Game();
		if ((SDL_GetModState() & SDL_KMOD_SHIFT) && m_held.IsEmpty() && !target.IsEmpty()) {
			const int left = other.Add(game.Content(), target.item, target.count, target.meta);
			target.count = left;
			if (left <= 0)
				target = ItemStack{};
			return;
		}
		const ItemDef *def = game.Content().FindItem(m_held.IsEmpty() ? target.item : m_held.item);
		const int maxStack = def ? def->maxStack : 64;
		if (m_held.IsEmpty()) {
			if (target.IsEmpty())
				return;
			const int taken = right ? (target.count + 1) / 2 : target.count;
			m_held = ItemStack{target.item, taken, target.wear, target.meta};
			target.count -= taken;
			if (target.count <= 0)
				target = ItemStack{};
			return;
		}
		if (target.IsEmpty() || target.item == m_held.item) {
			const int room = maxStack - (target.IsEmpty() ? 0 : target.count);
			const int moved = std::min(right ? 1 : m_held.count, room);
			if (moved <= 0)
				return;
			if (target.IsEmpty())
				target = ItemStack{m_held.item, 0, m_held.wear, m_held.meta};
			target.count += moved;
			m_held.count -= moved;
			if (m_held.count <= 0)
				m_held = ItemStack{};
		} else {
			std::swap(target, m_held);
		}
	}
	/// Tout ranger (sac → rangement, barre d'objets gardée) ou tout prendre.
	void MoveAll(bool store) {
		Session *s = Live(shell);
		Inventory *box = Box();
		Inventory *inv = s ? s->Inventory() : nullptr;
		if (!box || !inv)
			return;
		Inventory &from = store ? *inv : *box;
		Inventory &to = store ? *box : *inv;
		for (int i = store ? HOTBAR : 0; i < from.Size(); ++i)
			if (ItemStack &st = from.Slot(i); !st.IsEmpty()) {
				st.count = to.Add(s->Game().Content(), st.item, st.count, st.meta);
				if (st.count <= 0)
					st = ItemStack{};
			}
		shell.UiSound("ui_click");
	}
	void ReturnHeld() {
		Session *s = Live(shell);
		if (!s || m_held.IsEmpty())
			return;
		if (Inventory *inv = s->Inventory()) {
			const int left = inv->Add(s->Game().Content(), m_held.item, m_held.count, m_held.meta);
			if (left > 0)
				if (auto t = s->Game().Registry().GetComponent<Transform>(s->Player()); t.IsSome())
					(void)s->Game().DropItem(ItemStack{m_held.item, left, m_held.wear, m_held.meta}, t.Unwrap()->position + math::FVector3{0.f, 1.f, 0.f});
		}
		m_held = ItemStack{};
	}

	UiRequest m_request;
	BlockPos m_pos;
	int m_columns = 9;
	ItemStack m_held;
	sdl3::FRect m_boxRect{}, m_playerRect{};
	ui::Widget m_fill{};
};

class Dialog final : public Screen {
public:
	Dialog(Shell &shell_, UiRequest request) : Screen(shell_), m_request(std::move(request)) {}
	String Id() const override { return "dialog"; }
	bool Overlay() const override { return true; }

	void Build(ui::Widget root) override {
		const Palette &p = kit.Colors();
		ui::Widget card = ModalCard(kit, root, 520.f);
		WidgetBuilder head = kit.Row(12.f);
		ui::Widget h = card.Append(head);
		h.Append(kit.Icon(MaterialIcons::CHAT, 26.f, Some(p.primary)));
		h.Append(kit.Label(json::Str(m_request.data, "title", "PNJ"), Text::H2));
		WidgetBuilder bubble = kit.Column(4.f);
		bubble.GrowWidth().Padding(math::Sides(14.f, 12.f)).BorderRadius(10.f).Background(p.bg2);
		ui::Widget b = card.Append(bubble);
		b.Append(kit.Paragraph(json::Str(m_request.data, "text"), Text::BODY).Color(p.text));
		WidgetBuilder buttons = kit.Row(10.f);
		buttons.GrowWidth().JustifyContent(ui::Justify::End);
		ui::Widget bt = card.Append(buttons);
		std::vector<String> options = json::StrList(m_request.data, "options");
		if (options.empty())
			options.push_back("D'accord");
		for (size_t i = 0; i < options.size(); ++i) {
			WidgetBuilder btn = i + 1 == options.size() ? kit.PrimaryButton(options[i], [this] { shell.Pop(); })
														 : kit.Button(options[i], [this] { shell.Pop(); });
			bt.Append(btn);
		}
	}

private:
	UiRequest m_request;
};

} // namespace

std::unique_ptr<Screen> MakePause(Shell &shell) { return std::make_unique<Pause>(shell); }
std::unique_ptr<Screen> MakeInventory(Shell &shell) { return std::make_unique<InventoryScreen>(shell); }
std::unique_ptr<Screen> MakeCrafting(Shell &shell) { return std::make_unique<Crafting>(shell); }
std::unique_ptr<Screen> MakeChat(Shell &shell, String prefix) { return std::make_unique<Chat>(shell, std::move(prefix)); }
std::unique_ptr<Screen> MakeConsole(Shell &shell, String prefix) { return std::make_unique<Console>(shell, std::move(prefix)); }
std::unique_ptr<Screen> MakeWorldInfo(Shell &shell) { return std::make_unique<WorldInfo>(shell); }
std::unique_ptr<Screen> MakeTrade(Shell &shell, UiRequest request) { return std::make_unique<Trade>(shell, std::move(request)); }
std::unique_ptr<Screen> MakeDialog(Shell &shell, UiRequest request) { return std::make_unique<Dialog>(shell, std::move(request)); }
std::unique_ptr<Screen> MakeContainer(Shell &shell, UiRequest request) { return std::make_unique<ContainerScreen>(shell, std::move(request)); }

} // namespace voxelia
