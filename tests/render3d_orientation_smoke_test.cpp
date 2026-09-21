// Tests de non-régression — orientation d'image, sens d'enroulement et
// éclairage de render3d::.
//
// Ces trois réglages sont liés et se compensaient DEUX À DEUX dans le code
// d'origine : la projection retournait Y « pour Vulkan » (un doublon du
// retournement que SDL_GPU fait déjà), et le culling était réglé sur
// CLOCKWISE pour compenser. Résultat : toute scène sortait tête en bas, et
// corriger l'un sans l'autre faisait apparaître l'intérieur des objets.
//
// Chaque test ci-dessous est une OBSERVATION de pixels, pas une relecture de
// la formule : c'est la seule façon de vérifier une convention graphique.
#define USE_TEST

#include "core/test.hpp"
#include "render3d/canvas.hpp"
#include "render3d/offscreen.hpp"
#include "render3d/shape.hpp"
#include "sdl3/sdl3.hpp"

using namespace sdl3;
using namespace render3d;

namespace {

constexpr GpuTextureFormat COLOR_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr GpuTextureFormat DEPTH_FORMAT = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
constexpr uint32_t WIDTH = 96;
constexpr uint32_t HEIGHT = 96;

/// Fenêtre + Canvas + cible hors écran, montés une fois par test.
struct Harness {
	Option<Window> window = NONE;
	Option<Canvas> canvas = NONE;
	OffscreenTarget target;
	std::vector<uint8_t> pixels;

	[[nodiscard]] bool Setup() {
		auto windowResult = Window::Create(String("render3d_orientation"), int(WIDTH), int(HEIGHT), 0);
		if (!windowResult)
			return false;
		window = Some(std::move(windowResult.Value()));
		auto canvasResult = Canvas::Create(window.Value(), int(WIDTH), int(HEIGHT));
		if (!canvasResult)
			return false;
		canvas = Some(std::move(canvasResult.Value()));
		canvas.Value().SetBackgroundColor(Color{0, 0, 0, 255});
		return target.EnsureSize(canvas.Value().Device(), WIDTH, HEIGHT, COLOR_FORMAT, DEPTH_FORMAT).IsOk();
	}

	[[nodiscard]] bool Render(Object3D &root, const render3d::Camera &camera) {
		return RenderObjectToTexture(canvas.Value(), root, camera, target, pixels).IsOk();
	}

	/// Composante moyenne d'un canal sur une bande horizontale de l'image.
	[[nodiscard]] double AverageChannel(int channel, uint32_t firstRow, uint32_t lastRow) const {
		long sum = 0, count = 0;
		for (uint32_t y = firstRow; y < lastRow && y < HEIGHT; ++y)
			for (uint32_t x = 0; x < WIDTH; ++x) {
				sum += pixels[(size_t(y) * WIDTH + x) * 4 + size_t(channel)];
				++count;
			}
		return count ? double(sum) / double(count) : 0.0;
	}

	[[nodiscard]] long LitPixels() const {
		long count = 0;
		for (size_t i = 0; i + 3 < pixels.size(); i += 4)
			if (pixels[i] > 40 || pixels[i + 1] > 40 || pixels[i + 2] > 40)
				++count;
		return count;
	}
};

} // namespace

TEST(Render3dOrientation, WorldUpLandsInTheTopOfTheImage) {
	Harness harness;
	ASSERT_TRUE(harness.Setup());

	// Un cube ROUGE en haut du monde, un cube BLEU en bas, caméra de face.
	Object3D root;
	auto &high = static_cast<Shape &>(root.Add(
		std::make_unique<Shape>(Mesh::Box(1.5f, 1.5f, 1.5f), Material::Unlit(Color{255, 0, 0, 255}))));
	high.SetPosition({0.f, 2.f, 0.f});
	auto &low = static_cast<Shape &>(root.Add(
		std::make_unique<Shape>(Mesh::Box(1.5f, 1.5f, 1.5f), Material::Unlit(Color{0, 0, 255, 255}))));
	low.SetPosition({0.f, -2.f, 0.f});

	render3d::Camera camera;
	camera.position = {0.f, 0.f, -8.f};
	camera.target = {0.f, 0.f, 0.f};
	ASSERT_TRUE(harness.Render(root, camera));

	// Le rouge (monde EN HAUT) doit dominer la moitié HAUTE du tampon, et le
	// bleu la moitié basse. C'est la définition même de « l'image n'est pas
	// retournée ».
	double redTop = harness.AverageChannel(0, 0, HEIGHT / 2);
	double blueTop = harness.AverageChannel(2, 0, HEIGHT / 2);
	double redBottom = harness.AverageChannel(0, HEIGHT / 2, HEIGHT);
	double blueBottom = harness.AverageChannel(2, HEIGHT / 2, HEIGHT);

	EXPECT_TRUE(redTop > blueTop);
	EXPECT_TRUE(blueBottom > redBottom);
}

TEST(Render3dOrientation, BackFacesAreCulledAndFrontFacesKept) {
	Harness harness;
	ASSERT_TRUE(harness.Setup());

	// `Mesh::Plane` est une surface À FACE UNIQUE, normale +Y : visible de
	// dessus, éliminée vue de dessous. C'est le test direct du sens
	// d'enroulement retenu par le back-face culling.
	render3d::Camera camera;
	camera.up = {0.f, 0.f, 1.f}; // caméra à la verticale : `up` ne peut pas être ±Y

	Object3D fromAboveRoot;
	auto &planeAbove = static_cast<Shape &>(fromAboveRoot.Add(
		std::make_unique<Shape>(Mesh::Plane(6.f, 6.f), Material::Unlit(Color{255, 255, 255, 255}))));
	planeAbove.Materials()[0].doubleSided = false;
	camera.position = {0.f, 6.f, 0.001f};
	camera.target = {0.f, 0.f, 0.f};
	ASSERT_TRUE(harness.Render(fromAboveRoot, camera));
	long visibleFromFront = harness.LitPixels();

	Object3D fromBelowRoot;
	auto &planeBelow = static_cast<Shape &>(fromBelowRoot.Add(
		std::make_unique<Shape>(Mesh::Plane(6.f, 6.f), Material::Unlit(Color{255, 255, 255, 255}))));
	planeBelow.Materials()[0].doubleSided = false;
	camera.position = {0.f, -6.f, 0.001f};
	ASSERT_TRUE(harness.Render(fromBelowRoot, camera));
	long visibleFromBack = harness.LitPixels();

	EXPECT_TRUE(visibleFromFront > 500); // la face avant se voit
	EXPECT_TRUE(visibleFromBack == 0);   // la face arrière est éliminée
}

TEST(Render3dOrientation, DoubleSidedMaterialDisablesCulling) {
	// Contrôle du contrôle : avec `doubleSided`, la même surface DOIT se voir
	// des deux côtés — sinon le test précédent pourrait passer pour une
	// mauvaise raison (surface simplement hors champ).
	Harness harness;
	ASSERT_TRUE(harness.Setup());

	render3d::Camera camera;
	camera.up = {0.f, 0.f, 1.f};
	camera.position = {0.f, -6.f, 0.001f};
	camera.target = {0.f, 0.f, 0.f};

	Object3D root;
	auto &plane = static_cast<Shape &>(
		root.Add(std::make_unique<Shape>(Mesh::Plane(6.f, 6.f), Material::Unlit(Color{255, 255, 255, 255}))));
	plane.Materials()[0].doubleSided = true;
	ASSERT_TRUE(harness.Render(root, camera));
	EXPECT_TRUE(harness.LitPixels() > 500);
}

TEST(Render3dOrientation, SurfaceFacingTheSunIsBrighterThanTheOneInShadow) {
	// Normales et direction de lumière : la face tournée VERS le soleil doit
	// être la plus claire. Une inversion de normale (ou du signe de la
	// direction) échangerait les deux.
	Harness harness;
	ASSERT_TRUE(harness.Setup());

	DirectionalLight sun;
	sun.direction = {0.f, -1.f, 0.f}; // rayons vers le bas
	sun.color = Color::WHITE();
	sun.intensity = 1.f;
	AmbientLight ambient;
	ambient.color = Color{0, 0, 0}; // aucune ambiante : seul le diffus compte
	harness.canvas.Value().SetLighting(sun, ambient);

	render3d::Camera camera;
	camera.up = {0.f, 0.f, 1.f};
	camera.target = {0.f, 0.f, 0.f};

	Object3D topRoot;
	topRoot.Add(std::make_unique<Shape>(Mesh::Box(20.f, 0.4f, 20.f), Material::Plastic(Color{255, 255, 255, 255})));
	camera.position = {0.f, 8.f, 0.001f};
	ASSERT_TRUE(harness.Render(topRoot, camera));
	double litFace = harness.AverageChannel(0, 0, HEIGHT);

	Object3D bottomRoot;
	bottomRoot.Add(
		std::make_unique<Shape>(Mesh::Box(20.f, 0.4f, 20.f), Material::Plastic(Color{255, 255, 255, 255})));
	camera.position = {0.f, -8.f, 0.001f};
	ASSERT_TRUE(harness.Render(bottomRoot, camera));
	double shadowedFace = harness.AverageChannel(0, 0, HEIGHT);

	EXPECT_TRUE(litFace > shadowedFace + 40.0);
}

TEST(Render3dOrientation, FrustumCullingNeverRemovesSomethingVisible) {
	// Le culling de frustum doit être CONSERVATEUR : activé ou non, une scène
	// entièrement dans le champ donne exactement la même image.
	Harness harness;
	ASSERT_TRUE(harness.Setup());

	render3d::Camera camera;
	camera.position = {0.f, 3.f, -10.f};
	camera.target = {0.f, 0.f, 0.f};

	auto build = [](Object3D &root) {
		for (int i = -2; i <= 2; ++i) {
			auto &cube = static_cast<Shape &>(root.Add(
				std::make_unique<Shape>(Mesh::Box(1.f, 1.f, 1.f), Material::Unlit(Color{255, 200, 50, 255}))));
			cube.SetPosition({float(i) * 1.8f, 0.f, 0.f});
		}
	};

	Object3D culledRoot;
	build(culledRoot);
	harness.canvas.Value().SetFrustumCullingEnabled(true);
	ASSERT_TRUE(harness.Render(culledRoot, camera));
	long withCulling = harness.LitPixels();

	Object3D fullRoot;
	build(fullRoot);
	harness.canvas.Value().SetFrustumCullingEnabled(false);
	ASSERT_TRUE(harness.Render(fullRoot, camera));
	long withoutCulling = harness.LitPixels();

	EXPECT_TRUE(withoutCulling > 0);
	EXPECT_EQ(withCulling, withoutCulling);
}

TEST(Render3dOrientation, FrustumCullingRemovesWhatIsBehindTheCamera) {
	// ... et il doit quand même SERVIR à quelque chose : un objet derrière la
	// caméra ne doit pas être soumis au GPU.
	Harness harness;
	ASSERT_TRUE(harness.Setup());

	render3d::Camera camera;
	camera.position = {0.f, 0.f, -10.f};
	camera.target = {0.f, 0.f, 0.f};

	Object3D root;
	auto &visible = static_cast<Shape &>(
		root.Add(std::make_unique<Shape>(Mesh::Box(1.f, 1.f, 1.f), Material::Unlit(Color{255, 255, 255, 255}))));
	visible.SetPosition({0.f, 0.f, 0.f});
	for (int i = 0; i < 6; ++i) {
		auto &behind = static_cast<Shape &>(root.Add(
			std::make_unique<Shape>(Mesh::Box(1.f, 1.f, 1.f), Material::Unlit(Color{255, 255, 255, 255}))));
		behind.SetPosition({float(i), 0.f, -40.f}); // très en arrière de la caméra
	}

	harness.canvas.Value().SetFrustumCullingEnabled(true);
	ASSERT_TRUE(harness.Render(root, camera));
	EXPECT_TRUE(harness.canvas.Value().CulledDrawCount() >= 6);
	EXPECT_TRUE(harness.canvas.Value().SubmittedDrawCount() >= 1);
}

int main() { return RUN_ALL_TESTS(); }
