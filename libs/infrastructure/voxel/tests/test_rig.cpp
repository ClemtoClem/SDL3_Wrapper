// Tests — modèles articulés : animations à images clés (pistes, interpolations,
// clips idle / walk / imposé), convention de rotation, rechargement d'un rig.
#include "test.hpp"

#include <cmath>
#include <filesystem>

#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxel/rig.hpp>

using namespace voxel;

namespace {

const char *RIG = R"({
	"type": "model", "id": "test_rig", "format": "rig",
	"parts": [
		{"name": "body", "pivot": [0, 12, 0], "boxes": [{"from": [-4, 12, -2], "size": [8, 12, 4], "uv": [16, 16]}]},
		{"name": "head", "parent": "body", "pivot": [0, 24, 0], "rotation": [10, 0, 0], "idle": ["y", 0, 5],
		 "boxes": [{"from": [-4, 24, -4], "size": [8, 8, 8], "uv": [0, 0]}]}
	],
	"animations": {
		"idle": {"length": 2, "loop": true, "tracks": [
			{"part": "head", "channel": "rotation", "keys": [{"t": 0, "v": [0, 0, 0]}, {"t": 1, "v": [0, 40, 0]}, {"t": 2, "v": [0, 0, 0]}]}]},
		"walk": {"length": 1, "tracks": [
			{"part": "body", "channel": "position", "keys": [{"t": 0, "v": [0, 0, 0]}, {"t": 0.5, "v": [0, 2, 0], "interp": "smooth"}]}]},
		"wave": {"length": 1, "loop": false, "tracks": [
			{"part": "head", "channel": "rotation", "keys": [{"t": 0, "v": [0, 0, 0]}, {"t": 1, "v": [0, 0, 90], "interp": "step"}]}]}
	}
})";

bool Near(const math::FVector3 &a, const math::FVector3 &b, float eps = 1e-3f) {
	return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}

RigModel Parse(const char *text) {
	Result<RigModel, String> rig = RigFromJson(json::Parse(String(text)).Value());
	if (rig.IsError())
		std::fprintf(stderr, "\n  %s", rig.Error().CStr());
	return rig.IsOk() ? rig.Unwrap() : RigModel{};
}

} // namespace

TEST(Rig_KeyframeClipsAreParsed) {
	const RigModel rig = Parse(RIG);
	REQUIRE(rig.parts.size() == size_t(2));
	REQUIRE(rig.clips.size() == size_t(3));
	const RigClip *idle = rig.FindClip("idle");
	REQUIRE(idle != nullptr);
	CHECK(std::fabs(idle->length - 2.f) < 1e-6f);
	REQUIRE(idle->tracks.size() == size_t(1));
	CHECK_EQ(idle->tracks[0].part, 1);
	CHECK(idle->tracks[0].channel == RigChannel::ROTATION);
	CHECK(rig.FindClip("walk")->tracks[0].channel == RigChannel::POSITION);
	CHECK(!rig.FindClip("wave")->loop);
	CHECK(rig.FindClip("absent") == nullptr);
	// Erreurs : partie inconnue, canal inconnu, durée nulle.
	auto bad = [](const char *animation) {
		const String text = String(R"({"parts": [{"name": "a"}], "animations": )") + String(animation) + String("}");
		return RigFromJson(json::Parse(text).Value());
	};
	CHECK(bad(R"({"x": {"tracks": [{"part": "z", "keys": [{"t": 0, "v": [0,0,0]}]}]}})").Error().Contains("« z » inconnue"));
	CHECK(bad(R"({"x": {"tracks": [{"part": "a", "channel": "scale", "keys": [{"t": 0, "v": [0,0,0]}]}]}})").Error().Contains("canal"));
	CHECK(bad(R"({"x": {"length": 0}})").Error().Contains("length"));
}

TEST(Rig_TrackInterpolations) {
	RigTrack track;
	track.keys = {RigKey{0.f, {0.f, 0.f, 0.f}, RigInterp::LINEAR, {}, {}}, RigKey{1.f, {10.f, 0.f, 0.f}, RigInterp::LINEAR, {}, {}},
				  RigKey{2.f, {20.f, 0.f, 0.f}, RigInterp::STEP, {}, {}}, RigKey{3.f, {0.f, 0.f, 0.f}, RigInterp::SMOOTH, {}, {}}};
	CHECK(Near(SampleRigTrack(track, -1.f), {0.f, 0.f, 0.f}));  // avant : première clé
	CHECK(Near(SampleRigTrack(track, 0.5f), {5.f, 0.f, 0.f}));  // linéaire
	CHECK(Near(SampleRigTrack(track, 1.5f), {10.f, 0.f, 0.f})); // palier : garde la précédente
	CHECK(Near(SampleRigTrack(track, 2.f), {20.f, 0.f, 0.f}));
	CHECK(Near(SampleRigTrack(track, 2.5f), {10.f, 0.f, 0.f})); // douce : milieu au milieu
	CHECK(SampleRigTrack(track, 2.25f).x > 15.f);              // … mais lente au départ (> linéaire 15)
	CHECK(Near(SampleRigTrack(track, 9.f), {0.f, 0.f, 0.f}));   // après : dernière clé
	RigClip loop{"l", 2.f, true, {}}, once{"o", 2.f, false, {}};
	CHECK(std::fabs(RigClipTime(loop, 5.f) - 1.f) < 1e-5f);
	CHECK(std::fabs(RigClipTime(loop, -0.5f) - 1.5f) < 1e-5f);
	CHECK(std::fabs(RigClipTime(once, 5.f) - 2.f) < 1e-5f);
}

TEST(Rig_PoseCombinesRestMotionsAndClips) {
	const RigModel rig = Parse(RIG);
	RigPoseInput input;
	input.time = 0.5f;
	// Tête : repos (10, 0, 0) + idle sinusoïdal (y) + clip idle (y : 20 à t = 0,5).
	const RigPartPose head = PoseRig(rig, 1, input);
	const float sinus = 5.f * std::sin(0.5f * 2.5f);
	CHECK(Near(head.rotation, {10.f, sinus + 20.f, 0.f}));
	// Corps : clip walk pondéré par la marche (position, courbe douce).
	input.walkAmount = 0.5f;
	input.walkPhase = 3.14159265f * 0.5f; // un quart de pas → t = 0,25 dans un clip de 1 s
	const RigPartPose body = PoseRig(rig, 0, input);
	CHECK(Near(body.offset, {0.f, 2.f * 0.5f * 0.5f, 0.f})); // smoothstep(0,5) = 0,5 ; poids 0,5
	// Clip imposé (éditeur) : seul lui, plus le repos.
	input.clip = rig.FindClip("wave");
	input.clipTime = 0.99f;
	CHECK(Near(PoseRig(rig, 1, input).rotation, {10.f, 0.f, 0.f}));
	input.clipTime = 1.f;
	CHECK(Near(PoseRig(rig, 1, input).rotation, {10.f, 0.f, 90.f}));
	CHECK(Near(PoseRig(rig, 7, input).rotation, {0.f, 0.f, 0.f})); // partie hors bornes
}

TEST(Rig_RotationConventionIsXThenYThenZ) {
	// 90° autour de x puis 90° autour de y : +y → +z → +x.
	const math::FQuaternion q = RigRotation({90.f, 90.f, 0.f});
	const math::FVector3 v = q.Rotate({0.f, 1.f, 0.f});
	CHECK(Near(v, {1.f, 0.f, 0.f}, 1e-4f));
	CHECK(Near(RigRotation({0.f, 0.f, 0.f}).Rotate({1.f, 2.f, 3.f}), {1.f, 2.f, 3.f}, 1e-5f));
}

TEST(Rig_ReloadedDefinitionIsReread) {
	const char *saves = "build/tests/tmp/voxel/rig_reload";
	std::filesystem::remove_all(saves);
	Game game;
	GameOptions options;
	options.modDirectories = {};
	options.savesDirectory = String(saves);
	(void)game.LoadMods(options);
	ContentRegistry::LoadContext ctx;
	ctx.mod = "test";
	REQUIRE(game.Content().Load(json::Parse(String(RIG)).Value(), ctx).IsOk());
	std::shared_ptr<const RigModel> before = game.Assets().Rig("test:test_rig");
	REQUIRE(before != nullptr);
	CHECK_EQ(before->clips.size(), size_t(3));
	// Une animation de moins : rechargée, le cache est relu.
	NodePtr def = json::Parse(String(RIG)).Value();
	json::At(def, "animations")->Remove("wave");
	REQUIRE(game.ReloadDefinition(def, ctx).IsOk());
	std::shared_ptr<const RigModel> after = game.Assets().Rig("test:test_rig");
	REQUIRE(after != nullptr);
	CHECK(after != before);
	CHECK_EQ(after->clips.size(), size_t(2));
	CHECK_EQ(before->clips.size(), size_t(3)); // l'ancien reste valide pour qui le tient
	CHECK(game.Content().FindModel("test:test_rig")->source != nullptr);
}

TEST(Rig_BoxUvMatchesTheBuiltMesh) {
	// Le dépliage annoncé (éditeur) est celui que le maillage utilise : chaque
	// face du maillage couvre exactement le rectangle de RigBoxUv.
	for (bool mirror : {false, true}) {
		RigModel rig;
		rig.textureWidth = 64;
		rig.textureHeight = 32;
		RigPart part;
		RigBox box;
		box.from = {0.f, 0.f, 0.f};
		box.size = {8.f, 12.f, 4.f};
		box.u = 16;
		box.v = 16;
		box.mirror = mirror;
		part.boxes.push_back(box);
		rig.parts.push_back(part);
		const ModelMesh mesh = BuildRigPart(rig, rig.parts[0], 64, 32);
		REQUIRE(mesh.uvs.size() == size_t(24));
		const std::array<RigUvFace, 6> faces = RigBoxUv(box);
		// Ordre des faces du maillage : face, droite, gauche, dos, dessus, dessous.
		for (size_t f = 0; f < 6; ++f) {
			float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
			for (size_t k = 0; k < 4; ++k) {
				const math::FVector2 uv = mesh.uvs[f * 4 + k];
				u0 = std::min(u0, uv.x * 64.f);
				u1 = std::max(u1, uv.x * 64.f);
				v0 = std::min(v0, uv.y * 32.f);
				v1 = std::max(v1, uv.y * 32.f);
			}
			CHECK(std::fabs(u0 - faces[f].u) < 1e-3f);
			CHECK(std::fabs(v0 - faces[f].v) < 1e-3f);
			CHECK(std::fabs((u1 - u0) - faces[f].width) < 1e-3f);
			CHECK(std::fabs((v1 - v0) - faces[f].height) < 1e-3f);
		}
	}
}

TEST(Rig_BezierHandles) {
	RigTrack track;
	RigKey a{0.f, {0.f, 0.f, 0.f}, RigInterp::LINEAR, {}, {}}, b{1.f, {10.f, 0.f, 0.f}, RigInterp::BEZIER, {}, {}};
	track.keys = {a, b};
	// Poignées automatiques : identiques à « smooth » aux extrémités et au milieu.
	CHECK(std::fabs(SampleRigTrack(track, 0.5f).x - 5.f) < 1e-3f);
	CHECK(SampleRigTrack(track, 0.1f).x < 1.f); // départ lent
	// Poignée de sortie qui monte haut : la courbe DÉPASSE la clé d'arrivée.
	track.keys[0].out = RigHandle{0.33f, {30.f, 0.f, 0.f}};
	track.keys[1].in = RigHandle{-0.33f, {0.f, 0.f, 0.f}};
	float peak = 0.f;
	for (int i = 0; i <= 100; ++i)
		peak = std::max(peak, SampleRigTrack(track, float(i) / 100.f).x);
	CHECK(peak > 10.5f);
	CHECK(std::fabs(SampleRigTrack(track, 0.f).x) < 1e-4f);
	CHECK(std::fabs(SampleRigTrack(track, 1.f).x - 10.f) < 1e-3f);
	// Poignée de temps hors du segment : bornée (la courbe reste une fonction du temps).
	track.keys[0].out = RigHandle{5.f, {0.f, 0.f, 0.f}};
	const RigBezier bz = RigSegmentBezier(track.keys[0], track.keys[1]);
	CHECK(bz.t[1] <= 1.f + 1e-6f);
	float last = -1.f;
	bool monotonic = true;
	for (int i = 0; i <= 50; ++i) {
		const float v = SampleRigTrack(track, float(i) / 50.f).x;
		monotonic = monotonic && v >= last - 1e-3f;
		last = v;
	}
	CHECK(monotonic);
	// Lu depuis le JSON.
	const RigModel rig = Parse(R"({"parts": [{"name": "p"}], "animations": {"a": {"length": 1, "tracks": [
		{"part": "p", "keys": [{"t": 0, "v": [0,0,0], "out": {"t": 0.2, "v": [0, 9, 0]}},
		                      {"t": 1, "v": [0,0,0], "interp": "bezier", "in": {"t": -0.2, "v": [0, 9, 0]}}]}]}}})");
	REQUIRE(rig.clips.size() == size_t(1));
	const RigKey &k1 = rig.clips[0].tracks[0].keys[1];
	CHECK(k1.interp == RigInterp::BEZIER);
	REQUIRE(k1.in.has_value());
	CHECK(std::fabs(k1.in->value.y - 9.f) < 1e-6f);
	CHECK(SampleRigTrack(rig.clips[0].tracks[0], 0.5f).y > 5.f); // bosse au milieu
}

TEST(Rig_LayersBlendClips) {
	const char *text = R"({"parts": [{"name": "a", "rotation": [0, 10, 0]}, {"name": "b"}],
		"animations": {
			"turn": {"length": 1, "tracks": [{"part": "a", "keys": [{"t": 0, "v": [0, 40, 0]}]}]},
			"nod":  {"length": 1, "tracks": [{"part": "a", "keys": [{"t": 0, "v": [20, 0, 0]}]},
			                                 {"part": "b", "channel": "position", "keys": [{"t": 0, "v": [0, 4, 0]}]}]}},
		"layers": [{"clip": "turn", "weight": 0.5, "mode": "mix"}, {"clip": "nod", "weight": 0.5, "mode": "add"}]})";
	const RigModel rig = Parse(text);
	REQUIRE(rig.layers.size() == size_t(2));
	RigPoseInput input;
	// a : repos (0,10,0) ; mix 0,5 vers repos + (0,40,0) = (0,50,0) → (0,30,0) ; add 0,5 × (20,0,0).
	CHECK(Near(PoseRig(rig, 0, input).rotation, {10.f, 30.f, 0.f}));
	// b : position ajoutée, rotation non animée par les clips : intacte.
	CHECK(Near(PoseRig(rig, 1, input).offset, {0.f, 2.f, 0.f}));
	CHECK(Near(PoseRig(rig, 1, input).rotation, {0.f, 0.f, 0.f}));
	// Couches imposées par l'éditeur ; un clip imposé les ignore.
	std::vector<RigLayer> only{RigLayer{String("turn"), 1.f, 1.f, RigBlend::MIX}};
	input.layers = &only;
	CHECK(Near(PoseRig(rig, 0, input).rotation, {0.f, 50.f, 0.f}));
	input.clip = rig.FindClip("nod");
	CHECK(Near(PoseRig(rig, 0, input).rotation, {20.f, 10.f, 0.f}));
	// Erreurs.
	auto bad = [](const char *layers) {
		const String t = String(R"({"parts": [{"name": "a"}], "animations": {"x": {"length": 1}}, "layers": )") + String(layers) + String("}");
		return RigFromJson(json::Parse(t).Value());
	};
	CHECK(bad(R"([{"clip": "absent"}])").Error().Contains("inconnue"));
	CHECK(bad(R"([{"clip": "x", "mode": "multiply"}])").Error().Contains("mode"));
	CHECK(bad(R"([{"clip": "x", "mode": "add"}])").IsOk());
}

// ── glTF : modèles et parties maillées ───────────────────────────────────────

#include <dataio/documents/gltf_writer.hpp>

namespace {

/// Petite scène : un triangle « base » à la racine, un « bras » décalé de
/// (1, 0, 0) sous lui ; le .gltf est relu par le voxel (LoadGltfMesh).
String WriteTestGltf(const std::filesystem::path &dir) {
	dataio::gltf::SceneDescription scene;
	dataio::gltf::MeshDescription tri;
	tri.name = "tri";
	tri.positions = {0.f, 0.f, 0.f, 0.5f, 0.f, 0.f, 0.f, 0.5f, 0.f};
	tri.normals = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
	tri.uvs = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
	tri.indices = {0, 1, 2};
	scene.meshes.push_back(tri);
	scene.nodes.push_back({"base", 0, -1, {0.f, 2.f, 0.f}});
	scene.nodes.push_back({"bras", 0, 0, {1.f, 0.f, 0.f}});
	const String path((dir / "test.gltf").string().c_str());
	(void)dataio::gltf::SaveScene(path, scene);
	return path;
}

} // namespace

TEST(Gltf_ModelsAndRigParts) {
	const std::filesystem::path dir("build/tests/tmp/voxel/gltf_models");
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	const String path = WriteTestGltf(dir);
	// Scène entière : transformations monde (le bras est à (1, 2, 0)).
	Result<ModelMesh, String> all = LoadGltfMesh(path);
	REQUIRE(all.IsOk());
	CHECK_EQ(all.Value().positions.size(), size_t(6));
	CHECK_EQ(all.Value().indices.size(), size_t(6));
	CHECK(Near(all.Value().positions[0], {0.f, 2.f, 0.f}));
	CHECK(Near(all.Value().positions[3], {1.f, 2.f, 0.f}));
	CHECK_EQ(all.Value().groups.size(), size_t(2));
	// Un nœud : dans SON repère.
	Result<ModelMesh, String> arm = LoadGltfMesh(path, "bras");
	REQUIRE(arm.IsOk());
	CHECK_EQ(arm.Value().positions.size(), size_t(3));
	CHECK(Near(arm.Value().positions[1], {0.5f, 0.f, 0.f}));
	CHECK(LoadGltfMesh(path, "absent").Error().Contains("introuvable"));
	CHECK(LoadGltfMesh("build/tests/tmp/voxel/gltf_models/manquant.gltf").IsError());

	// Partie de rig maillée : relative au mod, en pixels, posée au pivot.
	const String def = String(R"({"parts": [{"name": "bras", "pivot": [16, 8, 0], "model": "test.gltf", "node": "bras"}]})");
	Result<RigModel, String> rig = RigFromJson(json::Parse(def).Value(), String(dir.string().c_str()));
	REQUIRE(rig.IsOk());
	const RigPart &part = rig.Value().parts[0];
	REQUIRE(part.mesh.positions.size() == size_t(3));
	CHECK(Near(part.mesh.positions[1], {16.f + 8.f, 8.f, 0.f})); // 0,5 bloc = 8 px, + pivot
	const ModelMesh built = BuildRigPart(rig.Value(), part, 64, 32);
	CHECK(Near(built.positions[1], {0.5f, 0.f, 0.f})); // relatif au pivot, en blocs
	CHECK(RigFromJson(json::Parse(String(R"({"parts": [{"name": "x", "model": "absent.gltf"}]})")).Value(), String(dir.string().c_str()))
			  .Error()
			  .Contains("partie « x »"));

	// Modèle .gltf chargé par l'AssetManager (forme de bloc, modèle d'entité).
	Game game;
	GameOptions options;
	options.savesDirectory = String("build/tests/tmp/voxel/gltf_models/saves");
	(void)game.LoadMods(options);
	std::shared_ptr<ModelResource> model = game.Assets().Model(path);
	REQUIRE(model != nullptr);
	CHECK_EQ(model->Mesh().indices.size(), size_t(6));
}

namespace {

const char *BIPED = R"({
	"type": "model", "id": "biped_test", "format": "rig",
	"parts": [
		{"name": "head", "pivot": [0, 24, 0], "look": true, "boxes": [
			{"from": [-4, 24, -4], "size": [8, 8, 8], "uv": [0, 0]},
			{"from": [-4, 24, -4], "size": [8, 8, 8], "uv": [32, 0], "inflate": 0.5}]},
		{"name": "body", "pivot": [0, 24, 0], "boxes": [{"from": [-4, 12, -2], "size": [8, 12, 4], "uv": [16, 16]}]},
		{"name": "right_arm", "pivot": [-5, 22, 0], "boxes": [{"from": [-8, 12, -2], "size": [4, 12, 4], "uv": [40, 16]}]},
		{"name": "left_arm", "pivot": [5, 22, 0], "boxes": [{"from": [4, 12, -2], "size": [4, 12, 4], "uv": [40, 16], "mirror": true}]}
	]
})";

} // namespace

TEST(Rig_EyeAndHandAnchors) {
	// Par défaut : devant la face avant de la tête (calque gonflé compris), à
	// mi-hauteur ; les mains au bas des bras.
	const RigModel rig = Parse(BIPED);
	REQUIRE(rig.eye.IsSet());
	CHECK_EQ(rig.eye.part, 0);
	CHECK(Near(rig.eye.at, {0.f, 28.f, 4.5f}));
	CHECK_EQ(rig.rightHand.part, 2);
	CHECK(Near(rig.rightHand.at, {-6.f, 12.f, 0.f}));
	CHECK_EQ(rig.leftHand.part, 3);
	CHECK(Near(rig.leftHand.at, {6.f, 12.f, 0.f}));
	// Déclarés : ils l'emportent.
	NodePtr def = json::Parse(String(BIPED)).Value();
	def->Set("eye", json::Parse(String(R"({"part": "body", "at": [0, 20, 3]})")).Value());
	def->Set("hands", json::Parse(String(R"({"left": {"part": "head", "at": [1, 2, 3]}})")).Value());
	const RigModel custom = RigFromJson(def).Unwrap();
	CHECK_EQ(custom.eye.part, 1);
	CHECK(Near(custom.eye.at, {0.f, 20.f, 3.f}));
	CHECK_EQ(custom.leftHand.part, 0);
	CHECK(Near(custom.leftHand.at, {1.f, 2.f, 3.f}));
	CHECK_EQ(custom.rightHand.part, 2); // l'autre main reste celle par défaut
	def->Set("eye", json::Parse(String(R"({"part": "tail"})")).Value());
	CHECK(RigFromJson(def).Error().Contains("« tail » inconnue"));
	// Sans partie qui suit le regard ni bras : aucun point.
	const RigModel bare = Parse(R"({"parts": [{"name": "a", "boxes": [{"from": [0, 0, 0], "size": [1, 1, 1]}]}]})");
	CHECK(!bare.eye.IsSet());
	CHECK(!bare.rightHand.IsSet());
}

TEST(Rig_PointInModelFollowsThePose) {
	const RigModel rig = Parse(BIPED);
	RigPoseInput input;
	// Au repos : le point tel quel.
	CHECK(Near(RigPointInModel(rig, 0, rig.eye.at, input), rig.eye.at));
	// Regard vers le haut de 90° : la tête bascule en arrière autour de son pivot.
	input.lookPitch = 1.5707963f;
	CHECK(Near(RigPointInModel(rig, 0, rig.eye.at, input), {0.f, 28.5f, -4.f}));
	// Vers le bas : le point passe sous le pivot, devant le torse.
	input.lookPitch = -1.5707963f;
	const math::FVector3 down = RigPointInModel(rig, 0, rig.eye.at, input);
	CHECK(Near(down, {0.f, 19.5f, 4.f}));
	// Chaîne des parents : la rotation du parent emporte l'enfant.
	const RigModel chain = Parse(R"({"parts": [
		{"name": "base", "pivot": [0, 0, 0], "rotation": [0, 90, 0]},
		{"name": "tip", "parent": "base", "pivot": [0, 0, 4], "rotation": [0, 0, 0]}]})");
	// (0, 0, 4) tourné de 90° autour de y : (4, 0, 0) ; le point (0, 0, 6) de tip : (6, 0, 0).
	CHECK(Near(RigPointInModel(chain, 1, {0.f, 0.f, 6.f}, RigPoseInput{}), {6.f, 0.f, 0.f}));
}

TEST(Rig_HeldItemsAndSwingRaiseTheArms) {
	const RigModel rig = Parse(BIPED);
	RigPoseInput input;
	const float rest = PoseRig(rig, 2, input).rotation.x;
	input.holdRight = true;
	CHECK(PoseRig(rig, 2, input).rotation.x < rest - 10.f); // vers l'avant (x négatif)
	CHECK_NEAR(PoseRig(rig, 3, input).rotation.x, PoseRig(rig, 3, RigPoseInput{}).rotation.x, 1e-5f);
	input.holdLeft = true;
	CHECK(PoseRig(rig, 3, input).rotation.x < rest - 10.f);
	// Le geste : nul aux extrémités, maximal à mi-course.
	input.swing = 0.5f;
	const float mid = PoseRig(rig, 2, input).rotation.x;
	input.swing = 0.f;
	CHECK(mid < PoseRig(rig, 2, input).rotation.x - 60.f);
	// Le bras levé avance la main : elle passe devant le corps (z > 0).
	input.holdRight = true;
	CHECK(RigPointInModel(rig, 2, rig.rightHand.at, input).z > 2.f);
}

TEST(Rig_TopAndBottomFacesKeepTheSkinOrientation) {
	// Convention des skins : sur le dessus comme sur le dessous, le bord AVANT
	// (+z) de la face est au bas (v + d) de son rectangle, le bord arrière en
	// haut (v) ; x croissant va vers la droite du rectangle.
	RigModel rig;
	RigPart part;
	RigBox box;
	box.from = {-4.f, 24.f, -4.f};
	box.size = {8.f, 8.f, 8.f};
	part.pivot = {0.f, 24.f, 0.f};
	part.boxes.push_back(box);
	rig.parts.push_back(part);
	const ModelMesh mesh = BuildRigPart(rig, rig.parts[0], 64, 32);
	REQUIRE(mesh.uvs.size() == size_t(24));
	for (size_t f : {size_t(4), size_t(5)}) { // dessus, dessous
		const float vMin = 0.f, vMax = 8.f / 32.f;
		const float uLeft = (f == 4 ? 8.f : 16.f) / 64.f;
		for (size_t k = 0; k < 4; ++k) {
			const math::FVector3 p = mesh.positions[f * 4 + k];
			const math::FVector2 uv = mesh.uvs[f * 4 + k];
			CHECK_NEAR(uv.y, p.z > 0.f ? vMax : vMin, 1e-5f);
			CHECK_NEAR(uv.x, p.x < 0.f ? uLeft : uLeft + 8.f / 64.f, 1e-5f);
		}
	}
}
