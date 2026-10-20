// Écrans de la partie : pause, inventaire, fabrication, discussion, terminal,
// informations du monde, commerce, dialogue.
#include "common.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <voxel/inventory.hpp>

namespace voxelgame {

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
	explicit Pause(Shell &shell) : Screen(shell) {}
	String Id() const override { return "pause"; }
	bool Overlay() const override { return true; }

	void Build(ecs::Entity root) override {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		const ecs::Entity card = ModalCard(kit, root, 420.f);
		kit.PixelTitle(s->Game().Metadata().name, 22.f, p.text, true).Parent(card).Spawn();
		kit.Muted(String::Format("%s · %s · %s de jeu", GameModeName(GameModeId(s->Game().Mode())).CStr(),
								 DifficultyName(DifficultyId(s->Game().GetDifficulty())).CStr(),
								 FormatDuration(s->Game().Metadata().playTime).CStr()),
				  Text::SMALL)
			.AlignSelf(ui::CrossAlign::Center)
			.Parent(card)
			.Spawn();
		kit.Divider().Parent(card).Spawn();
		kit.MenuButton(card, MaterialIcons::PLAY_ARROW, "Reprendre la partie", "Échap", [this] { shell.ClearScreens(); }, true);
		kit.MenuButton(card, MaterialIcons::BACKPACK, "Inventaire", String(), [this] {
			shell.Pop();
			shell.Push(MakeInventory(shell));
		});
		kit.MenuButton(card, MaterialIcons::SETTINGS, "Paramètres", String(), [this] { shell.Push(MakeSettings(shell)); });
		kit.MenuButton(card, MaterialIcons::INFO, "Informations du monde", String(), [this] { shell.Push(MakeWorldInfo(shell)); });
		kit.MenuButton(card, MaterialIcons::TUNE, "Configuration du monde", String(), [this] { shell.Push(MakeWorldSettings(shell, String())); });
		kit.MenuButton(card, MaterialIcons::SAVE, "Enregistrer", "F6", [this] {
			if (Session *s = Live(shell))
				s->Save(true);
		});
		kit.Divider().Parent(card).Spawn();
		kit.DangerButton("Enregistrer et quitter le monde", [this] { AskQuit(); }).GrowW().Parent(card).Spawn();
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
	explicit InventoryScreen(Shell &shell) : Screen(shell) {}
	String Id() const override { return "inventory"; }
	bool Overlay() const override { return true; }
	~InventoryScreen() override { ReturnHeld(); }

	void Build(ecs::Entity root) override {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		const float sc = kit.Scale();
		Game &game = s->Game();
		const bool creative = game.Mode() == GameMode::CREATIVE;
		const ecs::Entity card = ModalCard(kit, root, creative ? 1180.f : 1040.f);
		// En-tête : titre, recherche, catégories, capacité.
		WidgetBuilder head = kit.Row(10.f);
		head.GrowW().Parent(card);
		const ecs::Entity h = head.Spawn();
		kit.Icon(MaterialIcons::BACKPACK, 26.f, Some(p.primary)).Parent(h).Spawn();
		kit.Label("Inventaire", Text::H2).Parent(h).Spawn();
		WidgetBuilder search = kit.Input("Rechercher un objet…");
		search.W(Dimension::Px(240.f * sc)).OnTextChange([this](const String &q) { m_query = q.ToLower(); });
		const ecs::Entity se = search.Parent(h).Spawn();
		kit.SetInputText(se, m_query);
		kit.Combo(ITEM_CATEGORIES, m_category, [this](int i) { m_category = i; }).Parent(h).Spawn();
		int used = 0;
		if (Inventory *inv = s->Inventory())
			for (int i = 0; i < inv->Size(); ++i)
				used += !inv->Slot(i).IsEmpty();
		kit.Spacer().Parent(h).Spawn();
		kit.Chip(String::Format("%d / %d emplacements", used, s->Inventory() ? s->Inventory()->Size() : 0), used > 32 ? Tone::WARNING : Tone::NEUTRAL)
			.Parent(h)
			.Spawn();
		kit.IconButton(MaterialIcons::CLOSE, "Fermer (E)", [this] { shell.Pop(); }).Parent(h).Spawn();

		WidgetBuilder body = kit.Row(18.f);
		body.GrowW().Align(ui::CrossAlign::Start).Parent(card);
		const ecs::Entity b = body.Spawn();
		// Équipement : personnage + 4 emplacements d'armure.
		WidgetBuilder equip = kit.Card(12.f);
		equip.W(Dimension::Px(170.f * sc)).Align(ui::CrossAlign::Center).Parent(b);
		const ecs::Entity eq = equip.Spawn();
		kit.Label("Équipement", Text::SMALL).Bold().Parent(eq).Spawn();
		WidgetBuilder row = kit.Row(8.f);
		row.Parent(eq);
		const ecs::Entity r = row.Spawn();
		String skin;
		if (auto v = game.Registry().GetComponent<Visual>(s->Player()); v.IsSome())
			skin = v.Unwrap()->skin;
		SkinFront(shell, skin, 150.f * sc).Parent(r).Spawn();
		WidgetBuilder armor = f.Canvas([this](sdl3::Renderer &ren, sdl3::FRect rect) { DrawArmor(ren, rect); });
		armor.Size(46.f * sc, 4.f * 48.f * sc).OnClick([this] { ClickArmor(); }).Parent(r);
		armor.Spawn();
		m_armorPoints = kit.Muted(String(), Text::CAPTION).Parent(eq).Spawn();
		// Grille.
		WidgetBuilder center = kit.Column(8.f);
		center.Parent(b);
		const ecs::Entity c = center.Spawn();
		WidgetBuilder grid = f.Canvas([this](sdl3::Renderer &ren, sdl3::FRect rect) { DrawGrid(ren, rect); });
		const float cell = 50.f * sc;
		grid.Size(cell * 9.f, cell * 4.f + 12.f).OnClick([this] { ClickGrid(false); }).OnContextMenu([this](float, float) { ClickGrid(true); });
		grid.Parent(c).Spawn();
		kit.Muted("Clic : prendre / poser · Clic droit : moitié / un seul · Maj + clic : déplacer vers la barre d'objets", Text::CAPTION)
			.Parent(c)
			.Spawn();
		if (creative) {
			kit.Label("Catalogue créatif — cliquez pour prendre une pile", Text::SMALL).Bold().Parent(c).Spawn();
			WidgetBuilder catalog = f.Canvas([this](sdl3::Renderer &ren, sdl3::FRect rect) { DrawCatalog(ren, rect); });
			catalog.Size(cell * 9.f, cell * 4.f).OnClick([this] { ClickCatalog(); }).Parent(c).Spawn();
			WidgetBuilder pages = kit.Row(8.f);
			pages.Parent(c);
			const ecs::Entity pg = pages.Spawn();
			kit.IconButton(MaterialIcons::ARROW_BACK, "Page précédente", [this] { m_catalogPage = std::max(0, m_catalogPage - 1); }).Parent(pg).Spawn();
			m_catalogLabel = kit.Muted(String(), Text::CAPTION).Parent(pg).Spawn();
			kit.IconButton(MaterialIcons::ARROW_FORWARD, "Page suivante", [this] { ++m_catalogPage; }).Parent(pg).Spawn();
		}
		// Détail de l'objet survolé ou sélectionné.
		WidgetBuilder detail = kit.Card(14.f);
		detail.W(Dimension::Px(240.f * sc)).Parent(b);
		const ecs::Entity d = detail.Spawn();
		WidgetBuilder icon = f.Canvas([this](sdl3::Renderer &ren, sdl3::FRect rect) {
			if (Session *s = Live(shell); s && !m_detail.IsEmpty())
				s->DrawItem(ren, m_detail, rect, false);
		});
		icon.Size(64.f * sc, 64.f * sc).Parent(d).Spawn();
		m_detailName = kit.Label(String(), Text::H3).GrowW().TextWrap().Parent(d).Spawn();
		m_detailInfo = kit.Paragraph(String(), Text::SMALL).Parent(d).Spawn();
		WidgetBuilder actions = kit.Column(6.f);
		actions.GrowW().Parent(d);
		m_actions = actions.Spawn();
		kit.Button("Utiliser / équiper", [this] { UseSelected(); }).GrowW().Parent(m_actions).Spawn();
		kit.Button("Jeter un", [this] { DropSelected(false); }).GrowW().Parent(m_actions).Spawn();
		kit.DangerButton("Jeter la pile", [this] { DropSelected(true); }).GrowW().Parent(m_actions).Spawn();
		kit.Button("Fabrication…", [this] {
			shell.Pop();
			shell.Push(MakeCrafting(shell));
		}).GrowW().Parent(d).Spawn();
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
		if (shell.Prefs().Matches("inventory", code) && kit.Ui().KeyboardFocus() == ecs::Entity{}) {
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
	ecs::Entity m_detailName{}, m_detailInfo{}, m_actions{}, m_armorPoints{}, m_catalogLabel{};
};

// ── Fabrication ──────────────────────────────────────────────────────────────

class Crafting final : public Screen {
public:
	explicit Crafting(Shell &shell) : Screen(shell) {}
	String Id() const override { return "crafting"; }
	bool Overlay() const override { return true; }

	void Build(ecs::Entity root) override {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		const float sc = kit.Scale();
		Game &game = s->Game();
		m_stations = game.NearbyStations(s->Player());
		const ecs::Entity card = ModalCard(kit, root, 1160.f);
		WidgetBuilder head = kit.Row(10.f);
		head.GrowW().Parent(card);
		const ecs::Entity h = head.Spawn();
		kit.Icon(MaterialIcons::BUILD, 26.f, Some(p.primary)).Parent(h).Spawn();
		kit.Label("Fabrication", Text::H2).Parent(h).Spawn();
		// Ateliers : à la main + ceux qui sont à proximité (extensible par les mods).
		WidgetBuilder stations = kit.Row(6.f);
		stations.GrowW().Parent(h);
		const ecs::Entity st = stations.Spawn();
		kit.Chip("À la main", Tone::SUCCESS).Parent(st).Spawn();
		std::vector<String> known;
		for (const RecipeDef *r : game.Content().Recipes())
			if (const String station = game.RecipeStation(*r); !station.IsEmpty() && std::find(known.begin(), known.end(), station) == known.end())
				known.push_back(station);
		for (const String &station : known) {
			const BlockDef *def = game.Content().FindBlock(station);
			const bool near = std::find(m_stations.begin(), m_stations.end(), station) != m_stations.end();
			kit.Chip((def ? def->displayName : station) + (near ? String(" · à proximité") : String(" · absent")), near ? Tone::SUCCESS : Tone::NEUTRAL)
				.Parent(st)
				.Spawn();
		}
		kit.IconButton(MaterialIcons::CLOSE, "Fermer (C)", [this] { shell.Pop(); }).Parent(h).Spawn();
		WidgetBuilder body = kit.Row(16.f);
		body.GrowW().H(Dimension::Px(560.f * sc)).Align(ui::CrossAlign::Stretch).Parent(card);
		const ecs::Entity b = body.Spawn();
		// Catégories.
		WidgetBuilder cats = kit.Column(2.f);
		cats.W(Dimension::Px(180.f * sc)).Parent(b);
		const ecs::Entity cs = cats.Spawn();
		NodePtr list = json::At(game.Content().Config(), "crafting.categories");
		for (size_t i = 0; list && i < list->GetSize(); ++i) {
			const String id = json::Str(list->At(i), "id"), name = json::Str(list->At(i), "name", id);
			const bool on = id == m_category;
			WidgetBuilder item = kit.Row(6.f);
			item.GrowW().H(Dimension::Px(30.f * sc)).Pad(math::Sides(10.f, 0.f)).Radius(6.f);
			ui::UiStyle sty;
			sty.SetBg(on ? WithAlpha(p.primary, 0.18f) : WithAlpha(p.bg3, 0.f)).SetBgHovered(p.bg3);
			item.Bg(sdl3::FColor{}).Style(std::move(sty)).OnClick([this, id] {
				m_category = id;
				Refresh();
				shell.Rebuild();
			}).Parent(cs);
			const ecs::Entity it = item.Spawn();
			kit.Label(name, Text::SMALL).TextColor(on ? p.text : p.textMuted).PointerThrough().Parent(it).Spawn();
		}
		// Liste des recettes.
		WidgetBuilder mid = kit.Column(8.f);
		mid.GrowW().Parent(b);
		const ecs::Entity m = mid.Spawn();
		WidgetBuilder tools = kit.Row(8.f);
		tools.GrowW().Parent(m);
		const ecs::Entity t = tools.Spawn();
		kit.Icon(MaterialIcons::SEARCH, 18.f).Parent(t).Spawn();
		WidgetBuilder search = kit.Input("Rechercher une recette…");
		search.OnTextChange([this](const String &q) {
			m_query = q.ToLower();
			Refresh();
		});
		const ecs::Entity se = search.Parent(t).Spawn();
		kit.SetInputText(se, m_query);
		kit.Check(m_onlyCraftable, [this](bool v) {
			m_onlyCraftable = v;
			Refresh();
		}).Parent(t).Spawn();
		kit.Muted("Réalisables", Text::SMALL).Parent(t).Spawn();
		WidgetBuilder scroll = kit.Scroll(4.f);
		scroll.GrowW().GrowH().Parent(m);
		m_list = scroll.Spawn();
		// Détail.
		WidgetBuilder right = kit.Card(14.f);
		right.W(Dimension::Px(330.f * sc)).Parent(b);
		m_detail = right.Spawn();
		Refresh();
	}
	bool OnKey(int code) override {
		if (shell.Prefs().Matches("crafting", code) && kit.Ui().KeyboardFocus() == ecs::Entity{}) {
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
			row.GrowW().H(Dimension::Px(44.f)).Pad(math::Sides(8.f, 0.f)).Radius(6.f);
			ui::UiStyle st;
			st.SetBg(on ? WithAlpha(p.primary, 0.2f) : p.bg2).SetBgHovered(p.bg3).SetBorderColor(on ? p.primary : p.line).SetBordersWidth(1.f);
			row.Bg(sdl3::FColor{}).Style(std::move(st)).OnClick([this, id = r->id] {
				m_selected = id;
				Refresh();
			}).OnDoubleClick([this, id = r->id] {
				if (Session *s = Live(shell))
					s->Craft(id, 1);
				Refresh();
			}).Parent(m_list);
			const ecs::Entity e = row.Spawn();
			ItemIcon(shell, ItemStack{r->result, r->resultCount, 0, nullptr}, 34.f).Parent(e).Spawn();
			WidgetBuilder text = kit.Column(1.f);
			text.GrowW().PointerThrough().Parent(e);
			const ecs::Entity tx = text.Spawn();
			kit.Label(name, Text::SMALL).Bold().TextColor(blocker.IsEmpty() ? p.text : p.textMuted).PointerThrough().Parent(tx).Spawn();
			kit.Muted(blocker.IsEmpty() ? String("Prête à fabriquer") : blocker, Text::CAPTION)
				.TextColor(blocker.IsEmpty() ? p.success : locked ? p.accent : p.textDim)
				.PointerThrough()
				.Parent(tx)
				.Spawn();
			if (locked)
				kit.Icon(MaterialIcons::LOCK, 16.f, Some(p.accent)).PointerThrough().Parent(e).Spawn();
		}
		if (shown == 0)
			kit.Muted("Aucune recette ici.", Text::SMALL).Parent(m_list).Spawn();
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
			kit.Muted("Choisissez une recette pour voir ses ingrédients.", Text::SMALL).Parent(m_detail).Spawn();
			return;
		}
		WidgetBuilder head = kit.Row(10.f);
		head.Parent(m_detail);
		const ecs::Entity h = head.Spawn();
		ItemIcon(shell, ItemStack{r->result, r->resultCount, 0, nullptr}, 64.f).Parent(h).Spawn();
		WidgetBuilder ht = kit.Column(2.f);
		ht.GrowW().Parent(h);
		const ecs::Entity htt = ht.Spawn();
		kit.Label(s->ItemName(r->result), Text::H3).GrowW().TextWrap().Parent(htt).Spawn();
		kit.Muted(String::Format("Produit : %d", r->resultCount), Text::SMALL).Parent(htt).Spawn();
		kit.Label("Ingrédients", Text::SMALL).Bold().Parent(m_detail).Spawn();
		Inventory *inv = s->Inventory();
		for (const Ingredient &ing : RecipeNeeds(*r)) {
			const int have = inv ? inv->CountMatching(game.Content(), ing) : 0;
			WidgetBuilder row = kit.Row(8.f);
			row.GrowW().Parent(m_detail);
			const ecs::Entity e = row.Spawn();
			if (!ing.IsTag())
				ItemIcon(shell, ItemStack{ing.item, 1, 0, nullptr}, 28.f, false).Parent(e).Spawn();
			else
				kit.Icon(MaterialIcons::CATEGORY, 22.f).Parent(e).Spawn();
			kit.Label(ing.IsTag() ? String("Tout « ") + ing.item.Substring(1) + String(" »") : s->ItemName(ing.item), Text::SMALL)
				.GrowW()
				.Parent(e)
				.Spawn();
			kit.Label(String::Format("%d / %d", have, ing.count), Text::SMALL).Bold().TextColor(have >= ing.count ? p.success : p.danger).Parent(e).Spawn();
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
		buttons.GrowW().Margin(math::Sides(0.f, 8.f, 0.f, 0.f)).Parent(m_detail);
		const ecs::Entity bt = buttons.Spawn();
		for (const auto &[label, times] : std::vector<std::pair<const char *, int>>{{"Fabriquer", 1}, {"× 5", 5}, {"Tout", 999}}) {
			WidgetBuilder btn = times == 1 ? kit.PrimaryButton(label, [this, id = r->id, times] { Do(id, times); })
										   : kit.Button(label, [this, id = r->id, times] { Do(id, times); });
			if (!blocker.IsEmpty())
				btn.Disabled();
			btn.Parent(bt).Spawn();
		}
		if (!blocker.IsEmpty())
			kit.Muted(blocker, Text::CAPTION).TextColor(p.danger).Parent(m_detail).Spawn();
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
	ecs::Entity m_list{}, m_detail{};
};

// ── Discussion ───────────────────────────────────────────────────────────────

class Chat final : public Screen {
public:
	Chat(Shell &shell, String prefix) : Screen(shell), m_prefix(std::move(prefix)) {}
	String Id() const override { return "chat"; }
	bool Overlay() const override { return true; }

	void Build(ecs::Entity root) override {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		const float sc = kit.Scale();
		if (auto bg = kit.Ui().World().GetComponent<ui::UiStyle>(root); bg.IsSome()) {
			ui::UiStyle st = *bg.Unwrap();
			st.SetBg(WithAlpha(p.bg0, 0.f));
			ui::SetInlineStyle(kit.Ui().World(), root, std::move(st));
		}
		WidgetBuilder panel = kit.Column(6.f);
		panel.Absolute().Anchor(ui::Anchor::BottomLeft).Offset(14.f, -14.f).W(Dimension::Px(600.f * sc)).HAuto();
		panel.Pad(math::Sides(10.f, 10.f)).Radius(10.f).Bg(WithAlpha(p.bg0, std::max(0.35f, shell.Prefs().chatOpacity))).Parent(root);
		const ecs::Entity pn = panel.Spawn();
		WidgetBuilder head = kit.Row(6.f);
		head.GrowW().Parent(pn);
		const ecs::Entity h = head.Spawn();
		kit.Icon(MaterialIcons::CHAT, 18.f, Some(p.primary)).Parent(h).Spawn();
		int i = 0;
		for (const char *label : {"Tout", "Joueurs", "Système", "Événements"}) {
			const bool on = i == m_filter;
			WidgetBuilder chip = kit.GhostButton(label, [this, i] {
				m_filter = i;
				Refresh();
			});
			chip.H(Dimension::Px(26.f));
			if (on)
				chip.TextColor(p.primary);
			chip.Parent(h).Spawn();
			++i;
		}
		kit.Spacer().Parent(h).Spawn();
		kit.IconButton(m_collapsed ? MaterialIcons::ARROW_UPWARD : MaterialIcons::ARROW_DOWNWARD, m_collapsed ? "Agrandir" : "Réduire", [this] {
			m_collapsed = !m_collapsed;
			shell.Rebuild();
		}).Parent(h).Spawn();
		kit.IconButton(MaterialIcons::CLOSE, "Masquer (Échap)", [this] { shell.Pop(); }).Parent(h).Spawn();
		WidgetBuilder log = kit.Scroll(2.f);
		log.GrowW().H(Dimension::Px((m_collapsed ? 80.f : 300.f) * sc)).Parent(pn);
		m_log = log.Spawn();
		m_suggest = kit.Muted(String(), Text::CAPTION).TextColor(p.info).Parent(pn).Spawn();
		WidgetBuilder input = kit.Input("Message, ou / pour une commande (Tab : compléter, ↑ ↓ : historique)");
		input.MaxLen(256).OnSubmit([this](const String &text) {
			if (Session *s = Live(shell))
				s->Submit(text);
			shell.Pop();
		}).OnTextChange([this](const String &t) { Suggest(t); });
		m_input = input.Parent(pn).Spawn();
		kit.SetInputText(m_input, m_prefix);
		if (auto in = kit.Ui().World().GetComponent<ui::UiInput>(m_input); in.IsSome())
			in.Unwrap()->focused = true;
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
			row.GrowW().Parent(m_log);
			const ecs::Entity r = row.Spawn();
			if (shell.Prefs().chatTimestamps) {
				const std::time_t t = std::time_t(l.time);
				char buf[16];
				std::strftime(buf, sizeof(buf), "%H:%M", std::localtime(&t));
				kit.Muted(buf, Text::CAPTION).Parent(r).Spawn();
			}
			const sdl3::FColor c = l.channel == "event"	  ? p.accent
								   : l.channel == "error" ? p.danger
								   : l.channel == "system" ? p.info
								   : l.channel == "command" ? p.textDim
															: p.text;
			if (!l.author.IsEmpty())
				kit.Label(String("<") + l.author + String(">"), Text::SMALL).Bold().TextColor(p.primary).Parent(r).Spawn();
			kit.Label(l.text, Text::SMALL).TextColor(c).GrowW().TextWrap().Parent(r).Spawn();
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
	ecs::Entity m_log{}, m_input{}, m_suggest{};
	size_t m_seen = 0;
	int m_filter = 0, m_history = 0;
	bool m_collapsed = false;
};

// ── Terminal de commandes ────────────────────────────────────────────────────

class Console final : public Screen {
public:
	Console(Shell &shell, String prefix) : Screen(shell), m_prefix(std::move(prefix)) {}
	String Id() const override { return "console"; }
	bool Overlay() const override { return true; }

	void Build(ecs::Entity root) override {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		const float sc = kit.Scale();
		WidgetBuilder panel = kit.Row(12.f);
		panel.Absolute().Anchor(ui::Anchor::Top).Offset(0.f, 16.f).W(Dimension::Px(1100.f * sc)).H(Dimension::Px(470.f * sc));
		panel.Pad(math::Sides(14.f, 12.f)).Radius(10.f).Bg(WithAlpha(Rgb(6, 8, 12), 0.95f)).BorderColor(p.lineStrong, 1.f).Parent(root);
		panel.Align(ui::CrossAlign::Stretch);
		const ecs::Entity pn = panel.Spawn();
		WidgetBuilder main = kit.Column(6.f);
		main.GrowW().Parent(pn);
		const ecs::Entity m = main.Spawn();
		WidgetBuilder head = kit.Row(8.f);
		head.GrowW().Parent(m);
		const ecs::Entity h = head.Spawn();
		kit.Icon(MaterialIcons::KEYBOARD, 18.f, Some(p.primary)).Parent(h).Spawn();
		kit.Label("Terminal", Text::BODY).Bold().Parent(h).Spawn();
		kit.Muted(s->Game().Rule("commands") ? "commandes autorisées" : "commandes de triche désactivées dans ce monde", Text::CAPTION)
			.GrowW()
			.Parent(h)
			.Spawn();
		kit.IconButton(MaterialIcons::CLOSE, "Fermer (Échap)", [this] { shell.Pop(); }).Parent(h).Spawn();
		WidgetBuilder out = kit.Scroll(1.f);
		out.GrowW().GrowH().Bg(WithAlpha(Rgb(0, 0, 0), 0.35f)).Radius(6.f).Pad(math::Sides(8.f, 6.f)).Parent(m);
		m_output = out.Spawn();
		m_preview = kit.Label(String(), Text::SMALL).Parent(m).Spawn();
		WidgetBuilder inputRow = kit.Row(8.f);
		inputRow.GrowW().Parent(m);
		const ecs::Entity ir = inputRow.Spawn();
		kit.PixelTitle("›", 16.f, p.primary).Parent(ir).Spawn();
		WidgetBuilder input = kit.Input("/help — Tab : compléter · ↑ ↓ : historique");
		input.MaxLen(256).OnSubmit([this](const String &text) { Run(text); }).OnTextChange([this](const String &t) { Analyze(t); });
		m_input = input.Parent(ir).Spawn();
		kit.SetInputText(m_input, m_prefix);
		if (auto in = kit.Ui().World().GetComponent<ui::UiInput>(m_input); in.IsSome())
			in.Unwrap()->focused = true;
		m_hint = kit.Muted(String(), Text::CAPTION).Parent(m).Spawn();
		m_suggestions = kit.Row(6.f).Parent(m).Spawn();
		// Commandes récentes.
		WidgetBuilder side = kit.Column(4.f);
		side.W(Dimension::Px(230.f * sc)).Parent(pn);
		m_recent = side.Spawn();
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
		if (auto in = kit.Ui().World().GetComponent<ui::UiInput>(m_input); in.IsSome())
			in.Unwrap()->focused = true;
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
			kit.Muted("Tapez /help pour la liste des commandes.", Text::SMALL).Parent(m_output).Spawn();
		for (size_t i = lines.size() > 80 ? lines.size() - 80 : 0; i < lines.size(); ++i) {
			const LogLine &l = *lines[i];
			WidgetBuilder lb = kit.Label(l.channel == "command" ? String("› ") + l.text : (l.channel == "error" ? String("✖ ") : String("  ")) + l.text,
										 Text::SMALL);
			lb.GrowW().TextWrap().TextColor(l.channel == "command" ? p.secondary : l.channel == "error" ? p.danger : p.text);
			if (l.channel == "command")
				lb.Bold();
			lb.Parent(m_output).Spawn();
		}
		kit.ClearChildren(m_recent);
		kit.Label("Commandes récentes", Text::SMALL).Bold().Parent(m_recent).Spawn();
		std::vector<String> recent;
		for (auto it = s->History().rbegin(); it != s->History().rend() && recent.size() < 12; ++it)
			if (it->StartsWith("/") && std::find(recent.begin(), recent.end(), *it) == recent.end())
				recent.push_back(*it);
		if (recent.empty())
			kit.Muted("Aucune pour l'instant", Text::CAPTION).Parent(m_recent).Spawn();
		for (const String &r : recent)
			kit.GhostButton(r, [this, r] {
				kit.SetInputText(m_input, r);
				Analyze(r);
			}).GrowW().Parent(m_recent).Spawn();
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
			kit.GhostButton(options[i], [this, text, o = options[i]] {
				size_t last = String::NPOS;
				for (size_t k = 0; k < text.GetSize(); ++k)
					if (text.CharAt(k) == ' ')
						last = k;
				const String done = (last == String::NPOS ? String("/") : text.Substring(0, last + 1)) + o + String(" ");
				kit.SetInputText(m_input, done);
				Analyze(done);
			}).Parent(m_suggestions).Spawn();
	}
	String m_prefix;
	ecs::Entity m_output{}, m_input{}, m_preview{}, m_hint{}, m_suggestions{}, m_recent{};
	size_t m_seen = 0;
	int m_history = 0;
};

// ── Informations du monde ────────────────────────────────────────────────────

class WorldInfo final : public Screen {
public:
	explicit WorldInfo(Shell &shell) : Screen(shell) {}
	String Id() const override { return "worldinfo"; }
	bool Overlay() const override { return true; }

	void Build(ecs::Entity root) override {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		Game &game = s->Game();
		const ecs::Entity card = ModalCard(kit, root, 1080.f);
		WidgetBuilder head = kit.Row(10.f);
		head.GrowW().Parent(card);
		const ecs::Entity h = head.Spawn();
		kit.Icon(MaterialIcons::PUBLIC, 28.f, Some(p.primary)).Parent(h).Spawn();
		WidgetBuilder ht = kit.Column(2.f);
		ht.GrowW().Parent(h);
		const ecs::Entity htt = ht.Spawn();
		kit.Label(game.Metadata().name, Text::H2).Parent(htt).Spawn();
		kit.Muted(String::Format("%s · %s · graine %u · générateur %s", GameModeName(GameModeId(game.Mode())).CStr(),
								 DifficultyName(DifficultyId(game.GetDifficulty())).CStr(), game.Metadata().seed, game.Metadata().generator.CStr()),
				  Text::SMALL)
			.Parent(htt)
			.Spawn();
		kit.IconButton(MaterialIcons::CLOSE, "Fermer (Tab)", [this] { shell.Pop(); }).Parent(h).Spawn();
		auto t = game.Registry().GetComponent<Transform>(s->Player());
		const math::FVector3 pos = t.IsSome() ? t.Unwrap()->position : math::FVector3{0.f, 0.f, 0.f};
		const BlockPos b = BlockPos::FromWorld(pos);
		const BiomeDef *biome = game.GetWorld().Generator() ? game.GetWorld().Generator()->BiomeAt(b.x, b.z) : nullptr;
		WidgetBuilder grid = kit.Row(12.f);
		grid.GrowW().Align(ui::CrossAlign::Start).Parent(card);
		const ecs::Entity g = grid.Spawn();
		auto column = [&] {
			WidgetBuilder c = kit.Column(10.f);
			c.GrowW().Parent(g);
			return c.Spawn();
		};
		auto section = [&](ecs::Entity parent, const char *title, MaterialIcons icon) {
			WidgetBuilder c = kit.Card(12.f);
			c.GrowW().Gap(4.f).Parent(parent);
			const ecs::Entity e = c.Spawn();
			WidgetBuilder tr = kit.Row(6.f);
			tr.Parent(e);
			const ecs::Entity tt = tr.Spawn();
			kit.Icon(icon, 16.f, Some(p.primary)).Parent(tt).Spawn();
			kit.Label(title, Text::SMALL).Bold().TextColor(p.primary).Parent(tt).Spawn();
			return e;
		};
		const ecs::Entity c1 = column(), c2 = column(), c3 = column();
		const float day = game.DayFraction();
		const int hours = int(day * 24.f), minutes = int((day * 24.f - float(hours)) * 60.f);
		ecs::Entity e = section(c1, "Temps", MaterialIcons::SCHEDULE);
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
		kit.Button("Enregistrer maintenant", [this] {
			if (Session *s = Live(shell))
				s->Save(true);
		}).Parent(e).Spawn();
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
	Trade(Shell &shell, UiRequest request) : Screen(shell), m_request(std::move(request)) {}
	String Id() const override { return "trade"; }
	bool Overlay() const override { return true; }

	void Build(ecs::Entity root) override {
		Session *s = Live(shell);
		if (!s)
			return;
		const Palette &p = kit.Colors();
		const ecs::Entity card = ModalCard(kit, root, 620.f);
		WidgetBuilder head = kit.Row(12.f);
		head.GrowW().Parent(card);
		const ecs::Entity h = head.Spawn();
		kit.Icon(MaterialIcons::STOREFRONT, 30.f, Some(p.accent)).Parent(h).Spawn();
		WidgetBuilder ht = kit.Column(2.f);
		ht.GrowW().Parent(h);
		const ecs::Entity htt = ht.Spawn();
		kit.Label(json::Str(m_request.data, "title", "Marchand"), Text::H2).Parent(htt).Spawn();
		kit.Muted(json::Str(m_request.data, "profession", "Échanges"), Text::SMALL).Parent(htt).Spawn();
		kit.IconButton(MaterialIcons::CLOSE, "Fermer", [this] { shell.Pop(); }).Parent(h).Spawn();
		Inventory *inv = s->Inventory();
		NodePtr offers = json::At(m_request.data, "offers");
		for (size_t i = 0; offers && i < offers->GetSize(); ++i) {
			NodePtr o = offers->At(i);
			const String giveId = json::Str(json::At(o, "give")->At(0), ""), getId = json::Str(json::At(o, "get")->At(0), "");
			const int giveN = json::Int(json::At(o, "give")->At(1), "", 1), getN = json::Int(json::At(o, "get")->At(1), "", 1);
			const int have = inv ? inv->Count(giveId) : 0;
			WidgetBuilder row = kit.Row(10.f);
			row.GrowW().Pad(math::Sides(10.f, 8.f)).Radius(8.f).Bg(p.bg2).BorderColor(p.line, 1.f).Parent(card);
			const ecs::Entity r = row.Spawn();
			ItemIcon(shell, ItemStack{giveId, giveN, 0, nullptr}, 40.f).Parent(r).Spawn();
			WidgetBuilder gc = kit.Column(1.f);
			gc.W(Dimension::Px(150.f)).Parent(r);
			const ecs::Entity g = gc.Spawn();
			kit.Label(s->ItemName(giveId), Text::SMALL).Parent(g).Spawn();
			kit.Label(String::Format("vous avez %d / %d", have, giveN), Text::CAPTION).TextColor(have >= giveN ? p.success : p.danger).Parent(g).Spawn();
			kit.Icon(MaterialIcons::ARROW_FORWARD, 22.f, Some(p.textMuted)).Parent(r).Spawn();
			ItemIcon(shell, ItemStack{getId, getN, 0, nullptr}, 40.f).Parent(r).Spawn();
			kit.Label(String::Format("%d × %s", getN, s->ItemName(getId).CStr()), Text::SMALL).GrowW().Parent(r).Spawn();
			WidgetBuilder btn = kit.PrimaryButton("Échanger", [this, giveId, giveN, getId, getN] {
				if (Session *s = Live(shell)) {
					Result<bool, String> res = s->Game().Trade(s->Player(), giveId, giveN, getId, getN);
					if (res.IsError())
						shell.Notify(Tone::WARNING, "Échange impossible", res.Error());
				}
				shell.Rebuild();
			});
			if (have < giveN)
				btn.Disabled();
			btn.Parent(r).Spawn();
		}
	}

private:
	UiRequest m_request;
};

class Dialog final : public Screen {
public:
	Dialog(Shell &shell, UiRequest request) : Screen(shell), m_request(std::move(request)) {}
	String Id() const override { return "dialog"; }
	bool Overlay() const override { return true; }

	void Build(ecs::Entity root) override {
		const Palette &p = kit.Colors();
		const ecs::Entity card = ModalCard(kit, root, 520.f);
		WidgetBuilder head = kit.Row(12.f);
		head.Parent(card);
		const ecs::Entity h = head.Spawn();
		kit.Icon(MaterialIcons::CHAT, 26.f, Some(p.primary)).Parent(h).Spawn();
		kit.Label(json::Str(m_request.data, "title", "PNJ"), Text::H2).Parent(h).Spawn();
		WidgetBuilder bubble = kit.Column(4.f);
		bubble.GrowW().Pad(math::Sides(14.f, 12.f)).Radius(10.f).Bg(p.bg2).Parent(card);
		const ecs::Entity b = bubble.Spawn();
		kit.Paragraph(json::Str(m_request.data, "text"), Text::BODY).TextColor(p.text).Parent(b).Spawn();
		WidgetBuilder buttons = kit.Row(10.f);
		buttons.GrowW().Justify(ui::Justify::End).Parent(card);
		const ecs::Entity bt = buttons.Spawn();
		std::vector<String> options = json::StrList(m_request.data, "options");
		if (options.empty())
			options.push_back("D'accord");
		for (size_t i = 0; i < options.size(); ++i) {
			WidgetBuilder btn = i + 1 == options.size() ? kit.PrimaryButton(options[i], [this] { shell.Pop(); })
														 : kit.Button(options[i], [this] { shell.Pop(); });
			btn.Parent(bt).Spawn();
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

} // namespace voxelgame
