#pragma once
/**
 * Atelier de textures du studio (panneau) — peinture 2D (TextureDocument) sur
 * une texture du contenu, appliquée à chaud à la partie :
 *
 *  - toile à grille de pixels (zoom à la molette), outils crayon, gomme,
 *    pipette, remplissage, ligne, rectangle, ellipse, sélection, déplacement ;
 *    pinceau (taille, opacité, mode, symétrie), palette des couleurs récentes ;
 *  - calques (visibilité, opacité, mode, ordre, groupe, fusion), annulation
 *    par trait ;
 *  - aperçus : chaîne des mipmaps à la résolution de rendu choisie (ce que
 *    l'atlas fera), comparaison « éclairé par le ciel » / « lueur » (pleine
 *    luminosité, TextureAtlas::Glow), bloc témoin pour les variantes
 *    (voxel::VariantIndex : la variante que le jeu tire à chaque position) ;
 *  - fichiers : import / export PNG et JPG, projet en calques, enregistrement
 *    dans le mod puis texture relue (AssetManager::ReloadTexture) et atlas
 *    reconstruit (WorldRenderer::RebuildAtlas, paramètres réglables : temps,
 *    taille, occupation, liste des tuiles), chunks remaillés ;
 *  - variantes « nom.N.png » : ajout (copie ou bruit), suppression (avec
 *    confirmation).
 *
 * Tout geste est aussi une commande `studio.texture.*` (scénarios).
 * Fil principal uniquement, quand la simulation ne tourne pas (cf. Studio).
 */
#include <functional>
#include <memory>
#include <vector>

#include <sdl3/render.hpp>
#include <ui/ui.hpp>
#include <voxel/game.hpp>

#include "console.hpp"
#include "report.hpp"
#include "texture_document.hpp"

namespace voxelia {

class Shell;

namespace studio {

enum class PaintTool : uint8_t { PENCIL, ERASER, PICKER, FILL, LINE, RECT, ELLIPSE, SELECT, MOVE };
[[nodiscard]] const char *PaintToolId(PaintTool tool) noexcept;
[[nodiscard]] Option<PaintTool> PaintToolOf(const String &id) noexcept;

class TextureEditor {
public:
	TextureEditor(Shell &shell, StudioReport *report);
	~TextureEditor();
	TextureEditor(const TextureEditor &) = delete;
	TextureEditor &operator=(const TextureEditor &) = delete;

	void Build(ui::Widget parent);
	void BuildCatalog(ui::Widget parent);
	void Update(float dt);
	/// Souris sur la toile (évènement de la fenêtre du studio) ; vrai si pris.
	bool HandleEvent(const sdl3::Event &ev);
	void RegisterCommands(ConsoleModel &console);

	// ── Document ────────────────────────────────────────────────────────────
	CommandResult Open(const String &textureId);
	CommandResult New(int width, int height, const String &id);
	CommandResult Import(const String &path);
	CommandResult Export(const String &path, int size);
	/// Écrit dans le mod (fichier de la texture, ou « textures/<nom>.png »),
	/// relit la texture et reconstruit l'atlas.
	CommandResult Save();
	CommandResult SaveProject(const String &dir);
	CommandResult LoadProject(const String &dir);
	CommandResult Undo();
	CommandResult Redo();

	// ── Peinture (coordonnées de la toile) ──────────────────────────────────
	CommandResult SetTool(PaintTool tool);
	/// Le geste de l'outil courant de `a` à `b` (un clic : a == b).
	CommandResult Apply(int ax, int ay, int bx, int by);

	// ── Atlas et variantes ──────────────────────────────────────────────────
	CommandResult RebuildAtlas(int tileSize, int maxTileSize);
	/// "copy" : l'image courante ; "noise" : bruit entre les couleurs du pinceau.
	CommandResult AddVariant(const String &kind);
	CommandResult RemoveVariant(size_t index);

	[[nodiscard]] TextureDocument *Document() noexcept { return m_doc.IsSome() ? &m_doc.Value() : nullptr; }
	[[nodiscard]] const String &TextureId() const noexcept { return m_textureId; }
	[[nodiscard]] PaintTool Tool() const noexcept { return m_tool; }
	/// Variantes de la texture ouverte (ids), dans l'ordre du jeu ; vide : aucune.
	[[nodiscard]] std::vector<String> Variants() const;
	int renderSize = 16;           ///< résolution de rendu des aperçus
	int fillTolerance = 0, fillAlpha = 128;
	bool shapeFilled = false;
	std::function<void()> onDocumentOpened{};

private:
	[[nodiscard]] voxel::Game *Game();
	CommandResult Opened(TextureDocument doc, const String &id, const String &path, const char *feature);
	void Visit(const char *feature, FeatureStatus status = FeatureStatus::Ok, String detail = String());
	void RebuildCatalog();
	void RebuildLayers();
	void RebuildPalette();
	void RebuildAtlasInfo();
	void RebuildVariants();
	void RefreshHeader();
	/// Toile, mipmaps, comparaison, bloc témoin : textures du renderer de la fenêtre.
	void DrawCanvas(ui::Painter &painter, const sdl3::FRect &rect);
	void DrawPreviews(ui::Painter &painter, const sdl3::FRect &rect);
	void DrawVariants(ui::Painter &painter, const sdl3::FRect &rect);
	sdl3::Texture *Upload(sdl3::Renderer &renderer, const String &key, const voxel::Image &image);
	[[nodiscard]] Option<std::array<int, 2>> PixelAt(float x, float y) const;
	/// Fichier où enregistrer la texture `id` (celui de sa définition s'il est
	/// une image, sinon <mod>/textures/<nom>.png) ; vide : mod introuvable.
	[[nodiscard]] String TargetPath(const String &id) const;
	CommandResult Reload(const String &id, const String &path, bool newDefinition);

	Shell &m_shell;
	StudioReport *m_report;
	Option<TextureDocument> m_doc = NONE;
	String m_textureId, m_path;
	PaintTool m_tool = PaintTool::PENCIL;
	uint64_t m_shownVersion = ~0ull, m_previewVersion = ~0ull;
	float m_previewTimer = 0.f, m_catalogTimer = 0.f;

	// Toile : vue et geste en cours.
	sdl3::FRect m_canvasRect{}, m_imageRect{};
	float m_zoom = 0.f; ///< pixels d'écran par pixel ; 0 : ajusté
	bool m_dragging = false;
	std::array<int, 2> m_dragStart{0, 0}, m_dragNow{0, 0};

	// Textures du renderer (par clé) et images d'aperçu.
	struct Uploaded {
		sdl3::Texture texture;
		int width = 0, height = 0;
		uint64_t version = 0;
	};
	std::vector<std::pair<String, std::unique_ptr<Uploaded>>> m_uploads;
	std::vector<voxel::Image> m_mips;
	voxel::Image m_lit, m_glow;
	std::vector<voxel::Image> m_variantImages;
	uint64_t m_uploadSerial = 0;

	ui::Widget m_tabs, m_title, m_status, m_catalog, m_catalogFilter, m_layers, m_palette, m_atlasInfo, m_atlasTiles, m_variantList, m_ioPath,
		m_tileSize, m_maxTile, m_brushSize, m_color;
	String m_filter;
	Rgba m_shownColor{0, 0, 0, 255};
};

} // namespace studio
} // namespace voxelia
