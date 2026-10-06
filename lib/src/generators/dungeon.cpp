// Définitions de generators/dungeon.hpp
#include "generators/dungeon.hpp"

namespace generators {

// ── Rect ─────────────────────────────────────────────────────────────────────

bool Rect::Overlaps(const Rect &o, int margin) const noexcept {
	return x - margin < o.x + o.w && x + w + margin > o.x && y - margin < o.y + o.h && y + h + margin > o.y;
}

// ── Dungeon ──────────────────────────────────────────────────────────────────

std::vector<String> Dungeon::Rows() const {
	std::vector<String> rows;
	for (int y = 0; y < tiles.Height(); ++y) {
		String row;
		for (int x = 0; x < tiles.Width(); ++x)
			row.PushBack(TileChar(tiles.At(x, y)));
		rows.push_back(std::move(row));
	}
	return rows;
}

size_t Dungeon::Count(Tile t) const {
	return size_t(std::count(tiles.Cells().begin(), tiles.Cells().end(), t));
}

Grid<int> DistanceField(const Grid<Tile> &tiles, Cell start) {
	Grid<int> dist(tiles.Width(), tiles.Height(), -1);
	if (!IsWalkable(tiles.At(start.x, start.y)))
		return dist;
	std::deque<Cell> queue{start};
	dist.Set(start.x, start.y, 0);
	while (!queue.empty()) {
		const Cell c = queue.front();
		queue.pop_front();
		for (int d = 0; d < 4; ++d) {
			const int nx = c.x + DIR_X[d], ny = c.y + DIR_Y[d];
			if (tiles.Contains(nx, ny) && IsWalkable(tiles.At(nx, ny)) && dist.At(nx, ny) < 0) {
				dist.Set(nx, ny, dist.At(c.x, c.y) + 1);
				queue.push_back({nx, ny});
			}
		}
	}
	return dist;
}

std::pair<Grid<int>, int> Regions(const Grid<Tile> &tiles) {
	Grid<int> label(tiles.Width(), tiles.Height(), -1);
	int count = 0;
	for (int y = 0; y < tiles.Height(); ++y)
		for (int x = 0; x < tiles.Width(); ++x) {
			if (!IsWalkable(tiles.At(x, y)) || label.At(x, y) >= 0)
				continue;
			std::deque<Cell> queue{{x, y}};
			label.Set(x, y, count);
			while (!queue.empty()) {
				const Cell c = queue.front();
				queue.pop_front();
				for (int d = 0; d < 4; ++d) {
					const int nx = c.x + DIR_X[d], ny = c.y + DIR_Y[d];
					if (tiles.Contains(nx, ny) && IsWalkable(tiles.At(nx, ny)) && label.At(nx, ny) < 0) {
						label.Set(nx, ny, count);
						queue.push_back({nx, ny});
					}
				}
			}
			++count;
		}
	return {std::move(label), count};
}

void PlaceEntranceAndExit(Dungeon &d) {
	std::vector<Cell> candidates;
	if (!d.rooms.empty()) {
		for (const Rect &r : d.rooms)
			candidates.push_back({r.CenterX(), r.CenterY()});
	} else {
		for (int y = 0; y < d.tiles.Height(); ++y)
			for (int x = 0; x < d.tiles.Width(); ++x)
				if (IsWalkable(d.tiles.At(x, y)))
					candidates.push_back({x, y});
	}
	if (candidates.empty())
		return;
	auto farthest = [&](Cell from) {
		const Grid<int> dist = DistanceField(d.tiles, from);
		size_t best = 0;
		for (size_t i = 0; i < candidates.size(); ++i)
			if (dist.At(candidates[i].x, candidates[i].y, -1) > dist.At(candidates[best].x, candidates[best].y, -1))
				best = i;
		return best;
	};
	const size_t a = farthest(candidates.front());
	const size_t b = farthest(candidates[a]);
	d.entrance = candidates[a];
	d.exit = candidates[b];
	if (!d.rooms.empty()) {
		d.entranceRoom = int(a);
		d.exitRoom = int(b);
	}
	d.tiles.Set(d.entrance.x, d.entrance.y, Tile::ENTRANCE);
	d.tiles.Set(d.exit.x, d.exit.y, Tile::EXIT);
}

void CarveMaze(Grid<Tile> &tiles, int sx, int sy, Rng &rng, MazeAlgorithm algo, float wiggle, Tile floor,
		const Grid<int> *region) {
	auto open = [&](int x, int y) {
		return tiles.Contains(x, y) && x > 0 && y > 0 && x < tiles.Width() - 1 && y < tiles.Height() - 1 &&
			   tiles.At(x, y) == Tile::WALL && (!region || region->At(x, y) < 0);
	};
	if (!open(sx, sy))
		return;
	tiles.Set(sx, sy, floor);
	if (algo == MazeAlgorithm::BINARY_TREE) {
		for (int y = 1; y < tiles.Height() - 1; y += 2)
			for (int x = 1; x < tiles.Width() - 1; x += 2) {
				if (tiles.At(x, y) != Tile::WALL && !(x == sx && y == sy))
					continue;
				tiles.Set(x, y, floor);
				const bool north = y > 1, west = x > 1;
				if (north && (!west || rng.Chance(0.5)))
					tiles.Set(x, y - 1, floor);
				else if (west)
					tiles.Set(x - 1, y, floor);
			}
		return;
	}
	std::vector<Cell> frontier{{sx, sy}};
	int lastDir = -1;
	while (!frontier.empty()) {
		const size_t index = algo == MazeAlgorithm::PRIM ? size_t(rng.Int(0, int64_t(frontier.size()) - 1))
														 : frontier.size() - 1;
		const Cell c = frontier[index];
		std::vector<int> dirs;
		for (int d = 0; d < 4; ++d)
			if (open(c.x + DIR_X[d] * 2, c.y + DIR_Y[d] * 2))
				dirs.push_back(d);
		if (dirs.empty()) {
			frontier.erase(frontier.begin() + ptrdiff_t(index));
			lastDir = -1;
			continue;
		}
		int dir = dirs[size_t(rng.Int(0, int64_t(dirs.size()) - 1))];
		if (algo == MazeAlgorithm::BACKTRACKER && lastDir >= 0 &&
			std::find(dirs.begin(), dirs.end(), lastDir) != dirs.end() && !rng.Chance(wiggle))
			dir = lastDir;
		tiles.Set(c.x + DIR_X[dir], c.y + DIR_Y[dir], floor);
		tiles.Set(c.x + DIR_X[dir] * 2, c.y + DIR_Y[dir] * 2, floor);
		frontier.push_back({c.x + DIR_X[dir] * 2, c.y + DIR_Y[dir] * 2});
		lastDir = dir;
	}
}

Dungeon GenerateMaze(int width, int height, uint64_t seed, MazeAlgorithm algo, float wiggle) {
	width = std::max(5, width | 1);
	height = std::max(5, height | 1);
	Dungeon d;
	d.tiles = Grid<Tile>(width, height, Tile::WALL);
	Rng rng(seed);
	CarveMaze(d.tiles, 1, 1, rng, algo, wiggle, Tile::CORRIDOR);
	PlaceEntranceAndExit(d);
	return d;
}

RoomsAndMazesConfig FromConfig3D(const maze::Config3D &c, int width, int height, uint64_t seed) {
	RoomsAndMazesConfig out;
	out.width = width;
	out.height = height;
	out.roomAttempts = int(c.roomBaseNumber);
	out.roomSizeMin = int(std::min(c.roomSizeMin.x, c.roomSizeMin.y));
	out.roomSizeMax = int(std::max(c.roomSizeMax.x, c.roomSizeMax.y));
	out.wiggle = c.wiggleChance;
	out.extraConnectionChance = c.extraConnectionChance;
	out.deadendKeepChance = c.deadendChance;
	out.seed = seed;
	return out;
}

Dungeon GenerateRoomsAndMazes(RoomsAndMazesConfig c) {
	c.width = std::max(9, c.width | 1);
	c.height = std::max(9, c.height | 1);
	c.roomSizeMin = std::max(3, c.roomSizeMin | 1);
	c.roomSizeMax = std::max(c.roomSizeMin, c.roomSizeMax | 1);
	Rng rng(c.seed);
	Dungeon d;
	d.tiles = Grid<Tile>(c.width, c.height, Tile::WALL);
	// Région de chaque case (salle i, puis couloirs) pour les connecteurs.
	Grid<int> region(c.width, c.height, -1);
	int regions = 0;

	// 1. Salles aux coordonnées impaires, sans chevauchement.
	for (int attempt = 0; attempt < c.roomAttempts; ++attempt) {
		const int w = int(rng.Int(c.roomSizeMin / 2, c.roomSizeMax / 2)) * 2 + 1;
		const int h = int(rng.Int(c.roomSizeMin / 2, c.roomSizeMax / 2)) * 2 + 1;
		if (w >= c.width - 2 || h >= c.height - 2)
			continue;
		const int x = int(rng.Int(0, (c.width - w - 1) / 2)) * 2 + 1;
		const int y = int(rng.Int(0, (c.height - h - 1) / 2)) * 2 + 1;
		const Rect room{x, y, w, h};
		bool overlap = false;
		for (const Rect &other : d.rooms)
			if (room.Overlaps(other, 1)) {
				overlap = true;
				break;
			}
		if (overlap)
			continue;
		for (int yy = y; yy < y + h; ++yy)
			for (int xx = x; xx < x + w; ++xx) {
				d.tiles.Set(xx, yy, Tile::FLOOR);
				region.Set(xx, yy, regions);
			}
		d.rooms.push_back(room);
		++regions;
	}
	// 2. Labyrinthe dans chaque zone restante (une région par labyrinthe).
	for (int y = 1; y < c.height - 1; y += 2)
		for (int x = 1; x < c.width - 1; x += 2) {
			if (d.tiles.At(x, y) != Tile::WALL)
				continue;
			CarveMaze(d.tiles, x, y, rng, MazeAlgorithm::BACKTRACKER, c.wiggle, Tile::CORRIDOR, &region);
			// Étiquette de la nouvelle région : toutes les cases de couloir
			// encore sans région, atteignables depuis (x, y).
			std::deque<Cell> queue{{x, y}};
			region.Set(x, y, regions);
			while (!queue.empty()) {
				const Cell cell = queue.front();
				queue.pop_front();
				for (int dd = 0; dd < 4; ++dd) {
					const int nx = cell.x + DIR_X[dd], ny = cell.y + DIR_Y[dd];
					if (d.tiles.At(nx, ny) == Tile::CORRIDOR && region.At(nx, ny) < 0) {
						region.Set(nx, ny, regions);
						queue.push_back({nx, ny});
					}
				}
			}
			++regions;
		}
	// 3. Connecteurs : murs entre deux régions différentes ; on relie tout à
	// la région 0 (union-find), plus quelques portes en trop (boucles).
	struct Connector {
		Cell at;
		int a, b;
	};
	std::vector<Connector> connectors;
	for (int y = 1; y < c.height - 1; ++y)
		for (int x = 1; x < c.width - 1; ++x) {
			if (d.tiles.At(x, y) != Tile::WALL)
				continue;
			const int h1 = region.At(x - 1, y), h2 = region.At(x + 1, y);
			const int v1 = region.At(x, y - 1), v2 = region.At(x, y + 1);
			if (h1 >= 0 && h2 >= 0 && h1 != h2)
				connectors.push_back({{x, y}, h1, h2});
			else if (v1 >= 0 && v2 >= 0 && v1 != v2)
				connectors.push_back({{x, y}, v1, v2});
		}
	rng.Shuffle(connectors);
	std::vector<int> parent(size_t(std::max(1, regions)));
	for (size_t i = 0; i < parent.size(); ++i)
		parent[i] = int(i);
	auto find = [&](int r) {
		while (parent[size_t(r)] != r)
			r = parent[size_t(r)] = parent[size_t(parent[size_t(r)])];
		return r;
	};
	for (const Connector &k : connectors) {
		const int ra = find(k.a), rb = find(k.b);
		const bool joins = ra != rb;
		if (!joins && !rng.Chance(c.extraConnectionChance))
			continue;
		// Pas deux portes côte à côte.
		bool adjacent = false;
		for (const Cell &door : d.doors)
			if (std::abs(door.x - k.at.x) + std::abs(door.y - k.at.y) <= 1)
				adjacent = true;
		if (adjacent && !joins)
			continue;
		d.tiles.Set(k.at.x, k.at.y, Tile::DOOR);
		d.doors.push_back(k.at);
		if (joins)
			parent[size_t(ra)] = rb;
	}
	// 4. Culs-de-sac comblés. La décision de GARDER un cul-de-sac se prend
	// une fois, sur sa case terminale : gardé, tout le couloir qui y mène
	// reste (la case ne redevient jamais une impasse à combler).
	Grid<uint8_t> kept(c.width, c.height, 0);
	bool changed = true;
	while (changed) {
		changed = false;
		for (int y = 1; y < c.height - 1; ++y)
			for (int x = 1; x < c.width - 1; ++x) {
				const Tile t = d.tiles.At(x, y);
				if ((t != Tile::CORRIDOR && t != Tile::DOOR) || kept.At(x, y))
					continue;
				int exits = 0;
				for (int dd = 0; dd < 4; ++dd)
					exits += IsWalkable(d.tiles.At(x + DIR_X[dd], y + DIR_Y[dd])) ? 1 : 0;
				if (exits > 1)
					continue;
				if (rng.Chance(c.deadendKeepChance)) {
					kept.Set(x, y, 1);
					continue;
				}
				d.tiles.Set(x, y, Tile::WALL);
				changed = true;
			}
	}
	d.doors.erase(std::remove_if(d.doors.begin(), d.doors.end(),
								 [&](const Cell &door) { return d.tiles.At(door.x, door.y) != Tile::DOOR; }),
				  d.doors.end());
	PlaceEntranceAndExit(d);
	return d;
}

Dungeon GenerateBsp(const BspConfig &c) {
	Dungeon d;
	d.tiles = Grid<Tile>(std::max(8, c.width), std::max(8, c.height), Tile::WALL);
	Rng rng(c.seed);
	auto carveRect = [&](const Rect &r, Tile t) {
		for (int y = r.y; y < r.y + r.h; ++y)
			for (int x = r.x; x < r.x + r.w; ++x)
				if (d.tiles.At(x, y) == Tile::WALL || t == Tile::FLOOR)
					d.tiles.Set(x, y, t);
	};
	auto corridor = [&](Cell a, Cell b) {
		// En L, coude aléatoire.
		const bool horizontalFirst = rng.Chance(0.5);
		Cell mid = horizontalFirst ? Cell{b.x, a.y} : Cell{a.x, b.y};
		auto line = [&](Cell from, Cell to) {
			int x = from.x, y = from.y;
			while (x != to.x || y != to.y) {
				if (d.tiles.At(x, y) == Tile::WALL)
					d.tiles.Set(x, y, Tile::CORRIDOR);
				x += (to.x > x) - (to.x < x);
				y += (to.y > y) - (to.y < y);
			}
			if (d.tiles.At(x, y) == Tile::WALL)
				d.tiles.Set(x, y, Tile::CORRIDOR);
		};
		line(a, mid);
		line(mid, b);
	};
	// Récursion explicite (pile) : chaque nœud rend le centre d'une de ses salles.
	std::function<Cell(Rect, int)> split = [&](Rect area, int depth) -> Cell {
		const bool canH = area.h >= c.minLeaf * 2, canW = area.w >= c.minLeaf * 2;
		if (depth > 12 || (!canH && !canW)) {
			const int maxW = std::max(3, area.w - 2 * c.roomMargin), maxH = std::max(3, area.h - 2 * c.roomMargin);
			const int w = int(rng.Int(std::max(3, int(float(maxW) * c.fillMin)), maxW));
			const int h = int(rng.Int(std::max(3, int(float(maxH) * c.fillMin)), maxH));
			const int x = area.x + c.roomMargin + int(rng.Int(0, std::max(0, maxW - w)));
			const int y = area.y + c.roomMargin + int(rng.Int(0, std::max(0, maxH - h)));
			const Rect room{std::max(1, x), std::max(1, y), std::min(w, d.tiles.Width() - 1 - std::max(1, x)),
							std::min(h, d.tiles.Height() - 1 - std::max(1, y))};
			carveRect(room, Tile::FLOOR);
			d.rooms.push_back(room);
			return {room.CenterX(), room.CenterY()};
		}
		const bool horizontal = canH && (!canW || (area.h > area.w ? true : area.w > area.h ? false : rng.Chance(0.5)));
		Rect a = area, b = area;
		if (horizontal) {
			const int cut = int(rng.Int(c.minLeaf, area.h - c.minLeaf));
			a.h = cut;
			b.y = area.y + cut;
			b.h = area.h - cut;
		} else {
			const int cut = int(rng.Int(c.minLeaf, area.w - c.minLeaf));
			a.w = cut;
			b.x = area.x + cut;
			b.w = area.w - cut;
		}
		const Cell ca = split(a, depth + 1), cb = split(b, depth + 1);
		corridor(ca, cb);
		return rng.Chance(0.5) ? ca : cb;
	};
	(void)split({0, 0, d.tiles.Width(), d.tiles.Height()}, 0);
	// Portes : couloir qui touche une salle.
	for (int y = 1; y < d.tiles.Height() - 1; ++y)
		for (int x = 1; x < d.tiles.Width() - 1; ++x) {
			if (d.tiles.At(x, y) != Tile::CORRIDOR)
				continue;
			int floors = 0;
			for (int dd = 0; dd < 4; ++dd)
				floors += d.tiles.At(x + DIR_X[dd], y + DIR_Y[dd]) == Tile::FLOOR ? 1 : 0;
			if (floors == 1) {
				d.tiles.Set(x, y, Tile::DOOR);
				d.doors.push_back({x, y});
			}
		}
	PlaceEntranceAndExit(d);
	return d;
}

Dungeon GenerateCaves(const CaveConfig &c) {
	Dungeon d;
	const int w = std::max(8, c.width), h = std::max(8, c.height);
	d.tiles = Grid<Tile>(w, h, Tile::WALL);
	Rng rng(c.seed);
	Grid<uint8_t> wall(w, h, 1);
	for (int y = 1; y < h - 1; ++y)
		for (int x = 1; x < w - 1; ++x)
			wall.Set(x, y, rng.Chance(c.fill) ? 1 : 0);
	for (int step = 0; step < c.steps; ++step) {
		Grid<uint8_t> next(w, h, 1);
		for (int y = 1; y < h - 1; ++y)
			for (int x = 1; x < w - 1; ++x) {
				int walls = 0;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
						if ((dx || dy) && wall.At(x + dx, y + dy, 1))
							++walls;
				next.Set(x, y, wall.At(x, y) ? (walls >= c.survival ? 1 : 0) : (walls >= c.birth ? 1 : 0));
			}
		wall = std::move(next);
	}
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			d.tiles.Set(x, y, wall.At(x, y) ? Tile::WALL : Tile::FLOOR);
	if (c.keepLargest) {
		auto [label, count] = Regions(d.tiles);
		std::vector<int> sizes(size_t(std::max(1, count)), 0);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				if (label.At(x, y) >= 0)
					++sizes[size_t(label.At(x, y))];
		const int largest = int(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				if (label.At(x, y) >= 0 && label.At(x, y) != largest)
					d.tiles.Set(x, y, Tile::WALL);
	}
	PlaceEntranceAndExit(d);
	return d;
}

} // namespace generators
