// Tests unitaires — game_editor : mode de capture de la souris pendant une
// partie (libre / capturée / confinée), choisi par les scripts ou basculé au
// clavier. Sans fenêtre : on vérifie la décision du runtime, que l'hôte
// applique ensuite à la fenêtre (cf. App::Run).
#define USE_TEST

#include "core/test.hpp"

#include "../examples/game_editor_demo/document/project_files.hpp"
#include "../examples/game_editor_demo/engine/runtime.hpp"

using namespace game_editor;
using Mode = Runtime::MouseMode;

namespace {

struct Harness {
	ecs::ArchetypeRegistry registry;
	Runtime runtime{registry};

	Harness() {
		runtime.OpenProject(files::MakeBlankProject(String("Souris")));
		runtime.ActiveScene()->gameplayScript = String("var modes = []\n"
													   "class Souris extends Scene {\n"
													   "    fn on_mouse_mode(mode) { modes.append(mode) }\n"
													   "}\n");
	}

	data::script::Value Run(const char *source) {
		auto result = runtime.GameplayVm().Run(StringView(source));
		if (result.IsError()) {
			test::ReportFailure(__FILE__, __LINE__, result.Error().Format().CStr());
			return data::script::Value::Nil();
		}
		return result.Value();
	}
	String Error(const char *source) {
		auto result = runtime.GameplayVm().Run(StringView(source));
		return result.IsOk() ? String() : result.Error().Format();
	}
};

} // namespace

TEST(MouseMode, DefaultsFollowTheView) {
	Harness h;
	EXPECT_TRUE(h.runtime.EffectiveMouseMode() == Mode::FREE); // hors partie
	h.runtime.Play();
	EXPECT_TRUE(h.runtime.EffectiveMouseMode() == Mode::FREE); // vue de l'éditeur
	h.runtime.SetHostFullscreen(true);
	EXPECT_TRUE(h.runtime.EffectiveMouseMode() == Mode::CAPTURED); // plein écran
	h.runtime.Stop();
	EXPECT_TRUE(h.runtime.EffectiveMouseMode() == Mode::FREE);
}

TEST(MouseMode, ScriptsChooseAndAreNotified) {
	Harness h;
	h.runtime.SetHostFullscreen(true);
	h.runtime.Play();
	// Libérer la souris pour cliquer l'interface du jeu.
	h.Run("input.set_mouse_mode(\"free\")");
	EXPECT_EQ(h.Run("return input.mouse_mode()").ToDisplayString(), "free");
	EXPECT_TRUE(h.runtime.EffectiveMouseMode() == Mode::FREE);
	h.Run("input.set_mouse_mode(\"confined\")");
	EXPECT_TRUE(h.runtime.EffectiveMouseMode() == Mode::CONFINED);
	EXPECT_EQ(h.Run("return input.toggle_mouse_mode()").ToDisplayString(), "captured");
	// Le jeu est prévenu au début de l'image suivante, une fois par changement net.
	EXPECT_EQ(h.Run("return modes").ToDisplayString(), "[]");
	h.runtime.Update(1.f / 60.f);
	EXPECT_EQ(h.Run("return modes").ToDisplayString(), "[captured]");
	h.runtime.Update(1.f / 60.f);
	EXPECT_EQ(h.Run("return modes").ToDisplayString(), "[captured]");
	EXPECT_TRUE(h.Error("input.set_mouse_mode(\"magique\")").Contains("inconnu"));
	h.runtime.Stop();
}

TEST(MouseMode, ToggleKeyIsConfigurableAndResetPerGame) {
	Harness h;
	h.runtime.Play();
	ASSERT_TRUE(h.runtime.MouseToggleKey().IsSome());
	EXPECT_TRUE(h.runtime.MouseToggleKey().Unwrap() == SDLK_F7);
	EXPECT_TRUE(h.runtime.ToggleMouseCapture() == Mode::CAPTURED); // la touche, côté hôte
	EXPECT_TRUE(h.runtime.ToggleMouseCapture() == Mode::FREE);
	h.Run("input.set_mouse_toggle_key(\"tab\")");
	EXPECT_TRUE(h.runtime.MouseToggleKey().Unwrap() == SDLK_TAB);
	h.Run("input.set_mouse_toggle_key(\"m\")");
	EXPECT_TRUE(h.runtime.MouseToggleKey().Unwrap() == SDLK_M);
	h.Run("input.set_mouse_toggle_key(nil)");
	EXPECT_TRUE(h.runtime.MouseToggleKey().IsNone());
	EXPECT_TRUE(h.Error("input.set_mouse_toggle_key(\"hyper\")").Contains("touche inconnue"));
	h.runtime.Stop();
	// Une nouvelle partie repart des réglages par défaut.
	h.runtime.Play();
	EXPECT_TRUE(h.runtime.MouseToggleKey().IsSome() && h.runtime.EffectiveMouseMode() == Mode::FREE);
	h.runtime.Stop();
}

int main() { return RUN_ALL_TESTS(); }
