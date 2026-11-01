// Tests — atelier de textures de Voxelia Studio sans fenêtre : peinture
// (touches, symétries, sélection, gomme, traits), formes, remplissage
// (tolérance, seuil alpha), pipette et palette, déplacement, flou / netteté,
// génération, calques (fusion, opacité, ordre, fusion vers le bas),
// annulation par trait, rendu / mipmaps / lueur comme l'atlas, fichiers
// (PNG, projet en calques) et rechargement d'une texture dans une partie.
#include "test.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>

#include <voxel/game.hpp>
#include <voxel/meshing.hpp>
#include <voxelia/studio/texture_document.hpp>

using namespace voxelia::studio;

namespace {

const Rgba RED{255, 0, 0, 255}, BLUE{0, 0, 255, 255}, CLEAR{0, 0, 0, 0};

Rgba Pixel(const voxel::Image &img, int x, int y) {
	const uint8_t *p = &img.rgba[(size_t(y) * size_t(img.width) + size_t(x)) * 4];
	return {p[0], p[1], p[2], p[3]};
}

TextureDocument Canvas(int w = 16, int h = 16) { return TextureDocument::Create(w, h, String("toile")).Unwrap(); }

int CountColor(const voxel::Image &img, const Rgba &c) {
	int n = 0;
	for (int y = 0; y < img.height; ++y)
		for (int x = 0; x < img.width; ++x)
			n += Pixel(img, x, y) == c;
	return n;
}

} // namespace

TEST(StudioTextures_BlendModes) {
	// Sur un fond opaque : chaque mode, à la formule près.
	const Rgba grey{128, 128, 128, 255}, light{200, 100, 50, 255};
	CHECK(BlendPixel(light, grey, PaintBlend::NORMAL, 1.f) == light);
	const Rgba mul = BlendPixel(light, grey, PaintBlend::MULTIPLY, 1.f);
	CHECK_EQ(int(mul[0]), 100);
	const Rgba scr = BlendPixel(light, grey, PaintBlend::SCREEN, 1.f);
	CHECK(scr[0] > 200 && scr[2] > 128);
	const Rgba ovl = BlendPixel(light, grey, PaintBlend::OVERLAY, 1.f);
	CHECK(ovl[0] > 190); // fond à 0,5 : l'incrustation rend presque la couleur
	// Opacité 0 : rien ; sur du transparent : la couleur telle quelle.
	CHECK(BlendPixel(light, grey, PaintBlend::NORMAL, 0.f) == grey);
	CHECK(BlendPixel(light, CLEAR, PaintBlend::MULTIPLY, 1.f) == light);
	CHECK(PaintBlendOf("screen").Unwrap() == PaintBlend::SCREEN);
	CHECK(PaintBlendOf("dissolve").IsNone());
}

TEST(StudioTextures_BrushSymmetrySelectionAndStrokes) {
	TextureDocument doc = Canvas();
	CHECK(TextureDocument::Create(0, 4, String("x")).IsError());
	CHECK(TextureDocument::Create(4096, 4, String("x")).IsError());
	doc.brush.color = RED;
	doc.Dab(1, 2);
	CHECK(Pixel(doc.Composite(), 1, 2) == RED);
	CHECK_EQ(doc.UndoDepth(), size_t(1));
	// Symétrie HV : quatre touches.
	doc.brush.symmetry = PaintSymmetry::HV;
	doc.Dab(3, 4);
	const voxel::Image img = doc.Composite();
	CHECK(Pixel(img, 12, 4) == RED);
	CHECK(Pixel(img, 3, 11) == RED);
	CHECK(Pixel(img, 12, 11) == RED);
	doc.brush.symmetry = PaintSymmetry::NONE;
	// Taille 3 : un disque (le centre et ses voisins directs, pas les coins).
	doc.brush.size = 3;
	doc.brush.color = BLUE;
	doc.Dab(8, 8);
	CHECK(Pixel(doc.Composite(), 9, 8) == BLUE);
	CHECK(Pixel(doc.Composite(), 9, 9) != BLUE);
	doc.brush.size = 1;
	// Un trait glissé : une seule étape d'annulation, sans trou.
	const size_t before = doc.UndoDepth();
	doc.BeginStroke();
	doc.StrokeTo(0, 15);
	doc.StrokeTo(10, 15);
	doc.StrokeTo(10, 13);
	doc.EndStroke();
	CHECK_EQ(doc.UndoDepth(), before + 1);
	for (int x = 0; x <= 10; ++x)
		CHECK(Pixel(doc.Composite(), x, 15) == BLUE);
	CHECK(Pixel(doc.Composite(), 10, 14) == BLUE);
	CHECK(doc.Undo());
	CHECK(Pixel(doc.Composite(), 5, 15) == CLEAR);
	CHECK(doc.Redo());
	// Sélection : la peinture n'en sort pas.
	doc.Select(PaintRect{0, 0, 4, 4});
	doc.brush.color = RED;
	doc.Line(0, 3, 10, 3);
	CHECK(Pixel(doc.Composite(), 3, 3) == RED);
	CHECK(Pixel(doc.Composite(), 4, 3) == CLEAR);
	doc.Select(PaintRect{});
	// Gomme.
	doc.Dab(1, 2, true);
	CHECK_EQ(int(Pixel(doc.Composite(), 1, 2)[3]), 0);
}

TEST(StudioTextures_ShapesFillPickMoveFilters) {
	TextureDocument doc = Canvas();
	doc.brush.color = RED;
	doc.Rectangle(2, 2, 9, 7, false);
	const voxel::Image outline = doc.Composite();
	CHECK(Pixel(outline, 2, 2) == RED && Pixel(outline, 9, 7) == RED);
	CHECK(Pixel(outline, 5, 5) == CLEAR); // contour seulement
	// Remplissage de l'intérieur (zone transparente bornée par le contour).
	doc.brush.color = BLUE;
	const int inside = doc.Fill(5, 5);
	CHECK_EQ(inside, 6 * 4);
	CHECK(Pixel(doc.Composite(), 5, 5) == BLUE);
	CHECK(Pixel(doc.Composite(), 0, 0) == CLEAR); // l'extérieur n'est pas touché
	// Tolérance : un rouge un peu différent est pris avec, pas sans.
	TextureDocument tol = Canvas(4, 1);
	tol.brush.color = RED;
	tol.Dab(0, 0);
	tol.brush.color = Rgba{240, 0, 0, 255};
	tol.Dab(1, 0);
	tol.brush.color = BLUE;
	CHECK_EQ(tol.Fill(0, 0, 0), 1);
	tol.Undo();
	CHECK_EQ(tol.Fill(0, 0, 20), 2);
	// Ellipse pleine : symétrique.
	TextureDocument ell = Canvas();
	ell.brush.color = RED;
	ell.Ellipse(2, 4, 12, 10, true);
	const voxel::Image e = ell.Composite();
	CHECK(Pixel(e, 7, 7) == RED);
	CHECK(Pixel(e, 2, 4) == CLEAR); // coin de la boîte : hors de l'ellipse
	for (int y = 4; y <= 10; ++y) // symétrique autour de (7, 7)
		for (int x = 2; x <= 12; ++x) {
			CHECK(Pixel(e, x, y) == Pixel(e, 14 - x, y));
			CHECK(Pixel(e, x, y) == Pixel(e, x, 14 - y));
		}
	// Pipette : couleur du pinceau et palette.
	CHECK(doc.Pick(5, 5).Unwrap() == BLUE);
	CHECK(doc.brush.color == BLUE);
	CHECK(doc.Palette().front() == BLUE);
	// Déplacement de la sélection : le contenu suit, l'ancienne place se vide.
	doc.Select(PaintRect{2, 2, 8, 6});
	doc.Move(4, 0);
	CHECK(Pixel(doc.Composite(), 6, 2) == RED);
	CHECK(Pixel(doc.Composite(), 2, 2) == CLEAR);
	CHECK_EQ(doc.Selection().x, 6);
	doc.Select(PaintRect{});
	// Flou : un point isolé s'étale ; netteté : le contraste remonte.
	TextureDocument blur = Canvas(5, 5);
	blur.brush.color = Rgba{255, 255, 255, 255};
	REQUIRE(blur.Generate("checker", 1).IsOk());
	blur.brush.color = Rgba{0, 0, 0, 255};
	blur.Dab(2, 2);
	blur.Blur();
	const int afterBlur = Pixel(blur.Composite(), 2, 2)[0];
	CHECK(afterBlur > 0);
	blur.Sharpen();
	CHECK(Pixel(blur.Composite(), 2, 2)[0] < afterBlur);
	// Motifs.
	TextureDocument gen = Canvas();
	gen.brush.color = RED;
	gen.brush.secondary = BLUE;
	REQUIRE(gen.Generate("gradient", 0).IsOk());
	CHECK(Pixel(gen.Composite(), 0, 0) == RED);
	CHECK(Pixel(gen.Composite(), 0, 15) == BLUE);
	REQUIRE(gen.Generate("noise", 7).IsOk());
	REQUIRE(gen.Generate("bricks", 7).IsOk());
	CHECK(CountColor(gen.Composite(), BLUE) > 0); // les joints
	CHECK(gen.Generate("plaid", 0).IsError());
}

TEST(StudioTextures_LayersCompositeAndMerge) {
	TextureDocument doc = Canvas(4, 4);
	doc.brush.color = Rgba{100, 100, 100, 255};
	REQUIRE(doc.Generate("checker", 0).IsOk()); // fond (deux couleurs identiques ici : gris uni)
	doc.brush.secondary = doc.brush.color;
	REQUIRE(doc.Generate("checker", 0).IsOk());
	doc.AddLayer(String("Ombre"));
	CHECK_EQ(doc.Layers().size(), size_t(2));
	CHECK_EQ(doc.Active(), 1);
	doc.brush.color = Rgba{128, 128, 128, 255};
	doc.Dab(0, 0);
	REQUIRE(doc.SetLayerBlend(1, PaintBlend::MULTIPLY).IsOk());
	CHECK_EQ(int(Pixel(doc.Composite(), 0, 0)[0]), 50); // 100 × 0,5
	REQUIRE(doc.SetLayerOpacity(1, 0.f).IsOk());
	CHECK_EQ(int(Pixel(doc.Composite(), 0, 0)[0]), 100);
	REQUIRE(doc.SetLayerOpacity(1, 1.f).IsOk());
	REQUIRE(doc.SetLayerVisible(1, false).IsOk());
	CHECK_EQ(int(Pixel(doc.Composite(), 0, 0)[0]), 100);
	REQUIRE(doc.SetLayerVisible(1, true).IsOk());
	// Ordre : l'ombre passe au fond, sous le gris opaque (normal) : invisible.
	REQUIRE(doc.MoveLayer(1, 0).IsOk());
	CHECK_EQ(doc.Active(), 0);
	CHECK_EQ(int(Pixel(doc.Composite(), 0, 0)[0]), 100);
	REQUIRE(doc.MoveLayer(0, 1).IsOk());
	REQUIRE(doc.RenameLayer(1, String("Ombre portée")).IsOk());
	REQUIRE(doc.SetLayerGroup(1, String("Effets")).IsOk());
	CHECK_EQ(doc.Layers()[1].group, String("Effets"));
	CHECK(doc.RenameLayer(1, String("  ")).IsError());
	REQUIRE(doc.DuplicateLayer(1).IsOk());
	CHECK_EQ(doc.Layers().size(), size_t(3));
	REQUIRE(doc.RemoveLayer(2).IsOk());
	// Fusion vers le bas : l'image composée ne change pas.
	const voxel::Image before = doc.Composite();
	REQUIRE(doc.MergeDown(1).IsOk());
	CHECK_EQ(doc.Layers().size(), size_t(1));
	CHECK(doc.Composite().rgba == before.rgba);
	CHECK(doc.RemoveLayer(0).IsError()); // un calque reste toujours
	CHECK(doc.MergeDown(0).IsError());
}

TEST(StudioTextures_RenderMipmapsGlowLikeTheAtlas) {
	TextureDocument doc = TextureDocument::Create(64, 64, String("haute")).Unwrap();
	doc.brush.color = Rgba{40, 120, 60, 255};
	doc.brush.secondary = Rgba{90, 200, 110, 255};
	REQUIRE(doc.Generate("noise", 3).IsOk());
	const voxel::Image r16 = doc.Rendered(16);
	CHECK_EQ(r16.width, 16);
	CHECK(r16.rgba == voxel::TextureAtlas::Resample(doc.Composite(), 16).rgba); // exactement l'atlas
	CHECK_EQ(doc.Rendered(4096).width, TextureDocument::MAX_RENDER);
	const std::vector<voxel::Image> chain = doc.MipChain(16);
	REQUIRE_EQ(chain.size(), size_t(5)); // 16 8 4 2 1
	CHECK_EQ(chain.back().width, 1);
	CHECK(doc.Glow(16).rgba == voxel::TextureAtlas::Glow(r16).rgba);
	// Un pixel isolé disparaît dans les petits niveaux : ce que l'aperçu doit montrer.
	TextureDocument dot = Canvas(16, 16);
	dot.brush.color = RED;
	dot.Dab(7, 7);
	const std::vector<voxel::Image> dots = dot.MipChain(16);
	CHECK(Pixel(dots[0], 7, 7) == RED);
	CHECK(Pixel(dots[3], 0, 0)[3] < 128);
}

TEST(StudioTextures_FilesAndProjects) {
	const std::filesystem::path dir("build/tests/tmp/voxelia/studio_textures");
	std::filesystem::remove_all(dir);
	TextureDocument doc = Canvas(8, 8);
	doc.brush.color = RED;
	doc.Rectangle(0, 0, 7, 7, true);
	doc.AddLayer(String("Motif"));
	doc.brush.color = BLUE;
	doc.Dab(3, 3);
	REQUIRE(doc.SetLayerOpacity(1, 0.5f).IsOk());
	const String png((dir / "rouge.png").string().c_str());
	REQUIRE(doc.Export(png).IsOk());
	Result<TextureDocument, String> back = TextureDocument::Import(png);
	REQUIRE(back.IsOk());
	CHECK_EQ(back.Value().Name(), String("rouge"));
	CHECK(back.Value().Composite().rgba == doc.Composite().rgba);
	REQUIRE(doc.Export(String((dir / "petit.png").string().c_str()), 4).IsOk());
	CHECK_EQ(TextureDocument::Import(String((dir / "petit.png").string().c_str())).Value().Width(), 4);
	REQUIRE(doc.Export(String((dir / "photo.jpg").string().c_str())).IsOk());
	CHECK(std::filesystem::exists(dir / "photo.jpg"));
	// Projet en calques : tout revient (opacité, nom, ordre).
	const String project((dir / "projet").string().c_str());
	REQUIRE(doc.SaveProject(project).IsOk());
	Result<TextureDocument, String> loaded = TextureDocument::LoadProject(project);
	REQUIRE(loaded.IsOk());
	REQUIRE_EQ(loaded.Value().Layers().size(), size_t(2));
	CHECK_EQ(loaded.Value().Layers()[1].name, String("Motif"));
	CHECK(std::fabs(loaded.Value().Layers()[1].opacity - 0.5f) < 1e-3f);
	CHECK(loaded.Value().Composite().rgba == doc.Composite().rgba);
	CHECK(TextureDocument::LoadProject(String((dir / "absent").string().c_str())).IsError());
}

TEST(StudioTextures_ReloadedTextureReachesTheAtlas) {
	// Une texture de mod réécrite sur le disque : ReloadTexture la fait relire,
	// et l'atlas reconstruit la prend.
	const std::filesystem::path root("build/tests/tmp/voxelia/studio_textures_reload");
	std::filesystem::remove_all(root);
	const std::filesystem::path mod = root / "mods" / "peint";
	std::filesystem::create_directories(mod / "textures");
	{
		std::ofstream(mod / "mod.json") << R"({"id": "peint", "name": "Peinture", "version": "1"})";
		std::ofstream(mod / "blocks.json") << R"([{"id": "toile", "graphics": {"texture": "peint:toile"}}])";
	}
	TextureDocument doc = Canvas(16, 16);
	doc.brush.color = RED;
	doc.Rectangle(0, 0, 15, 15, true);
	const String file(((mod / "textures") / "toile.png").string().c_str());
	REQUIRE(doc.Export(file).IsOk());
	voxel::Game game;
	voxel::GameOptions options;
	options.modDirectories = {String((root / "mods").string().c_str())};
	options.savesDirectory = String((root / "saves").string().c_str());
	(void)game.LoadMods(options);
	REQUIRE(game.Content().FindTexture("peint:toile") != nullptr);
	auto centre = [&]() {
		voxel::TextureAtlas atlas;
		atlas.Build(game.Assets(), game.Content(), 16);
		const voxel::TextureAtlas::Rect r = atlas.Lookup("peint:toile");
		return Pixel(atlas.Pixels(), int((r.u0 + r.u1) * 0.5f * float(atlas.Pixels().width)),
					 int((r.v0 + r.v1) * 0.5f * float(atlas.Pixels().height)));
	};
	CHECK(centre() == RED);
	doc.brush.color = BLUE;
	doc.Rectangle(0, 0, 15, 15, true);
	REQUIRE(doc.Export(file).IsOk());
	CHECK(centre() == RED); // encore en cache
	CHECK(game.Assets().ReloadTexture("peint:toile"));
	CHECK(centre() == BLUE);
	CHECK(!game.Assets().ReloadTexture("peint:absente"));
	// Variantes : le mailleur et l'aperçu tirent la même.
	CHECK_EQ(voxel::VariantIndex({3, 4, 5}, 1), size_t(0));
	size_t seen[3] = {0, 0, 0};
	for (int x = 0; x < 30; ++x)
		++seen[voxel::VariantIndex({x, 0, 0}, 3)];
	CHECK(seen[0] > 0 && seen[1] > 0 && seen[2] > 0);
}
