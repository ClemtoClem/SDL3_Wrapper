// Définitions de session.hpp — la partie en cours et son HUD.
#include <voxelia/session.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

#include <SDL3/SDL.h>
#include <audio/clip.hpp>
#include <ecs/events.hpp>
#include <sdl3/image.hpp>
#include <voxel/json.hpp>
#include <voxel/rig.hpp>
#include <voxel/scripting.hpp>
#include <voxelia/screens.hpp>
#include <voxelia/shell.hpp>

namespace voxelia {

using namespace voxel;
using ui::WidgetBuilder;

namespace {

/// Réglages des mods d'une partie : ceux de l'utilisateur (écran « Mods »,
/// pour tous les mondes), remplacés clé par clé par ceux propres au monde.
dataio::NodePtr MergedModSettings(const dataio::NodePtr &user, const dataio::NodePtr &world) {
	dataio::NodePtr out = user && user->IsObject() ? user->Clone() : dataio::Node::MakeObject();
	if (!world || !world->IsObject())
		return out;
	for (const String &mod : world->Keys()) {
		dataio::NodePtr values = world->Get(mod);
		if (!values || !values->IsObject())
			continue;
		dataio::NodePtr target = out->Get(mod);
		if (!target || !target->IsObject()) {
			target = dataio::Node::MakeObject();
			out->Set(mod, target);
		}
		for (const String &key : values->Keys())
			target->Set(key, values->Get(key)->Clone());
	}
	return out;
}

constexpr float EYE = 1.62f;
constexpr int HOTBAR = 9;

double Now() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

/// État d'une action d'après le clavier ou la souris (touches configurables).
bool Held(const Settings &s, const char *action) {
	const KeyBinding *b = s.Binding(action);
	if (!b)
		return false;
	const bool *keys = SDL_GetKeyboardState(nullptr);
	const SDL_MouseButtonFlags mouse = SDL_GetMouseState(nullptr, nullptr);
	for (int code : b->keys) {
		if (code > 0 && code < MOUSE_BASE && keys[code])
			return true;
		if (code > MOUSE_BASE && code < WHEEL_UP && (mouse & SDL_BUTTON_MASK(code - MOUSE_BASE)))
			return true;
	}
	return false;
}

} // namespace

Session::Session(Shell &shell, String folder) : m_shell(shell), m_folder(std::move(folder)) {
	m_updateThread = std::make_unique<Worker>(String("voxel-update"), m_shell.Opts().threaded);
}

Session::~Session() {
	m_updateThread.reset(); // pas de simulation en cours pendant qu'on défait la partie
	if (m_audio)
		m_audio->StopAll();
	m_world.reset();
	if (m_game)
		m_game->CloseWorld();
}

// ── Chargement ───────────────────────────────────────────────────────────────

bool Session::Prepare(float &progress, String &status) {
	Settings &prefs = m_shell.Prefs();
	switch (m_stage) {
	case 0: {
		status = "Lecture des mods du monde…";
		progress = 0.05f;
		++m_stage;
		return false;
	}
	case 1: {
		Option<WorldEntry> entry = m_shell.Worlds().Find(m_folder, m_shell.Mods().Installed());
		if (entry.IsNone()) {
			m_error = String::Format("Le monde « %s » est introuvable ou illisible.", m_folder.CStr());
			return false;
		}
		GameOptions o;
		o.modDirectories = m_shell.Opts().mods;
		o.savesDirectory = m_shell.Opts().saves;
		o.structureDirectory = m_shell.Opts().structures;
		o.viewRadius = prefs.viewDistance;
		o.verticalRadius = std::clamp(prefs.viewDistance / 2, 2, 4);
		o.chunkBudget = 8;
		// Mods choisis pour ce monde (les autres sont désactivés).
		const WorldEntry &w = entry.Value();
		if (!w.enabledMods.empty())
			for (const voxel::ModInfo &m : m_shell.Mods().Installed())
				if (m.id != "core" && std::find(w.enabledMods.begin(), w.enabledMods.end(), m.id) == w.enabledMods.end())
					o.disabledMods.push_back(m.id);
		o.modOrder = json::StrList(w.settings, "mods.order");
		o.modSettings = MergedModSettings(prefs.modSettings, json::At(w.settings, "modSettings"));
		m_game = std::make_unique<voxel::Game>();
		const ModReport report = m_game->LoadMods(o);
		for (const String &e : report.errors)
			m_shell.Log(String("[erreur] ") + e);
		if (m_game->Content().BlockCount() <= 1) {
			m_error = String("Aucun contenu n'a pu être chargé (mods absents ou en erreur).");
			return false;
		}
		if (!report.errors.empty())
			m_shell.Notify(Tone::WARNING, String::Format("%d erreur(s) de chargement des mods", int(report.errors.size())),
						   report.errors.front(), true);
		status = "Ouverture du monde…";
		progress = 0.25f;
		++m_stage;
		return false;
	}
	case 2: {
		if (Result<bool, String> r = m_game->OpenWorld(m_folder); r.IsError()) {
			m_error = r.Error();
			return false;
		}
		m_player = m_game->JoinPlayer(prefs.playerName.IsEmpty() ? String("Joueur") : prefs.playerName);
		if (!prefs.skin.IsEmpty())
			if (auto v = m_game->Registry().GetComponent<Visual>(m_player); v.IsSome())
				v.Unwrap()->skin = prefs.skin;
		status = "Préparation des textures…";
		progress = 0.4f;
		++m_stage;
		return false;
	}
	case 3: {
		const Options &o = m_shell.Opts();
		m_world = std::make_unique<WorldRenderer>(*m_game, m_shell.Canvas(), o.threaded ? o.meshThreads : 0);
		if (Result<bool, String> r = m_world->Initialize(); r.IsError()) {
			m_error = r.Error();
			return false;
		}
		m_audio = std::make_unique<audio::AudioEngine>(m_game->Assets().Cache());
		audio::RegisterResourceFactories(m_game->Assets().Cache());
		m_audio->Start();
		ApplySettings();
		m_world->rebuildBudget = 24;
		if (auto t = m_game->Registry().GetComponent<Transform>(m_player); t.IsSome())
			m_lastPosition = t.Unwrap()->position;
		status = "Génération du terrain…";
		progress = 0.5f;
		++m_stage;
		m_targetChunks = 0;
		return false;
	}
	default: {
		// Chunks autour du joueur : générés puis maillés.
		m_game->Tick(0.f);
		m_camera = CameraFor();
		m_world->Update(m_camera, 1.f, NONE);
		const size_t pending = m_world->PendingChunks(), meshed = m_world->ChunkMeshCount();
		m_targetChunks = std::max(m_targetChunks, pending + meshed);
		const float k = m_targetChunks == 0 ? 0.f : float(meshed) / float(m_targetChunks);
		progress = 0.5f + 0.5f * std::min(1.f, k);
		status = String::Format("Génération du terrain… %d / %d chunks", int(meshed), int(m_targetChunks));
		if (++m_stage > 12 && (pending == 0 || m_stage > 400)) {
			m_world->rebuildBudget = size_t(std::max(2, prefs.chunkUpdates));
			m_ready = true;
			BuildHud(ui::Widget());
			AddLog(String::Format("Bienvenue dans « %s » !", m_game->Metadata().name.CStr()), "system");
			return true;
		}
		return false;
	}
	}
}

void Session::ApplySettings() {
	if (!m_game)
		return;
	const Settings &s = m_shell.Prefs();
	m_game->SetViewRadius(s.viewDistance, std::clamp(s.viewDistance / 2, 2, 4));
	if (m_world) {
		WorldRenderSettings r;
		r.shadows = s.shadows;
		r.dynamicLights = s.dynamicLights;
		r.ambientOcclusion = s.ambientOcclusion;
		r.clouds = s.clouds;
		r.vegetationDensity = s.vegetationDensity;
		r.entityDistance = float(s.entityDistance);
		r.brightness = s.brightness;
		r.particles = s.particles;
		m_world->Configure(r);
		m_world->rebuildBudget = size_t(std::max(2, s.chunkUpdates));
	}
	if (m_audio)
		m_audio->SetMasterVolume(s.masterVolume);
}

void Session::Save(bool notify) {
	if (!m_game || !m_game->HasWorld())
		return;
	Result<bool, String> r = m_game->Save();
	if (notify) {
		if (r.IsOk())
			m_shell.Notify(Tone::SUCCESS, "Partie enregistrée", m_game->Metadata().name);
		else
			m_shell.Notify(Tone::DANGER, "Échec de la sauvegarde", r.Error(), true);
	}
}

// ── HUD ──────────────────────────────────────────────────────────────────────

void Session::BuildHud(ui::Widget layer) {
	Kit &k = m_shell.K();
	if (m_hud.Valid())
		m_hud.Destroy();
	ui::Widget parent = layer;
	if (!parent.Valid())
		parent = k.Ui().Find("#hud-layer").UnwrapOr(k.Ui().Root());
	const Settings &s = m_shell.Prefs();
	const Palette &p = k.Colors();
	const sdl3::FColor panelBg = WithAlpha(Rgb(8, 10, 14), s.hudBackground);
	auto line = [&](ui::Widget &out, Text role, sdl3::FColor color) {
		return k.Label(String(), role).Color(color).PointerThrough().Ref(out);
	};
	auto panel = [&](float gap, ui::Anchor anchor, float x, float y) {
		WidgetBuilder b = k.Column(gap);
		b.Absolute().Anchor(anchor).Offset(x, y).Padding(10.f, 6.f).BorderRadius(8.f).Background(panelBg).PointerThrough();
		return b;
	};
	WidgetBuilder hud = ui::Stack();
	hud.Id("hud").Absolute().Fill().Padding(0.f).PointerThrough();
	// Barres, barre d'actions, réticule… dessinés à la main.
	hud.Child(ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &r) {
				  if (painter.Renderer())
					  DrawHud(*painter.Renderer(), r);
			  })
				  .Absolute()
				  .Fill()
				  .PointerThrough()
				  .Ref(m_hudCanvas));
	// Haut gauche : heure, biome, coordonnées.
	WidgetBuilder info = panel(2.f, ui::Anchor::TopLeft, 14.f, 14.f);
	for (ui::Widget &l : m_infoLines)
		info.Child(line(l, Text::SMALL, p.text));
	hud.Child(info.Ref(m_infoPanel));
	// Haut centre : bloc ou créature visée.
	WidgetBuilder target = panel(1.f, ui::Anchor::Top, 0.f, 14.f);
	target.Padding(14.f, 6.f).AlignItems(ui::Align::Center);
	target.Child(line(m_targetName, Text::BODY, p.text)).Child(line(m_targetDetail, Text::CAPTION, p.textMuted));
	hud.Child(target.Ref(m_targetPanel));
	// Bas droite : indications de commandes.
	WidgetBuilder hints = panel(3.f, ui::Anchor::BottomRight, -14.f, -14.f);
	hints.AlignItems(ui::Align::End);
	for (ui::Widget &l : m_hintLines)
		hints.Child(line(l, Text::CAPTION, p.textMuted));
	hud.Child(hints.Ref(m_hintPanel));
	// Bas gauche : discussion et messages. Rangée (fond, padding) puis texte
	// à la ligne : un message long se replie dans la largeur du panneau.
	WidgetBuilder chat = k.Column(2.f);
	chat.Absolute().Anchor(ui::Anchor::BottomLeft).Offset(14.f, -96.f * s.hudScale).Width(520.f).PointerThrough();
	for (size_t i = 0; i < m_chatLines.size(); ++i)
		chat.Child(k.Row(0.f)
					   .GrowWidth()
					   .Padding(6.f, 2.f)
					   .BorderRadius(4.f)
					   .Background(WithAlpha(Rgb(8, 10, 14), s.chatOpacity * 0.8f))
					   .PointerThrough()
					   .Ref(m_chatRows[i])
					   .Children({k.Paragraph(String(), Text::SMALL).PointerThrough().Ref(m_chatLines[i])}));
	hud.Child(chat.Ref(m_chatPanel));
	// Informations techniques (F3), sous le panneau d'informations.
	WidgetBuilder debug = panel(1.f, ui::Anchor::TopLeft, 14.f, 92.f);
	for (ui::Widget &l : m_debugLines)
		debug.Child(line(l, Text::CAPTION, p.text));
	hud.Child(debug.Ref(m_debugPanel));
	m_hud = parent.Append(hud);
	k.SetHidden(m_debugPanel, !m_debug);
	SetHudVisible(m_hudVisible);
}

void Session::SetHudVisible(bool visible) {
	m_hudVisible = visible;
	if (m_hud.Valid())
		m_shell.K().SetHidden(m_hud, !visible);
}

voxel::Inventory *Session::Inventory() {
	auto inv = m_game->Registry().GetComponent<InventoryComponent>(m_player);
	return inv.IsSome() ? &inv.Unwrap()->inventory : nullptr;
}

String Session::ItemName(const String &id) {
	if (const ItemDef *item = m_game->Content().FindItem(id))
		return item->displayName;
	if (const BlockDef *block = m_game->Content().FindBlock(id))
		return block->displayName;
	return id;
}

void Session::EnsureIcons(sdl3::Renderer &ren) {
	if (m_icons.IsSome() || !m_world)
		return;
	const Image &atlas = m_world->Atlas().Pixels();
	auto surface = sdl3::Surface::CreateFromPixels(atlas.width, atlas.height, sdl3::PixelFormat::RGBA32, atlas.rgba.data(), atlas.width * 4);
	if (surface.IsError())
		return;
	auto texture = sdl3::Texture::CreateFromSurface(ren, surface.Value());
	if (texture.IsError())
		return;
	(void)texture.Value().SetScaleMode(SDL_SCALEMODE_NEAREST);
	m_icons = Some(std::move(texture).Unwrap());
}

void Session::DrawItem(sdl3::Renderer &ren, const ItemStack &stack, sdl3::FRect cell, bool count) {
	EnsureIcons(ren);
	if (stack.IsEmpty() || m_icons.IsNone())
		return;
	const ItemDef *item = m_game->Content().FindItem(stack.item);
	const TextureAtlas::Rect r = m_world->Atlas().Lookup(item ? item->icon : String());
	const Image &atlas = m_world->Atlas().Pixels();
	const sdl3::FRect src{r.u0 * float(atlas.width), r.v0 * float(atlas.height), (r.u1 - r.u0) * float(atlas.width),
						  (r.v1 - r.v0) * float(atlas.height)};
	const float pad = cell.w * 0.14f;
	// Feuillages et herbes grises : teinte d'un biome tempéré, comme en jeu.
	sdl3::Color tint{255, 255, 255, 255};
	if (const BlockDef *block = m_game->Content().FindBlock(item ? item->placesBlock : String()))
		if (!block->look.biomeTint.IsEmpty())
			tint = block->look.biomeTint == "grass" ? sdl3::Color{124, 189, 80, 255} : sdl3::Color{98, 168, 58, 255};
	(void)m_icons.Value().SetColorMod(tint);
	(void)ren.Render(m_icons.Value(), src, sdl3::FRect{cell.x + pad, cell.y + pad, cell.w - 2.f * pad, cell.h - 2.f * pad});
	(void)m_icons.Value().SetColorMod(sdl3::Color{255, 255, 255, 255});
	Kit &k = m_shell.K();
	if (count && stack.count > 1) {
		const String text = String::Format("%d", stack.count);
		const float scale = std::max(1.f, cell.w / 26.f);
		const float w = k.Pixel().Measure(text, scale);
		k.Pixel().Draw(ren, text, cell.x + cell.w - w - 2.f, cell.y + cell.h - 9.f * scale, scale, sdl3::FColor::WHITE(), 1.f);
	}
	// Usure : barre sous l'icône.
	if (item && item->tool && item->tool->durability > 0 && stack.wear > 0) {
		const float k2 = 1.f - float(stack.wear) / float(item->tool->durability);
		ren.SetDrawColor(sdl3::FColor{0.f, 0.f, 0.f, 1.f});
		(void)ren.FillRect(sdl3::FRect{cell.x + 5.f, cell.y + cell.h - 6.f, cell.w - 10.f, 3.f});
		ren.SetDrawColor(sdl3::FColor{1.f - k2, k2, 0.15f, 1.f});
		(void)ren.FillRect(sdl3::FRect{cell.x + 5.f, cell.y + cell.h - 6.f, (cell.w - 10.f) * k2, 3.f});
	}
}

namespace {

/// Sprite de gui/icons.png (atlas 256×256 du pack).
struct Sprites {
	Option<sdl3::Texture> texture = NONE;
	bool tried = false;
	void Ensure(sdl3::Renderer &ren, const String &path) {
		if (tried)
			return;
		tried = true;
		if (auto t = sdl3::Texture::CreateFromFile(ren, path); t) {
			(void)t.Value().SetScaleMode(SDL_SCALEMODE_NEAREST);
			texture = Some(std::move(t).Unwrap());
		}
	}
	void Draw(sdl3::Renderer &ren, float sx, float sy, float sw, float sh, sdl3::FRect dst) const {
		if (texture.IsSome())
			(void)ren.Render(texture.Value(), sdl3::FRect{sx, sy, sw, sh}, dst);
	}
};
Sprites g_icons;

} // namespace

void Session::DrawBars(sdl3::Renderer &ren, float x, float y, float width) {
	const Settings &s = m_shell.Prefs();
	const float scale = 2.f * s.hudScale;
	const float icon = 9.f * scale, step = 8.f * scale;
	auto h = m_game->Registry().GetComponent<Health>(m_player);
	auto sv = m_game->Registry().GetComponent<Survival>(m_player);
	const bool survival = !m_game->Options().creative;
	// Expérience : barre au-dessus de la barre d'objets, niveau au centre.
	if (sv.IsSome()) {
		float progress = 0.f;
		const int level = XpLevel(sv.Unwrap()->xp, &progress);
		const float barW = width, barH = 5.f * s.hudScale;
		const sdl3::FRect bar{x, y - barH - 3.f, barW, barH};
		ren.SetDrawColor(WithAlpha(Rgb(10, 14, 8), 0.75f));
		(void)ren.FillRect(bar);
		ren.SetDrawColor(m_shell.K().Colors().xp);
		(void)ren.FillRect(sdl3::FRect{bar.x + 1.f, bar.y + 1.f, (bar.w - 2.f) * progress, bar.h - 2.f});
		if (level > 0) {
			const String text = String::Format("%d", level);
			const float ps = 1.5f * s.hudScale;
			m_shell.K().Pixel().Draw(ren, text, x + width * 0.5f - m_shell.K().Pixel().Measure(text, ps) * 0.5f, bar.y - 10.f * ps * 0.9f,
									 ps, m_shell.K().Colors().xp, 1.f);
		}
		y = bar.y - 4.f;
	}
	if (!survival)
		return;
	// Cœurs (à gauche) et faim (à droite), une icône = 2 points.
	const float rowY = y - icon - 2.f;
	if (h.IsSome()) {
		const int hearts = int(std::ceil(h.Unwrap()->max / 2.f));
		const bool flash = h.Unwrap()->hurtTime < 0.4f && !s.reduceFlashes;
		for (int i = 0; i < hearts; ++i) {
			const sdl3::FRect dst{x + float(i) * step, rowY, icon, icon};
			g_icons.Draw(ren, flash ? 25.f : 16.f, 0.f, 9.f, 9.f, dst);
			const float fill = h.Unwrap()->current / 2.f - float(i);
			if (fill >= 1.f)
				g_icons.Draw(ren, 52.f, 0.f, 9.f, 9.f, dst);
			else if (fill > 0.f)
				g_icons.Draw(ren, 61.f, 0.f, 9.f, 9.f, dst);
		}
	}
	if (sv.IsSome()) {
		for (int i = 0; i < 10; ++i) {
			const sdl3::FRect dst{x + width - float(i + 1) * step - 1.f, rowY, icon, icon};
			g_icons.Draw(ren, 16.f, 27.f, 9.f, 9.f, dst);
			const float fill = sv.Unwrap()->hunger / 2.f - float(i);
			if (fill >= 1.f)
				g_icons.Draw(ren, 52.f, 27.f, 9.f, 9.f, dst);
			else if (fill > 0.f)
				g_icons.Draw(ren, 61.f, 27.f, 9.f, 9.f, dst);
		}
		// Souffle (au-dessus de la faim) : sous l'eau, et quand on suffoque —
		// les bulles sur fond rouge.
		if (sv.Unwrap()->air < sv.Unwrap()->maxAir - 0.01f || sv.Unwrap()->suffocating) {
			const int bubbles = int(std::ceil(sv.Unwrap()->air / sv.Unwrap()->maxAir * 10.f));
			if (sv.Unwrap()->suffocating) {
				ren.SetDrawColor(Rgb(190, 40, 30, 170));
				(void)ren.FillRect(sdl3::FRect{x + width - 10.f * step - 2.f, rowY - icon - 3.f, 10.f * step + 2.f, icon + 2.f});
			}
			for (int i = 0; i < bubbles; ++i)
				g_icons.Draw(ren, 16.f, 18.f, 9.f, 9.f, sdl3::FRect{x + width - float(i + 1) * step - 1.f, rowY - icon - 2.f, icon, icon});
		}
		// Endurance : fine barre sous la faim.
		const float sw = 10.f * step;
		const sdl3::FRect stam{x + width - sw, rowY + icon + 1.f, sw * sv.Unwrap()->stamina, 2.f * s.hudScale};
		if (sv.Unwrap()->stamina < 0.999f) {
			ren.SetDrawColor(m_shell.K().Colors().stamina);
			(void)ren.FillRect(stam);
		}
	}
	// Armure (au-dessus des cœurs).
	if (const int armor = m_game->ArmorPoints(m_player); armor > 0)
		for (int i = 0; i < 10; ++i) {
			const sdl3::FRect dst{x + float(i) * step, rowY - icon - 2.f, icon, icon};
			const int points = armor - i * 2;
			g_icons.Draw(ren, points >= 2 ? 34.f : points == 1 ? 25.f : 16.f, 9.f, 9.f, 9.f, dst);
		}
}

void Session::DrawHud(sdl3::Renderer &ren, sdl3::FRect r) {
	if (!m_ready)
		return;
	const Settings &s = m_shell.Prefs();
	Kit &k = m_shell.K();
	const Palette &p = k.Colors();
	g_icons.Ensure(ren, m_shell.Opts().mods.front() + String("/core/textures/gui/icons.png"));
	(void)ren.SetBlendMode(sdl3::BlendMode::BLEND);
	// Effets d'écran : sous l'eau, blessure, vignette.
	if (s.screenEffects && m_game->GetWorld().BlockAt(BlockPos::FromWorld(m_camera.position)).fluid) {
		ren.SetDrawColor(Rgb(30, 70, 160, 90));
		(void)ren.FillRect(r);
	}
	// Suffocation (tête dans le sable, dans un bloc) : l'écran s'assombrit à
	// mesure que l'air manque.
	if (auto sv = m_game->Registry().GetComponent<Survival>(m_player); sv.IsSome() && sv.Unwrap()->suffocating) {
		const float missing = 1.f - sv.Unwrap()->air / std::max(0.1f, sv.Unwrap()->maxAir);
		ren.SetDrawColor(Rgb(70, 52, 30, uint8_t(std::clamp(120.f + missing * 120.f, 0.f, 235.f))));
		(void)ren.FillRect(r);
	}
	if (auto h = m_game->Registry().GetComponent<Health>(m_player); h.IsSome()) {
		if (h.Unwrap()->current < m_lastHealth - 0.01f)
			m_hurtFlash = s.reduceFlashes ? 0.15f : 0.35f;
		m_lastHealth = h.Unwrap()->current;
	}
	if (m_hurtFlash > 0.f && s.screenEffects) {
		ren.SetDrawColor(Rgb(200, 0, 0, int(m_hurtFlash * 260.f)));
		(void)ren.FillRect(r);
	}
	if (s.vignette) {
		const int steps = 10;
		for (int i = 0; i < steps; ++i) {
			const float t = float(i) / float(steps);
			ren.SetDrawColor(Rgb(0, 0, 0, int(26.f * (1.f - t))));
			const float m = t * 60.f;
			(void)ren.FillRect(sdl3::FRect{r.x, r.y + m, r.w, 6.f});
			(void)ren.FillRect(sdl3::FRect{r.x, r.y + r.h - m - 6.f, r.w, 6.f});
		}
	}
	if (!m_hudVisible)
		return;
	const float alpha = s.hudOpacity;
	const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
	// Réticule (forme, couleur et taille réglables).
	if (!m_thirdPerson) {
		const sdl3::FColor colors[] = {Rgb(255, 255, 255), Rgb(120, 255, 120), Rgb(255, 230, 80), Rgb(255, 90, 90), Rgb(90, 220, 255)};
		const sdl3::FColor c = WithAlpha(colors[std::clamp(s.crosshairColor, 0, 4)], 0.92f);
		const float size = 8.f * s.crosshairSize;
		ren.SetDrawColor(Rgb(0, 0, 0, 140));
		if (s.crosshair == 1) {
			(void)ren.FillRect(sdl3::FRect{cx - 3.f, cy - 3.f, 6.f, 6.f});
			ren.SetDrawColor(c);
			(void)ren.FillRect(sdl3::FRect{cx - 2.f, cy - 2.f, 4.f, 4.f});
		} else if (s.crosshair == 2) {
			ren.SetDrawColor(c);
			for (int i = 0; i < 24; ++i) {
				const float a = float(i) / 24.f * 6.2831853f;
				(void)ren.FillRect(sdl3::FRect{cx + std::cos(a) * size - 1.f, cy + std::sin(a) * size - 1.f, 2.f, 2.f});
			}
		} else {
			(void)ren.FillRect(sdl3::FRect{cx - size - 1.f, cy - 2.f, size * 2.f + 2.f, 4.f});
			(void)ren.FillRect(sdl3::FRect{cx - 2.f, cy - size - 1.f, 4.f, size * 2.f + 2.f});
			ren.SetDrawColor(c);
			(void)ren.FillRect(sdl3::FRect{cx - size, cy - 1.f, size * 2.f, 2.f});
			(void)ren.FillRect(sdl3::FRect{cx - 1.f, cy - size, 2.f, size * 2.f});
		}
	}
	// Progression du minage : arc sous le réticule.
	if (m_breakProgress > 0.f) {
		ren.SetDrawColor(Rgb(0, 0, 0, 150));
		(void)ren.FillRect(sdl3::FRect{cx - 30.f, cy + 18.f, 60.f, 6.f});
		ren.SetDrawColor(p.accent);
		(void)ren.FillRect(sdl3::FRect{cx - 29.f, cy + 19.f, 58.f * std::min(1.f, m_breakProgress), 4.f});
	}
	// Barre d'objets.
	const float slot = 46.f * s.hudScale;
	const float width = slot * float(HOTBAR);
	const float x0 = cx - width * 0.5f, y0 = r.y + r.h - slot - 14.f;
	ren.SetDrawColor(WithAlpha(Rgb(10, 13, 18), 0.55f * alpha));
	(void)ren.FillRect(sdl3::FRect{x0 - 4.f, y0 - 4.f, width + 6.f, slot + 6.f});
	voxel::Inventory *inv = Inventory();
	const int selected = SelectedSlot();
	for (int i = 0; i < HOTBAR; ++i) {
		const bool on = i == selected;
		const sdl3::FRect cell{x0 + float(i) * slot, y0 - (on ? 3.f : 0.f), slot - 2.f, slot - 2.f};
		ren.SetDrawColor(on ? WithAlpha(p.bg3, 0.9f * alpha) : WithAlpha(Rgb(30, 36, 46), 0.65f * alpha));
		(void)ren.FillRect(cell);
		ren.SetDrawColor(on ? WithAlpha(Rgb(255, 255, 255), alpha) : WithAlpha(p.line, 0.8f * alpha));
		(void)ren.DrawRect(cell);
		if (on) {
			(void)ren.DrawRect(sdl3::FRect{cell.x + 1.f, cell.y + 1.f, cell.w - 2.f, cell.h - 2.f});
			ren.SetDrawColor(p.primary);
			(void)ren.FillRect(sdl3::FRect{cell.x + 2.f, cell.y + cell.h - 3.f, cell.w - 4.f, 2.f});
		}
		if (inv)
			DrawItem(ren, inv->Slot(i), cell);
		// Numéro de l'emplacement.
		k.Pixel().Draw(ren, String::Format("%d", i + 1), cell.x + 3.f, cell.y + 2.f, 1.f, WithAlpha(p.textDim, 0.9f), 0.f);
	}
	DrawBars(ren, x0, y0 - 6.f, width - 2.f);
	// Nom de l'objet tenu (au-dessus des jauges).
	if (ItemStack *held = m_game->HeldItem(m_player); held && !held->IsEmpty()) {
		const String name = ItemName(held->item);
		const float ps = 1.5f * s.hudScale;
		const float w = k.Pixel().Measure(name, ps);
		k.Pixel().Draw(ren, name, cx - w * 0.5f, y0 - 64.f * s.hudScale, ps, WithAlpha(p.text, 0.95f), 1.f);
	}
	// Effets de statut (haut droite) : pastille colorée + durée.
	if (auto fx = m_game->Registry().GetComponent<StatusEffects>(m_player); fx.IsSome()) {
		float ex = r.x + r.w - 16.f;
		for (const StatusEffect &e : fx.Unwrap()->list) {
			const NodePtr def = json::At(m_game->Content().Config(), (String("effects.") + e.id).CStr());
			const auto color = json::Vec3(def, "color", {180.f, 180.f, 180.f});
			const bool good = json::Bool(def, "good", true);
			const sdl3::FRect box{ex - 44.f, r.y + 14.f, 44.f, 44.f};
			ren.SetDrawColor(WithAlpha(Rgb(10, 13, 18), 0.7f));
			(void)ren.FillRect(box);
			ren.SetDrawColor(good ? p.success : p.danger);
			(void)ren.DrawRect(box);
			ren.SetDrawColor(Rgb(int(color.x), int(color.y), int(color.z)));
			(void)ren.FillRect(sdl3::FRect{box.x + 10.f, box.y + 6.f, 24.f, 20.f});
			const String name = json::Str(def, "name", e.id);
			k.Pixel().Draw(ren, name.Substr(0, 1).ToUpper() + (e.level > 1 ? String::Format("%d", e.level) : String()), box.x + 15.f,
						   box.y + 10.f, 1.5f, sdl3::FColor::WHITE(), 1.f);
			const String time = String::Format("%d:%02d", int(e.remaining) / 60, int(e.remaining) % 60);
			k.Pixel().Draw(ren, time, box.x + 22.f - k.Pixel().Measure(time, 1.f) * 0.5f, box.y + 31.f, 1.f, p.text, 0.f);
			ex -= 50.f;
		}
	}
	// Boussole d'un mod d'interface (configuration « hud.compass »).
	if (json::Bool(m_game->Content().Config(), "hud.compass", false))
		if (auto t = m_game->Registry().GetComponent<Transform>(m_player); t.IsSome()) {
			const float w = 320.f, top = r.y + 64.f;
			ren.SetDrawColor(WithAlpha(Rgb(10, 13, 18), 0.6f * alpha));
			(void)ren.FillRect(sdl3::FRect{cx - w * 0.5f, top, w, 22.f});
			const char *names[8] = {"S", "SO", "O", "NO", "N", "NE", "E", "SE"};
			for (int i = 0; i < 8; ++i) {
				float a = float(i) * 0.785398f - t.Unwrap()->yaw;
				a = std::remainder(a, 6.2831853f);
				if (std::fabs(a) > 1.2f)
					continue;
				const float px = cx + a / 1.2f * w * 0.5f;
				const float ps = i % 2 == 0 ? 1.5f : 1.f;
				k.Pixel().Draw(ren, names[i], px - k.Pixel().Measure(names[i], ps) * 0.5f, top + 4.f, ps,
							   i == 4 ? p.danger : p.text, 1.f);
			}
			ren.SetDrawColor(p.accent);
			(void)ren.FillRect(sdl3::FRect{cx - 1.f, top - 3.f, 2.f, 28.f});
		}
}

void Session::UpdateHudLabels() {
	if (!m_hud.Valid())
		return;
	Kit &k = m_shell.K();
	const Settings &s = m_shell.Prefs();
	auto t = m_game->Registry().GetComponent<Transform>(m_player);
	if (t.IsNone())
		return;
	const math::FVector3 pos = t.Unwrap()->position;
	const BlockPos b = BlockPos::FromWorld(pos);
	const BiomeDef *biome = m_game->GetWorld().Generator() ? m_game->GetWorld().Generator()->BiomeAt(b.x, b.z) : nullptr;
	const float day = m_game->DayFraction();
	const int hours = int(day * 24.f), minutes = int((day * 24.f - float(hours)) * 60.f);
	const bool night = day > 0.78f || day < 0.22f;
	k.SetText(m_infoLines[0], s.showClock ? String::Format("%s  %02d:%02d — jour %d", night ? "☾" : "☀", hours, minutes,
														   int(m_game->GameTime() / double(m_game->Options().dayLength)) + 1)
										  : String());
	k.SetText(m_infoLines[1], s.showBiome && biome ? biome->displayName + String("  ·  ") + String(GameModeName(GameModeId(m_game->Mode())))
												   : String());
	k.SetText(m_infoLines[2], s.showCoordinates && m_game->Rule("showCoordinates")
								  ? String::Format("X %d   Y %d   Z %d%s", b.x, b.y, b.z, s.showFps ? String::Format("   ·   %.0f img/s", double(m_fps)).CStr() : "")
								  : s.showFps ? String::Format("%.0f img/s", double(m_fps)) : String());
	for (size_t i = 0; i < m_infoLines.size(); ++i)
		k.SetHidden(m_infoLines[i], m_infoLines[i].Text().IsEmpty());
	// Cible : créature (santé) ou bloc (outil conseillé).
	String name, detail;
	if (Option<EntityHit> e = m_game->TargetEntity(m_player); e.IsSome()) {
		auto kind = m_game->Registry().GetComponent<EntityKind>(e.Value().entity);
		auto h = m_game->Registry().GetComponent<Health>(e.Value().entity);
		name = kind.IsSome() && kind.Unwrap()->def ? kind.Unwrap()->def->displayName : String("Entité");
		if (h.IsSome())
			detail = String::Format("Santé %.0f / %.0f", double(std::max(0.f, h.Unwrap()->current)), double(h.Unwrap()->max));
		if (kind.IsSome() && kind.Unwrap()->def && !kind.Unwrap()->def->onInteract.IsEmpty())
			detail += String("   ·   clic droit : interagir");
	} else if (Option<BlockHit> hit = m_game->TargetBlock(m_player); hit.IsSome()) {
		const BlockDef &def = m_game->GetWorld().BlockAt(hit.Value().block);
		name = def.displayName;
		if (def.hardness < 0.f)
			detail = String("Incassable");
		else if (!def.preferredTool.IsEmpty())
			detail = String::Format("Outil conseillé : %s", def.preferredTool == "pickaxe" ? "pioche"
														 : def.preferredTool == "axe"	  ? "hache"
														 : def.preferredTool == "shovel"  ? "pelle"
														 : def.preferredTool == "shears"  ? "cisailles"
																						  : def.preferredTool.CStr());
		if (std::find(def.tags.begin(), def.tags.end(), String("station")) != def.tags.end())
			detail = String("Atelier : débloque des recettes à proximité");
	}
	k.SetText(m_targetName, name);
	k.SetText(m_targetDetail, detail);
	k.SetHidden(m_targetPanel, name.IsEmpty() || !m_hudVisible);
	// Indications contextuelles.
	std::vector<String> hints;
	if (s.showHints) {
		auto key = [&](const char *action) {
			const KeyBinding *binding = s.Binding(action);
			return binding ? InputName(binding->keys[0]) : String("?");
		};
		if (!name.IsEmpty() && detail.Contains("interagir"))
			hints.push_back(key("use") + String("  Parler / échanger"));
		hints.push_back(key("inventory") + String("  Inventaire   ·   ") + key("crafting") + String("  Fabrication"));
		hints.push_back(key("chat") + String("  Discussion   ·   ") + key("pause") + String("  Menu"));
	}
	for (size_t i = 0; i < m_hintLines.size(); ++i)
		k.SetText(m_hintLines[i], i < hints.size() ? hints[i] : String());
	k.SetHidden(m_hintPanel, hints.empty() || !m_hudVisible);
	// F3.
	if (m_debug) {
		k.SetText(m_debugLines[0], String::Format("%.0f img/s — %.2f %.2f %.2f — chunk %s", double(m_fps), double(pos.x), double(pos.y),
												  double(pos.z), ChunkOf(b).ToString().CStr()));
		k.SetText(m_debugLines[1], String::Format("%d chunk(s) chargé(s), %d maillé(s), %d en attente, %dk triangles",
												  int(m_game->GetWorld().LoadedChunkCount()), int(m_world->ChunkMeshCount()),
												  int(m_world->PendingChunks()), int(m_world->TriangleCount() / 1000)));
		k.SetText(m_debugLines[2], String::Format("%d entité(s), %d ressource(s), %d voix, graine %u", int(m_game->Registry().EntityCount()),
												  int(m_game->Assets().ResidentCount()), int(m_audio->VoiceCount()), m_game->Metadata().seed));
		k.SetText(m_debugLines[3], String::Format("lacet %.0f°, tangage %.0f°, lumière %s", double(t.Unwrap()->yaw * 57.3f),
												  double(t.Unwrap()->pitch * 57.3f), night ? "nuit" : "jour"));
		k.SetText(m_debugLines[4], String::Format("%d mod(s) — %d définition(s) — %s : pas %.1f ms, maillage %.1f ms/chunk (%d en cours)",
												  int(m_game->Mods().mods.size()), int(m_game->Content().DefinitionCount()),
												  m_updateThread->Threaded() ? "fils" : "fil unique", double(m_tickMs),
												  double(m_world->MeshMilliseconds()), int(m_world->MeshingChunks())));
	}
}

// ── Journal ──────────────────────────────────────────────────────────────────

void Session::AddLog(const String &text, const String &channel, const String &author) {
	m_logLines.push_back(LogLine{text, channel, author, Now(), 8.f});
	if (m_logLines.size() > 300)
		m_logLines.pop_front();
}

void Session::UpdateMessages(float dt) {
	ecs::EventsOf<GameMessage>(m_game->Registry()).Read(m_messageCursor, [this](const GameMessage &m) {
		String channel = m.channel;
		String author;
		String text = m.text;
		if (channel == "chat" && text.StartsWith("<")) {
			const size_t end = text.Find(">");
			if (end != String::NPOS) {
				author = text.Substring(1, end);
				text = text.Substring(std::min(text.GetSize(), end + 2));
			}
		} else if (channel == "erreur") {
			channel = "error";
		} else if (channel == "console" || channel == "exec") {
			channel = "result";
		} else if (channel != "event" && channel != "chat") {
			channel = "system";
		}
		AddLog(text, channel, author);
	});
	for (LogLine &l : m_logLines)
		l.shown -= dt;
	if (!m_chatPanel.Valid())
		return;
	Kit &k = m_shell.K();
	const Palette &p = k.Colors();
	const Settings &s = m_shell.Prefs();
	const bool chatOpen = m_shell.HasScreen("chat") || m_shell.HasScreen("console");
	std::vector<const LogLine *> shown;
	for (auto it = m_logLines.rbegin(); it != m_logLines.rend() && shown.size() < m_chatLines.size(); ++it)
		if ((it->shown > 0.f && it->channel != "command" && it->channel != "result") && !chatOpen)
			shown.push_back(&*it);
	for (size_t i = 0; i < m_chatLines.size(); ++i) {
		const size_t from = m_chatLines.size() - 1 - i;
		ui::Widget e = m_chatLines[i];
		if (from < shown.size()) {
			const LogLine &l = *shown[from];
			const String text = l.author.IsEmpty() ? l.text : String::Format("<%s> %s", l.author.CStr(), l.text.CStr());
			k.SetText(e, text);
			const sdl3::FColor c = l.channel == "event" ? p.accent : l.channel == "error" ? p.danger : l.channel == "system" ? p.info : p.text;
			const float fade = std::clamp(l.shown, 0.f, 1.f);
			e.Color(WithAlpha(c, fade));
			m_chatRows[i].Background(WithAlpha(Rgb(8, 10, 14), s.chatOpacity * 0.8f * fade));
			k.SetHidden(m_chatRows[i], false);
		} else {
			k.SetHidden(m_chatRows[i], true);
		}
	}
}

void Session::Submit(const String &raw) {
	const String text = raw.Trim();
	if (text.IsEmpty())
		return;
	m_history.push_back(text);
	if (text.StartsWith("/"))
		(void)Command(text);
	else
		m_game->Message(String::Format("<%s> %s", m_shell.Prefs().playerName.CStr(), text.CStr()), String("chat"));
}

Result<String, String> Session::Command(const String &line) {
	String text = line.Trim();
	if (!text.StartsWith("/"))
		text = String("/") + text;
	AddLog(text, "command");
	// Commandes de triche refusées si le monde ne les autorise pas.
	static const char *safe[] = {"/help", "/pos", "/recipes", "/craft", "/skin", "/skinnext", "/seed", "/mods", "/stats", "/sethome", "/home"};
	if (!m_game->Rule("commands") && std::none_of(std::begin(safe), std::end(safe), [&](const char *c) { return text.StartsWith(c); })) {
		AddLog("Les commandes sont désactivées dans ce monde (Configuration du monde > Règles).", "error");
		return Err(String("commandes désactivées"));
	}
	Result<String, String> r = m_game->Execute(text, m_player);
	const String out = r.IsOk() ? r.Value() : r.Error();
	size_t start = 0;
	while (start < out.GetSize()) {
		size_t end = out.Find("\n", start);
		if (end == String::NPOS)
			end = out.GetSize();
		if (end > start)
			AddLog(out.Substring(start, end), r.IsOk() ? "result" : "error");
		start = end + 1;
	}
	return r;
}

void Session::Craft(const String &recipe, int times) {
	Result<int, String> r = m_game->Craft(m_player, recipe, times);
	if (r.IsError()) {
		m_shell.UiSound("ui_error");
		m_shell.Notify(Tone::WARNING, "Fabrication impossible", r.Error());
		return;
	}
	m_stats.itemsCrafted += r.Value();
	if (const RecipeDef *def = m_game->Content().FindRecipe(recipe))
		AddLog(String::Format("Fabriqué : %d × %s", r.Value() * def->resultCount, ItemName(def->result).CStr()), "event");
}

// ── Commandes du joueur ──────────────────────────────────────────────────────

int Session::SelectedSlot() {
	auto p = m_game->Registry().GetComponent<voxel::Player>(m_player);
	return p.IsSome() ? p.Unwrap()->selectedSlot : 0;
}

void Session::SelectSlot(int slot) {
	if (auto p = m_game->Registry().GetComponent<voxel::Player>(m_player); p.IsSome())
		p.Unwrap()->selectedSlot = ((slot % HOTBAR) + HOTBAR) % HOTBAR;
	ResetBreaking();
}

void Session::SetCaptured(bool captured) {
	if (!m_ready)
		captured = false;
	m_captured = captured;
	(void)m_shell.Window().SetRelativeMouseMode(captured);
	if (captured)
		m_shell.Gui().ClearFocus();
	if (!captured) {
		m_leftDown = m_rightDown = false;
		ResetBreaking();
	}
}

void Session::HandleEvent(sdl3::Event &e, bool captureAllowed) {
	const Settings &s = m_shell.Prefs();
	if (e.IsMouseMotion() && m_captured) {
		if (auto t = m_game->Registry().GetComponent<Transform>(m_player); t.IsSome()) {
			// La tête en bas (gravité inversée), la vue est retournée : la
			// souris garde son sens à l'écran.
			const float flip = Inverted() ? -1.f : 1.f;
			const float k = 0.0025f * s.mouseSensitivity;
			t.Unwrap()->yaw -= float(e.MouseMotion().xrel) * k * flip;
			t.Unwrap()->pitch = std::clamp(t.Unwrap()->pitch - float(e.MouseMotion().yrel) * k * flip * (s.invertY ? -1.f : 1.f), -1.55f, 1.55f);
		}
		return;
	}
	int code = 0;
	bool down = false;
	if (e.IsKeyDown() && !e.Key().repeat) {
		code = int(e.Key().scancode);
		down = true;
	} else if (e.IsMouseDown()) {
		code = MOUSE_BASE + int(e.MouseButton().button);
		down = true;
		if (!m_captured && captureAllowed) {
			SetCaptured(true);
			return;
		}
	} else if (e.IsMouseUp()) {
		code = MOUSE_BASE + int(e.MouseButton().button);
	} else if (e.IsMouseWheel()) {
		code = e.MouseWheel().y > 0 ? WHEEL_UP : e.MouseWheel().y < 0 ? WHEEL_DOWN : 0;
		down = code != 0;
	}
	if (code == 0)
		return;
	if (!down) {
		if (s.Matches("attack", code)) {
			m_leftDown = false;
			ResetBreaking();
		}
		if (s.Matches("use", code))
			m_rightDown = false;
		return;
	}
	if (s.Matches("attack", code)) {
		m_leftDown = true;
		m_swing = 0.25f;
	}
	if (s.Matches("use", code)) {
		m_rightDown = true;
		m_useTimer = 0.f;
	}
	if (s.Matches("pick", code))
		PickBlock();
	if (s.Matches("nextSlot", code))
		SelectSlot(SelectedSlot() + 1);
	if (s.Matches("prevSlot", code))
		SelectSlot(SelectedSlot() - 1);
	for (int i = 1; i <= HOTBAR; ++i)
		if (s.Matches(String::Format("slot%d", i), code))
			SelectSlot(i - 1);
	if (s.Matches("drop", code))
		DropSelected(SDL_GetModState() & SDL_KMOD_CTRL);
	if (s.Matches("sprint", code) && s.toggleSprint)
		m_sprintToggled = !m_sprintToggled;
	if (s.Matches("sneak", code) && s.toggleSneak)
		m_sneakToggled = !m_sneakToggled;
	if (s.Matches("fly", code)) {
		if (m_game->Mode() == GameMode::CREATIVE || m_game->Mode() == GameMode::SPECTATOR)
			(void)Command("/fly");
	}
	if (s.Matches("inventory", code))
		m_shell.Push(MakeInventory(m_shell));
	else if (s.Matches("crafting", code))
		m_shell.Push(MakeCrafting(m_shell));
	else if (s.Matches("chat", code))
		m_shell.Push(MakeChat(m_shell, String()));
	else if (s.Matches("command", code))
		m_shell.Push(MakeConsole(m_shell, String("/")));
	else if (s.Matches("worldInfo", code))
		m_shell.Push(MakeWorldInfo(m_shell));
	else if (s.Matches("pause", code)) {
		m_shell.pendingThumbnail = m_shell.Worlds().Directory() + String("/") + m_folder + String("/thumbnail.png");
		m_shell.Push(MakePause(m_shell));
	} else if (s.Matches("hideHud", code))
		SetHudVisible(!m_hudVisible);
	else if (s.Matches("debug", code)) {
		m_debug = !m_debug;
		m_shell.K().SetHidden(m_debugPanel, !m_debug);
	} else if (s.Matches("camera", code))
		m_thirdPerson = !m_thirdPerson;
	else if (s.Matches("save", code))
		Save(true);
}

void Session::UpdateControls() {
	const Settings &s = m_shell.Prefs();
	auto intent = m_game->Registry().GetComponent<MoveIntent>(m_player);
	auto t = m_game->Registry().GetComponent<Transform>(m_player);
	if (intent.IsNone() || t.IsNone())
		return;
	MoveIntent &i = *intent.Unwrap();
	i.wish = {0.f, 0.f, 0.f};
	i.jump = i.sneak = i.sprint = i.descend = false;
	if (!m_captured)
		return;
	const float yaw = t.Unwrap()->yaw;
	const math::FVector3 forward{std::sin(yaw), 0.f, std::cos(yaw)}, right{-std::cos(yaw), 0.f, std::sin(yaw)};
	const float f = float(Held(s, "forward")) - float(Held(s, "back"));
	const float r = (float(Held(s, "right")) - float(Held(s, "left"))) * (Inverted() ? -1.f : 1.f);
	math::FVector3 wish = forward * f + right * r;
	if (wish.Length() > 1.f)
		wish = wish.Normalize();
	i.wish = wish;
	auto body = m_game->Registry().GetComponent<PhysicsBody>(m_player);
	i.jump = Held(s, "jump") || (s.autoJump && body.IsSome() && body.Unwrap()->hitWall && body.Unwrap()->onGround && f > 0.f);
	const bool sneak = s.toggleSneak ? m_sneakToggled : Held(s, "sneak");
	if (body.IsSome() && body.Unwrap()->flies)
		i.descend = sneak;
	else
		i.sneak = sneak;
	i.sprint = (s.toggleSprint ? m_sprintToggled : Held(s, "sprint")) && f > 0.f;
	if (body.IsSome() && body.Unwrap()->flies)
		i.speed = float(m_game->Content().ConfigNum("player.speed", 4.3)) * 2.5f;
}

void Session::ResetBreaking() {
	m_breakProgress = 0.f;
	m_breaking = BlockPos{0, -100000, 0};
}

void Session::UpdateActions(float dt, const Option<BlockHit> &target) {
	m_attackTimer -= dt;
	m_useTimer -= dt;
	m_breakRepeat -= dt;
	if (m_leftDown) {
		if (Option<EntityHit> entity = m_game->TargetEntity(m_player); entity.IsSome()) {
			if (m_attackTimer <= 0.f) {
				float damage = 1.f;
				if (ItemStack *held = m_game->HeldItem(m_player); held && !held->IsEmpty())
					if (const ItemDef *item = m_game->Content().FindItem(held->item); item && item->tool)
						damage = item->tool->damage;
				const ecs::Entity victim = entity.Value().entity;
				m_game->Damage(victim, damage, m_player);
				m_game->AddExhaustion(m_player, 0.1f);
				if (auto t = m_game->Registry().GetComponent<Transform>(victim); t.IsSome())
					m_game->PlaySound("core:attack", t.Unwrap()->position, 0.6f);
				m_attackTimer = 0.45f;
			}
			ResetBreaking();
		} else if (target.IsSome()) {
			const BlockPos pos = target.Value().block;
			if (!(pos == m_breaking)) {
				m_breaking = pos;
				m_breakProgress = 0.f;
			}
			const float time = m_game->BreakTime(m_player, pos);
			if (time >= 0.f && m_breakRepeat <= 0.f) {
				m_breakProgress += time <= 0.f ? 1.f : dt / time;
				m_hitTimer -= dt;
				if (m_hitTimer <= 0.f && m_breakProgress < 1.f) {
					m_game->PlayBlockSound(m_game->GetWorld().GetBlock(pos), "hit", pos);
					m_hitTimer = 0.25f;
				}
				if (m_breakProgress >= 1.f) {
					const BlockId broken = m_game->GetWorld().GetBlock(pos);
					if (m_game->BreakBlock(m_player, pos)) {
						++m_stats.blocksBroken;
						if (m_shell.Prefs().particles > 0)
							m_world->SpawnBreakParticles(broken, pos, m_shell.Prefs().particles == 2 ? 18 : 8);
					}
					ResetBreaking();
					m_breakRepeat = m_game->Options().creative ? 0.2f : 0.05f;
				}
			}
		} else {
			ResetBreaking();
		}
	}
	if (m_rightDown && m_useTimer <= 0.f) {
		const uint64_t before = m_game->GetWorld().LoadedChunkCount();
		(void)before;
		const BlockId placedBefore = target.IsSome() ? m_game->GetWorld().GetBlock(target.Value().previous) : AIR;
		if (m_game->UseHeldItem(m_player) && target.IsSome() && placedBefore == AIR &&
			m_game->GetWorld().GetBlock(target.Value().previous) != AIR)
			++m_stats.blocksPlaced;
		m_useTimer = 0.25f;
		m_swing = 0.2f;
	}
}

void Session::PickBlock() {
	Option<BlockHit> hit = m_game->TargetBlock(m_player);
	voxel::Inventory *inv = Inventory();
	if (hit.IsNone() || !inv)
		return;
	const String id = m_game->GetWorld().BlockAt(hit.Value().block).id;
	for (int i = 0; i < HOTBAR; ++i)
		if (inv->Slot(i).item == id) {
			SelectSlot(i);
			return;
		}
	if (m_game->Mode() == GameMode::CREATIVE)
		inv->Slot(SelectedSlot()) = ItemStack{id, 64, 0, nullptr};
}

void Session::DropSelected(bool all) {
	ItemStack *held = m_game->HeldItem(m_player);
	auto t = m_game->Registry().GetComponent<Transform>(m_player);
	if (!held || held->IsEmpty() || t.IsNone())
		return;
	const int count = all ? held->count : 1;
	const math::FVector3 forward = t.Unwrap()->Forward();
	(void)m_game->DropItem(ItemStack{held->item, count, held->wear, held->meta},
						   t.Unwrap()->position + math::FVector3{0.f, EYE - 0.3f, 0.f} + forward * 0.6f, forward * 5.f);
	held->count -= count;
	if (held->count <= 0)
		*held = ItemStack{};
}

render3d::Camera Session::CameraFor() {
	const Settings &s = m_shell.Prefs();
	render3d::Camera camera;
	float fov = s.fov;
	auto intent = m_game->Registry().GetComponent<MoveIntent>(m_player);
	if (intent.IsSome() && intent.Unwrap()->sprint && !s.reduceMotion)
		fov += 6.f;
	camera.fovYRadians = fov * 3.14159265f / 180.f;
	camera.nearPlane = 0.05f;
	camera.farPlane = float(s.viewDistance + 2) * 16.f * 1.6f;
	auto t = m_game->Registry().GetComponent<Transform>(m_player);
	if (t.IsNone())
		return camera;
	const float alpha = m_game->GetSchedule().FixedAlpha();
	const math::FVector3 feet = t.Unwrap()->previous + (t.Unwrap()->position - t.Unwrap()->previous) * alpha;
	float eye = EYE - (intent.IsSome() && intent.Unwrap()->sneak ? 0.15f : 0.f);
	if (s.viewBobbing && !s.reduceMotion && !m_thirdPerson)
		eye += std::sin(m_bob * 2.f) * 0.04f;
	// Gravité inversée : la tête en bas, les yeux près des « pieds » de la
	// boîte, la vue retournée.
	if (Inverted()) {
		eye = 1.8f - eye;
		camera.up = {0.f, -1.f, 0.f};
	}
	camera.position = feet + math::FVector3{0.f, eye, 0.f};
	const math::FVector3 forward = t.Unwrap()->Forward();
	if (m_thirdPerson) {
		// Derrière le joueur, sans traverser les murs.
		float distance = 4.f;
		if (Option<BlockHit> hit = RaycastBlocks(m_game->GetWorld(), camera.position, forward * -1.f, distance); hit.IsSome())
			distance = std::max(0.6f, hit.Value().distance - 0.3f);
		const math::FVector3 head = camera.position;
		camera.position = head - forward * distance;
		camera.target = head;
	} else {
		camera.target = camera.position + forward;
	}
	return camera;
}

// ── Son ──────────────────────────────────────────────────────────────────────

float Session::CategoryVolume(const String &sound) const {
	const Settings &s = m_shell.Prefs();
	// Nom sans son mod (« backrooms:backrooms_amb_city » → « backrooms_amb_city »).
	const size_t colon = sound.View().Find(':');
	const String id = colon != StringView::NPOS ? sound.Substring(colon + 1) : sound;
	if (id.StartsWith("music") || id.Contains("_music"))
		return s.musicVolume;
	if (id.StartsWith("amb_") || id.Contains("_amb_"))
		return s.ambienceVolume;
	if (id.StartsWith("ui_"))
		return s.uiVolume;
	if (id.Contains("villager") || id.Contains("guard_idle"))
		return s.voiceVolume;
	return s.effectsVolume;
}

void Session::UpdateSound() {
	const Settings &s = m_shell.Prefs();
	ecs::EventsOf<SoundEvent>(m_game->Registry()).Read(m_soundCursor, [&](const SoundEvent &ev) {
		if (ev.stop) { // canal d'ambiance éteint en fondu
			(void)m_audio->StopChannel(ev.channel, Some(ev.fadeOut));
			return;
		}
		const resources::AssetId id = m_game->Assets().SoundId(ev.sound, ev.variant);
		if (!m_game->Assets().Cache().Knows(id))
			return;
		const float volume = ev.volume * CategoryVolume(ev.sound);
		if (volume <= 0.001f) {
			if (!ev.channel.IsEmpty()) // catégorie coupée : le canal se tait aussi
				(void)m_audio->StopChannel(ev.channel, Some(ev.fadeOut));
			return;
		}
		audio::AudioPlayOptions options;
		options.volume = std::clamp(volume, 0.f, 2.f);
		options.pitch = ev.pitch;
		options.spatial = ev.positional && s.spatialAudio;
		options.position = ev.position;
		options.loop = ev.loop;
		if (!ev.channel.IsEmpty()) {
			// Ambiance, musique : un son à la fois par canal, en fondu enchaîné.
			(void)m_audio->PlayOnChannel(ev.channel, resources::AssetRef<audio::AudioClipResource>{id}, options,
										 Some(audio::ChannelFade{ev.fadeIn, ev.fadeOut}));
			return;
		}
		(void)m_audio->Play(resources::AssetRef<audio::AudioClipResource>{id}, options);
		// Sous-titres : sons des créatures et des événements.
		if (s.subtitles && ev.positional) {
			String what;
			const String sound = ev.sound;
			if (sound.Contains("zombie"))
				what = "Grognement de zombie";
			else if (sound.Contains("skeleton"))
				what = "Cliquetis d'os";
			else if (sound.Contains("creeper") || sound.Contains("fuse"))
				what = "Sifflement";
			else if (sound.Contains("explosion"))
				what = "Explosion";
			else if (sound.Contains("spider"))
				what = "Araignée";
			else if (sound.Contains("villager"))
				what = "Villageois";
			else if (sound.Contains("ghost"))
				what = "Plainte spectrale";
			if (!what.IsEmpty())
				AddLog(String::Format("[son] %s", what.CStr()), "event");
		}
	});
	m_audio->SetListener(m_camera.ViewMatrix());
	m_audio->Update();
	for (const String &e : m_audio->TakeErrors())
		m_shell.Log(String("[son] ") + e);
}

// ── Mise à jour ──────────────────────────────────────────────────────────────

void Session::HandleUiRequests() {
	for (UiRequest &r : m_game->TakeUiRequests()) {
		if (r.kind == "trade")
			m_shell.Push(MakeTrade(m_shell, std::move(r)));
		else if (r.kind == "dialog")
			m_shell.Push(MakeDialog(m_shell, std::move(r)));
		else if (r.kind == "container")
			m_shell.Push(MakeContainer(m_shell, std::move(r)));
		else if (r.kind == "form")
			m_shell.Push(MakeForm(m_shell, std::move(r)));
	}
}

void Session::UpdateStats(float dt) {
	m_stats.sessionTime += double(dt);
	if (auto t = m_game->Registry().GetComponent<Transform>(m_player); t.IsSome()) {
		const math::FVector3 d = t.Unwrap()->position - m_lastPosition;
		const float step = std::sqrt(d.x * d.x + d.z * d.z);
		if (step < 10.f)
			m_stats.distance += double(step);
		m_bob += step * 3.f;
		m_lastPosition = t.Unwrap()->position;
	}
	ecs::EventsOf<EntityDied>(m_game->Registry()).Read(m_diedCursor, [&](const EntityDied &d) {
		if (d.entity == m_player) {
			++m_stats.deaths;
			m_shell.Notify(Tone::DANGER, "Vous êtes mort", "Vous réapparaissez au point de départ.", true);
		} else if (const EntityDef *def = m_game->Content().FindEntity(d.kind)) {
			(void)def;
			++m_stats.creaturesKilled;
		}
	});
	// Images par seconde.
	++m_fpsFrames;
	const uint64_t now = sdl3::GetPerformanceCounter();
	if (m_fpsTicks == 0)
		m_fpsTicks = now;
	const double elapsed = double(now - m_fpsTicks) / double(sdl3::GetPerformanceFrequency());
	if (elapsed > 0.5) {
		m_fps = float(double(m_fpsFrames) / elapsed);
		m_fpsFrames = 0;
		m_fpsTicks = now;
	}
}

void Session::Update(float dt, bool paused, bool panelOpen) {
	if (!m_ready)
		return;
	UpdateControls();
	if (m_updateThread->Threaded())
		m_pendingTick = paused ? -1.f : dt; // avancera sur le fil de mise à jour, après l'image
	else if (!paused)
		RunTick(dt);
	Option<BlockHit> target;
	if (!paused && !panelOpen && m_captured) {
		target = m_game->TargetBlock(m_player);
		UpdateActions(dt, target);
	} else {
		m_leftDown = m_rightDown = false;
		ResetBreaking();
	}
	// Le joueur est visible en vue à la troisième personne.
	if (auto v = m_game->Registry().GetComponent<Visual>(m_player); v.IsSome())
		v.Unwrap()->visible = m_thirdPerson;
	m_camera = CameraFor();
	m_world->Update(m_camera, m_game->GetSchedule().FixedAlpha(), m_shell.Prefs().placeAssist ? target : Option<BlockHit>(NONE));
	m_hurtFlash = std::max(0.f, m_hurtFlash - dt);
	UpdateSound();
	UpdateMessages(dt);
	UpdateStats(dt);
	UpdateHudLabels();
	HandleUiRequests();
}

void Session::RunTick(float dt) {
	const uint64_t start = sdl3::GetPerformanceCounter();
	m_game->Tick(dt);
	const float ms = float(double(sdl3::GetPerformanceCounter() - start) * 1000.0 / double(sdl3::GetPerformanceFrequency()));
	m_tickMs = m_tickMs <= 0.f ? ms : m_tickMs * 0.95f + ms * 0.05f;
}

void Session::BeginSimulation() {
	if (!m_ready || m_pendingTick < 0.f)
		return;
	const float dt = m_pendingTick;
	m_pendingTick = -1.f;
	m_ticked = true;
	m_updateThread->Post([this, dt]() {
		// Les scripts sont pilotés par ce fil pendant le pas : l'API de l'hôte
		// s'y exécute (sinon l'interpréteur la renverrait au fil principal,
		// qui attend ce pas — interblocage).
		m_game->Scripts().Vm().SetMainThread();
		RunTick(dt);
	});
}

void Session::EndSimulation() {
	if (!m_ticked)
		return;
	m_updateThread->Wait();
	m_ticked = false;
	if (m_game)
		m_game->Scripts().Vm().SetMainThread(); // de retour au fil principal (commandes, actions du joueur)
}

Result<Session::ContentReload, String> Session::ReloadDefinition(const NodePtr &definition,
																 const ContentRegistry::LoadContext &context) {
	if (!m_game || !m_world)
		return Err(String("aucune partie prête"));
	ContentReload out = ReloadDefinitions({Definition{definition, context}});
	if (!out.errors.empty())
		return Err(out.errors.front());
	return Ok(std::move(out));
}

Session::ContentReload Session::ReloadDefinitions(const std::vector<Definition> &definitions) {
	ContentReload out;
	if (!m_game || !m_world) {
		out.errors.push_back(String("aucune partie prête"));
		return out;
	}
	const uint64_t start = sdl3::GetPerformanceCounter();
	EndSimulation();    // la simulation lit le contenu à chaque pas
	m_world->Quiesce(); // les fils de maillage aussi
	const std::set<std::string> before = m_world->TextureReferences();
	for (const Definition &d : definitions) {
		Result<std::vector<String>, String> loaded = m_game->ReloadDefinition(d.node, d.context);
		if (loaded.IsError()) {
			out.errors.push_back(loaded.Error());
			continue;
		}
		for (String &id : loaded.Value())
			out.blocks.push_back(std::move(id));
	}
	if (out.blocks.empty())
		return out;
	out.atlasRebuilt = m_world->AtlasMissing(out.blocks, before);
	if (Result<bool, String> r = m_world->ReloadContent(out.atlasRebuilt); r.IsError())
		out.errors.push_back(r.Error());
	out.milliseconds = double(sdl3::GetPerformanceCounter() - start) * 1000.0 / double(sdl3::GetPerformanceFrequency());
	return out;
}

} // namespace voxelia
