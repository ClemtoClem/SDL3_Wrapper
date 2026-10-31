// Définitions de world_renderer.hpp
#include <voxelia/world_renderer.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include <render3d/texture_loader.hpp>
#include <voxel/json.hpp>
#include <voxel/lighting.hpp>
#include <voxel/rig.hpp>

namespace voxelia {

using namespace voxel;

namespace {

/// Clé d'entité : identifiant + génération (une entité détruite n'est
/// jamais confondue avec celle qui réutilise sa place).
uint64_t EntityKey(ecs::Entity e) { return uint64_t(e.id) | (uint64_t(e.generation) << 32); }

sdl3::Color Mix(const std::array<uint8_t, 3> &a, float k) {
	return sdl3::Color{uint8_t(float(a[0]) * k), uint8_t(float(a[1]) * k), uint8_t(float(a[2]) * k), 255};
}

} // namespace

WorldRenderer::WorldRenderer(Game &game, render3d::Canvas &canvas, int meshThreads) : m_game(game), m_canvas(canvas) {
	m_chunkRoot = &m_root.Add(std::make_unique<render3d::Object3D>());
	m_entityRoot = &m_root.Add(std::make_unique<render3d::Object3D>());
	m_effectRoot = &m_root.Add(std::make_unique<render3d::Object3D>());
	m_cloudRoot = &m_root.Add(std::make_unique<render3d::Object3D>());
	if (auto mutex = sdl3::Mutex::Create())
		m_resultsMutex = Some(std::move(mutex.Value()));
	const bool threaded = meshThreads > 0 && m_resultsMutex.IsSome();
	for (int i = 0; i < std::max(1, meshThreads); ++i)
		m_renderThreads.push_back(std::make_unique<Worker>(String::Format("voxel-render-%d", i), threaded));
}

WorldRenderer::~WorldRenderer() {
	m_renderThreads.clear(); // maillages en cours terminés avant de défaire quoi que ce soit
	if (m_skyboxActive)
		m_canvas.ClearSkybox(); // le Canvas ne garde qu'une référence vers nos cubemaps
}

const sdl3::GpuTexture *WorldRenderer::Skybox(const String &texture) {
	auto it = m_skyboxes.find(texture.CStr());
	if (it != m_skyboxes.end())
		return it->second.get();
	std::unique_ptr<sdl3::GpuTexture> built;
	if (std::shared_ptr<TextureImage> image = m_game.Assets().Texture(texture)) {
		const Image &img = image->Pixels();
		const int size = img.width / 4;
		if (size > 0 && img.height == size * 3) {
			// Croix horizontale : dessus (1, 0) ; -X, +Z, +X, -Z sur la ligne du
			// milieu ; dessous (1, 2). Ordre des couches GPU : +X, -X, +Y, -Y, +Z, -Z.
			static constexpr int CELLS[6][2] = {{2, 1}, {0, 1}, {1, 0}, {1, 2}, {1, 1}, {3, 1}};
			std::vector<std::vector<uint8_t>> faces(6, std::vector<uint8_t>(size_t(size) * size_t(size) * 4));
			std::array<const uint8_t *, 6> pointers{};
			for (size_t f = 0; f < 6; ++f) {
				for (int y = 0; y < size; ++y) {
					const uint8_t *src = img.At(CELLS[f][0] * size, CELLS[f][1] * size + y);
					std::copy(src, src + size_t(size) * 4, faces[f].begin() + std::ptrdiff_t(size_t(y) * size_t(size) * 4));
				}
				pointers[f] = faces[f].data();
			}
			auto cubemap = render3d::CreateCubemapFromFaces(m_canvas.Device(), pointers, uint32_t(size));
			if (cubemap.IsOk())
				built = std::make_unique<sdl3::GpuTexture>(std::move(cubemap.Value()));
		}
	}
	const sdl3::GpuTexture *result = built.get();
	m_skyboxes[texture.CStr()] = std::move(built);
	return result;
}

render3d::Mesh WorldRenderer::ToMesh(const MeshBuffer &buffer) {
	std::vector<render3d::Vertex3D> vertices;
	vertices.reserve(buffer.vertices.size());
	for (const MeshVertex &v : buffer.vertices)
		vertices.push_back(render3d::Vertex3D{v.position, v.normal, v.uv, sdl3::Color{v.color[0], v.color[1], v.color[2], v.color[3]},
											  math::FVector4{v.light[0], v.light[1], v.light[2], v.light[3]}});
	return render3d::Mesh(std::move(vertices), buffer.indices);
}

Result<bool, String> WorldRenderer::Initialize() {
	// Textures fines (papiers peints, néons… en 2048 px) gardées jusqu'à
	// 256 px par bloc, avec leurs mipmaps (pas de moiré au loin).
	m_atlas.Build(m_game.Assets(), m_game.Content(), 16, 256);
	(void)m_game.Assets().ReleaseUnused(); // les images sources, souvent grandes, ne servent plus
	const Image &pixels = m_atlas.Pixels();
	std::vector<std::span<const uint8_t>> mipmaps;
	for (const Image &level : m_atlas.Mipmaps())
		mipmaps.emplace_back(level.rgba);
	auto texture = render3d::CreateTextureFromPixels(m_canvas.Device(), uint32_t(pixels.width), uint32_t(pixels.height), pixels.rgba,
													 mipmaps);
	if (texture.IsError())
		return Err(texture.Error());
	m_atlasTexture = Some(std::move(texture).Unwrap());
	m_mesher = std::make_unique<ChunkMesher>(m_game.Content(), m_atlas, &m_game.Assets());
	for (size_t i = 0; i < m_renderThreads.size(); ++i)
		m_jobMeshers.push_back(std::make_unique<ChunkMesher>(m_game.Content(), m_atlas, &m_game.Assets()));

	m_opaque = render3d::Material::Default();
	m_opaque.albedo = Some(MakeRef(m_atlasTexture.Value()));
	m_opaque.nearestFilter = true;
	m_opaque.roughness = 1.f; // pas de reflet spéculaire sur la pierre
	m_opaque.bakedLighting = true; // lumière des sommets (voxel/lighting.hpp)
	m_translucent = m_opaque;
	m_translucent.blend = true;
	m_translucent.doubleSided = true; // l'eau vue d'en dessous
	m_entity = m_opaque;
	m_emissive = m_opaque;
	m_emissive.lit = false; // une source de lumière n'est pas assombrie par la scène
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

void WorldRenderer::PlaceMesh(render3d::Shape *&slot, std::shared_ptr<render3d::Mesh> mesh, const render3d::Material &material,
							  const math::FVector3 &origin) {
	if (!mesh) {
		if (slot) {
			(void)m_chunkRoot->RemoveChild(slot);
			slot = nullptr;
		}
		return;
	}
	if (!slot) {
		auto shape = std::make_unique<render3d::Shape>(std::move(*mesh), material);
		shape->SetPosition(origin);
		slot = shape.get();
		m_chunkRoot->Add(std::move(shape));
	} else {
		slot->SetGeometry(std::move(*mesh));
	}
}

void WorldRenderer::QueueMeshing(const std::vector<ChunkPos> &batch) {
	const World &world = m_game.GetWorld();
	// Ce que lit le maillage d'un chunk : ses voisins (faces, occlusion), et
	// tout ce qu'atteignent les rayons de la lumière — sources jusqu'à leur
	// portée, part de ciel (rayons de lighting::MAX_RANGE blocs).
	const float reach = std::max(world.MaxLightRange(), lighting::MAX_RANGE) + 1.f;
	std::unordered_set<ChunkPos> needed;
	for (const ChunkPos &p : batch) {
		const math::FVector3 origin = p.Origin().ToVector();
		const ChunkPos lo = ChunkOf(BlockPos::FromWorld(origin - math::FVector3{reach + 1.f, reach + 1.f, reach + 1.f}));
		const ChunkPos hi = ChunkOf(BlockPos::FromWorld(origin + math::FVector3{float(CHUNK_SIZE) + reach, float(CHUNK_SIZE) + reach,
																			   float(CHUNK_SIZE) + reach}));
		for (int y = lo.y; y <= hi.y; ++y)
			for (int z = lo.z; z <= hi.z; ++z)
				for (int x = lo.x; x <= hi.x; ++x)
					if (world.GetChunk({x, y, z}))
						needed.insert({x, y, z});
	}
	// Copies figées : seules celles des chunks qui ont changé sont refaites.
	std::vector<std::shared_ptr<Chunk>> frozen;
	frozen.reserve(needed.size());
	for (const ChunkPos &c : needed) {
		const Chunk *live = world.GetChunk(c);
		auto &[revision, copy] = m_frozen[c];
		if (!copy || revision != live->Revision()) {
			copy = std::make_shared<Chunk>(*live);
			revision = live->Revision();
			(void)copy->LightCells(world.Content()); // cache rempli ici : les fils de rendu ne font que le lire
		}
		frozen.push_back(copy);
	}
	std::shared_ptr<const World> snapshot = World::Snapshot(world, frozen);
	for (const ChunkPos &p : batch)
		m_inFlight[p] = InFlight{world.GetChunk(p)->Revision(), m_meshSerial};
	// Un chunk par tâche, au fil de rendu le moins chargé (l'instantané est partagé).
	for (const ChunkPos &p : batch) {
		size_t worker = 0;
		for (size_t i = 1; i < m_renderThreads.size(); ++i)
			if (m_renderThreads[i]->Pending() < m_renderThreads[worker]->Pending())
				worker = i;
		ChunkMesher *mesher = m_jobMeshers[worker].get();
		m_renderThreads[worker]->Post([this, mesher, snapshot, p, options = m_mesher->options, serial = m_meshSerial]() {
			// Fil de rendu : ne lit que l'instantané (et le contenu, immuable).
			const uint64_t start = sdl3::GetPerformanceCounter();
			mesher->options = options;
			const ChunkMesh mesh = mesher->Build(*snapshot, p);
			MeshResult result;
			result.pos = p;
			result.revision = mesh.revision;
			result.serial = serial;
			if (!mesh.opaque.IsEmpty())
				result.opaque = std::make_shared<render3d::Mesh>(ToMesh(mesh.opaque));
			if (!mesh.translucent.IsEmpty())
				result.translucent = std::make_shared<render3d::Mesh>(ToMesh(mesh.translucent));
			if (!mesh.emissive.IsEmpty())
				result.emissive = std::make_shared<render3d::Mesh>(ToMesh(mesh.emissive));
			result.milliseconds =
				float(double(sdl3::GetPerformanceCounter() - start) * 1000.0 / double(sdl3::GetPerformanceFrequency()));
			const Option<sdl3::MutexGuard> lock = LockResults();
			m_results.push_back(std::move(result));
		});
	}
}

Option<sdl3::MutexGuard> WorldRenderer::LockResults() {
	if (m_resultsMutex.IsNone())
		return NONE;
	return Some(sdl3::MutexGuard(m_resultsMutex.Value()));
}

void WorldRenderer::ApplyMeshes() {
	std::vector<MeshResult> done;
	{
		const Option<sdl3::MutexGuard> lock = LockResults();
		done.swap(m_results);
	}
	const World &world = m_game.GetWorld();
	for (MeshResult &r : done) {
		if (auto it = m_inFlight.find(r.pos); it != m_inFlight.end() && it->second.revision == r.revision && it->second.serial == r.serial)
			m_inFlight.erase(it);
		// Réglages changés, autre dimension ou chunk parti : ce maillage ne vaut plus.
		if (r.serial != m_meshSerial || !world.GetChunk(r.pos))
			continue;
		m_meshMs = m_meshMs <= 0.f ? r.milliseconds : m_meshMs * 0.95f + r.milliseconds * 0.05f;
		ChunkView &view = m_chunks[r.pos];
		const math::FVector3 origin = r.pos.Origin().ToVector();
		PlaceMesh(view.opaque, std::move(r.opaque), m_opaque, origin);
		PlaceMesh(view.translucent, std::move(r.translucent), m_translucent, origin);
		PlaceMesh(view.emissive, std::move(r.emissive), m_emissive, origin);
		view.revision = r.revision;
	}
}

void WorldRenderer::Update(const render3d::Camera &camera, float alpha, const Option<BlockHit> &target) {
	m_lastCamera = camera.position;
	if (!m_game.HasWorld() || !m_mesher)
		return;
	World &world = m_game.GetWorld();
	// Autre dimension : le monde a été remplacé, rien de l'ancien ne vaut plus.
	if (m_game.WorldSerial() != m_worldSerial) {
		m_worldSerial = m_game.WorldSerial();
		// Lumière du ciel : selon la dimension (pas sous des îles flottantes).
		const DimensionDef *dim = m_game.CurrentDimensionDef();
		m_mesher->options.skyLight = !dim || dim->skyLight;
		for (auto &[p, view] : m_chunks) {
			if (view.opaque)
				(void)m_chunkRoot->RemoveChild(view.opaque);
			if (view.translucent)
				(void)m_chunkRoot->RemoveChild(view.translucent);
			if (view.emissive)
				(void)m_chunkRoot->RemoveChild(view.emissive);
		}
		m_chunks.clear();
		for (auto &[key, view] : m_entities)
			DropView(view);
		m_entities.clear();
		m_frozen.clear();
		m_inFlight.clear();
		++m_meshSerial; // les maillages de l'ancien monde, s'il en revient, sont jetés
	}
	// Chunks déchargés : leurs formes partent.
	for (auto it = m_chunks.begin(); it != m_chunks.end();) {
		if (!world.GetChunk(it->first)) {
			if (it->second.opaque)
				(void)m_chunkRoot->RemoveChild(it->second.opaque);
			if (it->second.translucent)
				(void)m_chunkRoot->RemoveChild(it->second.translucent);
			if (it->second.emissive)
				(void)m_chunkRoot->RemoveChild(it->second.emissive);
			it = m_chunks.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = m_frozen.begin(); it != m_frozen.end();)
		it = world.GetChunk(it->first) ? std::next(it) : m_frozen.erase(it);
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
		if (auto f = m_inFlight.find(p); f != m_inFlight.end() && f->second.revision == chunk->Revision() && f->second.serial == m_meshSerial)
			continue; // déjà sur le fil de rendu
		// Première fois : attendre (un peu) ses voisins horizontaux — leurs
		// blocs et leurs lampes le feraient refaire aussitôt.
		if (built == 0 && !chunk->IsEmpty()) {
			bool missing = false;
			for (int dz = -1; dz <= 1 && !missing; ++dz)
				for (int dx = -1; dx <= 1 && !missing; ++dx)
					missing = !world.GetChunk({p.x + dx, p.y, p.z + dz});
			if (missing && ++m_waiting[p] < 90)
				continue;
		}
		m_waiting.erase(p);
		const math::FVector3 center = p.Origin().ToVector() + math::FVector3{8.f, 8.f, 8.f};
		dirty.push_back({(center - camera.position).Length(), p});
	}
	std::sort(dirty.begin(), dirty.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	m_pending = dirty.size() + m_inFlight.size();
	// Quelques chunks par image, et une avance limitée pour les fils de rendu
	// (les plus proches passent devant au fil des images).
	const size_t ahead = rebuildBudget * (2 + m_renderThreads.size());
	std::vector<ChunkPos> batch;
	for (const auto &[distance, p] : dirty) {
		if (batch.size() >= rebuildBudget || m_inFlight.size() + batch.size() >= ahead)
			break;
		batch.push_back(p);
	}
	if (!batch.empty())
		QueueMeshing(batch);
	ApplyMeshes();
	m_triangles = 0;
	for (const auto &[p, view] : m_chunks) {
		if (view.opaque)
			m_triangles += view.opaque->Geometry().IndexCount() / 3;
		if (view.translucent)
			m_triangles += view.translucent->Geometry().IndexCount() / 3;
		if (view.emissive)
			m_triangles += view.emissive->Geometry().IndexCount() / 3;
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
	UpdateHighlights();
	UpdateSky();
}

void WorldRenderer::UpdateHighlights() {
	const std::vector<Game::Highlight> &wanted = m_game.Highlights();
	auto same = [](const Game::Highlight &a, const Game::Highlight &b) {
		return a.id == b.id && a.min == b.min && a.max == b.max && a.color == b.color;
	};
	if (wanted.size() == m_shownHighlights.size() && std::equal(wanted.begin(), wanted.end(), m_shownHighlights.begin(), same))
		return;
	m_shownHighlights = wanted;
	if (m_highlightShape) {
		(void)m_root.RemoveChild(m_highlightShape);
		m_highlightShape = nullptr;
	}
	if (wanted.empty())
		return;
	// Douze baguettes par boîte, le long des arêtes, un peu à l'extérieur des blocs.
	std::vector<render3d::Vertex3D> vertices;
	std::vector<uint32_t> indices;
	auto cuboid = [&](const math::FVector3 &lo, const math::FVector3 &hi, sdl3::Color color) {
		static const int FACES[6][4][3] = {
			{{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}},
			{{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, {{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}},
			{{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}}, {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},
		};
		static const math::FVector3 NORMALS[6] = {{1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
												  {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, -1.f}};
		for (int f = 0; f < 6; ++f) {
			const uint32_t base = uint32_t(vertices.size());
			for (const auto &c : FACES[f])
				vertices.push_back(render3d::Vertex3D{{c[0] ? hi.x : lo.x, c[1] ? hi.y : lo.y, c[2] ? hi.z : lo.z}, NORMALS[f], {0.f, 0.f}, color});
			for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
				indices.push_back(base + i);
		}
	};
	for (const Game::Highlight &h : wanted) {
		const float t = 0.035f, m = 0.01f; // demi-épaisseur, marge
		const math::FVector3 lo = h.min.ToVector() - math::FVector3{m, m, m};
		const math::FVector3 hi = (h.max + BlockPos{1, 1, 1}).ToVector() + math::FVector3{m, m, m};
		const sdl3::Color color{h.color[0], h.color[1], h.color[2], 255};
		const float l[3] = {lo.x, lo.y, lo.z}, u[3] = {hi.x, hi.y, hi.z};
		for (int axis = 0; axis < 3; ++axis)
			for (int a = 0; a < 2; ++a)
				for (int b = 0; b < 2; ++b) {
					float mn[3], mx[3];
					for (int k = 0; k < 3; ++k) {
						if (k == axis) {
							mn[k] = l[k] - t;
							mx[k] = u[k] + t;
						} else {
							const float c = (k == (axis + 1) % 3 ? (a ? u[k] : l[k]) : (b ? u[k] : l[k]));
							mn[k] = c - t;
							mx[k] = c + t;
						}
					}
					cuboid({mn[0], mn[1], mn[2]}, {mx[0], mx[1], mx[2]}, color);
				}
	}
	auto shape = std::make_unique<render3d::Shape>(render3d::Mesh(std::move(vertices), std::move(indices)),
												   render3d::Material::Unlit(sdl3::Color::WHITE()));
	m_highlightShape = shape.get();
	m_root.Add(std::move(shape));
}

std::shared_ptr<render3d::Mesh> WorldRenderer::EntityMesh(const Visual &visual, const String &mod, std::string &key, bool sprite) {
	if (sprite) {
		key = std::string("sprite:") + visual.look.faces[0].CStr();
		if (auto it = m_meshCache.find(key); it != m_meshCache.end())
			return it->second;
		auto mesh = std::make_shared<render3d::Mesh>(ToMesh(m_mesher->BuildSprite(visual.look.faces[0])));
		m_meshCache[key] = mesh;
		return mesh;
	}
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

void WorldRenderer::UpdateStructure(EntityView &view, const Visual &visual, const math::FVector3 &corner) {
	const std::string key = "structure:" + std::to_string(visual.structureSerial);
	if (view.meshKey != key) {
		DropView(view);
		view.meshKey = key;
		// Maillé comme un petit monde : faces, occlusion, lumière de sa lampe.
		const ChunkMesh built = m_mesher->BuildStructure(*visual.structure);
		auto add = [&](const MeshBuffer &buffer, const render3d::Material &material) {
			if (buffer.IsEmpty())
				return;
			auto shape = std::make_unique<render3d::Shape>(std::make_shared<render3d::Mesh>(ToMesh(buffer)), material);
			view.parts.push_back(shape.get());
			view.partShapes.push_back(shape.get());
			m_entityRoot->Add(std::move(shape));
		};
		add(built.opaque, m_opaque);
		add(built.emissive, m_emissive);
		add(built.translucent, m_translucent);
	}
	// Éclairé aussi de l'extérieur, là où il passe ; position interpolée comme les autres entités.
	const BlockPos size = visual.structure->Size();
	const math::FVector4 light = LightAt(corner + math::FVector3{float(size.x) * 0.5f, float(size.y) * 0.5f, float(size.z) * 0.5f});
	for (render3d::Shape *part : view.partShapes) {
		part->SetPosition(corner);
		for (render3d::Material &m : part->Materials())
			if (m.lit)
				m.objectLight = math::FVector4{light.x, light.y, light.z, 1.f};
	}
}

void WorldRenderer::DropView(EntityView &view) {
	if (view.rig == nullptr && view.rigRoot == nullptr)
		for (render3d::Object3D *part : view.parts)
			(void)m_entityRoot->RemoveChild(part); // assemblage : ses formes sont à la racine
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
	view.partShapes.clear();
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
			view.partShapes.push_back(shape.get());
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
	// Éclairé là où il se trouve (à mi-hauteur).
	const math::FVector4 light = LightAt(at + math::FVector3{0.f, 0.5f * scale, 0.f});
	for (render3d::Shape *part : view.partShapes)
		for (render3d::Material &m : part->Materials())
			m.objectLight = light;
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
		const bool shadow = m_settings.shadows > 0 && !far && !visual.structure && !registry.HasComponent<DroppedItem>(e) &&
							!registry.HasComponent<FallingBlock>(e);
		UpdateShadow(view, p, width, shadow);
		if (far)
			return;
		if (visual.rig.IsSet()) {
			UpdateRig(e, view, t, visual, p, dt);
			return;
		}
		if (view.rigRoot)
			DropView(view);
		if (visual.structure) { // assemblage de blocs mobile (cabine d'ascenseur…)
			UpdateStructure(view, visual, p);
			return;
		}
		String mod;
		if (auto kind = registry.GetComponent<EntityKind>(e); kind.IsSome() && kind.Unwrap()->def)
			mod = kind.Unwrap()->def->mod;
		std::string meshKey;
		// Objet au sol qui ne pose pas de bloc (outil, nourriture…) : son image, en plaque mince.
		const bool sprite = visual.block == AIR && visual.shape.kind == ShapeKind::CUBE && registry.HasComponent<DroppedItem>(e);
		std::shared_ptr<render3d::Mesh> mesh = EntityMesh(visual, mod, meshKey, sprite);
		if (!view.shape) {
			auto shape = std::make_unique<render3d::Shape>(mesh, m_entity);
			view.shape = shape.get();
			m_entityRoot->Add(std::move(shape));
		} else if (view.meshKey != meshKey) {
			view.shape->SetGeometry(mesh);
		}
		view.meshKey = meshKey;
		const bool translucent = visual.look.material == RenderMaterial::TRANSLUCENT || visual.look.material == RenderMaterial::TRANSPARENT;
		// Objet ou bloc lumineux (torche au sol, lave qui tombe) : pleine luminosité.
		const int glow = visual.block != AIR ? m_game.Content().Block(visual.block).look.lightEmission : visual.look.lightEmission;
		render3d::Material material = translucent ? m_translucent : glow > 0 ? m_emissive : m_entity;
		material.objectLight = LightAt(p + math::FVector3{0.f, 0.5f * visual.scale, 0.f});
		view.shape->Materials() = {material};
		float scale = visual.scale;
		float spin = t.yaw;
		math::FVector3 at = p;
		if (registry.HasComponent<DroppedItem>(e)) {
			// Objet au sol : petit, qui tourne et flotte ; une image plate, un peu plus grande qu'un bloc miniature.
			spin = float(std::fmod(now * 1.5, 6.2831853));
			if (sprite)
				scale *= 1.8f;
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
	float daylight = std::clamp(height * 2.5f + 0.35f, 0.08f, 1.f);
	const DimensionDef *dim = m_game.CurrentDimensionDef();
	std::array<uint8_t, 3> sky{140, 190, 245};
	if (dim && !dim->skyFromBiome)
		sky = dim->skyColor;
	else if (m_game.LocalPlayer().Valid())
		if (auto t = m_game.Registry().GetComponent<Transform>(m_game.LocalPlayer()); t.IsSome())
			if (ChunkGenerator *gen = m_game.GetWorld().Generator())
				if (const BiomeDef *biome = gen->BiomeAt(int(std::floor(t.Unwrap()->position.x)), int(std::floor(t.Unwrap()->position.z))))
					sky = biome->skyColor;
	// Effets du joueur : obscurité (zones sans lumière), vision nocturne.
	int darkness = 0, nightVision = 0;
	if (auto fx = m_game.Registry().GetComponent<StatusEffects>(m_game.LocalPlayer()); fx.IsSome()) {
		darkness = fx.Unwrap()->Level("darkness");
		nightVision = fx.Unwrap()->Level("night_vision");
	}
	// Éclairage en LINÉAIRE (cf. voxel/lighting.hpp) : les sources des blocs
	// sont dans les sommets ; ici le soleil et l'ambiante du ciel (pondérés
	// par la part de ciel de chaque sommet), la lumière de base de la
	// dimension et l'exposition. Aucun plancher caché : sans ciel ni base ni
	// source, c'est noir.
	auto linear = [](const std::array<uint8_t, 3> &c, float k) {
		const lighting::Rgb l = lighting::SrgbToLinear(c[0], c[1], c[2]);
		const float m = std::max({l[0], l[1], l[2], 1e-4f});
		// Couleur normalisée (composante max = 255) et intensité à part : la
		// précision des 8 bits reste sur la teinte.
		return std::make_pair(sdl3::Color{uint8_t(std::lround(l[0] / m * 255.f)), uint8_t(std::lround(l[1] / m * 255.f)),
										  uint8_t(std::lround(l[2] / m * 255.f)), 255},
							  m * k);
	};
	const bool skyLit = !dim || dim->skyLight;
	render3d::DirectionalLight sun;
	sun.direction = math::FVector3{0.3f, -std::max(0.15f, std::fabs(height)), -std::cos(angle)}.Normalize();
	const float sunUp = std::clamp(height * 2.5f, 0.f, 1.f);
	auto [sunColor, sunIntensity] = height > 0.f ? linear({255, 245, 225}, 2.2f * sunUp) : linear({150, 165, 220}, 0.08f);
	sun.color = sunColor;
	sun.intensity = (dim && !dim->sun) || !skyLit ? 0.f : sunIntensity;
	render3d::AmbientLight ambient;
	float base = dim && dim->baseLight.IsSome() ? dim->baseLight.Value() : 0.f;
	if (skyLit) {
		// Lumière diffuse du ciel, de la teinte du ciel, selon l'heure.
		const std::array<uint8_t, 3> tint{uint8_t((sky[0] + 255) / 2), uint8_t((sky[1] + 255) / 2), uint8_t((sky[2] + 255) / 2)};
		auto [color, intensity] = linear(tint, (0.04f + 0.56f * daylight) * (dim ? dim->ambient : 1.f));
		ambient.color = color;
		ambient.intensity = intensity;
	} else {
		// Monde sans ciel : la part « ambiante » des sommets n'est que
		// l'occlusion ambiante ; la lumière est la base de la dimension
		// (historique : « ambient » quand « base » n'est pas donnée).
		const float level = dim && dim->baseLight.IsSome() ? base : 0.55f * (dim ? dim->ambient : 1.f);
		auto [color, intensity] = linear({255, 250, 240}, level);
		ambient.color = color;
		ambient.intensity = intensity;
		base = 0.f;
	}
	float exposure = (dim ? dim->exposure : 1.f) * (0.75f + 0.5f * m_settings.brightness);
	if (darkness > 0) {
		const float k = 1.f / (1.f + 1.6f * float(darkness));
		exposure *= k;
		daylight *= k;
	}
	if (nightVision > 0)
		base += 0.6f; // la vision nocturne éclaire tout, même au fond d'une grotte
	m_canvas.SetLighting(sun, ambient);
	m_canvas.SetExposure(exposure);
	m_canvas.SetBaseLight(base);
	m_canvas.SetBackgroundColor(Mix(sky, dim && !dim->dayCycle ? std::min(1.f, daylight + 0.2f) : daylight));
	// Ciel en cubemap : celui de nuit quand il fait sombre (s'il existe),
	// assombri selon l'heure et l'obscurité.
	const bool night = dim && !dim->nightCubemap.IsEmpty() && daylight < 0.32f;
	const String skyTexture = !dim ? String() : night ? dim->nightCubemap : dim->cubemap;
	const sdl3::GpuTexture *cubemap = skyTexture.IsEmpty() ? nullptr : Skybox(skyTexture);
	if (cubemap) {
		float k = night ? 0.75f : dim->dayCycle ? std::clamp(daylight * 1.15f, 0.22f, 1.f) : 1.f;
		if (darkness > 0)
			k /= 1.f + 1.6f * float(darkness);
		m_canvas.SetSkybox(*cubemap);
		m_canvas.SetSkyboxTint(k, k, k);
		m_skyboxActive = true;
	} else if (m_skyboxActive) {
		m_canvas.ClearSkybox();
		m_skyboxActive = false;
	}
}

void WorldRenderer::Quiesce() {
	for (std::unique_ptr<Worker> &worker : m_renderThreads)
		worker->Wait();
}

namespace {

/// Textures d'un bloc telles que l'atlas les range (lueur : GlowId).
void BlockTextures(const BlockDef &b, std::vector<String> &out) {
	const bool glow = b.look.lightEmission > 0 && json::Bool(b.raw, "graphics.glow", true);
	for (const String &face : b.look.faces)
		if (!face.IsEmpty()) {
			out.push_back(face);
			if (glow)
				out.push_back(TextureAtlas::GlowId(face));
		}
	if (!b.liquid.flowingTexture.IsEmpty())
		out.push_back(b.liquid.flowingTexture);
}

} // namespace

std::set<std::string> WorldRenderer::TextureReferences() const {
	std::set<std::string> out;
	std::vector<String> textures;
	for (const BlockDef &b : m_game.Content().Blocks())
		BlockTextures(b, textures);
	for (const String &t : textures)
		out.insert(t.CStr());
	return out;
}

bool WorldRenderer::AtlasMissing(const std::vector<String> &blocks, const std::set<std::string> &before) const {
	std::vector<String> textures;
	for (const String &id : blocks)
		if (const BlockDef *b = m_game.Content().FindBlock(id))
			BlockTextures(*b, textures);
	for (const String &t : textures)
		if (!m_atlas.Contains(t) && !before.contains(t.CStr()))
			return true;
	return false;
}

Result<bool, String> WorldRenderer::ReloadContent(bool rebuildAtlas) {
	Quiesce();
	if (rebuildAtlas) {
		// L'ancienne texture peut encore servir à une image en vol.
		(void)m_canvas.Device().WaitIdle();
		m_atlas.Build(m_game.Assets(), m_game.Content(), 16, 256);
		(void)m_game.Assets().ReleaseUnused();
		const Image &pixels = m_atlas.Pixels();
		std::vector<std::span<const uint8_t>> mipmaps;
		for (const Image &level : m_atlas.Mipmaps())
			mipmaps.emplace_back(level.rgba);
		auto texture = render3d::CreateTextureFromPixels(m_canvas.Device(), uint32_t(pixels.width), uint32_t(pixels.height),
														 pixels.rgba, mipmaps);
		if (texture.IsError())
			return Err(texture.Error());
		m_atlasTexture = Some(std::move(texture).Unwrap());
		for (render3d::Material *material : {&m_opaque, &m_translucent, &m_entity, &m_emissive})
			material->albedo = Some(MakeRef(m_atlasTexture.Value()));
	}
	// Les mailleurs précalculent le rendu de chaque bloc (faces, lueur, teintes).
	const ChunkMesher::Options options = m_mesher->options;
	m_mesher = std::make_unique<ChunkMesher>(m_game.Content(), m_atlas, &m_game.Assets());
	m_mesher->options = options;
	for (std::unique_ptr<ChunkMesher> &mesher : m_jobMeshers)
		mesher = std::make_unique<ChunkMesher>(m_game.Content(), m_atlas, &m_game.Assets());
	for (auto &[p, view] : m_chunks)
		view.revision = 0; // remaillés au fil des images (les plus proches d'abord)
	m_inFlight.clear();
	++m_meshSerial; // maillés avec l'ancien contenu : jetés au retour
	m_meshCache.clear();
	for (auto &[key, view] : m_entities)
		DropView(view);
	m_entities.clear();
	++m_contentReloads;
	return Ok(true);
}

void WorldRenderer::Configure(const WorldRenderSettings &settings) {
	const bool remesh = settings.ambientOcclusion != m_settings.ambientOcclusion ||
						std::fabs(settings.vegetationDensity - m_settings.vegetationDensity) > 0.01f;
	m_settings = settings;
	if (m_mesher) {
		m_mesher->options.ambientOcclusion = settings.ambientOcclusion;
		m_mesher->options.crossDensity = std::clamp(settings.vegetationDensity, 0.f, 1.f);
	}
	if (remesh) {
		for (auto &[p, view] : m_chunks)
			view.revision = 0; // reconstruit au fil des images
		m_inFlight.clear();
		++m_meshSerial; // maillés avec les anciens réglages : jetés au retour
	}
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
	// Les lumières des blocs et des entités sont dans les sommets (occultées
	// par la géométrie, voxel/lighting.hpp). Reste celle de l'objet tenu en
	// main (torche, lanterne) : à la place de l'œil, elle n'éclaire que ce que
	// la caméra voit — pas d'occlusion à calculer, et elle suit sans délai.
	LightDef held;
	if (m_settings.dynamicLights > 0)
		if (const ItemStack *stack = m_game.LocalPlayer().Valid() ? m_game.HeldItem(m_game.LocalPlayer()) : nullptr; stack && !stack->IsEmpty())
			if (const ItemDef *item = m_game.Content().FindItem(stack->item)) {
				if (!item->placesBlock.IsEmpty())
					if (const BlockDef *block = m_game.Content().FindBlock(item->placesBlock))
						held = block->look.light;
				if (const int level = json::Int(item->raw, "light", 0); level > 0 && !held.Active()) {
					held.enabled = true;
					held.color = lighting::SrgbToLinear(lighting::LEGACY_COLOR[0], lighting::LEGACY_COLOR[1], lighting::LEGACY_COLOR[2]);
					held.intensity = lighting::LegacyIntensity(level);
					held.range = lighting::LegacyRange(level);
				}
			}
	// Toujours une lumière (une seule variante de shader à compiler).
	render3d::PointLight light;
	light.position = camera.position + math::FVector3{0.f, -0.3f, 0.f};
	light.decay = 2.f; // inverse du carré, comme les sources du monde
	if (held.Active()) {
		const float m = std::max({held.color[0], held.color[1], held.color[2], 1e-4f});
		light.color = sdl3::Color{uint8_t(std::lround(held.color[0] / m * 255.f)), uint8_t(std::lround(held.color[1] / m * 255.f)),
								  uint8_t(std::lround(held.color[2] / m * 255.f)), 255};
		light.intensity = held.intensity * m;
		light.distance = held.range;
	} else {
		light.position = camera.position + math::FVector3{0.f, -10000.f, 0.f};
		light.intensity = 0.f;
		light.distance = 1.f;
	}
	m_canvas.SetLights({&light, 1}, {});
}

math::FVector4 WorldRenderer::LightAt(const math::FVector3 &p) const {
	// Un objet mobile reçoit la lumière là où il se trouve : éclairement
	// sans direction (la moitié : les faces tournées vers la source
	// reçoivent, les autres non), et sa part de ciel.
	const World &world = m_game.GetWorld();
	std::vector<lighting::Source> sources;
	lighting::GatherSources(world, p, p, sources);
	const lighting::Rgb e = lighting::DirectIrradiance(world, p, math::FVector3{0.f, 0.f, 0.f}, sources);
	const DimensionDef *dim = m_game.CurrentDimensionDef();
	const float sky = !dim || dim->skyLight ? lighting::SkyAccess(world, p) : 1.f;
	return math::FVector4{0.5f * e[0], 0.5f * e[1], 0.5f * e[2], sky};
}

void WorldRenderer::UpdateClouds(const render3d::Camera &camera) {
	if (m_cloudTiles.empty())
		return;
	const DimensionDef *dim = m_game.CurrentDimensionDef();
	const bool visible = m_settings.clouds && (!dim || dim->clouds);
	m_cloudRoot->SetVisible(visible);
	if (!visible)
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
		render3d::Material material = m_entity;
		material.objectLight = LightAt(pos.Center());
		auto shape = std::make_unique<render3d::Shape>(ToMesh(buffer), material);
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

} // namespace voxelia
