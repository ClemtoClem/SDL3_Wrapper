// Tests — atelier de formes de Voxelia Studio sans fenêtre : document d'un
// modèle articulé (hiérarchie, boîtes, dépliage, animations à images clés,
// export OBJ, annulation) et son rechargement à chaud dans une vraie partie.
#include "test.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <dataio/documents/gltf.hpp>
#include <voxelia/studio/rig_document.hpp>

using namespace voxelia::studio;
using voxel::RigChannel;
using voxel::RigInterp;

namespace {

voxel::GameOptions RigOptions(const char *saves) {
	voxel::GameOptions o;
	o.modDirectories = {String("demos/voxelia/mods")};
	o.savesDirectory = String(saves);
	o.viewRadius = 2;
	o.verticalRadius = 2;
	o.chunkBudget = 200;
	return o;
}

RigDocument Fresh() { return RigDocument::New("poupee", "test", "build/tests/tmp/voxelia", "build/tests/tmp/voxelia/rig.json"); }

std::vector<String> Names(const RigDocument &doc) {
	std::vector<String> out;
	for (const RigPartInfo &p : doc.Parts())
		out.push_back(p.name);
	return out;
}

bool Near(const math::FVector3 &a, const math::FVector3 &b) {
	return std::fabs(a.x - b.x) < 1e-3f && std::fabs(a.y - b.y) < 1e-3f && std::fabs(a.z - b.z) < 1e-3f;
}

} // namespace

TEST(StudioRig_OpensTheModsModels) {
	voxel::Game game;
	(void)game.LoadMods(RigOptions("build/tests/tmp/voxelia/studio_rig_open"));
	Option<RigDocument> doc = RigDocument::FromModel(game.Content(), "core:biped");
	REQUIRE(doc.IsSome());
	CHECK(doc.Value().SourceFile().EndsWith("rigs.json"));
	CHECK(doc.Value().HasPart("head"));
	CHECK(doc.Value().Build().IsOk());
	CHECK(doc.Value().Validate().empty());
	CHECK(RigDocument::FromModel(game.Content(), "core:inexistant").IsNone());
}

TEST(StudioRig_HierarchyKeepsItsInvariants) {
	RigDocument doc = Fresh();
	REQUIRE(doc.AddPart("arm", "body").IsOk());
	REQUIRE(doc.AddPart("hand", "arm").IsOk());
	CHECK(doc.AddPart("arm", "").IsError());      // doublon
	CHECK(doc.AddPart("a b", "").IsError());      // nom invalide
	CHECK(doc.AddPart("x", "fantome").IsError()); // parent inconnu
	// Cycle refusé : « body » ne peut pas devenir enfant de « hand ».
	CHECK(doc.Reparent("body", "hand").IsError());
	// Reparenter : le parent reste AVANT l'enfant (exigé par le moteur).
	REQUIRE(doc.AddPart("tail", "").IsOk());
	REQUIRE(doc.Reparent("tail", "hand").IsOk());
	REQUIRE(doc.Reparent("hand", "").IsOk()); // la main devient racine, la queue la suit
	std::vector<String> names = Names(doc);
	auto at = [&](const char *n) { return std::find(names.begin(), names.end(), String(n)) - names.begin(); };
	CHECK(at("hand") < at("tail"));
	CHECK(doc.Build().IsOk());
	// Profondeurs.
	for (const RigPartInfo &p : doc.Parts()) {
		if (p.name == "tail")
			CHECK_EQ(p.depth, 1);
		if (p.name == "arm")
			CHECK_EQ(p.depth, 1);
	}
	// Supprimer : les enfants passent au grand-parent.
	REQUIRE(doc.Reparent("hand", "arm").IsOk());
	REQUIRE(doc.RemovePart("arm").IsOk());
	CHECK(!doc.HasPart("arm"));
	for (const RigPartInfo &p : doc.Parts())
		if (p.name == "hand")
			CHECK(p.parent == "body");
	CHECK(doc.Build().IsOk());
}

TEST(StudioRig_RenameFollowsParentsAndTracks) {
	RigDocument doc = Fresh();
	REQUIRE(doc.AddPart("head", "body").IsOk());
	REQUIRE(doc.AddClip("idle", 2.f, true).IsOk());
	REQUIRE(doc.SetKey("idle", "head", RigChannel::ROTATION, 1.f, {0.f, 30.f, 0.f}).IsOk());
	REQUIRE(doc.RenamePart("body", "torse").IsOk());
	REQUIRE(doc.RenamePart("head", "tete").IsOk());
	for (const RigPartInfo &p : doc.Parts())
		if (p.name == "tete")
			CHECK(p.parent == "torse");
	CHECK_EQ(doc.Keys("idle", "tete", RigChannel::ROTATION).size(), size_t(1));
	CHECK(doc.Build().IsOk());
	// Une partie supprimée perd ses pistes.
	REQUIRE(doc.RemovePart("tete").IsOk());
	CHECK(doc.Build().IsOk());
	CHECK(doc.Keys("idle", "tete", RigChannel::ROTATION).empty());
}

TEST(StudioRig_BoxesMirrorAndUv) {
	RigDocument doc = Fresh();
	REQUIRE(doc.AddBox("body", {2.f, 0.f, -1.f}, {3.f, 6.f, 2.f}, 40, 16).IsOk());
	CHECK(doc.AddBox("body", {0.f, 0.f, 0.f}, {0.f, 1.f, 1.f}, 0, 0).IsError());
	REQUIRE(doc.MirrorBox("body", 1).IsOk());
	const std::vector<voxel::RigBox> boxes = doc.Boxes("body");
	REQUIRE(boxes.size() == size_t(3));
	CHECK(Near(boxes[2].from, {-5.f, 0.f, -1.f})); // [2, 5] → [−5, −2]
	CHECK(boxes[2].mirror);
	CHECK_EQ(boxes[2].u, 40);
	// Le dépliage est celui du moteur.
	const std::array<voxel::RigUvFace, 6> uv = doc.UvLayout("body", 1);
	const std::array<voxel::RigUvFace, 6> engine = voxel::RigBoxUv(boxes[1]);
	for (size_t i = 0; i < 6; ++i)
		CHECK(uv[i].u == engine[i].u && uv[i].v == engine[i].v && uv[i].width == engine[i].width);
	// Hors texture : avertissement.
	voxel::RigBox far = boxes[1];
	far.u = 60;
	REQUIRE(doc.SetBox("body", 1, far).IsOk());
	bool warned = false;
	for (const RigDiagnostic &d : doc.Validate())
		warned = warned || d.message.Contains("hors de la texture");
	CHECK(warned);
	REQUIRE(doc.RemoveBox("body", 2).IsOk());
	CHECK_EQ(doc.Boxes("body").size(), size_t(2));
	CHECK(doc.RemoveBox("body", 9).IsError());
}

TEST(StudioRig_KeyframesAndCurves) {
	RigDocument doc = Fresh();
	REQUIRE(doc.AddClip("wave", 1.f, false).IsOk());
	CHECK(doc.AddClip("wave", 1.f, false).IsError());
	CHECK(doc.AddClip("vide", 0.f, true).IsError());
	REQUIRE(doc.SetKey("wave", "body", RigChannel::ROTATION, 0.f, {0.f, 0.f, 0.f}).IsOk());
	REQUIRE(doc.SetKey("wave", "body", RigChannel::ROTATION, 1.f, {0.f, 0.f, 90.f}, RigInterp::SMOOTH).IsOk());
	REQUIRE(doc.SetKey("wave", "body", RigChannel::ROTATION, 0.5f, {0.f, 0.f, 10.f}).IsOk());
	REQUIRE(doc.SetKey("wave", "body", RigChannel::ROTATION, 0.5f, {0.f, 0.f, 20.f}).IsOk()); // remplace
	CHECK(doc.SetKey("wave", "body", RigChannel::ROTATION, 2.f, {0.f, 0.f, 0.f}).IsError());  // hors du clip
	CHECK(doc.SetKey("wave", "nulle", RigChannel::ROTATION, 0.f, {0.f, 0.f, 0.f}).IsError());
	const std::vector<voxel::RigKey> keys = doc.Keys("wave", "body", RigChannel::ROTATION);
	REQUIRE(keys.size() == size_t(3));
	CHECK(std::fabs(keys[1].value.z - 20.f) < 1e-4f);
	CHECK(keys[2].interp == RigInterp::SMOOTH);
	// Courbe : échantillonnée par le moteur (SampleRigTrack).
	const std::vector<math::FVector3> curve = doc.SampleCurve("wave", "body", RigChannel::ROTATION, 5);
	REQUIRE(curve.size() == size_t(5));
	CHECK(Near(curve[0], {0.f, 0.f, 0.f}));
	CHECK(Near(curve[1], {0.f, 0.f, 10.f})); // linéaire : 0 → 20 à mi-chemin
	CHECK(Near(curve[2], {0.f, 0.f, 20.f}));
	CHECK(Near(curve[3], {0.f, 0.f, 20.f + 70.f * 0.5f})); // douce : smoothstep(0,5) = 0,5
	CHECK(Near(curve[4], {0.f, 0.f, 90.f}));
	// Retirer la dernière clé d'une piste retire la piste.
	REQUIRE(doc.RemoveKey("wave", "body", RigChannel::ROTATION, 0.5f).IsOk());
	CHECK(doc.RemoveKey("wave", "body", RigChannel::ROTATION, 0.5f).IsError());
	REQUIRE(doc.RemoveKey("wave", "body", RigChannel::ROTATION, 0.f).IsOk());
	REQUIRE(doc.RemoveKey("wave", "body", RigChannel::ROTATION, 1.f).IsOk());
	CHECK(doc.Keys("wave", "body", RigChannel::ROTATION).empty());
	CHECK(doc.Build().IsOk());
	REQUIRE(doc.RemoveClip("wave").IsOk());
	CHECK(doc.Clips().empty());
}

TEST(StudioRig_UndoAndRotation) {
	RigDocument doc = Fresh();
	const size_t depth = doc.UndoDepth();
	REQUIRE(doc.AddPart("arm", "body").IsOk());
	REQUIRE(doc.SetRotation("arm", {0.f, 45.f, 0.f}).IsOk());
	CHECK_EQ(doc.UndoDepth(), depth + 2);
	CHECK(Near(doc.Rotation("arm"), {0.f, 45.f, 0.f}));
	CHECK(doc.Undo());
	CHECK(Near(doc.Rotation("arm"), {0.f, 0.f, 0.f}));
	CHECK(doc.Undo());
	CHECK(!doc.HasPart("arm"));
	CHECK(doc.Redo());
	CHECK(doc.HasPart("arm"));
	// Un glissé de pivot = une étape.
	doc.BeginEdit();
	for (int i = 0; i < 20; ++i)
		REQUIRE(doc.SetPivot("arm", {float(i), 0.f, 0.f}).IsOk());
	doc.EndEdit();
	CHECK_EQ(doc.UndoDepth(), depth + 2);
	CHECK(Near(doc.Pivot("arm"), {19.f, 0.f, 0.f}));
}

TEST(StudioRig_ExportObj) {
	RigDocument doc = Fresh();
	REQUIRE(doc.AddPart("head", "body").IsOk());
	REQUIRE(doc.SetPivot("head", {0.f, 8.f, 0.f}).IsOk());
	REQUIRE(doc.AddBox("head", {-2.f, 8.f, -2.f}, {4.f, 4.f, 4.f}, 32, 0).IsOk());
	REQUIRE(doc.AddClip("nod", 1.f, true).IsOk());
	REQUIRE(doc.SetKey("nod", "head", RigChannel::POSITION, 1.f, {0.f, 16.f, 0.f}).IsOk());
	const String path("build/tests/tmp/voxelia/studio_rig/poupee.obj");
	std::filesystem::remove_all("build/tests/tmp/voxelia/studio_rig");
	Result<size_t, String> rest = doc.ExportObj(path);
	REQUIRE(rest.IsOk());
	CHECK_EQ(rest.Value(), size_t(24)); // 2 boîtes × 6 faces × 2 triangles
	std::ifstream in(path.CStr());
	std::stringstream text;
	text << in.rdbuf();
	const std::string obj = text.str();
	size_t v = 0, vt = 0, f = 0;
	float maxY = -1e9f;
	std::istringstream lines(obj);
	for (std::string line; std::getline(lines, line);) {
		if (line.rfind("v ", 0) == 0) {
			++v;
			float x, y, z;
			if (std::sscanf(line.c_str(), "v %f %f %f", &x, &y, &z) == 3)
				maxY = std::max(maxY, y);
		}
		vt += line.rfind("vt ", 0) == 0;
		f += line.rfind("f ", 0) == 0;
	}
	CHECK_EQ(v, size_t(48));
	CHECK_EQ(vt, size_t(48));
	CHECK_EQ(f, size_t(24));
	CHECK(std::fabs(maxY - 12.f / 16.f) < 1e-3f); // la tête culmine à 12 px (repos)
	// Posé par le clip : la tête monte de 16 px.
	REQUIRE(doc.ExportObj(path, "nod", 1.f).IsOk());
	std::ifstream again(path.CStr());
	maxY = -1e9f;
	for (std::string line; std::getline(again, line);) {
		float x, y, z;
		if (line.rfind("v ", 0) == 0 && std::sscanf(line.c_str(), "v %f %f %f", &x, &y, &z) == 3)
			maxY = std::max(maxY, y);
	}
	CHECK(std::fabs(maxY - 28.f / 16.f) < 1e-3f);
	CHECK(doc.ExportObj(path, "absent").IsError());
}

TEST(StudioRig_HotReloadInAGame) {
	voxel::Game game;
	(void)game.LoadMods(RigOptions("build/tests/tmp/voxelia/studio_rig_reload"));
	RigDocument doc = RigDocument::FromModel(game.Content(), "core:biped").Unwrap();
	std::shared_ptr<const voxel::RigModel> before = game.Assets().Rig("core:biped");
	REQUIRE(before != nullptr);
	REQUIRE(doc.AddClip("idle", 2.f, true).IsOk());
	REQUIRE(doc.SetKey("idle", "head", RigChannel::ROTATION, 1.f, {0.f, 30.f, 0.f}).IsOk());
	REQUIRE(game.ReloadDefinition(doc.Json(), doc.Context()).IsOk());
	std::shared_ptr<const voxel::RigModel> after = game.Assets().Rig("core:biped");
	REQUIRE(after != nullptr);
	CHECK(after->FindClip("idle") != nullptr);
	CHECK(before->FindClip("idle") == nullptr);
	// Le moteur joue le clip idle en permanence : la tête tourne à t = 1 s.
	voxel::RigPoseInput input;
	input.time = 1.f;
	const int head = after->Find("head");
	REQUIRE(head >= 0);
	CHECK(voxel::PoseRig(*after, size_t(head), input).rotation.y > 25.f);
}

TEST(StudioRig_BezierHandlesAndAutoSmooth) {
	RigDocument doc = Fresh();
	REQUIRE(doc.AddClip("swing", 2.f, true).IsOk());
	REQUIRE(doc.SetKey("swing", "body", RigChannel::ROTATION, 0.f, {0.f, 0.f, 0.f}).IsOk());
	REQUIRE(doc.SetKey("swing", "body", RigChannel::ROTATION, 1.f, {0.f, 30.f, 0.f}).IsOk());
	REQUIRE(doc.SetKey("swing", "body", RigChannel::ROTATION, 2.f, {0.f, 0.f, 0.f}).IsOk());
	CHECK(doc.SetKeyHandles("swing", "body", RigChannel::ROTATION, 0.5f, NONE, NONE).IsError()); // pas de clé
	// Lissage automatique : pente nulle à la clé du milieu (sommet), toutes en Bézier.
	REQUIRE(doc.AutoSmooth("swing", "body", RigChannel::ROTATION).IsOk());
	std::vector<voxel::RigKey> keys = doc.Keys("swing", "body", RigChannel::ROTATION);
	REQUIRE(keys.size() == size_t(3));
	CHECK(keys[1].interp == RigInterp::BEZIER);
	REQUIRE(keys[1].out.has_value());
	CHECK(std::fabs(keys[1].out->value.y) < 1e-4f);
	CHECK(std::fabs(keys[1].out->time - 1.f / 3.f) < 1e-4f);
	// Poignée de sortie de la première clé relevée : la courbe dépasse 30.
	REQUIRE(doc.SetKeyHandles("swing", "body", RigChannel::ROTATION, 0.f, NONE, Some(voxel::RigHandle{0.3f, {0.f, 60.f, 0.f}})).IsOk());
	keys = doc.Keys("swing", "body", RigChannel::ROTATION);
	REQUIRE(keys[0].out.has_value());
	float peak = 0.f;
	for (const math::FVector3 &v : doc.SampleCurve("swing", "body", RigChannel::ROTATION, 81))
		peak = std::max(peak, v.y);
	CHECK(peak > 30.5f);
	// Remplacer la valeur d'une clé garde ses poignées.
	REQUIRE(doc.SetKey("swing", "body", RigChannel::ROTATION, 0.f, {0.f, 5.f, 0.f}, RigInterp::BEZIER).IsOk());
	keys = doc.Keys("swing", "body", RigChannel::ROTATION);
	CHECK(keys[0].out.has_value());
	CHECK(doc.Build().IsOk());
}

TEST(StudioRig_ClipLayers) {
	RigDocument doc = Fresh();
	REQUIRE(doc.AddClip("a", 1.f, true).IsOk());
	REQUIRE(doc.AddClip("b", 1.f, true).IsOk());
	REQUIRE(doc.SetKey("a", "body", RigChannel::ROTATION, 0.f, {0.f, 40.f, 0.f}).IsOk());
	REQUIRE(doc.SetKey("b", "body", RigChannel::ROTATION, 0.f, {10.f, 0.f, 0.f}).IsOk());
	CHECK(doc.AddLayer({String("absent"), 1.f, 1.f, voxel::RigBlend::MIX}).IsError());
	REQUIRE(doc.AddLayer({String("a"), 0.5f, 1.f, voxel::RigBlend::MIX}).IsOk());
	REQUIRE(doc.AddLayer({String("b"), 1.f, 2.f, voxel::RigBlend::ADD}).IsOk());
	REQUIRE(doc.Layers().size() == size_t(2));
	CHECK(doc.Layers()[1].mode == voxel::RigBlend::ADD);
	CHECK(std::fabs(doc.Layers()[1].speed - 2.f) < 1e-6f);
	Result<voxel::RigModel, String> rig = doc.Build();
	REQUIRE(rig.IsOk());
	voxel::RigPoseInput input;
	CHECK(Near(voxel::PoseRig(rig.Value(), 0, input).rotation, {10.f, 20.f, 0.f}));
	REQUIRE(doc.SetLayer(0, {String("a"), 1.f, 1.f, voxel::RigBlend::MIX}).IsOk());
	CHECK(Near(voxel::PoseRig(doc.Build().Value(), 0, input).rotation, {10.f, 40.f, 0.f}));
	REQUIRE(doc.RemoveLayer(0).IsOk());
	REQUIRE(doc.RemoveLayer(0).IsOk());
	CHECK(doc.Layers().empty());
	CHECK(doc.RemoveLayer(0).IsError());
}

TEST(StudioRig_GltfExportAndImport) {
	namespace fs = std::filesystem;
	const fs::path dir("build/tests/tmp/voxelia/studio_rig_gltf");
	fs::remove_all(dir);
	fs::create_directories(dir);
	RigDocument doc = Fresh();
	REQUIRE(doc.AddPart("head", "body").IsOk());
	REQUIRE(doc.SetPivot("head", {0.f, 8.f, 0.f}).IsOk());
	REQUIRE(doc.SetRotation("head", {0.f, 90.f, 0.f}).IsOk());
	REQUIRE(doc.AddBox("head", {-2.f, 8.f, -2.f}, {4.f, 4.f, 4.f}, 32, 0).IsOk());
	REQUIRE(doc.AddClip("nod", 1.f, true).IsOk());
	REQUIRE(doc.SetKey("nod", "head", RigChannel::ROTATION, 0.f, {0.f, 0.f, 0.f}).IsOk());
	REQUIRE(doc.SetKey("nod", "head", RigChannel::ROTATION, 1.f, {30.f, 0.f, 0.f}, RigInterp::SMOOTH).IsOk());
	{
		std::ofstream png((dir / "skin.png").string(), std::ios::binary);
		png << "PNG";
	}
	const String gltf((dir / "out" / "poupee.gltf").string().c_str());
	Result<size_t, String> exported = doc.ExportGltf(gltf, String((dir / "skin.png").string().c_str()), 10.f);
	REQUIRE(exported.IsOk());
	CHECK_EQ(exported.Value(), size_t(24));
	CHECK(fs::exists(dir / "out" / "poupee.bin"));
	CHECK(fs::exists(dir / "out" / "skin.png")); // le skin voyage avec le modèle
	// Relu par le lecteur glTF du dépôt : hiérarchie, rotation de repos, image.
	Result<dataio::gltf::Document, String> read = dataio::gltf::LoadFile(gltf, true);
	REQUIRE(read.IsOk());
	REQUIRE(read.Value().nodes.size() == size_t(2));
	CHECK(read.Value().nodes[1].name == "head");
	CHECK(std::fabs(read.Value().nodes[1].translation[1] - 0.5f) < 1e-5f); // 8 px au-dessus du corps
	CHECK(std::fabs(read.Value().nodes[1].rotation[1] - 0.7071068f) < 1e-4f);
	REQUIRE(read.Value().images.size() == size_t(1));
	CHECK(read.Value().images[0].uri == "skin.png");
	std::ifstream text(gltf.CStr());
	const std::string body((std::istreambuf_iterator<char>(text)), std::istreambuf_iterator<char>());
	CHECK(body.find("\"animations\"") != std::string::npos);
	CHECK(body.find("\"nod\"") != std::string::npos);

	// Import dans un modèle vide : même hiérarchie, mêmes pivots, maillages du fichier.
	RigDocument imported = RigDocument::New("importe", "test", dir.string().c_str(), (dir / "rig.json").string().c_str());
	Result<size_t, String> parts = imported.ImportGltf(gltf);
	REQUIRE(parts.IsOk());
	CHECK_EQ(parts.Value(), size_t(2));
	CHECK(imported.HasPart("head"));
	for (const RigPartInfo &p : imported.Parts())
		if (p.name == "head")
			CHECK(p.parent == "body");
	CHECK(Near(imported.Pivot("head"), {0.f, 8.f, 0.f}));
	CHECK(imported.FieldText("texture").EndsWith("skin.png")); // le skin du glTF
	CHECK(Near(imported.Rotation("head"), {0.f, 90.f, 0.f}));
	Result<voxel::RigModel, String> rig = imported.Build();
	if (rig.IsError())
		std::fprintf(stderr, "\n  %s", rig.Error().CStr());
	REQUIRE(rig.IsOk());
	const int head = rig.Value().Find("head");
	REQUIRE(head >= 0);
	CHECK_EQ(rig.Value().parts[size_t(head)].mesh.indices.size(), size_t(36));
	// La géométrie relue reproduit l'originale (export OBJ au repos identique).
	const String a((dir / "a.obj").string().c_str()), b((dir / "b.obj").string().c_str());
	REQUIRE(doc.ExportObj(a).IsOk());
	REQUIRE(imported.ExportObj(b).IsOk());
	auto box = [](const String &path) {
		std::ifstream in(path.CStr());
		float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
		for (std::string line; std::getline(in, line);) {
			float v[3];
			if (line.rfind("v ", 0) == 0 && std::sscanf(line.c_str(), "v %f %f %f", &v[0], &v[1], &v[2]) == 3)
				for (int k = 0; k < 3; ++k) {
					lo[k] = std::min(lo[k], v[k]);
					hi[k] = std::max(hi[k], v[k]);
				}
		}
		return std::array<float, 6>{lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]};
	};
	const std::array<float, 6> ba = box(a), bb = box(b);
	for (size_t k = 0; k < 6; ++k)
		CHECK(std::fabs(ba[k] - bb[k]) < 1e-3f);
	// Angles retrouvés (cas général et blocage de cardan) : même rotation.
	for (const math::FVector3 &angles : {math::FVector3{20.f, -35.f, 70.f}, math::FVector3{0.f, 90.f, 0.f}, math::FVector3{30.f, -90.f, 0.f}}) {
		const math::FQuaternion q = voxel::RigRotation(angles), back = voxel::RigRotation(RigEulerFromQuaternion(q));
		CHECK(std::fabs(std::fabs(q.x * back.x + q.y * back.y + q.z * back.z + q.w * back.w) - 1.f) < 1e-4f);
	}
	CHECK(imported.ImportGltf(String("build/tests/tmp/voxelia/absent.gltf")).IsError());
	// Skin 64×64 sur un modèle déclaré 64×32 : les UV exportées visent la
	// VRAIE image (v ≤ 0,5 pour une boîte posée en haut du dépliage).
	RigDocument tall = Fresh();
	REQUIRE(tall.AddBox("body", {-2.f, 0.f, -2.f}, {4.f, 12.f, 4.f}, 16, 16).IsOk());
	const String tallPath((dir / "tall.gltf").string().c_str());
	REQUIRE(tall.ExportGltf(tallPath, String(), 30.f, 64, 64).IsOk());
	Result<dataio::gltf::Document, String> tallDoc = dataio::gltf::LoadFile(tallPath, true);
	REQUIRE(tallDoc.IsOk());
	auto uvs = tallDoc.Value().ReadFloats(tallDoc.Value().meshes[0].primitives[0].Find("TEXCOORD_0"));
	REQUIRE(uvs.IsOk());
	float maxV = 0.f;
	for (size_t i = 1; i < uvs.Value().size(); i += 2)
		maxV = std::max(maxV, uvs.Value()[i]);
	CHECK(maxV <= 0.5f + 1e-5f); // (16 + 4 + 12) / 64 ; avec 64×32 ce serait 1
}
