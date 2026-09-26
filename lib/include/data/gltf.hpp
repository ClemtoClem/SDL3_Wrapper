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

[[nodiscard]] inline size_t ComponentByteSize(ComponentType type) noexcept {
	switch (type) {
		case ComponentType::BYTE:
		case ComponentType::UNSIGNED_BYTE:
			return 1;
		case ComponentType::SHORT:
		case ComponentType::UNSIGNED_SHORT:
			return 2;
		case ComponentType::UNSIGNED_INT:
		case ComponentType::FLOAT:
			return 4;
	}
	return 0;
}

/// Nombre de composantes d'un `type` d'accesseur ("VEC3" → 3). 0 si inconnu.
[[nodiscard]] inline size_t ComponentCount(const String &type) noexcept {
	if (type == "SCALAR")
		return 1;
	if (type == "VEC2")
		return 2;
	if (type == "VEC3")
		return 3;
	if (type == "VEC4")
		return 4;
	if (type == "MAT2")
		return 4;
	if (type == "MAT3")
		return 9;
	if (type == "MAT4")
		return 16;
	return 0;
}

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

	[[nodiscard]] int Find(const char *attributeName) const noexcept {
		for (const Attribute &attribute : attributes)
			if (attribute.name == attributeName)
				return attribute.accessor;
		return -1;
	}
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
	[[nodiscard]] Result<std::vector<float>, String> ReadFloats(int accessorIndex) const {
		auto raw = Fetch(accessorIndex);
		if (raw.IsError())
			return Err(raw.Error());
		const FetchResult &fetched = raw.Value();

		std::vector<float> out;
		out.reserve(fetched.count * fetched.components);
		for (size_t element = 0; element < fetched.count; ++element) {
			const uint8_t *base = fetched.data + element * fetched.elementStride;
			for (size_t component = 0; component < fetched.components; ++component) {
				const uint8_t *at = base + component * ComponentByteSize(fetched.componentType);
				out.push_back(ToFloat(at, fetched.componentType, fetched.normalized));
			}
		}
		return Ok(std::move(out));
	}

	/// Rend les indices d'une primitive sous forme d'`uint32_t`, quel que
	/// soit leur type de stockage (byte/short/int).
	[[nodiscard]] Result<std::vector<uint32_t>, String> ReadIndices(int accessorIndex) const {
		auto raw = Fetch(accessorIndex);
		if (raw.IsError())
			return Err(raw.Error());
		const FetchResult &fetched = raw.Value();
		if (fetched.components != 1)
			return Err(String("glTF : un accesseur d'indices doit être SCALAR"));

		std::vector<uint32_t> out;
		out.reserve(fetched.count);
		for (size_t element = 0; element < fetched.count; ++element) {
			const uint8_t *at = fetched.data + element * fetched.elementStride;
			switch (fetched.componentType) {
				case ComponentType::UNSIGNED_BYTE:
					out.push_back(uint32_t(*at));
					break;
				case ComponentType::UNSIGNED_SHORT: {
					uint16_t value = 0;
					std::memcpy(&value, at, sizeof(value));
					out.push_back(uint32_t(value));
					break;
				}
				case ComponentType::UNSIGNED_INT: {
					uint32_t value = 0;
					std::memcpy(&value, at, sizeof(value));
					out.push_back(value);
					break;
				}
				default:
					return Err(String("glTF : type de composante d'indice non entier non signé"));
			}
		}
		return Ok(std::move(out));
	}

	/// Chemin d'une image, résolu par rapport au document.
	[[nodiscard]] String ResolveUri(const String &uri) const {
		if (uri.IsEmpty() || baseDirectory.IsEmpty())
			return uri;
		return baseDirectory + "/" + uri;
	}

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
	[[nodiscard]] Result<FetchResult, String> Fetch(int accessorIndex) const {
		if (accessorIndex < 0 || size_t(accessorIndex) >= accessors.size())
			return Err(String::Format("glTF : accesseur %d inexistant", accessorIndex));
		const Accessor &accessor = accessors[size_t(accessorIndex)];

		size_t components = ComponentCount(accessor.type);
		if (components == 0)
			return Err(String::Format("glTF : type d'accesseur inconnu `%s`", accessor.type.CStr()));
		size_t componentSize = ComponentByteSize(accessor.componentType);
		if (componentSize == 0)
			return Err(String("glTF : type de composante inconnu"));

		if (accessor.bufferView < 0 || size_t(accessor.bufferView) >= bufferViews.size())
			return Err(String::Format("glTF : accesseur %d sans bufferView (accesseur creux non géré)",
									  accessorIndex));
		const BufferView &view = bufferViews[size_t(accessor.bufferView)];
		if (view.buffer < 0 || size_t(view.buffer) >= buffers.size())
			return Err(String("glTF : bufferView pointant vers un tampon inexistant"));
		const Buffer &buffer = buffers[size_t(view.buffer)];
		if (buffer.data.empty())
			return Err(String::Format("glTF : tampon `%s` non chargé (appelez LoadBuffers)", buffer.uri.CStr()));

		size_t packedSize = components * componentSize;
		size_t stride = view.byteStride != 0 ? view.byteStride : packedSize;
		if (view.byteStride != 0 && view.byteStride != packedSize)
			return Err(String::Format("glTF : attributs entrelacés (byteStride %d ≠ %d) non gérés",
									  int(view.byteStride), int(packedSize)));

		size_t start = view.byteOffset + accessor.byteOffset;
		size_t needed = accessor.count == 0 ? 0 : (accessor.count - 1) * stride + packedSize;
		if (start + needed > buffer.data.size())
			return Err(String::Format("glTF : accesseur %d hors du tampon (%d octets requis, %d disponibles)",
									  accessorIndex, int(start + needed), int(buffer.data.size())));

		FetchResult result;
		result.data = buffer.data.data() + start;
		result.count = accessor.count;
		result.components = components;
		result.elementStride = stride;
		result.componentType = accessor.componentType;
		result.normalized = accessor.normalized;
		return Ok(result);
	}

	[[nodiscard]] static float ToFloat(const uint8_t *at, ComponentType type, bool normalized) noexcept {
		switch (type) {
			case ComponentType::FLOAT: {
				float value = 0.f;
				std::memcpy(&value, at, sizeof(value));
				return value;
			}
			case ComponentType::UNSIGNED_BYTE:
				return normalized ? float(*at) / 255.f : float(*at);
			case ComponentType::BYTE: {
				int8_t value = 0;
				std::memcpy(&value, at, sizeof(value));
				// Spécification glTF : la borne basse d'un signé normalisé est
				// -1, obtenue en divisant par 127 puis en bornant.
				return normalized ? (float(value) / 127.f < -1.f ? -1.f : float(value) / 127.f) : float(value);
			}
			case ComponentType::UNSIGNED_SHORT: {
				uint16_t value = 0;
				std::memcpy(&value, at, sizeof(value));
				return normalized ? float(value) / 65535.f : float(value);
			}
			case ComponentType::SHORT: {
				int16_t value = 0;
				std::memcpy(&value, at, sizeof(value));
				return normalized ? (float(value) / 32767.f < -1.f ? -1.f : float(value) / 32767.f) : float(value);
			}
			case ComponentType::UNSIGNED_INT: {
				uint32_t value = 0;
				std::memcpy(&value, at, sizeof(value));
				return float(value);
			}
		}
		return 0.f;
	}
};

// ============================================================================
// Lecture
// ============================================================================

namespace detail {

[[nodiscard]] inline int NodeInt(const NodePtr &node, int fallback = -1) noexcept {
	if (!node)
		return fallback;
	if (node->IsInt())
		return int(node->intValue);
	if (node->IsFloat())
		return int(node->floatValue);
	return fallback;
}

[[nodiscard]] inline float NodeFloat(const NodePtr &node, float fallback) noexcept {
	if (!node)
		return fallback;
	if (node->IsInt())
		return float(node->intValue);
	if (node->IsFloat())
		return float(node->floatValue);
	return fallback;
}

[[nodiscard]] inline String NodeStr(const NodePtr &node) {
	return (node && node->IsString()) ? node->stringValue : String();
}

[[nodiscard]] inline bool NodeBool(const NodePtr &node, bool fallback = false) noexcept {
	return (node && node->IsBool()) ? node->boolValue : fallback;
}

/// Remplit `out` (taille fixe) depuis un tableau JSON, en laissant les
/// valeurs par défaut si le tableau est absent ou trop court.
inline void ReadFloatArray(const NodePtr &node, float *out, size_t count) {
	if (!node || !node->IsArray())
		return;
	for (size_t i = 0; i < count && i < node->GetSize(); ++i)
		out[i] = NodeFloat(node->At(i), out[i]);
}

[[nodiscard]] inline std::vector<int> ReadIntArray(const NodePtr &node) {
	std::vector<int> out;
	if (!node || !node->IsArray())
		return out;
	for (size_t i = 0; i < node->GetSize(); ++i)
		out.push_back(NodeInt(node->At(i), 0));
	return out;
}

[[nodiscard]] inline TextureRef ReadTextureRef(const NodePtr &node) {
	TextureRef ref;
	if (!node || !node->IsObject())
		return ref;
	ref.index = NodeInt(node->Get("index"), -1);
	ref.texCoord = NodeInt(node->Get("texCoord"), 0);
	return ref;
}

/// Dossier parent d'un chemin (chaîne vide s'il n'y en a pas).
[[nodiscard]] inline String ParentDirectory(const String &path) {
	size_t slash = path.Rfind('/');
	if (slash == String::NPOS)
		return String();
	return path.Substr(0, slash);
}

} // namespace detail

/// Analyse un arbre JSON déjà décodé comme document glTF 2.0.
/// `baseDirectory` sert à résoudre les URI relatives (tampons, images).
[[nodiscard]] inline Result<Document, String> ParseJson(const NodePtr &root, const String &baseDirectory = String()) {
	if (!root || !root->IsObject())
		return Err(String("glTF : racine JSON invalide (objet attendu)"));

	Document document;
	document.baseDirectory = baseDirectory;

	if (auto asset = root->Get("asset"); asset && asset->IsObject()) {
		document.version = detail::NodeStr(asset->Get("version"));
		document.generator = detail::NodeStr(asset->Get("generator"));
	}
	// La version est le seul champ que la spécification rend obligatoire ; un
	// document qui ne l'a pas n'est pas du glTF, et le lire quand même
	// donnerait des erreurs incompréhensibles plus loin.
	if (document.version.IsEmpty())
		return Err(String("glTF : champ `asset.version` manquant — ce n'est pas un document glTF 2.0"));
	if (!document.version.StartsWith("2."))
		return Err(String::Format("glTF : version %s non gérée (2.x attendue)", document.version.CStr()));

	document.defaultScene = detail::NodeInt(root->Get("scene"), -1);

	if (auto buffers = root->Get("buffers"); buffers && buffers->IsArray()) {
		for (size_t i = 0; i < buffers->GetSize(); ++i) {
			auto entry = buffers->At(i);
			Buffer buffer;
			buffer.uri = detail::NodeStr(entry ? entry->Get("uri") : nullptr);
			buffer.byteLength = size_t(detail::NodeInt(entry ? entry->Get("byteLength") : nullptr, 0));
			document.buffers.push_back(std::move(buffer));
		}
	}

	if (auto views = root->Get("bufferViews"); views && views->IsArray()) {
		for (size_t i = 0; i < views->GetSize(); ++i) {
			auto entry = views->At(i);
			BufferView view;
			view.buffer = detail::NodeInt(entry ? entry->Get("buffer") : nullptr, -1);
			view.byteOffset = size_t(detail::NodeInt(entry ? entry->Get("byteOffset") : nullptr, 0));
			view.byteLength = size_t(detail::NodeInt(entry ? entry->Get("byteLength") : nullptr, 0));
			view.byteStride = size_t(detail::NodeInt(entry ? entry->Get("byteStride") : nullptr, 0));
			document.bufferViews.push_back(view);
		}
	}

	if (auto accessors = root->Get("accessors"); accessors && accessors->IsArray()) {
		for (size_t i = 0; i < accessors->GetSize(); ++i) {
			auto entry = accessors->At(i);
			Accessor accessor;
			accessor.bufferView = detail::NodeInt(entry ? entry->Get("bufferView") : nullptr, -1);
			accessor.byteOffset = size_t(detail::NodeInt(entry ? entry->Get("byteOffset") : nullptr, 0));
			accessor.componentType =
				ComponentType(detail::NodeInt(entry ? entry->Get("componentType") : nullptr, 5126));
			accessor.count = size_t(detail::NodeInt(entry ? entry->Get("count") : nullptr, 0));
			accessor.type = detail::NodeStr(entry ? entry->Get("type") : nullptr);
			if (accessor.type.IsEmpty())
				accessor.type = "SCALAR";
			accessor.normalized = detail::NodeBool(entry ? entry->Get("normalized") : nullptr, false);
			document.accessors.push_back(std::move(accessor));
		}
	}

	if (auto meshes = root->Get("meshes"); meshes && meshes->IsArray()) {
		for (size_t i = 0; i < meshes->GetSize(); ++i) {
			auto entry = meshes->At(i);
			Mesh mesh;
			mesh.name = detail::NodeStr(entry ? entry->Get("name") : nullptr);
			if (auto primitives = entry ? entry->Get("primitives") : nullptr; primitives && primitives->IsArray()) {
				for (size_t p = 0; p < primitives->GetSize(); ++p) {
					auto primitiveNode = primitives->At(p);
					Primitive primitive;
					primitive.indices = detail::NodeInt(primitiveNode ? primitiveNode->Get("indices") : nullptr, -1);
					primitive.material = detail::NodeInt(primitiveNode ? primitiveNode->Get("material") : nullptr, -1);
					primitive.mode = detail::NodeInt(primitiveNode ? primitiveNode->Get("mode") : nullptr, 4);
					if (auto attributes = primitiveNode ? primitiveNode->Get("attributes") : nullptr;
						attributes && attributes->IsObject()) {
						for (const String &key : attributes->Keys())
							primitive.attributes.push_back(
								Attribute{key, detail::NodeInt(attributes->Get(key), -1)});
					}
					mesh.primitives.push_back(std::move(primitive));
				}
			}
			document.meshes.push_back(std::move(mesh));
		}
	}

	if (auto nodes = root->Get("nodes"); nodes && nodes->IsArray()) {
		for (size_t i = 0; i < nodes->GetSize(); ++i) {
			auto entry = nodes->At(i);
			Node node;
			node.name = detail::NodeStr(entry ? entry->Get("name") : nullptr);
			node.mesh = detail::NodeInt(entry ? entry->Get("mesh") : nullptr, -1);
			node.children = detail::ReadIntArray(entry ? entry->Get("children") : nullptr);
			detail::ReadFloatArray(entry ? entry->Get("translation") : nullptr, node.translation, 3);
			detail::ReadFloatArray(entry ? entry->Get("rotation") : nullptr, node.rotation, 4);
			detail::ReadFloatArray(entry ? entry->Get("scale") : nullptr, node.scale, 3);
			document.nodes.push_back(std::move(node));
		}
	}

	if (auto scenes = root->Get("scenes"); scenes && scenes->IsArray()) {
		for (size_t i = 0; i < scenes->GetSize(); ++i) {
			auto entry = scenes->At(i);
			Scene scene;
			scene.name = detail::NodeStr(entry ? entry->Get("name") : nullptr);
			scene.nodes = detail::ReadIntArray(entry ? entry->Get("nodes") : nullptr);
			document.scenes.push_back(std::move(scene));
		}
	}

	if (auto materials = root->Get("materials"); materials && materials->IsArray()) {
		for (size_t i = 0; i < materials->GetSize(); ++i) {
			auto entry = materials->At(i);
			Material material;
			material.name = detail::NodeStr(entry ? entry->Get("name") : nullptr);
			material.doubleSided = detail::NodeBool(entry ? entry->Get("doubleSided") : nullptr, false);
			material.normalTexture = detail::ReadTextureRef(entry ? entry->Get("normalTexture") : nullptr);
			if (auto pbr = entry ? entry->Get("pbrMetallicRoughness") : nullptr; pbr && pbr->IsObject()) {
				detail::ReadFloatArray(pbr->Get("baseColorFactor"), material.baseColor, 4);
				material.metallic = detail::NodeFloat(pbr->Get("metallicFactor"), 1.f);
				material.roughness = detail::NodeFloat(pbr->Get("roughnessFactor"), 1.f);
				material.baseColorTexture = detail::ReadTextureRef(pbr->Get("baseColorTexture"));
				material.metallicRoughnessTexture = detail::ReadTextureRef(pbr->Get("metallicRoughnessTexture"));
			}
			document.materials.push_back(std::move(material));
		}
	}

	if (auto images = root->Get("images"); images && images->IsArray()) {
		for (size_t i = 0; i < images->GetSize(); ++i) {
			auto entry = images->At(i);
			Image image;
			image.name = detail::NodeStr(entry ? entry->Get("name") : nullptr);
			image.uri = detail::NodeStr(entry ? entry->Get("uri") : nullptr);
			document.images.push_back(std::move(image));
		}
	}

	if (auto textures = root->Get("textures"); textures && textures->IsArray()) {
		for (size_t i = 0; i < textures->GetSize(); ++i) {
			auto entry = textures->At(i);
			Texture texture;
			texture.source = detail::NodeInt(entry ? entry->Get("source") : nullptr, -1);
			texture.sampler = detail::NodeInt(entry ? entry->Get("sampler") : nullptr, -1);
			document.textures.push_back(texture);
		}
	}

	return Ok(std::move(document));
}

/// Charge les tampons binaires référencés par URI (`.bin` à côté du
/// document). Un tampon sans URI (donnée embarquée en base64 ou conteneur
/// `.glb`) est SIGNALÉ comme non géré plutôt que laissé vide en silence :
/// sinon l'échec ne surviendrait qu'au premier `ReadFloats`, très loin de sa
/// cause.
[[nodiscard]] inline Option<String> LoadBuffers(Document &document) {
	for (Buffer &buffer : document.buffers) {
		if (!buffer.data.empty())
			continue;
		if (buffer.uri.IsEmpty())
			return Some(String("glTF : tampon sans `uri` (donnée embarquée / .glb non gérée)"));
		if (buffer.uri.StartsWith("data:"))
			return Some(String("glTF : tampon en URI `data:` (base64 embarqué) non géré"));

		String path = document.ResolveUri(buffer.uri);
		auto bytes = sdl3::ReadFile(path);
		if (!bytes)
			return Some(String::Format("glTF : tampon illisible `%s` (%s)", path.CStr(),
									   String(bytes.Error()).CStr()));
		buffer.data = std::move(bytes.Value());
		if (buffer.byteLength != 0 && buffer.data.size() < buffer.byteLength)
			return Some(String::Format("glTF : tampon `%s` tronqué (%d octets, %d annoncés)", path.CStr(),
									   int(buffer.data.size()), int(buffer.byteLength)));
	}
	return NONE;
}

/// Lit un `.gltf` depuis le disque : JSON → `Document` → tampons chargés.
/// `loadBuffers = false` pour n'inspecter que la structure (lister les
/// maillages et les textures d'un fichier sans lire les mégaoctets de
/// géométrie) — c'est ce que fait le navigateur de ressources de
/// examples/game_editor.
[[nodiscard]] inline Result<Document, String> LoadFile(const String &path, bool loadBuffers = true) {
	auto bytes = sdl3::ReadFile(path);
	if (!bytes)
		return Err(String::Format("glTF : %s (%s)", path.CStr(), String(bytes.Error()).CStr()));

	String text(reinterpret_cast<const char *>(bytes.Value().data()), bytes.Value().size());
	JsonDocument json;
	if (auto error = json.DecodeStr(text); error.IsSome())
		return Err(String::Format("glTF : %s : %s", path.CStr(), error.Unwrap().Format().CStr()));

	auto document = ParseJson(json.GetRoot(), detail::ParentDirectory(path));
	if (document.IsError())
		return Err(String::Format("%s : %s", path.CStr(), document.Error().CStr()));

	Document loaded = std::move(document).Unwrap();
	if (loadBuffers)
		if (auto error = LoadBuffers(loaded); error.IsSome())
			return Err(error.Unwrap());
	return Ok(std::move(loaded));
}

} // namespace data::gltf
