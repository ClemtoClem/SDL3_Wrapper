#pragma once
/**
 * Préférences du joueur (indépendantes des mondes) : générales, commandes,
 * graphismes, son, jeu, accessibilité. Enregistrées dans
 * data/voxelgame/settings.json ; les réglages propres à un monde (mode,
 * difficulté, règles) vivent dans sa sauvegarde (voxel::WorldMetadata).
 */
#include <array>
#include <map>
#include <vector>

#include <core/core.hpp>
#include <dataio/node.hpp>

namespace voxelgame {

/// Entrée physique : touche (scancode SDL) ou bouton de souris (MOUSE_BASE + n).
inline constexpr int MOUSE_BASE = 1000;
inline constexpr int WHEEL_UP = 2000, WHEEL_DOWN = 2001;

struct KeyBinding {
	String action;   ///< « forward », « jump »…
	String label;    ///< « Avancer »
	String category; ///< « Déplacement », « Actions », « Interface »…
	std::array<int, 2> keys{0, 0}; ///< principale, secondaire (0 : aucune)
	std::array<int, 2> defaults{0, 0};
};

/// Nom lisible d'une entrée (« Z », « Clic gauche », « Molette ↑ »).
[[nodiscard]] String InputName(int code);

struct Settings {
	// ── Général ─────────────────────────────────────────────────────────────
	String language = "fr";
	String playerName = "Joueur";
	String skin;                  ///< id de texture ; vide : celui des mods
	bool confirmQuit = true, confirmDelete = true, confirmModChanges = true;
	int notifications = 2;        ///< 0 aucune, 1 importantes, 2 toutes
	float notificationSeconds = 4.f;
	int autosaveMinutes = 5;      ///< 0 : jamais
	bool showTips = true;
	bool pauseOnFocusLoss = true;

	// ── Commandes ───────────────────────────────────────────────────────────
	std::vector<KeyBinding> bindings;
	float mouseSensitivity = 1.f;
	bool invertY = false;
	float fov = 72.f;
	bool viewBobbing = true;
	bool allowDualBinding = false; ///< une même touche pour deux actions
	bool toggleSprint = false, toggleSneak = false;
	bool thirdPersonSmooth = true;

	// ── Graphismes ──────────────────────────────────────────────────────────
	int displayMode = 0;          ///< 0 fenêtre, 1 plein écran
	int width = 1280, height = 760;
	bool vsync = true;
	int fpsLimit = 0;             ///< 0 : illimité
	int viewDistance = 6;         ///< chunks
	int chunkUpdates = 8;         ///< reconstructions par image
	int shadows = 0;              ///< 0 désactivées, 1 proches, 2 étendues
	int dynamicLights = 8;        ///< lumières ponctuelles des blocs lumineux (0 : aucune)
	float brightness = 0.5f;      ///< 0..1
	bool ambientOcclusion = true;
	bool clouds = true;
	bool vignette = true;
	bool screenEffects = true;    ///< eau, dégâts
	int particles = 2;            ///< 0 aucune, 1 réduites, 2 toutes
	float vegetationDensity = 1.f;
	int entityDistance = 64;      ///< blocs

	// ── Son ─────────────────────────────────────────────────────────────────
	float masterVolume = 0.9f, musicVolume = 0.5f, effectsVolume = 1.f, ambienceVolume = 0.7f, uiVolume = 0.7f,
		  voiceVolume = 0.8f;
	bool spatialAudio = true;
	bool muteWhenUnfocused = false;

	// ── Jeu ─────────────────────────────────────────────────────────────────
	bool showCoordinates = true, showFps = false, showBiome = true, showClock = true;
	int crosshair = 0;            ///< 0 croix, 1 point, 2 cercle
	float hudOpacity = 0.85f;
	float hudScale = 1.f;
	bool autoJump = false;
	bool placeAssist = true;      ///< contour de pose du bloc
	bool showHints = true;        ///< indications de commandes contextuelles
	bool chatTimestamps = false;
	float chatOpacity = 0.6f;

	// ── Accessibilité ───────────────────────────────────────────────────────
	float uiScale = 1.f;
	float textScale = 1.f;
	bool highContrast = false;
	int colorBlind = 0;           ///< 0 aucun, 1 protanopie, 2 deutéranopie, 3 tritanopie
	bool subtitles = false;
	bool reduceMotion = false;
	bool reduceFlashes = false;
	float hudBackground = 0.35f;  ///< opacité du fond des textes du HUD
	int crosshairColor = 0;       ///< index de palette
	float crosshairSize = 1.f;
	bool smoothTextures = false;  ///< filtrage lissé au lieu du pixel net

	// ── Organisation des mods (préférences, pas la sauvegarde) ─────────────
	std::vector<String> modOrder;              ///< ordre de chargement souhaité
	std::map<std::string, String> modNames;    ///< nom d'affichage personnalisé (l'id interne ne change pas)
	std::map<std::string, String> modCategories; ///< catégorie personnalisée
	dataio::NodePtr modSettings;               ///< {mod: {réglage: valeur}} : valeurs globales
	[[nodiscard]] String ModName(const String &id, const String &fallback) const;

	/// Liste complète des actions et de leurs touches par défaut.
	void ResetBindings();
	[[nodiscard]] const KeyBinding *Binding(const String &action) const;
	/// Actions qui partagent une même entrée (conflits).
	[[nodiscard]] std::vector<std::pair<String, String>> Conflicts() const;
	/// Vrai si `code` déclenche `action`.
	[[nodiscard]] bool Matches(const String &action, int code) const;

	[[nodiscard]] dataio::NodePtr ToJson() const;
	void FromJson(const dataio::NodePtr &node);
	Result<bool, String> Save(const String &path) const;
	/// Lit le fichier (absent : valeurs par défaut).
	static Settings Load(const String &path);
};

} // namespace voxelgame
