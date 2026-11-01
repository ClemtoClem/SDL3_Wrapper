#pragma once
/**
 * Éditeur d'entités du studio (panneau) — créatures, entités, projectiles…
 * (EntityDocument), sur la partie en cours :
 *
 *  - catalogue par genre ; formulaire (identité, physique, gameplay,
 *    apparence, lumière, sons, apparition), comportements (IA) ordonnés avec
 *    leurs paramètres, butin, skins ; onglet JSON ; diagnostics à blanc ;
 *    composants que recevra l'entité ;
 *  - « Appliquer » : la définition est rechargée à chaud (Game::ReloadDefinition) ;
 *    les entités DÉJÀ apparues gardent leurs composants (copiés à
 *    l'apparition) — l'aperçu, lui, est refait ;
 *  - APERÇU DE COMPORTEMENT : une arène close (sol de pierre 13 × 13, murs et
 *    toit de verre) bâtie devant la caméra du studio, l'entité seule dedans,
 *    IA active ; le studio la sélectionne (inspection en direct) et la suit.
 *    « Arrêter » la retire (sans butin) et rend la région telle qu'elle était.
 *
 * Tout geste est aussi une commande `studio.entity.*` (scénarios).
 * Fil principal uniquement, quand la simulation ne tourne pas (cf. Studio).
 */
#include <functional>

#include <ui/ui.hpp>
#include <voxel/game.hpp>

#include "console.hpp"
#include "entity_document.hpp"
#include "report.hpp"

namespace voxelia {

class Shell;

namespace studio {

class EntityEditor {
public:
	static constexpr int ARENA_HALF = 6, ARENA_HEIGHT = 5;

	/// `arenaAt` : centre du sol de l'arène (devant la caméra de l'éditeur).
	EntityEditor(Shell &shell, StudioReport *report, std::function<voxel::BlockPos()> arenaAt);
	~EntityEditor();
	EntityEditor(const EntityEditor &) = delete;
	EntityEditor &operator=(const EntityEditor &) = delete;

	void Build(ui::Widget parent);
	void BuildCatalog(ui::Widget parent);
	void Update(float dt);
	void RegisterCommands(ConsoleModel &console);

	CommandResult Open(const String &entityId);
	CommandResult New(const String &id, const String &mod);
	/// Champ (chemin pointé) ; valeur JSON, ou mot nu pris comme texte ; vide : retiré.
	CommandResult SetField(const String &path, const String &value);
	CommandResult Apply();
	CommandResult Save();
	CommandResult Undo();
	CommandResult Redo();
	CommandResult AddBehavior(const String &id);
	CommandResult RemoveBehavior(size_t index);
	CommandResult MoveBehavior(size_t from, size_t to);
	CommandResult SetBehaviorParam(size_t index, const String &key, const String &value);
	CommandResult AddDrop(const String &item, int count);
	CommandResult RemoveDrop(size_t index);

	CommandResult StartPreview();
	CommandResult StopPreview();
	[[nodiscard]] bool Previewing() const noexcept { return m_preview.Valid(); }
	[[nodiscard]] ecs::Entity PreviewEntity() const noexcept { return m_preview; }

	[[nodiscard]] const EntityDocument *Document() const noexcept { return m_doc.IsSome() ? &m_doc.Value() : nullptr; }
	[[nodiscard]] const std::vector<EntityDiagnostic> &Diagnostics() const noexcept { return m_diagnostics; }
	[[nodiscard]] size_t Reloads() const noexcept { return m_reloads; }
	std::function<void()> onDocumentOpened{};
	/// L'entité d'aperçu vient d'apparaître (le studio la sélectionne et la suit).
	std::function<void(ecs::Entity)> onPreviewSpawned{};

private:
	[[nodiscard]] voxel::Game *Game();
	CommandResult Done(Result<bool, String> result, const char *feature, const String &ok);
	void Visit(const char *feature, FeatureStatus status = FeatureStatus::Ok, String detail = String());
	void Changed();
	void RebuildCatalog();
	void RebuildAll();
	void RebuildForm();
	void RebuildLists();
	void Revalidate();
	/// Fait (ou refait) apparaître l'entité au centre de l'arène.
	CommandResult SpawnPreview();

	Shell &m_shell;
	StudioReport *m_report;
	std::function<voxel::BlockPos()> m_arenaAt;
	Option<EntityDocument> m_doc = NONE;
	std::vector<EntityDiagnostic> m_diagnostics;
	uint64_t m_version = 0, m_shownVersion = ~0ull, m_validatedVersion = ~0ull;
	float m_validateDelay = 0.f;
	size_t m_reloads = 0;
	String m_filter;

	// Aperçu : l'entité, son arène et ce qu'elle a remplacé.
	ecs::Entity m_preview{};
	voxel::BlockPos m_arenaCenter{0, 0, 0};
	Option<std::pair<voxel::Schematic, voxel::BlockPos>> m_arenaBackup = NONE;

	ui::Widget m_title, m_status, m_form, m_behaviors, m_drops, m_skins, m_components, m_diagnosticList, m_json, m_jsonStatus, m_catalog,
		m_newBehavior, m_newDrop;
};

} // namespace studio
} // namespace voxelia
