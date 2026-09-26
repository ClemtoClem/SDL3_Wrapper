// Tests unitaires — UiInputArea en mode « éditeur de code » (ui::) :
// gouttière de numéros de ligne, cellule de caractère mesurée, coloration
// syntaxique par morceaux, défilement horizontal qui suit le curseur.
//
// Tout est calculatoire : la largeur de cellule est posée à la main (le rendu
// la publie d'ordinaire après l'avoir mesurée sur la police à chasse fixe),
// ce qui rend les positions de clic exactes et indépendantes de toute police.
#define USE_TEST

#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

#include <cstdlib>

using namespace ui;

namespace {

constexpr float CELL = 10.f; ///< largeur de cellule imposée
constexpr float FONT = 14.f; ///< police par défaut d'un widget sans style résolu

sdl3::Event ButtonEv(Uint32 type, float x, float y) {
	sdl3::Event ev{};
	ev.raw.type = type;
	ev.raw.button.x = x;
	ev.raw.button.y = y;
	ev.raw.button.button = SDL_BUTTON_LEFT;
	ev.raw.button.clicks = 1;
	return ev;
}

struct Harness {
	ecs::ArchetypeRegistry ar;
	LayoutSystem layout;
	InputSystem input;
	UiFactory f{ar, layout};
	ecs::Entity area{};

	Harness() { setenv("SDL_VIDEODRIVER", "dummy", 0); }

	/// Zone de code 400×200 en (0,0), texte donné, cellule imposée.
	void Spawn(const String &text, bool lineNumbers = true, float width = 400.f, float height = 200.f) {
		auto b = f.InputArea();
		b.Anchor(Anchor::TopLeft).Offset(0.f, 0.f).Size(width, height);
		b.LineNumbers(lineNumbers).Monospace();
		area = b.Spawn();
		UiInputArea &a = Area();
		a.text = text;
		a.cellWidth = CELL;
		layout.Run(ar, 800.f, 600.f);
	}
	UiInputArea &Area() { return *ar.GetComponent<UiInputArea>(area).Unwrap(); }
	UiRect &Rect() { return *ar.GetComponent<UiRect>(area).Unwrap(); }
	void Click(float x, float y) {
		input.HandleEvent(ar, ButtonEv(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y), layout);
		input.HandleEvent(ar, ButtonEv(SDL_EVENT_MOUSE_BUTTON_UP, x, y), layout);
	}
	void Tick() {
		input.Tick(ar, layout, 1.f / 60.f);
		layout.Run(ar, 800.f, 600.f);
	}
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// 1. Gouttière : absente sans numéros, au moins trois chiffres, s'élargit
//    au-delà de 999 lignes.
// ─────────────────────────────────────────────────────────────────────────
TEST(CodeArea, TheGutterIsSizedForTheLineCount) {
	UiInputArea plain;
	EXPECT_EQ(AreaGutterWidth(plain, 5000, CELL), 0.f);

	UiInputArea code;
	code.lineNumbers = true;
	const float three = AreaGutterWidth(code, 1, CELL);
	EXPECT_EQ(three, 3.f * CELL + 16.f);
	EXPECT_EQ(AreaGutterWidth(code, 999, CELL), three); // 99 → 100 ne fait pas sauter la marge
	EXPECT_EQ(AreaGutterWidth(code, 1000, CELL), 4.f * CELL + 16.f);

	const sdl3::FRect box = AreaTextBox(sdl3::FRect{10.f, 20.f, 300.f, 100.f}, three);
	EXPECT_EQ(box.x, 10.f + three);
	EXPECT_EQ(box.w, 300.f - three);
}

// ─────────────────────────────────────────────────────────────────────────
// 2. Un clic dans le texte place le curseur sous la cellule visée — la
//    gouttière est bien décomptée, et la cellule mesurée utilisée.
// ─────────────────────────────────────────────────────────────────────────
TEST(CodeArea, AClickLandsOnTheCellUnderThePointerPastTheGutter) {
	Harness h;
	h.Spawn(String("let answer = 42\nprint(answer)"));
	const float gutter = AreaGutterWidth(h.Area(), 2, CELL);
	const float lh = LineHeightApprox(FONT);

	// Colonne 4 de la ligne 0 : début de « answer ».
	h.Click(gutter + 8.f + 4.f * CELL + 1.f, 4.f + lh * 0.5f);
	EXPECT_TRUE(h.Area().focused);
	EXPECT_EQ(h.Area().cursor, size_t(4));

	// Colonne 6 de la ligne 1 : « answer » de print(answer).
	h.Click(gutter + 8.f + 6.f * CELL + 1.f, 4.f + lh * 1.5f);
	EXPECT_EQ(h.Area().cursor, size_t(16 + 6));
}

TEST(CodeArea, AClickInTheGutterPutsTheCursorAtTheStartOfThatLine) {
	Harness h;
	h.Spawn(String("alpha\nbeta\ngamma"));
	const float lh = LineHeightApprox(FONT);
	h.Click(5.f, 4.f + lh * 2.5f); // dans la gouttière, ligne 2
	EXPECT_EQ(h.Area().cursor, size_t(11)); // début de « gamma »
}

// ─────────────────────────────────────────────────────────────────────────
// 3. La largeur de contenu compte la gouttière (sinon la dernière colonne
//    d'une ligne longue ne serait jamais atteignable au défilement).
// ─────────────────────────────────────────────────────────────────────────
TEST(CodeArea, TheContentWidthIncludesTheGutter) {
	Harness h;
	String longLine;
	for (int i = 0; i < 80; ++i)
		longLine.Concat("x");
	h.Spawn(longLine);
	h.Tick();
	const float gutter = AreaGutterWidth(h.Area(), 1, CELL);
	EXPECT_EQ(h.Rect().content.x, 80.f * CELL + 16.f + gutter);
}

// ─────────────────────────────────────────────────────────────────────────
// 4. Le curseur en fin de ligne longue fait défiler horizontalement.
// ─────────────────────────────────────────────────────────────────────────
TEST(CodeArea, TheViewFollowsTheCursorHorizontally) {
	Harness h;
	String longLine;
	for (int i = 0; i < 120; ++i)
		longLine.Concat("y");
	h.Spawn(longLine);
	h.Click(100.f, 8.f);
	EXPECT_TRUE(h.Area().focused);
	h.Area().cursor = h.Area().selectionAnchor = longLine.size();
	h.Tick();
	const float gutter = AreaGutterWidth(h.Area(), 1, CELL);
	const float caretX = 120.f * CELL - h.Rect().scroll.x;
	EXPECT_TRUE(h.Rect().scroll.x > 0.f);
	EXPECT_TRUE(caretX <= 400.f - gutter); // le curseur est dans la vue

	h.Area().cursor = h.Area().selectionAnchor = 0;
	h.Tick();
	EXPECT_EQ(h.Rect().scroll.x, 0.f);
}

// ─────────────────────────────────────────────────────────────────────────
// 5. Le rendu ne colore QUE les lignes visibles — un fichier de 2000
//    lignes ne coûte pas 2000 appels par image.
// ─────────────────────────────────────────────────────────────────────────
namespace {
class NullBackend final : public IUiRenderBackend {
public:
	bool SetDrawColor(sdl3::Color) override { return true; }
	bool SetDrawColor(sdl3::FColor) override { return true; }
	bool SetBlendMode(sdl3::BlendMode) override { return true; }
	bool SetClipRect(const sdl3::Rect &) override { return true; }
	bool ClearClipRect() override { return true; }
	[[nodiscard]] sdl3::FColor GetDrawColorFloat() const override { return sdl3::FColor::WHITE(); }
	bool DrawLine(float, float, float, float) override { return true; }
	bool DrawLine(sdl3::FPoint, sdl3::FPoint) override { return true; }
	bool DrawRect(const sdl3::FRect &) override { return true; }
	bool FillRect(const sdl3::FRect &) override { return true; }
	bool DrawRoundedRect(const sdl3::FRect &, const math::Corners &) override { return true; }
	bool FillRoundedRect(const sdl3::FRect &, const math::Corners &) override { return true; }
	bool DrawCircle(sdl3::FPoint, float) override { return true; }
	bool FillCircle(sdl3::FPoint, float) override { return true; }
	bool DrawArc(sdl3::FPoint, float, float, float) override { return true; }
	bool DrawPie(sdl3::FPoint, float, float, float) override { return true; }
	bool DrawPolygon(std::span<const sdl3::FPoint>) override { return true; }
	bool FillPolygon(std::span<const sdl3::FPoint>) override { return true; }
	bool RenderGeometry(std::span<const sdl3::Vertex>, std::span<const int>) override { return true; }
	bool Render(const sdl3::Texture &, const sdl3::FRect &) override { return true; }
	bool Render(const sdl3::Texture &, const sdl3::FRect &, const sdl3::FRect &) override { return true; }
};
} // namespace

TEST(CodeArea, OnlyVisibleLinesAreHighlighted) {
	Harness h;
	String text;
	for (int i = 0; i < 2000; ++i)
		text.Concat(String::Format("line %d\n", i));
	int calls = 0;
	std::vector<String> seen;
	h.Spawn(text, true, 400.f, 100.f);
	h.Area().highlighter = [&](const String &line, std::vector<UiTextSpan> &out) {
		++calls;
		seen.push_back(line);
		out.push_back(UiTextSpan{0, 4, sdl3::FColor::WHITE()});
	};
	RenderSystem render;
	NullBackend backend;
	render.Run(h.ar, backend);
	const int visible = int(100.f / LineHeightApprox(FONT)) + 2;
	EXPECT_TRUE(calls > 0);
	EXPECT_TRUE(calls <= visible);
	EXPECT_TRUE(seen.front() == String("line 0"));
}

// ─────────────────────────────────────────────────────────────────────────
// 6. La coloration reçoit la ligne AFFICHÉE : tabulations développées.
// ─────────────────────────────────────────────────────────────────────────
TEST(CodeArea, TheHighlighterSeesTabsExpanded) {
	Harness h;
	h.Spawn(String("\tx = 1"));
	String got;
	h.Area().highlighter = [&](const String &line, std::vector<UiTextSpan> &) { got = line; };
	RenderSystem render;
	NullBackend backend;
	render.Run(h.ar, backend);
	EXPECT_TRUE(got == NormalizeDisplayText(String("\tx = 1")));
	EXPECT_TRUE(got.Find("\t") == String::NPOS);
}

// ─────────────────────────────────────────────────────────────────────────
// 7. Un journal suit la fin du texte ; un document de code reste en haut.
// ─────────────────────────────────────────────────────────────────────────
TEST(CodeArea, ALogFollowsItsTailButACodeDocumentStaysAtTheTop) {
	String text;
	for (int i = 0; i < 60; ++i)
		text.Concat(String::Format("ligne %d\n", i));

	Harness log;
	log.Spawn(text, false, 400.f, 100.f);
	log.Tick();
	log.Tick();
	EXPECT_TRUE(log.Rect().scroll.y > 0.f); // la fin du journal est visible

	Harness code;
	code.Spawn(text, true, 400.f, 100.f);
	code.Area().followTail = false; // ce que pose UiFactory::CodeEditor
	code.Tick();
	code.Tick();
	EXPECT_EQ(code.Rect().scroll.y, 0.f); // ouvert en haut, ligne 1 visible
}

int main() { return RUN_ALL_TESTS(); }
