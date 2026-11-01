// Définitions de voxelia/studio/rig_document.hpp
#include <voxelia/studio/rig_document.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>

#include <dataio/documents/gltf.hpp>
#include <dataio/documents/gltf_writer.hpp>
#include <dataio/mesh_export.hpp>
#include <voxel/json.hpp>

namespace voxelia::studio {

namespace json = voxel::json;
using dataio::Node;
using dataio::NodePtr;

namespace {

NodePtr Vec(const math::FVector3 &v) {
	NodePtr a = Node::MakeArray();
	for (float c : {v.x, v.y, v.z})
		a->Push(json::MakeNum(double(c)));
	return a;
}

NodePtr Pair(int a, int b) {
	NodePtr n = Node::MakeArray();
	n->Push(json::MakeInt(a));
	n->Push(json::MakeInt(b));
	return n;
}

std::vector<NodePtr> Items(const NodePtr &array) {
	std::vector<NodePtr> out;
	for (size_t i = 0; array && array->IsArray() && i < array->GetSize(); ++i)
		out.push_back(array->At(i));
	return out;
}

NodePtr Array(const std::vector<NodePtr> &items) {
	NodePtr a = Node::MakeArray();
	for (const NodePtr &n : items)
		a->Push(n);
	return a;
}

/// Un parent avant ses enfants (ordre stable sinon) — exigé par RigFromJson.
NodePtr Topological(const std::vector<NodePtr> &parts) {
	std::vector<NodePtr> out, pending = parts;
	std::vector<String> placed;
	while (!pending.empty()) {
		bool progress = false;
		for (auto it = pending.begin(); it != pending.end();) {
			const String parent = json::Str(*it, "parent");
			if (parent.IsEmpty() || std::find(placed.begin(), placed.end(), parent) != placed.end()) {
				placed.push_back(json::Str(*it, "name"));
				out.push_back(*it);
				it = pending.erase(it);
				progress = true;
			} else {
				++it;
			}
		}
		if (!progress) { // parent introuvable : laissé tel quel (Validate le signale)
			out.insert(out.end(), pending.begin(), pending.end());
			break;
		}
	}
	return Array(out);
}

bool ValidName(const String &name) {
	if (name.IsEmpty() || name.GetSize() > 64)
		return false;
	for (size_t i = 0; i < name.GetSize(); ++i) {
		const char c = name[i];
		if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
			return false;
	}
	return true;
}

voxel::RigBox BoxFrom(const NodePtr &b) {
	voxel::RigBox box;
	box.from = json::Vec3(b, "from", box.from);
	box.size = json::Vec3(b, "size", box.size);
	box.inflate = float(json::Num(b, "inflate", 0.0));
	box.mirror = json::Bool(b, "mirror", false);
	box.only64 = json::Bool(b, "only64", false);
	if (NodePtr uv = json::At(b, "uv"); uv && uv->IsArray() && uv->GetSize() >= 2) {
		box.u = int(json::Num(uv->At(0), "", 0.0));
		box.v = int(json::Num(uv->At(1), "", 0.0));
	}
	return box;
}

NodePtr BoxNode(const voxel::RigBox &box, const NodePtr &previous) {
	NodePtr b = previous ? previous->Clone() : Node::MakeObject();
	b->Set("from", Vec(box.from));
	b->Set("size", Vec(box.size));
	b->Set("uv", Pair(box.u, box.v));
	if (box.inflate != 0.f)
		b->Set("inflate", json::MakeNum(double(box.inflate)));
	else if (b->Has("inflate"))
		b->Remove("inflate");
	if (box.mirror)
		b->Set("mirror", json::MakeBool(true));
	else if (b->Has("mirror"))
		b->Remove("mirror");
	return b;
}

} // namespace

const char *RigChannelId(voxel::RigChannel channel) noexcept {
	return channel == voxel::RigChannel::POSITION ? "position" : "rotation";
}

const char *RigInterpId(voxel::RigInterp interp) noexcept {
	switch (interp) {
	case voxel::RigInterp::STEP:
		return "step";
	case voxel::RigInterp::SMOOTH:
		return "smooth";
	case voxel::RigInterp::BEZIER:
		return "bezier";
	case voxel::RigInterp::LINEAR:
		break;
	}
	return "linear";
}

math::FVector3 RigEulerFromQuaternion(const math::FQuaternion &q) {
	// Inverse de RigRotation (q = Rz · Ry · Rx) par la matrice de rotation.
	const float m00 = 1.f - 2.f * (q.y * q.y + q.z * q.z), m10 = 2.f * (q.x * q.y + q.w * q.z);
	const float m20 = 2.f * (q.x * q.z - q.w * q.y), m21 = 2.f * (q.y * q.z + q.w * q.x), m22 = 1.f - 2.f * (q.x * q.x + q.y * q.y);
	constexpr float DEG = 180.f / 3.14159265358979f;
	// atan2 plutôt qu'asin : bien conditionné près de ±90° (asin y perd ~0,02°).
	const float y = std::atan2(-m20, std::sqrt(m21 * m21 + m22 * m22));
	if (std::fabs(m20) > 0.9999f) {
		// Blocage de cardan (tangage ±90°) : x et z se confondent — z = 0,
		// x lu sur la première ligne (Ry(±90°) · Rx).
		const float m01 = 2.f * (q.x * q.y - q.w * q.z), m11 = 1.f - 2.f * (q.x * q.x + q.z * q.z);
		return {std::atan2(m20 < 0.f ? m01 : -m01, m11) * DEG, y * DEG, 0.f};
	}
	return {std::atan2(m21, m22) * DEG, y * DEG, std::atan2(m10, m00) * DEG};
}

// ── Création ─────────────────────────────────────────────────────────────────

Option<RigDocument> RigDocument::FromModel(const voxel::ContentRegistry &content, const String &modelId) {
	const voxel::ModelDef *model = content.FindModel(modelId);
	if (!model || model->format != "rig" || !model->source)
		return NONE;
	RigDocument doc;
	doc.m_json = model->source->Clone();
	doc.m_id = model->id;
	doc.m_mod = model->mod;
	doc.m_baseDir = model->baseDir;
	doc.m_file = model->sourceFile;
	return Some(std::move(doc));
}

RigDocument RigDocument::New(const String &id, const String &mod, const String &baseDir, const String &file) {
	RigDocument doc;
	doc.m_mod = mod;
	doc.m_id = voxel::ContentRegistry::Qualify(id, mod);
	doc.m_baseDir = baseDir;
	doc.m_file = file;
	const size_t colon = id.Find(":");
	doc.m_json = Node::MakeObject();
	doc.m_json->Set("type", json::MakeStr(String("model")));
	doc.m_json->Set("id", json::MakeStr(colon == String::NPOS ? id : id.Substring(colon + 1)));
	doc.m_json->Set("format", json::MakeStr(String("rig")));
	doc.m_json->Set("textureSize", Pair(64, 32));
	NodePtr part = Node::MakeObject();
	part->Set("name", json::MakeStr(String("body")));
	part->Set("pivot", Vec({0.f, 0.f, 0.f}));
	NodePtr box = Node::MakeObject();
	box->Set("from", Vec({-4.f, 0.f, -4.f}));
	box->Set("size", Vec({8.f, 8.f, 8.f}));
	box->Set("uv", Pair(0, 0));
	NodePtr boxes = Node::MakeArray();
	boxes->Push(box);
	part->Set("boxes", boxes);
	NodePtr parts = Node::MakeArray();
	parts->Push(part);
	doc.m_json->Set("parts", parts);
	doc.m_dirty = true;
	return doc;
}

Result<voxel::RigModel, String> RigDocument::Build() const { return voxel::RigFromJson(m_json, m_baseDir); }

std::vector<RigDiagnostic> RigDocument::Validate() const {
	std::vector<RigDiagnostic> out;
	Result<voxel::RigModel, String> rig = Build();
	if (rig.IsError()) {
		out.push_back({RigDiagnostic::Severity::Error, rig.Error(), String()});
		return out;
	}
	const voxel::RigModel &r = rig.Value();
	std::vector<String> names;
	for (const voxel::RigPart &part : r.parts) {
		if (std::find(names.begin(), names.end(), part.name) != names.end())
			out.push_back({RigDiagnostic::Severity::Error, String("nom de partie en double : ") + part.name, part.name});
		names.push_back(part.name);
		for (size_t i = 0; i < part.boxes.size(); ++i) {
			const voxel::RigBox &box = part.boxes[i];
			if (box.size.x <= 0.f || box.size.y <= 0.f || box.size.z <= 0.f)
				out.push_back({RigDiagnostic::Severity::Warning, String::Format("%s, boîte %zu : taille nulle ou négative", part.name.CStr(), i),
							   part.name});
			// Calque « only64 » : seulement sur un skin deux fois plus haut
			// (64×64 pour 64×32), comme BuildRigPart le décide.
			const bool tallLayer = box.only64 && r.textureHeight * 2 == r.textureWidth;
			const int height = tallLayer ? r.textureHeight * 2 : r.textureHeight;
			for (const voxel::RigUvFace &f : voxel::RigBoxUv(box))
				if (f.u < 0.f || f.v < 0.f || f.u + f.width > float(r.textureWidth) + 1e-3f || f.v + f.height > float(height) + 1e-3f) {
					out.push_back({RigDiagnostic::Severity::Warning,
								   String::Format("%s, boîte %zu : face « %s » hors de la texture (%d×%d)", part.name.CStr(), i, f.face,
												  r.textureWidth, height),
								   part.name});
					break;
				}
		}
	}
	for (const voxel::RigClip &clip : r.clips)
		for (const voxel::RigTrack &track : clip.tracks)
			for (const voxel::RigKey &key : track.keys)
				if (key.time < 0.f || key.time > clip.length + 1e-4f) {
					out.push_back({RigDiagnostic::Severity::Warning,
								   String::Format("animation « %s » : clé à %.2f s hors du clip (%.2f s)", clip.name.CStr(),
												  double(key.time), double(clip.length)),
								   r.parts[size_t(track.part)].name});
					break;
				}
	return out;
}

// ── Hiérarchie ───────────────────────────────────────────────────────────────

NodePtr RigDocument::PartNode(const String &name) const {
	for (const NodePtr &p : Items(json::At(m_json, "parts")))
		if (json::Str(p, "name") == name)
			return p;
	return nullptr;
}

bool RigDocument::HasPart(const String &name) const { return PartNode(name) != nullptr; }

bool RigDocument::IsDescendant(const String &part, const String &ancestor) const {
	String current = part;
	for (int guard = 0; guard < 256 && !current.IsEmpty(); ++guard) {
		if (current == ancestor)
			return true;
		NodePtr node = PartNode(current);
		current = node ? json::Str(node, "parent") : String();
	}
	return false;
}

std::vector<RigPartInfo> RigDocument::Parts() const {
	// Ordre d'ARBRE (parcours en profondeur) : un enfant juste sous son parent.
	std::vector<RigPartInfo> all;
	for (const NodePtr &p : Items(json::At(m_json, "parts"))) {
		RigPartInfo info;
		info.name = json::Str(p, "name");
		info.parent = json::Str(p, "parent");
		info.boxes = Items(json::At(p, "boxes")).size();
		info.hasMesh = json::Has(p, "mesh");
		all.push_back(info);
	}
	std::vector<RigPartInfo> out;
	std::vector<bool> placed(all.size(), false);
	std::function<void(const String &, int)> visit = [&](const String &parent, int depth) {
		for (size_t i = 0; i < all.size(); ++i)
			if (!placed[i] && all[i].parent == parent && out.size() < 4096) {
				placed[i] = true;
				all[i].depth = depth;
				out.push_back(all[i]);
				visit(all[i].name, depth + 1);
			}
	};
	visit(String(), 0);
	for (size_t i = 0; i < all.size(); ++i) // parent introuvable : à la racine
		if (!placed[i]) {
			placed[i] = true;
			out.push_back(all[i]);
			visit(all[i].name, 1);
		}
	return out;
}

Result<bool, String> RigDocument::AddPart(const String &name, const String &parent) {
	if (!ValidName(name))
		return Err(String("nom de partie invalide (lettres, chiffres, _ et -) : ") + name);
	if (HasPart(name))
		return Err(String("cette partie existe déjà : ") + name);
	if (!parent.IsEmpty() && !HasPart(parent))
		return Err(String("parent inconnu : ") + parent);
	const math::FVector3 pivot = parent.IsEmpty() ? math::FVector3{0.f, 0.f, 0.f} : Pivot(parent);
	Mutate([&](const NodePtr &root) {
		std::vector<NodePtr> parts = Items(json::At(root, "parts"));
		NodePtr part = Node::MakeObject();
		part->Set("name", json::MakeStr(name));
		if (!parent.IsEmpty())
			part->Set("parent", json::MakeStr(parent));
		part->Set("pivot", Vec(pivot));
		part->Set("boxes", Node::MakeArray());
		parts.push_back(part);
		root->Set("parts", Topological(parts));
	});
	return Ok(true);
}

Result<bool, String> RigDocument::RemovePart(const String &name) {
	NodePtr node = PartNode(name);
	if (!node)
		return Err(String("partie inconnue : ") + name);
	if (Items(json::At(m_json, "parts")).size() <= 1)
		return Err(String("un modèle garde au moins une partie"));
	const String parent = json::Str(node, "parent");
	Mutate([&](const NodePtr &root) {
		std::vector<NodePtr> parts;
		for (const NodePtr &p : Items(json::At(root, "parts"))) {
			if (json::Str(p, "name") == name)
				continue;
			if (json::Str(p, "parent") == name) { // les enfants passent au grand-parent
				if (parent.IsEmpty())
					p->Remove("parent");
				else
					p->Set("parent", json::MakeStr(parent));
			}
			parts.push_back(p);
		}
		root->Set("parts", Topological(parts));
		// Ses pistes d'animation disparaissent avec elle.
		if (NodePtr animations = json::At(root, "animations"); animations && animations->IsObject())
			for (const String &clip : animations->Keys()) {
				NodePtr a = animations->Get(clip);
				std::vector<NodePtr> kept;
				for (const NodePtr &t : Items(json::At(a, "tracks")))
					if (json::Str(t, "part") != name)
						kept.push_back(t);
				a->Set("tracks", Array(kept));
			}
	});
	return Ok(true);
}

Result<bool, String> RigDocument::RenamePart(const String &from, const String &to) {
	if (!HasPart(from))
		return Err(String("partie inconnue : ") + from);
	if (from == to)
		return Ok(true);
	if (!ValidName(to))
		return Err(String("nom de partie invalide (lettres, chiffres, _ et -) : ") + to);
	if (HasPart(to))
		return Err(String("cette partie existe déjà : ") + to);
	Mutate([&](const NodePtr &root) {
		for (const NodePtr &p : Items(json::At(root, "parts"))) {
			if (json::Str(p, "name") == from)
				p->Set("name", json::MakeStr(to));
			if (json::Str(p, "parent") == from)
				p->Set("parent", json::MakeStr(to));
		}
		if (NodePtr animations = json::At(root, "animations"); animations && animations->IsObject())
			for (const String &clip : animations->Keys())
				for (const NodePtr &t : Items(json::At(animations->Get(clip), "tracks")))
					if (json::Str(t, "part") == from)
						t->Set("part", json::MakeStr(to));
	});
	return Ok(true);
}

Result<bool, String> RigDocument::Reparent(const String &name, const String &parent) {
	if (!HasPart(name))
		return Err(String("partie inconnue : ") + name);
	if (!parent.IsEmpty() && !HasPart(parent))
		return Err(String("parent inconnu : ") + parent);
	if (!parent.IsEmpty() && IsDescendant(parent, name))
		return Err(String("cycle : « ") + parent + String(" » descend de « ") + name + String(" »"));
	Mutate([&](const NodePtr &root) {
		std::vector<NodePtr> parts = Items(json::At(root, "parts"));
		for (const NodePtr &p : parts)
			if (json::Str(p, "name") == name) {
				if (parent.IsEmpty())
					p->Remove("parent");
				else
					p->Set("parent", json::MakeStr(parent));
			}
		root->Set("parts", Topological(parts));
	});
	return Ok(true);
}

Result<bool, String> RigDocument::DuplicatePart(const String &name, const String &copy) {
	NodePtr node = PartNode(name);
	if (!node)
		return Err(String("partie inconnue : ") + name);
	if (!ValidName(copy) || HasPart(copy))
		return Err(String("nom de copie invalide ou déjà pris : ") + copy);
	Mutate([&](const NodePtr &root) {
		std::vector<NodePtr> parts = Items(json::At(root, "parts"));
		NodePtr clone = node->Clone();
		clone->Set("name", json::MakeStr(copy));
		parts.push_back(clone);
		root->Set("parts", Topological(parts));
	});
	return Ok(true);
}

// ── Transformations ──────────────────────────────────────────────────────────

math::FVector3 RigDocument::Pivot(const String &part) const { return json::Vec3(PartNode(part), "pivot", {0.f, 0.f, 0.f}); }

Result<bool, String> RigDocument::SetPivot(const String &part, const math::FVector3 &pivot) {
	if (!HasPart(part))
		return Err(String("partie inconnue : ") + part);
	Mutate([&](const NodePtr &) { PartNode(part)->Set("pivot", Vec(pivot)); });
	return Ok(true);
}

math::FVector3 RigDocument::Rotation(const String &part) const { return json::Vec3(PartNode(part), "rotation", {0.f, 0.f, 0.f}); }

Result<bool, String> RigDocument::SetRotation(const String &part, const math::FVector3 &degrees) {
	if (!HasPart(part))
		return Err(String("partie inconnue : ") + part);
	Mutate([&](const NodePtr &) {
		NodePtr node = PartNode(part);
		if (degrees.x == 0.f && degrees.y == 0.f && degrees.z == 0.f) {
			if (node->Has("rotation"))
				node->Remove("rotation");
		} else {
			node->Set("rotation", Vec(degrees));
		}
	});
	return Ok(true);
}

// ── Boîtes ───────────────────────────────────────────────────────────────────

std::vector<voxel::RigBox> RigDocument::Boxes(const String &part) const {
	std::vector<voxel::RigBox> out;
	for (const NodePtr &b : Items(json::At(PartNode(part), "boxes")))
		out.push_back(BoxFrom(b));
	return out;
}

Result<bool, String> RigDocument::AddBox(const String &part, const math::FVector3 &from, const math::FVector3 &size, int u, int v) {
	if (!HasPart(part))
		return Err(String("partie inconnue : ") + part);
	if (size.x <= 0.f || size.y <= 0.f || size.z <= 0.f)
		return Err(String("taille de boîte nulle ou négative"));
	voxel::RigBox box;
	box.from = from;
	box.size = size;
	box.u = u;
	box.v = v;
	Mutate([&](const NodePtr &) {
		NodePtr node = PartNode(part);
		std::vector<NodePtr> boxes = Items(json::At(node, "boxes"));
		boxes.push_back(BoxNode(box, nullptr));
		node->Set("boxes", Array(boxes));
	});
	return Ok(true);
}

Result<bool, String> RigDocument::SetBox(const String &part, size_t index, const voxel::RigBox &box) {
	NodePtr node = PartNode(part);
	std::vector<NodePtr> boxes = Items(json::At(node, "boxes"));
	if (!node || index >= boxes.size())
		return Err(String::Format("boîte %zu inconnue dans « %s »", index, part.CStr()));
	Mutate([&](const NodePtr &) {
		boxes[index] = BoxNode(box, boxes[index]);
		PartNode(part)->Set("boxes", Array(boxes));
	});
	return Ok(true);
}

Result<bool, String> RigDocument::RemoveBox(const String &part, size_t index) {
	NodePtr node = PartNode(part);
	std::vector<NodePtr> boxes = Items(json::At(node, "boxes"));
	if (!node || index >= boxes.size())
		return Err(String::Format("boîte %zu inconnue dans « %s »", index, part.CStr()));
	Mutate([&](const NodePtr &) {
		boxes.erase(boxes.begin() + std::ptrdiff_t(index));
		PartNode(part)->Set("boxes", Array(boxes));
	});
	return Ok(true);
}

Result<bool, String> RigDocument::MirrorBox(const String &part, size_t index) {
	NodePtr node = PartNode(part);
	std::vector<NodePtr> boxes = Items(json::At(node, "boxes"));
	if (!node || index >= boxes.size())
		return Err(String::Format("boîte %zu inconnue dans « %s »", index, part.CStr()));
	voxel::RigBox box = BoxFrom(boxes[index]);
	box.from.x = -(box.from.x + box.size.x); // symétrie par rapport à x = 0
	box.mirror = !box.mirror;
	Mutate([&](const NodePtr &) {
		boxes.push_back(BoxNode(box, boxes[index]));
		PartNode(part)->Set("boxes", Array(boxes));
	});
	return Ok(true);
}

std::array<voxel::RigUvFace, 6> RigDocument::UvLayout(const String &part, size_t index) const {
	const std::vector<voxel::RigBox> boxes = Boxes(part);
	return voxel::RigBoxUv(index < boxes.size() ? boxes[index] : voxel::RigBox{});
}

// ── Animation ────────────────────────────────────────────────────────────────

std::vector<String> RigDocument::Clips() const {
	NodePtr animations = json::At(m_json, "animations");
	return animations && animations->IsObject() ? animations->Keys() : std::vector<String>{};
}

Result<bool, String> RigDocument::AddClip(const String &name, float length, bool loop) {
	if (!ValidName(name))
		return Err(String("nom d'animation invalide : ") + name);
	if (!(length > 0.f))
		return Err(String("durée d'animation nulle ou négative"));
	const std::vector<String> clips = Clips();
	if (std::find(clips.begin(), clips.end(), name) != clips.end())
		return Err(String("cette animation existe déjà : ") + name);
	Mutate([&](const NodePtr &root) {
		NodePtr animations = json::At(root, "animations");
		if (!animations || !animations->IsObject()) {
			animations = Node::MakeObject();
			root->Set("animations", animations);
		}
		NodePtr clip = Node::MakeObject();
		clip->Set("length", json::MakeNum(double(length)));
		clip->Set("loop", json::MakeBool(loop));
		clip->Set("tracks", Node::MakeArray());
		animations->Set(name, clip);
	});
	return Ok(true);
}

Result<bool, String> RigDocument::RemoveClip(const String &name) {
	NodePtr animations = json::At(m_json, "animations");
	if (!animations || !animations->Has(name))
		return Err(String("animation inconnue : ") + name);
	Mutate([&](const NodePtr &root) { json::At(root, "animations")->Remove(name); });
	return Ok(true);
}

Result<bool, String> RigDocument::SetKey(const String &clip, const String &part, voxel::RigChannel channel, float time,
										 const math::FVector3 &value, voxel::RigInterp interp) {
	NodePtr animations = json::At(m_json, "animations");
	if (!animations || !animations->Has(clip))
		return Err(String("animation inconnue : ") + clip);
	if (!HasPart(part))
		return Err(String("partie inconnue : ") + part);
	const float length = float(json::Num(animations->Get(clip), "length", 1.0));
	if (time < 0.f || time > length + 1e-4f)
		return Err(String::Format("temps %.3f s hors du clip (0 à %.3f s)", double(time), double(length)));
	Mutate([&](const NodePtr &root) {
		NodePtr a = json::At(root, "animations")->Get(clip);
		std::vector<NodePtr> tracks = Items(json::At(a, "tracks"));
		NodePtr track;
		for (const NodePtr &t : tracks)
			if (json::Str(t, "part") == part && json::Str(t, "channel", "rotation") == RigChannelId(channel))
				track = t;
		if (!track) {
			track = Node::MakeObject();
			track->Set("part", json::MakeStr(part));
			track->Set("channel", json::MakeStr(String(RigChannelId(channel))));
			track->Set("keys", Node::MakeArray());
			tracks.push_back(track);
			a->Set("tracks", Array(tracks));
		}
		std::vector<NodePtr> keys;
		for (const NodePtr &k : Items(json::At(track, "keys")))
			if (std::fabs(float(json::Num(k, "t", 0.0)) - time) > 1e-3f)
				keys.push_back(k);
		NodePtr key = Node::MakeObject();
		key->Set("t", json::MakeNum(double(time)));
		key->Set("v", Vec(value));
		if (interp != voxel::RigInterp::LINEAR)
			key->Set("interp", json::MakeStr(String(RigInterpId(interp))));
		// Une clé remplacée garde ses poignées de Bézier.
		for (const NodePtr &k : Items(json::At(track, "keys")))
			if (std::fabs(float(json::Num(k, "t", 0.0)) - time) <= 1e-3f)
				for (const char *h : {"in", "out"})
					if (k->Has(h))
						key->Set(h, k->Get(h));
		keys.push_back(key);
		std::stable_sort(keys.begin(), keys.end(),
						 [](const NodePtr &x, const NodePtr &y) { return json::Num(x, "t", 0.0) < json::Num(y, "t", 0.0); });
		track->Set("keys", Array(keys));
	});
	return Ok(true);
}

Result<bool, String> RigDocument::RemoveKey(const String &clip, const String &part, voxel::RigChannel channel, float time) {
	const std::vector<voxel::RigKey> keys = Keys(clip, part, channel);
	if (std::none_of(keys.begin(), keys.end(), [&](const voxel::RigKey &k) { return std::fabs(k.time - time) <= 1e-3f; }))
		return Err(String::Format("pas de clé à %.3f s", double(time)));
	Mutate([&](const NodePtr &root) {
		NodePtr a = json::At(root, "animations")->Get(clip);
		std::vector<NodePtr> tracks;
		for (const NodePtr &t : Items(json::At(a, "tracks"))) {
			if (json::Str(t, "part") == part && json::Str(t, "channel", "rotation") == RigChannelId(channel)) {
				std::vector<NodePtr> kept;
				for (const NodePtr &k : Items(json::At(t, "keys")))
					if (std::fabs(float(json::Num(k, "t", 0.0)) - time) > 1e-3f)
						kept.push_back(k);
				if (kept.empty())
					continue; // piste vide : retirée
				t->Set("keys", Array(kept));
			}
			tracks.push_back(t);
		}
		a->Set("tracks", Array(tracks));
	});
	return Ok(true);
}

std::vector<voxel::RigKey> RigDocument::Keys(const String &clip, const String &part, voxel::RigChannel channel) const {
	Result<voxel::RigModel, String> rig = Build();
	if (rig.IsError())
		return {};
	const voxel::RigClip *c = rig.Value().FindClip(clip);
	const int index = rig.Value().Find(part);
	if (!c || index < 0)
		return {};
	for (const voxel::RigTrack &t : c->tracks)
		if (t.part == index && t.channel == channel)
			return t.keys;
	return {};
}

std::vector<math::FVector3> RigDocument::SampleCurve(const String &clip, const String &part, voxel::RigChannel channel,
													 size_t samples) const {
	std::vector<math::FVector3> out;
	Result<voxel::RigModel, String> rig = Build();
	if (rig.IsError() || samples < 2)
		return out;
	const voxel::RigClip *c = rig.Value().FindClip(clip);
	const int index = rig.Value().Find(part);
	if (!c || index < 0)
		return out;
	const voxel::RigTrack *track = nullptr;
	for (const voxel::RigTrack &t : c->tracks)
		if (t.part == index && t.channel == channel)
			track = &t;
	for (size_t i = 0; i < samples; ++i) {
		const float time = c->length * float(i) / float(samples - 1);
		out.push_back(track ? voxel::SampleRigTrack(*track, time) : math::FVector3{0.f, 0.f, 0.f});
	}
	return out;
}

namespace {

/// Nœud JSON de la clé `time` d'une piste (nullptr si absente).
NodePtr KeyNode(const NodePtr &root, const String &clip, const String &part, voxel::RigChannel channel, float time) {
	NodePtr animations = json::At(root, "animations");
	if (!animations || !animations->Has(clip))
		return nullptr;
	for (const NodePtr &t : Items(json::At(animations->Get(clip), "tracks")))
		if (json::Str(t, "part") == part && json::Str(t, "channel", "rotation") == RigChannelId(channel))
			for (const NodePtr &k : Items(json::At(t, "keys")))
				if (std::fabs(float(json::Num(k, "t", 0.0)) - time) <= 1e-3f)
					return k;
	return nullptr;
}

NodePtr HandleNode(const voxel::RigHandle &h) {
	NodePtr n = Node::MakeObject();
	n->Set("t", json::MakeNum(double(h.time)));
	n->Set("v", Vec(h.value));
	return n;
}

voxel::RigLayer LayerFrom(const NodePtr &l) {
	voxel::RigLayer layer;
	layer.clip = json::Str(l, "clip");
	layer.weight = float(json::Num(l, "weight", 1.0));
	layer.speed = float(json::Num(l, "speed", 1.0));
	layer.mode = json::Str(l, "mode", "mix") == "add" ? voxel::RigBlend::ADD : voxel::RigBlend::MIX;
	return layer;
}

NodePtr LayerNode(const voxel::RigLayer &layer) {
	NodePtr l = Node::MakeObject();
	l->Set("clip", json::MakeStr(layer.clip));
	l->Set("weight", json::MakeNum(double(layer.weight)));
	if (layer.speed != 1.f)
		l->Set("speed", json::MakeNum(double(layer.speed)));
	l->Set("mode", json::MakeStr(String(layer.mode == voxel::RigBlend::ADD ? "add" : "mix")));
	return l;
}

/// Nom de partie sûr (lettres, chiffres, _ et -), unique dans `taken`.
String SafeName(const String &raw, size_t index, std::vector<String> &taken) {
	String name;
	for (size_t i = 0; i < raw.GetSize() && name.GetSize() < 48; ++i) {
		const char c = raw[i];
		name += std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' ? c : '_';
	}
	if (name.IsEmpty())
		name = String::Format("node%zu", index);
	String unique = name;
	for (int n = 2; std::find(taken.begin(), taken.end(), unique) != taken.end(); ++n)
		unique = name + String::Format("_%d", n);
	taken.push_back(unique);
	return unique;
}

} // namespace

Result<bool, String> RigDocument::SetKeyHandles(const String &clip, const String &part, voxel::RigChannel channel, float time,
												Option<voxel::RigHandle> in, Option<voxel::RigHandle> out) {
	if (!KeyNode(m_json, clip, part, channel, time))
		return Err(String::Format("pas de clé à %.3f s (%s / %s / %s)", double(time), clip.CStr(), part.CStr(), RigChannelId(channel)));
	Mutate([&](const NodePtr &root) {
		NodePtr key = KeyNode(root, clip, part, channel, time);
		key->Set("interp", json::MakeStr(String("bezier")));
		if (in.IsSome())
			key->Set("in", HandleNode(in.Value()));
		else if (key->Has("in"))
			key->Remove("in");
		if (out.IsSome())
			key->Set("out", HandleNode(out.Value()));
		else if (key->Has("out"))
			key->Remove("out");
	});
	return Ok(true);
}

Result<bool, String> RigDocument::AutoSmooth(const String &clip, const String &part, voxel::RigChannel channel) {
	const std::vector<voxel::RigKey> keys = Keys(clip, part, channel);
	if (keys.size() < 2)
		return Err(String("il faut au moins deux clés pour lisser"));
	Mutate([&](const NodePtr &root) {
		for (size_t i = 0; i < keys.size(); ++i) {
			// Pente de Catmull-Rom (extrémités à plat), poignées au tiers.
			math::FVector3 slope{0.f, 0.f, 0.f};
			if (i > 0 && i + 1 < keys.size())
				slope = (keys[i + 1].value - keys[i - 1].value) * (1.f / std::max(keys[i + 1].time - keys[i - 1].time, 1e-6f));
			NodePtr key = KeyNode(root, clip, part, channel, keys[i].time);
			if (!key)
				continue;
			if (i > 0) {
				const float dt = (keys[i].time - keys[i - 1].time) / 3.f;
				key->Set("interp", json::MakeStr(String("bezier")));
				key->Set("in", HandleNode({-dt, slope * -dt}));
			}
			if (i + 1 < keys.size()) {
				const float dt = (keys[i + 1].time - keys[i].time) / 3.f;
				key->Set("out", HandleNode({dt, slope * dt}));
			}
		}
	});
	return Ok(true);
}

std::vector<voxel::RigLayer> RigDocument::Layers() const {
	std::vector<voxel::RigLayer> out;
	for (const NodePtr &l : Items(json::At(m_json, "layers")))
		out.push_back(LayerFrom(l));
	return out;
}

Result<bool, String> RigDocument::AddLayer(const voxel::RigLayer &layer) {
	const std::vector<String> clips = Clips();
	if (std::find(clips.begin(), clips.end(), layer.clip) == clips.end())
		return Err(String("animation inconnue : ") + layer.clip);
	Mutate([&](const NodePtr &root) {
		std::vector<NodePtr> layers = Items(json::At(root, "layers"));
		layers.push_back(LayerNode(layer));
		root->Set("layers", Array(layers));
	});
	return Ok(true);
}

Result<bool, String> RigDocument::SetLayer(size_t index, const voxel::RigLayer &layer) {
	std::vector<NodePtr> layers = Items(json::At(m_json, "layers"));
	if (index >= layers.size())
		return Err(String::Format("couche %zu inconnue", index));
	const std::vector<String> clips = Clips();
	if (std::find(clips.begin(), clips.end(), layer.clip) == clips.end())
		return Err(String("animation inconnue : ") + layer.clip);
	Mutate([&](const NodePtr &root) {
		layers[index] = LayerNode(layer);
		root->Set("layers", Array(layers));
	});
	return Ok(true);
}

Result<bool, String> RigDocument::RemoveLayer(size_t index) {
	std::vector<NodePtr> layers = Items(json::At(m_json, "layers"));
	if (index >= layers.size())
		return Err(String::Format("couche %zu inconnue", index));
	Mutate([&](const NodePtr &root) {
		layers.erase(layers.begin() + std::ptrdiff_t(index));
		if (layers.empty())
			root->Remove("layers");
		else
			root->Set("layers", Array(layers));
	});
	return Ok(true);
}

Result<size_t, String> RigDocument::ExportGltf(const String &path, const String &skinImage, float fps, int skinWidth,
											   int skinHeight) const {
	namespace fs = std::filesystem;
	Result<voxel::RigModel, String> built = Build();
	if (built.IsError())
		return Err(built.Error());
	const voxel::RigModel &rig = built.Value();
	dataio::gltf::SceneDescription scene;
	scene.generator = String("Voxelia Studio — ") + m_id;
	if (!skinImage.IsEmpty()) {
		// L'image voyage avec le modèle (même dossier).
		std::error_code ec;
		const fs::path target = fs::path(path.CStr()).parent_path() / fs::path(skinImage.CStr()).filename();
		fs::create_directories(target.parent_path(), ec);
		if (fs::absolute(skinImage.CStr(), ec) != fs::absolute(target, ec))
			fs::copy_file(skinImage.CStr(), target, fs::copy_options::overwrite_existing, ec);
		if (ec)
			return Err(String("copie du skin impossible : ") + String(ec.message().c_str()));
		dataio::gltf::MaterialDescription material;
		material.name = "skin";
		material.baseColorUri = String(target.filename().string().c_str());
		scene.materials.push_back(material);
	}
	size_t triangles = 0;
	auto parentOf = [&](size_t i) { return rig.parts[i].parent.IsEmpty() ? -1 : rig.Find(rig.parts[i].parent); };
	auto localTranslation = [&](size_t i, const math::FVector3 &offset) {
		math::FVector3 t = (rig.parts[i].pivot + offset) * rig.scale;
		if (const int parent = parentOf(i); parent >= 0)
			t = t - rig.parts[size_t(parent)].pivot * rig.scale;
		return t;
	};
	for (size_t i = 0; i < rig.parts.size(); ++i) {
		const voxel::RigPart &part = rig.parts[i];
		dataio::gltf::NodeDescription node;
		node.name = part.name;
		node.parent = parentOf(i);
		const math::FVector3 t = localTranslation(i, {0.f, 0.f, 0.f});
		node.translation[0] = t.x;
		node.translation[1] = t.y;
		node.translation[2] = t.z;
		const math::FQuaternion q = voxel::RigRotation(part.rotation);
		node.rotation[0] = q.x;
		node.rotation[1] = q.y;
		node.rotation[2] = q.z;
		node.rotation[3] = q.w;
		// UV pour l'image réellement exportée (cf. en-tête).
		const voxel::ModelMesh mesh = voxel::BuildRigPart(rig, part, skinWidth > 0 ? skinWidth : rig.textureWidth,
														  skinHeight > 0 ? skinHeight : rig.textureHeight);
		if (!mesh.indices.empty()) {
			dataio::gltf::MeshDescription m;
			m.name = part.name;
			for (size_t k = 0; k < mesh.positions.size(); ++k) {
				m.positions.insert(m.positions.end(), {mesh.positions[k].x, mesh.positions[k].y, mesh.positions[k].z});
				m.normals.insert(m.normals.end(), {mesh.normals[k].x, mesh.normals[k].y, mesh.normals[k].z});
				m.uvs.insert(m.uvs.end(), {mesh.uvs[k].x, mesh.uvs[k].y}); // v vers le bas : convention glTF
			}
			m.indices = mesh.indices;
			m.material = scene.materials.empty() ? -1 : 0;
			triangles += m.indices.size() / 3;
			scene.meshes.push_back(std::move(m));
			node.mesh = int(scene.meshes.size() - 1);
		}
		scene.nodes.push_back(node);
	}
	// Chaque clip, échantillonné par le moteur (PoseRig) : Bézier et paliers compris.
	for (const voxel::RigClip &clip : rig.clips) {
		dataio::gltf::AnimationDescription animation;
		animation.name = clip.name;
		const size_t frames = std::max<size_t>(2, size_t(std::ceil(clip.length * std::max(1.f, fps))) + 1);
		for (size_t i = 0; i < rig.parts.size(); ++i) {
			bool rotates = false, moves = false;
			for (const voxel::RigTrack &track : clip.tracks)
				if (track.part == int(i))
					(track.channel == voxel::RigChannel::ROTATION ? rotates : moves) = true;
			if (!rotates && !moves)
				continue;
			dataio::gltf::ChannelDescription rotation{int(i), "rotation", {}, {}, "LINEAR"};
			dataio::gltf::ChannelDescription translation{int(i), "translation", {}, {}, "LINEAR"};
			voxel::RigPoseInput input;
			input.clip = &clip;
			for (size_t f = 0; f < frames; ++f) {
				const float time = clip.length * float(f) / float(frames - 1);
				input.clipTime = time;
				const voxel::RigPartPose pose = voxel::PoseRig(rig, i, input);
				const math::FQuaternion q = voxel::RigRotation(pose.rotation);
				rotation.times.push_back(time);
				rotation.values.insert(rotation.values.end(), {q.x, q.y, q.z, q.w});
				const math::FVector3 t = localTranslation(i, pose.offset);
				translation.times.push_back(time);
				translation.values.insert(translation.values.end(), {t.x, t.y, t.z});
			}
			if (rotates)
				animation.channels.push_back(std::move(rotation));
			if (moves)
				animation.channels.push_back(std::move(translation));
		}
		if (!animation.channels.empty())
			scene.animations.push_back(std::move(animation));
	}
	if (Option<String> error = dataio::gltf::SaveScene(path, scene); error.IsSome())
		return Err(error.Value());
	return Ok(triangles);
}

Result<size_t, String> RigDocument::ImportGltf(const String &path) {
	namespace fs = std::filesystem;
	Result<dataio::gltf::Document, String> loaded = dataio::gltf::LoadFile(path, false);
	if (loaded.IsError())
		return Err(loaded.Error());
	const dataio::gltf::Document &doc = loaded.Value();
	Result<voxel::RigModel, String> current = Build();
	const float scale = current.IsOk() ? current.Value().scale : 1.f / 16.f;
	// Chemin enregistré : relatif au mod quand le fichier y est.
	String reference = path;
	std::error_code ec;
	if (!m_baseDir.IsEmpty()) {
		const fs::path rel = fs::relative(fs::absolute(path.CStr(), ec), fs::absolute(m_baseDir.CStr(), ec), ec);
		if (!ec && !rel.empty() && rel.string().rfind("..", 0) != 0)
			reference = String(rel.generic_string().c_str());
	}
	struct Imported {
		NodePtr node;
		String name;
	};
	std::vector<Imported> parts;
	std::vector<String> taken;
	std::vector<String> nodeNames;
	for (const dataio::gltf::Node &n : doc.nodes)
		nodeNames.push_back(n.name);
	std::function<void(int, const String &, const math::FVector3 &)> visit = [&](int index, const String &parent,
																					const math::FVector3 &parentPivot) {
		if (index < 0 || size_t(index) >= doc.nodes.size() || parts.size() > 1024)
			return;
		const dataio::gltf::Node &n = doc.nodes[size_t(index)];
		const String name = SafeName(n.name, size_t(index), taken);
		const math::FVector3 pivot = parentPivot + math::FVector3{n.translation[0], n.translation[1], n.translation[2]} * (1.f / scale);
		const math::FVector3 euler =
			RigEulerFromQuaternion(math::FQuaternion{n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]});
		NodePtr part = Node::MakeObject();
		part->Set("name", json::MakeStr(name));
		if (!parent.IsEmpty())
			part->Set("parent", json::MakeStr(parent));
		part->Set("pivot", Vec(pivot));
		if (std::fabs(euler.x) + std::fabs(euler.y) + std::fabs(euler.z) > 1e-3f)
			part->Set("rotation", Vec(euler));
		if (n.mesh >= 0) {
			part->Set("model", json::MakeStr(reference));
			part->Set("node", json::MakeStr(n.name));
		} else {
			part->Set("boxes", Node::MakeArray());
		}
		parts.push_back({part, name});
		for (int child : n.children)
			visit(child, name, pivot);
	};
	std::vector<int> roots;
	if (doc.defaultScene >= 0 && size_t(doc.defaultScene) < doc.scenes.size())
		roots = doc.scenes[size_t(doc.defaultScene)].nodes;
	else if (!doc.scenes.empty())
		roots = doc.scenes.front().nodes;
	for (int root : roots)
		visit(root, String(), {0.f, 0.f, 0.f});
	if (parts.empty())
		return Err(path + String(" : aucun nœud à importer"));
	// Un nœud maillé dont le nom est en double : LoadGltfMesh prendrait le premier.
	for (const Imported &p : parts)
		if (const String node = json::Str(p.node, "node"); !node.IsEmpty())
			if (std::count(nodeNames.begin(), nodeNames.end(), node) > 1)
				return Err(String("nœud glTF en double (renommez-le dans le fichier) : ") + node);
	// Skin : l'image du premier matériau texturé, si le modèle n'en a pas.
	String skin;
	for (const dataio::gltf::Material &material : doc.materials)
		if (skin.IsEmpty() && material.baseColorTexture.IsSet() && size_t(material.baseColorTexture.index) < doc.textures.size()) {
			const int source = doc.textures[size_t(material.baseColorTexture.index)].source;
			if (source >= 0 && size_t(source) < doc.images.size() && !doc.images[size_t(source)].uri.IsEmpty())
				skin = doc.ResolveUri(doc.images[size_t(source)].uri);
		}
	Mutate([&](const NodePtr &root) {
		std::vector<NodePtr> kept;
		for (const NodePtr &p : Items(json::At(root, "parts")))
			if (std::none_of(parts.begin(), parts.end(), [&](const Imported &i) { return i.name == json::Str(p, "name"); }))
				kept.push_back(p);
		for (const Imported &i : parts)
			kept.push_back(i.node);
		root->Set("parts", Topological(kept));
		if (!skin.IsEmpty() && json::Str(root, "texture").IsEmpty())
			root->Set("texture", json::MakeStr(skin));
	});
	return Ok(parts.size());
}

Result<size_t, String> RigDocument::ExportObj(const String &path, const String &clip, float time) const {
	Result<voxel::RigModel, String> built = Build();
	if (built.IsError())
		return Err(built.Error());
	const voxel::RigModel &rig = built.Value();
	voxel::RigPoseInput input;
	const voxel::RigClip *c = clip.IsEmpty() ? nullptr : rig.FindClip(clip);
	if (!clip.IsEmpty() && !c)
		return Err(String("animation inconnue : ") + clip);
	input.clip = c;
	input.clipTime = time;
	// Transformations monde : parent d'abord (garanti par l'ordre des parties).
	struct World {
		math::FQuaternion rotation;
		math::FVector3 position;
	};
	std::vector<World> world(rig.parts.size());
	std::vector<float> positions, normals, uvs;
	std::vector<uint32_t> indices;
	for (size_t i = 0; i < rig.parts.size(); ++i) {
		const voxel::RigPart &part = rig.parts[i];
		voxel::RigPartPose pose;
		pose.rotation = part.rotation;
		if (c)
			pose = voxel::PoseRig(rig, i, input);
		const int parent = part.parent.IsEmpty() ? -1 : rig.Find(part.parent);
		math::FVector3 local = (part.pivot + pose.offset) * rig.scale;
		if (parent >= 0)
			local = local - rig.parts[size_t(parent)].pivot * rig.scale;
		const math::FQuaternion rotation = voxel::RigRotation(pose.rotation);
		if (parent >= 0) {
			const World &up = world[size_t(parent)];
			world[i] = {up.rotation * rotation, up.position + up.rotation.Rotate(local)};
		} else {
			world[i] = {rotation, local};
		}
		const voxel::ModelMesh mesh = voxel::BuildRigPart(rig, part, rig.textureWidth, rig.textureHeight);
		const uint32_t base = uint32_t(positions.size() / 3);
		for (size_t k = 0; k < mesh.positions.size(); ++k) {
			const math::FVector3 p = world[i].position + world[i].rotation.Rotate(mesh.positions[k]);
			const math::FVector3 n = world[i].rotation.Rotate(mesh.normals[k]);
			positions.insert(positions.end(), {p.x, p.y, p.z});
			normals.insert(normals.end(), {n.x, n.y, n.z});
			uvs.insert(uvs.end(), {mesh.uvs[k].x, 1.f - mesh.uvs[k].y}); // OBJ : v vers le haut
		}
		for (uint32_t index : mesh.indices)
			indices.push_back(base + index);
	}
	if (indices.empty())
		return Err(String("modèle sans géométrie"));
	dataio::TriangleMeshView view;
	view.positions = positions;
	view.normals = normals;
	view.uvs = uvs;
	view.indices = indices;
	dataio::ObjOptions options;
	options.comment = String("Voxelia Studio — ") + m_id + (c ? String(" (") + clip + String::Format(" à %.2f s)", double(time)) : String(" (repos)"));
	const String text = dataio::EncodeObj(view, options);
	std::error_code ec;
	const std::filesystem::path parentDir = std::filesystem::path(path.CStr()).parent_path();
	if (!parentDir.empty())
		std::filesystem::create_directories(parentDir, ec);
	std::ofstream file(path.CStr(), std::ios::binary | std::ios::trunc);
	if (!file)
		return Err(path + String(" : écriture impossible"));
	file.write(text.CStr(), std::streamsize(text.GetSize()));
	if (!file)
		return Err(path + String(" : écriture incomplète"));
	return Ok(indices.size() / 3);
}

} // namespace voxelia::studio
