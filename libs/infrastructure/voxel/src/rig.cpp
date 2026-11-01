// Définitions de voxel/rig.hpp
#include <voxel/json.hpp>
#include <voxel/rig.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

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

const RigClip *RigModel::FindClip(const String &name) const {
	for (const RigClip &clip : clips)
		if (clip.name == name)
			return &clip;
	return nullptr;
}

int RigModel::Find(const String &name) const {
	for (size_t i = 0; i < parts.size(); ++i)
		if (parts[i].name == name)
			return int(i);
	return -1;
}

Result<RigModel, String> RigFromJson(const NodePtr &def, const String &baseDir) {
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
		// Partie maillée (glTF / OBJ) : en blocs dans le fichier, en pixels ici.
		if (const String model = json::Str(p, "model"); !model.IsEmpty()) {
			namespace fs = std::filesystem;
			String file = model;
			if (!baseDir.IsEmpty() && fs::path(model.CStr()).is_relative())
				if (std::error_code ec; fs::exists(fs::path(baseDir.CStr()) / model.CStr(), ec))
					file = String((fs::path(baseDir.CStr()) / model.CStr()).lexically_normal().string().c_str());
			const String node = json::Str(p, "node");
			Result<ModelMesh, String> mesh = Err(String());
			if (file.ToLower().EndsWith(".gltf")) {
				mesh = LoadGltfMesh(file, node);
			} else {
				std::ifstream in(file.CStr(), std::ios::binary);
				if (!in)
					mesh = Err(file + String(" : fichier illisible"));
				else
					mesh = ParseObj(String(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()).c_str()));
			}
			if (mesh.IsError())
				return Err(String::Format("rig : partie « %s » : %s", part.name.CStr(), mesh.Error().CStr()));
			part.mesh = std::move(mesh).Unwrap();
			const float toPixels = 1.f / std::max(rig.scale, 1e-6f);
			for (math::FVector3 &v : part.mesh.positions)
				v = v * toPixels + (node.IsEmpty() ? math::FVector3{0.f, 0.f, 0.f} : part.pivot);
		}
		if (!part.parent.IsEmpty() && rig.Find(part.parent) < 0)
			return Err(String::Format("rig : partie « %s » : parent « %s » inconnu (il doit la précéder)", part.name.CStr(),
									  part.parent.CStr()));
		rig.parts.push_back(std::move(part));
	}
	// Points d'attache : yeux et mains.
	auto anchor = [&](const NodePtr &n, RigAnchor &out, const char *what) -> Result<bool, String> {
		if (!n)
			return Ok(true);
		const String partName = json::Str(n, "part");
		out.part = rig.Find(partName);
		if (out.part < 0)
			return Err(String::Format("rig : « %s » : partie « %s » inconnue", what, partName.CStr()));
		out.at = Vec(n, "at", rig.parts[size_t(out.part)].pivot);
		return Ok(true);
	};
	if (auto r = anchor(json::At(def, "eye"), rig.eye, "eye"); r.IsError())
		return Err(r.Error());
	if (auto r = anchor(json::At(def, "hands.right"), rig.rightHand, "hands.right"); r.IsError())
		return Err(r.Error());
	if (auto r = anchor(json::At(def, "hands.left"), rig.leftHand, "hands.left"); r.IsError())
		return Err(r.Error());
	if (!rig.eye.IsSet())
		for (size_t i = 0; i < rig.parts.size(); ++i)
			if (const RigPart &p = rig.parts[i]; p.look && !p.boxes.empty()) {
				// Devant la face avant (+z), calques gonflés compris.
				const RigBox &first = p.boxes.front();
				float front = first.from.z + first.size.z + first.inflate;
				for (const RigBox &b : p.boxes)
					front = std::max(front, b.from.z + b.size.z + b.inflate);
				rig.eye = RigAnchor{int(i), {first.from.x + first.size.x * 0.5f, first.from.y + first.size.y * 0.5f, front}};
				break;
			}
	auto hand = [&](RigAnchor &out, const char *name) {
		if (out.IsSet())
			return;
		const int i = rig.Find(name);
		if (i < 0 || rig.parts[size_t(i)].boxes.empty())
			return;
		const RigBox &b = rig.parts[size_t(i)].boxes.front();
		out = RigAnchor{i, {b.from.x + b.size.x * 0.5f, b.from.y, b.from.z + b.size.z * 0.5f}};
	};
	hand(rig.rightHand, "right_arm");
	hand(rig.leftHand, "left_arm");
	// Animations à images clés.
	if (NodePtr animations = json::At(def, "animations"); animations && animations->IsObject())
		for (const String &name : animations->Keys()) {
			NodePtr a = animations->Get(name);
			RigClip clip;
			clip.name = name;
			clip.length = float(json::Num(a, "length", 1.0));
			clip.loop = json::Bool(a, "loop", true);
			if (!(clip.length > 0.f))
				return Err(String::Format("rig : animation « %s » : « length » doit être > 0", name.CStr()));
			NodePtr tracks = json::At(a, "tracks");
			for (size_t t = 0; tracks && tracks->IsArray() && t < tracks->GetSize(); ++t) {
				NodePtr tr = tracks->At(t);
				RigTrack track;
				const String partName = json::Str(tr, "part");
				track.part = rig.Find(partName);
				if (track.part < 0)
					return Err(String::Format("rig : animation « %s » : partie « %s » inconnue", name.CStr(), partName.CStr()));
				const String channel = json::Str(tr, "channel", "rotation").ToLower();
				if (channel != "rotation" && channel != "position")
					return Err(String::Format("rig : animation « %s » : canal « %s » inconnu (rotation, position)", name.CStr(),
											  channel.CStr()));
				track.channel = channel == "position" ? RigChannel::POSITION : RigChannel::ROTATION;
				NodePtr keys = json::At(tr, "keys");
				for (size_t k = 0; keys && keys->IsArray() && k < keys->GetSize(); ++k) {
					NodePtr kn = keys->At(k);
					RigKey key;
					key.time = float(json::Num(kn, "t", 0.0));
					key.value = Vec(kn, "v", key.value);
					const String interp = json::Str(kn, "interp", "linear").ToLower();
					key.interp = interp == "step"	? RigInterp::STEP
								 : interp == "smooth" ? RigInterp::SMOOTH
								 : interp == "bezier" ? RigInterp::BEZIER
													  : RigInterp::LINEAR;
					auto handle = [](const NodePtr &h) -> std::optional<RigHandle> {
						if (!h || !h->IsObject())
							return std::nullopt;
						return RigHandle{float(json::Num(h, "t", 0.0)), json::Vec3(h, "v", {0.f, 0.f, 0.f})};
					};
					key.in = handle(json::At(kn, "in"));
					key.out = handle(json::At(kn, "out"));
					track.keys.push_back(key);
				}
				std::stable_sort(track.keys.begin(), track.keys.end(), [](const RigKey &x, const RigKey &y) { return x.time < y.time; });
				if (!track.keys.empty())
					clip.tracks.push_back(std::move(track));
			}
			rig.clips.push_back(std::move(clip));
		}
	// Couches de mélange (après les clips : elles les désignent par leur nom).
	if (NodePtr layers = json::At(def, "layers"); layers && layers->IsArray())
		for (size_t i = 0; i < layers->GetSize(); ++i) {
			NodePtr l = layers->At(i);
			RigLayer layer;
			layer.clip = json::Str(l, "clip");
			if (!rig.FindClip(layer.clip))
				return Err(String::Format("rig : couche %zu : animation « %s » inconnue", i, layer.clip.CStr()));
			layer.weight = float(json::Num(l, "weight", 1.0));
			layer.speed = float(json::Num(l, "speed", 1.0));
			const String mode = json::Str(l, "mode", "mix").ToLower();
			if (mode != "mix" && mode != "add")
				return Err(String::Format("rig : couche %zu : mode « %s » inconnu (mix, add)", i, mode.CStr()));
			layer.mode = mode == "add" ? RigBlend::ADD : RigBlend::MIX;
			rig.layers.push_back(std::move(layer));
		}
	return Ok(std::move(rig));
}

std::array<RigUvFace, 6> RigBoxUv(const RigBox &box) {
	const float u = float(box.u), v = float(box.v);
	const float w = box.size.x, h = box.size.y, d = box.size.z;
	const float rightU = box.mirror ? u + d + w : u, leftU = box.mirror ? u : u + d + w;
	return {RigUvFace{"front", u + d, v + d, w, h},
			RigUvFace{"right", rightU, v + d, d, h},
			RigUvFace{"left", leftU, v + d, d, h},
			RigUvFace{"back", u + 2.f * d + w, v + d, w, h},
			RigUvFace{"top", u + d, v, w, d},
			RigUvFace{"bottom", u + d + w, v, w, d}};
}

RigBezier RigSegmentBezier(const RigKey &a, const RigKey &b) {
	const float span = std::max(b.time - a.time, 1e-6f);
	const RigHandle out = a.out ? *a.out : RigHandle{span / 3.f, {0.f, 0.f, 0.f}};
	const RigHandle in = b.in ? *b.in : RigHandle{-span / 3.f, {0.f, 0.f, 0.f}};
	RigBezier bz;
	bz.t[0] = a.time;
	bz.t[1] = a.time + std::clamp(out.time, 0.f, span);
	bz.t[2] = b.time + std::clamp(in.time, -span, 0.f);
	bz.t[3] = b.time;
	bz.v[0] = a.value;
	bz.v[1] = a.value + out.value;
	bz.v[2] = b.value + in.value;
	bz.v[3] = b.value;
	return bz;
}

math::FVector3 SampleRigTrack(const RigTrack &track, float time) {
	const std::vector<RigKey> &keys = track.keys;
	if (keys.empty())
		return {0.f, 0.f, 0.f};
	if (time <= keys.front().time)
		return keys.front().value;
	if (time >= keys.back().time)
		return keys.back().value;
	for (size_t i = 1; i < keys.size(); ++i) {
		if (time > keys[i].time)
			continue;
		const RigKey &a = keys[i - 1], &b = keys[i];
		const float span = b.time - a.time;
		float t = span > 1e-6f ? (time - a.time) / span : 1.f;
		switch (b.interp) {
		case RigInterp::STEP:
			t = time >= b.time ? 1.f : 0.f;
			break;
		case RigInterp::SMOOTH:
			t = t * t * (3.f - 2.f * t); // Hermite à tangentes nulles
			break;
		case RigInterp::BEZIER: {
			// x(s) = time : x est croissante (poignées bornées au segment) —
			// dichotomie, puis la valeur au même s.
			const RigBezier bz = RigSegmentBezier(a, b);
			auto at = [](float p0, float p1, float p2, float p3, float s) {
				const float u = 1.f - s;
				return u * u * u * p0 + 3.f * u * u * s * p1 + 3.f * u * s * s * p2 + s * s * s * p3;
			};
			float lo = 0.f, hi = 1.f;
			for (int it = 0; it < 32; ++it) {
				const float mid = 0.5f * (lo + hi);
				(at(bz.t[0], bz.t[1], bz.t[2], bz.t[3], mid) < time ? lo : hi) = mid;
			}
			const float s = 0.5f * (lo + hi);
			return {at(bz.v[0].x, bz.v[1].x, bz.v[2].x, bz.v[3].x, s), at(bz.v[0].y, bz.v[1].y, bz.v[2].y, bz.v[3].y, s),
					at(bz.v[0].z, bz.v[1].z, bz.v[2].z, bz.v[3].z, s)};
		}
		case RigInterp::LINEAR:
			break;
		}
		return a.value + (b.value - a.value) * t;
	}
	return keys.back().value;
}

float RigClipTime(const RigClip &clip, float time) {
	if (clip.length <= 0.f)
		return 0.f;
	if (clip.loop) {
		const float t = std::fmod(time, clip.length);
		return t < 0.f ? t + clip.length : t;
	}
	return std::clamp(time, 0.f, clip.length);
}

math::FQuaternion RigRotation(const math::FVector3 &degrees) {
	const math::FVector3 r = degrees * DEG;
	return math::FQuaternion::FromAxisAngle({0.f, 0.f, 1.f}, r.z) * math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, r.y) *
		   math::FQuaternion::FromAxisAngle({1.f, 0.f, 0.f}, r.x);
}

RigPartPose PoseRig(const RigModel &rig, size_t part, const RigPoseInput &input) {
	RigPartPose pose;
	if (part >= rig.parts.size())
		return pose;
	auto apply = [&](const RigClip &clip, float time, float weight) {
		const float t = RigClipTime(clip, time);
		for (const RigTrack &track : clip.tracks)
			if (track.part == int(part)) {
				const math::FVector3 v = SampleRigTrack(track, t) * weight;
				(track.channel == RigChannel::ROTATION ? pose.rotation : pose.offset) += v;
			}
	};
	if (input.clip) {
		// Clip imposé : il remplace les mouvements automatiques (repos et regard gardés).
		const RigPart &p = rig.parts[part];
		pose.rotation = p.rotation;
		if (p.look)
			pose.rotation.x -= input.lookPitch / DEG;
		apply(*input.clip, input.clipTime, 1.f);
		return pose;
	}
	pose.rotation = PoseRigPart(rig.parts[part], input.walkPhase, input.walkAmount, input.time, input.lookPitch);
	if (const RigClip *idle = rig.FindClip("idle"))
		apply(*idle, input.time, 1.f);
	if (const RigClip *walk = rig.FindClip("walk"); walk && input.walkAmount > 0.f)
		apply(*walk, input.walkPhase / (2.f * 3.14159265f) * walk->length, input.walkAmount);
	// Mélange : chaque couche, dans l'ordre.
	for (const RigLayer &layer : input.layers ? *input.layers : rig.layers) {
		const RigClip *clip = rig.FindClip(layer.clip);
		if (!clip || layer.weight == 0.f)
			continue;
		const float t = RigClipTime(*clip, input.time * layer.speed);
		for (const RigTrack &track : clip->tracks) {
			if (track.part != int(part))
				continue;
			const math::FVector3 v = SampleRigTrack(track, t);
			math::FVector3 &target = track.channel == RigChannel::ROTATION ? pose.rotation : pose.offset;
			if (layer.mode == RigBlend::ADD) {
				target += v * layer.weight;
			} else {
				// Pose du clip seul : repos + piste (rotation), piste (position).
				const math::FVector3 clipPose = track.channel == RigChannel::ROTATION ? rig.parts[part].rotation + v : v;
				target += (clipPose - target) * std::clamp(layer.weight, 0.f, 1.f);
			}
		}
	}
	// Gestes : bras levé vers l'avant quand la main tient un objet, coup du bras droit.
	if (int(part) == rig.rightHand.part) {
		if (input.holdRight)
			pose.rotation.x -= 18.f;
		if (input.swing > 0.f)
			pose.rotation.x -= 70.f * std::sin(std::clamp(input.swing, 0.f, 1.f) * 3.14159265f);
	}
	if (int(part) == rig.leftHand.part && input.holdLeft)
		pose.rotation.x -= 18.f;
	return pose;
}

math::FVector3 RigPointInModel(const RigModel &rig, size_t part, const math::FVector3 &point, const RigPoseInput &input) {
	if (part >= rig.parts.size())
		return point;
	// Repère de chaque partie : son pivot (décalé par la pose) puis sa rotation,
	// dans celui du parent (cf. le rendu des entités).
	math::FVector3 p = point - rig.parts[part].pivot;
	for (int i = int(part); i >= 0;) {
		const RigPart &node = rig.parts[size_t(i)];
		const RigPartPose pose = PoseRig(rig, size_t(i), input);
		const int parent = node.parent.IsEmpty() ? -1 : rig.Find(node.parent);
		const math::FVector3 origin = node.pivot + pose.offset - (parent >= 0 ? rig.parts[size_t(parent)].pivot : math::FVector3{0.f, 0.f, 0.f});
		p = origin + RigRotation(pose.rotation).Rotate(p);
		i = parent;
	}
	return p;
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
		// Dessous : comme le dessus, le bord AVANT au bas du rectangle (v + d)
		// — convention des skins ; l'ordre des sommets garde l'enroulement.
		Quad(m, {math::FVector3{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}, {0.f, -1.f, 0.f}, fu + dd + dw, fv + dd,
			 fu + dd + 2.f * dw, fv, mirror, w, h); // dessous
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
