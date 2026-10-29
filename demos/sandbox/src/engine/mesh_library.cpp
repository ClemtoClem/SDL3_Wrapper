// Définitions de mesh_library.hpp
#include <sandbox/engine/mesh_library.hpp>

#include <modeling/io.hpp>
#include <modeling/triangulate.hpp>

#include <filesystem>

namespace sandbox {

namespace {

std::string Key(const String &source) { return std::string(source.CStr()); }

sdl3::Color ToColor(const modeling::Color &c) {
	auto byte = [](float v) { return uint8_t(math::Clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
	return sdl3::Color{byte(c.r), byte(c.g), byte(c.b), byte(c.a)};
}

} // namespace

void MeshLibrary::SetRoot(const String &directory) { m_root = directory; }

String MeshLibrary::Resolve(const String &source) const {
	if (m_root.IsEmpty() || source.IsEmpty() || source.CStr()[0] == '/')
		return source;
	return m_root.EndsWith("/") ? m_root + source : m_root + "/" + source;
}

Result<modeling::Model *, String> MeshLibrary::Get(const String &source) {
	if (source.IsEmpty())
		return Err(String("maillage modélisé sans fichier source"));
	auto it = m_entries.find(Key(source));
	if (it != m_entries.end())
		return Ok(&it->second.model);
	Entry entry;
	const String path = Resolve(source);
	if (std::filesystem::exists(path.CStr())) {
		Result<modeling::Model, String> loaded = modeling::LoadModel(path);
		if (loaded.IsError())
			return Err(loaded.Error());
		entry.model = std::move(loaded).Unwrap();
	} else {
		entry.dirty = true; // nouveau maillage : à créer au prochain enregistrement
	}
	entry.savedRevision = entry.model.Revision();
	auto [inserted, ok] = m_entries.emplace(Key(source), std::move(entry));
	(void)ok;
	return Ok(&inserted->second.model);
}

bool MeshLibrary::IsLoaded(const String &source) const { return m_entries.contains(Key(source)); }

void MeshLibrary::MarkDirty(const String &source) {
	if (auto it = m_entries.find(Key(source)); it != m_entries.end())
		it->second.dirty = true;
}

bool MeshLibrary::IsDirty(const String &source) const {
	auto it = m_entries.find(Key(source));
	return it != m_entries.end() && (it->second.dirty || it->second.model.Revision() != it->second.savedRevision);
}

void MeshLibrary::Replace(const String &source, modeling::Model model) {
	Entry &entry = m_entries[Key(source)];
	entry.model = std::move(model);
	entry.model.Touch();
	entry.dirty = true;
}

std::shared_ptr<render3d::Mesh> MeshLibrary::RenderMesh(const String &source) {
	Result<modeling::Model *, String> model = Get(source);
	if (model.IsError())
		return nullptr;
	Entry &entry = m_entries[Key(source)];
	if (!entry.mesh || entry.builtRevision != entry.model.Revision()) {
		entry.mesh = std::make_shared<render3d::Mesh>(ToRenderMesh(entry.model));
		entry.builtRevision = entry.model.Revision();
	}
	return entry.mesh;
}

Option<String> MeshLibrary::Save(const String &source) {
	auto it = m_entries.find(Key(source));
	if (it == m_entries.end())
		return NONE;
	const String path = Resolve(source);
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(path.CStr()).parent_path(), ec);
	if (Option<String> err = modeling::SaveModel(path, it->second.model); err.IsSome())
		return err;
	it->second.dirty = false;
	it->second.savedRevision = it->second.model.Revision();
	return NONE;
}

std::vector<String> MeshLibrary::SaveAll() {
	std::vector<String> errors;
	for (auto &[key, entry] : m_entries)
		if (entry.dirty || entry.model.Revision() != entry.savedRevision)
			if (Option<String> err = Save(String(key.c_str())); err.IsSome())
				errors.push_back(err.Unwrap());
	return errors;
}

void MeshLibrary::MarkAllDirty() {
	for (auto &[key, entry] : m_entries)
		entry.dirty = true;
}

void MeshLibrary::Clear() { m_entries.clear(); }

String MeshLibrary::UniqueSource(const String &baseName) const {
	// Nom de fichier sûr : lettres (accents compris), chiffres, tirets ; le reste devient « _ ».
	String clean;
	for (size_t i = 0; i < baseName.GetSize(); ++i) {
		const char c = baseName.CharAt(i);
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || static_cast<unsigned char>(c) >= 0x80;
		clean += ok ? c : '_';
	}
	if (clean.IsEmpty())
		clean = String("maillage");
	for (int i = 1;; ++i) {
		const String source = i == 1 ? String::Format("meshes/%s.mesh", clean.CStr())
									 : String::Format("meshes/%s_%d.mesh", clean.CStr(), i);
		if (!IsLoaded(source) && !std::filesystem::exists(Resolve(source).CStr()))
			return source;
	}
}

render3d::Mesh MeshLibrary::ToRenderMesh(const modeling::Model &model) {
	// Faces vues des deux côtés : un modèle en cours de construction a
	// souvent des faces isolées, que l'on doit voir sous tous les angles.
	modeling::BuildOptions options;
	options.doubleSided = true;
	options.defaultColor = {1.f, 1.f, 1.f, 1.f}; // la couleur du nœud s'applique telle quelle
	const modeling::MeshData data = modeling::BuildMesh(model, options);
	std::vector<render3d::Vertex3D> vertices;
	vertices.reserve(data.positions.size());
	for (size_t i = 0; i < data.positions.size(); ++i)
		vertices.push_back(render3d::Vertex3D{data.positions[i], data.normals[i], data.uvs[i], ToColor(data.colors[i])});
	return render3d::Mesh(std::move(vertices), data.indices);
}

modeling::Model MeshLibrary::FromPrimitive(const render3d::PrimitiveSpec &spec) {
	const render3d::Mesh mesh = render3d::Mesh::FromPrimitive(spec);
	std::vector<math::FVector3> positions;
	positions.reserve(mesh.Vertices().size());
	for (const render3d::Vertex3D &v : mesh.Vertices())
		positions.push_back(v.position);
	return modeling::FromTriangles(positions, mesh.Indices());
}

} // namespace sandbox
