// Smoke test : module ui/ ECS — scènes, factory/builder, layout dirty-caché,
// simulation d'entrées (clic, slider, toggle, saisie), rendu headless, et
// vérification de performance (une UI au repos ne relance JAMAIS le layout).
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>

using Clock = std::chrono::steady_clock;

static double MsSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// ── Fabrique de sdl3::Event synthétiques ────────────────────────────────────
// InputSystem consomme directement des sdl3::Event (plus d'InputState
// agrégée à construire) ; ces helpers simulent la boucle de poll SDL.

static sdl3::Event MouseDownEv(float x, float y) {
    sdl3::Event ev{};
    ev.raw.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    ev.raw.button.x = x;
    ev.raw.button.y = y;
    ev.raw.button.button = SDL_BUTTON_LEFT;
    return ev;
}
static sdl3::Event MouseUpEv(float x, float y) {
    sdl3::Event ev{};
    ev.raw.type = SDL_EVENT_MOUSE_BUTTON_UP;
    ev.raw.button.x = x;
    ev.raw.button.y = y;
    ev.raw.button.button = SDL_BUTTON_LEFT;
    return ev;
}
static sdl3::Event MouseMotionEv(float x, float y) {
    sdl3::Event ev{};
    ev.raw.type = SDL_EVENT_MOUSE_MOTION;
    ev.raw.motion.x = x;
    ev.raw.motion.y = y;
    return ev;
}
[[maybe_unused]] static sdl3::Event WheelEv(float y) {
    sdl3::Event ev{};
    ev.raw.type = SDL_EVENT_MOUSE_WHEEL;
    ev.raw.wheel.y = y;
    return ev;
}
static sdl3::Event TextInputEv(const char *s) {
    static char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", s);
    sdl3::Event ev{};
    ev.raw.type = SDL_EVENT_TEXT_INPUT;
    ev.raw.text.text = buf;
    return ev;
}
static sdl3::Event KeyDownEv(SDL_Keycode kc) {
    sdl3::Event ev{};
    ev.raw.type = SDL_EVENT_KEY_DOWN;
    ev.raw.key.key = kc;
    return ev;
}

int main() {
    setenv("SDL_VIDEODRIVER", "dummy", 0);

    auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
    if (!sdl) {
        std::cerr << "sdl init failed\n";
        return 1;
    }
    auto winRes = sdl3::Window::Create("smoke-13", 800, 600, sdl3::window_flags::HIDDEN);
    if (!winRes) {
        std::cerr << "window failed\n";
        return 1;
    }
    auto renRes = sdl3::Renderer::Create(winRes.Value());
    if (!renRes) {
        std::cerr << "renderer failed\n";
        return 1;
    }
    auto &ren = renRes.Value();
    ui::SdlRendererBackend renBackend(ren);

    ecs::ArchetypeRegistry ar;
    ui::LayoutSystem layout;
    ui::InputSystem input;
    ui::RenderSystem render;
    ui::UiFactory f(ar, layout);
    ui::SceneManager scenes(f);

    // ── Scène "menu" ─────────────────────────────────────────────────────────
    int playClicks = 0;
    float sliderValue = -1.f;
    bool toggled = false;
    String submitted;

    scenes.Add("menu", [&](ui::UiFactory &fac) {
        ecs::Entity root =
            fac.Panel()
                .Pad(16)
                .Gap(10)
                .Offset(50, 50)
                .W(ui::Dimension::Px(300))
                .HAuto()
                .Children(fac.Label("Menu principal").FontSize(20).Name("title"),
                          fac.Button("Jouer").Name("play").OnClick([&] { playClicks++; }),
                          fac.Slider(0.f, 100.f, 25.f).H(ui::Dimension::Px(20)).Name("volume").OnChange([&](float v) {
                              sliderValue = v;
                          }),
                          fac.Row().Gap(8).Children(
                              fac.Toggle(false).Name("fullscreen").OnToggle([&](bool b) { toggled = b; }),
                              fac.Label("Plein écran")),
                          fac.Separator(),
                          fac.Input("Pseudo...").Name("nick").OnSubmit([&](const String &s) { submitted = s; }),
                          fac.Progress(0.f, 1.f, 0.4f).H(ui::Dimension::Px(8)))
                .Spawn();
        return std::vector{root};
    });

    scenes.Add("options", [&](ui::UiFactory &fac) {
        ecs::Entity root = fac.Panel()
                              .Pad(16)
                              .Offset(400, 50)
                              .W(ui::Dimension::Px(200))
                              .HAuto()
                              .Children(fac.Label("Options"), fac.Checkbox(true).Name("vsync"))
                              .Spawn();
        return std::vector{root};
    });

    // ── 1. show + layout ─────────────────────────────────────────────────────
    assert(scenes.SwitchTo("menu"));
    bool ran = layout.RunIfNeeded(ar, 800, 600);
    assert(ran);
    uint64_t passes = layout.PassCount();
    std::cout << "layout pass ran, passes=" << passes << "\n";

    auto playE = ui::FindByName(ar, "play");
    assert(playE.IsSome());
    auto playRect = ar.GetComponent<ui::UiComputed>(playE.Unwrap());
    assert(playRect.IsSome());
    sdl3::FRect pr = playRect.Unwrap()->screen;
    std::cout << "play button rect: " << pr.x << "," << pr.y << " " << pr.w << "x" << pr.h << "\n";
    assert(pr.w > 0 && pr.h > 0);

    // ── 2. dirty-cache : au repos, AUCUNE nouvelle passe ────────────────────
    for (int i = 0; i < 1000; ++i)
        layout.RunIfNeeded(ar, 800, 600);
    assert(layout.PassCount() == passes);
    std::cout << "1000 frames au repos -> 0 passe de layout supplementaire (GTK4-like)\n";

    // ── 3. clic sur "Jouer" ──────────────────────────────────────────────────
    float cx = pr.x + pr.w / 2, cy = pr.y + pr.h / 2;
    input.HandleEvent(ar, MouseDownEv(cx, cy), layout);
    input.HandleEvent(ar, MouseUpEv(cx, cy), layout);
    std::cout << "playClicks=" << playClicks << "\n";
    assert(playClicks == 1);

    // ── 4. drag du slider à 50% ──────────────────────────────────────────────
    auto volE = ui::FindByName(ar, "volume");
    sdl3::FRect vr = ar.GetComponent<ui::UiComputed>(volE.Unwrap()).Unwrap()->screen;
    float svx = vr.x + vr.w * 0.5f, svy = vr.y + vr.h / 2;
    input.HandleEvent(ar, MouseDownEv(svx, svy), layout);
    std::cout << "sliderValue=" << sliderValue << "\n";
    assert(sliderValue > 45.f && sliderValue < 55.f);
    input.HandleEvent(ar, MouseUpEv(svx, svy), layout);

    // ── 5. toggle ────────────────────────────────────────────────────────────
    auto fsE = ui::FindByName(ar, "fullscreen");
    sdl3::FRect tr = ar.GetComponent<ui::UiComputed>(fsE.Unwrap()).Unwrap()->screen;
    input.HandleEvent(ar, MouseDownEv(tr.x + 5, tr.y + 5), layout);
    assert(toggled);
    input.HandleEvent(ar, MouseUpEv(tr.x + 5, tr.y + 5), layout);
    std::cout << "toggle ok\n";

    // ── 6. saisie de texte ───────────────────────────────────────────────────
    auto nickE = ui::FindByName(ar, "nick");
    sdl3::FRect nr = ar.GetComponent<ui::UiComputed>(nickE.Unwrap()).Unwrap()->screen;
    float nfx = nr.x + 5, nfy = nr.y + 5;
    input.HandleEvent(ar, MouseDownEv(nfx, nfy), layout);
    input.HandleEvent(ar, MouseUpEv(nfx, nfy), layout);
    input.HandleEvent(ar, TextInputEv("abc"), layout);
    input.HandleEvent(ar, KeyDownEv(SDLK_BACKSPACE), layout);
    input.HandleEvent(ar, KeyDownEv(SDLK_RETURN), layout);
    std::cout << "submitted=\"" << submitted.c_str() << "\"\n";
    assert(submitted == "ab");

    // ── 7. bascule de scène : état conservé, pas de rebuild ─────────────────
    uint32_t playIdBefore = playE.Unwrap().id;
    assert(scenes.SwitchTo("options"));
    layout.RunIfNeeded(ar, 800, 600);

    // La scène cachée ne réagit plus au clic.
    input.HandleEvent(ar, MouseDownEv(cx, cy), layout);
    input.HandleEvent(ar, MouseUpEv(cx, cy), layout);
    assert(playClicks == 1);
    std::cout << "scene cachee inerte au clic: ok\n";

    assert(scenes.SwitchTo("menu"));
    layout.RunIfNeeded(ar, 800, 600);
    auto playE2 = ui::FindByName(ar, "play");
    assert(playE2.IsSome() && playE2.Unwrap().id == playIdBefore); // pas de rebuild
    auto vol2 = ar.GetComponent<ui::UiSlider>(volE.Unwrap());
    assert(vol2.IsSome() && vol2.Unwrap()->value > 45.f); // état conservé
    std::cout << "re-show sans rebuild, etat conserve (slider=" << vol2.Unwrap()->value << ")\n";

    // ── 8. rendu headless ────────────────────────────────────────────────────
    ren.SetDrawColor(sdl3::Color{20, 20, 28});
    ren.Clear();
    render.Run(ar, renBackend);
    ren.Present();
    std::cout << "render ok\n";

    // ── 8bis. unités typographiques Em / Pem / Rem ───────────────────────────
    // rootFontSize = police du thème (14). Le label a une police propre de 20 :
    //   em(2)  sur le label      → 2 × 20 = 40 px
    //   rem(3) sur le label      → 3 × 14 = 42 px
    //   pem(4) sur le conteneur  → 4 × police effective du parent (racine sans
    //                              police propre → hérite 14) = 56 px
    {
        ui::WidgetBuilder emRoot = f.Column();
        emRoot.Pad(0).Gap(0).Offset(0, 500).Children(
            f.Label("EM").FontSize(20).Name("emLabel").W(ui::Dimension::Em(2)).H(ui::Dimension::Rem(3)),
            f.Column().Name("emBox").W(ui::Dimension::Pem(4)).H(ui::Dimension::Px(10)));
        ecs::Entity emRootE = emRoot.Spawn();
        layout.RunIfNeeded(ar, 800, 600);

        auto emLabel = ui::FindByName(ar, "emLabel");
        sdl3::FRect er = ar.GetComponent<ui::UiComputed>(emLabel.Unwrap()).Unwrap()->screen;
        std::cout << "em(2)x rem(3)y avec font 20 / root 14: " << er.w << "x" << er.h << "\n";
        assert(er.w > 39.9f && er.w < 40.1f); // 2 em × 20
        assert(er.h > 41.9f && er.h < 42.1f); // 3 rem × 14

        auto emBox = ui::FindByName(ar, "emBox");
        sdl3::FRect br = ar.GetComponent<ui::UiComputed>(emBox.Unwrap()).Unwrap()->screen;
        std::cout << "pem(4) avec parent 14: " << br.w << "\n";
        assert(br.w > 55.9f && br.w < 56.1f); // 4 pem × 14

        ui::DespawnTree(ar, emRootE);
        layout.MarkDirty();
        std::cout << "unites Em/Pem/Rem: ok\n";
    }

    // ── 8ter. widgets Radio / ScrollBar / Knob / Canvas ──────────────────────
    {
        bool radioA = false, radioB = false;
        float scrolled = -1.f;
        float knobValue = -1.f;
        bool canvasDrawn = false;

        ui::WidgetBuilder wroot = f.Column();
        wroot.Pad(8).Gap(8).Offset(420, 60).Children(
            f.Radio("grp", "Option A", true).Name("radioA").OnToggle([&](bool b) { radioA = b; }),
            f.Radio("grp", "Option B").Name("radioB").OnToggle([&](bool b) { radioB = b; }),
            f.Scrollbar(300.f, 100.f).Name("sbar").W(ui::Dimension::Px(200)).onScroll([&](float o) { scrolled = o; }),
            f.Knob(0.5f).Name("knob").OnChange([&](float v) { knobValue = v; }),
            f.Canvas([&](sdl3::Renderer &r, sdl3::FRect rect) {
                 canvasDrawn = true;
                 r.SetDrawColor(sdl3::Color::RED());
                 r.FillRect(rect);
             })
                .Name("cnv")
                .Size(50, 30));
        ecs::Entity wrootE = wroot.Spawn();
        layout.RunIfNeeded(ar, 800, 600);

        // Radio : cliquer B coche B et décoche A (exclusivité de groupe).
        auto rbE = ui::FindByName(ar, "radioB");
        sdl3::FRect rb = ar.GetComponent<ui::UiComputed>(rbE.Unwrap()).Unwrap()->screen;
        input.HandleEvent(ar, MouseDownEv(rb.x + 5, rb.y + rb.h / 2), layout);
        input.HandleEvent(ar, MouseUpEv(rb.x + 5, rb.y + rb.h / 2), layout);
        auto raC = ar.GetComponent<ui::UiRadio>(ui::FindByName(ar, "radioA").Unwrap());
        auto rbC = ar.GetComponent<ui::UiRadio>(rbE.Unwrap());
        assert(rbC.Unwrap()->checked && !raC.Unwrap()->checked);
        assert(radioB == true && radioA == false); // callbacks des deux côtés
        std::cout << "radio group exclusif: ok\n";

        // ScrollBar : drag au 3/4 de la piste → offset proche de maxOffset*0.75+.
        auto sbE = ui::FindByName(ar, "sbar");
        sdl3::FRect sbr = ar.GetComponent<ui::UiComputed>(sbE.Unwrap()).Unwrap()->screen;
        float sdx = sbr.x + sbr.w * 0.75f, sdy = sbr.y + sbr.h / 2;
        input.HandleEvent(ar, MouseDownEv(sdx, sdy), layout);
        input.HandleEvent(ar, MouseUpEv(sdx, sdy), layout);
        auto sbC = ar.GetComponent<ui::UiScrollBar>(sbE.Unwrap());
        std::cout << "scrollbar offset=" << sbC.Unwrap()->offset << " (max=" << sbC.Unwrap()->MaxOffset() << ")\n";
        assert(scrolled >= 0.f && sbC.Unwrap()->offset > 100.f); // a bien scrollé vers la fin
        std::cout << "scrollbar drag: ok\n";

        // Knob : press au centre puis drag 40px vers le haut → +0.2.
        auto knE = ui::FindByName(ar, "knob");
        sdl3::FRect knr = ar.GetComponent<ui::UiComputed>(knE.Unwrap()).Unwrap()->screen;
        float kcx = knr.x + knr.w / 2, kcy = knr.y + knr.h / 2;
        input.HandleEvent(ar, MouseDownEv(kcx, kcy), layout);
        input.HandleEvent(ar, MouseMotionEv(kcx, kcy - 40.f), layout);
        input.HandleEvent(ar, MouseUpEv(kcx, kcy - 40.f), layout);
        std::cout << "knob value=" << knobValue << " (attendu ~0.7)\n";
        assert(knobValue > 0.69f && knobValue < 0.71f);
        std::cout << "knob drag: ok\n";

        // Canvas : le callback est invoqué pendant le rendu.
        render.Run(ar, renBackend);
        assert(canvasDrawn);
        std::cout << "canvas callback: ok\n";

        ui::DespawnTree(ar, wrootE);
        layout.MarkDirty();
    }

    // ── 9. perf : 1000 widgets ───────────────────────────────────────────────
    scenes.Add("big", [&](ui::UiFactory &fac) {
        ui::WidgetBuilder col = fac.Column();
        col.Pad(4).Gap(2).Offset(0, 0).W(ui::Dimension::Px(600)).HAuto().Scrollable();
        ecs::Entity root = col.Spawn();
        for (int i = 0; i < 200; ++i) {
            fac.Row()
                .Gap(4)
                .Parent(root)
                .Children(fac.Label(String("item ").Append(i)),
                          fac.Progress(0.f, 1.f, float(i % 100) / 100.f).H(ui::Dimension::Px(6)).GrowW(),
                          fac.Button("x").FontSize(10), fac.Checkbox(i % 2 == 0), fac.Toggle(i % 3 == 0))
                .Spawn();
        }
        return std::vector{root};
    });

    auto t0 = Clock::now();
    scenes.Show("big");
    double buildMs = MsSince(t0);

    t0 = Clock::now();
    layout.RunIfNeeded(ar, 800, 600);
    double layoutMs = MsSince(t0);

    t0 = Clock::now();
    for (int i = 0; i < 1000; ++i)
        layout.RunIfNeeded(ar, 800, 600);
    double idleMs = MsSince(t0);

    std::cout << "1200 widgets: build=" << buildMs << "ms, layout=" << layoutMs << "ms, 1000 frames au repos=" << idleMs
              << "ms\n";
    assert(idleMs < layoutMs || idleMs < 1.0); // le repos doit être ~gratuit

    // ── 10. destruction de scène + CommandBuffer ECS ────────────────────────
    uint32_t aliveBefore = ar.AliveCount();
    scenes.Destroy("big");
    std::cout << "alive avant destroy=" << aliveBefore << " apres=" << ar.AliveCount() << "\n";
    assert(ar.AliveCount() < aliveBefore);

    ecs::CommandBuffer cmds;
    ar.Query<ui::UiCheckbox>([&](ecs::Entity e, ui::UiCheckbox &) { cmds.Despawn(e); });
    cmds.Flush(ar);
    int checkboxesLeft = 0;
    ar.Query<ui::UiCheckbox>([&](ecs::Entity, ui::UiCheckbox &) { checkboxesLeft++; });
    assert(checkboxesLeft == 0);
    std::cout << "CommandBuffer despawn differe: ok\n";

    std::cout << "smoke test 13 done\n";
    return 0;
}
