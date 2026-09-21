/**
 * ui_shader_effects_demo — ui::UiShaderEffect (M23, Phase 4 du plan
 * d'expansion moteur) : quelques panneaux 2D ordinaires, chacun post-traité
 * par un effet shader GPU différent (TINT_SHIFT / GLOW) via
 * ui::ShaderEffectSystem — même intégration que ui::Viewport3D (M22) :
 * sdl3::Renderer (backend 2D normal) + render3d::Canvas (device GPU pour
 * l'effet) enregistrés ensemble sur ui::Ui, coexistence confirmée sans
 * conflit (cf. rapport de tâche M22).
 *
 * Pipeline (cf. lib/include/ui/shader_effect.hpp pour le détail complet) :
 * chaque widget portant un UiShaderEffect est d'abord dessiné normalement
 * dans une texture cible SDL_Renderer dédiée, relue (Renderer::ReadPixels,
 * M23), envoyée au GPU (UploadSurfaceToGpuTexture, M21), passée par un petit
 * pipeline GPU dédié au preset choisi, puis re-composée dans l'UI 2D via le
 * même DrawImageFit que n'importe quelle image — RenderSystem::DrawWidget
 * blitte alors le résultat POST-TRAITÉ à la place du sous-arbre normal du
 * widget.
 *
 *   make build/bin/ui_shader_effects_demo && ./build/bin/ui_shader_effects_demo
 */
#include <iostream>

#include "core/core.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

static constexpr int WIN_W = 900;
static constexpr int WIN_H = 420;
static constexpr float FONT_PT = 14.f;

int main() {
    auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO | sdl3::init_flags::EVENTS);
    if (!sdl) {
        std::cerr << "SDL init: " << sdl.Error().CStr() << "\n";
        return 1;
    }
    auto ttf = sdl3::TtfContext::Create();
    if (!ttf) {
        std::cerr << "TTF init: " << ttf.Error().CStr() << "\n";
        return 1;
    }
    auto fontRes = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, FONT_PT);
    if (!fontRes) {
        std::cerr << "Font: " << fontRes.Error().CStr() << "\n";
        return 1;
    }
    auto &font = fontRes.Value();

    auto winRes = sdl3::Window::Create(u8"ui:: - Shader effects demo", WIN_W, WIN_H, sdl3::window_flags::RESIZABLE);
    if (!winRes) {
        std::cerr << "Window: " << winRes.Error().CStr() << "\n";
        return 1;
    }
    auto &window = winRes.Value();

    // Renderer (backend 2D de ui::) ET Canvas (device GPU pour les effets
    // shader) sur la MÊME fenêtre — cf. en-tête du fichier.
    auto renRes = sdl3::Renderer::Create(window);
    if (!renRes) {
        std::cerr << "Renderer: " << renRes.Error().CStr() << "\n";
        return 1;
    }
    auto &ren = renRes.Value();

    auto canvasRes = render3d::Canvas::Create(window, window.GetWidth(), window.GetHeight());
    if (!canvasRes) {
        std::cerr << "Canvas::Create: " << canvasRes.Error().CStr() << "\n";
        return 1;
    }
    render3d::Canvas canvas = std::move(canvasRes.Value());

    auto engRes = sdl3::TextEngine::Create(ren);
    if (!engRes) {
        std::cerr << "TextEngine: " << engRes.Error().CStr() << "\n";
        return 1;
    }
    auto &eng = engRes.Value();

    ecs::ArchetypeRegistry ar;
    ui::Ui gui(ar, window, ren, ui::UiTheme::Dark());
    gui.Initialize(canvas); // enregistre le Canvas pour ShaderEffectSystem (M23) — ne touche PAS le backend 2D

    gui.SetTextEngine(eng, font);
    gui.Layout().measureText = [&font](const String &s, float fs) -> sdl3::FPoint {
        if (auto sz = font.Measure(s); sz.IsSome())
            return {float(sz.Unwrap().x) * (fs / FONT_PT), fs * 1.35f};
        return {float(s.size()) * fs * 0.55f, fs * 1.3f};
    };

    ui::UiFactory &f = gui.Factory();

    auto root = f.Row();
    root.Anchor(ui::Anchor::Center).Gap(24.f).Pad(24.f).WAuto().HAuto().Children(
        // ── Sans effet (référence) ───────────────────────────────────────
        f.Column().Gap(8.f).Children(
            f.Label("Normal").FontSize(15.f),
            f.Panel().Size(160.f, 160.f).Style(ui::UiStyle{}.SetBg(sdl3::FColor{0.35f, 0.55f, 0.85f, 1.f}))),
        // ── TINT_SHIFT : teinte le fond vers le orange, force 0.6 ───────────
        f.Column().Gap(8.f).Children(
            f.Label("TintShift (orange, 0.6)").FontSize(15.f),
            f.Panel()
                .Size(160.f, 160.f)
                .Style(ui::UiStyle{}.SetBg(sdl3::FColor{0.35f, 0.55f, 0.85f, 1.f}))
                .TintShiftEffect(sdl3::FColor{1.f, 0.55f, 0.15f, 1.f}, 0.6f)),
        // ── GLOW : halo rouge depuis le centre ──────────────────────────────
        f.Column().Gap(8.f).Children(
            f.Label("Glow (rouge)").FontSize(15.f),
            f.Panel()
                .Size(160.f, 160.f)
                .Style(ui::UiStyle{}.SetBg(sdl3::FColor::WHITE()))
                .GlowEffect(sdl3::FColor{1.f, 0.15f, 0.15f, 1.f}, 0.3f)));
    root.Spawn();

    bool running = true;
    while (running) {
        while (auto ev = sdl3::PollEvent()) {
            auto &e = ev.Value();
            if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE)) {
                running = false;
                break;
            }
            gui.HandleEvent(e);
        }
        gui.Tick(0.f);

        ren.SetDrawColor(sdl3::FColor::UI_APP_BG());
        ren.Clear();
        gui.Render();
        ren.Present();
    }
    return 0;
}
