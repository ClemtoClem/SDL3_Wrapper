#pragma once
/**
 * Éditeur de structures du studio (panneau) — voxel::Schematic + Formula sur
 * la partie en cours :
 *
 *  - catalogue : bibliothèque d'échange (GameOptions::structureDirectory,
 *    partagée entre mondes), structures du monde, presse-papiers nommés de
 *    la partie ; un clic ouvre ;
 *  - document (StructureDocument) : aperçu 3D maillé comme le monde,
 *    rotation, miroirs, enveloppe, remplacement, comptes par bloc, ancre ;
 *    enregistrement .vxs.json / .json de mod / .we / .mts ;
 *  - formule : éditeur coloré (TokenizeFormula), aide-mémoire, « vérifier »
 *    (erreur avec position), « tracer » (voxel::Plot : implicite, courbe,
 *    surface, solide ; convention maths ou jeu, échelle, enveloppe, limites) ;
 *  - monde : coins A / B surlignés (Game::SetHighlight), capture, remplissage
 *    (all, hollow, walls, outline, keep), remplacement ; collage au bloc visé
 *    (rotation, air gardé / fusionné / seulement l'air) avec FANTÔME
 *    translucide et boîte surlignée, et annulation du dernier collage.
 *
 * Tout geste est aussi une commande `studio.structure.*` (scénarios).
 * Fil principal uniquement, quand la simulation ne tourne pas (cf. Studio).
 */
#include <array>
#include <functional>
#include <memory>

#include <render3d/object3d.hpp>
#include <ui/ui.hpp>
#include <voxel/game.hpp>

#include "console.hpp"
#include "report.hpp"
#include "structure_document.hpp"

namespace voxelia {

class Shell;

namespace studio {

class StructureEditor {
public:
	/// `placeAt` : bloc visé par la caméra de l'éditeur (collage, coins).
	StructureEditor(Shell &shell, StudioReport *report, std::function<voxel::BlockPos()> placeAt);
	~StructureEditor();
	StructureEditor(const StructureEditor &) = delete;
	StructureEditor &operator=(const StructureEditor &) = delete;

	void Build(ui::Widget parent);
	void BuildCatalog(ui::Widget parent);
	void Update(float dt);
	void RegisterCommands(ConsoleModel &console);

	// ── Document ────────────────────────────────────────────────────────────
	CommandResult New(const voxel::BlockPos &size, const String &name);
	/// « clipboard:nom », « world:nom », nom de la bibliothèque, ou chemin.
	CommandResult Open(const String &what);
	/// Nom (bibliothèque, « .vxs.json » si sans extension), « world:nom », ou chemin.
	CommandResult Save(const String &target);
	CommandResult ToClipboard(const String &name);
	CommandResult Rotate(int quarters);
	CommandResult Mirror(int axis);
	CommandResult Hollow(int flatAxis);
	CommandResult Replace(const String &from, const String &to);
	CommandResult SetCell(const voxel::BlockPos &p, const String &block);
	CommandResult SetAnchor(const voxel::BlockPos &p);
	CommandResult Undo();
	CommandResult Redo();

	// ── Formule ─────────────────────────────────────────────────────────────
	[[nodiscard]] voxel::PlotSpec &Formula() noexcept { return m_spec; }
	CommandResult CheckFormula();
	CommandResult Plot();

	// ── Monde ───────────────────────────────────────────────────────────────
	/// Coin A (0) ou B (1) de la sélection ; NONE : le bloc visé.
	CommandResult SetCorner(int corner, Option<voxel::BlockPos> at);
	CommandResult Capture();
	CommandResult Fill(const String &block, const String &mode);
	CommandResult ReplaceRegion(const String &from, const String &to);
	/// Colle la structure au point donné (NONE : le bloc visé), selon `paste`.
	CommandResult Paste(Option<voxel::BlockPos> at);
	CommandResult UndoPaste();
	CommandResult ShowGhost(bool on);
	voxel::Schematic::PasteOptions paste;

	[[nodiscard]] const StructureDocument *Document() const noexcept { return m_doc.IsSome() ? &m_doc.Value() : nullptr; }
	[[nodiscard]] size_t PreviewTriangles() const noexcept { return m_previewTriangles; }
	[[nodiscard]] bool GhostShown() const noexcept { return m_ghost; }
	[[nodiscard]] Option<voxel::BlockPos> Corner(int i) const { return m_corners[size_t(i & 1)]; }
	std::function<void()> onDocumentOpened{};

private:
	[[nodiscard]] voxel::Game *Game();
	CommandResult Opened(StructureDocument doc, const String &feature, const String &detail);
	CommandResult Done(Result<bool, String> result, const char *feature, const String &ok);
	void Visit(const char *feature, FeatureStatus status = FeatureStatus::Ok, String detail = String());
	void RebuildCatalog();
	void RebuildInfo();
	void RebuildPreview();
	void RefreshSelection();
	void RefreshGhost();
	void ClearWorldMarks();
	void ReadFormulaFields();
	[[nodiscard]] String LibraryPath(const String &name, bool exchange) const;

	Shell &m_shell;
	StudioReport *m_report;
	std::function<voxel::BlockPos()> m_placeAt;
	Option<StructureDocument> m_doc = NONE;
	voxel::PlotSpec m_spec;
	std::array<Option<voxel::BlockPos>, 2> m_corners{NONE, NONE};
	bool m_ghost = false;
	/// Région écrasée par le dernier collage (pour l'annuler) et son coin.
	Option<std::pair<voxel::Schematic, voxel::BlockPos>> m_pasteUndo = NONE;
	uint64_t m_shownVersion = ~0ull, m_previewVersion = ~0ull;
	float m_catalogTimer = 0.f;

	// Fantôme : la structure tournée telle qu'elle sera collée.
	std::shared_ptr<const voxel::Schematic> m_ghostShape;
	uint64_t m_ghostVersion = ~0ull;
	int m_ghostRotation = -1;

	// Aperçu (scène propre au panneau).
	render3d::Object3D m_previewRoot;
	size_t m_previewTriangles = 0;
	math::FVector3 m_previewSize{1.f, 1.f, 1.f};
	float m_orbit = 0.6f;

	ui::Widget m_title, m_viewport, m_info, m_counts, m_catalog, m_saveName, m_replaceFrom, m_replaceTo, m_formula, m_formulaStatus,
		m_fx, m_fy, m_fz, m_block, m_scale, m_bounds, m_ranges, m_selection, m_fillBlock, m_status;
};

} // namespace studio
} // namespace voxelia
