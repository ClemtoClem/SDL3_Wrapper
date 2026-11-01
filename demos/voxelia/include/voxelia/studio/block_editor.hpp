#pragma once
/**
 * Éditeur de blocs du studio (panneau) : le document d'un bloc
 * (BlockDocument) en formulaire, en JSON et en états fusionnés, validé en
 * direct, appliqué À CHAUD sur la partie en cours (Session::ReloadDefinition)
 * et enregistré dans son mod.
 *
 * Portée d'édition : « Bloc (commun) » écrit les champs de la racine, un
 * état écrit `states.<nom>.<champ>` — ce que cet état surcharge.
 *
 * Gestes : un changement continu (curseur numérique glissé) n'ouvre qu'UNE
 * étape d'annulation, close après 0,4 s sans changement.
 *
 * Catalogue : affiché une fois la partie prête (tous les mods chargés) ; la
 * recherche des blocs (filtre compris) tourne sur un fil à part — un
 * spinner tourne pendant ce temps — et repart quand le contenu change
 * (voxel::Game::ContentGeneration) ; la partie l'attend avant de toucher au
 * contenu (Game::AddContentGuard).
 *
 * Fil principal uniquement, quand la simulation ne tourne pas (cf. Studio).
 */
#include <functional>
#include <memory>
#include <vector>

#include <render3d/object3d.hpp>
#include <ui/ui.hpp>
#include <voxel/game.hpp>

#include "block_document.hpp"
#include "console.hpp"
#include "report.hpp"

namespace voxelia {

class Shell;

namespace studio {

class BlockEditor {
public:
	/// `placeAt` : bloc devant la caméra de l'éditeur (pose).
	BlockEditor(Shell &shell, StudioReport *report, std::function<voxel::BlockPos()> placeAt);
	~BlockEditor();
	BlockEditor(const BlockEditor &) = delete;
	BlockEditor &operator=(const BlockEditor &) = delete;

	/// Construit le panneau sous `parent` (onglet du dock).
	void Build(ui::Widget parent);
	/// Liste des blocs (filtrable) sous `parent` — un clic ouvre le bloc.
	void BuildCatalog(ui::Widget parent);
	void Update(float dt);

	// ── Actions (boutons, console, scénarios) ───────────────────────────────
	CommandResult Open(const String &blockId);
	/// Filtre du catalogue (la recherche repart sur son fil).
	CommandResult SetCatalogFilter(const String &text);
	/// Une recherche du catalogue est en cours (ou en attente de la partie).
	[[nodiscard]] bool CatalogBusy() const;
	CommandResult New(const String &id, const String &mod);
	CommandResult SetField(const String &path, const String &jsonValue);
	CommandResult SetScope(const String &state); ///< "" : bloc commun
	CommandResult Apply();                       ///< rechargement à chaud
	CommandResult Save();
	CommandResult Place();
	CommandResult Undo();
	CommandResult Redo();
	/// Recharge depuis le disque la définition d'un bloc (`reload block`).
	CommandResult ReloadFromDisk(const String &blockId);
	/// Recharge depuis le disque toutes les définitions de blocs d'un mod.
	CommandResult ReloadMod(const String &mod);
	/// Forme nodebox (pixels 0..16) dans la portée courante.
	CommandResult ShapeAddBox(const BlockDocument::NodeBox &box);
	CommandResult ShapeMirror(size_t index, int axis);
	CommandResult ShapeRotate();
	CommandResult ShapeRemove(size_t index);
	/// Triangles de l'aperçu de forme (0 : rien à montrer).
	[[nodiscard]] size_t ShapePreviewTriangles() const noexcept { return m_shapeTriangles; }

	[[nodiscard]] bool HasDocument() const noexcept { return m_doc.IsSome(); }
	[[nodiscard]] const BlockDocument *Document() const noexcept { return m_doc.IsSome() ? &m_doc.Value() : nullptr; }
	[[nodiscard]] const std::vector<BlockDiagnostic> &Diagnostics() const noexcept { return m_diagnostics; }
	[[nodiscard]] size_t Reloads() const noexcept { return m_reloads; }
	/// Un bloc vient d'être ouvert ou créé (le studio affiche l'onglet).
	std::function<void()> onDocumentOpened{};

private:
	[[nodiscard]] voxel::Game *Game();
	[[nodiscard]] String Path(const char *field) const; ///< champ dans la portée courante
	void Edit(const char *field, dataio::NodePtr value, bool continuous);
	void CloseGesture();
	void RebuildForm();
	/// Le catalogue est à refaire (filtre, contenu) : la recherche repartira.
	void RebuildCatalog();
	/// Catalogue (à chaque image) : attend que la partie soit prête (mods
	/// chargés), lance la recherche sur son fil quand le contenu ou le filtre
	/// ont changé, et affiche son résultat quand elle est finie.
	void PumpCatalog();
	void RefreshDiagnostics();
	void RefreshStates();
	void RefreshHeader();
	void Visit(const char *feature, FeatureStatus status = FeatureStatus::Ok, String detail = String());
	CommandResult Reload(const dataio::NodePtr &definition, const voxel::ContentRegistry::LoadContext &context,
						 const String &what);

	Shell &m_shell;
	StudioReport *m_report;
	std::function<voxel::BlockPos()> m_placeAt;
	Option<BlockDocument> m_doc = NONE;
	String m_scope; ///< état édité (vide : bloc commun)
	std::vector<BlockDiagnostic> m_diagnostics;
	uint64_t m_version = 0, m_formVersion = ~0ull, m_validatedVersion = ~0ull;
	float m_validateDelay = 0.f;
	float m_gestureTimer = 0.f;
	bool m_gesture = false;
	size_t m_reloads = 0;
	String m_catalogFilter;
	/// Recherche des blocs sur le fil « studio-catalog » (partagée avec la
	/// garde de contenu de la partie, qui l'attend avant tout changement).
	struct CatalogSearch;
	std::shared_ptr<CatalogSearch> m_search;
	voxel::Game *m_guardedGame = nullptr;
	size_t m_guardToken = 0;
	uint64_t m_catalogGeneration = ~0ull; ///< génération du contenu affichée
	bool m_catalogDirty = true;

	void RebuildShape();
	CommandResult ShapeDone(Result<bool, String> result, const char *feature);

	// Aperçu de la forme (scène propre, maillée par le mailleur des chunks).
	render3d::Object3D m_shapeRoot;
	uint64_t m_shapeVersion = ~0ull;
	size_t m_shapeTriangles = 0;
	float m_shapeOrbit = 0.7f;
	ui::Widget m_shapeView, m_shapeBoxes, m_tabs;

	ui::Widget m_title, m_scopeRow, m_form, m_jsonEditor, m_jsonStatus, m_states, m_diagnosticList, m_catalogList, m_catalogBusy, m_catalogStatus, m_catalogFilterInput;
};

} // namespace studio
} // namespace voxelia
