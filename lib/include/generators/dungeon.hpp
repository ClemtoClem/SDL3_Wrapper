#pragma once
/**
 * generators — niveaux sur grille : labyrinthes, donjons, grottes.
 *
 *   - `GenerateMaze`   : labyrinthe PARFAIT (un seul chemin entre deux
 *                        cases) — retour sur trace (couloirs longs et
 *                        sinueux), Prim (beaucoup d'embranchements courts),
 *                        arbre binaire (biais diagonal, très rapide) ;
 *   - `GenerateRoomsAndMazes` : « Rooms and Mazes » (Bob Nystrom) — des
 *                        salles, un labyrinthe qui remplit le reste, des
 *                        portes qui relient tout, les culs-de-sac retirés.
 *                        C'est l'algorithme que décrit maze::Config3D ;
 *   - `GenerateBsp`    : partition binaire de l'espace — une salle par
 *                        feuille, des couloirs entre sœurs (donjon « rangé ») ;
 *   - `GenerateCaves`  : automate cellulaire (règle 4-5) — grottes
 *                        organiques, réduites à leur plus grande région.
 *
 * Chaque donjon désigne une ENTRÉE et une SORTIE : les deux salles (ou
 * cases) les plus éloignées à pied (parcours en largeur).
 */
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "../core/core.hpp"
#include "maze.hpp"
#include "random.hpp"

namespace generators {

enum class Tile : uint8_t { WALL, FLOOR, CORRIDOR, DOOR, ENTRANCE, EXIT };

/// Caractère d'une case pour l'affichage ASCII.
[[nodiscard]] constexpr char TileChar(Tile t) noexcept {
	switch (t) {
		case Tile::WALL:
			return '#';
		case Tile::FLOOR:
			return '.';
		case Tile::CORRIDOR:
			return ',';
		case Tile::DOOR:
			return '+';
		case Tile::ENTRANCE:
			return '<';
		case Tile::EXIT:
			return '>';
	}
	return '?';
}

[[nodiscard]] constexpr bool IsWalkable(Tile t) noexcept { return t != Tile::WALL; }

/// Grille 2D générique.
template <typename T> class Grid {
public:
	Grid() = default;
	Grid(int width, int height, T fill = T{})
		: m_width(std::max(1, width)), m_height(std::max(1, height)), m_cells(size_t(m_width) * size_t(m_height), fill) {}

	[[nodiscard]] int Width() const noexcept { return m_width; }
	[[nodiscard]] int Height() const noexcept { return m_height; }
	[[nodiscard]] bool Contains(int x, int y) const noexcept { return x >= 0 && y >= 0 && x < m_width && y < m_height; }
	[[nodiscard]] T At(int x, int y, T outside = T{}) const noexcept {
		return Contains(x, y) ? m_cells[size_t(y) * size_t(m_width) + size_t(x)] : outside;
	}
	void Set(int x, int y, T v) noexcept {
		if (Contains(x, y))
			m_cells[size_t(y) * size_t(m_width) + size_t(x)] = v;
	}
	void Fill(T v) { std::fill(m_cells.begin(), m_cells.end(), v); }
	[[nodiscard]] const std::vector<T> &Cells() const noexcept { return m_cells; }

private:
	int m_width = 1, m_height = 1;
	std::vector<T> m_cells = std::vector<T>(1);
};

struct Rect {
	int x = 0, y = 0, w = 0, h = 0;
	[[nodiscard]] int CenterX() const noexcept { return x + w / 2; }
	[[nodiscard]] int CenterY() const noexcept { return y + h / 2; }
	[[nodiscard]] bool Overlaps(const Rect &o, int margin = 0) const noexcept;
	[[nodiscard]] bool Contains(int px, int py) const noexcept { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct Cell {
	int x = 0, y = 0;
	[[nodiscard]] bool operator==(const Cell &o) const noexcept { return x == o.x && y == o.y; }
};

/// Résultat commun : la grille, les salles, les portes, l'entrée et la sortie.
struct Dungeon {
	Grid<Tile> tiles;
	std::vector<Rect> rooms;
	std::vector<Cell> doors;
	Cell entrance, exit;
	int entranceRoom = -1, exitRoom = -1;

	/// Une chaîne par ligne (cf. TileChar).
	[[nodiscard]] std::vector<String> Rows() const;
	[[nodiscard]] size_t Count(Tile t) const;
};

// ── Outils de grille ────────────────────────────────────────────────────────

inline constexpr int DIR_X[4] = {1, -1, 0, 0};
inline constexpr int DIR_Y[4] = {0, 0, 1, -1};

/// Distance à pied (4-voisinage) depuis `start` ; -1 : inaccessible.
[[nodiscard]] Grid<int> DistanceField(const Grid<Tile> &tiles, Cell start);

/// Régions connexes de cases praticables : étiquette par case (-1 : mur),
/// et nombre de régions.
[[nodiscard]] std::pair<Grid<int>, int> Regions(const Grid<Tile> &tiles);

/// Entrée et sortie : les deux points les plus éloignés à pied (double
/// parcours en largeur). Si des salles existent, on prend leurs centres.
void PlaceEntranceAndExit(Dungeon &d);

// ── Labyrinthes parfaits ────────────────────────────────────────────────────

enum class MazeAlgorithm : uint8_t { BACKTRACKER, PRIM, BINARY_TREE };

/// Creuse un labyrinthe dans les cases IMPAIRES de `tiles` (murs entre
/// elles), en partant de (sx, sy) impairs ; ne touche que des cases WALL.
/// `wiggle` : probabilité de changer de direction (retour sur trace).
void CarveMaze(Grid<Tile> &tiles, int sx, int sy, Rng &rng, MazeAlgorithm algo, float wiggle, Tile floor,
					  const Grid<int> *region = nullptr);

/// Labyrinthe parfait (largeur et hauteur forcées impaires).
[[nodiscard]] Dungeon GenerateMaze(int width, int height, uint64_t seed,
										  MazeAlgorithm algo = MazeAlgorithm::BACKTRACKER, float wiggle = 0.5f);

// ── Salles et labyrinthes (Nystrom) ─────────────────────────────────────────

struct RoomsAndMazesConfig {
	int width = 51, height = 51;   ///< forcés impairs
	int roomAttempts = 60;
	int roomSizeMin = 5, roomSizeMax = 11; ///< forcés impairs
	float wiggle = 0.35f;           ///< virages des couloirs
	float extraConnectionChance = 0.05f; ///< portes supplémentaires (boucles)
	float deadendKeepChance = 0.f;  ///< probabilité de GARDER un cul-de-sac
	uint64_t seed = 1;
};

/// Réglages depuis la configuration 3D historique du module (maze::Config3D).
[[nodiscard]] RoomsAndMazesConfig FromConfig3D(const maze::Config3D &c, int width, int height, uint64_t seed);

[[nodiscard]] Dungeon GenerateRoomsAndMazes(RoomsAndMazesConfig c);

// ── Partition binaire (BSP) ─────────────────────────────────────────────────

struct BspConfig {
	int width = 64, height = 48;
	int minLeaf = 10;       ///< plus petite zone découpée
	int roomMargin = 1;     ///< marge entre salle et bord de sa zone
	float fillMin = 0.55f;  ///< taille minimale de la salle dans sa zone
	uint64_t seed = 1;
};

[[nodiscard]] Dungeon GenerateBsp(const BspConfig &c);

// ── Grottes (automate cellulaire) ───────────────────────────────────────────

struct CaveConfig {
	int width = 64, height = 48;
	float fill = 0.45f;   ///< proportion initiale de murs
	int steps = 5;
	int birth = 5;        ///< un vide devient mur avec ≥ `birth` murs voisins (sur 8)
	int survival = 4;     ///< un mur reste mur avec ≥ `survival` murs voisins
	bool keepLargest = true;
	uint64_t seed = 1;
};

[[nodiscard]] Dungeon GenerateCaves(const CaveConfig &c);

} // namespace generators
