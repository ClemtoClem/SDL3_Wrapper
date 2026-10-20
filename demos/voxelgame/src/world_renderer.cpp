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
	m_effectRoot = &m_root.Add(std::make_unique<render3d::Object3D>());
	m_cloudRoot = &m_root.Add(std::make_unique<render3d::Object3D>());
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

	// Ombres des entités : disque sombre translucide posé au sol.
	{
		std::vector<render3d::Vertex3D> v;
		std::vector<uint32_t> idx;
		v.push_back(render3d::Vertex3D{{0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.5f, 0.5f}, sdl3::Color{0, 0, 0, 255}});
		const int n = 16;
		for (int i = 0; i <= n; ++i) {
			const float a = float(i) / float(n) * 6.2831853f;
			v.push_back(render3d::Vertex3D{{std::cos(a) * 0.5f, 0.f, std::sin(a) * 0.5f}, {0.f, 1.f, 0.f}, {0.5f, 0.5f}, sdl3::Color{0, 0, 0, 255}});
			if (i > 0) {
				idx.push_back(0);
				idx.push_back(uint32_t(i + 1));
				idx.push_back(uint32_t(i));
			}
		}
		m_shadowMesh = std::make_shared<render3d::Mesh>(std::move(v), std::move(idx));
		m_shadowMaterial = render3d::Material::Unlit(sdl3::Color{0, 0, 0, 90});
		m_shadowMaterial.blend = true;
		m_shadowMaterial.doubleSided = true;
	}
	// Nuages : la carte de nuages du pack (« sky.clouds » de la configuration),
	// en 3 × 3 dalles qui suivent la caméra et dérivent avec le vent.
	if (const String cloudsId = m_game.Content().ConfigStr("sky.clouds"); !cloudsId.IsEmpty())
		if (std::shared_ptr<TextureImage> img = m_game.Assets().Texture(cloudsId); img && !img->Pixels().IsEmpty()) {
			const Image &px = img->Pixels();
			auto tex = render3d::CreateTextureFromPixels(m_canvas.Device(), uint32_t(px.width), uint32_t(px.height), px.rgba);
			if (tex.IsOk()) {
				m_cloudTexture = Some(std::move(tex).Unwrap());
				m_cloudMaterial = render3d::Material::Unlit(sdl3::Color{255, 255, 255, 200});
				m_cloudMaterial.albedo = Some(MakeRef(m_cloudTexture.Value()));
				m_cloudMaterial.blend = true;
				m_cloudMaterial.doubleSided = true;
				m_cloudMaterial.nearestFilter = true;
				const float size = float(json::Num(m_game.Content().Config(), "sky.cloudSize", 384.0));
				std::vector<render3d::Vertex3D> v{
					{{0.f, 0.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f}, sdl3::Color::WHITE()},
					{{size, 0.f, 0.f}, {0.f, -1.f, 0.f}, {1.f, 0.f}, sdl3::Color::WHITE()},
					{{size, 0.f, size}, {0.f, -1.f, 0.f}, {1.f, 1.f}, sdl3::Color::WHITE()},
					{{0.f, 0.f, size}, {0.f, -1.f, 0.f}, {0.f, 1.f}, sdl3::Color::WHITE()}};
				auto mesh = std::make_shared<render3d::Mesh>(std::move(v), std::vector<uint32_t>{0, 1, 2, 0, 2, 3});
				for (int i = 0; i < 9; ++i) {
					auto tile = std::make_unique<render3d::Shape>(mesh, m_cloudMaterial);
					m_cloudTiles.push_back(tile.get());
					m_cloudRoot->Add(std::move(tile));
				}
			}
		}

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
	view.lights = mesh.lights;
}

void WorldRenderer::Update(const render3d::Camera &camera, float alpha, const Option<BlockHit> &target) {
	m_lastCamera = camera.position;
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

	{
		static uint64_t last = 0;
		const uint64_t now = sdl3::GetPerformanceCounter();
		m_frameDt = last == 0 ? 0.f : std::min(0.1f, float(double(now - last) / double(sdl3::GetPerformanceFrequency())));
		last = now;
	}
	UpdateEntities(alpha);
	UpdateLights(camera);
	UpdateClouds(camera);
	UpdateParticles(m_frameDt);

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
	if (view.shadow)
		(void)m_effectRoot->RemoveChild(view.shadow);
	view.shadow = nullptr;
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
		// Trop loin : masquée (réglage « distance des entités »).
		const bool far = (p - m_lastCamera).Length() > m_settings.entityDistance;
		if (view.rigRoot)
			view.rigRoot->SetVisible(!far);
		if (view.shape)
			view.shape->SetVisible(!far);
		float width = 0.6f;
		if (auto c = registry.GetComponent<Collider>(e); c.IsSome())
			width = std::max(c.Unwrap()->size.x, c.Unwrap()->size.z);
		const bool shadow = m_settings.shadows > 0 && !far && !registry.HasComponent<DroppedItem>(e) && !registry.HasComponent<FallingBlock>(e);
		UpdateShadow(view, p, width, shadow);
		if (far)
			return;
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
	sun.intensity = (m_settings.shadows >= 2 ? 1.05f : 0.85f) * daylight;
	sun.color = height > 0.f ? sdl3::Color{255, 245, 225} : sdl3::Color{150, 160, 220};
	render3d::AmbientLight ambient;
	const float light = (0.25f + 0.35f * daylight) * (0.7f + m_settings.brightness * 0.6f) * (m_settings.shadows >= 2 ? 0.85f : 1.f);
	ambient.color = Mix({200, 205, 220}, std::min(1.f, light));
	m_canvas.SetLighting(sun, ambient);
	m_canvas.SetBackgroundColor(Mix(sky, daylight));
}

void WorldRenderer::Configure(const WorldRenderSettings &settings) {
	const bool remesh = settings.ambientOcclusion != m_settings.ambientOcclusion ||
						std::fabs(settings.vegetationDensity - m_settings.vegetationDensity) > 0.01f;
	m_settings = settings;
	if (m_mesher) {
		m_mesher->options.ambientOcclusion = settings.ambientOcclusion;
		m_mesher->options.crossDensity = std::clamp(settings.vegetationDensity, 0.f, 1.f);
	}
	if (remesh)
		for (auto &[p, view] : m_chunks)
			view.revision = 0; // reconstruit au fil des images
	if (m_cloudRoot)
		m_cloudRoot->SetVisible(settings.clouds);
	if (settings.shadows == 0)
		for (auto &[key, view] : m_entities)
			UpdateShadow(view, {0.f, 0.f, 0.f}, 0.f, false);
}

void WorldRenderer::UpdateShadow(EntityView &view, const math::FVector3 &feet, float width, bool visible) {
	if (!visible) {
		if (view.shadow)
			view.shadow->SetVisible(false);
		return;
	}
	if (!view.shadow) {
		auto shape = std::make_unique<render3d::Shape>(m_shadowMesh, m_shadowMaterial);
		view.shadow = shape.get();
		m_effectRoot->Add(std::move(shape));
	}
	// Posée sur le premier bloc solide sous les pieds, plus pâle en hauteur.
	World &world = m_game.GetWorld();
	float ground = feet.y;
	for (int i = 0; i < 6; ++i) {
		const BlockPos below = BlockPos::FromWorld(feet - math::FVector3{0.f, float(i) + 0.01f, 0.f});
		if (world.BlockAt(below).solid) {
			ground = float(below.y + 1);
			break;
		}
		ground = feet.y - float(i + 1);
	}
	const float height = std::max(0.f, feet.y - ground);
	const float size = width * 1.15f * std::max(0.3f, 1.f - height / 6.f);
	view.shadow->SetVisible(height < 6.f);
	view.shadow->SetPosition({feet.x, ground + 0.02f, feet.z});
	view.shadow->SetScale({size, 1.f, size});
}

void WorldRenderer::UpdateLights(const render3d::Camera &camera) {
	const int count = std::clamp(m_settings.dynamicLights, 0, 16);
	if (count == 0) {
		m_canvas.SetLights({}, {});
		return;
	}
	// Les blocs lumineux les plus proches de la caméra (torches, lanternes…).
	std::vector<std::pair<float, std::pair<math::FVector3, int>>> nearest;
	for (const auto &[p, view] : m_chunks) {
		if (view.lights.empty())
			continue;
		const math::FVector3 center = p.Origin().ToVector() + math::FVector3{8.f, 8.f, 8.f};
		if ((center - camera.position).Length() > 48.f)
			continue;
		for (const auto &light : view.lights)
			nearest.push_back({(light.first - camera.position).Length(), light});
	}
	std::partial_sort(nearest.begin(), nearest.begin() + std::min<size_t>(size_t(count), nearest.size()), nearest.end(),
					  [](const auto &a, const auto &b) { return a.first < b.first; });
	// Toujours `count` lumières (une seule variante de shader à compiler).
	std::vector<render3d::PointLight> lights(static_cast<size_t>(count));
	const float daylight = std::clamp(std::sin((m_game.DayFraction() - 0.25f) * 6.2831853f) * 2.5f + 0.35f, 0.08f, 1.f);
	for (size_t i = 0; i < lights.size(); ++i) {
		render3d::PointLight &l = lights[i];
		if (i < nearest.size()) {
			l.position = nearest[i].second.first;
			l.color = sdl3::Color{255, 196, 130, 255};
			l.intensity = float(nearest[i].second.second) / 15.f * (2.2f - 1.4f * daylight);
			l.distance = 3.f + float(nearest[i].second.second) * 0.6f;
			l.decay = 2.f;
		} else {
			l.position = camera.position + math::FVector3{0.f, -10000.f, 0.f};
			l.intensity = 0.f;
			l.distance = 1.f;
		}
	}
	m_canvas.SetLights(lights, {});
}

void WorldRenderer::UpdateClouds(const render3d::Camera &camera) {
	if (m_cloudTiles.empty())
		return;
	m_cloudRoot->SetVisible(m_settings.clouds);
	if (!m_settings.clouds)
		return;
	const float size = float(json::Num(m_game.Content().Config(), "sky.cloudSize", 384.0));
	const float height = float(json::Num(m_game.Content().Config(), "sky.cloudHeight", 128.0));
	m_cloudOffset = std::fmod(m_cloudOffset + m_frameDt * 1.6f, size);
	const float bx = std::floor((camera.position.x - m_cloudOffset) / size) * size + m_cloudOffset;
	const float bz = std::floor(camera.position.z / size) * size;
	for (int i = 0; i < 9; ++i)
		m_cloudTiles[size_t(i)]->SetPosition({bx + float(i % 3 - 1) * size - size * 0.5f, height, bz + float(i / 3 - 1) * size - size * 0.5f});
}

void WorldRenderer::SpawnBreakParticles(BlockId block, const BlockPos &pos, int count) {
	const BlockDef &def = m_game.Content().Block(block);
	const TextureAtlas::Rect r = m_atlas.Lookup(def.look.Texture(Face::SOUTH).IsEmpty() ? def.look.Texture(Face::TOP)
																						 : def.look.Texture(Face::SOUTH));
	for (int i = 0; i < count && m_particles.size() < 240; ++i) {
		const float rx = float(HashPosition(uint32_t(i) * 977u, pos.x, pos.y, pos.z, 1) % 1000u) / 1000.f;
		const float ry = float(HashPosition(uint32_t(i) * 331u, pos.x, pos.y, pos.z, 2) % 1000u) / 1000.f;
		const float rz = float(HashPosition(uint32_t(i) * 613u, pos.x, pos.y, pos.z, 3) % 1000u) / 1000.f;
		// Un petit cube texturé par un coin de la texture du bloc.
		const float du = (r.u1 - r.u0) * 0.25f, dv = (r.v1 - r.v0) * 0.25f;
		const float u0 = r.u0 + (r.u1 - r.u0 - du) * rx, v0 = r.v0 + (r.v1 - r.v0 - dv) * rz;
		MeshBuffer buffer;
		const float h = 0.06f;
		const math::FVector3 c[8] = {{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}};
		const int faces[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
		for (const auto &f4 : faces) {
			const uint32_t base = uint32_t(buffer.vertices.size());
			const math::FVector2 uv[4] = {{u0, v0 + dv}, {u0 + du, v0 + dv}, {u0 + du, v0}, {u0, v0}};
			for (int k = 0; k < 4; ++k) {
				MeshVertex v;
				v.position = c[f4[k]];
				v.normal = (c[f4[0]] + c[f4[2]]).Normalize();
				v.uv = uv[k];
				v.color = def.look.tint;
				buffer.vertices.push_back(v);
			}
			for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
				buffer.indices.push_back(base + k);
		}
		auto shape = std::make_unique<render3d::Shape>(ToMesh(buffer), m_entity);
		Particle part;
		part.shape = shape.get();
		part.position = pos.Center() + math::FVector3{(rx - 0.5f) * 0.6f, (ry - 0.5f) * 0.6f, (rz - 0.5f) * 0.6f};
		part.velocity = {(rx - 0.5f) * 3.f, 2.f + ry * 2.5f, (rz - 0.5f) * 3.f};
		part.life = 0.6f + ry * 0.5f;
		part.spin = rx * 6.f;
		shape->SetPosition(part.position);
		m_effectRoot->Add(std::move(shape));
		m_particles.push_back(part);
	}
}

void WorldRenderer::UpdateParticles(float dt) {
	World &world = m_game.GetWorld();
	for (Particle &p : m_particles) {
		p.life -= dt;
		p.velocity.y -= 14.f * dt;
		math::FVector3 next = p.position + p.velocity * dt;
		if (world.BlockAt(BlockPos::FromWorld(next)).solid) {
			p.velocity = p.velocity * 0.3f;
			p.velocity.y = 0.f;
			next = p.position;
		}
		p.position = next;
		p.spin += dt * 4.f;
		p.shape->SetPosition(p.position);
		p.shape->SetRotation(math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, p.spin));
		const float s = std::clamp(p.life * 2.f, 0.f, 1.f);
		p.shape->SetScale({s, s, s});
	}
	for (auto it = m_particles.begin(); it != m_particles.end();) {
		if (it->life <= 0.f) {
			(void)m_effectRoot->RemoveChild(it->shape);
			it = m_particles.erase(it);
		} else {
			++it;
		}
	}
}

} // namespace voxelgame
