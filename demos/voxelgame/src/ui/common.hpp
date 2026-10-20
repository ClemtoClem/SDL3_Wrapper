#pragma once
// Outils communs aux écrans (privé aux sources de l'interface).
#include <voxelgame/screens.hpp>
#include <voxelgame/session.hpp>

#include <SDL3/SDL.h>
#include <voxel/game.hpp>
#include <voxel/json.hpp>

namespace voxelgame::uiutil {

using ui::Dimension;
using ui::MaterialIcons;
using ui::WidgetBuilder;
namespace json = voxel::json;

/// Page plein écran : voile dégradé sur le monde de fond, colonne centrée
/// (largeur max) ; rend la colonne de contenu.
inline ecs::Entity Page(Kit &k, ecs::Entity root, float maxWidth = 1100.f, float alpha = 0.94f) {
	WidgetBuilder veil = k.Factory().Row();
	veil.Absolute().Anchor(ui::Anchor::TopLeft).W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f)).Justify(ui::Justify::Center);
	veil.Bg(WithAlpha(k.Colors().bg0, 0.45f)).Pad(math::Sides(24.f, 22.f)).Parent(root);
	const ecs::Entity v = veil.Spawn();
	WidgetBuilder page = k.Surface(alpha);
	page.W(Dimension::Px(maxWidth * k.Scale())).GrowH().MaxSize(maxWidth * k.Scale(), 100000.f).Parent(v);
	return page.Spawn();
}

/// Fenêtre modale centrée (le voile est posé par le Shell).
inline ecs::Entity ModalCard(Kit &k, ecs::Entity root, float width) {
	WidgetBuilder center = k.Factory().Column();
	center.Absolute().Anchor(ui::Anchor::TopLeft).W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f));
	center.Justify(ui::Justify::Center).Align(ui::CrossAlign::Center).Parent(root);
	const ecs::Entity c = center.Spawn();
	WidgetBuilder card = k.Surface(0.98f);
	card.W(Dimension::Px(width * k.Scale())).HAuto().Gap(14.f * k.Scale()).Parent(c);
	return card.Spawn();
}

/// Image d'un fichier, mise en réserve dans le pool de textures de l'interface.
inline String ImageKey(Shell &shell, const String &path) {
	if (path.IsEmpty())
		return String();
	const String key = String("file:") + path;
	auto &pool = shell.Gui().RenderSystem().textures;
	if (pool.find(key) == pool.end()) {
		auto tex = sdl3::Texture::CreateFromFile(shell.Renderer(), path);
		if (!tex)
			return String();
		(void)tex.Value().SetScaleMode(SDL_SCALEMODE_NEAREST);
		pool.emplace(key, std::move(tex).Unwrap());
	}
	return key;
}

/// Texture d'un contenu (id de TextureDef) en réserve dans le pool ; rend sa clé.
inline String TextureKey(Shell &shell, voxel::Game &game, const String &id, int *w = nullptr, int *h = nullptr) {
	const String key = String("tex:") + id;
	auto &pool = shell.Gui().RenderSystem().textures;
	auto it = pool.find(key);
	if (it == pool.end()) {
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
		it = pool.emplace(key, std::move(tex).Unwrap()).first;
	}
	if (w || h) {
		float tw = 0.f, th = 0.f;
		(void)SDL_GetTextureSize(it->second.Get(), &tw, &th);
		if (w)
			*w = int(tw);
		if (h)
			*h = int(th);
	}
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
	WidgetBuilder b = shell.K().Factory().Canvas([&shell, key, tw, th](sdl3::Renderer &ren, sdl3::FRect r) {
		auto &pool = shell.Gui().RenderSystem().textures;
		auto it = pool.find(key);
		if (it == pool.end())
			return;
		const sdl3::Texture &tex = it->second;
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
			WidgetBuilder img = k.Factory().Image(key, size * k.Scale(), size * k.Scale());
			img.Fit(ui::ImageFit::COVER).Radius(8.f).PointerThrough();
			return img;
		}
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < mod.id.GetSize(); ++i)
		h = (h ^ uint8_t(mod.id.CharAt(i))) * 16777619u;
	const sdl3::FColor c = Rgb(60 + int(h % 140), 60 + int((h >> 8) % 140), 60 + int((h >> 16) % 140));
	const String letter = mod.name.IsEmpty() ? String("?") : mod.name.Substr(0, 1).ToUpper();
	WidgetBuilder tile = k.Factory().Column();
	tile.Size(size * k.Scale(), size * k.Scale()).Radius(8.f).Bg(c).Justify(ui::Justify::Center).Align(ui::CrossAlign::Center);
	tile.PointerThrough();
	tile.Children(k.PixelTitle(letter, size * 0.45f, Rgb(255, 255, 255), true));
	return tile;
}

/// Icône d'objet (dessinée par la session).
inline WidgetBuilder ItemIcon(Shell &shell, voxel::ItemStack stack, float size, bool count = true) {
	WidgetBuilder b = shell.K().Factory().Canvas([&shell, stack, count](sdl3::Renderer &ren, sdl3::FRect r) {
		if (Session *s = shell.Game(); s && s->Ready())
			s->DrawItem(ren, stack, r, count);
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
inline void InfoRow(Kit &k, ecs::Entity parent, const String &label, const String &value, Option<sdl3::FColor> color = NONE) {
	WidgetBuilder row = k.Row(8.f);
	row.GrowW().Parent(parent);
	const ecs::Entity e = row.Spawn();
	k.Muted(label, Text::SMALL).W(Dimension::Px(170.f * k.Scale())).Parent(e).Spawn();
	WidgetBuilder v = k.Label(value, Text::SMALL);
	v.GrowW().TextEllipsis();
	if (color.IsSome())
		v.TextColor(color.Value());
	v.Parent(e).Spawn();
}

} // namespace voxelgame::uiutil
