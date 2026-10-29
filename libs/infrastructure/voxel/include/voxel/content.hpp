#pragma once
/**
 * ContentRegistry — toutes les DÉFINITIONS du jeu, chargées depuis des
 * fichiers externes (JSON) ou déclarées par les scripts des mods (tables
 * converties en JSON : un seul chemin de lecture).
 *
 * Natures (champ "type") :
 *   block (ou directement la forme : "cube", "nodebox", "cross", "obj",
 *          "b3d", "mesh"), item, tool, creature, entity (entité
 *          interactive), recipe, biome, structure, generator, texture,
 *          model, sound, soundset, shader, behavior, config, dimension.
 *
 * Chaque définition garde son JSON d'origine (`raw`) : les propriétés que
 * le moteur ne connaît pas restent accessibles aux scripts (« propriétés
 * quelconques »). Les identifiants sont « mod:nom » ; un nom sans espace de
 * noms prend celui du mod qui le déclare.
 *
 * Versions de schéma : `schemaVersion` (0 si absent) est MIGRÉ jusqu'à
 * SCHEMA_VERSION par les fonctions enregistrées (RegisterMigration) avant
 * la lecture ; la migration 0 → 1 fournie convertit les anciens noms de
 * champs (drawtype, tiles, collision_box…).
 *
 * Après le chargement de tous les mods, `Validate()` vérifie les renvois
 * (textures, blocs des biomes, ingrédients des recettes…) et rend les
 * avertissements.
 */
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <core/core.hpp>
#include <dataio/node.hpp>

#include "coords.hpp"

namespace voxel {

using dataio::NodePtr;
using BlockId = uint16_t;
inline constexpr BlockId AIR = 0;
inline constexpr const char *AIR_ID = "core:air";
inline constexpr int SCHEMA_VERSION = 1;

enum class DefKind : uint8_t {
	BLOCK, ITEM, CREATURE, ENTITY, RECIPE, BIOME, STRUCTURE, GENERATOR, TEXTURE, MODEL, SOUND, SOUND_SET, SHADER, BEHAVIOR, CONFIG,
	DIMENSION,
};
[[nodiscard]] const char *DefKindName(DefKind kind) noexcept;

/// Rendu d'une surface : OPAQUE (cache ses voisins), CUTOUT (pixels
/// entièrement transparents : feuillages, grilles), TRANSLUCENT (verre,
/// glace), TRANSPARENT (eau : vue des deux côtés), INVISIBLE.
enum class RenderMaterial : uint8_t { OPAQUE, CUTOUT, TRANSLUCENT, TRANSPARENT, INVISIBLE };
enum class ShapeKind : uint8_t { NONE, CUBE, NODEBOX, CROSS, MODEL };
enum class CollisionKind : uint8_t { NONE, FULL_CUBE, BOXES };

/// Forme d'un bloc ou d'une entité. Boîtes en coordonnées LOCALES (bloc :
/// [0, 1]³ pour un bloc plein ; entité : centrée en x/z, base à y = 0).
struct ShapeDesc {
	ShapeKind kind = ShapeKind::CUBE;
	math::FVector3 size{1.f, 1.f, 1.f};
	std::vector<Box> boxes;
	String model; ///< id d'un ModelDef, ou chemin de fichier (obj/b3d) résolu
};

/**
 * Source de lumière d'un bloc ou d'une entité (« light » de sa définition).
 * Conventions (centralisées dans voxel/lighting.hpp) :
 *   - couleur : JSON en sRGB 0..255, stockée ici en RGB LINÉAIRE 0..1 ;
 *   - intensité : éclairement reçu à 1 bloc, face tournée vers la source
 *     (sans unité physique ; 1 ≈ « bien éclairé » avant le tone mapping) ;
 *   - distances en blocs (1 bloc = 1 m), angles en degrés dans le JSON ;
 *   - direction dans le repère du bloc à l'orientation 0, tournée avec lui.
 */
struct LightDef {
	enum class Type : uint8_t { POINT, SPOT, DIRECTIONAL };
	enum class Falloff : uint8_t { INVERSE_SQUARE, LINEAR, NONE };
	bool enabled = false;
	Type type = Type::POINT;
	std::array<float, 3> color{1.f, 1.f, 1.f};
	float intensity = 0.f;
	float range = 0.f; ///< au-delà, plus rien (fin progressive, pas de coupure)
	Falloff falloff = Falloff::INVERSE_SQUARE;
	math::FVector3 direction{0.f, -1.f, 0.f}; ///< spot, directionnelle (normée)
	float innerCos = 1.f, outerCos = 0.f;     ///< spot : cos des demi-angles du cône
	float radius = 0.5f;                      ///< directionnelle : rayon du faisceau (blocs)
	math::FVector3 offset{0.f, 0.f, 0.f};     ///< position dans la cellule (centre = 0)
	bool legacy = false;                      ///< déduite de « graphics.light » (niveau 0..15)
	[[nodiscard]] bool Active() const noexcept {
		return enabled && intensity > 0.f && range > 0.f && color[0] + color[1] + color[2] > 0.f;
	}
};

/// Transmission optique d'un bloc (« optics ») : ce que la lumière DIRECTE
/// garde en le traversant — indépendante de la collision (« physics.solid »).
struct OpticsDef {
	std::array<float, 3> transmittance{1.f, 1.f, 1.f}; ///< 0 : opaque, 1 : transparent (par canal, linéaire)
	/// Vrai : toute la cellule filtre ; faux : seules les boîtes visibles
	/// (portes, dalles, échelles…) arrêtent les rayons qui les touchent.
	bool fullCell = true;
	[[nodiscard]] bool Clear() const noexcept { return transmittance[0] >= 1.f && transmittance[1] >= 1.f && transmittance[2] >= 1.f; }
	[[nodiscard]] bool Opaque() const noexcept { return transmittance[0] <= 0.f && transmittance[1] <= 0.f && transmittance[2] <= 0.f; }
	[[nodiscard]] bool operator==(const OpticsDef &) const noexcept = default;
};

struct Appearance {
	/// Textures par face (ordre de Face : est, ouest, dessus, dessous, sud, nord).
	std::array<String, FACE_COUNT> faces;
	RenderMaterial material = RenderMaterial::OPAQUE;
	bool backfaceCulling = true;
	std::array<uint8_t, 4> tint{255, 255, 255, 255}; ///< couleur multipliée (RGBA)
	/// Teinte du biome multipliée en plus (« grass », « foliage », « water ») ;
	/// faces concernées (bit = Face), toutes par défaut.
	String biomeTint;
	uint8_t tintFaces = 0x3F;
	/// Niveau historique 0..15 (« graphics.light ») : décide de l'aspect
	/// lumineux (textures « lueur ») ; déduit de `light` s'il est donné.
	int lightEmission = 0;
	LightDef light; ///< la lumière réellement émise (cf. lighting.hpp)
	String shader;                                    ///< ShaderDef facultatif
	[[nodiscard]] const String &Texture(Face face) const { return faces[size_t(face)]; }
};

struct SoundSet {
	std::map<std::string, String> events; ///< "step", "place", "break", "hit", "idle", "hurt"…
	/// Jeu de sons partagé (SoundSetDef) dont les événements complètent ceux-ci.
	String group;
	[[nodiscard]] String Get(const char *event) const;
};

/// Écoulement d'un liquide (« liquid » d'un bloc fluide ; voir fluids.hpp).
struct LiquidProps {
	bool flows = false;        ///< faux : liquide immobile (« "liquid": false »)
	int range = 7;             ///< cellules parcourues depuis une source (1..15)
	float tick = 0.25f;        ///< secondes entre deux pas d'écoulement
	bool renewable = false;    ///< deux sources voisines en créent une troisième
	float current = 2.f;       ///< force du courant sur les entités
	String flowingTexture;     ///< faces d'une cellule qui coule (sinon celles du bloc)
	struct Mix {
		String with;    ///< autre liquide
		String source;  ///< bloc que devient une SOURCE à son contact
		String flowing; ///< … et une cellule qui coule
	};
	std::vector<Mix> mixes;
};

struct BlockDef {
	String id, mod, displayName;
	BlockId numeric = AIR;
	std::vector<String> tags;
	ShapeDesc shape;
	Appearance look;
	// Physique
	bool solid = true;
	CollisionKind collision = CollisionKind::FULL_CUBE;
	std::vector<Box> collisionBoxes; ///< locales au bloc (BOXES)
	float slipperiness = 0.f;        ///< 0 : adhérent, ~1 : glace
	bool walkable = true;
	bool climbable = false;
	float viscosity = 0.f;           ///< fluides : freinage des entités (0..1)
	/// Sables mouvants : une entité qui y entre s'enfonce à cette vitesse
	/// (blocs/s), presque sans pouvoir avancer ni sauter (0 : bloc ordinaire).
	float sinking = 0.f;
	/// Faux : le regard (visée, raycasts) traverse le bloc — champs, brumes.
	bool selectable = true;
	bool fluid = false;
	LiquidProps liquid;              ///< écoulement (fluides)
	bool replaceable = false;        ///< herbe haute, fluides : remplacés en posant un bloc
	bool requiresSupport = false;
	float supportStrength = 0.f;
	// Simulation
	float dropChance = 1.f;          ///< probabilité du butin (feuilles → pousse)
	bool falling = false;            ///< sable, gravier : tombe s'il n'est pas soutenu
	String fallingBehavior = "FALL_AS_BLOCK";
	// Gameplay
	float hardness = 1.f;            ///< secondes de minage à mains nues ; < 0 : incassable
	String preferredTool;            ///< type d'outil qui accélère le minage
	String drops;                    ///< objet laissé (le bloc lui-même par défaut, "" : rien)
	int dropCount = 1;
	SoundSet sounds;
	String behavior;                 ///< comportement scripté facultatif
	// Blocs en plusieurs cellules (porte : deux de haut, lit : deux de long),
	// posés et cassés ensemble. Décalages donnés pour l'orientation 0 (le
	// joueur regarde vers +z), tournés avec le bloc (RotateOffset).
	struct Part {
		BlockPos offset;
		String block;
	};
	std::vector<Part> parts;          ///< « parts » : autres cellules du bloc principal
	std::optional<BlockPos> partOf;   ///< « partOf » : position du bloc principal vue de cette partie
	OpticsDef optics; ///< transmission de la lumière directe (« optics », sinon déduite du matériau)
	// Clic droit (« interact »).
	String toggle;                   ///< bloc qui remplace celui-ci (porte ouverte ↔ fermée), orientation gardée
	String toggleSound;              ///< son joué en basculant VERS `toggle`
	String use;                      ///< action intégrée : « sleep » (lit), « container » (rangement)
	int containerSlots = 0;          ///< « container.slots » : emplacements d'un rangement
	// États (« states » : allumé / éteint…). Chaque état est un BlockDef à
	// part entière (textures, lumière, interaction propres) ; l'état par défaut
	// porte l'id du bloc, les autres « id_état » (ou leur « id »).
	String stateGroup;                               ///< id du bloc dont c'est un état (vide : sans états)
	String stateName;                                ///< nom de cet état
	std::vector<std::pair<String, String>> stateIds; ///< chaque état du bloc → son id
	/// « sensor » : change d'état quand un joueur entre dans `radius`
	/// (→ `nearBlock`) ou n'y est plus (→ `farBlock`).
	struct Sensor {
		float radius = 0.f;
		String nearBlock, farBlock;
	} sensor;
	NodePtr raw;

	[[nodiscard]] bool HasTag(const String &tag) const;
	[[nodiscard]] bool IsAir() const noexcept { return numeric == AIR; }
	[[nodiscard]] bool IsContainer() const noexcept { return containerSlots > 0; }
	[[nodiscard]] bool IsOpaqueCube() const noexcept {
		return shape.kind == ShapeKind::CUBE && look.material == RenderMaterial::OPAQUE && shape.size.x >= 1.f &&
			   shape.size.y >= 1.f && shape.size.z >= 1.f;
	}
};

struct ToolProps {
	String kind;          ///< "pickaxe", "axe", "shovel", "sword"…
	float speed = 1.f;    ///< multiplicateur de vitesse sur les blocs de ce type
	int durability = 0;   ///< 0 : inusable
	float damage = 1.f;
	int level = 0;
};

struct ItemDef {
	String id, mod, displayName;
	std::vector<String> tags;
	String icon;          ///< texture (par défaut celle du dessus du bloc posé)
	String placesBlock;   ///< bloc posé par cet objet
	int maxStack = 64;
	std::optional<ToolProps> tool;
	float food = 0.f;
	String onUse;         ///< fonction de script appelée à l'utilisation
	NodePtr raw;
	[[nodiscard]] bool HasTag(const String &tag) const;
};

struct BehaviorRef {
	String id;
	NodePtr params;
};

/// Créature ou entité interactive : forme, collision, rendu, physique et
/// sons quelconques, comportements composés.
/// Modèle articulé d'une entité (voir rig.hpp).
struct RigRef {
	String model;              ///< ModelDef (format "rig" ou maillage .obj)
	std::vector<String> skins; ///< textures possibles (une tirée au hasard)
	float scale = 1.f;
	float yaw = 0.f; ///< degrés : orientation du modèle (s'il ne regarde pas vers +z)
	[[nodiscard]] bool IsSet() const { return !model.IsEmpty(); }
};

struct EntityDef {
	String id, mod, kind, displayName;
	std::vector<String> tags;
	ShapeDesc shape;
	Appearance look;
	RigRef rig;
	math::FVector3 colliderSize{0.6f, 1.8f, 0.6f};
	float mass = 1.f, gravityScale = 1.f, friction = 0.6f, bounciness = 0.f, drag = 0.f;
	float maxSpeed = 4.f, jumpSpeed = 8.f, stepHeight = 0.6f;
	bool swims = true, flies = false, pushable = true, collidesWithEntities = true, persistent = true;
	float health = 10.f;
	std::vector<BehaviorRef> behaviors;
	SoundSet sounds;
	std::vector<std::pair<String, int>> drops;
	String onInteract;    ///< fonction de script (clic droit)
	NodePtr raw;
};

struct Ingredient {
	String item; ///< id d'objet, ou « #tag »
	int count = 1;
	[[nodiscard]] bool IsTag() const { return item.CStr()[0] == '#'; }
};

enum class RecipeKind : uint8_t { SHAPED, SHAPELESS, COOKING };

struct RecipeDef {
	String id, mod;
	RecipeKind kind = RecipeKind::SHAPELESS;
	std::vector<String> pattern;            ///< SHAPED : lignes de symboles
	std::map<char, Ingredient> key;         ///< symbole → ingrédient
	std::vector<Ingredient> ingredients;    ///< SHAPELESS / COOKING
	String result;
	int resultCount = 1;
	float time = 0.f;
	String station;      ///< bloc d'atelier requis à proximité (vide : défaut selon le type)
	String category;     ///< « tools », « building », « food »… (interface)
	int requiresLevel = 0; ///< niveau d'expérience pour la débloquer
	NodePtr raw;
};

struct StructureDef {
	String id, mod;
	std::vector<std::pair<BlockPos, String>> blocks; ///< position relative → bloc
	std::vector<uint8_t> params;                     ///< orientations (parallèle à `blocks`, vide : toutes nulles)
	BlockPos size{0, 0, 0};
	BlockPos anchor{0, 0, 0};                         ///< point posé sur le sol
	bool replaceSolid = false;
	NodePtr raw;
};

struct Decoration {
	String block;       ///< bloc posé au-dessus de la surface…
	String structure;   ///< …ou structure…
	/// …ou élément procédural du générateur (« giant_tree », « spire »,
	/// « patch »), réglé par `params` (cf. worldgen.hpp).
	String feature;
	NodePtr params;
	float chance = 0.f; ///< par colonne
	std::vector<String> onBlocks;
};

struct BiomeDef {
	String id, mod, displayName;
	/// Dimension propre du biome : un générateur sans liste « biomes » ne
	/// prend que les biomes sans dimension (le monde ordinaire).
	String dimension;
	float temperature = 0.5f, humidity = 0.5f; ///< position dans le diagramme climatique
	float heightBase = 64.f, heightVariation = 8.f, roughness = 0.5f;
	String surface, subsurface, filler, underwater;
	int subsurfaceDepth = 3;
	std::vector<Decoration> decorations;
	std::array<uint8_t, 3> skyColor{140, 190, 240}, fogColor{180, 210, 240};
	/// Teintes de la végétation et de l'eau ; absentes : lues dans les
	/// « colormaps » de la configuration selon température et humidité.
	std::optional<std::array<uint8_t, 3>> grassColor, foliageColor, waterColor;
	NodePtr raw;
};

struct GeneratorDef {
	String id, mod;
	String kind = "noise";   ///< "noise" (biomes + bruit), "flat", "script"
	int seaLevel = 62;
	String fluid, bedrock;
	float frequency = 0.008f;
	int octaves = 5;
	float caves = 0.f;       ///< densité de grottes (0 : aucune)
	std::vector<String> biomes; ///< vide : tous les biomes
	std::vector<std::pair<String, int>> layers; ///< "flat" : bloc, épaisseur (du bas vers le haut)
	String script;            ///< "script" : fonction appelée par chunk
	NodePtr raw;
};

/// Dimension : un monde à part au sein de la même sauvegarde (son
/// générateur, ses chunks, ses entités), généré quand on y entre puis chunk
/// par chunk autour du joueur. Un mod en ajoute avec `"type": "dimension"`.
struct DimensionDef {
	String id, mod, displayName, description;
	String generator;      ///< GeneratorDef ; vide : celui choisi à la création du monde
	// Ciel et lumière
	std::array<uint8_t, 3> skyColor{140, 190, 245}, fogColor{180, 210, 240};
	bool skyFromBiome = true; ///< ciel du biome (sinon `skyColor`)
	/// Ciel en cubemap : image « en croix » (4 × 3 faces : dessus, puis -X,
	/// +Z, +X, -Z, puis dessous), de jour et (facultatif) de nuit.
	String cubemap, nightCubemap;
	bool sun = true, clouds = true, stars = true;
	bool dayCycle = true;  ///< faux : heure figée à `fixedTime` (0..1)
	float fixedTime = 0.5f;
	float ambient = 1.f;   ///< multiplicateur de la lumière ambiante (du ciel)
	/// « light.base » : lumière uniforme, sans occlusion (0 : nuit noire
	/// possible). Absente d'un monde sans ciel : `ambient` en tient lieu
	/// (comportement historique).
	Option<float> baseLight;
	float exposure = 1.f; ///< « light.exposure » : exposition avant tone mapping
	/// Lumière du ciel par sommet (grottes et dessous des surplombs sombres) ;
	/// faux pour les îles flottantes et les mondes éclairés autrement.
	bool skyLight = true;
	float fogDistance = 0.f; ///< brume (blocs ; 0 : aucune)
	// Physique et apparition
	float gravity = 1.f;   ///< multiplicateur de gravité
	int spawnTop = 192, spawnBottom = -64; ///< bornes de recherche du point d'apparition
	/// Causes de mort qui y envoient le joueur au lieu de le faire réapparaître
	/// (« suffocation », « drowning », « fall », « void »…).
	std::vector<String> enterOnDeath;
	String arrivalPortal;  ///< bloc posé près du point d'arrivée (portail de retour)
	String music;          ///< ambiance (son) jouée à l'entrée
	std::array<uint8_t, 3> color{120, 120, 140}; ///< couleur d'interface
	NodePtr raw;
};

struct TextureDef {
	String id, mod;
	String path;        ///< fichier image (absolu) ; vide : texture procédurale
	NodePtr procedural; ///< {"color":[r,g,b], "noise":0.2, "pattern":"bricks"…}
	/// Composition (« (terre.png^herbe.png)^lighting(0.5) ») : voir
	/// ComposeTexture (assets.hpp). Les noms y sont résolus depuis `baseDir`.
	String compose;
	String baseDir;
	int frames = 1;
	float frameTime = 0.25f;
	/// Teinte du biome appliquée à toute face qui utilise cette texture
	/// (« grass », « foliage », « water ») : herbe grise du pack…
	String biomeTint;
	/// Variantes (« variants » : ids de textures) : chaque bloc posé en tire
	/// une d'après sa position ; la texture elle-même est la première.
	std::vector<String> variants;
	NodePtr raw;
};

struct ModelDef {
	String id, mod;
	String format; ///< "obj", "b3d", "json"
	String path;
	NodePtr data;  ///< modèle "json" en ligne (sommets/faces ou boîtes)
	NodePtr raw;
};

struct SoundDef {
	String id, mod, path;
	/// Variantes (« path » + « paths ») : une est tirée au hasard à chaque lecture.
	std::vector<String> paths;
	float volume = 1.f, pitchVariation = 0.f;
	NodePtr raw;
};

/// Sons d'une matière (pierre, bois…), partagés par les blocs qui y renvoient
/// (`"sounds": "core:stone"`).
struct SoundSetDef {
	String id, mod;
	SoundSet sounds;
	NodePtr raw;
};

struct ShaderDef {
	String id, mod, vertex, fragment; ///< sources GLSL (chemins absolus)
	NodePtr raw;
};

struct BehaviorDef {
	String id, mod;
	String builtin; ///< "wander", "follow", "flee", "idle", "float", "look_at_player"…
	String script;  ///< fonction de script par pas de simulation
	NodePtr params;
	NodePtr raw;
};

class ContentRegistry {
public:
	ContentRegistry();

	struct LoadContext {
		String mod = "core";
		String baseDir;    ///< dossier du mod (chemins relatifs des fichiers)
		String source;     ///< fichier lu (messages d'erreur)
	};

	/// Lit UNE définition (migrée, validée) ; rend son id. Une définition
	/// déjà connue sous cet id est REMPLACÉE (surcharge par un mod).
	Result<String, String> Load(const NodePtr &definition, const LoadContext &context);
	/// Fichier de définitions : un objet, ou un tableau d'objets.
	Result<std::vector<String>, String> LoadFile(const String &path, const LoadContext &context);

	using Migration = std::function<void(NodePtr &definition)>;
	/// Migration du schéma `fromVersion` vers `fromVersion + 1`.
	void RegisterMigration(int fromVersion, Migration migration);

	// ── Blocs ───────────────────────────────────────────────────────────────
	[[nodiscard]] const BlockDef &Block(BlockId id) const noexcept {
		return id < m_blocks.size() ? m_blocks[id] : m_blocks[AIR];
	}
	[[nodiscard]] const BlockDef *FindBlock(const String &id) const;
	/// L'état `state` du bloc `block` (nullptr s'il n'existe pas).
	[[nodiscard]] const BlockDef *BlockState(const BlockDef &block, const String &state) const;
	/// Plus grand rayon de détection des blocs à capteur (0 : aucun).
	[[nodiscard]] float MaxSensorRadius() const;
	/// Identifiant numérique (AIR si inconnu).
	[[nodiscard]] BlockId BlockIdOf(const String &id) const;
	[[nodiscard]] size_t BlockCount() const noexcept { return m_blocks.size(); }
	[[nodiscard]] const std::vector<BlockDef> &Blocks() const noexcept { return m_blocks; }

	// ── Autres natures ──────────────────────────────────────────────────────
	[[nodiscard]] const ItemDef *FindItem(const String &id) const;
	[[nodiscard]] const EntityDef *FindEntity(const String &id) const;
	[[nodiscard]] const RecipeDef *FindRecipe(const String &id) const;
	[[nodiscard]] const BiomeDef *FindBiome(const String &id) const;
	[[nodiscard]] const StructureDef *FindStructure(const String &id) const;
	[[nodiscard]] const GeneratorDef *FindGenerator(const String &id) const;
	[[nodiscard]] const TextureDef *FindTexture(const String &id) const;
	[[nodiscard]] const ModelDef *FindModel(const String &id) const;
	[[nodiscard]] const SoundDef *FindSound(const String &id) const;
	[[nodiscard]] const SoundSetDef *FindSoundSet(const String &id) const;
	[[nodiscard]] const ShaderDef *FindShader(const String &id) const;
	[[nodiscard]] const BehaviorDef *FindBehavior(const String &id) const;
	[[nodiscard]] const DimensionDef *FindDimension(const String &id) const;

	[[nodiscard]] std::vector<const ItemDef *> Items() const;
	[[nodiscard]] std::vector<const EntityDef *> Entities() const;
	[[nodiscard]] std::vector<const RecipeDef *> Recipes() const;
	[[nodiscard]] std::vector<const BiomeDef *> Biomes() const;
	[[nodiscard]] std::vector<const StructureDef *> Structures() const;
	[[nodiscard]] std::vector<const GeneratorDef *> Generators() const;
	[[nodiscard]] std::vector<const TextureDef *> Textures() const;
	[[nodiscard]] std::vector<const ModelDef *> Models() const;
	[[nodiscard]] std::vector<const SoundDef *> Sounds() const;
	[[nodiscard]] std::vector<const ShaderDef *> Shaders() const;
	[[nodiscard]] std::vector<const BehaviorDef *> Behaviors() const;
	[[nodiscard]] std::vector<const DimensionDef *> Dimensions() const;
	/// Dimension du monde ordinaire (configuration « world.dimension », par
	/// défaut « core:overworld » ; vide si aucun mod ne la déclare).
	[[nodiscard]] String DefaultDimension() const;

	/// Objets portant un tag (recettes « #planks »…).
	[[nodiscard]] bool ItemHasTag(const String &item, const String &tag) const;

	/// Réglages fusionnés de toutes les définitions "config" (le dernier mod
	/// chargé l'emporte, clé par clé).
	[[nodiscard]] const NodePtr &Config() const noexcept { return m_config; }
	[[nodiscard]] String ConfigStr(const char *path, const String &fallback = String()) const;
	[[nodiscard]] double ConfigNum(const char *path, double fallback = 0.0) const;

	/// Après le chargement de TOUS les mods : complète les sons des blocs et
	/// des entités par leurs jeux de sons (l'ordre des fichiers n'importe pas).
	void Finalize();

	/// Vérifie les renvois entre définitions ; rend les avertissements.
	[[nodiscard]] std::vector<String> Validate() const;
	/// Plus grande portée des lumières (blocs et entités) ; 0 : aucune.
	[[nodiscard]] float MaxLightRange() const;

	/// Id complet (« mod:nom ») d'un nom éventuellement court.
	[[nodiscard]] static String Qualify(const String &id, const String &mod);
	/// Vrai si un renvoi de texture est une expression de composition (ou un
	/// simple nom de fichier image) plutôt qu'un id de TextureDef.
	[[nodiscard]] static bool IsTextureExpression(const String &ref);

	[[nodiscard]] size_t DefinitionCount() const noexcept;

private:
	/// Valeurs refusées ou corrigées au chargement (lumières, optique) : rendues par Validate.
	std::vector<String> m_loadWarnings;
	template <typename T> struct Store {
		std::map<std::string, T> byId;
		std::vector<std::string> order;
		void Put(T def) {
			const std::string key(def.id.CStr());
			if (!byId.contains(key))
				order.push_back(key);
			byId[key] = std::move(def);
		}
		[[nodiscard]] const T *Find(const String &id) const {
			auto it = byId.find(std::string(id.CStr()));
			return it == byId.end() ? nullptr : &it->second;
		}
		[[nodiscard]] std::vector<const T *> All() const {
			std::vector<const T *> out;
			for (const std::string &key : order)
				out.push_back(&byId.at(key));
			return out;
		}
	};

	void Migrate(NodePtr &definition) const;
	Result<String, String> LoadBlock(const NodePtr &def, const LoadContext &ctx, const String &type);
	/// Bloc à « states » : un LoadBlock par état (cf. BlockDef::stateGroup).
	Result<String, String> LoadBlockStates(const NodePtr &def, const LoadContext &ctx, const String &type);
	Result<String, String> LoadItem(const NodePtr &def, const LoadContext &ctx, bool tool);
	Result<String, String> LoadEntity(const NodePtr &def, const LoadContext &ctx, const String &kind);
	Result<String, String> LoadRecipe(const NodePtr &def, const LoadContext &ctx);
	Result<String, String> LoadBiome(const NodePtr &def, const LoadContext &ctx);
	Result<String, String> LoadStructure(const NodePtr &def, const LoadContext &ctx);
	Result<String, String> LoadGenerator(const NodePtr &def, const LoadContext &ctx);
	Result<String, String> LoadTexture(const NodePtr &def, const LoadContext &ctx);
	/// Renvoi de texture d'une définition : id qualifié, ou id d'une texture
	/// implicite créée pour une expression de composition.
	String TextureRef(const String &ref, const LoadContext &ctx);
	Result<String, String> LoadModel(const NodePtr &def, const LoadContext &ctx);
	Result<String, String> LoadSound(const NodePtr &def, const LoadContext &ctx);
	Result<String, String> LoadSoundSet(const NodePtr &def, const LoadContext &ctx);
	Result<String, String> LoadShader(const NodePtr &def, const LoadContext &ctx);
	Result<String, String> LoadBehavior(const NodePtr &def, const LoadContext &ctx);
	Result<String, String> LoadConfig(const NodePtr &def, const LoadContext &ctx);
	Result<String, String> LoadDimension(const NodePtr &def, const LoadContext &ctx);

	std::vector<BlockDef> m_blocks;
	std::map<std::string, BlockId> m_blockIds;
	Store<ItemDef> m_items;
	Store<EntityDef> m_entities;
	Store<RecipeDef> m_recipes;
	Store<BiomeDef> m_biomes;
	Store<StructureDef> m_structures;
	Store<GeneratorDef> m_generators;
	Store<TextureDef> m_textures;
	Store<ModelDef> m_models;
	Store<SoundDef> m_sounds;
	Store<SoundSetDef> m_soundSets;
	Store<ShaderDef> m_shaders;
	Store<BehaviorDef> m_behaviors;
	Store<DimensionDef> m_dimensions;
	NodePtr m_config;
	std::map<int, Migration> m_migrations;
};

/// Lecture des sous-structures communes (utilisées aussi par les scripts).
namespace parse {
/// Boîtes de nodebox (convention centrée [−0,5 ; 0,5]) → boîtes locales [0, 1].
[[nodiscard]] std::vector<Box> NodeBoxes(const NodePtr &list);
[[nodiscard]] Appearance Look(const NodePtr &graphics);
[[nodiscard]] SoundSet Sounds(const NodePtr &sounds);
[[nodiscard]] RenderMaterial Material(const String &name);
[[nodiscard]] ShapeDesc Shape(const NodePtr &def, const String &type, const String &baseDir);
} // namespace parse

} // namespace voxel
