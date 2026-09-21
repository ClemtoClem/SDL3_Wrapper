#pragma once
/**
 * GameView — affichage d'une `EmulatorSession` dans un rectangle de la
 * fenêtre : récupère la dernière image émulée, la téléverse dans une ou deux
 * textures et la dessine à l'échelle en gardant les proportions.
 *
 * Ne possède NI le cœur NI le fil d'émulation (cf. EmulatorSession). Il est
 * dessiné depuis un widget `ui::Canvas` de la zone centrale : l'ordre de
 * dessin de l'arbre d'interface garantit donc que le bandeau de performances
 * et les boîtes de dialogue passent AU-DESSUS du jeu, sans la bidouille de
 * l'ancienne version (panneau central rendu transparent quand un jeu tourne).
 */
#include "sdl3/sdl3.hpp"

#include "emulator_session.hpp"

namespace emulator_demo::app {

class GameView {
public:
	GameView(sdl3::Renderer &renderer, EmulatorSession &session);

	GameView(const GameView &) = delete;
	GameView &operator=(const GameView &) = delete;

	/// Récupère la dernière image produite (à appeler une fois par image).
	void Update();

	/// Dessine dans `viewport` (coordonnées fenêtre).
	void Render(sdl3::Renderer &renderer, const sdl3::FRect &viewport);

	/// Souris -> écran tactile NDS. Rend vrai si l'évènement a été consommé.
	bool HandleMouse(const sdl3::Event &event);

	[[nodiscard]] const FrameSnapshot &Snapshot() const { return m_snapshot; }

private:
	static constexpr int NDS_SCREEN_WIDTH = 256;
	static constexpr int NDS_SCREEN_HEIGHT = 192;

	void Upload();
	[[nodiscard]] static sdl3::FRect Fit(const sdl3::FRect &viewport, float width, float height);

	EmulatorSession &m_session;
	FrameSnapshot m_snapshot;
	bool m_uploadPending = false;

	// Deux textures pour la NDS : chaque écran se place et se dimensionne
	// indépendamment selon la disposition choisie (côte à côte / empilés).
	sdl3::Texture m_topTexture;
	sdl3::Texture m_bottomTexture;
	sdl3::FRect m_topRect{};
	sdl3::FRect m_touchRect{};

	Option<sdl3::Cursor> m_defaultCursor = NONE;
	Option<sdl3::Cursor> m_pointerCursor = NONE;
	bool m_mouseDown = false;
	bool m_hoveringTouch = false;
};

} // namespace emulator_demo::app
