// Smoke test : ui::UiShaderEffect / ui::ShaderEffectSystem (lib/include/ui/
// shader_effect.hpp) + sdl3::Renderer::ReadPixels (lib/include/sdl3/
// render.hpp) — M23 du plan (Phase 4). Trois familles de vérification, même
// rigueur par pixel que tests/offscreen_smoke_test.cpp / tests/
// viewport3d_smoke_test.cpp :
//
// 1. Renderer::ReadPixels en isolation (primitive neuve, jamais testée avant
//    ce jalon) : deux rectangles de couleur connue dessinés dans une texture
//    cible, relus, convertis en RGBA32 (PAS RGBA8888 — piège réel découvert
//    ici, cf. la note détaillée sur Renderer::ReadPixels dans render.hpp :
//    RGBA8888 est un format PACKED dont l'ordre en mémoire sur little-endian
//    est l'inverse de son nom, RGBA32 seul garantit l'ordre littéral
//    [R,G,B,A]) — comparés EXACTEMENT (tolérance +/-2) aux couleurs sources à
//    des coordonnées connues.
// 2. TINT_SHIFT : un panneau de couleur de fond CONNUE, effet appliqué avec
//    une couleur/force connues — comparé à la formule exacte hand-computed
//    (outColor = lerp(bg, bg*tintColor, strength)) à un pixel intérieur du
//    panneau (loin de tout bord, insensible à l'arrondi de rasterisation).
// 3. GLOW : même panneau, effet GLOW — comparé à smoothstep(innerRadius,
//    0.5, dist) hand-computed à 3 échantillons UV choisis pour tomber sur
//    des valeurs de smoothstep exactement rationnelles (t=0, t=1, t=0.5).
//
// Couvre aussi, en passant : le décalage de repère de coordonnées (Renderer::
// SetViewport, cf. shader_effect.hpp en-tête) — le panneau testé est
// délibérément positionné loin de (0,0) pour exercer ce chemin, pas juste au
// coin de la fenêtre.
#define USE_TEST
#include <array>
#include <cmath>
#include <stdexcept>

#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

using namespace sdl3;

namespace {

/// Fabrique commune : fenêtre + Canvas (GPU) + Renderer SDL classique sur la
/// MÊME fenêtre — même construction, sans conflit empiriquement confirmée,
/// que tests/viewport3d_smoke_test.cpp::TestRig.
struct TestRig {
    Window window;
    render3d::Canvas canvas;
    Renderer renderer;

    static TestRig Create(int w, int h) {
        auto windowResult = Window::Create(String("ui_shader_effects_smoke_test"), w, h, 0);
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

/// Pose UiComputed.screen ET .clip directement (comme viewport3d_smoke_test::
/// SetScreenRect) — un widget racine sans parent a clip == screen (aucun
/// ancêtre pour restreindre). Position (x,y) NON nulle par défaut dans les
/// tests ci-dessous : exerce délibérément le décalage de repère de
/// coordonnées (cf. shader_effect.hpp en-tête).
void SetScreenRect(ecs::ArchetypeRegistry &world, ecs::Entity e, float x, float y, float w, float h) {
    auto computed = world.GetComponent<ui::UiComputed>(e);
    ASSERT_TRUE(computed.IsSome());
    FRect r{x, y, w, h};
    computed.Unwrap()->screen = r;
    computed.Unwrap()->clip = r;
}

/// Spawne un panneau opaque de couleur `bg` CONNUE, portant `effect`, sans
/// passer par UiFactory/UiTheme (évite tout style de classe/dégradé thématique
/// — cf. rapport de tâche : seul un fond PLAT à couleur exacte permet une
/// vérification par pixel hand-computed).
ecs::Entity SpawnEffectPanel(ecs::ArchetypeRegistry &world, FColor bg, ui::UiShaderEffect effect, float x, float y,
                             float w, float h) {
    ecs::Entity e = world.Spawn();
    world.AddComponent(e, ui::UiRect{});
    world.AddComponent(e, ui::UiComputed{}); // normalement ajouté par UiFactory::Spawn() (factory.hpp:672) — pas ici
    world.AddComponent(e, ui::UiPanel{});
    ui::UiStyle style{};
    style.SetBg(bg);
    ui::SetInlineStyle(world, e, style);
    world.AddComponent(e, effect);
    SetScreenRect(world, e, x, y, w, h);
    return e;
}

/// Relit les pixels d'une sdl3::Texture déjà composée (ex: RenderSystem::
/// effectTextures) : la redessine (pleine taille, sans déformation) sur une
/// texture cible temporaire de même taille, puis ReadPixels + Convert vers un
/// layout RGBA32 (ordre littéral [R,G,B,A]) connu — PAS RGBA8888, cf. la note
/// sur Renderer::ReadPixels dans render.hpp. Format de CRÉATION de la cible
/// temporaire elle-même sans importance (simple canevas 2D, jamais nourri
/// d'octets bruts — le compositing `Render()` gère le tag correctement).
Result<Surface, StringView> ReadBackTexture(Renderer &ren, Texture &tex, int w, int h) {
    auto targetResult = ren.CreateTexture(PixelFormat::RGBA8888, TextureAccess::TARGET, w, h);
    if (!targetResult)
        return Err(targetResult.Error());
    Texture target = std::move(targetResult.Value());
    if (!ren.SetTarget(target))
        return Err(StringView("SetTarget failed"));
    ren.SetDrawColor(FColor{0.f, 0.f, 0.f, 0.f});
    ren.Clear();
    ren.Render(tex, FRect{0.f, 0.f, float(w), float(h)});
    auto read = ren.ReadPixels();
    ren.ResetTarget();
    if (!read)
        return Err(read.Error());
    return read.Value().Convert(PixelFormat::RGBA32);
}

std::array<uint8_t, 4> SamplePixel(const Surface &surf, int x, int y) {
    const auto *s = surf.Get();
    const auto *px = static_cast<const uint8_t *>(s->pixels) + size_t(y) * size_t(s->pitch) + size_t(x) * 4;
    return {px[0], px[1], px[2], px[3]};
}

} // namespace

TEST(UiShaderEffectsSmokeTest, ReadPixelsRoundTripsKnownColorsExactly) {
    auto windowResult = Window::Create(String("ui_shader_effects_smoke_test_readpixels"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());
    auto rendererResult = Renderer::Create(window);
    ASSERT_TRUE(rendererResult.IsOk());
    Renderer renderer = std::move(rendererResult.Value());

    auto targetResult = renderer.CreateTexture(PixelFormat::RGBA8888, TextureAccess::TARGET, 64, 64);
    ASSERT_TRUE(targetResult.IsOk());
    Texture target = std::move(targetResult.Value());
    ASSERT_TRUE(renderer.SetTarget(target));
    renderer.SetDrawColor(FColor{0.f, 0.f, 0.f, 1.f});
    renderer.Clear();
    // Gauche = rouge pur, droite = vert pur (mêmes deux couleurs/agencement
    // que OffscreenSmokeTest::UploadSurfaceToGpuTextureRoundTripsExactColors,
    // pour la même raison : aucune ambiguïté possible sur un flip d'axe).
    renderer.SetDrawColor(FColor{1.f, 0.f, 0.f, 1.f});
    renderer.FillRect(FRect{0.f, 0.f, 32.f, 64.f});
    renderer.SetDrawColor(FColor{0.f, 1.f, 0.f, 1.f});
    renderer.FillRect(FRect{32.f, 0.f, 32.f, 64.f});

    auto readResult = renderer.ReadPixels();
    renderer.ResetTarget();
    ASSERT_TRUE(readResult.IsOk());
    auto converted = readResult.Value().Convert(PixelFormat::RGBA32);
    ASSERT_TRUE(converted.IsOk());
    Surface &surf = converted.Value();
    ASSERT_EQ(surf.GetWidth(), 64);
    ASSERT_EQ(surf.GetHeight(), 64);

    auto left = SamplePixel(surf, 16, 32);
    EXPECT_TRUE(std::abs(int(left[0]) - 255) <= 2);
    EXPECT_TRUE(std::abs(int(left[1]) - 0) <= 2);
    EXPECT_TRUE(std::abs(int(left[2]) - 0) <= 2);
    EXPECT_TRUE(std::abs(int(left[3]) - 255) <= 2);

    auto right = SamplePixel(surf, 48, 32);
    EXPECT_TRUE(std::abs(int(right[0]) - 0) <= 2);
    EXPECT_TRUE(std::abs(int(right[1]) - 255) <= 2);
    EXPECT_TRUE(std::abs(int(right[2]) - 0) <= 2);
    EXPECT_TRUE(std::abs(int(right[3]) - 255) <= 2);
}

TEST(UiShaderEffectsSmokeTest, TintShiftMatchesHandComputedLerp) {
    TestRig rig = TestRig::Create(160, 160);

    // Fond opaque gris moyen connu, tint vers le bleu pur à force 0.5 :
    // formule exacte outColor = lerp(bg, bg*tint, 0.5) = bg*(1-0.5) + bg*tint*0.5.
    FColor bg{0.6f, 0.6f, 0.6f, 1.f};
    FColor tint{0.f, 0.f, 1.f, 1.f};
    float strength = 0.5f;
    ui::UiShaderEffect effect{ui::UiShaderEffectKind::TINT_SHIFT, tint, strength};

    ecs::ArchetypeRegistry world;
    // Positionné loin de (0,0) : exerce le décalage de repère de coordonnées
    // (Renderer::SetViewport, cf. shader_effect.hpp en-tête).
    ecs::Entity e = SpawnEffectPanel(world, bg, effect, 37.f, 21.f, 48.f, 48.f);

    ui::StyleSystem styleSystem;
    styleSystem.Resolve(world); // peuple UiComputedStyle -- requis par GetResolved (DrawWidget)

    ui::SdlRendererBackend backend(rig.renderer);
    ui::RenderSystem render;
    ui::ShaderEffectSystem system;
    system.Update(world, rig.canvas, backend, render);

    auto it = render.effectTextures.find(e);
    ASSERT_TRUE(it != render.effectTextures.end());

    auto surfResult = ReadBackTexture(rig.renderer, it->second, 48, 48);
    ASSERT_TRUE(surfResult.IsOk());
    Surface &surf = surfResult.Value();

    // Pixel intérieur (24,24 -- centre du panneau 48x48) : loin de tout bord,
    // insensible à un éventuel arrondi de rasterisation sur 1px.
    auto px = SamplePixel(surf, 24, 24);
    float expectedR = bg.r * (1.f - strength) + (bg.r * tint.r) * strength;
    float expectedG = bg.g * (1.f - strength) + (bg.g * tint.g) * strength;
    float expectedB = bg.b * (1.f - strength) + (bg.b * tint.b) * strength;
    EXPECT_TRUE(std::abs(int(px[0]) - int(expectedR * 255.f)) <= 4);
    EXPECT_TRUE(std::abs(int(px[1]) - int(expectedG * 255.f)) <= 4);
    EXPECT_TRUE(std::abs(int(px[2]) - int(expectedB * 255.f)) <= 4);
    EXPECT_TRUE(std::abs(int(px[3]) - 255) <= 4);

    // Non-régression : la couleur obtenue doit rester DIFFÉRENTE du fond brut
    // (l'effet a bien été appliqué, pas juste un blit du sous-arbre normal).
    EXPECT_TRUE(px[2] > px[0]); // plus bleu que rouge (tint pur bleu, force 0.5)
}

TEST(UiShaderEffectsSmokeTest, GlowMatchesHandComputedSmoothstepAtThreeSamples) {
    TestRig rig = TestRig::Create(160, 160);

    // Panneau 64x64 (coordonnées UV rondes -> centre=32,32 exact) fond blanc
    // opaque, glow rouge pur. innerRadius=0.25 : la formule smoothstep(0.25,
    // 0.5, dist) atteint t=0 pour dist<=0.25, t=1 pour dist>=0.5, t=0.5 (donc
    // smoothstep=0.5 EXACTEMENT, t*t*(3-2t) à t=0.5 -> 0.25*2=0.5) pour
    // dist=0.375 (le milieu de l'intervalle [0.25,0.5]).
    FColor bg = FColor::WHITE();
    FColor glowColor{1.f, 0.f, 0.f, 1.f};
    float innerRadius = 0.25f;
    ui::UiShaderEffect effect{ui::UiShaderEffectKind::GLOW, glowColor, innerRadius};

    ecs::ArchetypeRegistry world;
    ecs::Entity e = SpawnEffectPanel(world, bg, effect, 10.f, 10.f, 64.f, 64.f);

    ui::StyleSystem styleSystem;
    styleSystem.Resolve(world);

    ui::SdlRendererBackend backend(rig.renderer);
    ui::RenderSystem render;
    ui::ShaderEffectSystem system;
    system.Update(world, rig.canvas, backend, render);

    auto it = render.effectTextures.find(e);
    ASSERT_TRUE(it != render.effectTextures.end());

    auto surfResult = ReadBackTexture(rig.renderer, it->second, 64, 64);
    ASSERT_TRUE(surfResult.IsOk());
    Surface &surf = surfResult.Value();

    auto lerpToward = [&](float t) {
        float r = bg.r * (1.f - t) + glowColor.r * t;
        float g = bg.g * (1.f - t) + glowColor.g * t;
        float b = bg.b * (1.f - t) + glowColor.b * t;
        return std::array<float, 3>{r, g, b};
    };
    // `tolerance` élargie au point milieu délibérément (cf. rapport de tâche
    // M23) : c'est le point de PENTE MAXIMALE de smoothstep (dérivée max
    // exactement au centre de [edge0,edge1]) — un écart sous-pixel de
    // rastérisation/échantillonnage GPU (confirmé : centre et coin, tous deux
    // dans une région PLATE/saturée de la courbe, retombent pile exacts) s'y
    // traduit mécaniquement en un écart de couleur bien plus grand qu'ailleurs
    // sur la courbe — un défaut de précision de rendu attendu à ce point
    // précis, pas une erreur de logique (cf. formule smoothstep déjà vérifiée
    // manuellement correcte dans shader_effect.hpp).
    auto checkAt = [&](int px, int py, float expectedT, int tolerance) {
        auto expected = lerpToward(expectedT);
        auto sample = SamplePixel(surf, px, py);
        EXPECT_TRUE(std::abs(int(sample[0]) - int(expected[0] * 255.f)) <= tolerance);
        EXPECT_TRUE(std::abs(int(sample[1]) - int(expected[1] * 255.f)) <= tolerance);
        EXPECT_TRUE(std::abs(int(sample[2]) - int(expected[2] * 255.f)) <= tolerance);
    };

    // Centre exact (32,32) : uv=(0.5,0.5), dist=0 <= innerRadius -> t=0 (fond intact) —
    // région plate (dérivée nulle) : tolérance serrée.
    checkAt(32, 32, 0.f, 6);
    // dist=0.375 en UV -> 24px depuis le centre le long d'un axe (0.375*64=24) : t=0.5 exact —
    // point de pente MAXIMALE de la courbe, cf. commentaire ci-dessus.
    checkAt(32 + 24, 32, 0.5f, 20);
    // Coin (1,1) : uv proche de (0,0), dist ~= 0.686 (> 0.5) -> t=1 (glow plein) —
    // région plate (saturée) : tolérance serrée.
    checkAt(1, 1, 1.f, 6);
}

int main() {
    return RUN_ALL_TESTS();
}
