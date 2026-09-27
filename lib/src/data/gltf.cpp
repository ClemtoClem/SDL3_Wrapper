// Définitions de data/gltf.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/gltf.hpp"

namespace data::gltf {

size_t ComponentByteSize(ComponentType type) noexcept {
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

size_t ComponentCount(const String &type) noexcept {
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

// ── Primitive ────────────────────────────────────────────────────────────────

int Primitive::Find(const char *attributeName) const noexcept {
	for (const Attribute &attribute : attributes)
		if (attribute.name == attributeName)
			return attribute.accessor;
	return -1;
}

// ── Document ─────────────────────────────────────────────────────────────────

Result<std::vector<float>, String> Document::ReadFloats(int accessorIndex) const {
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

Result<std::vector<uint32_t>, String> Document::ReadIndices(int accessorIndex) const {
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

String Document::ResolveUri(const String &uri) const {
	if (uri.IsEmpty() || baseDirectory.IsEmpty())
		return uri;
	return baseDirectory + "/" + uri;
}

Result<Document::FetchResult, String> Document::Fetch(int accessorIndex) const {
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

float Document::ToFloat(const uint8_t *at, ComponentType type, bool normalized) noexcept {
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

namespace detail {

int NodeInt(const NodePtr &node, int fallback) noexcept {
	if (!node)
		return fallback;
	if (node->IsInt())
		return int(node->intValue);
	if (node->IsFloat())
		return int(node->floatValue);
	return fallback;
}

float NodeFloat(const NodePtr &node, float fallback) noexcept {
	if (!node)
		return fallback;
	if (node->IsInt())
		return float(node->intValue);
	if (node->IsFloat())
		return float(node->floatValue);
	return fallback;
}

String NodeStr(const NodePtr &node) {
	return (node && node->IsString()) ? node->stringValue : String();
}

bool NodeBool(const NodePtr &node, bool fallback) noexcept {
	return (node && node->IsBool()) ? node->boolValue : fallback;
}

void ReadFloatArray(const NodePtr &node, float *out, size_t count) {
	if (!node || !node->IsArray())
		return;
	for (size_t i = 0; i < count && i < node->GetSize(); ++i)
		out[i] = NodeFloat(node->At(i), out[i]);
}

std::vector<int> ReadIntArray(const NodePtr &node) {
	std::vector<int> out;
	if (!node || !node->IsArray())
		return out;
	for (size_t i = 0; i < node->GetSize(); ++i)
		out.push_back(NodeInt(node->At(i), 0));
	return out;
}

TextureRef ReadTextureRef(const NodePtr &node) {
	TextureRef ref;
	if (!node || !node->IsObject())
		return ref;
	ref.index = NodeInt(node->Get("index"), -1);
	ref.texCoord = NodeInt(node->Get("texCoord"), 0);
	return ref;
}

String ParentDirectory(const String &path) {
	size_t slash = path.Rfind('/');
	if (slash == String::NPOS)
		return String();
	return path.Substr(0, slash);
}

} // namespace detail

Result<Document, String> ParseJson(const NodePtr &root, const String &baseDirectory) {
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

Option<String> LoadBuffers(Document &document) {
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

Result<Document, String> LoadFile(const String &path, bool loadBuffers) {
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
