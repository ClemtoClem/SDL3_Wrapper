// Smoke test : ui::UiViewport3D / ui::Viewport3DSystem (lib/include/ui/
// viewport3d.hpp) — M22 du plan (Phase 3). Vérifie, par lecture directe des
// pixels de l'OffscreenTarget/buffer de chaque widget (même technique que
// tests/offscreen_smoke_test.cpp — la fenêtre SDL_Renderer composée finale
// ne peut pas être capturée dans cet environnement, cf. mémoire) :
//
// 1. Aucune déformation : un widget large (128x64) et un widget haut
//    (64x128), même scène (carré plat face caméra, halfExtent=1) — la
//    silhouette doit rester CARRÉE en pixels dans les deux cas, et matcher
//    la formule de projection perspective symétrique hand-computed (même
//    formule que OffscreenSmokeTest::CameraAspectOverriddenFromTargetPrevents
//    Distortion), maintenant vérifiée à travers TOUT le pipeline Viewport3D
//    (UiComputed.screen -> Viewport3DSystem::Update -> OffscreenTarget).
// 2. Deux caméras réellement différentes sur la MÊME scène (deux cubes
//    colorés distincts) produisent un contenu pixel différent aux points
//    d'échantillonnage correspondants.
// 3. Viewport3DSystem::Update reste sûr vis-à-vis du hazard d'invalidation
//    d'itérateur ECS documenté (memory/project_ui_ecs_module.md) avec 2+
//    entités UiViewport3D, y compris combiné à des mutations ECS non liées
//    la même frame (spawn/despawn d'entités quelconques).
#define USE_TEST
#include <cmath>

#include "core/test.hpp"
#include "render3d/shape.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

using namespace sdl3;

namespace {

/// Fabrique commune : fenêtre + Canvas (GPU) + Renderer SDL classique sur la
/// MÊME fenêtre — confirmé sans conflit empiriquement pendant ce jalon (cf.
/// rapport de tâche M22) : Canvas::Create() réclame le swapchain de la
/// fenêtre pour SDL_GPU, mais Viewport3DSystem::Update ne rend JAMAIS dans ce
/// swapchain (RenderObjectToTexture/RenderObjectOffscreen utilisent leur
/// propre command buffer local, cf. offscreen.hpp) donc les deux coexistent.
struct TestRig {
    Window window;
    render3d::Canvas canvas;
    Renderer renderer;

    static TestRig Create(int w, int h) {
        auto windowResult = Window::Create(String("viewport3d_smoke_test"), w, h, 0);
        if (!windowResult)
            throw std::runtime_error(windowResult.Error().CStr());
        Window window = std::move(windowResult.Value());

        auto canvasResult = render3d::Canvas::Create(window, w, h);
        if (!canvasResult)
            throw std::runtime_error(canvasResult.Error().CStr());
        render3d::Canvas canvas = std::move(canvasResult.Value());

        auto rendererResult = Renderer::Create(window);
        if (!rendererResult)
            throw std::runtime_error(rendererResult.Error().CStr());
        Renderer renderer = std::move(rendererResult.Value());

        return TestRig{std::move(window), std::move(canvas), std::move(renderer)};
    }
};

/// Pose UiComputed.screen directement (plutôt que de dépendre de LayoutSystem
/// — autorisé par la tâche : "drive UiComputed::screen directly if that's
/// easier than running a full layout pass") : les tests ci-dessous ont besoin
/// d'une taille pixel EXACTE et déterministe par widget, indépendante de tout
/// détail de l'algorithme flexbox-like de LayoutSystem.
void SetScreenRect(ecs::ArchetypeRegistry &world, ecs::Entity e, float w, float h) {
    auto computed = world.GetComponent<ui::UiComputed>(e);
    ASSERT_TRUE(computed.IsSome());
    computed.Unwrap()->screen = FRect{0.f, 0.f, w, h};
}

/// Mesure la boîte englobante des pixels "blancs" (silhouette du carré plat,
/// même seuil que OffscreenSmokeTest::CameraAspectOverriddenFromTargetPrevents
/// Distortion) dans un buffer RGBA8 tightly-packed.
void MeasureWhiteSilhouette(const std::vector<uint8_t> &pixels, uint32_t width, uint32_t height,
                             float &outHalfWidthPx, float &outHalfHeightPx) {
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
}

} // namespace

TEST(Viewport3dSmokeTest, NoStretchAtDifferentAspectRatiosThroughFullPipeline) {
    TestRig rig = TestRig::Create(64, 64);

    // Carré plat face caméra, halfExtent=1, même construction que
    // tests/offscreen_smoke_test.cpp::CameraAspectOverriddenFromTargetPrevents
    // Distortion — reste dans le champ pour les deux rapports d'aspect testés
    // (demi-largeur frustum minimale, aspect=0.5, halfWidth ~= 1.44 > 1).
    float halfExtent = 1.f;
    std::vector<render3d::Vertex3D> quadVerts = {
        {{-halfExtent, -halfExtent, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
        {{halfExtent, -halfExtent, 0.f}, {0.f, 0.f, -1.f}, {1.f, 0.f}, Color::WHITE()},
        {{halfExtent, halfExtent, 0.f}, {0.f, 0.f, -1.f}, {1.f, 1.f}, Color::WHITE()},
        {{-halfExtent, halfExtent, 0.f}, {0.f, 0.f, -1.f}, {0.f, 1.f}, Color::WHITE()},
    };
    std::vector<uint32_t> quadIdx = {0, 1, 2, 0, 2, 3};
    render3d::Material squareMaterial = render3d::Material::Unlit(Color::WHITE());
    squareMaterial.doubleSided = true;
    render3d::Object3D root;
    auto &square = static_cast<render3d::Shape &>(
        root.Add(std::make_unique<render3d::Shape>(render3d::Mesh(quadVerts, quadIdx), squareMaterial)));
    (void)square;

    render3d::Camera camera; // aspect écrasé par RenderObjectToTexture (via Viewport3DSystem), même défauts que M21

    ecs::ArchetypeRegistry world;
    ui::LayoutSystem layout;
    ui::UiFactory factory(world, layout);

    ecs::Entity wide = factory.Viewport3D(root, camera).Spawn();
    SetScreenRect(world, wide, 128.f, 64.f);
    ecs::Entity tall = factory.Viewport3D(root, camera).Spawn();
    SetScreenRect(world, tall, 64.f, 128.f);

    ui::SdlRendererBackend backend(rig.renderer);
    ui::Viewport3DSystem system;
    system.Update(world, rig.canvas, backend);

    auto wideVp = world.GetComponent<ui::UiViewport3D>(wide);
    auto tallVp = world.GetComponent<ui::UiViewport3D>(tall);
    ASSERT_TRUE(wideVp.IsSome());
    ASSERT_TRUE(tallVp.IsSome());
    ASSERT_EQ(wideVp.Unwrap()->pixels.size(), size_t(128 * 64 * 4));
    ASSERT_EQ(tallVp.Unwrap()->pixels.size(), size_t(64 * 128 * 4));
    // Le blit 2D a bien eu quelque chose à afficher (backend SDL réel ici).
    ASSERT_TRUE(wideVp.Unwrap()->displayTexture.IsSome());
    ASSERT_TRUE(tallVp.Unwrap()->displayTexture.IsSome());

    float wideHalfWidthPx = 0.f, wideHalfHeightPx = 0.f;
    MeasureWhiteSilhouette(wideVp.Unwrap()->pixels, 128, 64, wideHalfWidthPx, wideHalfHeightPx);
    float tallHalfWidthPx = 0.f, tallHalfHeightPx = 0.f;
    MeasureWhiteSilhouette(tallVp.Unwrap()->pixels, 64, 128, tallHalfWidthPx, tallHalfHeightPx);

    // Invariant clé (identique à M21) : silhouette carrée (largeur px ==
    // hauteur px) quel que soit le rapport d'aspect du widget — cassé
    // exactement si camera.aspect n'est pas dérivé de la taille RÉELLE du
    // widget quelque part dans Viewport3DSystem::Update.
    EXPECT_TRUE(std::abs(wideHalfWidthPx - wideHalfHeightPx) <= 2.f);
    EXPECT_TRUE(std::abs(tallHalfWidthPx - tallHalfHeightPx) <= 2.f);

    float distance = std::abs(camera.position.z - 0.f);
    float tanHalfFovY = std::tan(camera.fovYRadians * 0.5f);
    float expectedWide = halfExtent * 64.f / (2.f * distance * tanHalfFovY);
    float expectedTall = halfExtent * 128.f / (2.f * distance * tanHalfFovY);
    EXPECT_TRUE(std::abs(wideHalfWidthPx - expectedWide) <= 2.f);
    EXPECT_TRUE(std::abs(tallHalfWidthPx - expectedTall) <= 2.f);
}

TEST(Viewport3dSmokeTest, DistinctCamerasOnSameSceneShowDifferentContent) {
    TestRig rig = TestRig::Create(64, 64);
    rig.canvas.SetBackgroundColor(Color{8, 8, 8, 255}); // distinct de rouge/vert ci-dessous

    // Même racine de scène pour les deux widgets (cf. en-tête du fichier) :
    // deux cubes colorés distincts, écartés en X, pour que le contenu visible
    // dépende réellement de l'angle de vue et non juste d'une teinte plate
    // uniforme (un seul cube de couleur unie regarderait pareil de face quel
    // que soit le cadrage).
    render3d::Object3D root;
    auto &redCube = static_cast<render3d::Shape &>(
        root.Add(std::make_unique<render3d::Shape>(render3d::Mesh::Cube(1.6f), render3d::Material::Unlit(Color::RED()))));
    redCube.SetPosition({-2.f, 0.f, 0.f});
    auto &greenCube = static_cast<render3d::Shape &>(root.Add(
        std::make_unique<render3d::Shape>(render3d::Mesh::Cube(1.6f), render3d::Material::Unlit(Color::GREEN()))));
    greenCube.SetPosition({2.f, 0.f, 0.f});

    // Caméra A : vue d'ensemble depuis (0,0,-8) visant l'origine — à cette
    // distance, demi-largeur frustum ~= 8*tan(30 deg) ~= 4.62, les deux cubes
    // (bords en x=[-2.8,-1.2] et [1.2,2.8]) sont visibles de part et d'autre
    // du CENTRE, qui reste donc fond (aucun cube ne couvre x=0).
    render3d::Camera camA;
    camA.position = {0.f, 0.f, -8.f};
    camA.target = {0.f, 0.f, 0.f};

    // Caméra B : cadrée directement sur le cube ROUGE (0,0,-4 relatif au
    // centre du cube, distance 4) — demi-largeur frustum ~= 4*tan(30) ~=
    // 2.31, largement plus grand que le demi-côté du cube (0.8) : le pixel
    // central montre donc à coup sûr du rouge.
    render3d::Camera camB;
    camB.position = {-2.f, 0.f, -4.f};
    camB.target = {-2.f, 0.f, 0.f};

    ecs::ArchetypeRegistry world;
    ui::LayoutSystem layout;
    ui::UiFactory factory(world, layout);

    ecs::Entity overview = factory.Viewport3D(root, camA).Spawn();
    SetScreenRect(world, overview, 64.f, 64.f);
    ecs::Entity onRed = factory.Viewport3D(root, camB).Spawn();
    SetScreenRect(world, onRed, 64.f, 64.f);

    ui::SdlRendererBackend backend(rig.renderer);
    ui::Viewport3DSystem system;
    system.Update(world, rig.canvas, backend);

    auto overviewVp = world.GetComponent<ui::UiViewport3D>(overview);
    auto onRedVp = world.GetComponent<ui::UiViewport3D>(onRed);
    ASSERT_TRUE(overviewVp.IsSome());
    ASSERT_TRUE(onRedVp.IsSome());

    auto centerPixel = [](const std::vector<uint8_t> &pixels, uint32_t width, uint32_t height) {
        size_t idx = (size_t(height / 2) * width + width / 2) * 4;
        return std::array<uint8_t, 4>{pixels[idx + 0], pixels[idx + 1], pixels[idx + 2], pixels[idx + 3]};
    };
    auto overviewCenter = centerPixel(overviewVp.Unwrap()->pixels, 64, 64);
    auto onRedCenter = centerPixel(onRedVp.Unwrap()->pixels, 64, 64);

    // Caméra A (vue d'ensemble) : centre = fond, PAS rouge.
    EXPECT_TRUE(overviewCenter[0] < 100);
    // Caméra B (cadrée sur le cube rouge) : centre nettement rouge.
    EXPECT_TRUE(onRedCenter[0] > 200);
    EXPECT_TRUE(onRedCenter[1] < 60);
    // Les deux widgets, même racine de scène, montrent bien un contenu
    // pixel différent au même point d'échantillonnage (centre) — c'est
    // l'invariant recherché par ce test.
    EXPECT_TRUE(overviewCenter[0] != onRedCenter[0] || overviewCenter[1] != onRedCenter[1] ||
                overviewCenter[2] != onRedCenter[2]);
}

TEST(Viewport3dSmokeTest, UpdateSafeWithMultipleViewportsAndUnrelatedEcsChurn) {
    TestRig rig = TestRig::Create(64, 64);

    render3d::Object3D root;
    root.Add(std::make_unique<render3d::Shape>(render3d::Mesh::Cube(1.f), render3d::Material::Unlit(Color::WHITE())));

    ecs::ArchetypeRegistry world;
    ui::LayoutSystem layout;
    ui::UiFactory factory(world, layout);

    // 3 UiViewport3D (cf. tâche : "2+ viewport entities") de tailles
    // distinctes, entrelacées avec du churn ECS SANS RAPPORT (spawn/despawn
    // d'entités quelconques, ajout de composant) juste avant Update() — le
    // hazard documenté (memory/project_ui_ecs_module.md) est une migration
    // d'archétype PENDANT une itération de Query ; ce test vérifie que
    // Viewport3DSystem::Update (collecte-puis-mute, cf. viewport3d.hpp) reste
    // correct même quand l'état ECS global a bougé juste avant.
    std::vector<ecs::Entity> viewports;
    for (int i = 0; i < 3; ++i) {
        ecs::Entity e = factory.Viewport3D(root).Spawn();
        SetScreenRect(world, e, float(32 + i * 16), float(32 + i * 8));
        viewports.push_back(e);
    }

    ecs::Entity churnA = world.Spawn();
    world.AddComponent(churnA, ui::UiName{String("churn-a")});
    ecs::Entity churnB = world.Spawn();
    world.Despawn(churnB);
    world.AddComponent(viewports[0], ui::UiName{String("first-viewport")});

    ui::SdlRendererBackend backend(rig.renderer);
    ui::Viewport3DSystem system;
    system.Update(world, rig.canvas, backend);

    for (size_t i = 0; i < viewports.size(); ++i) {
        auto vp = world.GetComponent<ui::UiViewport3D>(viewports[i]);
        ASSERT_TRUE(vp.IsSome());
        uint32_t w = 32 + uint32_t(i) * 16;
        uint32_t h = 32 + uint32_t(i) * 8;
        EXPECT_TRUE(vp.Unwrap()->pixels.size() == size_t(w) * h * 4);
        EXPECT_TRUE(vp.Unwrap()->displayTexture.IsSome());
    }
    // Le composant ajouté après coup au premier viewport doit avoir survécu
    // sans corruption (preuve indirecte qu'aucune migration d'archétype
    // déclenchée pendant Update() n'a laissé l'ECS dans un état incohérent).
    auto name = world.GetComponent<ui::UiName>(viewports[0]);
    ASSERT_TRUE(name.IsSome());
    EXPECT_TRUE(name.Unwrap()->name == String("first-viewport"));
}

int main() {
    return RUN_ALL_TESTS();
}
