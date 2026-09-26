// Tests unitaires — ui::UiFactory::ColorPicker (lib/include/ui/factory.hpp).
//
// Régression : depuis la migration sdl3::Color → sdl3::FColor, le sélecteur
// divisait encore l'alpha par 255 (à l'ouverture ET à la saisie hexadécimale).
// Ouvert sur une couleur opaque, il partait d'un alpha de 0,004 : la première
// retouche de teinte rendait la couleur transparente.
#define USE_TEST

#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

#include <cstdlib>

using namespace ui;

namespace {

struct Picker {
	ecs::ArchetypeRegistry ar;
	LayoutSystem layout;
	UiFactory f{ar, layout};
	sdl3::FColor last{0.f, 0.f, 0.f, 0.f};
	int changes = 0;

	explicit Picker(sdl3::FColor initial) {
		setenv("SDL_VIDEODRIVER", "dummy", 0);
		(void)f.ColorPicker(initial, [this](sdl3::FColor c) {
			last = c;
			++changes;
		}).Spawn();
	}

	template <typename T> T *Only() {
		T *found = nullptr;
		ar.Query<T>([&found](ecs::Entity, T &component) { found = &component; });
		return found;
	}
	template <typename T> UiCallbacks *CallbacksOf() {
		UiCallbacks *found = nullptr;
		ar.Query<T, UiCallbacks>([&found](ecs::Entity, T &, UiCallbacks &cb) { found = &cb; });
		return found;
	}
};

bool Near(float a, float b) { return sdl3::Abs(a - b) < 1e-2f; }

} // namespace

TEST(ColorPicker, AnOpaqueColourOpensWithAnOpaqueAlpha) {
	Picker p(sdl3::FColor{1.f, 0.5f, 0.f, 1.f});
	UiAlphaSlider *alpha = p.Only<UiAlphaSlider>();
	ASSERT_TRUE(alpha != nullptr);
	EXPECT_TRUE(Near(alpha->alpha, 1.f));
	EXPECT_TRUE(Near(alpha->baseColor.a, 1.f));
}

TEST(ColorPicker, TouchingTheHueKeepsTheColourOpaque) {
	Picker p(sdl3::FColor{1.f, 0.5f, 0.f, 1.f});
	UiHueSlider *hue = p.Only<UiHueSlider>();
	UiCallbacks *callbacks = p.CallbacksOf<UiHueSlider>();
	ASSERT_TRUE(hue != nullptr && callbacks != nullptr && callbacks->onChange);
	hue->hue = 200.f;
	callbacks->onChange(200.f);
	EXPECT_EQ(p.changes, 1);
	EXPECT_TRUE(Near(p.last.a, 1.f));
	EXPECT_TRUE(p.last.b > p.last.r); // 200° : du côté des bleus
}

TEST(ColorPicker, AHexAlphaIsReadAsAFraction) {
	Picker p(sdl3::FColor{1.f, 1.f, 1.f, 1.f});
	UiCallbacks *callbacks = p.CallbacksOf<UiInput>();
	ASSERT_TRUE(callbacks != nullptr && callbacks->onSubmit);
	callbacks->onSubmit(String("#FF000080"));
	EXPECT_TRUE(Near(p.Only<UiAlphaSlider>()->alpha, 128.f / 255.f));
	EXPECT_TRUE(Near(p.last.a, 128.f / 255.f));
	EXPECT_TRUE(Near(p.last.r, 1.f) && Near(p.last.g, 0.f));
}

int main() { return RUN_ALL_TESTS(); }
