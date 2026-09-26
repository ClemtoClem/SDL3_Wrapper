// Tests unitaires — mesure de texte de ui:: avec codes d'échappement.
//
// Le défaut corrigé ici : SDL_ttf ne traite pas `\n`, `\r` et `\t` de la même
// façon selon qu'on MESURE ou qu'on DESSINE. `TTF_GetStringSize` compte chacun
// comme un glyphe MANQUANT (une boîte d'environ 10 px) et reste sur une seule
// ligne, alors que l'objet `TTF_Text` dessiné, lui, casse bien la ligne sur
// `\n`. Le layout réservait donc une boîte d'une ligne, trop large, dans
// laquelle le texte débordait par le bas.
//
// Ces tests verrouillent les deux bouts : la normalisation (ce qui est
// dessiné) et la mesure par lignes (ce qui est réservé), avec l'heuristique
// sans police ET avec une vraie police TTF quand elle est disponible.
#define USE_TEST

#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

#include <cstdlib>
#include <iostream>

using namespace ui;

namespace {

constexpr float FS = 16.f;

[[nodiscard]] bool Near(float a, float b, float eps = 1e-3f) {
	return (a - b) < eps && (b - a) < eps;
}

/// Mesure de ligne factice : une largeur proportionnelle au nombre de
/// CARACTÈRES, pour que les attentes des tests soient calculables à la main.
[[nodiscard]] std::function<sdl3::FPoint(const String&, float)> CountingMeasure() {
	return [](const String& line, float fs) -> sdl3::FPoint {
		return {float(CharCount(line)) * 10.f, fs};
	};
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// 1. Normalisation : ce qui part réellement à SDL_ttf
// ─────────────────────────────────────────────────────────────────────────

TEST(TextEscapes, PlainTextIsReturnedUnchanged) {
	const String text("Bonjour, éditeur !");
	EXPECT_TRUE(NormalizeDisplayText(text) == text);
}

TEST(TextEscapes, CarriageReturnsBecomeASingleLineBreak) {
	// `\r\n` est UN saut de ligne, pas deux — sinon tout fichier Windows
	// collé dans un libellé gagnerait une ligne vide sur deux.
	EXPECT_TRUE(NormalizeDisplayText(String("a\r\nb")) == String("a\nb"));
	// `\r` seul est un saut de ligne aussi (vieux fichiers Mac, terminaux).
	EXPECT_TRUE(NormalizeDisplayText(String("a\rb")) == String("a\nb"));
	EXPECT_TRUE(NormalizeDisplayText(String("a\n\rb")) == String("a\n\nb"));
}

TEST(TextEscapes, TabsExpandToTheNextTabStop) {
	// Taquets tous les 4 caractères : « a » occupe la colonne 0, la
	// tabulation comble donc jusqu'à la colonne 4 (3 espaces).
	EXPECT_TRUE(NormalizeDisplayText(String("a\tb")) == String("a   b"));
	EXPECT_TRUE(NormalizeDisplayText(String("abcd\tb")) == String("abcd    b"));
	EXPECT_TRUE(NormalizeDisplayText(String("\tx")) == String("    x"));
	// Le compteur de colonne repart à zéro à chaque ligne.
	EXPECT_TRUE(NormalizeDisplayText(String("abc\na\tb")) == String("abc\na   b"));
	// Largeur de taquet configurable.
	EXPECT_TRUE(NormalizeDisplayText(String("a\tb"), 2) == String("a b"));
}

TEST(TextEscapes, TabStopsCountCharactersNotBytes) {
	// « é » fait DEUX octets en UTF-8 mais UNE colonne : compter les octets
	// décalerait les taquets de tout texte accentué.
	EXPECT_TRUE(NormalizeDisplayText(String("é\tb")) == String("é   b"));
	EXPECT_TRUE(NormalizeDisplayText(String("éàü\tb")) == String("éàü b"));
}

TEST(TextEscapes, OtherControlCharactersAreDropped) {
	// SDL_ttf leur dessine une boîte « glyphe manquant » : ils n'ont rien à
	// faire dans un libellé.
	// Littéraux découpés : `"\x0Bb"` serait lu comme UN échappement
	// hexadécimal de trois chiffres (0, B, b sont tous hexadécimaux).
	EXPECT_TRUE(NormalizeDisplayText(String("a\x07"
											"\x0B"
											"b")) == String("ab"));
	EXPECT_TRUE(NormalizeDisplayText(String("a\x7F"
											"b")) == String("ab"));
	// …mais les caractères accentués, eux, ne sont PAS des caractères de
	// contrôle (leurs octets UTF-8 valent pourtant plus de 0x7F).
	EXPECT_TRUE(NormalizeDisplayText(String("déjà vu")) == String("déjà vu"));
}

// ─────────────────────────────────────────────────────────────────────────
// 2. Mesure par lignes
// ─────────────────────────────────────────────────────────────────────────

TEST(TextMetrics, ASingleLineMeasuresLikeTheHookItself) {
	const TextMetrics m = MeasureTextBlock(String("abc"), FS, CountingMeasure());
	EXPECT_TRUE(Near(m.width, 30.f));
	EXPECT_EQ(m.lineCount, size_t(1));
	EXPECT_TRUE(Near(m.height, FS));
}

TEST(TextMetrics, NewlinesGiveTheWidestLineAndStackTheHeight) {
	// C'est LE cas qui était faux : « abc\nabcdef » mesurait 9 caractères de
	// large sur une ligne, au lieu de 6 de large sur deux lignes.
	const TextMetrics m = MeasureTextBlock(String("abc\nabcdef"), FS, CountingMeasure());
	EXPECT_TRUE(Near(m.width, 60.f)); // la ligne la plus large, pas leur somme
	EXPECT_EQ(m.lineCount, size_t(2));
	EXPECT_TRUE(Near(m.height, 2.f * FS));
}

TEST(TextMetrics, ATrailingNewlineReservesAnExtraLine) {
	const TextMetrics m = MeasureTextBlock(String("abc\n"), FS, CountingMeasure());
	EXPECT_EQ(m.lineCount, size_t(2));
	EXPECT_TRUE(Near(m.width, 30.f));
	// La ligne vide a une largeur nulle mais garde une HAUTEUR de ligne.
	EXPECT_TRUE(Near(m.height, 2.f * FS));
}

TEST(TextMetrics, EmptyTextStillOccupiesOneLine) {
	const TextMetrics m = MeasureTextBlock(String(), FS, CountingMeasure());
	EXPECT_EQ(m.lineCount, size_t(1));
	EXPECT_TRUE(Near(m.width, 0.f));
	EXPECT_TRUE(m.height > 0.f);
}

TEST(TextMetrics, CrLfCountsAsOneLineBreakAndTabsWiden) {
	const TextMetrics crlf = MeasureTextBlock(String("a\r\nb"), FS, CountingMeasure());
	EXPECT_EQ(crlf.lineCount, size_t(2));
	EXPECT_TRUE(Near(crlf.width, 10.f));

	// « a\tb » -> « a   b » : 5 caractères, pas 3.
	const TextMetrics tabbed = MeasureTextBlock(String("a\tb"), FS, CountingMeasure());
	EXPECT_TRUE(Near(tabbed.width, 50.f));
	EXPECT_EQ(tabbed.lineCount, size_t(1));
}

TEST(TextMetrics, ControlCharactersDoNotAddWidth) {
	const TextMetrics m = MeasureTextBlock(String("ab\x07"), FS, CountingMeasure());
	EXPECT_TRUE(Near(m.width, 20.f)); // 2 caractères, pas 3
}

TEST(TextMetrics, TheDefaultHeuristicCountsCharactersNotBytes) {
	LayoutSystem layout;
	// « Réglages » : 8 caractères, 10 octets.
	const sdl3::FPoint accented = layout.MeasureText(String("Réglages"), FS);
	const sdl3::FPoint plain = layout.MeasureText(String("Réglages"), FS);
	EXPECT_TRUE(Near(accented.x, plain.x));
	EXPECT_TRUE(Near(accented.x, 8.f * CharWidthApprox(FS)));
}

// ─────────────────────────────────────────────────────────────────────────
// 3. Effet réel sur la mise en page
// ─────────────────────────────────────────────────────────────────────────

TEST(TextLayout, AMultiLineLabelIsAsTallAsItsLines) {
	ecs::ArchetypeRegistry ar;
	LayoutSystem layout;
	UiFactory f(ar, layout);

	auto one = f.Label(String("Ligne"));
	one.Anchor(Anchor::TopLeft).Offset(0.f, 0.f);
	ecs::Entity single = one.Spawn();

	auto three = f.Label(String("Ligne\nLigne\nLigne"));
	three.Anchor(Anchor::TopLeft).Offset(0.f, 100.f);
	ecs::Entity multi = three.Spawn();

	layout.Run(ar, 800.f, 600.f);

	const sdl3::FRect a = ar.GetComponent<UiComputed>(single).Unwrap()->screen;
	const sdl3::FRect b = ar.GetComponent<UiComputed>(multi).Unwrap()->screen;
	// Trois fois plus haut…
	EXPECT_TRUE(Near(b.h, a.h * 3.f, 0.5f));
	// …et PAS plus large : c'est la ligne la plus large qui compte, alors que
	// la mesure fautive additionnait les trois plus deux glyphes manquants.
	EXPECT_TRUE(Near(b.w, a.w, 0.5f));
}

TEST(TextLayout, ATabbedLabelIsWiderThanTheSameTextWithoutTheTab) {
	ecs::ArchetypeRegistry ar;
	LayoutSystem layout;
	UiFactory f(ar, layout);

	auto plain = f.Label(String("ab"));
	plain.Anchor(Anchor::TopLeft);
	ecs::Entity a = plain.Spawn();
	auto tabbed = f.Label(String("a\tb"));
	tabbed.Anchor(Anchor::TopLeft).Offset(0.f, 50.f);
	ecs::Entity b = tabbed.Spawn();

	layout.Run(ar, 800.f, 600.f);
	const float wa = ar.GetComponent<UiComputed>(a).Unwrap()->screen.w;
	const float wb = ar.GetComponent<UiComputed>(b).Unwrap()->screen.w;
	EXPECT_TRUE(wb > wa);
}

// ─────────────────────────────────────────────────────────────────────────
// 4. Avec une VRAIE police : mesuré == dessiné
// ─────────────────────────────────────────────────────────────────────────

TEST(TextMetrics, RealFontMeasurementMatchesWhatIsDrawn) {
	setenv("SDL_VIDEODRIVER", "dummy", 0);
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
	auto ttf = sdl3::TtfContext::Create();
	if (!sdl || !ttf) {
		std::cout << "(police indisponible : test ignoré)\n";
		return;
	}
	auto font = sdl3::Font::Open(String("assets/fonts/DejaVuSans.ttf"), FS);
	if (!font) {
		std::cout << "(assets/fonts/DejaVuSans.ttf absent : test ignoré)\n";
		return;
	}

	// La mesure d'une ligne, telle qu'une application la branche.
	auto measureLine = [&font](const String& line, float fs) -> sdl3::FPoint {
		(void)fs;
		if (auto sz = font.Value().Measure(line); sz.IsSome())
			return {float(sz.Unwrap().x), float(sz.Unwrap().y)};
		return {0.f, 0.f};
	};

	const sdl3::FPoint oneLine = measureLine(String("abc"), FS);
	ASSERT_TRUE(oneLine.x > 0.f);

	// Mesure NAÏVE (celle d'avant) : SDL_ttf compte le `\n` comme un glyphe
	// manquant et reste sur une ligne.
	const sdl3::FPoint naive = measureLine(String("abc\nabc"), FS);
	EXPECT_TRUE(naive.x > oneLine.x * 1.5f);	 // beaucoup trop large
	EXPECT_TRUE(Near(naive.y, oneLine.y, 0.5f)); // et une seule ligne de haut

	// Mesure corrigée : la largeur d'UNE ligne, la hauteur de DEUX.
	const TextMetrics fixed = MeasureTextBlock(String("abc\nabc"), FS, measureLine);
	EXPECT_TRUE(Near(fixed.width, oneLine.x, 0.5f));
	EXPECT_EQ(fixed.lineCount, size_t(2));
	EXPECT_TRUE(Near(fixed.height, oneLine.y * 2.f, 0.5f));

	// Et elle correspond à ce que SDL_ttf DESSINE réellement : un objet
	// TTF_Text de la forme normalisée. C'est la propriété qui compte — la
	// place réservée est la place occupée.
	auto win = sdl3::Window::Create(u8"metrics", 64, 64, 0);
	if (!win)
		return; // pas d'affichage : la partie mesure ci-dessus suffit
	auto renderer = sdl3::Renderer::Create(win.Value());
	if (!renderer)
		return;
	auto engine = sdl3::TextEngine::Create(renderer.Value());
	if (!engine)
		return;
	for (const char* raw : {"abc\nabc", "a\r\nb", "a\tb", "ligne\x07"}) {
		const String normalized = NormalizeDisplayText(String(raw));
		auto text = sdl3::Text::Create(engine.Value(), font.Value(), normalized);
		ASSERT_TRUE(text.IsOk());
		auto drawn = text.Value().GetSize();
		ASSERT_TRUE(drawn.IsSome());
		const TextMetrics measured = MeasureTextBlock(String(raw), FS, measureLine);
		EXPECT_TRUE(Near(measured.width, float(drawn.Unwrap().x), 1.f));
		EXPECT_TRUE(Near(measured.height, float(drawn.Unwrap().y), 1.f));
	}
}

// ─────────────────────────────────────────────────────────────────────────
// 5. Grille d'un champ éditable : curseur et clics sur le texte DESSINÉ
// ─────────────────────────────────────────────────────────────────────────

TEST(TextColumns, TabsOccupySeveralCellsAndAccentsOnlyOne) {
	// « a\tb » est dessiné « a   b » : après la tabulation, on est en
	// colonne 4, et « b » occupe la cinquième cellule.
	EXPECT_EQ(DisplayColumns(String("a\tb")), size_t(5));
	EXPECT_EQ(DisplayColumn(String("a\tb"), 2), size_t(4)); // juste après la tabulation
	// « é » pèse deux octets mais occupe une seule cellule.
	EXPECT_EQ(DisplayColumns(String("été")), size_t(3));
	EXPECT_EQ(DisplayColumn(String("été"), 2), size_t(1)); // après le « é »
	// Un caractère de contrôle n'est pas dessiné : il n'occupe rien.
	EXPECT_EQ(DisplayColumns(String("a\x07" "b")), size_t(2));
}

TEST(TextColumns, ColumnToByteOffsetIsTheInverseAndLandsOnCharacterBoundaries) {
	const String line("a\tbé");
	for (size_t byte = 0; byte <= line.size(); ++byte) {
		// Un décalage en MILIEU de caractère n'existe pas : on ne teste que
		// les frontières.
		if (byte < line.size() && (static_cast<unsigned char>(line[byte]) & 0xC0) == 0x80)
			continue;
		const size_t column = DisplayColumn(line, byte);
		EXPECT_EQ(ColumnToByteOffset(line, column), byte);
	}
	// Cliquer DANS une tabulation (qui s'étend des colonnes 1 à 4) retombe
	// sur son bord le plus proche, jamais entre deux octets d'un caractère.
	EXPECT_EQ(ColumnToByteOffset(line, 2), size_t(1)); // plus près du début
	EXPECT_EQ(ColumnToByteOffset(line, 3), size_t(2)); // plus près de la fin
	EXPECT_EQ(ColumnToByteOffset(line, 99), line.size());
}

TEST(TextColumns, ACaretInATabbedLineSitsWhereTheTextIsDrawn) {
	// Régression : le curseur se plaçait à `octets * largeur de cellule`,
	// alors que le texte dessiné développe la tabulation — il tombait donc
	// AVANT le texte au lieu d'être dedans.
	const String line("a\tb");
	const String drawn = NormalizeDisplayText(line);
	// La colonne du curseur en fin de ligne doit être la largeur du texte
	// réellement dessiné.
	EXPECT_EQ(DisplayColumn(line, line.size()), CharCount(drawn));
}

// Le texte d'un widget est dessiné À SA TAILLE : RenderSystem::FontAt rend
// une copie redimensionnée de la police (créée une fois), sans toucher la
// police partagée — dont la taille servait jusque-là à TOUS les widgets.
TEST(TextMetrics, EachFontSizeGetsItsOwnResizedCopy) {
	setenv("SDL_VIDEODRIVER", "dummy", 0);
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
	auto ttf = sdl3::TtfContext::Create();
	if (!sdl || !ttf) {
		std::cout << "(police indisponible : test ignoré)\n";
		return;
	}
	auto font = sdl3::Font::Open(String("assets/fonts/DejaVuSans.ttf"), 14.f);
	if (!font) {
		std::cout << "(assets/fonts/DejaVuSans.ttf absent : test ignoré)\n";
		return;
	}
	sdl3::Font &base = font.Value();
	RenderSystem render;
	EXPECT_TRUE(render.FontAt(&base, 14.f) == &base); // déjà à la bonne taille

	sdl3::Font *big = render.FontAt(&base, 20.f);
	ASSERT_TRUE(big != nullptr && big != &base);
	EXPECT_TRUE(Near(big->PointSize(), 20.f, 0.01f));
	EXPECT_TRUE(render.FontAt(&base, 20.f) == big);        // gardée, pas recréée
	EXPECT_TRUE(Near(base.PointSize(), 14.f, 0.01f));      // l'originale intacte

	const auto small = base.Measure(String("Inspecteur"));
	const auto large = big->Measure(String("Inspecteur"));
	ASSERT_TRUE(small.IsSome() && large.IsSome());
	// Largeur proportionnelle à la taille (au lissage près).
	EXPECT_TRUE(Near(float(large.Unwrap().x) / float(small.Unwrap().x), 20.f / 14.f, 0.08f));
}

// ─────────────────────────────────────────────────────────────────────────
// Largeur d'affichage : UTF-8, UTF-16 et UTF-32 donnent la même place
// ─────────────────────────────────────────────────────────────────────────

TEST(DisplayWidth, WideAndZeroWidthCharactersAreWeighted) {
	EXPECT_EQ(CodepointCells(U'a'), 1);
	EXPECT_EQ(CodepointCells(U'é'), 1);
	EXPECT_EQ(CodepointCells(U'日'), 2);
	EXPECT_EQ(CodepointCells(U'😀'), 2);
	EXPECT_EQ(CodepointCells(U'\u0301'), 0); // accent aigu combinant
	EXPECT_EQ(CodepointCells(U'\u200D'), 0); // liant sans chasse
	EXPECT_EQ(DisplayCells(String("Réglages")), size_t(8));
	EXPECT_EQ(DisplayCells(String("日本")), size_t(4));
	EXPECT_EQ(DisplayCells(String("e\xCC\x81")), size_t(1)); // e + accent combinant
}

TEST(DisplayWidth, Utf16SurrogatePairsCountAsOneCharacter) {
	const std::u16string emoji = u"a😀b";
	EXPECT_EQ(emoji.size(), size_t(4)); // la paire de substitution pèse deux unités
	EXPECT_EQ(CharCount(std::u16string_view(emoji)), size_t(3));
	EXPECT_EQ(DisplayCells(std::u16string_view(emoji)), size_t(4));
	// Moitié isolée : un caractère de remplacement, pas une lecture hors borne.
	const std::u16string lone{u'x', char16_t(0xD83D)};
	EXPECT_EQ(CharCount(std::u16string_view(lone)), size_t(2));
}

TEST(DisplayWidth, TheThreeEncodingsMeasureTheSame) {
	const String utf8("Nœud 日本 😀 é");
	const std::u16string utf16 = utf8.ToUtf16();
	const std::u32string utf32 = utf8.ToUtf32();
	EXPECT_EQ(DisplayCells(utf8), DisplayCells(std::u16string_view(utf16)));
	EXPECT_EQ(DisplayCells(utf8), DisplayCells(std::u32string_view(utf32)));
	EXPECT_EQ(CharCount(std::u16string_view(utf16)), CharCount(std::u32string_view(utf32)));

	LayoutSystem layout; // heuristique par défaut, sans police
	const sdl3::FPoint a = layout.MeasureText(utf8, 14.f);
	const sdl3::FPoint b = layout.MeasureText(std::u16string_view(utf16), 14.f);
	const sdl3::FPoint c = layout.MeasureText(std::u32string_view(utf32), 14.f);
	EXPECT_TRUE(Near(a.x, b.x) && Near(a.x, c.x));
	EXPECT_TRUE(Near(a.y, b.y) && Near(a.y, c.y));
	// Un idéogramme réserve la place de deux lettres latines.
	EXPECT_TRUE(Near(layout.MeasureText(String("日"), 14.f).x, layout.MeasureText(String("ab"), 14.f).x));
}

TEST(DisplayWidth, TheEditingGridUsesTheSameCells) {
	const String line("a日b");
	// a (1) + 日 (2) : « b » commence à la colonne 3.
	EXPECT_EQ(DisplayColumn(line, line.Find("b")), size_t(3));
	// Un clic sur la seconde moitié de l'idéogramme retombe sur une frontière.
	const size_t offset = ColumnToByteOffset(line, 2);
	EXPECT_TRUE(offset == line.Find("日") || offset == line.Find("b"));
}

int main() {
	return RUN_ALL_TESTS();
}
