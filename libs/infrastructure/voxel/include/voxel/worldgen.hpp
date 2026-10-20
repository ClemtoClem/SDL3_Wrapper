#pragma once
/**
 * Génération procédurale pilotée par les définitions (GeneratorDef,
 * BiomeDef, StructureDef).
 *
 *  - "flat"   : couches (bloc, épaisseur) empilées depuis y = 0.
 *  - "noise"  : deux bruits climatiques (température, humidité) désignent
 *               le biome de chaque colonne ; la hauteur est le mélange
 *               pondéré des hauteurs des biomes voisins dans le diagramme
 *               climatique (pas de falaise aux frontières) ; surface,
 *               sous-sol et remplissage viennent du biome ; eau sous le
 *               niveau de la mer ; grottes (bruit 3D) ; minerais
 *               ("ores" du générateur) ; décorations et structures des
 *               biomes, déterministes par colonne.
 *  - "script" : la fonction du mod remplit le chunk (cf. ScriptHost).
 *
 * Tout est déterministe : même graine, mêmes chunks, dans n'importe quel
 * ordre de génération.
 */
#include <functional>
#include <memory>

#include <generators/noise.hpp>

#include "world.hpp"

namespace voxel {

/// Hasard déterministe d'une position (splitmix64).
[[nodiscard]] uint64_t HashPosition(uint32_t seed, int x, int y, int z, uint32_t salt = 0) noexcept;
/// Même chose dans [0, 1[.
[[nodiscard]] float RandomAt(uint32_t seed, int x, int y, int z, uint32_t salt = 0) noexcept;

class TerrainGenerator final : public ChunkGenerator {
public:
	/// Fonction de génération d'un mod ("script") : remplit le chunk.
	using ScriptFn = std::function<void(Chunk &chunk, World &world, const GeneratorDef &def)>;

	TerrainGenerator(const ContentRegistry &content, const GeneratorDef &def, uint32_t seed, ScriptFn script = {});

	void Generate(Chunk &chunk, World &world) override;
	[[nodiscard]] int SurfaceHeight(int x, int z) const override;
	[[nodiscard]] const BiomeDef *BiomeAt(int x, int z) const override;

	[[nodiscard]] const GeneratorDef &Definition() const noexcept { return m_def; }

private:
	struct Column {
		int height;
		const BiomeDef *biome;
	};
	struct Ore {
		BlockId block, in;
		float chance;
		int minY, maxY, size;
	};
	[[nodiscard]] Column ColumnAt(int x, int z) const;
	void GenerateFlat(Chunk &chunk);
	void GenerateNoise(Chunk &chunk, World &world);
	void Decorate(Chunk &chunk, World &world, int x, int z, const Column &column);

	const ContentRegistry &m_content;
	GeneratorDef m_def;
	uint32_t m_seed;
	ScriptFn m_script;
	std::vector<const BiomeDef *> m_biomes;
	std::vector<Ore> m_ores;
	generators::Noise m_height, m_detail, m_temperature, m_humidity, m_caves;
	BlockId m_fluid = AIR, m_bedrock = AIR;
	int m_bedrockLevel = -64;
};

/// Générateur du monde : le GeneratorDef demandé (ou le premier), sinon un
/// monde plat de secours.
[[nodiscard]] std::shared_ptr<TerrainGenerator> MakeGenerator(const ContentRegistry &content, const String &generatorId,
															  uint32_t seed, TerrainGenerator::ScriptFn script = {});

} // namespace voxel
