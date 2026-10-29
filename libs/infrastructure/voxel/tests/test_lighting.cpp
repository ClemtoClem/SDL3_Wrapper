// Tests — éclairage (voxel/lighting.hpp) : sources JSON, cumul, couleurs,
// occlusion par la géométrie, obscurité, mises à jour (extinction, murs,
// chunks), projecteurs, compatibilité et stabilité numérique. Les valeurs
// sont lues dans les éclairements calculés (et la lumière des sommets du
// mailleur), jamais dans une couleur affichée.
#include "test.hpp"

#include <chrono>
#include <cmath>

#include <voxel/json.hpp>
#include <voxel/lighting.hpp>
#include <voxel/meshing.hpp>

using namespace voxel;
using lighting::Rgb;

namespace {

void Load(ContentRegistry &content, const char *text) {
	auto parsed = json::Parse(String(text));
	if (parsed.IsError() || content.Load(parsed.Unwrap(), {"t", "", "test"}).IsError())
		std::printf("définition refusée : %s\n", text);
}

float Sum(const Rgb &c) { return c[0] + c[1] + c[2]; }
bool Finite(const Rgb &c) { return std::isfinite(c[0]) && std::isfinite(c[1]) && std::isfinite(c[2]); }

const math::FVector3 UP{0.f, 1.f, 0.f};

/// Contenu de test : pierre opaque, verre bleuté, lampes de toutes sortes.
struct Lab {
	ContentRegistry content;
	std::unique_ptr<World> world;
	Lab() {
		Load(content, R"({"type": "texture", "id": "grey", "procedural": {"color": [128, 128, 128]}})");
		Load(content, R"({"id": "stone", "graphics": {"texture": "grey"}})");
		Load(content, R"({"id": "glass", "graphics": {"texture": "grey", "material": "TRANSLUCENT", "tint": [80, 120, 255, 255]}})");
		Load(content, R"({"id": "door", "type": "nodebox", "nodebox": [[-0.5, -0.5, -0.5, 0.5, 0.5, -0.3125]], "graphics": {"texture": "grey"}})");
		Load(content, R"({"id": "white_lamp", "graphics": {"texture": "grey"},
			"light": {"type": "point", "color": [255, 255, 255], "intensity": 10, "range": 16}})");
		Load(content, R"({"id": "red_lamp", "graphics": {"texture": "grey"},
			"light": {"color": [255, 0, 0], "intensity": 10, "range": 16}})");
		Load(content, R"({"id": "blue_lamp", "graphics": {"texture": "grey"},
			"light": {"color": "#0000ff", "intensity": 4, "range": 16}})");
		Load(content, R"({"id": "spot", "graphics": {"texture": "grey"},
			"light": {"type": "spot", "color": [255, 255, 255], "intensity": 20, "range": 16, "direction": [1, 0, 0], "innerAngle": 15, "outerAngle": 25}})");
		Load(content, R"({"id": "beam", "graphics": {"texture": "grey"},
			"light": {"type": "directional", "intensity": 2, "range": 12, "direction": [0, 0, 1], "radius": 0.8}})");
		Load(content, R"({"id": "legacy_lamp", "graphics": {"texture": "grey", "light": 14}})");
		Load(content, R"({"id": "switch_lamp", "graphics": {"texture": "grey"}, "defaultState": "on",
			"light": {"color": [255, 240, 200], "intensity": 8, "range": 12},
			"states": {"on": {}, "half": {"light": {"intensity": 3}}, "off": {"light": {"enabled": false}}}})");
		world = std::make_unique<World>(content);
		for (int y = -1; y <= 1; ++y)
			for (int z = -2; z <= 2; ++z)
				for (int x = -2; x <= 2; ++x)
					(void)world->EnsureChunk({x, y, z});
	}
	void Put(const BlockPos &p, const char *id, uint8_t param = 0) {
		const String qualified = ContentRegistry::Qualify(String(id), String("t"));
		REQUIRE(content.FindBlock(qualified) != nullptr);
		(void)world->SetBlock(p, qualified, param, ChangeCause::SCRIPT);
	}
	void Fill(const BlockPos &a, const BlockPos &b, const char *id) {
		for (int y = a.y; y <= b.y; ++y)
			for (int z = a.z; z <= b.z; ++z)
				for (int x = a.x; x <= b.x; ++x)
					Put({x, y, z}, id);
	}
	/// Pièce creuse : murs, sol, plafond de pierre ; intérieur [a+1, b-1].
	void Room(const BlockPos &a, const BlockPos &b) {
		Fill(a, b, "stone");
		Fill(a + BlockPos{1, 1, 1}, b - BlockPos{1, 1, 1}, "core:air");
	}
	/// Éclairement direct d'un point du sol (face du dessus du bloc en y - 1).
	[[nodiscard]] Rgb Floor(float x, float y, float z) const {
		std::vector<lighting::Source> sources;
		const math::FVector3 p{x, y + 0.04f, z};
		lighting::GatherSources(*world, p, p, sources);
		return lighting::DirectIrradiance(*world, p, UP, sources);
	}
};

/// Lumière (rgb, a) des sommets d'un chunk dont la position (monde) est dans [lo, hi].
std::vector<std::array<float, 4>> VertexLight(const Lab &lab, const ChunkPos &cp, const math::FVector3 &lo, const math::FVector3 &hi) {
	TextureAtlas atlas;
	AssetManager assets;
	assets.Declare(lab.content);
	atlas.Build(assets, lab.content, 16);
	ChunkMesher mesher(lab.content, atlas, &assets);
	std::vector<std::array<float, 4>> out;
	const ChunkMesh mesh = mesher.Build(*lab.world, cp);
	const math::FVector3 o = cp.Origin().ToVector();
	for (const MeshBuffer *b : {&mesh.opaque, &mesh.translucent})
		for (const MeshVertex &v : b->vertices) {
			const math::FVector3 p = o + v.position;
			if (Box{lo, hi}.Contains(p))
				out.push_back(v.light);
		}
	return out;
}

} // namespace

// A — pièce close, aucune source, aucun ciel : tout est noir (y compris
// après qu'une lampe y a brillé puis disparu : rien de périmé).
TEST(Lighting_A_ClosedRoomWithoutSourcesIsDark) {
	Lab lab;
	lab.Room({0, 0, 0}, {10, 6, 10});
	lab.Put({5, 3, 5}, "white_lamp");
	CHECK(Sum(lab.Floor(5.5f, 1.f, 7.5f)) > 0.f);
	lab.Put({5, 3, 5}, "core:air");
	CHECK_EQ(Sum(lab.Floor(5.5f, 1.f, 7.5f)), 0.f);
	const auto inside = VertexLight(lab, {0, 0, 0}, {1.f, 1.f, 1.f}, {10.f, 5.f, 10.f});
	REQUIRE(!inside.empty());
	for (const auto &l : inside) {
		CHECK_EQ(l[0] + l[1] + l[2], 0.f); // aucune lumière directe
		CHECK_EQ(l[3], 0.f);              // aucun ciel visible
	}
}

// B — une lampe : la lumière décroît avec la distance (inverse du carré).
TEST(Lighting_B_SingleSourceFallsOffWithDistance) {
	Lab lab;
	lab.Room({-12, -1, -12}, {12, 8, 12});
	lab.Put({0, 4, 0}, "white_lamp");
	// Sol en y = 0 : la lampe (centre y = 4,5) est à 4,5 au-dessus.
	float previous = 1e9f;
	for (int d = 0; d <= 8; d += 2) {
		const float e = Sum(lab.Floor(0.5f + float(d), 0.f, 0.5f));
		CHECK(e > 0.f);
		CHECK(e < previous);
		previous = e;
	}
	// Juste sous la lampe : I × cos / d² (d = 4,46 jusqu'au point mesuré).
	const float d = 4.5f - 0.04f;
	CHECK_NEAR(lab.Floor(0.5f, 0.f, 0.5f)[0], 10.f / (d * d) * std::pow(1.f - std::pow(d / 16.f, 4.f), 2.f), 0.01f);
	// Au-delà de la portée : rien, et sans marche brusque juste avant.
	CHECK_EQ(Sum(lab.Floor(16.5f, 0.f, 16.5f)), 0.f);
}

// C — cumul : deux sources identiques donnent deux fois plus de lumière.
TEST(Lighting_C_SourcesAccumulate) {
	Lab lab;
	lab.Room({-12, -1, -12}, {12, 8, 12});
	lab.Put({0, 4, -2}, "white_lamp");
	const Rgb one = lab.Floor(0.5f, 0.f, 0.5f);
	lab.Put({0, 4, 2}, "white_lamp"); // centres en z = −1,5 et 2,5 : symétriques autour de z = 0,5
	const Rgb two = lab.Floor(0.5f, 0.f, 0.5f);
	CHECK(two[0] > one[0]);
	CHECK_NEAR(two[0], 2.f * one[0], 1e-4f);
	// Même source comptée deux fois (superposées) : exactement le double.
	std::vector<lighting::Source> sources;
	const math::FVector3 p{0.5f, 0.04f, 0.5f};
	lighting::GatherSources(*lab.world, p, p, sources);
	REQUIRE_EQ(sources.size(), size_t(2));
	sources.push_back(sources[0]);
	sources.push_back(sources[1]);
	CHECK_NEAR(Sum(lighting::DirectIrradiance(*lab.world, p, UP, sources)), 2.f * Sum(two), 1e-4f);
}

// D — couleurs : rouge + bleu se mélangent (somme par canal), quelles que
// soient leurs intensités ; aucune n'écrase l'autre.
TEST(Lighting_D_ColoredSourcesMixPerChannel) {
	Lab lab;
	lab.Room({-12, -1, -12}, {12, 8, 12});
	lab.Put({-2, 4, 0}, "red_lamp");
	const Rgb red = lab.Floor(0.5f, 0.f, 0.5f);
	lab.Put({-2, 4, 0}, "core:air");
	lab.Put({3, 4, 0}, "blue_lamp"); // plus faible (4 contre 10)
	const Rgb blue = lab.Floor(0.5f, 0.f, 0.5f);
	lab.Put({-2, 4, 0}, "red_lamp");
	const Rgb both = lab.Floor(0.5f, 0.f, 0.5f);
	CHECK(red[0] > 0.f && red[1] == 0.f && red[2] == 0.f);
	CHECK(blue[2] > 0.f && blue[0] == 0.f);
	for (int k = 0; k < 3; ++k)
		CHECK_NEAR(both[size_t(k)], red[size_t(k)] + blue[size_t(k)], 1e-5f);
	CHECK(both[0] > 0.f && both[2] > 0.f); // magenta, pas « la plus forte gagne »
}

// E — occlusion : un mur opaque annule la contribution directe ; une autre
// source, du bon côté, éclaire toujours la cible.
TEST(Lighting_E_OpaqueWallBlocksDirectLight) {
	Lab lab;
	lab.Room({-12, -1, -12}, {12, 8, 12});
	lab.Put({-4, 2, 0}, "white_lamp");
	CHECK(Sum(lab.Floor(2.5f, 0.f, 0.5f)) > 0.f);
	lab.Fill({0, 0, -11}, {0, 7, 11}, "stone"); // mur plein entre les deux
	CHECK_EQ(Sum(lab.Floor(2.5f, 0.f, 0.5f)), 0.f);
	lab.Put({5, 2, 0}, "white_lamp");
	const Rgb behind = lab.Floor(2.5f, 0.f, 0.5f);
	lab.Put({-4, 2, 0}, "core:air");
	CHECK(Sum(behind) > 0.f);
	CHECK_NEAR(Sum(lab.Floor(2.5f, 0.f, 0.5f)), Sum(behind), 1e-6f); // la lampe masquée n'apportait rien
	// Le verre filtre (teinte bleutée) sans arrêter.
	lab.Put({5, 2, 0}, "core:air");
	lab.Put({-4, 2, 0}, "white_lamp");
	lab.Fill({0, 0, -11}, {0, 7, 11}, "glass");
	const Rgb filtered = lab.Floor(2.5f, 0.f, 0.5f);
	CHECK(filtered[2] > 0.f && filtered[0] < filtered[2]);
	// Une porte (boîte fine) n'arrête que les rayons qui la touchent : de
	// côté (panneau le long de −z), la lumière passe ; en travers, plus rien.
	lab.Fill({0, 0, -11}, {0, 7, 11}, "door");
	CHECK(Sum(lab.Floor(2.5f, 0.f, 0.5f)) > 0.f);
	for (int y = 0; y <= 7; ++y)
		for (int z = -11; z <= 11; ++z)
			lab.Put({0, y, z}, "door", 1); // panneau tourné côté +x : barre le passage
	CHECK_EQ(Sum(lab.Floor(2.5f, 0.f, 0.5f)), 0.f);
}

// F — deux pièces séparées par un mur : rien ne passe.
TEST(Lighting_F_SeparateRoomsStayDark) {
	Lab lab;
	lab.Room({0, 0, 0}, {20, 6, 10});
	lab.Fill({10, 0, 0}, {10, 6, 10}, "stone");
	lab.Put({5, 3, 5}, "white_lamp");
	CHECK(Sum(lab.Floor(8.5f, 1.f, 8.5f)) > 0.f);
	for (int x = 11; x <= 19; ++x)
		for (int z = 1; z <= 9; ++z)
			CHECK_EQ(Sum(lab.Floor(float(x) + 0.5f, 1.f, float(z) + 0.5f)), 0.f);
	const auto second = VertexLight(lab, {0, 0, 0}, {11.f, 1.f, 1.f}, {16.f, 5.f, 10.f});
	REQUIRE(!second.empty());
	for (const auto &l : second)
		CHECK_EQ(l[0] + l[1] + l[2], 0.f);
}

// G — extinction : changer d'état retire la contribution, et les chunks
// éclairés sont à refaire.
TEST(Lighting_G_SwitchingOffRemovesTheContribution) {
	Lab lab;
	lab.Room({-12, -1, -12}, {12, 8, 12});
	lab.Put({0, 4, 0}, "white_lamp");
	const Rgb other = lab.Floor(-6.5f, 0.f, 0.5f);
	lab.Put({4, 4, 0}, "switch_lamp");
	const Rgb on = lab.Floor(4.5f, 0.f, 0.5f);
	const uint64_t before = lab.world->GetChunk({0, 0, 0})->Revision();
	lab.Put({4, 4, 0}, "switch_lamp_half");
	const Rgb half = lab.Floor(4.5f, 0.f, 0.5f);
	lab.Put({4, 4, 0}, "switch_lamp_off");
	const Rgb off = lab.Floor(4.5f, 0.f, 0.5f);
	CHECK(lab.world->GetChunk({0, 0, 0})->Revision() > before);
	CHECK(Sum(on) > Sum(half) && Sum(half) > Sum(off));
	CHECK(Sum(off) > 0.f); // la lampe blanche, toujours là
	// Il ne reste que la contribution de la lampe blanche.
	lab.Put({0, 4, 0}, "core:air");
	CHECK_EQ(Sum(lab.Floor(4.5f, 0.f, 0.5f)), 0.f);
	CHECK_EQ(Sum(lab.Floor(-6.5f, 0.f, 0.5f)), 0.f);
	CHECK(Sum(other) > 0.f);
	CHECK_EQ(lab.content.FindBlock("t:switch_lamp_off")->look.lightEmission, 0);
}

// H — casser puis reconstruire un mur ouvre puis referme le passage.
TEST(Lighting_H_BreakingAndRebuildingAWall) {
	Lab lab;
	lab.Room({-12, -1, -12}, {12, 8, 12});
	lab.Fill({0, 0, -11}, {0, 7, 11}, "stone");
	lab.Put({-4, 2, 0}, "white_lamp");
	CHECK_EQ(Sum(lab.Floor(3.5f, 0.f, 0.5f)), 0.f);
	const uint64_t before = lab.world->GetChunk({0, 0, 0})->Revision();
	lab.Fill({0, 0, -1}, {0, 4, 1}, "core:air"); // une brèche
	CHECK(lab.world->GetChunk({0, 0, 0})->Revision() > before);
	CHECK(Sum(lab.Floor(3.5f, 0.f, 0.5f)) > 0.f);
	lab.Fill({0, 0, -1}, {0, 4, 1}, "stone");
	CHECK_EQ(Sum(lab.Floor(3.5f, 0.f, 0.5f)), 0.f);
}

// I — une source au bord d'un chunk éclaire le voisin sans raccord, et sa
// disparition y est répercutée.
TEST(Lighting_I_LightCrossesChunkBorders) {
	Lab lab;
	lab.Room({4, -1, 4}, {28, 8, 12});
	lab.Put({15, 3, 8}, "white_lamp"); // dernier bloc du chunk x = 0
	const auto across = VertexLight(lab, {1, 0, 0}, {16.f, 0.f, 5.f}, {20.f, 0.1f, 12.f});
	REQUIRE(!across.empty());
	float lit = 0.f;
	for (const auto &l : across)
		lit += l[0];
	CHECK(lit > 0.f);
	// De part et d'autre de la frontière : même valeur (calculée du monde, pas du chunk).
	CHECK_NEAR(lab.Floor(15.99f, 0.f, 8.5f)[0], lab.Floor(16.01f, 0.f, 8.5f)[0], 0.01f);
	const uint64_t before = lab.world->GetChunk({1, 0, 0})->Revision();
	lab.Put({15, 3, 8}, "core:air");
	CHECK(lab.world->GetChunk({1, 0, 0})->Revision() > before);
	for (const auto &l : VertexLight(lab, {1, 0, 0}, {16.f, 0.f, 5.f}, {20.f, 0.1f, 12.f}))
		CHECK_EQ(l[0] + l[1] + l[2], 0.f);
}

// J — projecteur et faisceau : la lumière suit l'orientation, s'arrête aux obstacles.
TEST(Lighting_J_SpotAndBeamFollowTheirDirection) {
	Lab lab;
	lab.Room({-14, -1, -14}, {14, 8, 14});
	lab.Put({0, 1, 0}, "spot"); // vers +x à l'orientation 0
	auto at = [&](float x, float z) {
		std::vector<lighting::Source> sources;
		const math::FVector3 p{x, 1.5f, z};
		lighting::GatherSources(*lab.world, p, p, sources);
		return Sum(lighting::DirectIrradiance(*lab.world, p, math::FVector3{0.f, 0.f, 0.f}, sources));
	};
	CHECK(at(6.5f, 0.5f) > 0.f);
	CHECK_EQ(at(-6.5f, 0.5f), 0.f);  // derrière
	CHECK_EQ(at(0.5f, 6.5f), 0.f);   // hors du cône
	lab.Put({0, 1, 0}, "spot", 1); // un quart de tour : vers +z
	CHECK(at(0.5f, 6.5f) > 0.f);
	CHECK_EQ(at(6.5f, 0.5f), 0.f);
	lab.Put({0, 1, 3}, "stone"); // obstacle dans le faisceau
	CHECK_EQ(at(0.5f, 6.5f), 0.f);
	// Faisceau parallèle : même éclairement à 3 et à 9 blocs, rien hors du rayon.
	lab.Put({0, 1, 3}, "core:air");
	lab.Put({0, 1, 0}, "core:air");
	lab.Put({-5, 1, -10}, "beam");
	const float near = at(-4.5f, -7.5f), far = at(-4.5f, -1.5f);
	CHECK(near > 0.f && far > 0.f);
	// Pas d'inverse du carré : le rapport n'est que celui de la fin de portée.
	auto window = [](float d) { return std::pow(1.f - std::pow(d / 12.f, 4.f), 2.f); };
	CHECK_NEAR(far / near, window(8.f) / window(2.f), 1e-3f);
	CHECK_EQ(at(-2.5f, -5.5f), 0.f);
}

// K — compatibilité : « graphics.light » seul, objet « light », états,
// valeurs invalides corrigées et signalées.
TEST(Lighting_K_LegacyAndExtendedDefinitions) {
	Lab lab;
	const BlockDef *legacy = lab.content.FindBlock("t:legacy_lamp");
	REQUIRE(legacy);
	CHECK(legacy->look.light.Active() && legacy->look.light.legacy);
	CHECK_EQ(legacy->look.lightEmission, 14);
	CHECK_NEAR(legacy->look.light.intensity, lighting::LegacyIntensity(14), 1e-6f);
	CHECK_NEAR(legacy->look.light.range, 15.f, 1e-6f);
	const BlockDef *red = lab.content.FindBlock("t:red_lamp");
	CHECK(!red->look.light.legacy && red->look.light.type == LightDef::Type::POINT);
	CHECK_NEAR(red->look.light.color[0], 1.f, 1e-5f);
	CHECK_EQ(red->look.light.color[1], 0.f);
	CHECK(red->look.lightEmission > 0); // aspect lumineux déduit de l'intensité
	const BlockDef *spot = lab.content.FindBlock("t:spot");
	CHECK(spot->look.light.type == LightDef::Type::SPOT);
	CHECK_NEAR(spot->look.light.outerCos, std::cos(25.f * 3.14159265f / 180.f), 1e-5f);
	// Les états portent leur propre lumière.
	CHECK_NEAR(lab.content.FindBlock("t:switch_lamp_half")->look.light.intensity, 3.f, 1e-6f);
	CHECK(!lab.content.FindBlock("t:switch_lamp_off")->look.light.Active());
	// sRGB → linéaire : un gris moyen n'est pas « à moitié » en linéaire.
	CHECK_NEAR(lighting::SrgbToLinear(128, 128, 128)[0], 0.2158f, 1e-3f);
	// Valeurs invalides : corrigées, signalées par Validate.
	Load(lab.content, R"({"id": "bad", "light": {"intensity": -3, "color": [1, 0.5, 0], "innerAngle": 60, "outerAngle": 20,
		"type": "laser", "flux": 4, "range": 500}})");
	const BlockDef *bad = lab.content.FindBlock("t:bad");
	REQUIRE(bad);
	CHECK_EQ(bad->look.light.intensity, 0.f);
	CHECK(!bad->look.light.Active());
	CHECK(bad->look.light.innerCos >= bad->look.light.outerCos);
	CHECK(bad->look.light.range <= lighting::MAX_RANGE);
	int reported = 0;
	for (const String &w : lab.content.Validate())
		reported += w.Contains("t:bad") ? 1 : 0;
	CHECK(reported >= 5); // intensité, couleur 0..1, angles, type, clé inconnue, portée
	// Optique : déduite du matériau, ou donnée.
	CHECK(lab.content.FindBlock("t:stone")->optics.Opaque());
	CHECK(!lab.content.FindBlock("t:glass")->optics.Opaque() && !lab.content.FindBlock("t:glass")->optics.Clear());
	CHECK(!lab.content.FindBlock("t:door")->optics.fullCell);
	Load(lab.content, R"({"id": "screen", "optics": {"transmittance": [1, 0, 0]}})");
	CHECK_NEAR(lab.content.FindBlock("t:screen")->optics.transmittance[0], 1.f, 1e-6f);
	// Dimension : lumière de base et exposition.
	Load(lab.content, R"({"type": "dimension", "id": "dark", "light": {"sky": false, "base": 0, "exposure": 1.4}})");
	Load(lab.content, R"({"type": "dimension", "id": "old", "light": {"sky": false, "ambient": 0.8}})");
	CHECK(lab.content.FindDimension("t:dark")->baseLight.IsSome());
	CHECK_EQ(lab.content.FindDimension("t:dark")->baseLight.Value(), 0.f);
	CHECK(lab.content.FindDimension("t:old")->baseLight.IsNone()); // historique : « ambient » en tient lieu
}

// L — stabilité : intensités extrêmes, distance nulle, sources superposées.
TEST(Lighting_L_NumericalStability) {
	Lab lab;
	Load(lab.content, R"({"id": "sun_in_a_box", "light": {"intensity": 10000, "range": 32}})");
	lab.world = std::make_unique<World>(lab.content);
	(void)lab.world->EnsureChunk({0, 0, 0});
	for (int i = 0; i < 4; ++i)
		lab.Put({8, 8, 8 + i}, "sun_in_a_box");
	std::vector<lighting::Source> sources;
	const math::FVector3 center{8.5f, 8.5f, 8.5f};
	lighting::GatherSources(*lab.world, center, center, sources);
	REQUIRE(!sources.empty());
	for (const math::FVector3 &p : {center, math::FVector3{8.5f, 9.01f, 8.5f}, math::FVector3{8.f, 8.f, 8.f}, math::FVector3{0.f, 0.f, 0.f}}) {
		const Rgb e = lighting::DirectIrradiance(*lab.world, p, UP, sources);
		const Rgb s = lighting::DirectIrradiance(*lab.world, p, math::FVector3{0.f, 0.f, 0.f}, sources);
		CHECK(Finite(e) && Finite(s));
		CHECK(e[0] >= 0.f && s[0] >= 0.f);
		CHECK(s[0] <= 4.f * 10000.f / lighting::MIN_DISTANCE_SQ + 1.f); // borné par la régularisation
	}
	// Segment de longueur nulle, direction nulle : pas de NaN.
	const Rgb t = lighting::Transmittance(*lab.world, center, center, BlockPos{99, 99, 99});
	CHECK(Finite(t));
	CHECK(std::isfinite(lighting::SkyAccess(*lab.world, center)));
}

// Coût : maillage d'un chunk de salles éclairées comme les backrooms (une
// lampe toutes les 4 cellules au plafond, portée 14). Mesuré, pas exigé :
// la durée s'affiche (build release pour un chiffre utile).
TEST(Lighting_MeshingCostIsMeasured) {
	Lab lab;
	Load(lab.content, R"({"id": "panel", "light": {"type": "spot", "intensity": 7, "range": 14, "direction": [0, -1, 0],
		"innerAngle": 50, "outerAngle": 82, "offset": [0, -0.5, 0]}})");
	lab.world = std::make_unique<World>(lab.content);
	for (int y = -1; y <= 1; ++y)
		for (int z = -2; z <= 2; ++z)
			for (int x = -2; x <= 2; ++x)
				(void)lab.world->EnsureChunk({x, y, z});
	lab.Fill({-32, -1, -32}, {47, -1, 47}, "stone");
	lab.Fill({-32, 5, -32}, {47, 5, 47}, "stone");
	for (int x = -32; x <= 47; x += 7) // murs percés de portes
		for (int z = -32; z <= 47; ++z)
			if (((z % 7) + 7) % 7 > 2)
				lab.Fill({x, 0, z}, {x, 4, z}, "stone");
	for (int x = -30; x <= 46; x += 4)
		for (int z = -30; z <= 46; z += 4)
			lab.Put({x, 5, z}, "panel");
	TextureAtlas atlas;
	AssetManager assets;
	assets.Declare(lab.content);
	atlas.Build(assets, lab.content, 16);
	ChunkMesher mesher(lab.content, atlas, &assets);
	const auto t0 = std::chrono::steady_clock::now();
	size_t vertices = 0;
	for (int i = 0; i < 4; ++i)
		vertices += mesher.Build(*lab.world, {0, 0, 0}).opaque.vertices.size();
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 4.0;
	auto timed = [&](bool block, bool sky) {
		mesher.options.blockLight = block;
		mesher.options.skyLight = sky;
		const auto t = std::chrono::steady_clock::now();
		for (int i = 0; i < 4; ++i)
			(void)mesher.Build(*lab.world, {0, 0, 0});
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count() / 4.0;
	};
	const double blockOnly = timed(true, false), skyOnly = timed(false, true);
	std::printf("\n    sources seules : %.2f ms, ciel seul : %.2f ms", blockOnly, skyOnly);
	mesher.options.blockLight = false;
	mesher.options.skyLight = false;
	const auto t1 = std::chrono::steady_clock::now();
	for (int i = 0; i < 4; ++i)
		(void)mesher.Build(*lab.world, {0, 0, 0});
	const double plain = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count() / 4.0;
	std::printf("\n    maillage éclairé : %.2f ms (%zu sommets), sans éclairage : %.2f ms\n", ms, vertices / 4, plain);
	CHECK(vertices > 0);
}
