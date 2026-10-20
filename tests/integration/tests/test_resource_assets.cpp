// Tests d'intégration : ResourceCache/AssetCatalog (resources) peuplés par les
// fabriques de ressources concrètes de render3d (maillages, textures,
// matériaux) et d'audio (clips) — la composition que fait une application.
// Ils vivent ici, au-dessus des trois modules, pour que resources n'ait
// jamais à dépendre de render3d ni d'audio.
#include "test.hpp"
#include <audio/clip.hpp>
#include <render3d/resources.hpp>
#include <resources/resources.hpp>
#include <sdl3/sdl3.hpp>

#include <cstdio>
#include <filesystem>

using namespace resources;

namespace {

/// Un WAV minimal (PCM 16 bits mono, 8000 Hz, `frames` échantillons nuls).
String WriteWav(const char *name, int frames) {
	const std::string dir = "build/tests/tmp/resources_asset";
	std::filesystem::create_directories(dir);
	const std::string path = dir + "/" + name;
	FILE *file = std::fopen(path.c_str(), "wb");
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, file); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, file); };
	const uint32_t dataBytes = uint32_t(frames) * 2;
	std::fwrite("RIFF", 1, 4, file);
	u32(36 + dataBytes);
	std::fwrite("WAVEfmt ", 1, 8, file);
	u32(16);
	u16(1);    // PCM
	u16(1);    // mono
	u32(8000); // fréquence
	u32(16000);
	u16(2);
	u16(16);
	std::fwrite("data", 1, 4, file);
	u32(dataBytes);
	for (int i = 0; i < frames; ++i)
		u16(0);
	std::fclose(file);
	return String(path.c_str());
}

void RegisterFactories(ResourceCache &cache) {
	render3d::RegisterResourceFactories(cache);
	audio::RegisterResourceFactories(cache);
}

} // namespace

TEST(AssetCatalog_JsonRoundTrip) {
	AssetCatalog catalog;
	AssetRecord mesh{AssetId::FromPath(StringView("models/k.gltf")), AssetType::MESH, String("k.gltf"),
					 String("models/k.gltf"), String(), nullptr};
	AssetRecord material{AssetId::FromKey(StringView("material:Pierre")), AssetType::MATERIAL, String("Pierre"),
						 String(), String("material:Pierre"), render3d::MaterialToJson(render3d::Material::Wood())};
	(void)catalog.Put(mesh);
	(void)catalog.Put(material);
	auto back = AssetCatalog::FromJson(catalog.ToJson());
	CHECK(back.IsOk());
	CHECK_EQ(back.Value().Size(), size_t(2));
	CHECK(back.Value().FindByPath(StringView("models/k.gltf"))->id == mesh.id);
	const AssetRecord *pierre = back.Value().FindByKey(StringView("material:Pierre"));
	CHECK(pierre != nullptr);
	CHECK(pierre->type == AssetType::MATERIAL);
	CHECK(render3d::MaterialFromJson(pierre->params).baseColor.r == render3d::Material::Wood().baseColor.r);
}

TEST(ResourceCache_PrimitivesAreSharedNotCopied) {
	ResourceCache cache(String(), 2);
	RegisterFactories(cache);
	const AssetRef<render3d::MeshResource> a = render3d::Primitive(cache, render3d::PrimitiveSpec::Box(2.f, 4.6f, 0.6f));
	const AssetRef<render3d::MeshResource> b =
		render3d::Primitive(cache, render3d::PrimitiveSpec::Box(2.0000001f, 4.6f, 0.6f)); // même clé
	const AssetRef<render3d::MeshResource> c = render3d::Primitive(cache, render3d::PrimitiveSpec::Box(1.f, 1.f, 1.f));
	CHECK(a == b);
	CHECK(a != c);
	auto meshA = cache.GetReady(a);
	auto meshB = cache.GetReady(b);
	CHECK(meshA != nullptr);
	CHECK(meshA.get() == meshB.get()); // UNE géométrie, deux références
	CHECK(meshA->Geometry().IndexCount() > 0);
	CHECK_EQ(cache.ResidentCount(AssetType::MESH), size_t(2));
	// Une primitive se recrée depuis sa clé : oubliée, elle revient.
	meshA.reset();
	meshB.reset();
	CHECK_EQ(cache.UnloadUnused(), size_t(2));
	CHECK(!cache.IsResident(a.id));
	CHECK(cache.LoadNow(a.id));
	CHECK(cache.GetReady(a)->Geometry().IndexCount() > 0);
}

TEST(ResourceCache_FilesLoadAsynchronously) {
	ResourceCache cache(String(), 2);
	RegisterFactories(cache);
	const AssetRef<render3d::MeshResource> knight =
		render3d::ImportMesh(cache, String("assets/models/knight/KnightCharacter.gltf"));
	// Même fichier : même id, pas de second chargement.
	CHECK(render3d::ImportMesh(cache, String("assets/models/knight/KnightCharacter.gltf")) == knight);
	const AssetRef<render3d::TextureResource> texture =
		render3d::ImportTexture(cache, String("assets/textures/default_illumi_root.png"));
	const String wavPath = WriteWav("bip.wav", 800);
	const AssetRef<audio::AudioClipResource> sound = audio::ImportAudio(cache, wavPath);
	CHECK(cache.WaitIdle());
	auto mesh = cache.GetReady(knight);
	CHECK(mesh != nullptr);
	CHECK(mesh->Geometry().Vertices().size() > 100);
	auto image = cache.GetReady(texture);
	CHECK(image != nullptr);
	CHECK_EQ(image->Pixels().size(), size_t(image->Width()) * size_t(image->Height()) * 4);
	auto clip = cache.GetReady(sound);
	CHECK(clip != nullptr);
	CHECK_EQ(clip->Frequency(), 8000);
	CHECK(sdl3::Abs(float(clip->DurationSeconds()) - 0.1f) < 1e-3f);
	CHECK(cache.Missing().empty());
	// Une référence typée ne résout pas une autre nature.
	CHECK(cache.Get(AssetRef<render3d::TextureResource>{knight.id}) == nullptr);
}

TEST(ResourceCache_MissingReferencesAreReportedNotFatal) {
	ResourceCache cache(String(), 2);
	RegisterFactories(cache);
	const AssetRef<render3d::MeshResource> broken =
		render3d::ImportMesh(cache, String("build/tests/tmp/absent.gltf"), LoadPolicy::SYNC);
	auto mesh = cache.Get(broken, StringView("/scenes/Test/Socle"));
	CHECK(mesh != nullptr);
	CHECK(mesh->HasFailed());
	CHECK(cache.GetReady(broken) == nullptr);
	const AssetRef<render3d::MeshResource> unknown{AssetId::Generate()};
	CHECK(cache.Get(unknown, StringView("/scenes/Test/Fantôme")) == nullptr);
	const std::vector<MissingAsset> missing = cache.Missing();
	ASSERT_EQ(missing.size(), size_t(2));
	bool sawContext = false;
	for (const MissingAsset &entry : missing)
		sawContext = sawContext || entry.context == String("/scenes/Test/Fantôme");
	CHECK(sawContext);
	cache.ClearMissing();
	CHECK(cache.Missing().size() == 1); // l'échec résident est toujours là
}

TEST(ResourceCache_MaterialsAreSharedAndReloadFromTheCatalog) {
	ResourceCache cache(String(), 2);
	RegisterFactories(cache);
	const AssetRef<render3d::TextureResource> leaves =
		render3d::ImportTexture(cache, String("assets/textures/default_crystal_leaves.png"), LoadPolicy::SYNC);
	const AssetRef<render3d::MaterialResource> stone =
		render3d::CreateMaterial(cache, String("Pierre"), render3d::Material::Plastic(sdl3::Color{120, 110, 100, 255}),
								 leaves, render3d::BuiltinShader(cache, render3d::ShaderKind::PHONG));
	auto material = cache.GetReady(stone);
	CHECK(material != nullptr);
	CHECK(material->Albedo() == leaves);
	// Modifier le matériau partagé : tous ses utilisateurs le voient.
	render3d::Material darker = material->Parameters();
	darker.baseColor = sdl3::Color{20, 20, 20, 255};
	material->Set(darker, material->Albedo(), material->Shader());
	CHECK(cache.GetReady(stone)->Parameters().baseColor.r == 20);

	// Le catalogue suffit à tout recréer (matériau, texture, shader, primitive).
	const AssetRef<render3d::MeshResource> box = render3d::Primitive(cache, render3d::PrimitiveSpec::Box(1.f, 2.f, 3.f));
	ResourceCache fresh(String(), 2);
	RegisterFactories(fresh);
	CHECK(fresh.LoadCatalog(cache.CatalogToJson()).IsNone());
	auto reloaded = fresh.Get(stone);
	CHECK(reloaded != nullptr);
	CHECK(reloaded->Albedo() == leaves);
	CHECK(reloaded->Parameters().baseColor.r == 120); // l'état catalogué (CreateMaterial)
	CHECK(fresh.LoadNow(leaves.id));
	CHECK(fresh.GetReady(leaves)->Width() > 0);
	CHECK(fresh.LoadNow(box.id));
	CHECK(fresh.GetReady(box)->Primitive().Unwrap().size.z == 3.f);
	CHECK(fresh.GetReady(render3d::BuiltinShader(fresh, render3d::ShaderKind::PHONG)) != nullptr);
}