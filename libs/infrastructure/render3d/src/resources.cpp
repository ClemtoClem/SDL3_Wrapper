// Définitions de render3d/resources.hpp
#include <render3d/resources.hpp>

#include <cstring>

#include <sdl3/image.hpp>
#include <sdl3/iostream.hpp>
#include <sdl3/render.hpp>

namespace render3d {

namespace {

String StrOf(const dataio::NodePtr &node) {
	return node && node->IsString() ? node->stringValue : String();
}

double NumberOf(const dataio::NodePtr &node, double fallback) {
	if (!node)
		return fallback;
	if (node->IsFloat())
		return node->floatValue;
	if (node->IsInt())
		return double(node->intValue);
	return fallback;
}

bool BoolOf(const dataio::NodePtr &node, bool fallback) {
	return node && node->IsBool() ? node->boolValue : fallback;
}

/// Nom affiché d'un chemin : son dernier segment.
String LeafName(const String &path) {
	const size_t slash = path.Rfind('/');
	return slash == String::NPOS ? path : path.Substr(slash + 1);
}

/// Chemin tel qu'il est catalogué : relatif à la base du cache s'il est
/// dessous, séparateurs « / », sans « ./ » de tête.
String Catalogued(const ResourceCache &cache, const String &path) {
	String normalized = path;
	for (size_t i = 0; i < normalized.GetSize(); ++i)
		if (normalized[i] == '\\')
			normalized = normalized.Substr(0, i) + String("/") + normalized.Substr(i + 1);
	while (normalized.StartsWith("./"))
		normalized = normalized.Substr(2);
	String base = cache.BasePath();
	if (!base.IsEmpty() && !base.EndsWith("/"))
		base = base + String("/");
	if (!base.IsEmpty() && normalized.StartsWith(base.View()))
		normalized = normalized.Substr(base.GetSize());
	return normalized;
}

/// Catalogue (si besoin) puis rend la ressource d'un fichier, chargée selon
/// `policy`. `make` crée la ressource non chargée.
template <typename T, typename Make>
AssetRef<T> ImportFile(ResourceCache &cache, const String &path, AssetType type, LoadPolicy policy, Make make) {
	const String source = Catalogued(cache, path);
	const AssetId id = resources::AssetId::FromPath(source.View());
	if (cache.IsResident(id)) {
		if (policy == LoadPolicy::SYNC)
			(void)cache.LoadNow(id);
		return AssetRef<T>{id};
	}
	AssetRecord record;
	record.id = id;
	record.type = type;
	record.name = LeafName(source);
	record.path = source;
	(void)cache.Add(make(source, cache.Resolve(source.View())), std::move(record), policy);
	return AssetRef<T>{id};
}

} // namespace

// ── MeshResource ─────────────────────────────────────────────────────────────

std::shared_ptr<MeshResource> MeshResource::FromFile(String source, String diskPath) {
	std::shared_ptr<MeshResource> mesh(new MeshResource());
	mesh->m_source = std::move(source);
	mesh->m_diskPath = std::move(diskPath);
	return mesh;
}

std::shared_ptr<MeshResource> MeshResource::FromPrimitive(const PrimitiveSpec &spec) {
	std::shared_ptr<MeshResource> mesh(new MeshResource());
	mesh->m_source = spec.Key();
	mesh->m_primitive = Some(spec);
	return mesh;
}

std::shared_ptr<MeshResource> MeshResource::FromGeometry(String name, Mesh geometry) {
	std::shared_ptr<MeshResource> mesh(new MeshResource());
	mesh->m_source = String("mesh:") + name;
	mesh->m_geometry = std::make_shared<Mesh>(std::move(geometry));
	mesh->m_provided = true;
	mesh->MarkReadyInternal();
	return mesh;
}

Result<bool, String> MeshResource::DoLoad() {
	if (m_provided)
		return Ok(true);
	if (m_primitive.IsSome()) {
		m_geometry = std::make_shared<Mesh>(Mesh::FromPrimitive(m_primitive.Value()));
		return Ok(true);
	}
	auto loaded = Mesh::FromGltf(m_diskPath);
	if (loaded.IsError())
		return Err(String::Format("%s : %s", m_source.CStr(), loaded.Error().CStr()));
	m_geometry = std::make_shared<Mesh>(std::move(loaded).Unwrap());
	return Ok(true);
}

void MeshResource::DoUnload() {
	if (!m_provided)
		m_geometry = std::make_shared<Mesh>(); // les Shape gardent l'ancienne
}

// ── TextureResource ──────────────────────────────────────────────────────────

std::shared_ptr<TextureResource> TextureResource::FromFile(String source, String diskPath) {
	std::shared_ptr<TextureResource> texture(new TextureResource());
	texture->m_source = std::move(source);
	texture->m_diskPath = std::move(diskPath);
	return texture;
}

std::shared_ptr<TextureResource> TextureResource::FromPixels(String name, int width, int height,
															   std::vector<uint8_t> rgba) {
	std::shared_ptr<TextureResource> texture(new TextureResource());
	texture->m_source = String("texture:") + name;
	texture->m_width = width;
	texture->m_height = height;
	texture->m_rgba = std::move(rgba);
	texture->m_provided = true;
	texture->MarkReadyInternal();
	return texture;
}

Result<bool, String> TextureResource::DoLoad() {
	if (m_provided)
		return Ok(true);
	auto surface = sdl3::ImgLoad(m_diskPath);
	if (surface.IsError())
		return Err(String::Format("%s : %s", m_source.CStr(), String(surface.Error()).CStr()));
	auto rgba = surface.Value().Convert(sdl3::PixelFormat::RGBA32);
	if (rgba.IsError())
		return Err(String::Format("%s : conversion RGBA impossible (%s)", m_source.CStr(), String(rgba.Error()).CStr()));
	const SDL_Surface *raw = rgba.Value().Get();
	if (!raw || !raw->pixels)
		return Err(String::Format("%s : image vide", m_source.CStr()));
	m_width = raw->w;
	m_height = raw->h;
	m_rgba.resize(size_t(m_width) * size_t(m_height) * 4);
	for (int y = 0; y < m_height; ++y)
		std::memcpy(m_rgba.data() + size_t(y) * size_t(m_width) * 4,
					static_cast<const uint8_t *>(raw->pixels) + size_t(y) * size_t(raw->pitch), size_t(m_width) * 4);
	return Ok(true);
}

void TextureResource::DoUnload() {
	if (m_provided)
		return;
	m_rgba.clear();
	m_rgba.shrink_to_fit();
	m_width = m_height = 0;
}

// ── ShaderResource ───────────────────────────────────────────────────────────

const char *ShaderResource::KindName(ShaderKind kind) noexcept {
	switch (kind) {
		case ShaderKind::BASIC:
			return "basic";
		case ShaderKind::PHONG:
			return "phong";
		case ShaderKind::PBR:
			return "pbr";
	}
	return "phong";
}

std::shared_ptr<ShaderResource> ShaderResource::Builtin(ShaderKind kind) {
	std::shared_ptr<ShaderResource> shader(new ShaderResource());
	shader->m_source = String("shader:") + KindName(kind);
	shader->m_kind = kind;
	shader->MarkReadyInternal();
	return shader;
}

std::shared_ptr<ShaderResource> ShaderResource::FromFiles(String name, String vertexPath, String fragmentPath) {
	std::shared_ptr<ShaderResource> shader(new ShaderResource());
	shader->m_source = String("shader:") + name;
	shader->m_vertexPath = std::move(vertexPath);
	shader->m_fragmentPath = std::move(fragmentPath);
	return shader;
}

Result<bool, String> ShaderResource::DoLoad() {
	if (IsBuiltin())
		return Ok(true);
	auto readText = [](const String &path) -> Result<String, String> {
		auto bytes = sdl3::ReadFile(path);
		if (bytes.IsError())
			return Err(String::Format("%s : %s", path.CStr(), String(bytes.Error()).CStr()));
		const std::vector<uint8_t> &data = bytes.Value();
		return Ok(String(reinterpret_cast<const char *>(data.data()), data.size()));
	};
	auto vertex = readText(m_vertexPath);
	if (vertex.IsError())
		return Err(vertex.Error());
	auto fragment = readText(m_fragmentPath);
	if (fragment.IsError())
		return Err(fragment.Error());
	m_vertexSource = std::move(vertex).Unwrap();
	m_fragmentSource = std::move(fragment).Unwrap();
	return Ok(true);
}

void ShaderResource::DoUnload() {
	m_vertexSource = String();
	m_fragmentSource = String();
}

// ── MaterialResource ─────────────────────────────────────────────────────────

dataio::NodePtr MaterialToJson(const Material &material) {
	auto json = dataio::Node::MakeObject();
	auto color = dataio::Node::MakeArray();
	for (uint8_t channel : {material.baseColor.r, material.baseColor.g, material.baseColor.b, material.baseColor.a})
		color->Push(dataio::Node::MakeInt(channel));
	json->Set("base_color", color);
	json->Set("roughness", dataio::Node::MakeFloat(double(material.roughness)));
	json->Set("metallic", dataio::Node::MakeFloat(double(material.metallic)));
	json->Set("lit", dataio::Node::MakeBool(material.lit));
	json->Set("pbr", dataio::Node::MakeBool(material.pbr));
	json->Set("double_sided", dataio::Node::MakeBool(material.doubleSided));
	json->Set("wireframe", dataio::Node::MakeBool(material.wireframe));
	json->Set("depth_test", dataio::Node::MakeBool(material.depthTest));
	return json;
}

Material MaterialFromJson(const dataio::NodePtr &json) {
	Material material;
	if (!json || !json->IsObject())
		return material;
	if (const dataio::NodePtr color = json->Get("base_color"); color && color->IsArray() && color->GetSize() >= 3) {
		auto channel = [&](size_t i, uint8_t fallback) {
			return uint8_t(sdl3::Clamp(int(NumberOf(color->At(i), fallback)), 0, 255));
		};
		material.baseColor = sdl3::Color{channel(0, 255), channel(1, 255), channel(2, 255), channel(3, 255)};
	}
	material.roughness = float(NumberOf(json->Get("roughness"), material.roughness));
	material.metallic = float(NumberOf(json->Get("metallic"), material.metallic));
	material.lit = BoolOf(json->Get("lit"), material.lit);
	material.pbr = BoolOf(json->Get("pbr"), material.pbr);
	material.doubleSided = BoolOf(json->Get("double_sided"), material.doubleSided);
	material.wireframe = BoolOf(json->Get("wireframe"), material.wireframe);
	material.depthTest = BoolOf(json->Get("depth_test"), material.depthTest);
	return material;
}

std::shared_ptr<MaterialResource> MaterialResource::Create(String name, Material material,
														   AssetRef<TextureResource> albedo,
														   AssetRef<ShaderResource> shader) {
	std::shared_ptr<MaterialResource> resource(new MaterialResource());
	resource->m_source = String("material:") + name;
	resource->Set(std::move(material), albedo, shader);
	resource->MarkReadyInternal();
	return resource;
}

void MaterialResource::Set(Material material, AssetRef<TextureResource> albedo, AssetRef<ShaderResource> shader) {
	material.albedo = NONE; // une RÉFÉRENCE (m_albedo), pas une texture GPU possédée
	m_material = std::move(material);
	m_albedo = albedo;
	m_shader = shader;
}

dataio::NodePtr MaterialResource::ToJson() const {
	dataio::NodePtr json = MaterialToJson(m_material);
	if (m_albedo.IsValid())
		json->Set("albedo", dataio::Node::MakeString(m_albedo.ToString()));
	if (m_shader.IsValid())
		json->Set("shader", dataio::Node::MakeString(m_shader.ToString()));
	return json;
}

// ── Fabriques ────────────────────────────────────────────────────────────────

void RegisterResourceFactories(ResourceCache &cache) {
	cache.RegisterFactory(AssetType::MESH, [](const AssetRecord &record,
											  const ResourceCache &owner) -> std::shared_ptr<resources::Resource> {
		if (auto spec = PrimitiveSpec::FromKey(record.key.View()); spec.IsSome())
			return MeshResource::FromPrimitive(spec.Unwrap());
		if (record.path.IsEmpty())
			return nullptr; // maillage généré : ne se recrée pas
		return MeshResource::FromFile(record.path, owner.Resolve(record.path.View()));
	});
	cache.RegisterFactory(AssetType::TEXTURE, [](const AssetRecord &record,
												 const ResourceCache &owner) -> std::shared_ptr<resources::Resource> {
		if (record.path.IsEmpty())
			return nullptr;
		return TextureResource::FromFile(record.path, owner.Resolve(record.path.View()));
	});
	cache.RegisterFactory(AssetType::SHADER, [](const AssetRecord &record,
												const ResourceCache &owner) -> std::shared_ptr<resources::Resource> {
		for (ShaderKind kind : {ShaderKind::BASIC, ShaderKind::PHONG, ShaderKind::PBR})
			if (record.key == String("shader:") + ShaderResource::KindName(kind))
				return ShaderResource::Builtin(kind);
		const String vertex = StrOf(record.params ? record.params->Get("vertex") : nullptr);
		const String fragment = StrOf(record.params ? record.params->Get("fragment") : nullptr);
		if (vertex.IsEmpty() || fragment.IsEmpty())
			return nullptr;
		return ShaderResource::FromFiles(record.name, owner.Resolve(vertex.View()), owner.Resolve(fragment.View()));
	});
	cache.RegisterFactory(AssetType::MATERIAL, [](const AssetRecord &record,
												  const ResourceCache &) -> std::shared_ptr<resources::Resource> {
		const dataio::NodePtr params = record.params;
		return MaterialResource::Create(
			record.name, MaterialFromJson(params),
			AssetRef<TextureResource>::FromString(StrOf(params ? params->Get("albedo") : nullptr).View()),
			AssetRef<ShaderResource>::FromString(StrOf(params ? params->Get("shader") : nullptr).View()));
	});
}

AssetRef<MeshResource> ImportMesh(ResourceCache &cache, const String &path, LoadPolicy policy) {
	return ImportFile<MeshResource>(cache, path, AssetType::MESH, policy, [](String source, String disk) {
		return MeshResource::FromFile(std::move(source), std::move(disk));
	});
}

AssetRef<MeshResource> Primitive(ResourceCache &cache, const PrimitiveSpec &spec, LoadPolicy policy) {
	const String key = spec.Key();
	const AssetId id = resources::AssetId::FromKey(key.View());
	if (!cache.IsResident(id)) {
		AssetRecord record;
		record.id = id;
		record.type = AssetType::MESH;
		record.name = key.Substr(10); // « box 2 4.6 0.6 »
		record.key = key;
		(void)cache.Add(MeshResource::FromPrimitive(spec), std::move(record), policy);
	} else if (policy == LoadPolicy::SYNC) {
		(void)cache.LoadNow(id);
	}
	return AssetRef<MeshResource>{id};
}

AssetRef<MeshResource> AddMesh(ResourceCache &cache, String name, Mesh geometry) {
	AssetRecord record;
	record.id = AssetId::Generate();
	record.type = AssetType::MESH;
	record.name = name;
	return AssetRef<MeshResource>{cache.Add(MeshResource::FromGeometry(std::move(name), std::move(geometry)),
											std::move(record), LoadPolicy::SYNC)};
}

AssetRef<TextureResource> ImportTexture(ResourceCache &cache, const String &path, LoadPolicy policy) {
	return ImportFile<TextureResource>(cache, path, AssetType::TEXTURE, policy, [](String source, String disk) {
		return TextureResource::FromFile(std::move(source), std::move(disk));
	});
}

AssetRef<TextureResource> AddTexture(ResourceCache &cache, String name, int width, int height,
									 std::vector<uint8_t> rgba) {
	AssetRecord record;
	record.id = AssetId::Generate();
	record.type = AssetType::TEXTURE;
	record.name = name;
	return AssetRef<TextureResource>{
		cache.Add(TextureResource::FromPixels(std::move(name), width, height, std::move(rgba)), std::move(record),
				  LoadPolicy::SYNC)};
}

AssetRef<MaterialResource> CreateMaterial(ResourceCache &cache, String name, Material material,
										  AssetRef<TextureResource> albedo, AssetRef<ShaderResource> shader) {
	const String key = String("material:") + name;
	const AssetId id = resources::AssetId::FromKey(key.View());
	std::shared_ptr<MaterialResource> resource = MaterialResource::Create(name, std::move(material), albedo, shader);
	AssetRecord record;
	record.id = id;
	record.type = AssetType::MATERIAL;
	record.name = std::move(name);
	record.key = key;
	record.params = resource->ToJson();
	(void)cache.Add(std::move(resource), std::move(record), LoadPolicy::SYNC);
	return AssetRef<MaterialResource>{id};
}

AssetRef<ShaderResource> BuiltinShader(ResourceCache &cache, ShaderKind kind) {
	const String key = String("shader:") + ShaderResource::KindName(kind);
	const AssetId id = resources::AssetId::FromKey(key.View());
	if (!cache.IsResident(id)) {
		AssetRecord record;
		record.id = id;
		record.type = AssetType::SHADER;
		record.name = String(ShaderResource::KindName(kind));
		record.key = key;
		(void)cache.Add(ShaderResource::Builtin(kind), std::move(record), LoadPolicy::SYNC);
	}
	return AssetRef<ShaderResource>{id};
}

} // namespace render3d
