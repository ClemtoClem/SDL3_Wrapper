#pragma once
/**
 * Éditeur de biomes, de générateurs et de dimensions du studio (panneau) —
 * WorldgenDocument sur la partie en cours :
 *
 *  - catalogue (dimensions, générateurs, biomes) ; formulaire selon le genre,
 *    onglet JSON, diagnostics à blanc ;
 *  - APERÇU : N × N chunks générés à part (BuildPreview : copie du contenu,
 *    document chargé, générateur et graine de la dimension), maillés avec
 *    l'atlas de la partie dans la vue du panneau, coupés au plafond d'un
 *    labyrinthe ; la partie n'est pas touchée ;
 *  - carte des biomes du générateur (couleur par biome, légende) ;
 *  - plan d'un labyrinthe (étages : sol, plafond, graine « + 7919 × étage »,
 *    niveau ; gaines d'ascenseur et étages desservis) ; réglages de mod
 *    référencés et leur valeur figée dans ce monde (Game::ResolveSettings) ;
 *  - chaîne des dimensions (morts, ascenseurs, portails), dessinée ;
 *  - « Appliquer à chaud » : la définition est rechargée, MAIS les chunks déjà
 *    générés ne changent pas (avertissement) ; « Régénérer dans une dimension
 *    temporaire » : une copie « <dimension>_studio » (même graine :
 *    « seedFrom »), avec le document, où le joueur est envoyé (confirmation :
 *    la dimension courante est déchargée) ; « Revenir » l'en ramène.
 *
 * Tout geste est aussi une commande `studio.world.*` (scénarios).
 * Fil principal uniquement, quand la simulation ne tourne pas (cf. Studio).
 */
#include <functional>
#include <memory>

#include <render3d/object3d.hpp>
#include <sdl3/render.hpp>
#include <ui/ui.hpp>
#include <voxel/game.hpp>

#include "console.hpp"
#include "report.hpp"
#include "worldgen_document.hpp"

namespace voxelia {

class Shell;

namespace studio {

class WorldgenEditor {
public:
	WorldgenEditor(Shell &shell, StudioReport *report);
	~WorldgenEditor();
	WorldgenEditor(const WorldgenEditor &) = delete;
	WorldgenEditor &operator=(const WorldgenEditor &) = delete;

	void Build(ui::Widget parent);
	void BuildCatalog(ui::Widget parent);
	void Update(float dt);
	void RegisterCommands(ConsoleModel &console);

	CommandResult Open(WorldgenKind kind, const String &id);
	CommandResult New(WorldgenKind kind, const String &id, const String &mod);
	CommandResult SetField(const String &path, const String &value);
	CommandResult Apply();
	CommandResult Save();
	CommandResult Undo();
	CommandResult Redo();
	/// Aperçu de `side` × `side` chunks autour du joueur ; `cutY` NONE : auto.
	CommandResult Preview(int side, Option<int> cutY);
	CommandResult RegenerateTemporary();
	CommandResult ReturnFromTemporary();

	[[nodiscard]] const WorldgenDocument *Document() const noexcept { return m_doc.IsSome() ? &m_doc.Value() : nullptr; }
	[[nodiscard]] const WorldgenPreview *LastPreview() const noexcept { return m_preview.get(); }
	[[nodiscard]] size_t PreviewTriangles() const noexcept { return m_previewTriangles; }
	std::function<void()> onDocumentOpened{};

private:
	[[nodiscard]] voxel::Game *Game();
	void Visit(const char *feature, FeatureStatus status = FeatureStatus::Ok, String detail = String());
	void Changed();
	void RebuildCatalog();
	void RebuildAll();
	void RebuildForm();
	void RebuildMaze();
	void RebuildSettings();
	void Revalidate();
	void DrawBiomeMap(ui::Painter &painter, const sdl3::FRect &rect);
	void DrawGraph(ui::Painter &painter, const sdl3::FRect &rect);

	Shell &m_shell;
	StudioReport *m_report;
	Option<WorldgenDocument> m_doc = NONE;
	std::vector<WorldgenDiagnostic> m_diagnostics;
	uint64_t m_version = 0, m_shownVersion = ~0ull, m_validatedVersion = ~0ull;
	float m_validateDelay = 0.f;
	String m_filter;
	String m_returnDimension; ///< d'où vient le joueur envoyé dans une dimension temporaire
	bool m_inTemporary = false;

	// Aperçu.
	std::unique_ptr<WorldgenPreview> m_preview;
	render3d::Object3D m_previewRoot;
	size_t m_previewTriangles = 0;
	float m_orbit = 0.5f;
	// Carte des biomes : image et légende.
	voxel::Image m_map;
	std::vector<std::pair<String, std::array<uint8_t, 3>>> m_legend;
	Option<sdl3::Texture> m_mapTexture = NONE;
	uint64_t m_mapVersion = 0, m_mapUploaded = ~0ull;

	ui::Widget m_title, m_status, m_form, m_diagnosticList, m_json, m_jsonStatus, m_catalog, m_viewport, m_maze, m_settings, m_legendList,
		m_side, m_cut;
};

} // namespace studio
} // namespace voxelia
