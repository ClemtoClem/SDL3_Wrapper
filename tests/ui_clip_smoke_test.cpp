// Tests unitaires — bornage de l'affichage à la partie visible d'un widget.
//
// Le défaut corrigé ici, visible sur le champ de script de l'éditeur de
// niveau : `UiComputed::clip` est la région héritée du PARENT, et c'est elle
// que le rendu posait avant de dessiner un widget. Un widget qui peint son
// propre contenu (champ de texte, tableau, liste, tracé) pouvait donc déborder
// de lui-même tant qu'il restait dans son parent — la dernière ligne d'un
// éditeur de script passait sous sa bordure, la première par-dessus.
//
// Ces tests verrouillent la géométrie (ce que le layout calcule) et les deux
// fonctions pures dont dépend le rendu ; le rendu lui-même est vérifié par
// capture d'écran, qui ne se teste pas ici.
#define USE_TEST

#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

#include <cstdlib>

using namespace ui;

namespace {

[[nodiscard]] bool Near(float a, float b, float eps = 1e-3f) {
	return (a - b) < eps && (b - a) < eps;
}

/// Vrai si `inner` tient entièrement dans `outer` (à un epsilon près).
[[nodiscard]] bool Contains(const sdl3::FRect& outer, const sdl3::FRect& inner) {
	if (inner.w <= 0.f || inner.h <= 0.f)
		return true; // région vide : contenue par définition
	return inner.x >= outer.x - 1e-3f && inner.y >= outer.y - 1e-3f &&
		   inner.x + inner.w <= outer.x + outer.w + 1e-3f &&
		   inner.y + inner.h <= outer.y + outer.h + 1e-3f;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// 1. ToClipRect : conversion vers un rect entier
// ─────────────────────────────────────────────────────────────────────────

TEST(ClipRect, RoundsOutwardsWithoutInflatingAWholePixel) {
	// Bords entiers : conversion exacte, aucun pixel ajouté. L'ancienne forme
	// (`int(w) + 1`) élargissait ici de 1 px en largeur ET en hauteur.
	const sdl3::Rect exact = ToClipRect({10.f, 20.f, 100.f, 50.f});
	EXPECT_EQ(exact.x, 10);
	EXPECT_EQ(exact.y, 20);
	EXPECT_EQ(exact.w, 100);
	EXPECT_EQ(exact.h, 50);

	// Bords fractionnaires : on arrondit vers l'EXTÉRIEUR, pour ne jamais
	// rogner une ligne de pixels du contenu.
	const sdl3::Rect frac = ToClipRect({10.4f, 20.6f, 100.2f, 50.5f});
	EXPECT_EQ(frac.x, 10);
	EXPECT_EQ(frac.y, 20);
	EXPECT_EQ(frac.x + frac.w, 111); // ceil(110.6)
	EXPECT_EQ(frac.y + frac.h, 72);	 // ceil(71.1)
}

TEST(ClipRect, AnEmptyRegionStaysEmpty) {
	const sdl3::Rect empty = ToClipRect({10.f, 20.f, 0.f, 0.f});
	EXPECT_EQ(empty.w, 0);
	EXPECT_EQ(empty.h, 0);
	// Une intersection vide ne doit pas devenir une région d'un pixel qui
	// laisserait passer du contenu.
	const sdl3::FRect a{0.f, 0.f, 10.f, 10.f};
	const sdl3::FRect b{50.f, 50.f, 10.f, 10.f};
	const sdl3::Rect none = ToClipRect(a.Intersection(b));
	EXPECT_EQ(none.w, 0);
	EXPECT_EQ(none.h, 0);
}

// ─────────────────────────────────────────────────────────────────────────
// 2. Position du texte : jamais hors de sa propre boîte
// ─────────────────────────────────────────────────────────────────────────

TEST(TextOrigin, TheComfortMarginIsKeptWhenThereIsRoom) {
	const sdl3::FRect box{100.f, 0.f, 200.f, 20.f};
	EXPECT_TRUE(Near(TextOriginX(box, 50.f, TextAlign::Left), 108.f));
	EXPECT_TRUE(Near(TextOriginX(box, 50.f, TextAlign::Right), 242.f)); // 100 + 200 - 50 - 8
	EXPECT_TRUE(Near(TextOriginX(box, 50.f, TextAlign::Center), 175.f));
}

TEST(TextOrigin, ALabelExactlyAsWideAsItsTextStaysInsideItsBox) {
	// Le cas de l'outliner : la boîte d'un UiLabel fait EXACTEMENT la largeur
	// du texte (sa mesure ne réserve aucune marge). Les 8 px de confort
	// faisaient déborder le texte à droite, et il perdait sa dernière lettre
	// une fois le clip resserré.
	const sdl3::FRect tight{100.f, 0.f, 50.f, 20.f};
	for (TextAlign align : {TextAlign::Left, TextAlign::Center, TextAlign::Right}) {
		const float x = TextOriginX(tight, 50.f, align);
		EXPECT_TRUE(Near(x, 100.f));
		EXPECT_TRUE(x + 50.f <= tight.x + tight.w + 1e-3f);
	}
}

TEST(TextOrigin, ATextWiderThanItsBoxStartsAtTheLeftEdge) {
	// Débordement inévitable : on préfère couper la FIN du texte, pas son
	// début — un libellé tronqué reste identifiable par ses premières lettres.
	const sdl3::FRect box{100.f, 0.f, 30.f, 20.f};
	EXPECT_TRUE(Near(TextOriginX(box, 80.f, TextAlign::Left), 100.f));
	EXPECT_TRUE(Near(TextOriginX(box, 80.f, TextAlign::Center), 100.f));
	EXPECT_TRUE(Near(TextOriginX(box, 80.f, TextAlign::Right), 100.f));
}

// ─────────────────────────────────────────────────────────────────────────
// 3. Géométrie de clip calculée par le layout
// ─────────────────────────────────────────────────────────────────────────

TEST(ClipGeometry, EveryWidgetIsBoundedByItsOwnBoxAndByItsParent) {
	ecs::ArchetypeRegistry ar;
	LayoutSystem layout;
	UiFactory f(ar, layout);

	auto panel = f.Panel();
	panel.Anchor(Anchor::TopLeft)
		.Offset(20.f, 20.f)
		.Size(200.f, 120.f)
		.Scrollable()
		.Pad(math::Sides{6.f});
	{
		auto row = f.Row();
		row.GrowW().HAuto().Children(f.Label(String("Un libellé assez long pour déborder")),
									 f.Button(String("Bouton")));
		auto tall = f.Label(String("Ligne\nLigne\nLigne\nLigne\nLigne\nLigne"));
		panel.Children(std::move(row), std::move(tall));
	}
	ecs::Entity root = panel.Spawn();
	layout.Run(ar, 800.f, 600.f);

	// 1. La zone de dessin d'un widget (childClip) ne sort jamais de sa boîte…
	ar.Query<UiComputed>([&](ecs::Entity, UiComputed& c) {
		EXPECT_TRUE(Contains(c.screen, c.childClip));
		// …ni de la région héritée de son parent.
		EXPECT_TRUE(Contains(c.clip, c.childClip));
	});

	// 2. Un enfant hérite exactement de la zone visible de son parent.
	const UiComputed& parent = *ar.GetComponent<UiComputed>(root).Unwrap();
	for (ecs::Entity child : ar.GetComponent<UiChildren>(root).Unwrap()->list) {
		const UiComputed& kid = *ar.GetComponent<UiComputed>(child).Unwrap();
		EXPECT_TRUE(Near(kid.clip.x, parent.childClip.x));
		EXPECT_TRUE(Near(kid.clip.y, parent.childClip.y));
		EXPECT_TRUE(Near(kid.clip.w, parent.childClip.w));
		EXPECT_TRUE(Near(kid.clip.h, parent.childClip.h));
		// Et ce qu'il peut peindre reste dans la boîte du parent : c'est la
		// propriété que le débordement du champ de script violait.
		EXPECT_TRUE(Contains(parent.screen, kid.childClip));
	}
}

TEST(ClipGeometry, AChildOverflowingItsParentIsClippedNotHidden) {
	// Contenu plus haut que son conteneur : le clip du contenu est réduit à
	// la partie VISIBLE, sans être vidé (il reste quelque chose à dessiner).
	ecs::ArchetypeRegistry ar;
	LayoutSystem layout;
	UiFactory f(ar, layout);

	auto panel = f.Panel();
	panel.Anchor(Anchor::TopLeft)
		.Offset(0.f, 0.f)
		.Size(200.f, 40.f)
		.Scrollable()
		.Pad(math::Sides{0.f})
		.Gap(0.f);
	auto inner = f.Label(String("A\nB\nC\nD\nE\nF\nG\nH"));
	panel.Children(std::move(inner));
	ecs::Entity root = panel.Spawn();
	layout.Run(ar, 800.f, 600.f);

	ecs::Entity child = ar.GetComponent<UiChildren>(root).Unwrap()->list[0];
	const UiComputed& kid = *ar.GetComponent<UiComputed>(child).Unwrap();
	const UiComputed& parent = *ar.GetComponent<UiComputed>(root).Unwrap();

	EXPECT_TRUE(kid.screen.h > parent.screen.h);			 // le contenu déborde vraiment
	EXPECT_TRUE(kid.childClip.h <= parent.screen.h + 1e-3f); // mais son dessin est borné
	EXPECT_TRUE(kid.childClip.h > 0.f);						 // sans pour autant tout masquer
	EXPECT_TRUE(Contains(parent.screen, kid.childClip));
}

TEST(ClipGeometry, AFixedOverlayEscapesItsParentsClipOnPurpose) {
	// Popups, menus et modales doivent pouvoir sortir de leur parent : ils
	// sont dessinés par une passe séparée, avec la fenêtre entière pour clip.
	ecs::ArchetypeRegistry ar;
	LayoutSystem layout;
	UiFactory f(ar, layout);

	auto host = f.Panel();
	host.Anchor(Anchor::TopLeft).Offset(0.f, 0.f).Size(80.f, 40.f).Scrollable();
	auto popup = f.Panel();
	popup.Fixed().Anchor(Anchor::TopLeft).Offset(0.f, 0.f).Size(300.f, 200.f);
	host.Children(std::move(popup));
	ecs::Entity root = host.Spawn();
	layout.Run(ar, 800.f, 600.f);

	ecs::Entity overlay = ar.GetComponent<UiChildren>(root).Unwrap()->list[0];
	const UiComputed& c = *ar.GetComponent<UiComputed>(overlay).Unwrap();
	EXPECT_TRUE(c.clip.w >= 800.f - 1e-3f);
	EXPECT_TRUE(c.clip.h >= 600.f - 1e-3f);
	EXPECT_TRUE(c.childClip.w > 80.f); // pas rogné à la boîte de son hôte
}

// ─────────────────────────────────────────────────────────────────────────
// 4. Le RENDU lui-même : ce qui est peint reste dans la boîte du widget
// ─────────────────────────────────────────────────────────────────────────

namespace {

/// Backend d'enregistrement : ne dessine rien, retient le clip courant et
/// chaque rectangle peint. Suffit à vérifier la règle qui a été violée —
/// aucun GPU ni fenêtre, donc aucune capture d'écran à comparer.
class RecordingBackend final : public IUiRenderBackend {
public:
	struct Painted {
		sdl3::FRect rect;
		sdl3::Rect clip;
	};
	std::vector<Painted> painted;
	sdl3::Rect clip{0, 0, 1 << 20, 1 << 20};

	bool SetDrawColor(sdl3::Color) override { return true; }
	bool SetDrawColor(sdl3::FColor) override { return true; }
	bool SetBlendMode(sdl3::BlendMode) override { return true; }
	bool SetClipRect(const sdl3::Rect& r) override {
		clip = r;
		return true;
	}
	bool ClearClipRect() override {
		clip = sdl3::Rect{0, 0, 1 << 20, 1 << 20};
		return true;
	}
	[[nodiscard]] sdl3::FColor GetDrawColorFloat() const override { return sdl3::FColor::WHITE(); }

	bool DrawLine(float, float, float, float) override { return true; }
	bool DrawLine(sdl3::FPoint, sdl3::FPoint) override { return true; }
	bool DrawRect(const sdl3::FRect& r) override { return Record(r); }
	bool FillRect(const sdl3::FRect& r) override { return Record(r); }
	bool DrawRoundedRect(const sdl3::FRect& r, const math::Corners&) override { return Record(r); }
	bool FillRoundedRect(const sdl3::FRect& r, const math::Corners&) override { return Record(r); }
	bool DrawCircle(sdl3::FPoint, float) override { return true; }
	bool FillCircle(sdl3::FPoint, float) override { return true; }
	bool DrawArc(sdl3::FPoint, float, float, float) override { return true; }
	bool DrawPie(sdl3::FPoint, float, float, float) override { return true; }
	bool DrawPolygon(std::span<const sdl3::FPoint>) override { return true; }
	bool FillPolygon(std::span<const sdl3::FPoint>) override { return true; }
	bool RenderGeometry(std::span<const sdl3::Vertex>, std::span<const int>) override {
		return true;
	}
	bool Render(const sdl3::Texture&, const sdl3::FRect&) override { return true; }
	bool Render(const sdl3::Texture&, const sdl3::FRect&, const sdl3::FRect&) override {
		return true;
	}

private:
	bool Record(const sdl3::FRect& r) {
		painted.push_back({r, clip});
		return true;
	}
};

/// Partie réellement peinte d'un rectangle : son intersection avec le clip
/// actif au moment du dessin.
[[nodiscard]] sdl3::FRect VisiblePart(const RecordingBackend::Painted& p) {
	const sdl3::FRect clip{float(p.clip.x), float(p.clip.y), float(p.clip.w), float(p.clip.h)};
	return p.rect.Intersection(clip);
}

} // namespace

TEST(ClipRender, AWidgetPaintsUnderItsOwnBoxNotUnderItsParentsClip) {
	// LE test du défaut. Un petit widget dans un grand panneau : la région
	// héritée (le panneau) est bien plus large que sa propre boîte. Tant que
	// le rendu posait le clip HÉRITÉ, ce widget pouvait peindre partout dans
	// son parent — c'est ainsi que les lignes d'un champ de script passaient
	// sous sa bordure.
	ecs::ArchetypeRegistry ar;
	LayoutSystem layout;
	UiFactory f(ar, layout);
	RenderSystem render;
	RecordingBackend backend;

	auto panel = f.Panel();
	panel.Anchor(Anchor::TopLeft)
		.Offset(0.f, 0.f)
		.Size(400.f, 300.f)
		.Pad(math::Sides{0.f})
		.Gap(0.f);
	{
		auto bar = f.Separator(); // peint exactement sa boîte : témoin idéal
		bar.Size(40.f, 10.f);
		panel.Children(std::move(bar));
	}
	ecs::Entity root = panel.Spawn();
	layout.Run(ar, 800.f, 600.f);
	render.Run(ar, backend);

	ecs::Entity separator = ar.GetComponent<UiChildren>(root).Unwrap()->list[0];
	const UiComputed& c = *ar.GetComponent<UiComputed>(separator).Unwrap();
	// La région héritée est bien plus large que le widget : c'est ce qui rend
	// ce test discriminant.
	ASSERT_TRUE(c.clip.w > c.screen.w * 2.f);

	bool found = false;
	for (const RecordingBackend::Painted& p : backend.painted) {
		if (!Near(p.rect.w, c.screen.w) || !Near(p.rect.h, c.screen.h))
			continue;
		found = true;
		const sdl3::Rect expected = ToClipRect(c.childClip);
		EXPECT_EQ(p.clip.x, expected.x);
		EXPECT_EQ(p.clip.y, expected.y);
		EXPECT_EQ(p.clip.w, expected.w);
		EXPECT_EQ(p.clip.h, expected.h);
		// Formulé autrement : le clip ne dépasse pas la boîte du widget.
		EXPECT_TRUE(p.clip.w <= int(sdl3::Ceil(c.screen.w)));
		EXPECT_TRUE(p.clip.h <= int(sdl3::Ceil(c.screen.h)));
	}
	EXPECT_TRUE(found);
}

TEST(ClipRender, AWidgetLargerThanItsParentIsStillBoundedByThatParent) {
	// L'autre sens : un contenu plus grand que son conteneur reste borné au
	// conteneur — la règle qui marchait déjà, et qu'il ne faut pas perdre.
	ecs::ArchetypeRegistry ar;
	LayoutSystem layout;
	UiFactory f(ar, layout);
	RenderSystem render;
	RecordingBackend backend;

	auto panel = f.Panel();
	panel.Anchor(Anchor::TopLeft)
		.Offset(50.f, 50.f)
		.Size(160.f, 60.f)
		.Scrollable()
		.Pad(math::Sides{0.f})
		.Gap(0.f);
	{
		auto bar = f.Separator();
		bar.Size(400.f, 200.f);
		panel.Children(std::move(bar));
	}
	ecs::Entity root = panel.Spawn();
	layout.Run(ar, 800.f, 600.f);
	render.Run(ar, backend);

	const sdl3::FRect box = ar.GetComponent<UiComputed>(root).Unwrap()->screen;
	ASSERT_TRUE(!backend.painted.empty());
	for (const RecordingBackend::Painted& p : backend.painted) {
		const sdl3::FRect visible = VisiblePart(p);
		if (visible.w <= 0.f || visible.h <= 0.f)
			continue; // entièrement clipé : rien n'atteint l'écran
		EXPECT_TRUE(Contains(box, visible));
	}
}

TEST(ClipRender, AGlowingWidgetKeepsExactlyTheMarginItsHaloNeeds) {
	// La seule exception à « rien hors de sa boîte » : le halo de lueur
	// « verre » est dessiné AUTOUR du widget par conception (cf.
	// RenderSystem::DrawGlowRing). Le clip lui accorde donc GLOW_MAX_OUTSET —
	// pas plus, et jamais au-delà du parent.
	ecs::ArchetypeRegistry ar;
	LayoutSystem layout;
	UiFactory f(ar, layout);
	StyleSystem styles;
	styles.sheet = &f.sheet;
	RenderSystem render;
	RecordingBackend backend;

	auto panel = f.Panel();
	panel.Anchor(Anchor::TopLeft)
		.Offset(0.f, 0.f)
		.Size(400.f, 300.f)
		.Pad(math::Sides{40.f})
		.Gap(0.f);
	{
		auto glowing = f.Separator(); // témoin qui peint exactement sa boîte
		glowing.Size(40.f, 10.f).Glass();
		panel.Children(std::move(glowing));
	}
	ecs::Entity root = panel.Spawn();
	layout.Run(ar, 800.f, 600.f);
	styles.Resolve(ar);
	render.Run(ar, backend);

	ecs::Entity glowing = ar.GetComponent<UiChildren>(root).Unwrap()->list[0];
	const UiComputed& c = *ar.GetComponent<UiComputed>(glowing).Unwrap();
	ASSERT_TRUE(GetResolved(ar, glowing).HasGlow());

	bool found = false;
	for (const RecordingBackend::Painted& p : backend.painted) {
		if (!Near(p.rect.w, c.screen.w) || !Near(p.rect.h, c.screen.h))
			continue;
		found = true;
		// Élargi d'exactement la marge du halo, pas d'un pixel de plus.
		EXPECT_EQ(p.clip.w, int(sdl3::Ceil(c.screen.w + 2.f * GLOW_MAX_OUTSET)));
		EXPECT_EQ(p.clip.h, int(sdl3::Ceil(c.screen.h + 2.f * GLOW_MAX_OUTSET)));
	}
	EXPECT_TRUE(found);
}

TEST(ClipRender, AWidgetWithoutGlowGetsNoExtraMargin) {
	ecs::ArchetypeRegistry ar;
	LayoutSystem layout;
	UiFactory f(ar, layout);
	StyleSystem styles;
	styles.sheet = &f.sheet;
	RenderSystem render;
	RecordingBackend backend;

	auto panel = f.Panel();
	panel.Anchor(Anchor::TopLeft)
		.Offset(0.f, 0.f)
		.Size(400.f, 300.f)
		.Pad(math::Sides{40.f})
		.Gap(0.f);
	{
		auto plain = f.Separator();
		plain.Size(40.f, 10.f);
		panel.Children(std::move(plain));
	}
	ecs::Entity root = panel.Spawn();
	layout.Run(ar, 800.f, 600.f);
	styles.Resolve(ar);
	render.Run(ar, backend);

	ecs::Entity plain = ar.GetComponent<UiChildren>(root).Unwrap()->list[0];
	const UiComputed& c = *ar.GetComponent<UiComputed>(plain).Unwrap();
	for (const RecordingBackend::Painted& p : backend.painted) {
		if (!Near(p.rect.w, c.screen.w) || !Near(p.rect.h, c.screen.h))
			continue;
		EXPECT_EQ(p.clip.w, int(sdl3::Ceil(c.screen.w)));
		EXPECT_EQ(p.clip.h, int(sdl3::Ceil(c.screen.h)));
	}
}

// Un rayon d'angle plus grand que la moitié du côté faisait se chevaucher
// les quarts de disque (un carré de 8 px au rayon 8 sortait en « × ») : les
// rayons sont bornés au dessin.
TEST(ClipRect, CornerRadiiAreClampedToHalfTheSmallestSide) {
	const sdl3::Corners c = sdl3::Renderer::ClampCorners(sdl3::FRect{0.f, 0.f, 8.f, 20.f}, sdl3::Corners(12.f));
	EXPECT_TRUE(c.tl == 4.f && c.tr == 4.f && c.bl == 4.f && c.br == 4.f);
	const sdl3::Corners d = sdl3::Renderer::ClampCorners(sdl3::FRect{0.f, 0.f, 100.f, 40.f}, sdl3::Corners(1.f, 2.f, 3.f, 30.f));
	EXPECT_TRUE(d.tl == 1.f && d.tr == 2.f && d.bl == 3.f && d.br == 20.f);
}

int main() {
	return RUN_ALL_TESTS();
}
