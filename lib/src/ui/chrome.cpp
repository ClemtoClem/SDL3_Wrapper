// Définitions de ui/chrome.hpp
#include "ui/chrome.hpp"

namespace ui {

// ── WindowChrome ─────────────────────────────────────────────────────────────

void WindowChrome::Attach(sdl3::Window &window, ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity titleBarEntity,
		std::vector<ecs::Entity> buttonEntities, bool resizable) {
	m_window = &window;
	m_world = &world;
	m_layout = &layout;
	m_titleBarEntity = titleBarEntity;
	m_buttonEntities = std::move(buttonEntities);
	cache.resizable = resizable;
	lastLayoutPass = ~uint64_t(0); // force la 1re reconstruction
	RebuildIfNeeded();
	window.SetHitTest([this](const SDL_Point &p) { return HitTest(p); });
}

void WindowChrome::AddResizeGrip(ecs::Entity grip) {
	m_gripEntities.push_back(grip);
	lastLayoutPass = ~uint64_t(0);
	RebuildIfNeeded();
}

void WindowChrome::Attach(sdl3::Window &window, ecs::ArchetypeRegistry &world, LayoutSystem &layout,
						  const TitleBarWidgets &titleBar, const StatusBarWidgets *statusBar, bool resizable) {
	Attach(window, world, layout, titleBar.root, titleBar.Buttons(), resizable);
	if (statusBar && statusBar->grip.Valid())
		AddResizeGrip(statusBar->grip);
}

void WindowChrome::Detach() {
	if (m_window)
		m_window->SetHitTest(nullptr);
	m_window = nullptr;
}

void WindowChrome::RebuildIfNeeded() {
	if (!m_world || !m_layout)
		return;
	uint64_t pass = m_layout->PassCount();
	if (pass == lastLayoutPass)
		return;
	lastLayoutPass = pass;
	if (auto c = m_world->GetComponent<UiComputed>(m_titleBarEntity); c.IsSome())
		cache.titleBar = c.Unwrap()->screen;
	cache.excluded.clear();
	for (ecs::Entity b : m_buttonEntities)
		if (auto c = m_world->GetComponent<UiComputed>(b); c.IsSome())
			cache.excluded.push_back(c.Unwrap()->screen);
	cache.grips.clear();
	for (ecs::Entity g : m_gripEntities)
		if (auto c = m_world->GetComponent<UiComputed>(g); c.IsSome())
			cache.grips.push_back(c.Unwrap()->screen);
	if (m_window) {
		auto sz = m_window->GetSize();
		cache.w = float(sz.x);
		cache.h = float(sz.y);
	}
}

SDL_HitTestResult WindowChrome::HitTest(const SDL_Point &p) const {
	sdl3::FPoint fp{float(p.x), float(p.y)};
	// Boutons de la barre de titre : cliquables normalement (testés en
	// premier — ils chevauchent typiquement la bande draggable/les bords).
	for (const sdl3::FRect &r : cache.excluded)
		if (r.Contains(fp))
			return SDL_HITTEST_NORMAL;
	if (cache.resizable)
		for (const sdl3::FRect &r : cache.grips)
			if (r.Contains(fp))
				return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
	// Coins AVANT bords (une zone de coin est aussi dans sa bande de bord).
	if (cache.resizable) {
		float rb = cache.resizeBorder;
		bool left = fp.x < rb, right = fp.x > cache.w - rb;
		bool top = fp.y < rb, bottom = fp.y > cache.h - rb;
		if (top && left)
			return SDL_HITTEST_RESIZE_TOPLEFT;
		if (top && right)
			return SDL_HITTEST_RESIZE_TOPRIGHT;
		if (bottom && left)
			return SDL_HITTEST_RESIZE_BOTTOMLEFT;
		if (bottom && right)
			return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
		if (top)
			return SDL_HITTEST_RESIZE_TOP;
		if (bottom)
			return SDL_HITTEST_RESIZE_BOTTOM;
		if (left)
			return SDL_HITTEST_RESIZE_LEFT;
		if (right)
			return SDL_HITTEST_RESIZE_RIGHT;
	}
	if (cache.titleBar.Contains(fp))
		return SDL_HITTEST_DRAGGABLE;
	return SDL_HITTEST_NORMAL;
}

// ── Bouton à icône, police d'icônes ──────────────────────────────────────────

ecs::Entity SpawnIconButton(UiFactory &f, ecs::Entity parent, MaterialIcons glyph, const String &fallback, bool icons,
							std::function<void()> onClick, const String &tooltip, float width, float height,
							ecs::Entity *iconOut) {
	WidgetBuilder button = f.Button(icons ? String() : fallback);
	button.Size(width, height).Pad(0.f).Align(CrossAlign::Center).Justify(Justify::Center);
	if (!tooltip.IsEmpty())
		button.Tooltip(tooltip);
	if (onClick)
		button.OnClick(std::move(onClick));
	if (parent.Valid())
		button.Parent(parent);
	ecs::Entity b = button.Spawn();
	if (iconOut)
		*iconOut = ecs::Entity{};
	if (icons) {
		// Le glyphe ne doit pas intercepter le pointeur : clic et survol vont
		// au bouton (hit-test « premier plan », cf. HitTestIndex).
		WidgetBuilder icon = f.Icon(glyph, sdl3::Min(width, height) * 0.65f);
		icon.PointerThrough().Parent(b);
		ecs::Entity e = icon.Spawn();
		if (iconOut)
			*iconOut = e;
	}
	return b;
}

Option<sdl3::Font> OpenMaterialIconFont(float size) {
	std::vector<String> dirs = {"assets/fonts/"};
	if (const char *base = SDL_GetBasePath()) {
		dirs.push_back(String(base) + "assets/fonts/");
		dirs.push_back(String(base) + "../../assets/fonts/");
	}
	for (const String &dir : dirs)
		if (auto font = sdl3::Font::Open(dir + MATERIALICONS_FILENAME, size))
			return Some(std::move(font.Value()));
	return NONE;
}

// ── Barre de titre ───────────────────────────────────────────────────────────

std::vector<ecs::Entity> TitleBarWidgets::Buttons() const {
	std::vector<ecs::Entity> out;
	for (ecs::Entity e : {minimizeBtn, maximizeBtn, closeBtn})
		if (e.Valid())
			out.push_back(e);
	return out;
}

void TitleBarWidgets::Update(ecs::ArchetypeRegistry &world, const sdl3::Window &window) const {
	if (!maximizeIcon.Valid())
		return;
	auto icon = world.GetComponent<UiIcon>(maximizeIcon);
	if (icon.IsNone())
		return;
	String glyph = Glyphs::ToUtf8(window.IsMaximized() ? MaterialIcons::FILTER_NONE : MaterialIcons::CROP_SQUARE);
	if (icon.Unwrap()->glyph != glyph)
		icon.Unwrap()->glyph = glyph;
}

void TitleBarWidgets::SetTitle(ecs::ArchetypeRegistry &world, const String &text) const {
	if (auto label = world.GetComponent<UiLabel>(title); label.IsSome())
		label.Unwrap()->text = text;
}

TitleBarWidgets TitleBar(UiFactory &f, sdl3::Window &window, TitleBarOptions options, ecs::Entity parent) {
	TitleBarWidgets tb;
	sdl3::FColor bg = options.background;
	if (bg.a <= 0.f) {
		bg = f.theme.panelBg;
		bg.a = 1.f;
	}

	WidgetBuilder bar = f.Row();
	// Hauteur fixe, jamais Auto : une barre de titre ne change pas de taille
	// selon son contenu. Sans parent, rpct résout contre la fenêtre.
	bar.Gap(8.f)
		.Pad(math::Sides{10.f, 3.f, 4.f, 3.f})
		.Align(CrossAlign::Center)
		.H(Dimension::Px(options.height));
	if (options.fillBackground)
		bar.Bg(bg);
	if (parent.Valid())
		bar.GrowW().Parent(parent);
	else
		bar.W(Dimension::Rpct(100.f));
	tb.root = bar.Spawn();

	if (options.appIcon && options.icons) {
		WidgetBuilder icon = f.Icon(*options.appIcon, options.height * 0.55f);
		icon.TextColor(f.theme.accent).Parent(tb.root);
		tb.appIcon = icon.Spawn();
	}
	WidgetBuilder title = f.Label(options.title);
	title.FontSize(options.titleSize).Parent(tb.root);
	if (options.boldTitle)
		title.Bold();
	tb.title = title.Spawn();
	if (options.moveHandle) {
		WidgetBuilder move = options.icons ? f.Icon(MaterialIcons::OPEN_WITH, options.height * 0.5f)
										   : f.Label(String("\xe2\x9c\xa5")); // U+2725 ✥
		move.TextColor(f.theme.muted).Tooltip("Glisser la barre de titre pour déplacer la fenêtre").Parent(tb.root);
		tb.moveHandle = move.Spawn();
	}
	// Espace vide extensible : il pousse les boutons à droite (et reste
	// draggable, comme toute la barre hors boutons).
	f.Label(String()).GrowW().Parent(tb.root).Spawn();

	float bw = options.height * 1.1f;
	float bh = options.height - 8.f;
	if (options.minimizeButton)
		tb.minimizeBtn = SpawnIconButton(f, tb.root, MaterialIcons::MINIMIZE, String("\xe2\x80\x94"), options.icons,
										 [&window] { window.Minimize(); }, "Réduire", bw, bh);
	if (options.maximizeButton)
		tb.maximizeBtn = SpawnIconButton(
			f, tb.root, MaterialIcons::CROP_SQUARE, String("\xe2\x96\xa1"), options.icons,
			[&window] {
				if (window.IsMaximized())
					window.Restore();
				else
					window.Maximize();
			},
			"Agrandir / restaurer", bw, bh, &tb.maximizeIcon);
	tb.closeBtn = SpawnIconButton(
		f, tb.root, MaterialIcons::CLOSE, String("\xc3\x97"), options.icons,
		[&window, onClose = std::move(options.onClose)] {
			if (onClose)
				onClose();
			else
				window.Hide();
		},
		"Fermer", bw, bh);
	return tb;
}

TitleBarWidgets TitleBar(UiFactory &f, sdl3::Window &window, String title, std::function<void()> onClose) {
	TitleBarOptions options;
	options.title = std::move(title);
	options.moveHandle = false;
	options.icons = false;
	options.onClose = std::move(onClose);
	options.height = 32.f;
	options.titleSize = f.theme.fontSize;
	options.boldTitle = false;
	options.fillBackground = false; // le verre Aero reste visible
	return TitleBar(f, window, std::move(options));
}

// ── Barre d'état ─────────────────────────────────────────────────────────────

void StatusBarWidgets::SetText(ecs::ArchetypeRegistry &world, const String &text, sdl3::FColor color) const {
	if (auto l = world.GetComponent<UiLabel>(label); l.IsSome())
		l.Unwrap()->text = text;
	if (color.a > 0.f)
		if (auto style = world.GetOrAddComponent<UiStyle>(label); style.IsSome())
			style.Unwrap()->SetTextColor(color);
}

StatusBarWidgets StatusBar(UiFactory &f, StatusBarOptions options, ecs::Entity parent) {
	StatusBarWidgets sb;
	WidgetBuilder row = f.Row();
	row.Gap(6.f).Pad(math::Sides{10.f, 0.f, 2.f, 0.f}).Align(CrossAlign::Center).H(Dimension::Px(options.height));
	if (parent.Valid())
		row.GrowW().Parent(parent);
	else
		row.W(Dimension::Rpct(100.f));
	sb.root = row.Spawn();
	WidgetBuilder label = f.Label(options.text);
	label.TextColor(f.theme.muted).GrowW().TextEllipsis().Parent(sb.root);
	sb.label = label.Spawn();
	if (options.resizeGrip) {
		WidgetBuilder grip = options.icons ? f.Icon(MaterialIcons::SOUTH_EAST, options.height * 0.8f)
										   : f.Label(String("\xe2\x87\xb2")); // U+21F2 ⇲
		grip.TextColor(f.theme.muted).Tooltip("Glisser pour redimensionner la fenêtre").Parent(sb.root);
		sb.grip = grip.Spawn();
	}
	return sb;
}

// ── PanelChrome ──────────────────────────────────────────────────────────────

void PanelChrome::Attach(ecs::ArchetypeRegistry &world, ecs::Entity panelEntity, ecs::Entity titleBarEntity,
		ecs::Entity resizeGripEntity, float minWidth, float minHeight) {
	world.AddComponent(titleBarEntity, UiDraggable{panelEntity, false, minWidth, minHeight});
	if (resizeGripEntity.Valid())
		world.AddComponent(resizeGripEntity, UiDraggable{panelEntity, true, minWidth, minHeight});
}

} // namespace ui
