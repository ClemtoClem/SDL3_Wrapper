// Définitions de world_renderer.hpp
#include <voxelgame/world_renderer.hpp>

#include <algorithm>
#include <cmath>

#include <render3d/texture_loader.hpp>
#include <voxel/json.hpp>
#include <voxel/rig.hpp>

namespace voxelgame {

using namespace voxel;

namespace {

/// Clé d'entité : identifiant + génération (une entité détruite n'est
/// jamais confondue avec celle qui réutilise sa place).
uint64_t EntityKey(ecs::Entity e) { return uint64_t(e.id) | (uint64_t(e.generation) << 32); }

sdl3::Color Mix(const std::array<uint8_t, 3> &a, float k) {
	return sdl3::Color{uint8_t(float(a[0]) * k), uint8_t(float(a[1]) * k), uint8_t(float(a[2]) * k), 255};
}

} // namespace

WorldRenderer::WorldRenderer(Game &game, render3d::Canvas &canvas) : m_game(game), m_canvas(canvas) {
	m_chunkRoot = &m_root.Add(std::make_unique<render3d::Object3D>());
	m_entityRoot = &m_root.Add(std::make_unique<render3d::Object3D>());
}

WorldRenderer::~WorldRenderer() = default;

render3d::Mesh WorldRenderer::ToMesh(const MeshBuffer &buffer) {
	std::vector<render3d::Vertex3D> vertices;
	vertices.reserve(buffer.vertices.size());
	for (const MeshVertex &v : buffer.vertices)
		vertices.push_back(render3d::Vertex3D{v.position, v.normal, v.uv, sdl3::Color{v.color[0], v.color[1], v.color[2], v.color[3]}});
	return render3d::Mesh(std::move(vertices), buffer.indices);
}

Result<bool, String> WorldRenderer::Initialize() {
	m_atlas.Build(m_game.Assets(), m_game.Content(), 16);
	const Image &pixels = m_atlas.Pixels();
	auto texture = render3d::CreateTextureFromPixels(m_canvas.Device(), uint32_t(pixels.width), uint32_t(pixels.height), pixels.rgba);
	if (texture.IsError())
		return Err(texture.Error());
	m_atlasTexture = Some(std::move(texture).Unwrap());
	m_mesher = std::make_unique<ChunkMesher>(m_game.Content(), m_atlas, &m_game.Assets());

	m_opaque = render3d::Material::Default();
	m_opaque.albedo = Some(MakeRef(m_atlasTexture.Value()));
	m_opaque.nearestFilter = true;
	m_opaque.roughness = 1.f; // pas de reflet spéculaire sur la pierre
	m_translucent = m_opaque;
	m_translucent.blend = true;
	m_translucent.doubleSided = true; // l'eau vue d'en dessous
	m_entity = m_opaque;
	m_entity.doubleSided = true;

	// Contour du bloc visé : douze baguettes fines le long des arêtes d'un
	// cube unité centré (boîtes « nodebox » du lecteur de modèles json).
	NodePtr boxes = dataio::Node::MakeArray();
	const float e = 0.502f, t = 0.012f;
	for (int axis = 0; axis < 3; ++axis)
		for (int a = -1; a <= 1; a += 2)
			for (int b = -1; b <= 1; b += 2) {
				float lo[3], hi[3];
				for (int k = 0; k < 3; ++k) {
					if (k == axis) {
						lo[k] = -e - t;
						hi[k] = e + t;
					} else {
						const float c = (k == (axis + 1) % 3 ? float(a) : float(b)) * e;
						lo[k] = c - t;
						hi[k] = c + t;
					}
				}
				NodePtr box = dataio::Node::MakeArray();
				for (float v : {lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]})
					box->Push(json::MakeNum(v));
				boxes->Push(box);
			}
	NodePtr data = dataio::Node::MakeObject();
	data->Set("boxes", boxes);
	std::vector<render3d::Vertex3D> vertices;
	std::vector<uint32_t> indices;
	if (auto model = ModelFromJson(data); model.IsOk()) {
		for (size_t i = 0; i < model.Value().positions.size(); ++i)
			vertices.push_back(render3d::Vertex3D{model.Value().positions[i] - math::FVector3{0.5f, 0.5f, 0.5f}, model.Value().normals[i],
												  {0.f, 0.f}, sdl3::Color{20, 20, 24, 255}});
		indices = model.Value().indices;
	}
	render3d::Material outline = render3d::Material::Unlit(sdl3::Color::WHITE());
	auto shape = std::make_unique<render3d::Shape>(render3d::Mesh(std::move(vertices), std::move(indices)), outline);
	m_outline = shape.get();
	m_outline->SetVisible(false);
	m_root.Add(std::move(shape));
	return Ok(true);
}

void WorldRenderer::RebuildChunk(const ChunkPos &p, ChunkView &view) {
	const ChunkMesh mesh = m_mesher->Build(m_game.GetWorld(), p);
	const math::FVector3 origin = p.Origin().ToVector();
	auto place = [&](render3d::Shape *&slot, const MeshBuffer &buffer, const render3d::Material &material) {
		if (buffer.IsEmpty()) {
			if (slot) {
				(void)m_chunkRoot->RemoveChild(slot);
				slot = nullptr;
			}
			return;
		}
		if (!slot) {
			auto shape = std::make_unique<render3d::Shape>(ToMesh(buffer), material);
			shape->SetPosition(origin);
			slot = shape.get();
			m_chunkRoot->Add(std::move(shape));
		} else {
			slot->SetGeometry(ToMesh(buffer));
		}
	};
	place(view.opaque, mesh.opaque, m_opaque);
	place(view.translucent, mesh.translucent, m_translucent);
	view.revision = mesh.revision;
}

void WorldRenderer::Update(const render3d::Camera &camera, float alpha, const Option<BlockHit> &target) {
	if (!m_game.HasWorld() || !m_mesher)
		return;
	World &world = m_game.GetWorld();
	// Chunks déchargés : leurs formes partent.
	for (auto it = m_chunks.begin(); it != m_chunks.end();) {
		if (!world.GetChunk(it->first)) {
			if (it->second.opaque)
				(void)m_chunkRoot->RemoveChild(it->second.opaque);
			if (it->second.translucent)
				(void)m_chunkRoot->RemoveChild(it->second.translucent);
			it = m_chunks.erase(it);
		} else {
			++it;
		}
	}
	// Chunks à (re)construire, les plus proches de la caméra d'abord. Un
	// chunk ne se maille qu'avec ses voisins horizontaux présents (sinon ses
	// bords seraient à refaire aussitôt).
	std::vector<std::pair<float, ChunkPos>> dirty;
	for (const ChunkPos &p : world.LoadedChunks()) {
		const Chunk *chunk = world.GetChunk(p);
		auto it = m_chunks.find(p);
		const uint64_t built = it == m_chunks.end() ? 0 : it->second.revision;
		if (built == chunk->Revision())
			continue;
		const math::FVector3 center = p.Origin().ToVector() + math::FVector3{8.f, 8.f, 8.f};
		dirty.push_back({(center - camera.position).Length(), p});
	}
	std::sort(dirty.begin(), dirty.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	m_pending = dirty.size();
	size_t rebuilt = 0;
	for (const auto &[distance, p] : dirty) {
		if (rebuilt >= rebuildBudget)
			break;
		RebuildChunk(p, m_chunks[p]);
		++rebuilt;
	}
	m_triangles = 0;
	for (const auto &[p, view] : m_chunks) {
		if (view.opaque)
			m_triangles += view.opaque->Geometry().IndexCount() / 3;
		if (view.translucent)
			m_triangles += view.translucent->Geometry().IndexCount() / 3;
	}

	UpdateEntities(alpha);

	// Contour : les boîtes de sélection du bloc visé.
	if (target.IsSome()) {
		const std::vector<Box> boxes = SelectionBoxes(world, target.Value().block);
		Box b = boxes.empty() ? Box{} : boxes.front();
		for (const Box &o : boxes) {
			b.min = {std::min(b.min.x, o.min.x), std::min(b.min.y, o.min.y), std::min(b.min.z, o.min.z)};
			b.max = {std::max(b.max.x, o.max.x), std::max(b.max.y, o.max.y), std::max(b.max.z, o.max.z)};
		}
		m_outline->SetPosition(b.Center());
		m_outline->SetScale(b.Size());
		m_outline->SetVisible(true);
	} else {
		m_outline->SetVisible(false);
	}
	UpdateSky();
}

std::shared_ptr<render3d::Mesh> WorldRenderer::EntityMesh(const Visual &visual, const String &mod, std::string &key) {
	if (visual.block != AIR) {
		key = "block:" + std::to_string(visual.block);
	} else {
		key = "shape:" + std::to_string(int(visual.shape.kind)) + ":" + std::string(visual.shape.model.CStr()) + ":" +
			  std::to_string(visual.shape.size.x) + "," + std::to_string(visual.shape.size.y) + "," + std::to_string(visual.shape.size.z);
		for (const String &face : visual.look.faces)
			key += std::string(":") + face.CStr();
		for (const Box &b : visual.shape.boxes)
			key += ":" + std::to_string(b.min.x + b.max.y * 3.f + b.max.z * 7.f);
	}
	if (auto it = m_meshCache.find(key); it != m_meshCache.end())
		return it->second;
	const MeshBuffer buffer = visual.block != AIR ? m_mesher->BuildBlock(visual.block) : m_mesher->BuildShape(visual.shape, visual.look, 0, mod);
	auto mesh = std::make_shared<render3d::Mesh>(ToMesh(buffer));
	m_meshCache[key] = mesh;
	return mesh;
}

void WorldRenderer::DropView(EntityView &view) {
	if (view.shape)
		(void)m_entityRoot->RemoveChild(view.shape);
	if (view.rigRoot)
		(void)m_entityRoot->RemoveChild(view.rigRoot);
	view.shape = nullptr;
	view.rigRoot = nullptr;
	view.parts.clear();
	view.rig.reset();
}

const WorldRenderer::Skin *WorldRenderer::SkinFor(const String &skin, bool translucent) {
	const std::string key = std::string(skin.CStr()) + (translucent ? "#t" : "");
	if (auto it = m_skins.find(key); it != m_skins.end())
		return it->second.get();
	auto entry = std::make_unique<Skin>();
	Image pixels;
	if (std::shared_ptr<TextureImage> tex = m_game.Assets().Texture(skin); tex && !tex->Pixels().IsEmpty())
		pixels = tex->Pixels();
	else if (auto file = LoadImageFile(skin); file.IsOk()) // modèle .obj : image à côté du fichier
		pixels = std::move(file).Unwrap();
	if (pixels.IsEmpty()) { // damier : skin introuvable
		pixels.width = pixels.height = 2;
		pixels.rgba = {255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 255, 255};
	}
	auto texture = render3d::CreateTextureFromPixels(m_canvas.Device(), uint32_t(pixels.width), uint32_t(pixels.height), pixels.rgba);
	entry->width = pixels.width;
	entry->height = pixels.height;
	entry->material = m_entity;
	if (texture.IsOk()) {
		entry->texture = Some(std::move(texture).Unwrap());
		entry->material.albedo = Some(MakeRef(entry->texture.Value()));
	}
	entry->material.blend = translucent;
	const Skin *out = entry.get();
	m_skins[key] = std::move(entry);
	return out;
}

void WorldRenderer::UpdateRig(ecs::Entity e, EntityView &view, const Transform &t, const Visual &visual, const math::FVector3 &at,
							  float dt) {
	ecs::ArchetypeRegistry &registry = m_game.Registry();
	const String skinId = !visual.skin.IsEmpty() ? visual.skin : !visual.rig.skins.empty() ? visual.rig.skins.front() : String();
	const std::string key = std::string("rig:") + visual.rig.model.CStr() + "|" + skinId.CStr();
	if (view.meshKey != key || !view.rigRoot) {
		DropView(view);
		view.meshKey = key;
		view.rig = m_game.Assets().Rig(visual.rig.model);
		if (!view.rig)
			return;
		const String skin = skinId.IsEmpty() ? view.rig->texture : skinId;
		const bool translucent = visual.look.material == RenderMaterial::TRANSLUCENT || visual.look.material == RenderMaterial::TRANSPARENT;
		const Skin *material = SkinFor(skin, translucent);
		auto root = std::make_unique<render3d::Object3D>();
		view.rigRoot = root.get();
		m_entityRoot->Add(std::move(root));
		for (size_t i = 0; i < view.rig->parts.size(); ++i) {
			const RigPart &part = view.rig->parts[i];
			const std::string meshKey = std::string("rigpart:") + visual.rig.model.CStr() + ":" + std::to_string(i) + ":" +
										std::to_string(material->width) + "x" + std::to_string(material->height);
			std::shared_ptr<render3d::Mesh> mesh;
			if (auto it = m_meshCache.find(meshKey); it != m_meshCache.end()) {
				mesh = it->second;
			} else {
				const ModelMesh built = BuildRigPart(*view.rig, part, material->width, material->height);
				std::vector<render3d::Vertex3D> vertices;
				for (size_t k = 0; k < built.positions.size(); ++k)
					vertices.push_back(render3d::Vertex3D{built.positions[k], built.normals[k], built.uvs[k], sdl3::Color::WHITE()});
				mesh = std::make_shared<render3d::Mesh>(std::move(vertices), built.indices);
				m_meshCache[meshKey] = mesh;
			}
			auto shape = std::make_unique<render3d::Shape>(mesh, material->material);
			render3d::Object3D *node = shape.get();
			const int parent = part.parent.IsEmpty() ? -1 : view.rig->Find(part.parent);
			math::FVector3 pivot = part.pivot * view.rig->scale;
			if (parent >= 0)
				pivot = pivot - view.rig->parts[size_t(parent)].pivot * view.rig->scale;
			node->SetPosition(pivot);
			(parent >= 0 ? *view.parts[size_t(parent)] : *view.rigRoot).Add(std::move(shape));
			view.parts.push_back(node);
		}
	}
	if (!view.rig)
		return;
	// Marche : la phase avance avec la vitesse horizontale.
	float speed = 0.f;
	if (auto v = registry.GetComponent<Velocity>(e); v.IsSome())
		speed = std::sqrt(v.Unwrap()->linear.x * v.Unwrap()->linear.x + v.Unwrap()->linear.z * v.Unwrap()->linear.z);
	view.walkPhase = std::fmod(view.walkPhase + speed * dt * 2.6f, 6.2831853f * 64.f);
	const float amount = std::clamp(speed / 2.5f, 0.f, 1.f);
	const float time = float(m_game.GameTime()) + float(e.id) * 0.37f;
	for (size_t i = 0; i < view.parts.size(); ++i) {
		const math::FVector3 r = PoseRigPart(view.rig->parts[i], view.walkPhase, amount, time, t.pitch) * (3.14159265f / 180.f);
		view.parts[i]->SetRotation(math::FQuaternion::FromAxisAngle({0.f, 0.f, 1.f}, r.z) *
								   math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, r.y) *
								   math::FQuaternion::FromAxisAngle({1.f, 0.f, 0.f}, r.x));
	}
	float scale = visual.scale * visual.rig.scale;
	if (view.rig->unitHeight)
		if (auto c = registry.GetComponent<Collider>(e); c.IsSome())
			scale *= c.Unwrap()->size.y;
	view.rigRoot->SetPosition(at);
	view.rigRoot->SetRotation(math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, t.yaw + visual.rig.yaw * (3.14159265f / 180.f)));
	view.rigRoot->SetScale({scale, scale, scale});
}

void WorldRenderer::UpdateEntities(float alpha) {
	ecs::ArchetypeRegistry &registry = m_game.Registry();
	const double now = m_game.GameTime();
	const float dt = m_lastTime < 0.0 ? 0.f : std::clamp(float(now - m_lastTime), 0.f, 0.1f);
	m_lastTime = now;
	std::unordered_map<uint64_t, bool> seen;
	registry.Query<Transform, Visual>([&](ecs::Entity e, Transform &t, Visual &visual) {
		if (!visual.visible)
			return;
		const uint64_t key = EntityKey(e);
		seen[key] = true;
		EntityView &view = m_entities[key];
		// Position interpolée entre les deux derniers pas de physique.
		const math::FVector3 p = t.previous + (t.position - t.previous) * alpha;
		if (visual.rig.IsSet()) {
			UpdateRig(e, view, t, visual, p, dt);
			return;
		}
		if (view.rigRoot)
			DropView(view);
		String mod;
		if (auto kind = registry.GetComponent<EntityKind>(e); kind.IsSome() && kind.Unwrap()->def)
			mod = kind.Unwrap()->def->mod;
		std::string meshKey;
		std::shared_ptr<render3d::Mesh> mesh = EntityMesh(visual, mod, meshKey);
		if (!view.shape) {
			auto shape = std::make_unique<render3d::Shape>(mesh, m_entity);
			view.shape = shape.get();
			m_entityRoot->Add(std::move(shape));
		} else if (view.meshKey != meshKey) {
			view.shape->SetGeometry(mesh);
		}
		view.meshKey = meshKey;
		const bool translucent = visual.look.material == RenderMaterial::TRANSLUCENT || visual.look.material == RenderMaterial::TRANSPARENT;
		view.shape->Materials() = {translucent ? m_translucent : m_entity};
		float scale = visual.scale;
		float spin = t.yaw;
		math::FVector3 at = p;
		if (registry.HasComponent<DroppedItem>(e)) {
			// Objet au sol : petit, qui tourne et flotte.
			spin = float(std::fmod(now * 1.5, 6.2831853));
			at.y += 0.15f + 0.08f * float(std::sin(now * 2.0 + double(e.id)));
		} else if (registry.HasComponent<FallingBlock>(e)) {
			scale = 1.f;
		}
		view.shape->SetPosition(at);
		view.shape->SetRotation(math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, spin));
		view.shape->SetScale({scale, scale, scale});
	});
	for (auto it = m_entities.begin(); it != m_entities.end();) {
		if (!seen.contains(it->first)) {
			DropView(it->second);
			it = m_entities.erase(it);
		} else {
			++it;
		}
	}
}

void WorldRenderer::UpdateSky() {
	// Soleil : tourne autour de l'axe X ; 0,25 lever, 0,5 midi, 0,75 coucher.
	const float day = m_game.DayFraction();
	const float angle = (day - 0.25f) * 6.2831853f;
	const float height = std::sin(angle); // > 0 : jour
	const float daylight = std::clamp(height * 2.5f + 0.35f, 0.08f, 1.f);
	std::array<uint8_t, 3> sky{140, 190, 245};
	if (m_game.LocalPlayer().Valid())
		if (auto t = m_game.Registry().GetComponent<Transform>(m_game.LocalPlayer()); t.IsSome())
			if (ChunkGenerator *gen = m_game.GetWorld().Generator())
				if (const BiomeDef *biome = gen->BiomeAt(int(std::floor(t.Unwrap()->position.x)), int(std::floor(t.Unwrap()->position.z))))
					sky = biome->skyColor;
	render3d::DirectionalLight sun;
	sun.direction = math::FVector3{0.3f, -std::max(0.15f, std::fabs(height)), -std::cos(angle)}.Normalize();
	sun.intensity = 0.85f * daylight;
	sun.color = height > 0.f ? sdl3::Color{255, 245, 225} : sdl3::Color{150, 160, 220};
	render3d::AmbientLight ambient;
	ambient.color = Mix({200, 205, 220}, 0.25f + 0.35f * daylight);
	m_canvas.SetLighting(sun, ambient);
	m_canvas.SetBackgroundColor(Mix(sky, daylight));
}

} // namespace voxelgame
