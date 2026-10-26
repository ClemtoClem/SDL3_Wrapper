#pragma once
// Outils communs aux écrans (privé aux sources de l'interface).
#include <voxelia/screens.hpp>
#include <voxelia/session.hpp>

#include <SDL3/SDL.h>
#include <voxel/game.hpp>
#include <voxel/json.hpp>

namespace voxelia::uiutil {

using ui::MaterialIcons;
using ui::WidgetBuilder;
namespace json = voxel::json;

/// Page plein écran : voile dégradé sur le monde de fond, colonne centrée
/// (largeur max) ; rend la colonne de contenu.
inline ui::Widget Page(Kit &k, ui::Widget root, float maxWidth = 1100.f, float alpha = 0.94f) {
	WidgetBuilder veil = ui::Row();
	veil.Absolute().Anchor(ui::Anchor::TopLeft).Width(ui::Length::Pct(100.f)).Height(ui::Length::Pct(100.f)).JustifyContent(ui::Justify::Center);
	veil.Background(WithAlpha(k.Colors().bg0, 0.45f)).Padding(math::Sides(24.f, 22.f));
	ui::Widget v = root.Append(veil);
	WidgetBuilder page = k.Surface(alpha);
	// Toute la largeur disponible, plafonnée : jamais plus large que la fenêtre.
	page.GrowWidth().GrowHeight().MaxSize(maxWidth * k.Scale(), 100000.f);
	return v.Append(page);
}

/// Fenêtre modale centrée (le voile est posé par le Shell).
inline ui::Widget ModalCard(Kit &k, ui::Widget root, float width) {
	WidgetBuilder center = ui::Column();
	center.Absolute().Anchor(ui::Anchor::TopLeft).Width(ui::Length::Pct(100.f)).Height(ui::Length::Pct(100.f));
	center.JustifyContent(ui::Justify::Center).AlignItems(ui::Align::Center);
	ui::Widget c = root.Append(center);
	WidgetBuilder card = k.Surface(0.98f);
	// Largeur voulue si la fenêtre le permet, sinon celle de la fenêtre.
	card.Width(ui::Length::Pct(100.f)).MaxSize(width * k.Scale(), 100000.f).Gap(14.f * k.Scale());
	return c.Append(card);
}

/// Image d'un fichier, mise en réserve dans le pool de textures de l'interface.
inline String ImageKey(Shell &shell, const String &path) {
	if (path.IsEmpty())
		return String();
	const String key = String("file:") + path;
	ui::Textures &pool = shell.Gui().GetTextures();
	if (!pool.Has(key)) {
		auto tex = sdl3::Texture::CreateFromFile(shell.Renderer(), path);
		if (!tex)
			return String();
		(void)tex.Value().SetScaleMode(SDL_SCALEMODE_NEAREST);
		pool.Add(key, std::move(tex).Unwrap());
	}
	return key;
}

/// Texture d'un contenu (id de TextureDef) en réserve dans le pool ; rend sa clé.
inline String TextureKey(Shell &shell, voxel::Game &game, const String &id, int *w = nullptr, int *h = nullptr) {
	const String key = String("tex:") + id;
	ui::Textures &pool = shell.Gui().GetTextures();
	if (!pool.Has(key)) {
		std::shared_ptr<voxel::TextureImage> img = game.Assets().Texture(id);
		if (!img || img->Pixels().IsEmpty())
			return String();
		const voxel::Image &px = img->Pixels();
		auto surface = sdl3::Surface::CreateFromPixels(px.width, px.height, sdl3::PixelFormat::RGBA32, px.rgba.data(), px.width * 4);
		if (!surface)
			return String();
		auto tex = sdl3::Texture::CreateFromSurface(shell.Renderer(), surface.Value());
		if (!tex)
			return String();
		(void)tex.Value().SetScaleMode(SDL_SCALEMODE_NEAREST);
		pool.Add(key, std::move(tex).Unwrap());
	}
	const sdl3::FPoint size = pool.SizeOf(key);
	if (w)
		*w = int(size.x);
	if (h)
		*h = int(size.y);
	return key;
}

/// Jeu donnant accès au contenu (partie en cours, sinon monde des menus).
inline voxel::Game *ContentGame(Shell &shell) {
	if (Session *s = shell.Game(); s && s->Ready())
		return &s->Game();
	return shell.MenuGame();
}

/// Personnage vu de face d'après un skin (64×32 ou 64×64).
inline WidgetBuilder SkinFront(Shell &shell, const String &skin, float height) {
	voxel::Game *game = ContentGame(shell);
	int tw = 64, th = 32;
	const String key = game ? TextureKey(shell, *game, skin, &tw, &th) : String();
	WidgetBuilder b = ui::Canvas([&shell, key, tw, th](ui::Painter &painter, const sdl3::FRect &r) {
		sdl3::Texture *found = shell.Gui().GetTextures().Get(key);
		if (!found || !painter.Renderer())
			return;
		sdl3::Renderer &ren = *painter.Renderer();
		const sdl3::Texture &tex = *found;
		const float k = std::min(r.w / 16.f, r.h / 32.f);
		const float ox = r.x + (r.w - 16.f * k) * 0.5f, oy = r.y + (r.h - 32.f * k) * 0.5f;
		const float t = float(tw) / 64.f; // skins haute définition
		const bool tall = th >= tw;
		auto part = [&](float sx, float sy, float sw, float sh, float dx, float dy, bool flip = false) {
			sdl3::FRect dst{ox + dx * k, oy + dy * k, sw * k, sh * k};
			if (flip) {
				const SDL_FRect src{sx * t, sy * t, sw * t, sh * t};
				const SDL_FRect out{dst.x, dst.y, dst.w, dst.h};
				(void)SDL_RenderTextureRotated(ren.Get(), tex.Get(), &src, &out, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
				return;
			}
			(void)ren.Render(tex, sdl3::FRect{sx * t, sy * t, sw * t, sh * t}, dst);
		};
		part(8, 8, 8, 8, 4, 0);			  // tête
		part(20, 20, 8, 12, 4, 8);		  // torse
		part(44, 20, 4, 12, 0, 8);		  // bras droit
		part(4, 20, 4, 12, 4, 20);		  // jambe droite
		if (tall) {
			part(36, 52, 4, 12, 12, 8); // bras gauche
			part(20, 52, 4, 12, 8, 20); // jambe gauche
			part(20, 36, 8, 12, 4, 8);	// veste
		} else {
			part(44, 20, 4, 12, 12, 8, true);
			part(4, 20, 4, 12, 8, 20, true);
		}
		part(40, 8, 8, 8, 4, 0); // chapeau (calque)
	});
	b.Size(height * 0.5f, height).PointerThrough();
	return b;
}

/// Icône d'un mod : son image, sinon une tuile colorée à son initiale.
inline WidgetBuilder ModIcon(Shell &shell, const voxel::ModInfo &mod, float size) {
	Kit &k = shell.K();
	if (!mod.icon.IsEmpty())
		if (const String key = ImageKey(shell, mod.icon); !key.IsEmpty()) {
			WidgetBuilder img = ui::Image(key);
			img.Size(size * k.Scale(), size * k.Scale()).ObjectFit(ui::ObjectFit::Cover).BorderRadius(8.f).PointerThrough();
			return img;
		}
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < mod.id.GetSize(); ++i)
		h = (h ^ uint8_t(mod.id.CharAt(i))) * 16777619u;
	const sdl3::FColor c = Rgb(60 + int(h % 140), 60 + int((h >> 8) % 140), 60 + int((h >> 16) % 140));
	const String letter = mod.name.IsEmpty() ? String("?") : mod.name.Substr(0, 1).ToUpper();
	WidgetBuilder tile = ui::Column();
	tile.Size(size * k.Scale(), size * k.Scale()).BorderRadius(8.f).Background(c).JustifyContent(ui::Justify::Center).AlignItems(ui::Align::Center);
	tile.PointerThrough();
	tile.Child(k.PixelTitle(letter, size * 0.45f, Rgb(255, 255, 255), true));
	return tile;
}

/// Icône d'objet (dessinée par la session).
inline WidgetBuilder ItemIcon(Shell &shell, voxel::ItemStack stack, float size, bool count = true) {
	WidgetBuilder b = ui::Canvas([&shell, stack, count](ui::Painter &painter, const sdl3::FRect &r) {
		if (Session *s = shell.Game(); s && s->Ready() && painter.Renderer())
			s->DrawItem(*painter.Renderer(), stack, r, count);
	});
	b.Size(size * shell.K().Scale(), size * shell.K().Scale()).PointerThrough();
	return b;
}

inline const std::vector<String> &Tips() {
	static const std::vector<String> tips{
		"Les feuilles laissent parfois tomber une pousse : replantez-la sur de l'herbe pour faire pousser un arbre.",
		"La houe transforme l'herbe en terre labourée ; les graines de blé y poussent en quelques minutes.",
		"Un établi à proximité débloque les recettes en 3 × 3 ; un four, la cuisson (un combustible par cuisson).",
		"Les villageois échangent des émeraudes contre vos récoltes : clic droit pour ouvrir leur boutique.",
		"La nuit, les morts-vivants rôdent ; ils brûlent au soleil s'ils restent à découvert.",
		"F5 passe en vue à la troisième personne pour admirer votre skin.",
		"Tab affiche les informations du monde : statistiques, mods actifs, sauvegarde.",
		"Le terminal (/) complète les commandes avec Tab et rappelle l'historique avec ↑ et ↓.",
		"Chaque mod peut se configurer monde par monde : Mondes > Mods > Configurer.",
		"Paramètres > Accessibilité : contraste élevé, daltonisme, taille du texte et réduction des animations.",
		"Les créatures frappées gardent rancune : les loups et les ours défendent leur territoire.",
		"La poudre d'os accélère la pousse des cultures et des arbres.",
	};
	return tips;
}

/// Ligne d'information « libellé : valeur » (écrans d'informations).
inline void InfoRow(Kit &k, ui::Widget parent, const String &label, const String &value, Option<sdl3::FColor> color = NONE) {
	WidgetBuilder row = k.Row(8.f);
	row.GrowWidth();
	ui::Widget e = parent.Append(row);
	e.Append(k.Muted(label, Text::SMALL).Width(170.f * k.Scale()));
	WidgetBuilder v = k.Label(value, Text::SMALL);
	v.GrowWidth().Ellipsis();
	if (color.IsSome())
		v.Color(color.Value());
	e.Append(v);
}

} // namespace voxelia::uiutil
