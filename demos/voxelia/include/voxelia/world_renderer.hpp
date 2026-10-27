#pragma once
/**
 * WorldRenderer — l'affichage du monde voxel avec render3d.
 *
 *  - Atlas des textures du contenu → une texture GPU (filtrage au plus
 *    proche), un matériau opaque (le « cutout » des feuillages vient du
 *    shader) et un matériau translucide (mélangé, vu des deux côtés).
 *  - Chunks : maillés sur le CPU (voxel::ChunkMesher), reconstruits quand
 *    leur révision change — les plus proches d'abord, quelques-uns par image
 *    (pas d'à-coup quand on casse un bloc ou quand le terrain arrive).
 *  - Entités : une forme par entité visible (cube, nodebox, croix, modèle,
 *    bloc en miniature), maillages partagés par forme, placés à la position
 *    interpolée entre deux pas de physique.
 *  - Contour du bloc visé, ciel et éclairage selon l'heure du jeu et la
 *    dimension (ciel en cubemap, de jour et de nuit ; heure figée, lumière
 *    ambiante, nuages).
 *  - Changement de dimension : chunks et entités repartent de zéro.
 */
#include <map>
#include <memory>
#include <unordered_map>

#include <render3d/canvas.hpp>
#include <render3d/cubemap.hpp>
#include <render3d/object3d.hpp>
#include <render3d/shape.hpp>
#include <voxel/game.hpp>
#include <voxel/meshing.hpp>
#include <voxel/rig.hpp>

namespace voxelia {

/// Réglages graphiques appliqués au rendu du monde.
struct WorldRenderSettings {
	int shadows = 1;            ///< 0 aucune, 1 ombres des entités, 2 + ombrage marqué des faces
	int dynamicLights = 8;      ///< lumières ponctuelles des blocs lumineux proches (nombre fixe)
	bool ambientOcclusion = true;
	bool clouds = true;
	float vegetationDensity = 1.f;
	float entityDistance = 64.f;
	float brightness = 0.5f;
	int particles = 2;
};

class WorldRenderer {
public:
	WorldRenderer(voxel::Game &game, render3d::Canvas &canvas);
	~WorldRenderer();

	/// Atlas et matériaux (après le chargement des mods).
	Result<bool, String> Initialize();
	[[nodiscard]] const voxel::TextureAtlas &Atlas() const noexcept { return m_atlas; }

	/// Une image : chunks à (re)construire, entités, contour, ciel.
	/// `alpha` : part du pas fixe écoulée (interpolation des positions).
	void Update(const render3d::Camera &camera, float alpha, const Option<voxel::BlockHit> &target);

	[[nodiscard]] render3d::Object3D &Root() noexcept { return m_root; }
	[[nodiscard]] size_t ChunkMeshCount() const noexcept { return m_chunks.size(); }
	[[nodiscard]] size_t PendingChunks() const noexcept { return m_pending; }
	[[nodiscard]] size_t TriangleCount() const noexcept { return m_triangles; }
	/// Budget de reconstruction (chunks par image).
	size_t rebuildBudget = 6;

	/// Applique les réglages (l'occlusion et la végétation font remailler).
	void Configure(const WorldRenderSettings &settings);
	/// Éclats d'un bloc cassé (petits cubes texturés qui retombent).
	void SpawnBreakParticles(voxel::BlockId block, const voxel::BlockPos &pos, int count);

private:
	struct ChunkView {
		render3d::Shape *opaque = nullptr, *translucent = nullptr, *emissive = nullptr;
		uint64_t revision = 0;
	};
	struct Particle {
		render3d::Shape *shape = nullptr;
		math::FVector3 position, velocity;
		float life = 1.f, spin = 0.f;
	};
	struct EntityView {
		render3d::Shape *shape = nullptr;   ///< forme simple (cube, nodebox, bloc)
		render3d::Object3D *rigRoot = nullptr; ///< modèle articulé
		std::vector<render3d::Object3D *> parts;
		std::vector<render3d::Shape *> partShapes; ///< les mêmes, pour leur lumière
		std::shared_ptr<const voxel::RigModel> rig;
		std::string meshKey;
		float walkPhase = 0.f;
		render3d::Shape *shadow = nullptr; ///< disque sous l'entité
	};
	/// Skin chargé sur le GPU (une texture et son matériau par image).
	struct Skin {
		Option<sdl3::GpuTexture> texture = NONE;
		render3d::Material material;
		int width = 0, height = 0;
	};

	const Skin *SkinFor(const String &skin, bool translucent);
	void UpdateRig(ecs::Entity e, EntityView &view, const voxel::Transform &t, const voxel::Visual &visual, const math::FVector3 &at,
				   float dt);
	void DropView(EntityView &view);

	void RebuildChunk(const voxel::ChunkPos &p, ChunkView &view);
	void UpdateEntities(float alpha);
	void UpdateSky();
	/// Cubemap d'une image « en croix » (4 × 3 faces), construite au premier besoin.
	const sdl3::GpuTexture *Skybox(const String &texture);
	void UpdateLights(const render3d::Camera &camera);
	void UpdateClouds(const render3d::Camera &camera);
	void UpdateParticles(float dt);
	/// Boîtes surlignées par les mods (Game::Highlights) : arêtes d'épaisseur
	/// constante, refaites quand la liste change.
	void UpdateHighlights();
	/// Lumière reçue par un objet mobile en `p` (Material::objectLight) :
	/// éclairement des sources (rgb, linéaire) et part de ciel (a).
	[[nodiscard]] math::FVector4 LightAt(const math::FVector3 &p) const;
	void UpdateShadow(EntityView &view, const math::FVector3 &feet, float width, bool visible);
	[[nodiscard]] std::shared_ptr<render3d::Mesh> EntityMesh(const voxel::Visual &visual, const String &mod, std::string &key);
	[[nodiscard]] static render3d::Mesh ToMesh(const voxel::MeshBuffer &buffer);

	voxel::Game &m_game;
	render3d::Canvas &m_canvas;
	render3d::Object3D m_root;
	render3d::Object3D *m_chunkRoot = nullptr, *m_entityRoot = nullptr;
	render3d::Shape *m_outline = nullptr;
	render3d::Shape *m_highlightShape = nullptr;
	std::vector<voxel::Game::Highlight> m_shownHighlights;
	/// Chunks pas encore maillés qui attendent leurs voisins (images d'attente).
	std::unordered_map<voxel::ChunkPos, int> m_waiting;
	voxel::TextureAtlas m_atlas;
	std::unique_ptr<voxel::ChunkMesher> m_mesher;
	Option<sdl3::GpuTexture> m_atlasTexture = NONE;
	render3d::Material m_opaque, m_translucent, m_entity;
	render3d::Material m_emissive; ///< blocs lumineux : sans éclairage (pleine luminosité)
	std::map<voxel::ChunkPos, ChunkView> m_chunks;
	std::unordered_map<uint64_t, EntityView> m_entities;
	std::unordered_map<std::string, std::shared_ptr<render3d::Mesh>> m_meshCache;
	std::unordered_map<std::string, std::unique_ptr<Skin>> m_skins;
	WorldRenderSettings m_settings;
	uint64_t m_worldSerial = 0; ///< monde affiché (Game::WorldSerial) : un autre → tout est refait
	/// Ciels (cubemaps) déjà envoyés au GPU, par id de texture ; nullptr : image inutilisable.
	std::unordered_map<std::string, std::unique_ptr<sdl3::GpuTexture>> m_skyboxes;
	bool m_skyboxActive = false;
	std::vector<Particle> m_particles;
	render3d::Object3D *m_cloudRoot = nullptr, *m_effectRoot = nullptr;
	std::vector<render3d::Shape *> m_cloudTiles;
	Option<sdl3::GpuTexture> m_cloudTexture = NONE;
	render3d::Material m_cloudMaterial, m_shadowMaterial;
	std::shared_ptr<render3d::Mesh> m_shadowMesh;
	float m_cloudOffset = 0.f;
	float m_frameDt = 0.f;
	math::FVector3 m_lastCamera{0.f, 0.f, 0.f};
	double m_lastTime = -1.0;
	size_t m_pending = 0, m_triangles = 0;
};

} // namespace voxelia
