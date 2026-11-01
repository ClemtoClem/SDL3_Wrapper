#pragma once
/**
 * Voxelia Studio — l'éditeur, dans SA fenêtre SDL (ui::WindowManager), sur la
 * MÊME partie que la fenêtre de jeu (Shell / Session / voxel::Game) : rien
 * n'est copié, l'outliner et l'inspecteur lisent le registre ECS vivant, la
 * vue 3D affiche la scène du WorldRenderer de la partie.
 *
 * Même architecture d'interface que la sandbox (module partagé `editor`) :
 *
 *   cadre    : ui::WindowFrame (WindowFrameOptions) — barre de titre
 *              personnalisée (déplacer, agrandir, fermer), barre d'état
 *              (img/s, chunks, simulation, maillage, fils) et poignée ;
 *   menus    : Fichier, Édition, Affichage, Simulation, Outils, Aide ;
 *   gauche   : dock « World Outliner » (filtres texte / genre / distance /
 *              vivants, 5 Hz) et « Éléments » (navigateur du contenu à la
 *              MCreator : genres, nombres, création guidée) ;
 *   centre   : dock « Vue 3D » — barre d'outils (simulation, poursuite,
 *              vues normalisées, gizmo déplacer / tourner / échelle,
 *              accroche), vue DIRECT (surface de l'application : les clics
 *              vont au studio) et calque du gizmo ;
 *   bas      : dock des catalogues (blocs, modèles, structures, textures,
 *              entités, monde) et dock « Console » (code d'état, filtres
 *              info / avertissement / erreur, marque-pages) ;
 *   droite   : dock de l'inspecteur (entité ou bloc sélectionné, valeurs
 *              modifiables à la volée) et des éditeurs (bloc, modèle,
 *              structure, texture, entité, monde) ;
 *   palette  : F1 ou Ctrl+Maj+P — toute commande par quelques lettres ;
 *   diagnostic : bouton de la barre d'outils, menu Outils, `studio.diag.*` —
 *              capture de performances (diagnostics::trace) exportée en
 *              .vcap à l'arrêt, ou à la fermeture du jeu si elle tourne
 *              encore (cf. Shell::StartDiagnostic, demos/profiler).
 *
 * Vue 3D : clic gauche sélectionne (entité, sinon bloc) ou saisit le gizmo ;
 * milieu orbite autour du point visé (Maj : panoramique) ; molette : zoom
 * vers le curseur ; droit maintenu + ZQSD/WASD, E/A : vol ; F : cadrer la
 * sélection ; W/E/R : déplacer / tourner / échelle (hors vol) ; double-clic
 * d'une entité : caméra dessus ; « Suivre » : poursuite.
 *
 * Fils : Update et HandleEvent ne s'appellent que lorsque le fil principal a
 * la main sur le jeu (après Shell::EndSimulation, avant BeginSimulation) —
 * le studio ne touche jamais au jeu pendant un pas de « voxel-update ».
 *
 * Échap : quitte la poursuite, puis désélectionne ; ne ferme jamais rien.
 */
#include <memory>
#include <vector>

#include <editor/editor.hpp>
#include <ui/window_frame.hpp>
#include <ui/window_manager.hpp>

#include "block_editor.hpp"
#include "entity_editor.hpp"
#include "model_editor.hpp"
#include "console.hpp"
#include "outliner.hpp"
#include "report.hpp"
#include "structure_editor.hpp"
#include "texture_editor.hpp"
#include "worldgen_editor.hpp"

namespace voxelia {

class Shell;

namespace studio {

/// Caméra libre de l'éditeur (cap/tangage en radians, tangage borné ±1,5).
struct EditorCamera {
	math::FVector3 position{0.f, 80.f, 0.f};
	float yaw = 0.f, pitch = -0.3f;
	[[nodiscard]] math::FVector3 Forward() const noexcept;
	[[nodiscard]] render3d::Camera ToCamera(float aspect, float farPlane) const;
	void Look(float dyaw, float dpitch) noexcept;
	/// Regarde `target` depuis la position courante.
	void LookAt(const math::FVector3 &target) noexcept;
};

class Studio {
public:
	Studio(Shell &shell, ui::WindowContext &window, StudioReport *report, String viewportTransfer);
	~Studio();
	Studio(const Studio &) = delete;
	Studio &operator=(const Studio &) = delete;

	/// Évènement destiné à la fenêtre du studio (déjà passé par
	/// WindowManager::Dispatch : `consumed` = pris par l'interface).
	void HandleEvent(const sdl3::Event &ev, bool consumed);
	void Update(float dt);
	/// Commande pilotée (--exec=N:!studio …, scénarios) : même chemin que la
	/// console.
	CommandResult Execute(const String &line);

	[[nodiscard]] ui::WindowContext &Window() noexcept { return m_window; }
	[[nodiscard]] ConsoleModel &Console() noexcept { return m_console; }
	[[nodiscard]] ecs::Entity Selected() const noexcept { return m_selected; }
	[[nodiscard]] const std::vector<OutlinerEntry> &Entries() const noexcept { return m_entries; }
	[[nodiscard]] const EditorCamera &Camera() const noexcept { return m_camera; }
	[[nodiscard]] BlockEditor &Blocks() noexcept { return *m_blocks; }
	[[nodiscard]] ModelEditor &Models() noexcept { return *m_models; }
	[[nodiscard]] StructureEditor &Structures() noexcept { return *m_structures; }
	[[nodiscard]] TextureEditor &Textures() noexcept { return *m_textures; }
	[[nodiscard]] EntityEditor &Entities() noexcept { return *m_entities; }
	[[nodiscard]] WorldgenEditor &Worldgen() noexcept { return *m_worldgen; }
	[[nodiscard]] const editor::TransformGizmo &Gizmo() const noexcept { return m_gizmo; }
	[[nodiscard]] Option<voxel::BlockPos> SelectedBlock() const noexcept { return m_selectedBlock; }
	[[nodiscard]] editor::CommandPalette &Palette() noexcept { return m_palette; }
	/// Docks (tests, scénarios) : gauche, centre, catalogues, console, droite.
	[[nodiscard]] editor::DockPanel &LeftDock() noexcept { return m_leftDock; }
	[[nodiscard]] editor::DockPanel &CatalogDock() noexcept { return m_catalogDock; }
	[[nodiscard]] editor::DockPanel &RightDock() noexcept { return m_rightDock; }

private:
	void Build();
	void BuildMenus(ui::Widget parent);
	void BuildViewport(ui::Widget page);
	void BuildOutliner(ui::Widget page);
	void BuildElements(ui::Widget page);
	void BuildConsole(ui::Widget page);
	void BuildInspector(ui::Widget page);
	void BuildPalette();
	void RegisterCommands();
	void Select(ecs::Entity entity);
	void SelectBlock(Option<voxel::BlockPos> pos);
	void FocusOn(ecs::Entity entity);
	void RefreshOutliner();
	void RebuildOutliner();
	void RefreshInspector();
	void RefreshConsole();
	void RebuildConsole();
	void RefreshToolbar();
	void RefreshElements();
	void UpdateCamera(float dt);
	void UpdateGizmo();
	void DrawOverlay(ui::Painter &painter, const sdl3::FRect &rect);
	void OpenNewElement(int kind);
	void ShowRight(int index);
	[[nodiscard]] bool HandleViewportEvent(const sdl3::Event &ev, bool consumed);
	[[nodiscard]] Option<editor::GizmoView> ViewportView();
	/// Point du monde sous le curseur (bloc, sinon 12 blocs devant).
	[[nodiscard]] math::FVector3 PointUnder(float x, float y);
	/// Navigation orbitale (module editor) appliquée à la caméra libre.
	void Navigate(const std::function<void(editor::OrbitNavigator &)> &op, const math::FVector3 &pivot);
	void Visit(const char *feature, FeatureStatus status = FeatureStatus::Ok, String detail = String());
	[[nodiscard]] voxel::Game *Game();

	Shell &m_shell;
	ui::WindowContext &m_window;
	StudioReport *m_report;
	String m_transfer;
	ConsoleModel m_console;
	std::unique_ptr<BlockEditor> m_blocks;
	std::unique_ptr<ModelEditor> m_models;
	std::unique_ptr<StructureEditor> m_structures;
	std::unique_ptr<TextureEditor> m_textures;
	std::unique_ptr<EntityEditor> m_entities;
	std::unique_ptr<WorldgenEditor> m_worldgen;

	// Cadre, docks, palette (module editor)
	ui::WindowFrame m_frame;
	editor::DockPanel m_leftDock, m_centerDock, m_catalogDock, m_consoleDock, m_rightDock;
	editor::CommandPalette m_palette;
	std::unique_ptr<editor::CommandPaletteView> m_paletteView;

	// Widgets
	ui::Widget m_root, m_viewport, m_outlinerList, m_outlinerCount, m_inspector, m_consoleLog, m_consoleInput, m_consoleBar;
	ui::Widget m_diagnosticButton;
	ui::Widget m_pauseButton, m_followButton, m_stats, m_elementList, m_wizard, m_wizardId, m_wizardMod, m_wizardStatus;
	ui::Widget m_gizmoButtons[3];
	int m_wizardKind = 0;
	struct RowView {
		String label;
		ui::Widget value;
		bool editable = false;
	};
	std::vector<RowView> m_inspectorRows;
	String m_inspectorSignature;
	std::vector<String> m_consoleKinds; ///< filtre du journal (vide : tout)
	size_t m_consoleShown = 0;
	bool m_consoleDirty = true;
	String m_elementSignature;
	float m_elementTimer = 0.f;
	/// Barre d'état : 4 mises à jour par seconde (un texte qui change de
	/// largeur à chaque image coûterait une mise en page complète par image).
	float m_statusTimer = 0.f;

	// Modèles
	OutlinerFilter m_filter;
	std::vector<OutlinerEntry> m_entries;
	String m_outlinerSignature;
	float m_outlinerTimer = 0.f;
	ecs::Entity m_selected{};
	Option<voxel::BlockPos> m_selectedBlock = NONE;
	bool m_follow = false;
	bool m_showStats = false;
	math::FVector3 m_followHeading{0.f, 0.f, 1.f};

	// Caméra, navigation, gizmo
	EditorCamera m_camera;
	editor::OrbitNavigator m_nav;
	editor::TransformGizmo m_gizmo;
	bool m_cameraPlaced = false;
	bool m_flying = false;
	bool m_orbiting = false, m_panning = false;
	math::FVector3 m_orbitPivot{0.f, 0.f, 0.f};
	float m_wheel = 0.f, m_wheelX = 0.f, m_wheelY = 0.f;
	struct GizmoStart {
		math::FVector3 position{0.f, 0.f, 0.f};
		float yaw = 0.f, pitch = 0.f, scale = 1.f;
	} m_gizmoStart;
	render3d::Object3D m_emptyScene;
};

} // namespace studio
} // namespace voxelia
