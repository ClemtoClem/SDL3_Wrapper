#pragma once
/**
 * Voxelia Studio — l'éditeur, dans SA fenêtre SDL (ui::WindowManager), sur la
 * MÊME partie que la fenêtre de jeu (Shell / Session / voxel::Game) : rien
 * n'est copié, l'outliner et l'inspecteur lisent le registre ECS vivant, la
 * vue 3D affiche la scène du WorldRenderer de la partie.
 *
 * Disposition (docks redimensionnables) :
 *   barre d'outils : pause / pas à pas de la simulation, suivre, caméra au
 *                    joueur, état (img/s, chunks en attente, ms simulation,
 *                    ms maillage, fils) ;
 *   gauche  : World Outliner (filtre, groupes par catégorie, 5 Hz) ;
 *   centre  : vue 3D DIRECT (même device GPU que la fenêtre de jeu : aucune
 *             copie) — clic droit maintenu + ZQSD/WASD, E/A montée/descente,
 *             molette avance/recule ; double-clic d'une entité : caméra
 *             dessus ; « Suivre » : poursuite, cap pris sur la vitesse ;
 *   droite  : inspecteur de l'entité sélectionnée (rafraîchi à chaque image) ;
 *   bas     : console à code d'état (ConsoleModel).
 *
 * Fils : Update et HandleEvent ne s'appellent que lorsque le fil principal a
 * la main sur le jeu (après Shell::EndSimulation, avant BeginSimulation) —
 * le studio ne touche jamais au jeu pendant un pas de « voxel-update ».
 *
 * Échap : quitte la poursuite, puis désélectionne ; ne ferme jamais rien.
 */
#include <memory>
#include <vector>

#include <ui/window_manager.hpp>

#include "block_editor.hpp"
#include "console.hpp"
#include "outliner.hpp"
#include "report.hpp"

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

private:
	void Build();
	void RegisterCommands();
	void Select(ecs::Entity entity);
	void FocusOn(ecs::Entity entity);
	void RefreshOutliner();
	void RebuildOutliner();
	void RefreshInspector();
	void RefreshConsole();
	void RefreshToolbar();
	void UpdateCamera(float dt);
	void Visit(const char *feature, FeatureStatus status = FeatureStatus::Ok, String detail = String());
	[[nodiscard]] voxel::Game *Game();

	Shell &m_shell;
	ui::WindowContext &m_window;
	StudioReport *m_report;
	String m_transfer;
	ConsoleModel m_console;
	std::unique_ptr<BlockEditor> m_blocks;

	// Widgets
	ui::Widget m_viewport, m_outlinerList, m_outlinerCount, m_inspector, m_consoleLog, m_consoleInput, m_consoleBar;
	ui::Widget m_status, m_pauseButton, m_followButton, m_stats;
	struct RowView {
		String label;
		ui::Widget value;
	};
	std::vector<RowView> m_inspectorRows;
	String m_inspectorSignature;
	size_t m_consoleShown = 0;

	// Modèles
	OutlinerFilter m_filter;
	std::vector<OutlinerEntry> m_entries;
	String m_outlinerSignature;
	float m_outlinerTimer = 0.f;
	ecs::Entity m_selected{};
	bool m_follow = false;
	bool m_showStats = false;
	math::FVector3 m_followHeading{0.f, 0.f, 1.f};

	// Caméra
	EditorCamera m_camera;
	bool m_cameraPlaced = false;
	bool m_flying = false;
	float m_wheel = 0.f;
	render3d::Object3D m_emptyScene;
};

} // namespace studio
} // namespace voxelia
