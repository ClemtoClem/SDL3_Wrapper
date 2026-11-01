// Définitions de voxelia/studio/worldgen_editor.hpp
#include <voxelia/studio/worldgen_editor.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <SDL3/SDL.h>
#include <render3d/shape.hpp>
#include <voxel/json.hpp>
#include <voxel/worldgen.hpp>
#include <voxelia/session.hpp>
#include <voxelia/shell.hpp>

namespace voxelia::studio {

namespace json = voxel::json;
using dataio::NodePtr;
using voxel::BlockPos;

namespace {

constexpr float VALIDATE_DELAY = 0.3f;
constexpr int MAP_SIZE = 128;  ///< pixels de la carte des biomes
constexpr int MAP_SCALE = 4;   ///< blocs par pixel

enum class FieldKind : uint8_t { Text, Number, Bool, Json };

struct FieldSpec {
	WorldgenKind kind;
	const char *section, *label, *path;
	FieldKind type;
	float min = 0.f, max = 1.f, step = 1.f, fallback = 0.f;
	bool fallbackBool = false;
};

/// Champs (chemins lus par ContentRegistry::LoadBiome / LoadGenerator / LoadDimension).
const std::vector<FieldSpec> &Fields() {
	using K = WorldgenKind;
	static const std::vector<FieldSpec> fields = {
		{K::Biome, "Identité", "Nom affiché", "displayName", FieldKind::Text},
		{K::Biome, "Identité", "Dimension propre", "dimension", FieldKind::Text},
		{K::Biome, "Climat", "Température", "climate.temperature", FieldKind::Number, 0.f, 1.f, 0.05f, 0.5f},
		{K::Biome, "Climat", "Humidité", "climate.humidity", FieldKind::Number, 0.f, 1.f, 0.05f, 0.5f},
		{K::Biome, "Terrain", "Hauteur", "terrain.height", FieldKind::Number, -128.f, 256.f, 1.f, 64.f},
		{K::Biome, "Terrain", "Variation", "terrain.variation", FieldKind::Number, 0.f, 128.f, 0.5f, 8.f},
		{K::Biome, "Terrain", "Rugosité", "terrain.roughness", FieldKind::Number, 0.f, 1.f, 0.05f, 0.5f},
		{K::Biome, "Couches", "Surface", "blocks.surface", FieldKind::Text},
		{K::Biome, "Couches", "Sous-sol", "blocks.subsurface", FieldKind::Text},
		{K::Biome, "Couches", "Épaisseur du sous-sol", "blocks.subsurfaceDepth", FieldKind::Number, 0.f, 32.f, 1.f, 3.f},
		{K::Biome, "Couches", "Remplissage", "blocks.filler", FieldKind::Text},
		{K::Biome, "Couches", "Sous l'eau", "blocks.underwater", FieldKind::Text},
		{K::Biome, "Ciel", "Ciel [r, g, b]", "sky", FieldKind::Json},
		{K::Biome, "Ciel", "Brume [r, g, b]", "fog", FieldKind::Json},
		{K::Biome, "Décorations", "Décorations", "decorations", FieldKind::Json},
		{K::Generator, "Générateur", "Genre (noise, flat, islands, caverns, maze, script)", "kind", FieldKind::Text},
		{K::Generator, "Générateur", "Niveau de la mer", "seaLevel", FieldKind::Number, -256.f, 256.f, 1.f, 62.f},
		{K::Generator, "Générateur", "Liquide", "fluid", FieldKind::Text},
		{K::Generator, "Générateur", "Socle", "bedrock", FieldKind::Text},
		{K::Generator, "Bruit", "Fréquence", "frequency", FieldKind::Number, 0.0001f, 0.1f, 0.0005f, 0.008f},
		{K::Generator, "Bruit", "Octaves", "octaves", FieldKind::Number, 1.f, 10.f, 1.f, 5.f},
		{K::Generator, "Bruit", "Grottes", "caves", FieldKind::Number, 0.f, 1.f, 0.01f, 0.f},
		{K::Generator, "Biomes", "Biomes (vide : tous)", "biomes", FieldKind::Json},
		{K::Generator, "Plat", "Couches [[bloc, épaisseur]…]", "layers", FieldKind::Json},
		{K::Generator, "Labyrinthe", "maze", "maze", FieldKind::Json},
		{K::Generator, "Labyrinthe", "levels", "levels", FieldKind::Json},
		{K::Generator, "Îles / cavernes", "islands", "islands", FieldKind::Json},
		{K::Generator, "Îles / cavernes", "caverns", "caverns", FieldKind::Json},
		{K::Dimension, "Identité", "Nom affiché", "displayName", FieldKind::Text},
		{K::Dimension, "Identité", "Nom (ancien champ)", "name", FieldKind::Text},
		{K::Dimension, "Identité", "Description", "description", FieldKind::Text},
		{K::Dimension, "Identité", "Générateur", "generator", FieldKind::Text},
		{K::Dimension, "Ciel", "Couleur [r, g, b]", "sky.color", FieldKind::Json},
		{K::Dimension, "Ciel", "Celui du biome", "sky.fromBiome", FieldKind::Bool, 0.f, 1.f, 1.f, 0.f, true},
		{K::Dimension, "Ciel", "Cubemap", "sky.cubemap", FieldKind::Text},
		{K::Dimension, "Ciel", "Soleil", "sky.sun", FieldKind::Bool, 0.f, 1.f, 1.f, 0.f, true},
		{K::Dimension, "Ciel", "Nuages", "sky.clouds", FieldKind::Bool, 0.f, 1.f, 1.f, 0.f, true},
		{K::Dimension, "Temps", "Cycle jour / nuit", "time.cycle", FieldKind::Bool, 0.f, 1.f, 1.f, 0.f, true},
		{K::Dimension, "Temps", "Heure figée (0..1)", "time.fixed", FieldKind::Number, 0.f, 1.f, 0.01f, 0.5f},
		{K::Dimension, "Lumière", "Lumière du ciel", "light.sky", FieldKind::Bool, 0.f, 1.f, 1.f, 0.f, true},
		{K::Dimension, "Lumière", "Ambiante", "light.ambient", FieldKind::Number, 0.f, 4.f, 0.05f, 1.f},
		{K::Dimension, "Lumière", "Base (sans occlusion)", "light.base", FieldKind::Number, 0.f, 4.f, 0.01f, 0.f},
		{K::Dimension, "Lumière", "Exposition", "light.exposure", FieldKind::Number, 0.05f, 20.f, 0.05f, 1.f},
		{K::Dimension, "Physique", "Gravité (facteur)", "physics.gravity", FieldKind::Number, -4.f, 4.f, 0.05f, 1.f},
		{K::Dimension, "Apparition", "Haut", "spawn.top", FieldKind::Number, -512.f, 512.f, 1.f, 192.f},
		{K::Dimension, "Apparition", "Bas", "spawn.bottom", FieldKind::Number, -512.f, 512.f, 1.f, -64.f},
		{K::Dimension, "Apparition", "Y entrer à la mort (causes)", "enterOnDeath", FieldKind::Json},
		{K::Dimension, "Apparition", "Portail d'arrivée", "arrival.portal", FieldKind::Text},
		{K::Dimension, "Apparition", "Musique", "music", FieldKind::Text},
		{K::Dimension, "Apparition", "Graine d'une autre dimension", "seedFrom", FieldKind::Text},
	};
	return fields;
}

NodePtr ParseValue(const String &text) {
	const String t = text.Trim();
	if (t.IsEmpty())
		return nullptr;
	if (Result<NodePtr, String> parsed = json::Parse(t); parsed.IsOk())
		return parsed.Value();
	return json::MakeStr(t);
}

Option<double> Number(const String &s) {
	char *end = nullptr;
	const double v = std::strtod(s.CStr(), &end);
	if (!end || end == s.CStr() || *end != '\0')
		return NONE;
	return Some(v);
}

Option<WorldgenKind> KindOf(const String &s) {
	if (s == "biome")
		return Some(WorldgenKind::Biome);
	if (s == "generator")
		return Some(WorldgenKind::Generator);
	if (s == "dimension")
		return Some(WorldgenKind::Dimension);
	return NONE;
}

/// Couleur d'un biome sur la carte : son herbe, sinon tirée de son id.
std::array<uint8_t, 3> BiomeColor(const voxel::BiomeDef *b) {
	if (!b)
		return {20, 20, 24};
	if (b->grassColor)
		return *b->grassColor;
	const uint64_t h = voxel::HashPosition(0xb10e, int(std::hash<std::string>{}(b->id.CStr()) & 0x7fffffff), 0, 0, 1);
	return {uint8_t(60 + h % 170), uint8_t(60 + (h >> 8) % 170), uint8_t(60 + (h >> 16) % 170)};
}

} // namespace

WorldgenEditor::WorldgenEditor(Shell &shell, StudioReport *report) : m_shell(shell), m_report(report) {}

WorldgenEditor::~WorldgenEditor() = default;

voxel::Game *WorldgenEditor::Game() {
	Session *session = m_shell.Game();
	return session && session->Ready() ? &session->Game() : nullptr;
}

void WorldgenEditor::Visit(const char *feature, FeatureStatus status, String detail) {
	if (m_report)
		m_report->Visit(String(feature), status, std::move(detail));
}

void WorldgenEditor::Changed() {
	++m_version;
	m_validateDelay = VALIDATE_DELAY;
}

// ── Panneau ──────────────────────────────────────────────────────────────────

void WorldgenEditor::Build(ui::Widget parent) {
	ui::ViewportLighting lighting;
	lighting.sun.direction = math::FVector3{-0.4f, -1.f, -0.3f}.Normalize();
	lighting.sun.intensity = 1.f;
	lighting.ambient.color = sdl3::Color{160, 165, 175};
	lighting.background = sdl3::Color{120, 160, 210, 255};
	parent.Append(ui::Column().Grow().Gap(4.f).Children({
		ui::Label("Aucune définition — choisissez une dimension, un générateur ou un biome (onglet « Monde »).").Class("studio-section").Ref(m_title),
		ui::Row().Gap(4.f).Children({
			ui::Button("Appliquer à chaud").OnClick([this] { (void)Apply(); }),
			ui::Button("Enregistrer").OnClick([this] { (void)Save(); }),
			ui::Button("↶").OnClick([this] { (void)Undo(); }),
			ui::Button("↷").OnClick([this] { (void)Redo(); }),
		}),
		ui::Row().Gap(4.f).Children({
			ui::Button("Régénérer dans une dimension temporaire").OnClick([this] { (void)RegenerateTemporary(); }),
			ui::Button("Revenir").OnClick([this] { (void)ReturnFromTemporary(); }),
		}),
		ui::Row().Gap(4.f).Children({
			ui::Button("Aperçu").OnClick([this] {
				const Option<double> side = Number(m_side.Text().Trim()), cut = Number(m_cut.Text().Trim());
				(void)Preview(side.IsSome() ? int(side.Value()) : 4, cut.IsSome() ? Some(int(cut.Value())) : Option<int>(NONE));
			}),
			ui::Label("chunks").Class("studio-status"),
			ui::Input("4", String("4")).Width(40.f).Ref(m_side),
			ui::Label("coupe y (vide : auto)").Class("studio-status"),
			ui::Input("auto").Width(60.f).Ref(m_cut),
		}),
		ui::Viewport3D(m_previewRoot, render3d::Camera{}, true, Some(lighting)).Id("world-preview").Height(220.f).Ref(m_viewport),
		ui::Label("").Class("studio-status").Id("world-status").Ref(m_status),
		ui::Column().Id("world-diagnostics").Ref(m_diagnosticList),
		ui::TabView({String("Propriétés"), String("Carte"), String("Labyrinthe"), String("Réglages"), String("Chaîne"), String("JSON")})
			.Grow()
			.Children({
				ui::ScrollView().Grow().Children({ui::Column().Id("world-form").Gap(2.f).Ref(m_form)}),
				ui::ScrollView().Grow().Children({
					ui::Column().Gap(4.f).Children({
						ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &rect) { DrawBiomeMap(painter, rect); }).Id("world-map").Height(260.f),
						ui::Column().Id("world-legend").Ref(m_legendList),
					}),
				}),
				ui::ScrollView().Grow().Children({ui::Column().Id("world-maze").Ref(m_maze)}),
				ui::ScrollView().Grow().Children({ui::Column().Id("world-settings").Ref(m_settings)}),
				ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &rect) { DrawGraph(painter, rect); }).Id("world-graph").Grow(),
				ui::Column().Grow().Gap(4.f).Children({
					ui::CodeEditor(String()).Id("world-json").Grow().Ref(m_json),
					ui::Row().Gap(4.f).Children({
						ui::Button("Appliquer le JSON").OnClick([this] {
							if (!m_doc.IsSome())
								return;
							Result<bool, String> r = m_doc.Value().SetJson(m_json.Text());
							m_jsonStatus.SetText(r.IsOk() ? String("JSON appliqué au document") : r.Error());
							if (r.IsOk())
								Changed();
						}),
						ui::Label("").Class("studio-status").Ref(m_jsonStatus),
					}),
				}),
			}),
	}));
}

void WorldgenEditor::BuildCatalog(ui::Widget parent) {
	parent.Append(ui::Column().Grow().Gap(4.f).Children({
		ui::Label("Monde").Class("studio-dock-title"),
		ui::Input("Filtrer…").OnTextChange([this](const String &text) {
			m_filter = text.ToLower();
			RebuildCatalog();
		}),
		ui::ScrollView().Id("world-catalog").Grow().Ref(m_catalog),
	}));
	RebuildCatalog();
}

void WorldgenEditor::RebuildCatalog() {
	if (!m_catalog.Valid())
		return;
	m_catalog.Clear();
	voxel::Game *game = Game();
	if (!game) {
		m_catalog.Append(ui::Label("aucune partie en cours").Class("studio-status"));
		return;
	}
	auto section = [&](const char *title, WorldgenKind kind, std::vector<String> ids) {
		std::sort(ids.begin(), ids.end());
		m_catalog.Append(ui::Label(String(title)).Class("studio-section"));
		for (const String &id : ids)
			if (m_filter.IsEmpty() || id.ToLower().Contains(m_filter))
				m_catalog.Append(ui::Label(String("  ") + id).Class("studio-row").OnClick([this, kind, id] { (void)Open(kind, id); }));
	};
	std::vector<String> dims, gens, biomes;
	for (const voxel::DimensionDef *d : game->Content().Dimensions())
		dims.push_back(d->id);
	for (const voxel::GeneratorDef *g : game->Content().Generators())
		gens.push_back(g->id);
	for (const voxel::BiomeDef *b : game->Content().Biomes())
		biomes.push_back(b->id);
	section("Dimensions", WorldgenKind::Dimension, dims);
	section("Générateurs", WorldgenKind::Generator, gens);
	section("Biomes", WorldgenKind::Biome, biomes);
}

void WorldgenEditor::RebuildAll() {
	m_shownVersion = m_version;
	if (!m_doc.IsSome())
		return;
	const WorldgenDocument &doc = m_doc.Value();
	m_title.SetText(String::Format("%s %s%s   %s", WorldgenKindId(doc.Kind()), doc.Id().CStr(), doc.Dirty() ? " ●" : "", doc.SourceFile().CStr()));
	m_json.SetText(doc.Text());
	RebuildForm();
	RebuildMaze();
	RebuildSettings();
}

void WorldgenEditor::RebuildForm() {
	m_form.Clear();
	WorldgenDocument &doc = m_doc.Value();
	const char *section = "";
	for (const FieldSpec &spec : Fields()) {
		if (spec.kind != doc.Kind())
			continue;
		if (std::strcmp(section, spec.section) != 0) {
			section = spec.section;
			m_form.Append(ui::Label(String(section)).Class("studio-section"));
		}
		const NodePtr value = doc.Field(spec.path);
		const String path(spec.path);
		ui::WidgetBuilder editor = ui::Label("");
		switch (spec.type) {
		case FieldKind::Text:
			editor = ui::Input(String(), value ? json::Str(value, "") : String()).Grow().OnSubmit([this, path](const String &text) {
				(void)SetField(path, text.Trim().IsEmpty() ? String() : String("\"") + text.Trim() + String("\""));
			});
			break;
		case FieldKind::Json:
			editor = ui::Input(String("JSON"), value ? CompactJson(json::Encode(value)) : String()).Grow().OnSubmit([this, path](const String &text) {
				(void)SetField(path, text);
			});
			break;
		case FieldKind::Number:
			editor = ui::NumberInput(spec.min, spec.max, value ? float(json::Num(value, "", 0.0)) : spec.fallback, spec.step, 3)
						 .Grow()
						 .OnChange([this, path](float v) { (void)SetField(path, String::Format("%g", double(v))); });
			break;
		case FieldKind::Bool:
			editor = ui::Checkbox(String(), value ? json::Bool(value, "", spec.fallbackBool) : spec.fallbackBool).OnToggle([this, path](bool on) {
				(void)SetField(path, String(on ? "true" : "false"));
			});
			break;
		}
		m_form.Append(ui::Row().Gap(6.f).Children({ui::Label(String(spec.label)).Class("studio-key").Width(170.f), editor}));
	}
}

void WorldgenEditor::RebuildMaze() {
	m_maze.Clear();
	voxel::Game *game = Game();
	if (!game || !m_doc.IsSome())
		return;
	Result<voxel::ContentRegistry, String> scratch = m_doc.Value().Scratch(game->Content());
	if (scratch.IsError())
		return;
	const String dimension = m_doc.Value().PreviewDimension(scratch.Value());
	std::shared_ptr<voxel::TerrainGenerator> gen = game->DimensionGenerator(scratch.Value(), dimension);
	const voxel::MazeLayout layout = gen ? gen->Maze() : voxel::MazeLayout{};
	m_maze.Append(ui::Label(String::Format("dimension montrée : %s · générateur %s (%s) · graine %u", dimension.CStr(),
										   gen ? gen->Definition().id.CStr() : "?", gen ? gen->Definition().kind.CStr() : "?",
										   game->DimensionSeed(dimension))));
	if (layout.floors.empty()) {
		m_maze.Append(ui::Label("Pas un labyrinthe (« maze »)").Class("studio-status"));
		return;
	}
	m_maze.Append(ui::Label(String::Format("cellule %d · hauteur %d · niveaux :", layout.cell, layout.height)).Class("studio-section"));
	String levels;
	for (const String &l : layout.levels)
		levels += (levels.IsEmpty() ? String() : String(", ")) + l;
	m_maze.Append(ui::Label(levels).Class("studio-status"));
	m_maze.Append(ui::Label("Étages (sol, plafond, graine figée, niveau)").Class("studio-section"));
	for (const voxel::MazeLayout::Floor &f : layout.floors)
		m_maze.Append(ui::Label(String::Format("  %d : y %d → %d · graine %u · %s", f.index, f.floorY, f.ceilingY, f.seed,
											   f.level.IsEmpty() ? "régions" : f.level.CStr())));
	m_maze.Append(ui::Label("Gaines d'ascenseur").Class("studio-section"));
	if (layout.shafts.empty())
		m_maze.Append(ui::Label("  aucune").Class("studio-status"));
	for (const voxel::MazeLayout::Shaft &s : layout.shafts)
		m_maze.Append(ui::Label(String::Format("  %s : cellule %d %d, %d × %d, paliers côté %s, étages %d → %d", s.id.CStr(), s.cellX, s.cellZ,
											   s.size, s.size, s.side.CStr(), s.from, s.to)));
}

void WorldgenEditor::RebuildSettings() {
	m_settings.Clear();
	voxel::Game *game = Game();
	if (!game || !m_doc.IsSome())
		return;
	const std::vector<SettingRef> refs = m_doc.Value().Settings();
	if (refs.empty()) {
		m_settings.Append(ui::Label("Aucun réglage de mod référencé (« {\"setting\": …} »)").Class("studio-status"));
		return;
	}
	m_settings.Append(ui::Label("Valeurs figées dans ce monde (modifiables dans le menu des mods du monde)").Class("studio-status"));
	for (const SettingRef &r : refs) {
		NodePtr probe = dataio::Node::MakeObject();
		probe->Set("setting", json::MakeStr(r.id));
		if (!r.fallback.IsEmpty())
			if (Result<NodePtr, String> f = json::Parse(r.fallback); f.IsOk())
				probe->Set("fallback", f.Value());
		const NodePtr resolved = game->ResolveSettings(probe, m_doc.Value().Mod());
		m_settings.Append(ui::Label(String::Format("  %s ← %s = %s%s", r.path.CStr(), r.id.CStr(), CompactJson(json::Encode(resolved)).CStr(),
												   r.fallback.IsEmpty() ? "" : (String(" (repli ") + r.fallback + String(")")).CStr())));
	}
	if (!game->GetWorld().LoadedChunks().empty())
		m_settings.Append(ui::Label("⚠ le monde a déjà généré des chunks : un réglage changé ne vaut que pour les nouveaux").Class("studio-log-warning"));
}

void WorldgenEditor::Revalidate() {
	m_validatedVersion = m_version;
	voxel::Game *game = Game();
	if (!game || !m_doc.IsSome())
		return;
	m_diagnostics = m_doc.Value().Validate(game->Content());
	m_diagnosticList.Clear();
	if (m_diagnostics.empty())
		m_diagnosticList.Append(ui::Label("✓ aucun problème").Class("studio-log-ok"));
	for (const WorldgenDiagnostic &d : m_diagnostics)
		m_diagnosticList.Append(
			ui::Label(d.message).Class(d.severity == WorldgenDiagnostic::Severity::Error ? "studio-log-error" : "studio-log-warning"));
	Visit("world.validate", m_diagnostics.empty() ? FeatureStatus::Ok : FeatureStatus::Warning, String::Format("%zu diagnostic(s)", m_diagnostics.size()));
}

void WorldgenEditor::Update(float dt) {
	if (m_doc.IsSome() && m_version != m_shownVersion)
		RebuildAll();
	m_validateDelay -= dt;
	if (m_doc.IsSome() && m_version != m_validatedVersion && m_validateDelay <= 0.f)
		Revalidate();
	// Caméra de l'aperçu : orbite haute au-dessus du centre.
	m_orbit += dt * 0.2f;
	if (m_preview)
		if (auto vp = m_viewport.Get<ui::Viewport3DState>(); vp.IsSome()) {
			// Recul d'après l'étendue HORIZONTALE (les couches verticales ne comptent pas).
			int x0 = 0, x1 = 0;
			for (size_t i = 0; i < m_preview->chunks.size(); ++i) {
				x0 = i == 0 ? m_preview->chunks[i].x : std::min(x0, m_preview->chunks[i].x);
				x1 = i == 0 ? m_preview->chunks[i].x : std::max(x1, m_preview->chunks[i].x);
			}
			const float r = float(x1 - x0 + 1) * float(voxel::CHUNK_SIZE) * 0.75f;
			const math::FVector3 c{float(m_preview->centre.x), float(m_preview->centre.y), float(m_preview->centre.z)};
			render3d::Camera camera;
			camera.target = c;
			camera.position = c + math::FVector3{std::sin(m_orbit) * r * 1.1f, r * 1.1f, std::cos(m_orbit) * r * 1.1f};
			camera.farPlane = r * 8.f;
			if (vp.Unwrap()->textureHeight > 0)
				camera.aspect = float(vp.Unwrap()->textureWidth) / float(vp.Unwrap()->textureHeight);
			vp.Unwrap()->camera = camera;
		}
}

// ── Dessins ──────────────────────────────────────────────────────────────────

void WorldgenEditor::DrawBiomeMap(ui::Painter &painter, const sdl3::FRect &rect) {
	painter.FillRect(rect, ui::Color{0.12f, 0.13f, 0.16f, 1.f});
	if (m_map.IsEmpty() || !painter.Renderer())
		return;
	if (m_mapTexture.IsNone() || m_mapUploaded != m_mapVersion) {
		auto tex = sdl3::Texture::Create(*painter.Renderer(), m_map.width, m_map.height, sdl3::PixelFormat::RGBA32);
		if (tex.IsError())
			return;
		m_mapTexture = Some(std::move(tex).Unwrap());
		(void)m_mapTexture.Value().SetScaleMode(SDL_SCALEMODE_NEAREST);
		(void)m_mapTexture.Value().Update(m_map.rgba.data(), m_map.width * 4);
		m_mapUploaded = m_mapVersion;
	}
	const float side = std::min(rect.w, rect.h) - 8.f;
	const sdl3::FRect dst{rect.x + 4.f, rect.y + 4.f, side, side};
	painter.Image(m_mapTexture.Value(), dst);
	// Zone de l'aperçu (son centre) : une croix.
	const float cx = dst.x + side * 0.5f, cy = dst.y + side * 0.5f;
	painter.Line({cx - 6.f, cy}, {cx + 6.f, cy}, 2.f, ui::Color{1.f, 1.f, 1.f, 0.9f});
	painter.Line({cx, cy - 6.f}, {cx, cy + 6.f}, 2.f, ui::Color{1.f, 1.f, 1.f, 0.9f});
}

void WorldgenEditor::DrawGraph(ui::Painter &painter, const sdl3::FRect &rect) {
	painter.FillRect(rect, ui::Color{0.12f, 0.13f, 0.16f, 1.f});
	voxel::Game *game = Game();
	if (!game)
		return;
	const DimensionGraph g = BuildDimensionGraph(game->Content());
	// Nœuds en cercle ; « * » (de partout) au centre.
	const size_t n = g.nodes.size();
	const sdl3::FPoint centre{rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
	const float radius = std::max(40.f, std::min(rect.w, rect.h) * 0.38f);
	auto pos = [&](const String &id) -> sdl3::FPoint {
		for (size_t i = 0; i < n; ++i)
			if (g.nodes[i] == id) {
				const float a = float(i) / float(std::max<size_t>(n, 1)) * 6.2831853f - 1.5707963f;
				return {centre.x + std::cos(a) * radius, centre.y + std::sin(a) * radius};
			}
		return centre;
	};
	sdl3::Font *font = painter.GetFonts() ? painter.GetFonts()->Resolve(StringView(""), 13.f) : nullptr;
	for (const DimensionLink &l : g.links) {
		const sdl3::FPoint a = pos(l.from), b = pos(l.to);
		const ui::Color c = l.kind == "death" ? ui::Color{0.95f, 0.4f, 0.4f, 0.8f}
						  : l.kind == "elevator" ? ui::Color{0.4f, 0.8f, 1.f, 0.9f}
												 : ui::Color{0.8f, 0.6f, 1.f, 0.7f};
		painter.Line(a, b, 2.f, c);
		// Pointe de flèche à 85 % du trajet.
		const float dx = b.x - a.x, dy = b.y - a.y, len = std::max(1.f, std::sqrt(dx * dx + dy * dy));
		const sdl3::FPoint tip{a.x + dx * 0.85f, a.y + dy * 0.85f};
		const float ux = dx / len, uy = dy / len;
		painter.Line(tip, {tip.x - ux * 9.f - uy * 5.f, tip.y - uy * 9.f + ux * 5.f}, 2.f, c);
		painter.Line(tip, {tip.x - ux * 9.f + uy * 5.f, tip.y - uy * 9.f - ux * 5.f}, 2.f, c);
	}
	for (const String &id : g.nodes) {
		const sdl3::FPoint p = pos(id);
		const bool current = id == game->CurrentDimension() || (game->CurrentDimension().IsEmpty() && id == game->Content().DefaultDimension());
		painter.FillCircle(p, 7.f, current ? ui::Color{1.f, 0.85f, 0.3f, 1.f} : ui::Color{0.75f, 0.78f, 0.85f, 1.f});
		painter.Text(id, font, ui::Color{0.9f, 0.92f, 0.95f, 1.f}, p.x + 9.f, p.y - 7.f);
	}
	painter.FillCircle(centre, 5.f, ui::Color{0.95f, 0.4f, 0.4f, 1.f});
	painter.Text(String("partout"), font, ui::Color{0.7f, 0.72f, 0.78f, 1.f}, centre.x + 7.f, centre.y - 7.f);
	painter.Text(String("rouge : mort · bleu : ascenseur · violet : portail · jaune : ici"), font, ui::Color{0.7f, 0.72f, 0.78f, 1.f},
				 rect.x + 6.f, rect.y + rect.h - 18.f);
}

// ── Actions ──────────────────────────────────────────────────────────────────

CommandResult WorldgenEditor::Open(WorldgenKind kind, const String &id) {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	Option<WorldgenDocument> doc = WorldgenDocument::FromContent(game->Content(), kind, id);
	if (doc.IsNone())
		return CommandResult::Error(String::Format("%s inconnu ou sans source : %s", WorldgenKindId(kind), id.CStr()));
	m_doc = std::move(doc);
	Changed();
	m_validateDelay = 0.f;
	if (onDocumentOpened)
		onDocumentOpened();
	Visit("world.open", FeatureStatus::Ok, id);
	return CommandResult::Ok(String::Format("ouvert : %s %s", WorldgenKindId(kind), id.CStr()));
}

CommandResult WorldgenEditor::New(WorldgenKind kind, const String &id, const String &mod) {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	String baseDir;
	for (const voxel::ModInfo &m : game->Mods().mods)
		if (m.id == mod)
			baseDir = m.path;
	if (baseDir.IsEmpty())
		return CommandResult::Error(String("mod inconnu : ") + mod);
	m_doc = Some(WorldgenDocument::New(kind, id, mod, baseDir, baseDir + String("/world/studio.json")));
	Changed();
	m_validateDelay = 0.f;
	if (onDocumentOpened)
		onDocumentOpened();
	Visit("world.new", FeatureStatus::Ok, m_doc.Value().Id());
	return CommandResult::Ok(String("nouveau : ") + m_doc.Value().Id() + String(" (world/studio.json)"));
}

CommandResult WorldgenEditor::SetField(const String &path, const String &value) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune définition ouverte"));
	m_doc.Value().SetField(path.CStr(), ParseValue(value));
	Changed();
	Visit("world.field", FeatureStatus::Ok, path);
	return CommandResult::Ok(path + String(" = ") + (value.Trim().IsEmpty() ? String("(retiré)") : value.Trim()));
}

CommandResult WorldgenEditor::Apply() {
	Session *session = m_shell.Game();
	if (!session || !session->Ready() || !m_doc.IsSome())
		return CommandResult::Error(String("aucune définition ouverte"));
	Revalidate();
	for (const WorldgenDiagnostic &d : m_diagnostics)
		if (d.severity == WorldgenDiagnostic::Severity::Error)
			return CommandResult::Error(String("refusé : ") + d.message);
	Result<Session::ContentReload, String> r = session->ReloadDefinition(m_doc.Value().Json(), m_doc.Value().Context());
	if (r.IsError()) {
		Visit("world.hot_reload", FeatureStatus::Error, r.Error());
		return CommandResult::Error(r.Error());
	}
	RebuildCatalog();
	Visit("world.hot_reload", FeatureStatus::Warning, String("chunks existants non régénérés"));
	// Le monde est déjà généré : rien ne change sous les pieds du joueur.
	return CommandResult::Warning(String::Format("%s rechargé (%.0f ms) — les chunks déjà générés NE changent PAS ; pour voir le résultat : "
												 "« Aperçu » ou « Régénérer dans une dimension temporaire »",
												 m_doc.Value().Id().CStr(), r.Value().milliseconds));
}

CommandResult WorldgenEditor::Save() {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune définition ouverte"));
	if (Result<bool, String> r = m_doc.Value().Save(); r.IsError())
		return CommandResult::Error(r.Error());
	Changed();
	Visit("world.save", FeatureStatus::Ok, m_doc.Value().SourceFile());
	return CommandResult::Ok(String("enregistré : ") + m_doc.Value().SourceFile());
}

CommandResult WorldgenEditor::Undo() {
	if (!m_doc.IsSome() || !m_doc.Value().Undo())
		return CommandResult::Warning(String("rien à annuler"));
	Changed();
	return CommandResult::Ok(String("annulé"));
}

CommandResult WorldgenEditor::Redo() {
	if (!m_doc.IsSome() || !m_doc.Value().Redo())
		return CommandResult::Warning(String("rien à rétablir"));
	Changed();
	return CommandResult::Ok(String("rétabli"));
}

CommandResult WorldgenEditor::Preview(int side, Option<int> cutY) {
	Session *session = m_shell.Game();
	voxel::Game *game = Game();
	if (!session || !game || !m_doc.IsSome())
		return CommandResult::Error(String("aucune définition ouverte"));
	Result<voxel::ContentRegistry, String> scratch = m_doc.Value().Scratch(game->Content());
	if (scratch.IsError())
		return CommandResult::Error(scratch.Error());
	const String dimension = m_doc.Value().PreviewDimension(scratch.Value());
	BlockPos centre{0, 0, 0};
	if (auto t = game->Registry().GetComponent<voxel::Transform>(session->Player()); t.IsSome())
		centre = BlockPos::FromWorld(t.Unwrap()->position);
	Result<WorldgenPreview, String> built = BuildPreview(*game, scratch.Value(), dimension, centre, side, cutY);
	if (built.IsError())
		return CommandResult::Error(built.Error());
	m_preview = std::make_unique<WorldgenPreview>(std::move(built).Unwrap());
	// Maillage avec l'atlas de la partie, dans la scène du panneau.
	while (!m_previewRoot.Children().empty())
		(void)m_previewRoot.RemoveChild(m_previewRoot.Children().front().get());
	m_previewTriangles = 0;
	for (WorldRenderer::MeshPart &part : session->Renderer().ChunkMeshes(*m_preview->world, *m_preview->content, m_preview->chunks)) {
		m_previewTriangles += part.mesh->IndexCount() / 3;
		auto shape = std::make_unique<render3d::Shape>(part.mesh, part.material);
		shape->SetPosition(part.origin);
		m_previewRoot.Add(std::move(shape));
	}
	// Carte des biomes : MAP_SIZE² pixels de MAP_SCALE blocs autour du centre.
	m_map.width = m_map.height = MAP_SIZE;
	m_map.rgba.assign(size_t(MAP_SIZE * MAP_SIZE * 4), 255);
	m_legend.clear();
	for (int py = 0; py < MAP_SIZE; ++py)
		for (int px = 0; px < MAP_SIZE; ++px) {
			const voxel::BiomeDef *b = m_preview->generator->BiomeAt(centre.x + (px - MAP_SIZE / 2) * MAP_SCALE, centre.z + (py - MAP_SIZE / 2) * MAP_SCALE);
			const auto color = BiomeColor(b);
			uint8_t *dst = &m_map.rgba[size_t((py * MAP_SIZE + px) * 4)];
			dst[0] = color[0], dst[1] = color[1], dst[2] = color[2];
			const String name = b ? b->id : String("(aucun)");
			if (std::none_of(m_legend.begin(), m_legend.end(), [&](const auto &e) { return e.first == name; }))
				m_legend.push_back({name, color});
		}
	++m_mapVersion;
	m_legendList.Clear();
	for (const auto &[name, color] : m_legend)
		m_legendList.Append(ui::Row().Gap(6.f).Children({
			ui::ColorSwatch(ui::Color{float(color[0]) / 255.f, float(color[1]) / 255.f, float(color[2]) / 255.f, 1.f}).Width(14.f).Height(14.f),
			ui::Label(name),
		}));
	const String text = String::Format("aperçu de %s : %zu chunks, %zu triangles, y %d..%d%s, %.0f ms (graine %u)", dimension.CStr(),
									   m_preview->chunks.size(), m_previewTriangles, m_preview->minY, m_preview->maxY,
									   m_preview->cutY != std::numeric_limits<int>::max() ? String::Format(", coupé à %d", m_preview->cutY).CStr() : "",
									   m_preview->milliseconds, game->DimensionSeed(dimension));
	m_status.SetText(text);
	Visit("world.preview", FeatureStatus::Ok, text);
	return CommandResult::Ok(text);
}

CommandResult WorldgenEditor::RegenerateTemporary() {
	Session *session = m_shell.Game();
	voxel::Game *game = Game();
	if (!session || !game || !m_doc.IsSome())
		return CommandResult::Error(String("aucune définition ouverte"));
	Result<voxel::ContentRegistry, String> scratch = m_doc.Value().Scratch(game->Content());
	if (scratch.IsError())
		return CommandResult::Error(scratch.Error());
	const String dimension = m_doc.Value().PreviewDimension(scratch.Value());
	const voxel::DimensionDef *source = scratch.Value().FindDimension(dimension);
	if (!source || !source->source)
		return CommandResult::Error(String("le monde ordinaire n'a pas de définition à copier : ouvrez une dimension de mod"));
	return CommandResult::Confirm(
		String::Format("Envoyer le joueur dans une copie temporaire de %s, générée avec ce document ? La dimension courante sera déchargée "
					   "(enregistrée).",
					   dimension.CStr()),
		[this, dimension, mod = source->mod, copySource = source->source->Clone()]() mutable {
			Session *s = m_shell.Game();
			voxel::Game *g = Game();
			if (!s || !g || !m_doc.IsSome())
				return CommandResult::Error(String("partie fermée"));
			// 1. Le document lui-même, rechargé (sauf si c'est la dimension, copiée telle quelle).
			if (m_doc.Value().Kind() != WorldgenKind::Dimension)
				if (auto r = s->ReloadDefinition(m_doc.Value().Json(), m_doc.Value().Context()); r.IsError())
					return CommandResult::Error(r.Error());
			// 2. La copie : « <id>_studio », même graine (seedFrom).
			NodePtr copy = m_doc.Value().Kind() == WorldgenKind::Dimension ? m_doc.Value().Json()->Clone() : copySource;
			const String bare = dimension.Substring(dimension.Find(":") + 1);
			const String tempId = mod + String(":") + bare + String("_studio");
			copy->Set("id", json::MakeStr(bare + String("_studio")));
			copy->Set("seedFrom", json::MakeStr(dimension));
			copy->Remove("enterOnDeath"); // une copie de travail ne capture pas les morts
			voxel::ContentRegistry::LoadContext ctx;
			ctx.mod = mod;
			if (auto r = s->ReloadDefinition(copy, ctx); r.IsError())
				return CommandResult::Error(r.Error());
			// 3. Le joueur y va.
			if (!m_inTemporary)
				m_returnDimension = g->CurrentDimension();
			if (Result<bool, String> r = g->TravelTo(s->Player(), tempId); r.IsError())
				return CommandResult::Error(r.Error());
			m_inTemporary = true;
			Visit("world.regenerate_temporary", FeatureStatus::Ok, tempId);
			return CommandResult::Ok(String("dans ") + tempId + String(" (« Revenir » pour retourner)"));
		});
}

CommandResult WorldgenEditor::ReturnFromTemporary() {
	Session *session = m_shell.Game();
	voxel::Game *game = Game();
	if (!session || !game)
		return CommandResult::Error(String("aucune partie en cours"));
	if (!m_inTemporary)
		return CommandResult::Warning(String("pas dans une dimension temporaire"));
	const String back = m_returnDimension.IsEmpty() ? game->Content().DefaultDimension() : m_returnDimension;
	if (Result<bool, String> r = game->TravelTo(session->Player(), back); r.IsError())
		return CommandResult::Error(r.Error());
	m_inTemporary = false;
	Visit("world.return", FeatureStatus::Ok, back);
	return CommandResult::Ok(String("retour dans ") + back);
}

// ── Console ──────────────────────────────────────────────────────────────────

void WorldgenEditor::RegisterCommands(ConsoleModel &console) {
	auto need = [](const std::vector<String> &args, size_t n, const char *usage) -> Option<CommandResult> {
		if (args.size() < n)
			return Some(CommandResult::Error(String("usage : ") + String(usage)));
		return NONE;
	};
	console.Register("studio.world.open", "studio.world.open biome|generator|dimension <id>", [this, need](const auto &a) {
		if (auto e = need(a, 3, "studio.world.open biome|generator|dimension <id>"); e.IsSome())
			return e.Value();
		const Option<WorldgenKind> kind = KindOf(a[1]);
		return kind.IsSome() ? Open(kind.Value(), a[2]) : CommandResult::Error(String("biome, generator ou dimension"));
	});
	console.Register("studio.world.new", "studio.world.new biome|generator|dimension <id> [mod]", [this, need](const auto &a) {
		if (auto e = need(a, 3, "studio.world.new biome|generator|dimension <id> [mod]"); e.IsSome())
			return e.Value();
		const Option<WorldgenKind> kind = KindOf(a[1]);
		return kind.IsSome() ? New(kind.Value(), a[2], a.size() > 3 ? a[3] : String("core")) : CommandResult::Error(String("biome, generator ou dimension"));
	});
	console.Register("studio.world.set", "studio.world.set <champ> <json|mot> — vide : retiré", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.world.set <champ> <valeur>"); e.IsSome())
			return e.Value();
		String value;
		for (size_t i = 2; i < a.size(); ++i)
			value += (i > 2 ? String(" ") : String()) + a[i];
		return SetField(a[1], value);
	});
	console.Register("studio.world.apply", "studio.world.apply — rechargement (chunks existants inchangés)", [this](const auto &) { return Apply(); });
	console.Register("studio.world.save", "studio.world.save", [this](const auto &) { return Save(); });
	console.Register("studio.world.undo", "studio.world.undo", [this](const auto &) { return Undo(); });
	console.Register("studio.world.redo", "studio.world.redo", [this](const auto &) { return Redo(); });
	console.Register("studio.world.preview", "studio.world.preview [chunks] [coupe y] — aperçu autour du joueur", [this](const auto &a) {
		const Option<double> side = a.size() > 1 ? Number(a[1]) : Some(4.0);
		const Option<double> cut = a.size() > 2 ? Number(a[2]) : Option<double>(NONE);
		return Preview(side.IsSome() ? int(side.Value()) : 4, cut.IsSome() ? Some(int(cut.Value())) : Option<int>(NONE));
	});
	console.Register("studio.world.regenerate", "studio.world.regenerate — dans une dimension temporaire (confirmation)",
					 [this](const auto &) { return RegenerateTemporary(); });
	console.Register("studio.world.return", "studio.world.return — quitte la dimension temporaire", [this](const auto &) { return ReturnFromTemporary(); });
	console.Register("studio.world.check", "studio.world.check — diagnostics", [this](const auto &) {
		if (!m_doc.IsSome())
			return CommandResult::Error(String("aucune définition ouverte"));
		Revalidate();
		if (m_diagnostics.empty())
			return CommandResult::Ok(String("aucun problème"));
		String text = String::Format("%zu diagnostic(s)", m_diagnostics.size());
		for (const WorldgenDiagnostic &d : m_diagnostics)
			text += String("\n  ") + d.message;
		return CommandResult::Warning(text);
	});
	console.Register("studio.world.graph", "studio.world.graph — chaîne des dimensions", [this](const auto &) {
		voxel::Game *game = Game();
		if (!game)
			return CommandResult::Error(String("aucune partie en cours"));
		const DimensionGraph g = BuildDimensionGraph(game->Content());
		String text = String::Format("%zu dimension(s), %zu lien(s)", g.nodes.size(), g.links.size());
		for (const DimensionLink &l : g.links)
			text += String::Format("\n  %s → %s (%s)", l.from.CStr(), l.to.CStr(), l.label.CStr());
		Visit("world.graph", FeatureStatus::Ok, String::Format("%zu liens", g.links.size()));
		return CommandResult::Ok(text);
	});
}

} // namespace voxelia::studio
