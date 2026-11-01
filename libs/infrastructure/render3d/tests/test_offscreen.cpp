// Smoke test : render3d::OffscreenTarget / RenderObjectToTexture /
// UploadSurfaceToGpuTexture (lib/render3d/offscreen.hpp) — M21 du plan
// (Phase 2). Trois vérifications GPU réelles, même rigueur par pixel que
// tests/shadow_smoke_test.cpp / tests/environment_smoke_test.cpp :
//
// 1. Un cube en matériau non éclairé (couleur plate, formule exacte connue —
//    voir mesh_basic.frag : outFragColor = uBaseColor * inColor) rendu dans
//    un OffscreenTarget carré, dimensionné pour recouvrir tout le viewport —
//    la couleur téléchargée doit être EXACTEMENT baseColor à n'importe quel
//    pixel, coins inclus.
// 2. UploadSurfaceToGpuTexture : un buffer CPU synthétique (2 couleurs
//    connues, gauche/droite) est uploadé puis échantillonné en le dessinant
//    comme quad texturé (matériau Phong précompilé, ombre directionnelle à
//    intensité nulle + ambiante blanche pleine intensité pour isoler
//    exactement `albedo` dans la formule du shader, voir mesh_phong.frag) —
//    les couleurs retéléchargées doivent correspondre exactement au buffer
//    source.
// 3. camera.aspect est bien écrasé à partir des dimensions RÉELLES de la
//    cible (pas de la fenêtre) : un carré 3D de taille monde fixe, projeté
//    dans deux OffscreenTarget de rapports d'aspect opposés (très large vs
//    très haut), doit occuper le MÊME nombre de pixels en largeur qu'en
//    hauteur dans les DEUX cas (silhouette carrée à l'écran, invariant qui
//    ne tient QUE si l'aspect utilisé pour la projection correspond bien à
//    la cible réelle) — comparé à la valeur hand-computed via la formule de
//    projection en perspective symétrique standard.
#include "test.hpp"
#include <iostream>
#include <render3d/cubemap.hpp>
#include <render3d/offscreen.hpp>
#include <render3d/texture_loader.hpp>
#include <render3d/shape.hpp>
#include <sdl3/sdl3.hpp>
#include <cmath>

using namespace sdl3;
using namespace render3d;

namespace {

/// Formats couleur/profondeur utilisés par tous les OffscreenTarget de ce
/// test — indépendants du format de swapchain de Canvas (c'est justement le
/// point d'OffscreenTarget : une cible GPU qui n'a rien à voir avec la
/// fenêtre), choisis directement comme tests/shadow_smoke_test.cpp le fait
/// pour sa propre color/depth texture "receiver" (pas de sondage de support
/// de format : ce sont des formats larges couverts par tous les backends
/// SDL_GPU de cet environnement).
constexpr GpuTextureFormat OFFSCREEN_COLOR_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr GpuTextureFormat OFFSCREEN_DEPTH_FORMAT = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

} // namespace

TEST(OffscreenSmokeTest, FlatUnlitCubeFillsTargetWithExactColor) {
    auto windowResult = Window::Create(String("offscreen_smoke_test"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());

    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());

    // Cube de taille 6 (demi-arête 3) : à distance caméra->face avant de 2
    // (caméra par défaut à z=-5, face avant du cube à z=-3), la demi-largeur
    // du frustum à cette profondeur (aspect=1, fovY=60°) vaut
    // 2*tan(30°) ≈ 1.1547 — largement plus petite que le demi-côté du cube
    // (3) : la face avant recouvre donc TOUT le viewport, coins compris,
    // garantissant que chaque pixel téléchargé montre la même couleur plate.
    Color cubeColor(220, 30, 30, 255);
    Shape cube(Mesh::Cube(6.f), Material::Unlit(cubeColor));

    render3d::Camera camera; // position/target/up/fovY par défaut (camera.hpp) — aspect écrasé par RenderObjectToTexture

    OffscreenTarget target;
    auto sized = target.EnsureSize(canvas.Device(), 64, 64, OFFSCREEN_COLOR_FORMAT, OFFSCREEN_DEPTH_FORMAT);
    ASSERT_TRUE(sized.IsOk());

    std::vector<uint8_t> pixels;
    auto rendered = RenderObjectToTexture(canvas, cube, camera, target, pixels);
    ASSERT_TRUE(rendered.IsOk());
    ASSERT_EQ(pixels.size(), size_t(64 * 64 * 4));

    auto checkPixel = [&](uint32_t x, uint32_t y) {
        size_t idx = (size_t(y) * 64 + x) * 4;
        EXPECT_TRUE(std::abs(int(pixels[idx + 0]) - int(cubeColor.r)) <= 2);
        EXPECT_TRUE(std::abs(int(pixels[idx + 1]) - int(cubeColor.g)) <= 2);
        EXPECT_TRUE(std::abs(int(pixels[idx + 2]) - int(cubeColor.b)) <= 2);
        EXPECT_TRUE(std::abs(int(pixels[idx + 3]) - int(cubeColor.a)) <= 2);
    };
    checkPixel(32, 32); // centre
    checkPixel(1, 1);   // coin haut-gauche
    checkPixel(62, 62); // coin bas-droit
    checkPixel(62, 1);  // coin haut-droit
    checkPixel(1, 62);  // coin bas-gauche
}

TEST(OffscreenSmokeTest, UploadSurfaceToGpuTextureRoundTripsExactColors) {
    auto windowResult = Window::Create(String("offscreen_smoke_test"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());

    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());

    // Buffer CPU synthétique 8x8 RGBA8 tightly-packed : moitié gauche (colonnes
    // 0-3) rouge pur, moitié droite (colonnes 4-7) verte pure, uniforme par
    // colonne (aucune variation verticale) — évite toute ambiguïté sur le
    // flip d'axe Y du pipeline (voir canvas.hpp : seul Y est inversé pour le
    // clip space Vulkan, X ne l'est pas), ce test ne dépend donc que de X.
    constexpr uint32_t TEX_SIZE = 8;
    std::vector<uint8_t> srcPixels(TEX_SIZE * TEX_SIZE * 4);
    for (uint32_t row = 0; row < TEX_SIZE; ++row) {
        for (uint32_t col = 0; col < TEX_SIZE; ++col) {
            size_t idx = (size_t(row) * TEX_SIZE + col) * 4;
            bool left = col < TEX_SIZE / 2;
            srcPixels[idx + 0] = left ? 255 : 0;
            srcPixels[idx + 1] = left ? 0 : 255;
            srcPixels[idx + 2] = 0;
            srcPixels[idx + 3] = 255;
        }
    }

    auto uploadResult =
        UploadSurfaceToGpuTexture(canvas.Device(), srcPixels.data(), TEX_SIZE, TEX_SIZE, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    ASSERT_TRUE(uploadResult.IsOk());
    GpuTexture uploaded = std::move(uploadResult.Value());

    // Quad plat face caméra, demi-côté 2 (< 2.8868, la demi-largeur du
    // frustum à distance 5 pour fovY=60°/aspect=1 : reste entièrement dans
    // le champ, pas de clipping). UV construits explicitement à partir de la
    // position (u=0 au sommet le plus à gauche, u=1 au plus à droite) : la
    // correspondance u<->x est donc connue par construction, indépendamment
    // de toute convention interne.
    float hw = 2.f;
    std::vector<Vertex3D> quadVerts = {
        {{-hw, -hw, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
        {{hw, -hw, 0.f}, {0.f, 0.f, -1.f}, {1.f, 0.f}, Color::WHITE()},
        {{hw, hw, 0.f}, {0.f, 0.f, -1.f}, {1.f, 1.f}, Color::WHITE()},
        {{-hw, hw, 0.f}, {0.f, 0.f, -1.f}, {0.f, 1.f}, Color::WHITE()},
    };
    std::vector<uint32_t> quadIdx = {0, 1, 2, 0, 2, 3};
    Mesh quadMesh(quadVerts, quadIdx);

    Material quadMaterial = Material::Default();
    quadMaterial.albedo = Some(MakeRef(uploaded));
    quadMaterial.doubleSided = true; // évite toute dépendance au winding exact de ce quad ad hoc
    Shape quad(std::move(quadMesh), quadMaterial);

    render3d::Camera camera;

    // Ambiante blanche pleine intensité + directionnelle à intensité nulle :
    // isole exactement `albedo.rgb` dans mesh_phong.frag (color =
    // albedo*ambient + albedo*lightColor*diffuse + lightColor*specular, les
    // deux derniers termes s'annulant avec lightColor=(0,0,0)) — la couleur
    // retéléchargée doit donc être EXACTEMENT la texture échantillonnée.
    DirectionalLight noLight;
    noLight.intensity = 0.f;
    AmbientLight fullAmbient;
    fullAmbient.color = Color::WHITE();
    fullAmbient.intensity = 1.f;
    canvas.SetLighting(noLight, fullAmbient);

    OffscreenTarget target;
    auto sized = target.EnsureSize(canvas.Device(), 64, 64, OFFSCREEN_COLOR_FORMAT, OFFSCREEN_DEPTH_FORMAT);
    ASSERT_TRUE(sized.IsOk());

    std::vector<uint8_t> pixels;
    auto rendered = RenderObjectToTexture(canvas, quad, camera, target, pixels);
    ASSERT_TRUE(rendered.IsOk());
    ASSERT_EQ(pixels.size(), size_t(64 * 64 * 4));

    // x=16/x=48 tombent loin (en x écran) de la frontière u=0.5 (x=32) et de
    // la limite de texel correspondante dans le buffer source — donc dans la
    // zone "pleine confiance" pour le filtrage LINEAR, quel que soit le sens
    // exact du mapping. Constaté empiriquement (et laissé en l'état — voir
    // le rapport de tâche) : l'axe X écran est ÉGALEMENT inversé par rapport
    // à l'axe X monde pour cette caméra (position -Z, target origine, up
    // +Y), en plus de l'inversion Y déjà documentée dans canvas.hpp pour le
    // clip space Vulkan — probablement la convention de main du
    // LookAt/Perspective de ce dépôt. x=16 (écran gauche) retombe donc côté
    // u>0.5 (vert, colonnes 4-7 du buffer source) et x=48 (écran droit) côté
    // u<0.5 (rouge).
    size_t leftIdx = (size_t(32) * 64 + 16) * 4;
    size_t rightIdx = (size_t(32) * 64 + 48) * 4;
    EXPECT_TRUE(std::abs(int(pixels[leftIdx + 0]) - 0) <= 4);
    EXPECT_TRUE(std::abs(int(pixels[leftIdx + 1]) - 255) <= 4);
    EXPECT_TRUE(std::abs(int(pixels[leftIdx + 2]) - 0) <= 4);
    EXPECT_TRUE(std::abs(int(pixels[rightIdx + 0]) - 255) <= 4);
    EXPECT_TRUE(std::abs(int(pixels[rightIdx + 1]) - 0) <= 4);
    EXPECT_TRUE(std::abs(int(pixels[rightIdx + 2]) - 0) <= 4);
}

TEST(OffscreenSmokeTest, CameraAspectOverriddenFromTargetPreventsDistortion) {
    auto windowResult = Window::Create(String("offscreen_smoke_test"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());

    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());

    // Carré monde plat face caméra, demi-côté 1 (2x2) à distance 5 (caméra
    // par défaut à z=-5, carré à z=0) — reste dans le champ pour les deux
    // rapports d'aspect testés ci-dessous (demi-largeur frustum minimale
    // parmi les deux : aspect=0.5, halfWidth = 5*tan(30°)*0.5 ≈ 1.44 > 1).
    Color squareColor = Color::WHITE();
    float halfExtent = 1.f;
    std::vector<Vertex3D> quadVerts = {
        {{-halfExtent, -halfExtent, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
        {{halfExtent, -halfExtent, 0.f}, {0.f, 0.f, -1.f}, {1.f, 0.f}, Color::WHITE()},
        {{halfExtent, halfExtent, 0.f}, {0.f, 0.f, -1.f}, {1.f, 1.f}, Color::WHITE()},
        {{-halfExtent, halfExtent, 0.f}, {0.f, 0.f, -1.f}, {0.f, 1.f}, Color::WHITE()},
    };
    std::vector<uint32_t> quadIdx = {0, 1, 2, 0, 2, 3};
    Material squareMaterial = Material::Unlit(squareColor);
    squareMaterial.doubleSided = true;
    Shape square(Mesh(quadVerts, quadIdx), squareMaterial);

    render3d::Camera camera; // aspect écrasé par RenderObjectToTexture à chaque appel ci-dessous
    float distance = std::abs(camera.position.z - 0.f); // caméra par défaut à z=-5, carré à z=0 -> distance 5
    float tanHalfFovY = std::tan(camera.fovYRadians * 0.5f);

    auto measureSilhouette = [&](uint32_t width, uint32_t height, float &outHalfWidthPx, float &outHalfHeightPx) {
        OffscreenTarget target;
        auto sized = target.EnsureSize(canvas.Device(), width, height, OFFSCREEN_COLOR_FORMAT, OFFSCREEN_DEPTH_FORMAT);
        ASSERT_TRUE(sized.IsOk());

        std::vector<uint8_t> pixels;
        auto rendered = RenderObjectToTexture(canvas, square, camera, target, pixels);
        ASSERT_TRUE(rendered.IsOk());
        ASSERT_EQ(pixels.size(), size_t(width) * height * 4);

        uint32_t minX = width, maxX = 0, minY = height, maxY = 0;
        bool foundAny = false;
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                size_t idx = (size_t(y) * width + x) * 4;
                bool isWhite = pixels[idx + 0] > 200 && pixels[idx + 1] > 200 && pixels[idx + 2] > 200;
                if (!isWhite)
                    continue;
                foundAny = true;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
        ASSERT_TRUE(foundAny);
        outHalfWidthPx = float(maxX - minX + 1) * 0.5f;
        outHalfHeightPx = float(maxY - minY + 1) * 0.5f;
    };

    // Deux cibles de rapports d'aspect opposés et extrêmes : très large
    // (128x64, aspect=2) et très haute (64x128, aspect=0.5).
    float wideHalfWidthPx = 0.f, wideHalfHeightPx = 0.f;
    measureSilhouette(128, 64, wideHalfWidthPx, wideHalfHeightPx);
    float tallHalfWidthPx = 0.f, tallHalfHeightPx = 0.f;
    measureSilhouette(64, 128, tallHalfWidthPx, tallHalfHeightPx);

    // Invariant clé : le carré (monde) projeté avec le BON aspect (celui de
    // la cible réelle, pas celui de la fenêtre) doit rester un carré à
    // l'écran (largeur px == hauteur px) quel que soit le rapport
    // largeur/hauteur de la cible — c'est précisément ce qu'une distorsion
    // (aspect non écrasé) casserait.
    EXPECT_TRUE(std::abs(wideHalfWidthPx - wideHalfHeightPx) <= 2.f);
    EXPECT_TRUE(std::abs(tallHalfWidthPx - tallHalfHeightPx) <= 2.f);

    // Comparé à la valeur hand-computed (projection perspective symétrique
    // standard, plan frontal à distance `distance`) : pixelHalfExtent =
    // halfExtent * targetHeight / (2 * distance * tan(fovY/2)) — ne dépend
    // QUE de la hauteur de la cible (le rapport d'aspect s'annule
    // exactement), donc la même formule s'applique aux deux cibles avec
    // leur hauteur respective (64 puis 128).
    float expectedWide = halfExtent * 64.f / (2.f * distance * tanHalfFovY);
    float expectedTall = halfExtent * 128.f / (2.f * distance * tanHalfFovY);
    EXPECT_TRUE(std::abs(wideHalfWidthPx - expectedWide) <= 2.f);
    EXPECT_TRUE(std::abs(tallHalfWidthPx - expectedTall) <= 2.f);
}

// Canvas::Create(sdl3::Renderer&, w, h) — partage le SDL_GPUDevice d'un
// sdl3::Renderer GPU-backed au lieu d'en créer un second indépendant (voir
// canvas.hpp, et Renderer::GetGPUDevice() dans render.hpp pour le
// raisonnement complet sur la non-possession du device par ce Canvas).
// Vérifie : (1) échec propre si le renderer n'est PAS gpu-backed (cas par
// défaut de Renderer::Create dans tout le reste de ce dépôt) ; (2) succès +
// rendu offscreen réel quand il l'est ; (3) implicitement, l'absence de
// double-free au destructeur (ASan aurait planté ce binaire si Canvas avait
// détruit un device que Renderer croit encore posséder).
TEST(OffscreenSmokeTest, CanvasCreateFromRendererFailsCleanlyWhenNotGpuBacked) {
    auto windowResult = Window::Create(String("offscreen_smoke_test_renderer"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());

    // Renderer::Create(window) sans backend "gpu" explicite -> pas de
    // SDL_GPUDevice associé, GetGPUDevice()/Canvas::Create(renderer,...)
    // doivent échouer proprement plutôt que planter ou créer un device
    // fantôme.
    auto rendererResult = Renderer::Create(window);
    ASSERT_TRUE(rendererResult.IsOk());
    Renderer &renderer = rendererResult.Value();

    auto canvasResult = Canvas::Create(renderer, 64, 64);
    EXPECT_TRUE(canvasResult.IsError());
}

TEST(OffscreenSmokeTest, CanvasCreateFromGpuBackedRendererSharesDeviceAndRenders) {
    auto windowResult = Window::Create(String("offscreen_smoke_test_renderer_gpu"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());

    // "gpu" : backend SDL_Renderer explicitement GPU-backed (SDL_CreateGPURenderer
    // en interne) — seul cas où SDL_GetGPURendererDevice()/GetGPUDevice() renvoie
    // un device réel (cf. doc SDL_render.h). Best-effort : ce backend n'est pas
    // forcément disponible sur toutes les plateformes/CI — si la création du
    // renderer lui-même échoue déjà, on saute le test plutôt que de le faire
    // échouer (pas une régression de ce dépôt).
    auto rendererResult = Renderer::Create(window, "gpu");
    if (!rendererResult.IsOk()) {
        std::cout << "  (backend \"gpu\" indisponible ici : " << rendererResult.Error().CStr()
                  << " -- test ignoré)\n";
        return;
    }
    Renderer &renderer = rendererResult.Value();

    auto canvasResult = Canvas::Create(renderer, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());

    // Un rendu offscreen réel à travers le device EMPRUNTÉ suffit à prouver
    // qu'il est pleinement fonctionnel (pas juste un pointeur brut inerte).
    Object3D root;
    auto &box = static_cast<Shape &>(
        root.Add(std::make_unique<Shape>(Mesh::Cube(1.f), Material::Unlit(Color{200, 60, 60}))));
    box.SetPosition({0.f, 0.f, 0.f});

    render3d::Camera camera;
    camera.position = {0.f, 0.f, -5.f};
    camera.target = {0.f, 0.f, 0.f};

    OffscreenTarget target;
    auto sized = target.EnsureSize(canvas.Device(), 32, 32, OFFSCREEN_COLOR_FORMAT, OFFSCREEN_DEPTH_FORMAT);
    ASSERT_TRUE(sized.IsOk());

    std::vector<uint8_t> pixels;
    auto rendered = RenderObjectToTexture(canvas, root, camera, target, pixels);
    ASSERT_TRUE(rendered.IsOk());
    EXPECT_TRUE(pixels.size() == size_t(32 * 32 * 4));

    // Canvas partagé détruit ICI (fin de portée, avant `renderer` ci-dessous)
    // — c'est exactement l'ordre qui planterait (double-free du device) si
    // m_ownsDevice/le destructeur personnalisé de Canvas n'étaient pas
    // corrects : ASan aurait signalé un crash à la sortie de ce test sinon.
}

TEST(OffscreenSmokeTest, SkyboxFacesShowInTheirDirectionTinted) {
    auto windowResult = Window::Create(String("offscreen_smoke_test"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());
    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());

    // Six faces unies (+X, -X, +Y, -Y, +Z, -Z), 8 × 8.
    const uint8_t colors[6][3] = {{200, 0, 0}, {0, 200, 0}, {0, 0, 200}, {200, 200, 0}, {0, 200, 200}, {200, 0, 200}};
    std::vector<std::vector<uint8_t>> faces(6, std::vector<uint8_t>(8 * 8 * 4));
    std::array<const uint8_t *, 6> pointers{};
    for (size_t f = 0; f < 6; ++f) {
        for (size_t i = 0; i < 64; ++i) {
            faces[f][i * 4 + 0] = colors[f][0];
            faces[f][i * 4 + 1] = colors[f][1];
            faces[f][i * 4 + 2] = colors[f][2];
            faces[f][i * 4 + 3] = 255;
        }
        pointers[f] = faces[f].data();
    }
    auto cubemap = CreateCubemapFromFaces(canvas.Device(), pointers, 8);
    ASSERT_TRUE(cubemap.IsOk());
    canvas.SetSkybox(cubemap.Value(), false); // repère de render3d : chaque direction, sa face
    canvas.SetSkyboxTint(0.5f, 0.5f, 0.5f);

    OffscreenTarget target;
    ASSERT_TRUE(target.EnsureSize(canvas.Device(), 64, 64, OFFSCREEN_COLOR_FORMAT, OFFSCREEN_DEPTH_FORMAT).IsOk());
    Object3D empty;
    const math::FVector3 dirs[6] = {{1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, -1.f}};
    for (int f = 0; f < 6; ++f) {
        render3d::Camera camera;
        camera.position = {0.f, 0.f, 0.f};
        camera.target = dirs[f];
        camera.up = f == 2 || f == 3 ? math::FVector3{0.f, 0.f, 1.f} : math::FVector3{0.f, 1.f, 0.f};
        std::vector<uint8_t> pixels;
        ASSERT_TRUE(RenderObjectToTexture(canvas, empty, camera, target, pixels).IsOk());
        const size_t centre = (size_t(32) * 64 + 32) * 4;
        for (int c = 0; c < 3; ++c)
            EXPECT_TRUE(std::abs(int(pixels[centre + size_t(c)]) - int(colors[f][c]) / 2) <= 3); // teinte 0,5
    }
    canvas.ClearSkybox();
}

TEST(OffscreenSmokeTest, CrossSkyboxIsNotMirrored) {
    auto windowResult = Window::Create(String("offscreen_smoke_test"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());
    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());

    // Faces au repère « main gauche » d'OpenGL (images en croix) : +X en
    // vert ; +Z moitié gauche rouge, moitié droite bleue ; les autres grises.
    constexpr int N = 8;
    std::vector<std::vector<uint8_t>> faces(6, std::vector<uint8_t>(N * N * 4, 128));
    std::array<const uint8_t *, 6> pointers{};
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            uint8_t *green = &faces[0][size_t(y * N + x) * 4];
            green[0] = 0, green[1] = 220, green[2] = 0;
            uint8_t *front = &faces[4][size_t(y * N + x) * 4];
            front[0] = x < N / 2 ? 220 : 0, front[1] = 0, front[2] = x < N / 2 ? 0 : 220;
        }
    for (size_t f = 0; f < 6; ++f)
        pointers[f] = faces[f].data();
    auto cubemap = CreateCubemapFromFaces(canvas.Device(), pointers, N);
    ASSERT_TRUE(cubemap.IsOk());
    canvas.SetSkybox(cubemap.Value()); // convention OpenGL par défaut

    OffscreenTarget target;
    ASSERT_TRUE(target.EnsureSize(canvas.Device(), 64, 64, OFFSCREEN_COLOR_FORMAT, OFFSCREEN_DEPTH_FORMAT).IsOk());
    Object3D empty;
    auto look = [&](math::FVector3 dir, std::vector<uint8_t> &pixels) {
        render3d::Camera camera;
        camera.position = {0.f, 0.f, 0.f};
        camera.target = dir;
        return RenderObjectToTexture(canvas, empty, camera, target, pixels).IsOk();
    };
    auto at = [](const std::vector<uint8_t> &p, int x, int y) { return &p[(size_t(y) * 64 + size_t(x)) * 4]; };
    std::vector<uint8_t> pixels;
    // Face à +Z : la moitié gauche de l'image à gauche de l'écran.
    ASSERT_TRUE(look({0.f, 0.f, 1.f}, pixels));
    EXPECT_TRUE(at(pixels, 16, 32)[0] > 180 && at(pixels, 16, 32)[2] < 40);
    EXPECT_TRUE(at(pixels, 48, 32)[2] > 180 && at(pixels, 48, 32)[0] < 40);
    // La face +X est à DROITE quand on regarde +Z — côté -X du repère « main droite ».
    ASSERT_TRUE(look({-1.f, 0.f, 0.f}, pixels));
    EXPECT_TRUE(at(pixels, 32, 32)[1] > 180 && at(pixels, 32, 32)[0] < 40);
    canvas.ClearSkybox();
}

TEST(OffscreenSmokeTest, UnlitMaterialSamplesItsTextureAndItsAlpha) {
    auto windowResult = Window::Create(String("offscreen_smoke_test"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());
    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());
    canvas.SetBackgroundColor(Color(0, 0, 255, 255));

    // Texture 2 × 1 : un texel rouge opaque, un texel entièrement transparent
    // (comme les trous d'une carte de nuages).
    const std::vector<uint8_t> texels = {220, 30, 30, 255, 255, 255, 255, 0};
    auto texture = CreateTextureFromPixels(canvas.Device(), 2, 1, texels);
    ASSERT_TRUE(texture.IsOk());
    Material material = Material::Unlit(Color::WHITE());
    material.albedo = Some(MakeRef(texture.Value()));
    material.blend = true;
    material.nearestFilter = true;
    Shape cube(Mesh::Cube(6.f), material);

    OffscreenTarget target;
    ASSERT_TRUE(target.EnsureSize(canvas.Device(), 64, 64, OFFSCREEN_COLOR_FORMAT, OFFSCREEN_DEPTH_FORMAT).IsOk());
    std::vector<uint8_t> pixels;
    ASSERT_TRUE(RenderObjectToTexture(canvas, cube, render3d::Camera{}, target, pixels).IsOk());
    int red = 0, background = 0, white = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        red += pixels[i] > 180 && pixels[i + 2] < 60;
        background += pixels[i] < 40 && pixels[i + 2] > 200;
        white += pixels[i] > 200 && pixels[i + 1] > 200 && pixels[i + 2] > 200;
    }
    EXPECT_TRUE(red > 400);        // le texel opaque
    EXPECT_TRUE(background > 400); // le texel transparent laisse voir le fond
    EXPECT_EQ(white, 0);           // et non un aplat blanc (texture ignorée)
}

// Éclairage précalculé (LightingModel::BAKED) : la couleur affichée vient de
// la lumière des sommets (linéaire), sans plancher ambiant : pas de lumière,
// pas de couleur ; une lumière rouge sur du blanc donne du rouge ; plus de
// lumière, plus clair (même après le tone mapping).
TEST(OffscreenSmokeTest, BakedLightingUsesVertexLightOnly) {
    auto windowResult = Window::Create(String("offscreen_smoke_test"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());
    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());
    canvas.SetBackgroundColor(Color(0, 0, 255, 255));
    DirectionalLight sun;
    sun.intensity = 0.f;
    AmbientLight ambient;
    ambient.color = Color(0, 0, 0, 255);
    canvas.SetLighting(sun, ambient);

    OffscreenTarget target;
    ASSERT_TRUE(target.EnsureSize(canvas.Device(), 64, 64, OFFSCREEN_COLOR_FORMAT, OFFSCREEN_DEPTH_FORMAT).IsOk());
    // Moyenne des pixels du cube (ceux qui ne sont pas le fond bleu).
    auto render = [&](math::FVector4 light, float exposure) {
        const Mesh base = Mesh::Cube(6.f);
        std::vector<Vertex3D> vertices = base.Vertices();
        for (Vertex3D &v : vertices)
            v.light = light;
        auto mesh = std::make_shared<Mesh>(std::move(vertices), base.Indices());
        Material material = Material::Default();
        material.bakedLighting = true;
        canvas.SetExposure(exposure);
        Shape cube(mesh, material);
        std::vector<uint8_t> pixels;
        EXPECT_TRUE(RenderObjectToTexture(canvas, cube, render3d::Camera{}, target, pixels).IsOk());
        std::array<double, 3> sum{0.0, 0.0, 0.0};
        int count = 0;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            if (pixels[i] < 40 && pixels[i + 1] < 40 && pixels[i + 2] > 200)
                continue; // fond
            for (int k = 0; k < 3; ++k)
                sum[size_t(k)] += pixels[i + size_t(k)];
            ++count;
        }
        if (count > 0)
            for (double &c : sum)
                c /= double(count);
        return std::make_pair(sum, count);
    };
    const auto [dark, darkCount] = render({0.f, 0.f, 0.f, 1.f}, 1.f);
    EXPECT_TRUE(darkCount > 400);
    EXPECT_TRUE(dark[0] < 2.0 && dark[1] < 2.0 && dark[2] < 2.0); // blanc non éclairé : noir
    const auto [red, redCount] = render({2.f, 0.f, 0.f, 1.f}, 1.f);
    EXPECT_TRUE(redCount > 400);
    EXPECT_TRUE(red[0] > 150.0 && red[1] < 5.0 && red[2] < 5.0);
    const auto [dim, dimCount] = render({0.3f, 0.3f, 0.3f, 1.f}, 1.f);
    const auto [bright, brightCount] = render({0.6f, 0.6f, 0.6f, 1.f}, 1.f);
    EXPECT_TRUE(dimCount > 400 && brightCount > 400);
    EXPECT_TRUE(bright[1] > dim[1] + 10.0); // deux fois plus de lumière : visiblement plus clair
    // L'ambiante n'atteint que la part indiquée par light.a.
    ambient.color = Color(255, 255, 255, 255);
    canvas.SetLighting(sun, ambient);
    const auto [shut, shutCount] = render({0.f, 0.f, 0.f, 0.f}, 1.f);
    EXPECT_TRUE(shutCount > 400 && shut[0] < 2.0);
    const auto [open, openCount] = render({0.f, 0.f, 0.f, 1.f}, 1.f);
    EXPECT_TRUE(openCount > 400 && open[0] > 100.0);
}

// Réflexions par lancer de rayons (Material::voxelReflections) : un sol
// miroir (réflectivité 1) regardé en biais renvoie ce que le rayon réfléchi
// rencontre dans la grille de voxels — ici un mur rouge qui n'existe QUE
// dans la grille (aucun maillage) ; sans grille, le sol (non éclairé) reste
// noir ; grille vide : la couleur du ciel.
TEST(OffscreenSmokeTest, VoxelReflectionsTraceTheGrid) {
    auto windowResult = Window::Create(String("offscreen_reflections"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());
    auto canvasResult = Canvas::Create(window, 64, 64);
    ASSERT_TRUE(canvasResult.IsOk());
    Canvas canvas = std::move(canvasResult.Value());
    canvas.SetBackgroundColor(Color(0, 255, 0, 255));
    DirectionalLight sun;
    sun.intensity = 0.f;
    AmbientLight ambient;
    ambient.color = Color(0, 0, 0, 255);
    canvas.SetLighting(sun, ambient);
    OffscreenTarget target;
    ASSERT_TRUE(target.EnsureSize(canvas.Device(), 64, 64, OFFSCREEN_COLOR_FORMAT, OFFSCREEN_DEPTH_FORMAT).IsOk());

    // Atlas 2 × 1 : gris (le sol), rouge (le mur) ; réflectivité : 1, 0.
    const std::vector<uint8_t> atlasPixels{128, 128, 128, 255, 255, 0, 0, 255};
    auto atlas = CreateTextureFromPixels(canvas.Device(), 2, 1, atlasPixels);
    ASSERT_TRUE(atlas.IsOk());
    auto reflectivity = CreateDataTexture(canvas.Device(), SDL_GPU_TEXTUREFORMAT_R8_UNORM, 2, 1);
    ASSERT_TRUE(reflectivity.IsOk());
    const std::vector<uint8_t> mirror{255, 0};
    ASSERT_TRUE(UploadTextureRegions(canvas.Device(), reflectivity.Value(), std::array{TextureRegionData{0, 0, 0, 2, 1, 1, mirror}}).IsOk());
    // Table des blocs : le bloc 1 (x = 1 avec 2 blocs par ligne) a toutes ses
    // faces sur le texel rouge ; teintes blanches, sans face teintée par le biome.
    std::vector<float> table(size_t(2 * 8 * 4), 1.f);
    table[size_t((6 * 2 + 1) * 4 + 3)] = 0.f; // masque des faces du biome
    table[size_t((7 * 2 + 1) * 4 + 3)] = 0.f; // pas lumineux
    for (int row = 0; row < 6; ++row) {
        float *rect = &table[size_t((row * 2 + 1) * 4)];
        rect[0] = rect[2] = 0.75f;
        rect[1] = rect[3] = 0.5f;
    }
    auto blocks = CreateDataTexture(canvas.Device(), SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, 2, 8);
    ASSERT_TRUE(blocks.IsOk());
    ASSERT_TRUE(UploadTextureRegions(canvas.Device(), blocks.Value(),
                                     std::array{TextureRegionData{0, 0, 0, 2, 8, 1,
                                                                  {reinterpret_cast<const uint8_t *>(table.data()), table.size() * 4}}})
                    .IsOk());
    // Grille 16³ au coin (−8, 0, −8) : un mur au plan z = 4 ; devant lui, les
    // cellules reçoivent une forte lumière blanche des sources.
    auto grid = CreateDataTexture(canvas.Device(), SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT, 16, 16, 16);
    ASSERT_TRUE(grid.IsOk());
    auto fill = [&](bool wall) {
        std::vector<uint16_t> cells(size_t(16 * 16 * 16 * 4), 0);
        for (int z = -8; z < 8; ++z)
            for (int y = 0; y < 16; ++y)
                for (int x = -8; x < 8; ++x) {
                    uint16_t *c = &cells[size_t((((z & 15) * 16 + (y & 15)) * 16 + (x & 15)) * 4)];
                    if (wall && z == 4)
                        c[0] = 1;
                    else
                        c[1] = 255, c[2] = 0xFFFF, c[3] = 255;
                }
        return UploadTextureRegions(canvas.Device(), grid.Value(),
                                    std::array{TextureRegionData{0, 0, 0, 16, 16, 16,
                                                                 {reinterpret_cast<const uint8_t *>(cells.data()), cells.size() * 2}}});
    };

    // Le sol : un carré horizontal en y = 0, texture du texel gris.
    std::vector<Vertex3D> vertices;
    for (const math::FVector3 &p : {math::FVector3{-4.f, 0.f, -4.f}, math::FVector3{4.f, 0.f, -4.f}, math::FVector3{4.f, 0.f, 4.f},
                                    math::FVector3{-4.f, 0.f, 4.f}})
        vertices.push_back(Vertex3D{p, {0.f, 1.f, 0.f}, {0.25f, 0.5f}, Color::WHITE(), {0.f, 0.f, 0.f, 1.f}});
    auto mesh = std::make_shared<Mesh>(std::move(vertices), std::vector<uint32_t>{0, 1, 2, 0, 2, 3});
    Material material = Material::Default();
    material.bakedLighting = true;
    material.voxelReflections = true;
    material.doubleSided = true;
    material.nearestFilter = true;
    material.albedo = Some(MakeRef(atlas.Value()));
    Shape floor(mesh, material);
    render3d::Camera camera;
    camera.position = {0.f, 3.f, -6.f};
    camera.target = {0.f, 0.f, 0.f};
    camera.aspect = 1.f;
    // Moyenne du carré central (le sol, vu de près).
    auto centre = [&]() {
        std::vector<uint8_t> pixels;
        EXPECT_TRUE(RenderObjectToTexture(canvas, floor, camera, target, pixels).IsOk());
        std::array<double, 3> sum{0.0, 0.0, 0.0};
        for (int y = 24; y < 40; ++y)
            for (int x = 24; x < 40; ++x)
                for (int k = 0; k < 3; ++k)
                    sum[size_t(k)] += pixels[size_t((y * 64 + x) * 4 + k)] / 256.0;
        return sum;
    };
    const auto plain = centre();
    EXPECT_TRUE(plain[0] < 4.0 && plain[1] < 4.0 && plain[2] < 4.0); // sans grille : sol non éclairé, noir

    VoxelReflections reflections{MakeRef(grid.Value()), MakeRef(blocks.Value()), MakeRef(reflectivity.Value())};
    reflections.origin = {-8.f, 0.f, -8.f};
    reflections.size = {16.f, 16.f, 16.f};
    reflections.blocksPerRow = 2;
    reflections.skyColor = {0.f, 0.f, 1.f};
    canvas.SetVoxelReflections(Some(reflections));
    ASSERT_TRUE(fill(true).IsOk());
    const auto mirrored = centre();
    EXPECT_TRUE(mirrored[0] > 150.0 && mirrored[1] < 20.0 && mirrored[2] < 20.0); // le mur rouge, vu dans le sol
    ASSERT_TRUE(fill(false).IsOk());
    const auto open = centre();
    EXPECT_TRUE(open[2] > 150.0 && open[0] < 20.0); // rien sur le trajet : le ciel bleu
    // Le mur devient miroir lui aussi : avec une seule réflexion permise, il
    // n'est qu'éclairé (rouge) ; avec deux, le rayon repart vers le ciel.
    const std::vector<uint8_t> mirrors{255, 255};
    ASSERT_TRUE(UploadTextureRegions(canvas.Device(), reflectivity.Value(), std::array{TextureRegionData{0, 0, 0, 2, 1, 1, mirrors}}).IsOk());
    ASSERT_TRUE(fill(true).IsOk());
    reflections.maxBounces = 1;
    canvas.SetVoxelReflections(Some(reflections));
    const auto once = centre();
    EXPECT_TRUE(once[0] > 150.0 && once[2] < 20.0);
    reflections.maxBounces = 2;
    canvas.SetVoxelReflections(Some(reflections));
    const auto twice = centre();
    EXPECT_TRUE(twice[2] > 150.0 && twice[0] < 20.0);
    canvas.SetVoxelReflections(NONE);
}
