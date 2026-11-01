// Définitions de dataio/documents/gltf_writer.hpp
#include <dataio/documents/gltf_writer.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <dataio/documents/json.hpp>
#include <dataio/node.hpp>

namespace dataio::gltf {

namespace {

constexpr int FLOAT = 5126;
constexpr int UNSIGNED_INT = 5125;
constexpr int ARRAY_BUFFER = 34962;
constexpr int ELEMENT_ARRAY_BUFFER = 34963;
constexpr int NEAREST = 9728;
constexpr int REPEAT = 10497;

NodePtr Num(double v) { return Node::MakeFloat(v); }
NodePtr Int(int64_t v) { return Node::MakeInt(v); }
NodePtr Str(const String &s) { return Node::MakeString(s); }

NodePtr Floats(const float *values, size_t count) {
	NodePtr a = Node::MakeArray();
	for (size_t i = 0; i < count; ++i)
		a->Push(Num(double(values[i])));
	return a;
}

/// Tampon unique en construction : vues et accesseurs ajoutés au fil de l'eau.
struct Packer {
	std::vector<uint8_t> bytes;
	NodePtr views = Node::MakeArray();
	NodePtr accessors = Node::MakeArray();

	int View(const void *data, size_t size, int target) {
		while (bytes.size() % 4 != 0)
			bytes.push_back(0);
		const size_t offset = bytes.size();
		bytes.resize(offset + size);
		if (size > 0)
			std::memcpy(bytes.data() + offset, data, size);
		NodePtr v = Node::MakeObject();
		v->Set("buffer", Int(0));
		v->Set("byteOffset", Int(int64_t(offset)));
		v->Set("byteLength", Int(int64_t(size)));
		if (target != 0)
			v->Set("target", Int(target));
		views->Push(v);
		return int(views->GetSize() - 1);
	}

	/// Accesseur de flottants (`components` par élément) ; `bounds` : min / max.
	int Floats(const std::vector<float> &data, int components, const char *type, int target, bool bounds) {
		const int view = View(data.data(), data.size() * sizeof(float), target);
		NodePtr a = Node::MakeObject();
		a->Set("bufferView", Int(view));
		a->Set("componentType", Int(FLOAT));
		a->Set("count", Int(int64_t(data.size() / size_t(components))));
		a->Set("type", Str(String(type)));
		if (bounds && !data.empty()) {
			std::vector<float> lo(size_t(components), 1e30f), hi(size_t(components), -1e30f);
			for (size_t i = 0; i < data.size(); ++i) {
				lo[i % size_t(components)] = std::min(lo[i % size_t(components)], data[i]);
				hi[i % size_t(components)] = std::max(hi[i % size_t(components)], data[i]);
			}
			a->Set("min", gltf::Floats(lo.data(), lo.size()));
			a->Set("max", gltf::Floats(hi.data(), hi.size()));
		}
		accessors->Push(a);
		return int(accessors->GetSize() - 1);
	}

	int Indices(const std::vector<uint32_t> &data) {
		const int view = View(data.data(), data.size() * sizeof(uint32_t), ELEMENT_ARRAY_BUFFER);
		NodePtr a = Node::MakeObject();
		a->Set("bufferView", Int(view));
		a->Set("componentType", Int(UNSIGNED_INT));
		a->Set("count", Int(int64_t(data.size())));
		a->Set("type", Str(String("SCALAR")));
		accessors->Push(a);
		return int(accessors->GetSize() - 1);
	}
};

} // namespace

Option<String> ValidateScene(const SceneDescription &scene) {
	for (size_t i = 0; i < scene.meshes.size(); ++i) {
		const MeshDescription &m = scene.meshes[i];
		const size_t vertices = m.positions.size() / 3;
		if (m.positions.empty() || m.positions.size() % 3 != 0)
			return Some(String::Format("glTF : maillage %zu : positions vides ou non multiples de 3", i));
		if (!m.normals.empty() && m.normals.size() != vertices * 3)
			return Some(String::Format("glTF : maillage %zu : %zu normales pour %zu sommets", i, m.normals.size() / 3, vertices));
		if (!m.uvs.empty() && m.uvs.size() != vertices * 2)
			return Some(String::Format("glTF : maillage %zu : %zu UV pour %zu sommets", i, m.uvs.size() / 2, vertices));
		if (m.indices.size() % 3 != 0)
			return Some(String::Format("glTF : maillage %zu : indices non multiples de 3", i));
		for (uint32_t index : m.indices)
			if (index >= vertices)
				return Some(String::Format("glTF : maillage %zu : indice %u hors des %zu sommets", i, index, vertices));
		if (m.material >= int(scene.materials.size()))
			return Some(String::Format("glTF : maillage %zu : matériau %d inconnu", i, m.material));
	}
	for (size_t i = 0; i < scene.nodes.size(); ++i) {
		const NodeDescription &n = scene.nodes[i];
		if (n.mesh >= int(scene.meshes.size()))
			return Some(String::Format("glTF : nœud %zu : maillage %d inconnu", i, n.mesh));
		if (n.parent >= int(i))
			return Some(String::Format("glTF : nœud %zu : le parent %d doit le précéder", i, n.parent));
	}
	for (const AnimationDescription &a : scene.animations)
		for (const ChannelDescription &c : a.channels) {
			if (c.node < 0 || c.node >= int(scene.nodes.size()))
				return Some(String::Format("glTF : animation « %s » : nœud %d inconnu", a.name.CStr(), c.node));
			const size_t width = c.path == "rotation" ? 4 : (c.path == "translation" || c.path == "scale") ? 3 : 0;
			if (width == 0)
				return Some(String::Format("glTF : animation « %s » : chemin « %s » inconnu", a.name.CStr(), c.path.CStr()));
			if (c.times.empty() || c.values.size() != c.times.size() * width)
				return Some(String::Format("glTF : animation « %s » : %zu valeurs pour %zu temps (%zu par temps)", a.name.CStr(),
										   c.values.size(), c.times.size(), width));
			for (size_t k = 1; k < c.times.size(); ++k)
				if (c.times[k] < c.times[k - 1])
					return Some(String::Format("glTF : animation « %s » : temps non croissants", a.name.CStr()));
			if (c.interpolation != "LINEAR" && c.interpolation != "STEP")
				return Some(String::Format("glTF : animation « %s » : interpolation « %s » non gérée (LINEAR, STEP)", a.name.CStr(),
										   c.interpolation.CStr()));
		}
	return NONE;
}

Option<String> SaveScene(const String &path, const SceneDescription &scene) {
	if (Option<String> error = ValidateScene(scene); error.IsSome())
		return error;
	namespace fs = std::filesystem;
	const fs::path gltfPath(path.CStr());
	fs::path binPath = gltfPath;
	binPath.replace_extension(".bin");
	const String binName(binPath.filename().string().c_str());

	Packer pack;
	NodePtr root = Node::MakeObject();
	NodePtr asset = Node::MakeObject();
	asset->Set("version", Str(String("2.0")));
	asset->Set("generator", Str(scene.generator));
	root->Set("asset", asset);

	// Matériaux, images, textures (échantillonneur au plus proche).
	NodePtr materials = Node::MakeArray(), images = Node::MakeArray(), textures = Node::MakeArray();
	for (const MaterialDescription &m : scene.materials) {
		NodePtr pbr = Node::MakeObject();
		pbr->Set("baseColorFactor", Floats(m.baseColor, 4));
		pbr->Set("metallicFactor", Num(0.0));
		pbr->Set("roughnessFactor", Num(1.0));
		if (!m.baseColorUri.IsEmpty()) {
			NodePtr image = Node::MakeObject();
			image->Set("uri", Str(m.baseColorUri));
			images->Push(image);
			NodePtr texture = Node::MakeObject();
			texture->Set("source", Int(int64_t(images->GetSize() - 1)));
			texture->Set("sampler", Int(0));
			textures->Push(texture);
			NodePtr ref = Node::MakeObject();
			ref->Set("index", Int(int64_t(textures->GetSize() - 1)));
			pbr->Set("baseColorTexture", ref);
		}
		NodePtr material = Node::MakeObject();
		material->Set("name", Str(m.name));
		material->Set("pbrMetallicRoughness", pbr);
		material->Set("doubleSided", Node::MakeBool(m.doubleSided));
		if (m.alphaMask)
			material->Set("alphaMode", Str(String("MASK")));
		materials->Push(material);
	}

	// Maillages.
	NodePtr meshes = Node::MakeArray();
	for (const MeshDescription &m : scene.meshes) {
		NodePtr attributes = Node::MakeObject();
		attributes->Set("POSITION", Int(pack.Floats(m.positions, 3, "VEC3", ARRAY_BUFFER, true)));
		if (!m.normals.empty())
			attributes->Set("NORMAL", Int(pack.Floats(m.normals, 3, "VEC3", ARRAY_BUFFER, false)));
		if (!m.uvs.empty())
			attributes->Set("TEXCOORD_0", Int(pack.Floats(m.uvs, 2, "VEC2", ARRAY_BUFFER, false)));
		NodePtr primitive = Node::MakeObject();
		primitive->Set("attributes", attributes);
		if (!m.indices.empty())
			primitive->Set("indices", Int(pack.Indices(m.indices)));
		if (m.material >= 0)
			primitive->Set("material", Int(m.material));
		primitive->Set("mode", Int(4));
		NodePtr primitives = Node::MakeArray();
		primitives->Push(primitive);
		NodePtr mesh = Node::MakeObject();
		mesh->Set("name", Str(m.name));
		mesh->Set("primitives", primitives);
		meshes->Push(mesh);
	}

	// Nœuds et scène (racines).
	NodePtr nodes = Node::MakeArray(), sceneRoots = Node::MakeArray();
	std::vector<std::vector<int>> children(scene.nodes.size());
	for (size_t i = 0; i < scene.nodes.size(); ++i)
		if (scene.nodes[i].parent >= 0)
			children[size_t(scene.nodes[i].parent)].push_back(int(i));
	for (size_t i = 0; i < scene.nodes.size(); ++i) {
		const NodeDescription &n = scene.nodes[i];
		NodePtr node = Node::MakeObject();
		node->Set("name", Str(n.name));
		if (n.mesh >= 0)
			node->Set("mesh", Int(n.mesh));
		node->Set("translation", Floats(n.translation, 3));
		node->Set("rotation", Floats(n.rotation, 4));
		node->Set("scale", Floats(n.scale, 3));
		if (!children[i].empty()) {
			NodePtr list = Node::MakeArray();
			for (int c : children[i])
				list->Push(Int(c));
			node->Set("children", list);
		}
		nodes->Push(node);
		if (n.parent < 0)
			sceneRoots->Push(Int(int64_t(i)));
	}

	// Animations : un échantillonneur par canal.
	NodePtr animations = Node::MakeArray();
	for (const AnimationDescription &a : scene.animations) {
		NodePtr samplers = Node::MakeArray(), channels = Node::MakeArray();
		for (const ChannelDescription &c : a.channels) {
			NodePtr sampler = Node::MakeObject();
			sampler->Set("input", Int(pack.Floats(c.times, 1, "SCALAR", 0, true)));
			sampler->Set("output", Int(pack.Floats(c.values, c.path == "rotation" ? 4 : 3, c.path == "rotation" ? "VEC4" : "VEC3", 0, false)));
			sampler->Set("interpolation", Str(c.interpolation));
			samplers->Push(sampler);
			NodePtr target = Node::MakeObject();
			target->Set("node", Int(c.node));
			target->Set("path", Str(c.path));
			NodePtr channel = Node::MakeObject();
			channel->Set("sampler", Int(int64_t(samplers->GetSize() - 1)));
			channel->Set("target", target);
			channels->Push(channel);
		}
		NodePtr animation = Node::MakeObject();
		animation->Set("name", Str(a.name));
		animation->Set("samplers", samplers);
		animation->Set("channels", channels);
		animations->Push(animation);
	}

	NodePtr sceneNode = Node::MakeObject();
	sceneNode->Set("nodes", sceneRoots);
	NodePtr scenes = Node::MakeArray();
	scenes->Push(sceneNode);
	root->Set("scene", Int(0));
	root->Set("scenes", scenes);
	root->Set("nodes", nodes);
	if (meshes->GetSize() > 0)
		root->Set("meshes", meshes);
	if (materials->GetSize() > 0)
		root->Set("materials", materials);
	if (images->GetSize() > 0) {
		root->Set("images", images);
		root->Set("textures", textures);
		NodePtr sampler = Node::MakeObject();
		sampler->Set("magFilter", Int(NEAREST));
		sampler->Set("minFilter", Int(NEAREST));
		sampler->Set("wrapS", Int(REPEAT));
		sampler->Set("wrapT", Int(REPEAT));
		NodePtr samplers = Node::MakeArray();
		samplers->Push(sampler);
		root->Set("samplers", samplers);
	}
	if (animations->GetSize() > 0)
		root->Set("animations", animations);
	while (pack.bytes.size() % 4 != 0)
		pack.bytes.push_back(0);
	NodePtr buffer = Node::MakeObject();
	buffer->Set("uri", Str(binName));
	buffer->Set("byteLength", Int(int64_t(pack.bytes.size())));
	NodePtr buffers = Node::MakeArray();
	buffers->Push(buffer);
	root->Set("buffers", buffers);
	root->Set("bufferViews", pack.views);
	root->Set("accessors", pack.accessors);

	std::error_code ec;
	if (gltfPath.has_parent_path())
		fs::create_directories(gltfPath.parent_path(), ec);
	{
		std::ofstream bin(binPath, std::ios::binary | std::ios::trunc);
		if (!bin)
			return Some(String::Format("glTF : écriture impossible : %s", binPath.string().c_str()));
		bin.write(reinterpret_cast<const char *>(pack.bytes.data()), std::streamsize(pack.bytes.size()));
		if (!bin)
			return Some(String::Format("glTF : écriture incomplète : %s", binPath.string().c_str()));
	}
	JsonDocument document;
	document.SetRoot(root);
	const String text = document.EncodeStr();
	std::ofstream out(gltfPath, std::ios::binary | std::ios::trunc);
	if (!out)
		return Some(String::Format("glTF : écriture impossible : %s", path.CStr()));
	out.write(text.CStr(), std::streamsize(text.GetSize()));
	if (!out)
		return Some(String::Format("glTF : écriture incomplète : %s", path.CStr()));
	return NONE;
}

} // namespace dataio::gltf
