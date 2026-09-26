// Tests unitaires — modes de débordement d'une chaîne trop large pour son
// widget (cf. TextOverflow, components.hpp) : rognage, points de suspension,
// barre horizontale, retour à la ligne (aligné ou justifié) et défilement
// automatique.
//
// Aucun GPU : la géométrie vient du layout, et le rendu est observé avec un
// backend d'enregistrement qui retient les rectangles peints et le clip actif.
#define USE_TEST

#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

#include <cstdlib>

using namespace ui;

namespace {

constexpr float FS = 14.f;

[[nodiscard]] bool Near(float a, float b, float eps = 1e-3f) {
	return (a - b) < eps && (b - a) < eps;
}

/// Mesure d'une ligne prévisible : 10 px par caractère, 20 px de hauteur.
/// Les attentes des tests se calculent alors de tête.
[[nodiscard]] std::function<sdl3::FPoint(const String&, float)> TenPxPerChar() {
	return [](const String& line, float) -> sdl3::FPoint {
		return {float(CharCount(line)) * 10.f, 20.f};
	};
}

struct Harness {
	ecs::ArchetypeRegistry ar;
	LayoutSystem layout;
	InputSystem input;
	UiFactory f{ar, layout};

	Harness() {
		setenv("SDL_VIDEODRIVER", "dummy", 0);
		layout.measureText = TenPxPerChar();
	}
	[[nodiscard]] const UiComputed& Computed(ecs::Entity e) {
		return *ar.GetComponent<UiComputed>(e).Unwrap();
	}
	[[nodiscard]] const UiRect& Rect(ecs::Entity e) { return *ar.GetComponent<UiRect>(e).Unwrap(); }
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// 1. Découpe par mots, mesurée
// ─────────────────────────────────────────────────────────────────────────

TEST(TextWrap, BreaksOnSpacesAndKeepsWordsWhole) {
	// 10 px par caractère, largeur 100 px : « un deux » (8 caractères avec
	// l'espace) tient, « un deux trois » non.
	const std::vector<WrappedLine> lines =
		WrapTextToWidth(String("un deux trois quatre"), FS, 100.f, TenPxPerChar());
	ASSERT_TRUE(lines.size() >= 2);
	for (const WrappedLine& line : lines)
		EXPECT_TRUE(line.width <= 100.f);
	// Aucun mot n'est coupé : chaque ligne commence par un mot entier.
	EXPECT_TRUE(lines[0].text.StartsWith("un "));
	// Seule la dernière ligne est marquée comme fin de paragraphe.
	EXPECT_TRUE(lines.back().lastOfParagraph);
	EXPECT_TRUE(!lines.front().lastOfParagraph || lines.size() == 1);
}

TEST(TextWrap, AWordLongerThanTheLineIsSplitOnCharacterBoundaries) {
	const std::vector<WrappedLine> lines =
		WrapTextToWidth(String("anticonstitutionnellement"), FS, 50.f, TenPxPerChar());
	ASSERT_TRUE(lines.size() > 1);
	for (const WrappedLine& line : lines)
		EXPECT_TRUE(line.width <= 50.f);
	// Rien n'est perdu : la concaténation redonne le mot.
	String joined;
	for (const WrappedLine& line : lines)
		joined.Concat(line.text);
	EXPECT_TRUE(joined == String("anticonstitutionnellement"));
}

TEST(TextWrap, ExplicitLineBreaksStartANewParagraph) {
	const std::vector<WrappedLine> lines =
		WrapTextToWidth(String("a\nb"), FS, 500.f, TenPxPerChar());
	ASSERT_EQ(lines.size(), size_t(2));
	EXPECT_TRUE(lines[0].text == String("a"));
	EXPECT_TRUE(lines[1].text == String("b"));
	// Chacune termine SON paragraphe : aucune ne doit être justifiée.
	EXPECT_TRUE(lines[0].lastOfParagraph);
	EXPECT_TRUE(lines[1].lastOfParagraph);
}

TEST(TextWrap, WrappedHeightGrowsWithTheNumberOfLines) {
	const String text("un deux trois quatre cinq six");
	const float wide = WrappedHeight(text, FS, 1000.f, TenPxPerChar());
	const float narrow = WrappedHeight(text, FS, 100.f, TenPxPerChar());
	EXPECT_TRUE(Near(wide, 20.f)); // une seule ligne
	EXPECT_TRUE(narrow > wide);
	EXPECT_TRUE(Near(sdl3::Fmod(narrow, 20.f), 0.f)); // un multiple de la hauteur de ligne
}

// ─────────────────────────────────────────────────────────────────────────
// 2. Ce que le layout réserve, mode par mode
// ─────────────────────────────────────────────────────────────────────────

TEST(TextOverflowLayout, AnUnconstrainedWidgetAlwaysStretchesToItsText) {
	// Règle commune à TOUS les modes : sans contrainte de largeur, le widget
	// s'élargit jusqu'au texte — le débordement ne se pose pas.
	for (TextOverflow mode : {TextOverflow::CLIP, TextOverflow::ELLIPSIS, TextOverflow::SCROLL,
							  TextOverflow::WRAP, TextOverflow::MARQUEE}) {
		Harness h;
		auto label = h.f.Label(String("douze chars"));
		label.Anchor(Anchor::TopLeft).WAuto().HAuto().TextOverflowMode(mode);
		ecs::Entity e = label.Spawn();
		h.layout.Run(h.ar, 800.f, 600.f);
		EXPECT_TRUE(Near(h.Computed(e).screen.w, 11.f * 10.f));
	}
}

TEST(TextOverflowLayout, WrapMakesTheWidgetTallerNotWider) {
	Harness h;
	auto panel = h.f.Panel();
	panel.Anchor(Anchor::TopLeft).Size(120.f, 400.f).Pad(math::Sides{0.f}).Gap(0.f);
	{
		auto label = h.f.Label(String("un deux trois quatre cinq"));
		label.WAuto().HAuto().TextWrap();
		panel.Children(std::move(label));
	}
	ecs::Entity root = panel.Spawn();
	h.layout.Run(h.ar, 800.f, 600.f);

	ecs::Entity label = h.ar.GetComponent<UiChildren>(root).Unwrap()->list[0];
	const UiComputed& c = h.Computed(label);
	// Étiré à la largeur du panneau (alignement transverse par défaut)…
	EXPECT_TRUE(Near(c.screen.w, 120.f));
	// …et assez haut pour plusieurs lignes.
	EXPECT_TRUE(c.screen.h >= 2.f * 20.f);
	EXPECT_TRUE(Near(sdl3::Fmod(c.screen.h, 20.f), 0.f));
}

TEST(TextOverflowLayout, ScrollDeclaresAContentWiderThanTheBox) {
	Harness h;
	auto label = h.f.Label(String("un texte nettement trop long"));
	label.Anchor(Anchor::TopLeft).Size(100.f, 24.f).TextScroll();
	ecs::Entity e = label.Spawn();
	h.layout.Run(h.ar, 800.f, 600.f);

	const UiRect& r = h.Rect(e);
	EXPECT_TRUE(r.content.x > r.size.x); // d'où la barre horizontale
	EXPECT_TRUE(r.MaxScroll().x > 0.f);
	EXPECT_TRUE(HScrollbarThumbRect(r, h.Computed(e).screen).w > 0.f);
	// Et rien en vertical : une seule ligne.
	EXPECT_TRUE(r.MaxScroll().y <= 0.f);
}

TEST(TextOverflowLayout, AWrappedLabelWithABoundedHeightGetsAVerticalScrollbar) {
	Harness h;
	auto label = h.f.Label(String("un deux trois quatre cinq six sept huit"));
	label.Anchor(Anchor::TopLeft).Size(100.f, 40.f).TextWrap();
	ecs::Entity e = label.Spawn();
	h.layout.Run(h.ar, 800.f, 600.f);

	const UiRect& r = h.Rect(e);
	EXPECT_TRUE(r.content.y > r.size.y);
	EXPECT_TRUE(r.MaxScroll().y > 0.f);
	EXPECT_TRUE(VScrollbarThumbRect(r, h.Computed(e).screen).w > 0.f);
}

TEST(TextOverflowLayout, ClipAndEllipsisDeclareNoScrollableContent) {
	for (TextOverflow mode : {TextOverflow::CLIP, TextOverflow::ELLIPSIS}) {
		Harness h;
		auto label = h.f.Label(String("un texte nettement trop long"));
		label.Anchor(Anchor::TopLeft).Size(100.f, 24.f).TextOverflowMode(mode);
		ecs::Entity e = label.Spawn();
		h.layout.Run(h.ar, 800.f, 600.f);
		EXPECT_TRUE(h.Rect(e).MaxScroll().x <= 0.f);
		EXPECT_TRUE(h.Rect(e).MaxScroll().y <= 0.f);
	}
}

// ─────────────────────────────────────────────────────────────────────────
// 3. Défilement automatique
// ─────────────────────────────────────────────────────────────────────────

TEST(TextMarquee, ScrollsBackAndForthWithinTheOverflowOnly) {
	Harness h;
	auto label = h.f.Label(String("un texte nettement trop long pour la boite"));
	label.Anchor(Anchor::TopLeft).Size(100.f, 24.f).TextMarquee(50.f, 0.f);
	ecs::Entity e = label.Spawn();
	h.layout.Run(h.ar, 800.f, 600.f);

	// La largeur du texte est posée par le layout : c'est elle qui fixe
	// l'amplitude (InputSystem n'a pas de police).
	const UiTextOverflow& o = *h.ar.GetComponent<UiTextOverflow>(e).Unwrap();
	ASSERT_TRUE(o.textWidth > 100.f);
	const float travel = o.textWidth - (100.f - 2.f * TEXT_INSET);

	// Assez d'images pour un aller-retour complet, quelle que soit
	// l'amplitude : (travel / vitesse) secondes par sens, à 60 images/s.
	const int framesForOneWay = int(travel / 50.f * 60.f) + 2;
	float maxSeen = 0.f;
	for (int i = 0; i < framesForOneWay; ++i) {
		h.input.Tick(h.ar, h.layout, 1.f / 60.f);
		const float offset = h.ar.GetComponent<UiTextOverflow>(e).Unwrap()->offset;
		EXPECT_TRUE(offset >= -1e-3f);
		EXPECT_TRUE(offset <= travel + 1e-3f); // jamais au-delà du débordement
		maxSeen = sdl3::Max(maxSeen, offset);
	}
	EXPECT_TRUE(maxSeen > 0.f);				 // ça défile vraiment…
	EXPECT_TRUE(Near(maxSeen, travel, 1.f)); // …jusqu'au bout, et pas plus loin

	// Puis le retour : l'offset redescend.
	for (int i = 0; i < framesForOneWay / 2; ++i)
		h.input.Tick(h.ar, h.layout, 1.f / 60.f);
	EXPECT_TRUE(h.ar.GetComponent<UiTextOverflow>(e).Unwrap()->offset < travel - 1.f);
}

TEST(TextMarquee, ATextThatFitsNeverMoves) {
	Harness h;
	auto label = h.f.Label(String("court"));
	label.Anchor(Anchor::TopLeft).Size(400.f, 24.f).TextMarquee(50.f, 0.f);
	ecs::Entity e = label.Spawn();
	h.layout.Run(h.ar, 800.f, 600.f);
	for (int i = 0; i < 60; ++i)
		h.input.Tick(h.ar, h.layout, 1.f / 60.f);
	EXPECT_TRUE(Near(h.ar.GetComponent<UiTextOverflow>(e).Unwrap()->offset, 0.f));
}

TEST(TextMarquee, ThePauseHoldsTheTextStillAtEachEnd) {
	Harness h;
	auto label = h.f.Label(String("un texte nettement trop long pour la boite"));
	label.Anchor(Anchor::TopLeft).Size(100.f, 24.f).TextMarquee(1000.f, 0.5f);
	ecs::Entity e = label.Spawn();
	h.layout.Run(h.ar, 800.f, 600.f);

	// Vitesse énorme : l'extrémité est atteinte en quelques images, et la
	// pause fige alors le texte.
	float atEnd = 0.f;
	for (int i = 0; i < 100; ++i) {
		h.input.Tick(h.ar, h.layout, 1.f / 60.f);
		const UiTextOverflow& state = *h.ar.GetComponent<UiTextOverflow>(e).Unwrap();
		if (state.hold > 0.f) { // extrémité atteinte : la pause court
			atEnd = state.offset;
			break;
		}
	}
	ASSERT_TRUE(atEnd > 0.f);
	// Une image courte pendant la pause ne déplace rien.
	h.input.Tick(h.ar, h.layout, 0.1f);
	EXPECT_TRUE(Near(h.ar.GetComponent<UiTextOverflow>(e).Unwrap()->offset, atEnd));
	// La pause épuisée, le défilement repart (en sens inverse).
	h.input.Tick(h.ar, h.layout, 0.5f);
	h.input.Tick(h.ar, h.layout, 0.05f);
	EXPECT_TRUE(h.ar.GetComponent<UiTextOverflow>(e).Unwrap()->offset < atEnd);
}

// ─────────────────────────────────────────────────────────────────────────
// 4. Points de suspension
// ─────────────────────────────────────────────────────────────────────────

TEST(TextEllipsis, KeepsTheLongestPrefixThatFitsWithTheEllipsis) {
	RenderSystem render; // sans police : heuristique 0,55 x taille par caractère
	const float charW = CharWidthApprox(FS);
	const String text("abcdefghij");

	// Large : rien à tronquer.
	EXPECT_TRUE(render.TruncateWithEllipsis(text, FS, 1000.f) == text);

	// Étroit : le résultat se termine par « … » et tient dans la largeur.
	const float limit = 5.f * charW;
	const String shown = render.TruncateWithEllipsis(text, FS, limit);
	EXPECT_TRUE(shown != text);
	EXPECT_TRUE(shown.EndsWith("…"));
	EXPECT_TRUE(float(CharCount(shown)) * charW <= limit + 1e-3f);
	// Et c'est bien le PLUS LONG préfixe qui tienne : un caractère de plus
	// déborderait.
	EXPECT_TRUE((float(CharCount(shown)) + 1.f) * charW > limit);
}

TEST(TextEllipsis, CutsOnCharacterBoundariesNotBytes) {
	RenderSystem render;
	// « éàü » : deux octets par caractère. Une coupe à l'octet produirait un
	// glyphe invalide.
	const String text("éàüéàü");
	const String shown = render.TruncateWithEllipsis(text, FS, 3.f * CharWidthApprox(FS));
	EXPECT_TRUE(shown.EndsWith("…"));
	// Chaque octet de tête doit être un début de caractère valide.
	for (size_t i = 0; i < shown.size();) {
		const unsigned char c = static_cast<unsigned char>(shown[i]);
		const size_t len = (c < 0x80) ? 1 : ((c >> 5) == 0x6 ? 2 : ((c >> 4) == 0xE ? 3 : 4));
		EXPECT_TRUE(len >= 1 && i + len <= shown.size());
		i += len;
	}
}

TEST(TextEllipsis, AnImpossiblyNarrowBoxDrawsNothingRatherThanOverflow) {
	RenderSystem render;
	EXPECT_TRUE(render.TruncateWithEllipsis(String("abc"), FS, 1.f).IsEmpty());
}

int main() {
	return RUN_ALL_TESTS();
}
