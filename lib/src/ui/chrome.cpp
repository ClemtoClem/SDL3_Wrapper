// Définitions de ui/chrome.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

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

TitleBarWidgets TitleBar(UiFactory &f, sdl3::Window &window, String title, std::function<void()> onClose) {
	TitleBarWidgets tb;

	WidgetBuilder bar = f.Row();
	// Racine (pas de .parent() — cf. plus bas) : rpct résout contre la
	// fenêtre, exactement ce qu'il faut pour une barre de titre pleine
	// largeur. Hauteur fixe, jamais Auto (une barre de titre ne doit pas
	// changer de taille selon son contenu).
	bar.Gap(6.f)
		.Pad(math::Sides{10.f, 4.f, 6.f, 4.f})
		.Align(CrossAlign::Center)
		.W(Dimension::Rpct(100.f))
		.H(Dimension::Px(32.f));

	WidgetBuilder lbl = f.Label(std::move(title));
	lbl.GrowW();

	WidgetBuilder minB = f.Button(String("\xe2\x80\x94")); // U+2014 EM DASH
	minB.Size(28.f, 22.f).OnClick([&window] { window.Minimize(); });

	// État "agrandie" suivi par le bouton lui-même (capturé par valeur dans
	// la closure — un simple bool partagé n'a pas besoin de vivre sur l'ECS).
	auto maximized = std::make_shared<bool>(false);
	WidgetBuilder maxB = f.Button(String("\xe2\x96\xa1")); // U+25A1 WHITE SQUARE
	maxB.Size(28.f, 22.f).OnClick([&window, maximized] {
		if (*maximized)
			window.Restore();
		else
			window.Maximize();
		*maximized = !*maximized;
	});

	WidgetBuilder closeB = f.Button(String("\xc3\x97")); // U+00D7 MULTIPLICATION SIGN
	closeB.Size(28.f, 22.f).OnClick([&window, onClose] {
		if (onClose)
			onClose();
		else
			window.Hide();
	});

	bar.Children(std::move(lbl), std::move(minB), std::move(maxB), std::move(closeB));
	tb.root = bar.Spawn();

	if (auto ch = f.World().GetComponent<UiChildren>(tb.root); ch.IsSome() && ch.Unwrap()->list.size() == 4) {
		tb.minimizeBtn = ch.Unwrap()->list[1];
		tb.maximizeBtn = ch.Unwrap()->list[2];
		tb.closeBtn = ch.Unwrap()->list[3];
	}
	return tb;
}

// ── PanelChrome ──────────────────────────────────────────────────────────────

void PanelChrome::Attach(ecs::ArchetypeRegistry &world, ecs::Entity panelEntity, ecs::Entity titleBarEntity,
		ecs::Entity resizeGripEntity, float minWidth, float minHeight) {
	world.AddComponent(titleBarEntity, UiDraggable{panelEntity, false, minWidth, minHeight});
	if (resizeGripEntity.Valid())
		world.AddComponent(resizeGripEntity, UiDraggable{panelEntity, true, minWidth, minHeight});
}

} // namespace ui
