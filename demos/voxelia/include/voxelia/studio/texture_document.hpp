#pragma once
/**
 * Document de l'atelier de textures — une image peinte en calques, à la
 * manière d'Aseprite :
 *
 *  - source jusqu'à MAX_SOURCE px de côté (gardée telle quelle), rendu choisi
 *    jusqu'à MAX_RENDER px : Rendered / MipChain / Glow passent par les
 *    fonctions de voxel::TextureAtlas (Resample, Glow) — ce que l'atlas du
 *    jeu fera de la texture ;
 *  - calques : visibilité, opacité, mode de fusion (normal, multiply, screen,
 *    overlay), nom, groupe, ordre (le premier est au fond) ;
 *  - pinceau : couleur RGBA, taille 1..N (carré jusqu'à 2, disque au-delà),
 *    opacité, mode de fusion, symétrie H / V / HV ; peinture limitée à la
 *    sélection rectangulaire si elle existe ;
 *  - outils : point / trait (Dab, StrokeTo), gomme, pipette (palette des
 *    couleurs récentes), remplissage par zone (tolérance + seuil alpha),
 *    ligne, rectangle, ellipse (contour ou plein), déplacement du calque ou
 *    de la sélection, flou léger, netteté, génération procédurale (bruit,
 *    dégradé, briques, damier) ;
 *  - annulation PAR TRAIT : BeginStroke / EndStroke encadrent un geste (un
 *    glissé du crayon = une étape) ; chaque autre opération en est une ;
 *    64 étapes au plus et MAX_UNDO_BYTES en tout (la dernière toujours gardée).
 *
 * Modèle sans fenêtre (couleurs non prémultipliées, sRGB comme les fichiers).
 */
#include <array>
#include <deque>
#include <vector>

#include <voxel/assets.hpp>

namespace voxelia::studio {

using Rgba = std::array<uint8_t, 4>;

enum class PaintBlend : uint8_t { NORMAL, MULTIPLY, SCREEN, OVERLAY };
enum class PaintSymmetry : uint8_t { NONE, H, V, HV };

[[nodiscard]] const char *PaintBlendId(PaintBlend mode) noexcept; ///< "normal", "multiply", "screen", "overlay"
[[nodiscard]] Option<PaintBlend> PaintBlendOf(const String &id) noexcept;

struct PaintLayer {
	String name, group;
	voxel::Image pixels;
	bool visible = true;
	float opacity = 1.f;
	PaintBlend blend = PaintBlend::NORMAL;
};

struct PaintRect {
	int x = 0, y = 0, w = 0, h = 0;
	[[nodiscard]] bool Empty() const noexcept { return w <= 0 || h <= 0; }
	[[nodiscard]] bool Contains(int px, int py) const noexcept { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct PaintBrush {
	Rgba color{0, 0, 0, 255};
	Rgba secondary{255, 255, 255, 255}; ///< seconde couleur (dégradés, motifs)
	int size = 1;
	float opacity = 1.f;
	PaintBlend blend = PaintBlend::NORMAL;
	PaintSymmetry symmetry = PaintSymmetry::NONE;
};

class TextureDocument {
public:
	static constexpr int MAX_SOURCE = 2048, MAX_RENDER = 256;
	static constexpr size_t MAX_UNDO = 64;
	static constexpr size_t MAX_UNDO_BYTES = size_t(256) << 20;
	static constexpr size_t MAX_PALETTE = 24;

	TextureDocument() = default;
	/// Toile transparente (1..MAX_SOURCE px de côté).
	[[nodiscard]] static Result<TextureDocument, String> Create(int width, int height, const String &name);
	/// Un calque fait de `image`.
	[[nodiscard]] static Result<TextureDocument, String> FromImage(voxel::Image image, const String &name);

	[[nodiscard]] int Width() const noexcept { return m_width; }
	[[nodiscard]] int Height() const noexcept { return m_height; }
	[[nodiscard]] const String &Name() const noexcept { return m_name; }
	void SetName(String name) { m_name = std::move(name); }
	[[nodiscard]] bool Dirty() const noexcept { return m_dirty; }
	void MarkSaved() noexcept { m_dirty = false; }
	[[nodiscard]] uint64_t Version() const noexcept { return m_version; }

	PaintBrush brush;

	// ── Calques ─────────────────────────────────────────────────────────────
	[[nodiscard]] const std::vector<PaintLayer> &Layers() const noexcept { return m_layers; }
	[[nodiscard]] int Active() const noexcept { return m_active; }
	Result<bool, String> SetActive(int index);
	/// Calque transparent au-dessus de l'actif, qui devient actif.
	void AddLayer(const String &name);
	Result<bool, String> DuplicateLayer(int index);
	Result<bool, String> RemoveLayer(int index); ///< un calque reste toujours
	Result<bool, String> MoveLayer(int from, int to);
	Result<bool, String> SetLayerVisible(int index, bool visible);
	Result<bool, String> SetLayerOpacity(int index, float opacity);
	Result<bool, String> SetLayerBlend(int index, PaintBlend blend);
	Result<bool, String> RenameLayer(int index, const String &name);
	Result<bool, String> SetLayerGroup(int index, const String &group);
	/// Fusionne le calque avec celui du dessous (son mode et son opacité appliqués).
	Result<bool, String> MergeDown(int index);

	// ── Sélection ───────────────────────────────────────────────────────────
	[[nodiscard]] const PaintRect &Selection() const noexcept { return m_selection; }
	/// Rectangle borné à la toile ; vide : aucune sélection.
	void Select(PaintRect rect);

	// ── Peinture (calque actif) ─────────────────────────────────────────────
	/// Un geste = une étape d'annulation, quel que soit le nombre de touches.
	void BeginStroke();
	void EndStroke();
	[[nodiscard]] bool InStroke() const noexcept { return m_strokeDepth > 0; }
	/// Touche du pinceau (ou de la gomme) en (x, y), symétries comprises.
	void Dab(int x, int y, bool erase = false);
	/// Trait continu depuis la dernière touche du geste (ou un point seul).
	void StrokeTo(int x, int y, bool erase = false);
	void Line(int x0, int y0, int x1, int y1, bool erase = false);
	void Rectangle(int x0, int y0, int x1, int y1, bool filled);
	void Ellipse(int x0, int y0, int x1, int y1, bool filled);
	/// Remplissage de la zone 4-connexe de (x, y) : pixels dont l'alpha est du
	/// même côté de `alphaThreshold` et (s'ils sont visibles) dont aucune
	/// composante n'écarte de plus de `tolerance` de celle du départ. Rend le
	/// nombre de pixels peints.
	int Fill(int x, int y, int tolerance = 0, int alphaThreshold = 128);
	/// Couleur en (x, y) de l'image composée (ou du calque actif) ; elle
	/// devient la couleur du pinceau et entre dans la palette.
	Option<Rgba> Pick(int x, int y, bool composite = true);
	/// Décale le calque actif (ou le contenu de la sélection, qui suit).
	void Move(int dx, int dy);
	void Blur();    ///< flou 3 × 3 (sélection ou calque)
	void Sharpen(); ///< netteté (masque flou)
	void Clear();   ///< sélection ou calque vidés
	/// Remplit la sélection (ou le calque) : "noise" (bruit de valeur à
	/// octaves entre les deux couleurs du pinceau), "gradient" (vertical),
	/// "bricks", "checker".
	Result<bool, String> Generate(const String &kind, uint32_t seed);

	// ── Résultats ───────────────────────────────────────────────────────────
	[[nodiscard]] voxel::Image Composite() const;
	/// Carré `size` × `size` comme l'atlas le rangera (TextureAtlas::Resample).
	[[nodiscard]] voxel::Image Rendered(int size) const;
	/// Rendered(size) puis chaque moitié jusqu'à 1 px (mipmaps).
	[[nodiscard]] std::vector<voxel::Image> MipChain(int size) const;
	/// Variante « lueur » (TextureAtlas::Glow) du rendu.
	[[nodiscard]] voxel::Image Glow(int size) const;
	[[nodiscard]] const std::vector<Rgba> &Palette() const noexcept { return m_palette; }
	void RememberColor(const Rgba &color);

	// ── Annulation ──────────────────────────────────────────────────────────
	bool Undo();
	bool Redo();
	[[nodiscard]] size_t UndoDepth() const noexcept { return m_undo.size(); }
	[[nodiscard]] size_t RedoDepth() const noexcept { return m_redo.size(); }

	// ── Fichiers ────────────────────────────────────────────────────────────
	/// Image composée en PNG / JPG ; `size` > 0 : au rendu de cette taille.
	Result<bool, String> Export(const String &path, int size = 0) const;
	[[nodiscard]] static Result<TextureDocument, String> Import(const String &path);
	/// Projet en calques : `dir`/layers.json + un PNG par calque.
	Result<bool, String> SaveProject(const String &dir) const;
	[[nodiscard]] static Result<TextureDocument, String> LoadProject(const String &dir);

private:
	struct Snapshot {
		std::vector<PaintLayer> layers;
		int active = 0;
		PaintRect selection;
		[[nodiscard]] size_t Bytes() const;
	};
	[[nodiscard]] Snapshot Capture() const;
	void Restore(Snapshot s);
	void Commit(); ///< instantané avant une opération (rien pendant un geste)
	void Changed();
	[[nodiscard]] PaintLayer &Layer() { return m_layers[size_t(m_active)]; }
	[[nodiscard]] bool Paintable(int x, int y) const noexcept;
	/// Peint un pixel du calque actif (couleur, opacité, mode du pinceau).
	void Plot(int x, int y, const Rgba &color, float opacity, bool erase);
	void Stamp(int x, int y, bool erase); ///< un disque / carré du pinceau, sans symétrie
	[[nodiscard]] PaintRect Area() const noexcept; ///< sélection, ou toute la toile
	void Convolve(const float kernel[9]);

	int m_width = 0, m_height = 0;
	String m_name;
	std::vector<PaintLayer> m_layers;
	int m_active = 0;
	PaintRect m_selection;
	std::vector<Rgba> m_palette;
	std::deque<Snapshot> m_undo, m_redo;
	int m_strokeDepth = 0;
	bool m_strokeCommitted = false;
	Option<std::array<int, 2>> m_last = NONE; ///< dernière touche du geste
	bool m_dirty = false;
	uint64_t m_version = 0;
};

/// Pixel de `a` (blend) sur `b` : composition alpha non prémultipliée.
[[nodiscard]] Rgba BlendPixel(const Rgba &src, const Rgba &dst, PaintBlend mode, float opacity) noexcept;

} // namespace voxelia::studio
