// Définitions de voxel/rig.hpp
#include <voxel/json.hpp>
#include <voxel/rig.hpp>

#include <algorithm>
#include <cmath>

namespace voxel {

namespace {

constexpr float DEG = 3.14159265f / 180.f;

std::optional<RigMotion> Motion(const NodePtr &node) {
	if (!node || !node->IsArray() || node->GetSize() < 3)
		return std::nullopt;
	RigMotion m;
	const String axis = json::Str(node->At(0), "", "x").ToLower();
	m.axis = axis == "y" ? 1 : axis == "z" ? 2 : 0;
	m.phase = float(json::Num(node->At(1), "", 0.0));
	m.amplitude = float(json::Num(node->At(2), "", 0.0));
	return m;
}

math::FVector3 Vec(const NodePtr &node, const char *key, math::FVector3 fallback) { return json::Vec3(node, key, fallback); }

/// Une face : quatre coins vus de l'extérieur (haut-gauche, haut-droit,
/// bas-droit, bas-gauche) et son rectangle de texture en pixels.
void Quad(ModelMesh &m, const std::array<math::FVector3, 4> &c, math::FVector3 normal, float u0, float v0, float u1, float v1, bool mirror,
		  float width, float height) {
	if (mirror)
		std::swap(u0, u1);
	const uint32_t base = uint32_t(m.positions.size());
	const std::array<math::FVector2, 4> uv{math::FVector2{u0 / width, v0 / height}, math::FVector2{u1 / width, v0 / height},
										   math::FVector2{u1 / width, v1 / height}, math::FVector2{u0 / width, v1 / height}};
	for (int i = 0; i < 4; ++i) {
		m.positions.push_back(c[size_t(i)]);
		m.normals.push_back(normal);
		m.uvs.push_back(uv[size_t(i)]);
	}
	for (uint32_t i : {0u, 3u, 2u, 0u, 2u, 1u})
		m.indices.push_back(base + i);
}

} // namespace

int RigModel::Find(const String &name) const {
	for (size_t i = 0; i < parts.size(); ++i)
		if (parts[i].name == name)
			return int(i);
	return -1;
}

Result<RigModel, String> RigFromJson(const NodePtr &def) {
	RigModel rig;
	if (NodePtr size = json::At(def, "textureSize"); size && size->IsArray() && size->GetSize() >= 2) {
		rig.textureWidth = std::max(1, int(json::Num(size->At(0), "", 64.0)));
		rig.textureHeight = std::max(1, int(json::Num(size->At(1), "", 32.0)));
	}
	rig.scale = float(json::Num(def, "scale", 1.0 / 16.0));
	rig.texture = json::Str(def, "texture");
	NodePtr parts = json::At(def, "parts");
	if (!parts || !parts->IsArray() || parts->GetSize() == 0)
		return Err(String("rig : « parts » (tableau non vide) attendu"));
	for (size_t i = 0; i < parts->GetSize(); ++i) {
		NodePtr p = parts->At(i);
		RigPart part;
		part.name = json::Str(p, "name", String::Format("part%d", int(i)));
		part.parent = json::Str(p, "parent");
		part.pivot = Vec(p, "pivot", part.pivot);
		part.rotation = Vec(p, "rotation", part.rotation);
		part.walk = Motion(json::At(p, "walk"));
		part.idle = Motion(json::At(p, "idle"));
		part.look = json::Bool(p, "look", false);
		NodePtr boxes = json::At(p, "boxes");
		for (size_t k = 0; boxes && boxes->IsArray() && k < boxes->GetSize(); ++k) {
			NodePtr b = boxes->At(k);
			RigBox box;
			box.from = Vec(b, "from", box.from);
			box.size = Vec(b, "size", box.size);
			box.inflate = float(json::Num(b, "inflate", 0.0));
			box.mirror = json::Bool(b, "mirror", false);
			box.only64 = json::Bool(b, "only64", false);
			if (NodePtr uv = json::At(b, "uv"); uv && uv->IsArray() && uv->GetSize() >= 2) {
				box.u = int(json::Num(uv->At(0), "", 0.0));
				box.v = int(json::Num(uv->At(1), "", 0.0));
			}
			if (NodePtr uv = json::At(b, "uv64"); uv && uv->IsArray() && uv->GetSize() >= 2)
				box.uv64 = std::pair{int(json::Num(uv->At(0), "", 0.0)), int(json::Num(uv->At(1), "", 0.0))};
			part.boxes.push_back(box);
		}
		if (!part.parent.IsEmpty() && rig.Find(part.parent) < 0)
			return Err(String::Format("rig : partie « %s » : parent « %s » inconnu (il doit la précéder)", part.name.CStr(),
									  part.parent.CStr()));
		rig.parts.push_back(std::move(part));
	}
	return Ok(std::move(rig));
}

RigModel RigFromMesh(ModelMesh mesh, const String &texture) {
	RigModel rig;
	rig.scale = 1.f;
	rig.unitHeight = true;
	rig.texture = texture;
	const Box b = mesh.Bounds();
	const float height = std::max(b.Size().y, 1e-6f);
	const math::FVector3 base{(b.min.x + b.max.x) * 0.5f, b.min.y, (b.min.z + b.max.z) * 0.5f};
	for (math::FVector3 &p : mesh.positions)
		p = (p - base) * (1.f / height);
	RigPart part;
	part.name = "body";
	part.mesh = std::move(mesh);
	part.walk = RigMotion{2, 0.f, 5.f}; // dandinement
	rig.parts.push_back(std::move(part));
	return rig;
}

ModelMesh BuildRigPart(const RigModel &rig, const RigPart &part, int width, int height) {
	ModelMesh m;
	const float s = rig.scale;
	if (!part.mesh.positions.empty()) {
		m = part.mesh;
		for (math::FVector3 &p : m.positions)
			p = (p - part.pivot) * s;
		return m;
	}
	// Les UV sont écrites en pixels de la taille DÉCLARÉE ; un skin plus grand
	// (64×64 au lieu de 64×32) garde la même échelle horizontale.
	const float w = float(width), h = float(height);
	const bool tall = height >= rig.textureHeight * 2 && rig.textureHeight * 2 == rig.textureWidth;
	const float texScale = float(width) / float(rig.textureWidth);
	for (const RigBox &box : part.boxes) {
		const float x0 = (box.from.x - box.inflate - part.pivot.x) * s, x1 = (box.from.x + box.size.x + box.inflate - part.pivot.x) * s;
		const float y0 = (box.from.y - box.inflate - part.pivot.y) * s, y1 = (box.from.y + box.size.y + box.inflate - part.pivot.y) * s;
		const float z0 = (box.from.z - box.inflate - part.pivot.z) * s, z1 = (box.from.z + box.size.z + box.inflate - part.pivot.z) * s;
		if (box.only64 && !tall)
			continue;
		int u = box.u, v = box.v;
		bool mirror = box.mirror;
		if (tall && box.uv64) {
			u = box.uv64->first;
			v = box.uv64->second;
			mirror = false;
		}
		const float fu = float(u) * texScale, fv = float(v) * texScale;
		const float dw = box.size.x * texScale, dh = box.size.y * texScale, dd = box.size.z * texScale;
		// Côtés droit (−x) et gauche (+x) du personnage ; le miroir les échange.
		const float rightU = mirror ? fu + dd + dw : fu, leftU = mirror ? fu : fu + dd + dw;
		Quad(m, {math::FVector3{x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1}}, {0.f, 0.f, 1.f}, fu + dd, fv + dd, fu + dd + dw,
			 fv + dd + dh, mirror, w, h); // face (+z)
		Quad(m, {math::FVector3{x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0}}, {-1.f, 0.f, 0.f}, rightU, fv + dd, rightU + dd,
			 fv + dd + dh, mirror, w, h);
		Quad(m, {math::FVector3{x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1}}, {1.f, 0.f, 0.f}, leftU, fv + dd, leftU + dd,
			 fv + dd + dh, mirror, w, h);
		Quad(m, {math::FVector3{x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0}}, {0.f, 0.f, -1.f}, fu + 2.f * dd + dw, fv + dd,
			 fu + 2.f * dd + 2.f * dw, fv + dd + dh, mirror, w, h); // dos
		Quad(m, {math::FVector3{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}, {0.f, 1.f, 0.f}, fu + dd, fv, fu + dd + dw, fv + dd,
			 mirror, w, h); // dessus
		Quad(m, {math::FVector3{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}, {0.f, -1.f, 0.f}, fu + dd + dw, fv,
			 fu + dd + 2.f * dw, fv + dd, mirror, w, h); // dessous
	}
	return m;
}

math::FVector3 PoseRigPart(const RigPart &part, float walkPhase, float walkAmount, float time, float lookPitch) {
	float r[3] = {part.rotation.x, part.rotation.y, part.rotation.z};
	if (part.walk)
		r[part.walk->axis] += part.walk->amplitude * std::sin(walkPhase + part.walk->phase * DEG) * walkAmount;
	if (part.idle)
		r[part.idle->axis] += part.idle->amplitude * std::sin(time * 2.5f + part.idle->phase * DEG);
	if (part.look)
		r[0] -= lookPitch / DEG;
	return {r[0], r[1], r[2]};
}

} // namespace voxel
