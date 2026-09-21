#pragma once
/**
 * ui::chrome — chrome de fenêtre borderless (Phase 9) : barre de titre
 * dessinée par des widgets ui:: (icône/titre/réduire/agrandir/fermer, zone
 * de drag) plutôt que par la décoration système. Deux variantes, cf.
 * Décisions d'architecture du plan (mossy-forging-fog.md) :
 *
 *   - WindowChrome : hit-test AU NIVEAU OS (sdl3::Window::SetHitTest) — pour
 *     une vraie fenêtre SDL3 borderless (examples/ui_aero_*.cpp).
 *   - PanelChrome  : drag/resize gérés à l'ECS (cf. interaction.hpp
 *     UiDraggable), PAS d'OS hit-test — pour un "bureau simulé" (Phase 10)
 *     où plusieurs panneaux flottants vivent DANS une seule fenêtre SDL3.
 */
#include <vector>

#include "factory.hpp"

namespace ui {

// ============================================================================
// WindowChrome — barre de titre OS-level (fenêtre borderless réelle)
// ============================================================================

/// Cache POD reconstruit UNIQUEMENT quand layout.RunIfNeeded() a
/// effectivement tourné depuis le dernier appel (même garde dirty-flag que
/// LayoutSystem lui-même, pas de second mécanisme — cf. Décision
/// d'architecture #2 du plan). Le callback passé à Window::SetHitTest ne
/// touche JAMAIS l'ECS : il ne fait qu'un test point-dans-rect sur CE cache,
/// capturé par pointeur — sûr à appeler depuis n'importe quel thread/
/// contexte où SDL invoque le callback de hit-test.
struct HitTestCache {
	sdl3::FRect titleBar{};            ///< bande draggable (hors boutons)
	std::vector<sdl3::FRect> excluded; ///< boutons de la barre de titre — NORMAL pour rester cliquables
	float resizeBorder = 6.f;    ///< épaisseur de la bande de redimensionnement sur les 4 bords
	float w = 0.f, h = 0.f;      ///< taille de fenêtre courante (coins/bords)
	bool resizable = true;
};

class WindowChrome {
public:
	/// `titleBarEntity` : l'entité racine de la barre de titre (typiquement
	/// un `.Row()` en haut de la fenêtre, cf. UiFactory::TitleBar()) — son
	/// UiComputed.screen après layout définit la bande draggable.
	/// `buttonEntities` : réduire/agrandir/fermer (ou tout autre widget de la
	/// barre) — exclus du drag pour rester cliquables normalement (coins
	/// testés avant bords, bords avant bande draggable — cf. hitTest()).
	void Attach(sdl3::Window &window, ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity titleBarEntity,
				std::vector<ecs::Entity> buttonEntities = {}, bool resizable = true) {
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

	/// Retire le hit-test (fenêtre redevient un widget normal côté OS —
	/// utile pour repasser en fenêtré/plein écran décoré, si jamais).
	void Detach() {
		if (m_window)
			m_window->SetHitTest(nullptr);
		m_window = nullptr;
	}

	/// À appeler une fois par frame (avant/après Ui::Render(), l'ordre
	/// n'importe pas) — ne reconstruit le cache que si layout.RunIfNeeded()
	/// a effectivement tourné depuis le dernier appel.
	void Update() { RebuildIfNeeded(); }

private:
	sdl3::Window *m_window = nullptr;
	ecs::ArchetypeRegistry *m_world = nullptr;
	LayoutSystem *m_layout = nullptr;
	ecs::Entity m_titleBarEntity{};
	std::vector<ecs::Entity> m_buttonEntities;
	HitTestCache cache;
	uint64_t lastLayoutPass = 0;

	void RebuildIfNeeded() {
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

	[[nodiscard]] SDL_HitTestResult HitTest(const SDL_Point &p) const {
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
};

// ============================================================================
// Barre de titre — icône + titre + réduire/agrandir/fermer
// ============================================================================

/// Entités d'une barre de titre construite par UiFactory::TitleBar() — à
/// passer telles quelles à WindowChrome::Attach() (buttonEntities =
/// {minimizeBtn, maximizeBtn, closeBtn}).
struct TitleBarWidgets {
	ecs::Entity root{};
	ecs::Entity minimizeBtn{};
	ecs::Entity maximizeBtn{};
	ecs::Entity closeBtn{};
};

/// Construit une barre de titre standard (titre + réduire/agrandir/fermer),
/// prête pour WindowChrome::Attach(). Réduire/agrandir appellent directement
/// `window.Minimize()`/`maximize()`/`restore()` (bascule agrandi/restauré
/// suivie via un état local capturé par les callbacks) ; fermer appelle
/// `onClose` si fourni, sinon `window.Hide()`. Glyphes texte simples (—/□/×)
/// plutôt que MaterialIcons : rendus par la police déjà chargée pour tout le
/// reste du texte, sans exiger que l'appelant ait enregistré une police
/// d'icônes avant de construire la barre (cf. UiFactory::Icon<E>() si une
/// vraie icône est préférée — remplacer le bouton après coup reste possible,
/// ce sont des UiButton ordinaires).
[[nodiscard]] inline TitleBarWidgets TitleBar(UiFactory &f, sdl3::Window &window, String title,
											  std::function<void()> onClose = nullptr) {
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

// ============================================================================
// PanelChrome — panneau flottant "bureau simulé" (Phase 9/10) : drag/resize
// gérés à l'ECS (cf. interaction.hpp UiDraggable), PAS d'OS hit-test.
// ============================================================================

class PanelChrome {
public:
	/// `panelEntity` doit être positionné en `.Absolute()` avec une taille
	/// PIXEL explicite (pas Auto/Grow, sans quoi le redimensionnement
	/// n'aurait aucun effet visible) ; `titleBarEntity` est la zone qui
	/// déplace le panneau au glisser (typiquement son en-tête) ;
	/// `resizeGripEntity` (optionnel, ex: un petit widget en coin
	/// bas-droit) redimensionne `panelEntity` au glisser. Attache
	/// directement les composants UiDraggable nécessaires — pas de
	/// WidgetBuilder ici, les entités existent déjà.
	static void Attach(ecs::ArchetypeRegistry &world, ecs::Entity panelEntity, ecs::Entity titleBarEntity,
					   ecs::Entity resizeGripEntity = ecs::Entity{}, float minWidth = 160.f, float minHeight = 100.f) {
		world.AddComponent(titleBarEntity, UiDraggable{panelEntity, false, minWidth, minHeight});
		if (resizeGripEntity.Valid())
			world.AddComponent(resizeGripEntity, UiDraggable{panelEntity, true, minWidth, minHeight});
	}
};

} // namespace ui
