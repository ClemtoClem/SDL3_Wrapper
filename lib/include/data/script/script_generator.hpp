#pragma once
/**
 * data::script — la génération procédurale (generators::) vue depuis le
 * langage (« Sled »), dans l'espace de noms `gen`.
 *
 *   let bruit = gen.noise({type: "simplex", fractal: "ridged", octaves: 6, frequency: 0.01, seed: 7})
 *   let carte = gen.heightmap(128, 128).fill(bruit).normalize().terrace(6, 0.5)
 *   carte.erode({droplets: 20000}).save_pgm("relief.pgm", true)
 *   let terrain = gen.terrain({width: 129, height: 129, seed: 3, island: 2, sea_level: 0.3})
 *   let donjon = gen.dungeon({width: 41, height: 41, seed: 9})    # salles et labyrinthes
 *   for (ligne in donjon.rows()) { print(ligne) }
 *   let arbres = gen.poisson(100, 100, 4, 1, fn(x, y) { return carte.sample_uv(x / 100, y / 100) })
 *   let plante = gen.lsystem("X", {X: "F[+X][-X]FX", F: "FF"}, 5)
 *   let noms = gen.names(["aldoria", "belmora", "cendrial"], 2)
 *
 * Les objets (`gen.noise`, `gen.heightmap`, `gen.tilemap` — rendu par
 * gen.dungeon/maze/bsp/caves —, `gen.names`)
 * portent leur verrou : on peut générer dans une fonction `async` pendant
 * qu'un écran de chargement s'affiche. Options : tables `{clé: valeur}`,
 * toutes facultatives ; une clé inconnue est une erreur (faute de frappe).
 */
#include <memory>
#include <mutex>
#include <vector>

#include "../../generators/generators.hpp"
#include "script_std.hpp"

namespace data::script {

namespace genlib {

using lib::Args;
using lib::As;
using lib::Fail;
using lib::TypeBuilder;

// ── Lecture des options ─────────────────────────────────────────────────────

/// Options d'une table : lecture typée, et refus des clés inconnues.
class Options {
public:
	Options(const Value &v, const char *fn);
	[[nodiscard]] Option<Value> Get(const char *key);
	[[nodiscard]] double Number(const char *key, double fallback);
	[[nodiscard]] int Int(const char *key, int fallback) { return int(Number(key, double(fallback))); }
	[[nodiscard]] bool Bool(const char *key, bool fallback);
	[[nodiscard]] String Text(const char *key, const char *fallback);
	void Fault(String message);
	/// Erreur de lecture, ou clé inconnue (à appeler après toutes les lectures).
	[[nodiscard]] Option<ScriptError> Finish();

private:
	const char *m_fn;
	std::vector<std::pair<String, Value>> m_entries;
	std::vector<String> m_known;
	Option<ScriptError> m_error = NONE;
};

template <typename E> [[nodiscard]] Option<E> Named(const String &name, std::initializer_list<std::pair<const char *, E>> table) {
	for (const auto &[key, value] : table)
		if (name == key)
			return Some(value);
	return NONE;
}

/// Réglages de bruit depuis une table (cf. en-tête).
[[nodiscard]] Result<generators::NoiseSettings, ScriptError> ReadNoise(Options &o, const char *fn);

[[nodiscard]] Option<generators::BlendMode> ReadBlend(const String &name);

[[nodiscard]] generators::HydraulicErosion ReadErosion(Options &o);

[[nodiscard]] generators::ThermalErosion ReadThermal(Options &o);

// ── Objets ──────────────────────────────────────────────────────────────────

struct NoiseObject : HostObject {
	generators::Noise noise;
};

struct HeightMapObject : HostObject {
	mutable std::mutex mutex;
	generators::HeightMap map;
};

struct DungeonObject : HostObject {
	generators::Dungeon dungeon; // immuable une fois généré : aucune garde
};

struct NamesObject : HostObject {
	std::mutex mutex;
	generators::MarkovNames names{{}, 2};
	generators::Rng rng;
};

struct GenTypes {
	std::shared_ptr<const HostType> noise, heightmap, dungeon, names;
};
[[nodiscard]] GenTypes TypesOf(Interpreter &vm);

[[nodiscard]] Value MakeHeightMap(Interpreter &vm, generators::HeightMap map);

[[nodiscard]] const generators::Noise *AsNoise(const Value &v);
[[nodiscard]] HeightMapObject *AsMap(const Value &v);

[[nodiscard]] Value Pair(double a, double b);
[[nodiscard]] Value IntPair(int a, int b);
template <typename T> [[nodiscard]] Value NumberList(const std::vector<T> &values) {
	auto list = std::make_shared<ListObject>();
	list->items.reserve(values.size());
	for (T v : values) {
		if constexpr (std::is_floating_point_v<T>)
			list->items.push_back(Value::Number(double(v)));
		else
			list->items.push_back(Value::Int(int64_t(v)));
	}
	return Value::List(std::move(list));
}

void DefineNoise(TypeBuilder &builder);

void DefineHeightMap(TypeBuilder &builder);

// ── Donjons ─────────────────────────────────────────────────────────────────

[[nodiscard]] const char *TileName(generators::Tile t);

[[nodiscard]] Value MakeDungeon(Interpreter &vm, generators::Dungeon dungeon);

void DefineDungeon(TypeBuilder &builder);

void DefineNames(TypeBuilder &builder);

// ── Fonctions ───────────────────────────────────────────────────────────────

[[nodiscard]] Result<generators::TerrainSettings, ScriptError> ReadTerrain(Options &o);

void InstallFunctions(Interpreter &vm);

} // namespace genlib

/// Installe l'espace de noms `gen` (cf. en-tête).
void InstallGeneratorLibrary(Interpreter &vm);

} // namespace data::script
