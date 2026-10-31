#pragma once
/**
 * ui::Context — état partagé d'une interface, rangé dans les RESSOURCES du
 * monde ECS (`world.resources`) : tous les systèmes (`ui_input`, `ui_style`,
 * `ui_layout`, `ui_render`) et toutes les poignées `ui::Widget` le
 * retrouvent depuis le seul monde.
 *
 * Il contient ce qui n'appartient à aucune entité : feuilles de style
 * globales, polices, textures, peintre, état d'interaction (survol, focus,
 * glisser en cours, popups ouverts), drapeaux de recalcul.
 */
#include <memory>
#include <unordered_map>
#include <vector>

#include <sdl3/render.hpp>

#include "components.hpp"
#include "painter.hpp"

namespace render3d {
class Canvas;
} // namespace render3d

namespace ui {

/// Temps passé dans chaque étape de la dernière image, en millisecondes.
struct FrameTimings {
	double styleMs = 0.0;
	double layoutMs = 0.0;
	double viewport3dMs = 0.0;
	double shaderEffectMs = 0.0;
	double drawMs = 0.0;
	[[nodiscard]] double TotalMs() const noexcept { return styleMs + layoutMs + viewport3dMs + shaderEffectMs + drawMs; }
};

/// Entrée de l'ordre de dessin (hit-test du premier plan).
struct PaintEntry {
	ecs::Entity entity;
	sdl3::FRect box;
	sdl3::FRect clip;
	bool blocksBelow = false; ///< voile de modale : rien de dessiné avant n'est atteignable
	size_t subtreeEnd = 0;	  ///< indice qui suit le dernier descendant dessiné dans le flux
};

/// État d'interaction (tenu par l'InputSystem, lu par le rendu et les Widgets).
struct Interaction {
	sdl3::FPoint pointer{-1.f, -1.f};
	bool pointerInside = false;
	ecs::Entity hovered{};				///< widget le plus en avant sous le pointeur
	std::vector<ecs::Entity> hoverChain; ///< hovered + ses ancêtres (:hover)
	bool overUi = false;				 ///< un widget visible ou interactif est sous le pointeur
	ecs::Entity pressed{}; ///< widget enfoncé (bouton gauche, :active)
	ecs::Entity focus{};
	bool focusVisible = false;

	// Clics multiples / appui long : reconnaissance du module sdl3 (input.hpp).
	sdl3::ButtonGestureTracker gestures;
	/// Cible de l'appui en cours par bouton (source, code) : un clic n'est
	/// rendu que si le relâchement a lieu sur elle ou un descendant.
	struct PressTarget {
		sdl3::ButtonSource source;
		int code;
		ecs::Entity target;
	};
	std::vector<PressTarget> pressTargets;
	ecs::Entity lastClickTarget{};
	std::vector<uint32_t> holdFired; ///< liaisons Hold déjà déclenchées pour l'appui en cours

	// Glisser en cours (un seul à la fois)
	enum class Drag : uint8_t { None, Range, Range2D, Scroll, Text, Resize, Move, Reorder, Payload, Column, Pan } drag = Drag::None;
	ecs::Entity dragEntity{};
	sdl3::FPoint dragStart{};
	sdl3::FPoint dragStartValue{};
	float dragStartScalar = 0.f;
	bool dragVertical = false;
	bool dragActive = false; ///< seuil de déplacement franchi
	ecs::Entity dropHover{};
	ecs::Entity reorderTarget{};

	// Infobulle
	ecs::Entity tooltipEntity{};
	float hoverTime = 0.f;
	bool tooltipVisible = false;
	String tooltipText;
	sdl3::FPoint tooltipPos{};

	// Menus
	ecs::Entity menuHover{};
	float menuHoverTime = 0.f;

	// Popups ouverts, du plus ancien au plus récent
	std::vector<ecs::Entity> popups;

	Cursor cursor = Cursor::DEFAULT;
	uint64_t timeMs = 0; ///< horloge interne (somme des dt)
};

class Context;

/// Point d'extension des systèmes pour les widgets autonomes (plot.hpp,
/// nodegraph.hpp, viewport3d.hpp…), enregistré dans le contexte.
struct Extension {
	String name;
	/// Dessin du contenu d'une entité (après le fond, avant les enfants).
	std::function<void(ecs::ArchetypeRegistry &, Context &, ecs::Entity, const Rect &, const ComputedStyle &)> draw;
	/// Dessin au-dessus des enfants (connexions d'un graphe, sélection…).
	std::function<void(ecs::ArchetypeRegistry &, Context &, ecs::Entity, const Rect &, const ComputedStyle &)> drawOver;
	/// Évènement, avant le traitement standard ; vrai = consommé.
	std::function<bool(ecs::ArchetypeRegistry &, Context &, const sdl3::Event &)> event;
	/// Chaque image, avant style et mise en page.
	std::function<void(ecs::ArchetypeRegistry &, Context &, float)> tick;
	/// Remplace le dessin d'un sous-arbre entier (vrai : déjà dessiné, la
	/// descendance n'est pas parcourue) — effets shader.
	std::function<bool(ecs::ArchetypeRegistry &, Context &, ecs::Entity, const Rect &)> drawSubtree;
	/// Avant le dessin 2D (rendus hors écran : vues 3D, effets).
	std::function<void(ecs::ArchetypeRegistry &, Context &)> prepare;
	/// Taille intrinsèque d'une entité (NONE : non concernée).
	std::function<Option<sdl3::FPoint>(ecs::ArchetypeRegistry &, Context &, ecs::Entity, const ComputedStyle &)> measure;
};


class Context {
public:
	Context();
	Context(const Context &) = delete;
	Context &operator=(const Context &) = delete;
	~Context();

	// ── Sorties ─────────────────────────────────────────────────────────────
	sdl3::Window *window = nullptr;
	sdl3::Renderer *renderer = nullptr;
	render3d::Canvas *canvas3d = nullptr;
	sdl3::FPoint viewport{800.f, 600.f};
	/// Unités logiques → pixels du renderer (HiDPI, cf. Ui::SetContentScale) :
	/// 2 = tout est dessiné deux fois plus grand, pointeur converti en retour.
	float contentScale = 1.f;
	/// Taille logique de la fenêtre : sa taille en points à l'échelle 1, la
	/// sortie du renderer divisée par l'échelle sinon ; `viewport` sans fenêtre.
	[[nodiscard]] sdl3::FPoint WindowViewport() const;

	Fonts fonts;
	Textures textures;
	Painter painter;

	// ── Arbre ───────────────────────────────────────────────────────────────
	ecs::Entity root{};

	// ── Styles ──────────────────────────────────────────────────────────────
	/// Feuilles du module (structure des widgets, puis thème) : priorité la
	/// plus basse, au-dessus des seules valeurs par défaut C++.
	std::vector<StyleSheetPtr> moduleSheets;
	/// Feuilles globales de l'application (portée : la racine).
	std::vector<StyleSheetPtr> sheets;
	/// Tout restyler à la prochaine passe.
	bool styleDirtyAll = true;
	std::vector<ecs::Entity> styleDirty;

	// ── Mise en page ────────────────────────────────────────────────────────
	bool layoutDirty = true;
	uint64_t layoutPass = 0;
	std::vector<PaintEntry> paintOrder;
	float rootFontSize = 14.f;

	// ── Saisie ──────────────────────────────────────────────────────────────
	Interaction input;
	float tooltipDelay = 0.6f;
	float doubleClickTime = 0.4f;
	float holdTime = 0.5f;
	float wheelSpeed = 40.f;
	float submenuDelay = 0.2f;
	float dragThreshold = 4.f;
	/// Vrai : la touche continue vers l'application malgré le focus d'un champ.
	std::function<bool(const sdl3::Event &, ecs::Entity focus)> keyPassThrough;

	FrameTimings timings;
	/// Vrai pendant la capture d'un sous-arbre (effet shader) : drawSubtree
	/// n'est pas rappelé sur lui-même.
	bool capturing = false;

	/// Évènements en attente du système `ui_input` (cf. ui::PushEvent).
	std::vector<sdl3::Event> queued;
	/// Extensions enregistrées (graphiques, node-graph, vues 3D…).
	std::vector<Extension> extensions;
	/// Feuilles déjà chargées, par chemin (imports de widgets).
	std::unordered_map<String, StyleSheetPtr> sheetCache;
	/// Curseurs système créés (un par forme utilisée).
	std::unordered_map<int, sdl3::Cursor> cursors;
	Cursor appliedCursor = Cursor::DEFAULT;

	/// Charge une feuille (mise en cache) et ses polices.
	Result<StyleSheetPtr, String> LoadSheet(const String &path);

	// ── Aides ───────────────────────────────────────────────────────────────
	void MarkStyleDirty(ecs::Entity e) { styleDirty.push_back(e); }
	void MarkAllDirty() noexcept {
		styleDirtyAll = true;
		layoutDirty = true;
	}
	void MarkLayoutDirty() noexcept { layoutDirty = true; }
	/// Charge (si besoin) les polices @font-face d'une feuille.
	void RegisterFonts(const StyleSheet &sheet);
};

/// Poignée stockée dans les ressources du monde (les ressources exigent un
/// type copiable).
struct ContextHandle {
	std::shared_ptr<Context> context;
};

/// Contexte du monde, nullptr si aucune interface n'y est installée.
[[nodiscard]] Context *ContextOf(ecs::ArchetypeRegistry &world);
[[nodiscard]] const Context *ContextOf(const ecs::ArchetypeRegistry &world);

/// Signale un changement de style / de mise en page (sans contexte : sans effet).
void MarkStyleDirty(ecs::ArchetypeRegistry &world, ecs::Entity e);
void MarkLayoutDirty(ecs::ArchetypeRegistry &world);

/// Règle par défaut de transmission des touches quand un champ a le focus :
/// F1–F24 et les raccourcis Ctrl/Alt/Cmd qui ne sont pas de l'édition.
[[nodiscard]] bool DefaultKeyPassThrough(const sdl3::Event &ev) noexcept;

} // namespace ui
