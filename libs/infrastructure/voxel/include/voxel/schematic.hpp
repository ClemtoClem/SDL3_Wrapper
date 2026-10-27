#pragma once
/**
 * Schematic — un morceau de monde copié (ou construit) : boîte de cellules,
 * chacune VIDE (rien n'est touché au collage), un bloc (air compris) et
 * son orientation, plus le contenu des rangements. Sert à copier / coller /
 * déplacer, aux annulations, aux formes et aux formules, et aux fichiers de
 * structures.
 *
 * Formats de fichier (choisis par l'extension) :
 *   .vxs.json  natif : palette d'identifiants TEXTUELS, cellules en plages
 *              répétées, orientations, contenu des rangements
 *   .json      définition de contenu d'un mod (« "type": "structure" » :
 *              blocs [x, y, z, id, orientation]) — à déposer dans un mod
 *   .we        WorldEdit (Minetest) : table Lua « 5:return {{x=…}} »
 *   .mts       schéma Minetest (binaire, données compressées zlib)
 * Les noms inconnus d'un fichier importé passent par des ALIAS (« default:
 * stone » → « core:stone ») : configuration « structures.aliases », puis
 * « mod:nom » → « <mod connu>:nom » ; un bloc introuvable devient vide et
 * est signalé.
 */
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "formula.hpp"
#include "world.hpp"

namespace voxel {

struct BlockPosLess {
	bool operator()(const BlockPos &a, const BlockPos &b) const noexcept {
		return a.y != b.y ? a.y < b.y : a.z != b.z ? a.z < b.z : a.x < b.x;
	}
};

class Schematic {
public:
	static constexpr uint16_t VOID = 0xFFFF; ///< cellule vide : rien n'est posé

	Schematic() = default;
	explicit Schematic(const BlockPos &size);

	/// Copie la boîte [a, b] (bornes incluses, dans n'importe quel ordre) ;
	/// `anchor` : point de la boîte qui sera posé au point de collage (par
	/// défaut le coin minimal). Chunks générés au besoin.
	static Schematic Capture(World &world, const BlockPos &a, const BlockPos &b, Option<BlockPos> anchor = NONE);

	[[nodiscard]] const BlockPos &Size() const noexcept { return m_size; }
	[[nodiscard]] int64_t Volume() const noexcept { return int64_t(m_size.x) * m_size.y * m_size.z; }
	[[nodiscard]] bool Empty() const noexcept { return Volume() == 0; }
	/// Cellules non vides (air compris) et blocs non-air.
	[[nodiscard]] int64_t Filled() const;
	[[nodiscard]] int64_t Solid() const;

	/// Position relative (0..size-1) du point posé au collage.
	BlockPos anchor{0, 0, 0};
	String name, author, description;

	/// Cellule (x, y, z dans la boîte) ; id vide : cellule vide.
	[[nodiscard]] String BlockAt(const BlockPos &p) const;
	[[nodiscard]] uint8_t ParamAt(const BlockPos &p) const;
	void Set(const BlockPos &p, const String &block, uint8_t param = 0);
	void Clear(const BlockPos &p);
	[[nodiscard]] const std::vector<String> &Palette() const noexcept { return m_palette; }
	/// Nombre de cellules par bloc.
	[[nodiscard]] std::map<String, int64_t> Counts() const;

	/// Contenu des rangements copiés (position relative → Inventory::ToJson).
	std::map<BlockPos, NodePtr, BlockPosLess> containers;

	struct PasteOptions {
		int rotation = 0;       ///< quarts de tour autour de Y (sens de RotateOffset)
		bool keepAir = true;    ///< faux : l'air copié ne remplace rien (fusion)
		bool onlyAir = false;   ///< ne pose qu'à la place de l'air (ne détruit rien)
	};
	/// Colle avec `anchor` au point `at` ; rend les cellules modifiées. Les
	/// rangements sont remplis par `containerFill` (Game) s'il est donné.
	int64_t Paste(World &world, const BlockPos &at, const PasteOptions &options,
				  const std::function<void(const BlockPos &, const NodePtr &)> &containerFill = {}) const;
	/// Boîte monde [min, max] que couvrirait le collage.
	[[nodiscard]] std::pair<BlockPos, BlockPos> Bounds(const BlockPos &at, int rotation = 0) const;

	/// Tourné de `quarters` quarts de tour autour de Y (orientations comprises).
	[[nodiscard]] Schematic Rotated(int quarters) const;
	/// Miroir selon un axe (0 : x, 1 : y, 2 : z).
	[[nodiscard]] Schematic Mirrored(int axis) const;
	/// Ne garde que l'enveloppe : cellules pleines dont un voisin (6) est vide.
	[[nodiscard]] Schematic Hollow() const;
	/// Remplace un bloc par un autre (vide : tous les blocs non-air).
	int64_t Replace(const String &from, const String &to);

	// ── Fichiers ────────────────────────────────────────────────────────────

	[[nodiscard]] NodePtr ToJson() const;
	[[nodiscard]] static Result<Schematic, String> FromJson(const NodePtr &json);
	/// Définition « "type": "structure" » d'un mod (id donné).
	[[nodiscard]] NodePtr ToContentJson(const String &id) const;
	[[nodiscard]] String ToWorldEdit() const;
	[[nodiscard]] static Result<Schematic, String> FromWorldEdit(const String &text);
	[[nodiscard]] std::vector<uint8_t> ToMts() const;
	[[nodiscard]] static Result<Schematic, String> FromMts(const std::vector<uint8_t> &bytes);

	/// Format d'après le nom (« vxs », « json », « we », « mts » ; vide : inconnu).
	[[nodiscard]] static String FormatOf(const String &file);
	/// Lit / écrit un fichier dans le format de son extension.
	[[nodiscard]] static Result<Schematic, String> Load(const String &path);
	Result<bool, String> Save(const String &path) const;

	/// Remplace les ids inconnus du contenu (alias, puis même nom dans un
	/// mod chargé) ; rend les noms restés inconnus (cellules vidées).
	std::vector<String> ResolveNames(const ContentRegistry &content, const NodePtr &aliases = nullptr);

private:
	[[nodiscard]] size_t Index(const BlockPos &p) const { return (size_t(p.y) * size_t(m_size.z) + size_t(p.z)) * size_t(m_size.x) + size_t(p.x); }
	[[nodiscard]] bool Inside(const BlockPos &p) const {
		return p.x >= 0 && p.y >= 0 && p.z >= 0 && p.x < m_size.x && p.y < m_size.y && p.z < m_size.z;
	}
	uint16_t PaletteIndex(const String &block);

	BlockPos m_size{0, 0, 0};
	std::vector<String> m_palette;
	std::vector<uint16_t> m_cells; ///< index de palette ou VOID ; ordre y, z, x
	std::vector<uint8_t> m_params; ///< vide si toutes nulles
};

// ── Formules ─────────────────────────────────────────────────────────────────

/// Construction d'après une formule (à la manière de mathplot) ; les
/// coordonnées de la formule sont relatives au point de collage.
struct PlotSpec {
	enum class Kind : uint8_t {
		IMPLICIT, ///< f(x, y, z) : condition (région pleine) ou expression (surface où elle s'annule)
		CURVE,    ///< (x(t), y(t), z(t)), t ∈ [t0, t1]
		SURFACE,  ///< (x(u, v), y(u, v), z(u, v))
		SOLID,    ///< (x(u, v, w), …)
	};
	Kind kind = Kind::IMPLICIT;
	String expression;           ///< IMPLICIT
	String fx, fy, fz;           ///< paramétriques
	BlockPos min{-8, -8, -8}, max{8, 8, 8}; ///< IMPLICIT : boîte explorée (bornes incluses)
	double range[3][2] = {{0.0, 1.0}, {0.0, 1.0}, {0.0, 1.0}}; ///< t / u, v, w : [début, fin]
	int samples[3] = {64, 32, 16};                            ///< points par paramètre (au moins)
	bool zUp = true;     ///< convention mathématique : z vers le haut (sinon y, comme le jeu)
	double scale = 1.0;  ///< unité de la formule en blocs
	String block;
	bool hollow = false; ///< ne garder que l'enveloppe
	BlockPos maxSize{64, 64, 64}; ///< refus au-delà (taille de la boîte résultante)
	double timeLimit = 10.0;       ///< secondes de calcul au plus
};

/// Schéma de la formule (anchor : l'origine de la formule).
[[nodiscard]] Result<Schematic, String> Plot(const PlotSpec &spec);

} // namespace voxel
