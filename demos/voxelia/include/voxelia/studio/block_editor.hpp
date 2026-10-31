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
 * Fil principal uniquement, quand la simulation ne tourne pas (cf. Studio).
 */
#include <functional>
#include <vector>

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

	/// Construit le panneau sous `parent` (onglet du dock).
	void Build(ui::Widget parent);
	/// Liste des blocs (filtrable) sous `parent` — un clic ouvre le bloc.
	void BuildCatalog(ui::Widget parent);
	void Update(float dt);

	// ── Actions (boutons, console, scénarios) ───────────────────────────────
	CommandResult Open(const String &blockId);
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
	void RebuildCatalog();
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

	ui::Widget m_title, m_scopeRow, m_form, m_jsonEditor, m_jsonStatus, m_states, m_diagnosticList, m_catalogList;
};

} // namespace studio
} // namespace voxelia
