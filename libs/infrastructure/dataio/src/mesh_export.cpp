// Définitions de dataio/mesh_export.hpp
#include <dataio/mesh_export.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace dataio {

namespace {

void Append(String &out, const char *fmt, auto... args) { out += String::Format(fmt, args...); }

struct Normal {
	float x, y, z;
};

Normal TriangleNormal(const TriangleMeshView &m, size_t t) {
	const float *a = &m.positions[size_t(m.indices[t * 3]) * 3];
	const float *b = &m.positions[size_t(m.indices[t * 3 + 1]) * 3];
	const float *c = &m.positions[size_t(m.indices[t * 3 + 2]) * 3];
	const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
	const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
	Normal n{uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
	const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
	if (len > 1e-20f)
		n = {n.x / len, n.y / len, n.z / len};
	return n;
}

String BaseName(const String &path) {
	const size_t slash = path.Rfind('/');
	return slash == String::NPOS ? path : path.Substr(slash + 1);
}

Option<String> WriteBytes(const String &path, const void *data, size_t size) {
	std::FILE *file = std::fopen(path.CStr(), "wb");
	if (!file)
		return Some(String::Format("impossible d'écrire %s", path.CStr()));
	const size_t written = size ? std::fwrite(data, 1, size, file) : 0;
	const bool ok = std::fclose(file) == 0 && written == size;
	return ok ? Option<String>(NONE) : Some(String::Format("écriture incomplète de %s", path.CStr()));
}

} // namespace

String EncodeObj(const TriangleMeshView &m, const ObjOptions &options) {
	String out;
	if (!options.comment.IsEmpty())
		Append(out, "# %s\n", options.comment.CStr());
	if (!options.mtlFileName.IsEmpty() && !m.materials.empty())
		Append(out, "mtllib %s\n", options.mtlFileName.CStr());
	const size_t n = m.VertexCount();
	const bool withColor = m.colors.size() == n * 3;
	const bool withUv = m.uvs.size() == n * 2;
	const bool withNormal = m.normals.size() == n * 3;
	for (size_t i = 0; i < n; ++i) {
		if (withColor)
			Append(out, "v %g %g %g %.3f %.3f %.3f\n", m.positions[i * 3], m.positions[i * 3 + 1], m.positions[i * 3 + 2],
				   m.colors[i * 3] / 255.0, m.colors[i * 3 + 1] / 255.0, m.colors[i * 3 + 2] / 255.0);
		else
			Append(out, "v %g %g %g\n", m.positions[i * 3], m.positions[i * 3 + 1], m.positions[i * 3 + 2]);
	}
	if (withUv)
		for (size_t i = 0; i < n; ++i)
			Append(out, "vt %g %g\n", m.uvs[i * 2], m.uvs[i * 2 + 1]);
	if (withNormal)
		for (size_t i = 0; i < n; ++i)
			Append(out, "vn %g %g %g\n", m.normals[i * 3], m.normals[i * 3 + 1], m.normals[i * 3 + 2]);
	auto corner = [&](uint32_t index) {
		const uint32_t k = index + 1;
		if (withUv && withNormal)
			return String::Format("%u/%u/%u", k, k, k);
		if (withUv)
			return String::Format("%u/%u", k, k);
		if (withNormal)
			return String::Format("%u//%u", k, k);
		return String::Format("%u", k);
	};
	auto faces = [&](size_t first, size_t count) {
		for (size_t i = first; i + 2 < first + count && i + 2 < m.indices.size(); i += 3)
			Append(out, "f %s %s %s\n", corner(m.indices[i]).CStr(), corner(m.indices[i + 1]).CStr(),
				   corner(m.indices[i + 2]).CStr());
	};
	if (m.materials.empty()) {
		faces(0, m.indices.size());
	} else {
		for (const MeshMaterialGroup &g : m.materials) {
			if (!options.mtlFileName.IsEmpty())
				Append(out, "usemtl %s\n", g.name.CStr());
			faces(g.firstIndex, g.indexCount);
		}
	}
	return out;
}

String EncodeMtl(std::span<const MeshMaterialGroup> materials) {
	String out;
	std::vector<String> seen;
	for (const MeshMaterialGroup &g : materials) {
		bool dup = false;
		for (const String &s : seen)
			dup = dup || s == g.name;
		if (dup)
			continue;
		seen.push_back(g.name);
		Append(out, "newmtl %s\nKd %.4f %.4f %.4f\nd %.4f\n\n", g.name.CStr(), g.r, g.g, g.b, g.a);
	}
	return out;
}

String EncodeStlAscii(const TriangleMeshView &m, const String &solidName) {
	String out;
	Append(out, "solid %s\n", solidName.CStr());
	for (size_t t = 0; t < m.TriangleCount(); ++t) {
		const Normal n = TriangleNormal(m, t);
		Append(out, "  facet normal %g %g %g\n    outer loop\n", n.x, n.y, n.z);
		for (int k = 0; k < 3; ++k) {
			const float *p = &m.positions[size_t(m.indices[t * 3 + size_t(k)]) * 3];
			Append(out, "      vertex %g %g %g\n", p[0], p[1], p[2]);
		}
		out += "    endloop\n  endfacet\n";
	}
	Append(out, "endsolid %s\n", solidName.CStr());
	return out;
}

std::vector<uint8_t> EncodeStlBinary(const TriangleMeshView &m) {
	const uint32_t count = uint32_t(m.TriangleCount());
	std::vector<uint8_t> out(80 + 4 + size_t(count) * 50, 0);
	const char header[] = "STL binaire - dataio::EncodeStlBinary";
	std::memcpy(out.data(), header, sizeof(header) - 1);
	auto put32 = [&](size_t at, const void *v) { std::memcpy(out.data() + at, v, 4); };
	put32(80, &count); // petit-boutiste, comme l'exige le format (hôte x86/ARM)
	for (uint32_t t = 0; t < count; ++t) {
		const size_t at = 84 + size_t(t) * 50;
		const Normal n = TriangleNormal(m, t);
		put32(at, &n.x);
		put32(at + 4, &n.y);
		put32(at + 8, &n.z);
		for (int k = 0; k < 3; ++k) {
			const float *p = &m.positions[size_t(m.indices[size_t(t) * 3 + size_t(k)]) * 3];
			for (int c = 0; c < 3; ++c)
				put32(at + 12 + size_t(k) * 12 + size_t(c) * 4, &p[c]);
		}
	}
	return out;
}

String EncodePly(const TriangleMeshView &m) {
	const size_t n = m.VertexCount();
	const bool withColor = m.colors.size() == n * 3;
	const bool withNormal = m.normals.size() == n * 3;
	String out;
	Append(out, "ply\nformat ascii 1.0\nelement vertex %zu\nproperty float x\nproperty float y\nproperty float z\n", n);
	if (withNormal)
		out += "property float nx\nproperty float ny\nproperty float nz\n";
	if (withColor)
		out += "property uchar red\nproperty uchar green\nproperty uchar blue\n";
	Append(out, "element face %zu\nproperty list uchar int vertex_indices\nend_header\n", m.TriangleCount());
	for (size_t i = 0; i < n; ++i) {
		Append(out, "%g %g %g", m.positions[i * 3], m.positions[i * 3 + 1], m.positions[i * 3 + 2]);
		if (withNormal)
			Append(out, " %g %g %g", m.normals[i * 3], m.normals[i * 3 + 1], m.normals[i * 3 + 2]);
		if (withColor)
			Append(out, " %u %u %u", unsigned(m.colors[i * 3]), unsigned(m.colors[i * 3 + 1]), unsigned(m.colors[i * 3 + 2]));
		out += "\n";
	}
	for (size_t t = 0; t < m.TriangleCount(); ++t)
		Append(out, "3 %u %u %u\n", m.indices[t * 3], m.indices[t * 3 + 1], m.indices[t * 3 + 2]);
	return out;
}

Option<String> SaveMesh(const String &path, const TriangleMeshView &mesh, const String &comment) {
	const String lower = path.ToLower();
	if (lower.EndsWith(".stl")) {
		const std::vector<uint8_t> bytes = EncodeStlBinary(mesh);
		return WriteBytes(path, bytes.data(), bytes.size());
	}
	if (lower.EndsWith(".ply")) {
		const String text = EncodePly(mesh);
		return WriteBytes(path, text.CStr(), text.GetSize());
	}
	if (lower.EndsWith(".obj")) {
		ObjOptions options;
		options.comment = comment;
		if (!mesh.materials.empty()) {
			options.mtlFileName = BaseName(path.Substr(0, path.GetSize() - 4) + ".mtl");
			const String mtl = EncodeMtl(mesh.materials);
			const String mtlPath = path.Substr(0, path.GetSize() - 4) + ".mtl";
			if (Option<String> err = WriteBytes(mtlPath, mtl.CStr(), mtl.GetSize()); err.IsSome())
				return err;
		}
		const String text = EncodeObj(mesh, options);
		return WriteBytes(path, text.CStr(), text.GetSize());
	}
	return Some(String::Format("format d'export inconnu pour %s (.obj, .stl, .ply)", path.CStr()));
}

} // namespace dataio
