#pragma once
/**
 * data::gltf — lecture de documents glTF 2.0 (`.gltf`, JSON) par-dessus
 * `data::JsonDocument`.
 *
 * Le module `data::` sait déjà décoder du JSON en `data::Node` ; ce fichier
 * fait la couche au-dessus : INTERPRÉTER cet arbre selon la spécification
 * glTF, et surtout DÉCODER les tampons binaires que le JSON ne fait que
 * référencer (`buffers` → `bufferViews` → `accessors`), pour rendre des
 * tableaux de `float` / d'indices directement consommables.
 *
 * Volontairement INDÉPENDANT de `render3d::` : ce lecteur rend une
 * description neutre (`Document`), et c'est l'appelant qui construit ses
 * propres maillages. Un module de données n'a pas à connaître le moteur de
 * rendu, et c'est ce qui rend ce fichier testable sans GPU.
 *
 * ── Ce qui est couvert ───────────────────────────────────────────────────
 * `asset`, `scenes`/`nodes` (hiérarchie, translation/rotation/scale),
 * `meshes`/`primitives` (attributs + indices), `materials`
 * (`pbrMetallicRoughness` : couleur de base, métallicité, rugosité, plus le
 * nom des textures référencées), `images`/`textures`/`samplers` (les URI,
 * pour que l'appelant charge les images avec `sdl3::ImgLoad`), et le décodage
 * d'accesseurs vers `float`/`uint32_t`.
 *
 * ── Ce qui ne l'est PAS (limites explicites) ─────────────────────────────
 * `.glb` (conteneur binaire), accesseurs *sparse*, animations, peaux
 * (`skins`), extensions, et `byteStride` non contigu (entrelacement) — dans
 * ce dernier cas le décodage ÉCHOUE avec un message clair plutôt que de
 * rendre des données fausses silencieusement.
 *
 * Aucune exception (cf. memory/feedback_no_exceptions.md) : tout remonte en
 * `Result<…, String>`.
 */
#include <cstdint>
#include <cstring>
#include <vector>

#include "../core/core.hpp"
#include "../sdl3/iostream.hpp"
#include "json.hpp"
#include "node.hpp"

namespace data::gltf {

// ============================================================================
// Types de la spécification
// ============================================================================

/// `componentType` d'un accesseur (valeurs numériques de la spécification).
enum class ComponentType : int {
	BYTE = 5120,
	UNSIGNED_BYTE = 5121,
	SHORT = 5122,
	UNSIGNED_SHORT = 5123,
	UNSIGNED_INT = 5125,
	FLOAT = 5126,
};

[[nodiscard]] size_t ComponentByteSize(ComponentType type) noexcept;

/// Nombre de composantes d'un `type` d'accesseur ("VEC3" → 3). 0 si inconnu.
[[nodiscard]] size_t ComponentCount(const String &type) noexcept;

struct Buffer {
	String uri; ///< vide si le tampon est embarqué (non géré, cf. en-tête)
	size_t byteLength = 0;
	std::vector<uint8_t> data; ///< rempli par `LoadBuffers`
};

struct BufferView {
	int buffer = -1;
	size_t byteOffset = 0;
	size_t byteLength = 0;
	size_t byteStride = 0; ///< 0 = données contiguës
};

struct Accessor {
	int bufferView = -1;
	size_t byteOffset = 0;
	ComponentType componentType = ComponentType::FLOAT;
	size_t count = 0;
	String type = "SCALAR";
	bool normalized = false;
};

struct TextureRef {
	int index = -1; ///< index dans `textures`, -1 = absent
	int texCoord = 0;

	[[nodiscard]] bool IsSet() const noexcept { return index >= 0; }
};

struct Material {
	String name;
	float baseColor[4] = {1.f, 1.f, 1.f, 1.f};
	float metallic = 1.f;
	float roughness = 1.f;
	TextureRef baseColorTexture;
	TextureRef metallicRoughnessTexture;
	TextureRef normalTexture;
	bool doubleSided = false;
};

struct Image {
	String name;
	String uri; ///< chemin relatif au document (vide si embarquée)
};

struct Texture {
	int source = -1; ///< index dans `images`
	int sampler = -1;
};

/// Un attribut de primitive : son nom glTF (`POSITION`, `NORMAL`,
/// `TEXCOORD_0`…) et l'accesseur qui le porte.
struct Attribute {
	String name;
	int accessor = -1;
};

struct Primitive {
	std::vector<Attribute> attributes;
	int indices = -1;  ///< accesseur d'indices, -1 = non indexé
	int material = -1; ///< index dans `materials`
	int mode = 4;      ///< 4 = TRIANGLES (seul mode produit par la plupart des exportateurs)

	[[nodiscard]] int Find(const char *attributeName) const noexcept;
};

struct Mesh {
	String name;
	std::vector<Primitive> primitives;
};

struct Node {
	String name;
	int mesh = -1;
	std::vector<int> children;
	float translation[3] = {0.f, 0.f, 0.f};
	float rotation[4] = {0.f, 0.f, 0.f, 1.f}; ///< quaternion (x, y, z, w)
	float scale[3] = {1.f, 1.f, 1.f};
};

struct Scene {
	String name;
	std::vector<int> nodes; ///< racines
};

// ============================================================================
// Document
// ============================================================================

class Document {
public:
	String version;
	String generator;
	int defaultScene = -1;

	std::vector<Buffer> buffers;
	std::vector<BufferView> bufferViews;
	std::vector<Accessor> accessors;
	std::vector<Mesh> meshes;
	std::vector<Node> nodes;
	std::vector<Scene> scenes;
	std::vector<Material> materials;
	std::vector<Image> images;
	std::vector<Texture> textures;

	/// Dossier du document, pour résoudre les URI relatives (tampons, images).
	String baseDirectory;

	// ── Décodage d'accesseurs ────────────────────────────────────────────────

	/// Rend `accessor.count * composantes` flottants, convertis depuis le
	/// type de composante réel (les entiers normalisés sont ramenés dans
	/// [0,1] ou [-1,1] conformément à la spécification).
	[[nodiscard]] Result<std::vector<float>, String> ReadFloats(int accessorIndex) const;

	/// Rend les indices d'une primitive sous forme d'`uint32_t`, quel que
	/// soit leur type de stockage (byte/short/int).
	[[nodiscard]] Result<std::vector<uint32_t>, String> ReadIndices(int accessorIndex) const;

	/// Chemin d'une image, résolu par rapport au document.
	[[nodiscard]] String ResolveUri(const String &uri) const;

private:
	struct FetchResult {
		const uint8_t *data = nullptr;
		size_t count = 0;
		size_t components = 0;
		size_t elementStride = 0;
		ComponentType componentType = ComponentType::FLOAT;
		bool normalized = false;
	};

	/// Localise et VALIDE les octets d'un accesseur. Toutes les bornes sont
	/// vérifiées ici une bonne fois : un document tronqué ou incohérent doit
	/// donner une erreur, jamais une lecture hors tampon.
	[[nodiscard]] Result<FetchResult, String> Fetch(int accessorIndex) const;

	[[nodiscard]] static float ToFloat(const uint8_t *at, ComponentType type, bool normalized) noexcept;
};

// ============================================================================
// Lecture
// ============================================================================

namespace detail {

[[nodiscard]] int NodeInt(const NodePtr &node, int fallback = -1) noexcept;

[[nodiscard]] float NodeFloat(const NodePtr &node, float fallback) noexcept;

[[nodiscard]] String NodeStr(const NodePtr &node);

[[nodiscard]] bool NodeBool(const NodePtr &node, bool fallback = false) noexcept;

/// Remplit `out` (taille fixe) depuis un tableau JSON, en laissant les
/// valeurs par défaut si le tableau est absent ou trop court.
void ReadFloatArray(const NodePtr &node, float *out, size_t count);

[[nodiscard]] std::vector<int> ReadIntArray(const NodePtr &node);

[[nodiscard]] TextureRef ReadTextureRef(const NodePtr &node);

/// Dossier parent d'un chemin (chaîne vide s'il n'y en a pas).
[[nodiscard]] String ParentDirectory(const String &path);

} // namespace detail

/// Analyse un arbre JSON déjà décodé comme document glTF 2.0.
/// `baseDirectory` sert à résoudre les URI relatives (tampons, images).
[[nodiscard]] Result<Document, String> ParseJson(const NodePtr &root, const String &baseDirectory = String());

/// Charge les tampons binaires référencés par URI (`.bin` à côté du
/// document). Un tampon sans URI (donnée embarquée en base64 ou conteneur
/// `.glb`) est SIGNALÉ comme non géré plutôt que laissé vide en silence :
/// sinon l'échec ne surviendrait qu'au premier `ReadFloats`, très loin de sa
/// cause.
[[nodiscard]] Option<String> LoadBuffers(Document &document);

/// Lit un `.gltf` depuis le disque : JSON → `Document` → tampons chargés.
/// `loadBuffers = false` pour n'inspecter que la structure (lister les
/// maillages et les textures d'un fichier sans lire les mégaoctets de
/// géométrie) — c'est ce que fait le navigateur de ressources de
/// examples/game_editor.
[[nodiscard]] Result<Document, String> LoadFile(const String &path, bool loadBuffers = true);

} // namespace data::gltf
