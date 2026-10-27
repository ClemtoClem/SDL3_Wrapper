#pragma once
/**
 * Game — la partie, SANS fenêtre ni GPU : contenu (mods), monde,
 * sauvegarde, entités et systèmes ECS, scripts, console. L'application
 * (demos/voxelia) n'y ajoute que l'affichage, le son et les entrées ;
 * les tests pilotent une partie complète sans rien de tout cela.
 *
 * Systèmes (ecs::Schedule) :
 *   PRE_UPDATE    world.stream      chunks autour des joueurs
 *   FIXED_UPDATE  ai                comportements (intégrés et scriptés)
 *                 physics           physique voxel
 *                 portals           portails, champs de gravité
 *                 falling_blocks    sable & co
 *                 items             objets au sol (ramassage, fusion, durée)
 *                 health            chutes, morts, butin
 *   UPDATE        scripts.tick      fonctions « tick » des mods
 *                 sounds.idle       sons d'ambiance des créatures
 *   POST_UPDATE   events.dispatch   événements ECS → scripts
 * Un mod peut ajouter ses propres systèmes (`voxel.system`).
 *
 * Dimensions (DimensionDef) : un seul monde ACTIF à la fois, celui du
 * joueur ; changer de dimension enregistre puis décharge le monde courant
 * (chunks, entités), et ouvre l'autre — généré à l'arrivée, puis chunk par
 * chunk. Chaque dimension garde ses chunks et ses entités dans la même
 * sauvegarde, son point d'arrivée et ses données libres (scripts).
 *
 * Actions de jeu (interface, scripts, console) : casser/poser un bloc,
 * utiliser l'objet tenu, interagir avec une entité, blesser, faire
 * apparaître, fabriquer, téléporter… Les événements (`block_broken`,
 * `block_placed`, `entity_died`, `player_join`…) sont offerts aux scripts,
 * qui peuvent ANNULER une action en rendant `false`.
 */
#include <functional>
#include <map>
#include <memory>

#include <ecs/schedule.hpp>

#include "assets.hpp"
#include "entities.hpp"
#include "fluids.hpp"
#include "mods.hpp"
#include "physics.hpp"
#include "save.hpp"
#include "schematic.hpp"
#include "worldgen.hpp"

namespace voxel {

class ScriptHost;

struct GameOptions {
	std::vector<String> modDirectories{"mods"};
	std::vector<String> disabledMods;
	std::vector<String> modOrder;  ///< ordre de chargement souhaité (dépendances d'abord malgré tout)
	NodePtr modSettings;           ///< {mod: {réglage: valeur}} : lu par voxel.setting() côté script
	String savesDirectory = "saves";
	/// Dossier d'ÉCHANGE des structures entre mondes (import / export) ; chaque
	/// monde a aussi le sien, <sauvegarde>/structures.
	String structureDirectory = "structures";
	int viewRadius = 6;        ///< chunks
	int verticalRadius = 3;
	size_t chunkBudget = 6;    ///< chunks chargés par image au plus
	bool creative = false;     ///< blocs infinis, pas de dégâts
	float dayLength = 600.f;   ///< secondes par jour
};

enum class GameMode : uint8_t { SURVIVAL, CREATIVE, ADVENTURE, SPECTATOR };
enum class Difficulty : uint8_t { PEACEFUL, EASY, NORMAL, HARD };

[[nodiscard]] const char *GameModeId(GameMode mode) noexcept;   ///< "survival"…
[[nodiscard]] GameMode GameModeFromId(const String &id) noexcept;
[[nodiscard]] const char *DifficultyId(Difficulty d) noexcept;  ///< "normal"…
[[nodiscard]] Difficulty DifficultyFromId(const String &id) noexcept;

/// Niveau d'expérience atteint avec `xp` points ; `progress` : avancement
/// vers le niveau suivant (0..1).
[[nodiscard]] int XpLevel(int xp, float *progress = nullptr) noexcept;

/// Demande d'interface émise par le jeu ou un script (commerce avec un PNJ,
/// dialogue…) : l'application l'affiche comme elle l'entend.
struct UiRequest {
	String kind;   ///< "trade", "dialog"…
	NodePtr data;  ///< contenu (offres, texte…)
	ecs::Entity player{}, source{};
};

class Game {
public:
	using CommandFn = std::function<Result<String, String>(Game &, ecs::Entity sender, const std::vector<String> &args)>;
	struct Command {
		String name, usage, help;
		CommandFn fn;
	};

	Game();
	~Game();
	Game(const Game &) = delete;
	Game &operator=(const Game &) = delete;

	// ── Contenu ─────────────────────────────────────────────────────────────

	/// Mods (JSON puis scripts) ; à appeler avant OpenWorld.
	ModReport LoadMods(const GameOptions &options);
	[[nodiscard]] ContentRegistry &Content() noexcept { return m_content; }
	[[nodiscard]] const ModReport &Mods() const noexcept { return m_mods; }
	[[nodiscard]] AssetManager &Assets() noexcept { return *m_assets; }
	[[nodiscard]] ScriptHost &Scripts() noexcept { return *m_scripts; }
	[[nodiscard]] const GameOptions &Options() const noexcept { return m_options; }
	void SetCreative(bool creative) noexcept { m_options.creative = creative; }
	/// Distance de chargement (chunks) : réglages graphiques.
	void SetViewRadius(int radius, int vertical) noexcept {
		m_options.viewRadius = radius;
		m_options.verticalRadius = vertical;
	}

	// ── Monde ───────────────────────────────────────────────────────────────

	/// Ouvre (ou crée) le monde `name` du dossier des sauvegardes.
	Result<bool, String> OpenWorld(const String &name, Option<uint32_t> seed = NONE, const String &generator = String());
	/// Enregistre tout (chunks modifiés, entités, joueurs, métadonnées).
	Result<bool, String> Save();
	void CloseWorld();
	[[nodiscard]] bool HasWorld() const noexcept { return m_world != nullptr; }
	[[nodiscard]] World &GetWorld() noexcept { return *m_world; }
	[[nodiscard]] WorldSave &GetSave() noexcept { return *m_save; }
	[[nodiscard]] WorldMetadata &Metadata() noexcept { return m_metadata; }
	[[nodiscard]] ecs::ArchetypeRegistry &Registry() noexcept { return m_registry; }
	[[nodiscard]] ecs::Schedule &GetSchedule() noexcept { return m_schedule; }
	[[nodiscard]] PhysicsSettings &Physics() noexcept { return m_physics; }
	/// Écoulement des liquides du monde courant (règle « liquidFlow »).
	[[nodiscard]] FluidSimulation &Fluids() noexcept { return *m_fluids; }

	// ── Dimensions ──────────────────────────────────────────────────────────

	/// Dimension active (celle du joueur local) ; vide si aucun mod n'en déclare.
	[[nodiscard]] const String &CurrentDimension() const noexcept { return m_dimension; }
	[[nodiscard]] const DimensionDef *CurrentDimensionDef() const;
	/// Envoie le joueur dans `dimension`, en `at` ou à son point d'arrivée.
	/// Demandé pendant un pas de jeu (script, portail, mort), le voyage a lieu
	/// à la fin du pas. Erreur si la dimension est inconnue.
	Result<bool, String> TravelTo(ecs::Entity player, const String &dimension, Option<math::FVector3> at = NONE);
	/// Avance à chaque remplacement du monde (changement de dimension) :
	/// l'affichage le reconstruit de zéro.
	[[nodiscard]] uint64_t WorldSerial() const noexcept { return m_worldSerial; }
	/// Données persistantes d'une dimension (vide : la courante) — « state »
	/// est réservé aux scripts (énergie du noyau, hostilité de la forêt…).
	[[nodiscard]] NodePtr DimensionData(const String &dimension = String());
	/// Secondes de jeu passées dans la dimension courante depuis l'arrivée.
	[[nodiscard]] double TimeInDimension() const noexcept { return m_metadata.playTime - m_dimensionEnter; }
	/// Position des yeux (gravité inversée : près des pieds, la tête en bas).
	[[nodiscard]] math::FVector3 EyePosition(ecs::Entity e);

	/// Un pas de jeu (systèmes, scripts).
	void Tick(float dt);

	// ── Règles du monde (metadata.settings) ─────────────────────────────────

	[[nodiscard]] GameMode Mode() const noexcept { return m_mode; }
	void SetMode(GameMode mode);
	[[nodiscard]] Difficulty GetDifficulty() const noexcept { return m_difficulty; }
	void SetDifficulty(Difficulty difficulty);
	/// Règle booléenne (« mobSpawning », « keepInventory »…) ou numérique.
	[[nodiscard]] bool Rule(const char *name) const;
	[[nodiscard]] double RuleNum(const char *name, double fallback) const;
	void SetRule(const char *name, bool value);
	/// Règles connues et leur valeur par défaut.
	[[nodiscard]] static const std::vector<std::pair<const char *, bool>> &DefaultRules();

	// ── Survie ──────────────────────────────────────────────────────────────

	/// Mange l'objet tenu (« food ») ; faux s'il n'est pas comestible ou que
	/// le joueur n'a pas faim.
	bool Eat(ecs::Entity actor);
	void AddEffect(ecs::Entity target, const String &effect, float seconds, int level = 1);
	void AddXp(ecs::Entity player, int amount);
	void AddExhaustion(ecs::Entity player, float amount);
	/// Points d'armure portés (0..20).
	[[nodiscard]] int ArmorPoints(ecs::Entity e);
	/// Équipe l'objet tenu dans son emplacement d'armure.
	bool EquipHeld(ecs::Entity player);

	// ── Interface demandée par le jeu ───────────────────────────────────────

	void RequestUi(UiRequest request) { m_uiRequests.push_back(std::move(request)); }
	[[nodiscard]] std::vector<UiRequest> TakeUiRequests() { return std::exchange(m_uiRequests, {}); }
	// ── Fabrication ─────────────────────────────────────────────────────────

	/// Atelier d'une recette : le sien, sinon celui de la configuration
	/// (« crafting.cookingStation » pour la cuisson, « crafting.tableStation »
	/// pour une grille de plus de 2×2) ; vide : à la main.
	[[nodiscard]] String RecipeStation(const RecipeDef &recipe) const;
	/// Ateliers (blocs « station ») à portée du joueur.
	[[nodiscard]] std::vector<String> NearbyStations(ecs::Entity player, int radius = 4);
	/// Pourquoi une recette n'est pas réalisable (vide : elle l'est).
	[[nodiscard]] String CraftBlocker(ecs::Entity player, const RecipeDef &recipe, const std::vector<String> &stations);
	/// Fabrique `times` fois (ateliers, niveau, combustible de cuisson) ; rend le nombre réalisé.
	Result<int, String> Craft(ecs::Entity player, const String &recipe, int times = 1);

	/// Échange avec un marchand : retire `give` de l'inventaire, ajoute `get`.
	Result<bool, String> Trade(ecs::Entity player, const String &giveItem, int giveCount, const String &getItem, int getCount);

	// ── Joueurs et entités ──────────────────────────────────────────────────

	ecs::Entity JoinPlayer(const String &name);
	[[nodiscard]] ecs::Entity LocalPlayer() const noexcept { return m_localPlayer; }
	[[nodiscard]] math::FVector3 SpawnPoint();
	ecs::Entity SpawnEntity(const String &id, const math::FVector3 &feet);
	ecs::Entity DropItem(const ItemStack &stack, const math::FVector3 &position, const math::FVector3 &velocity = {0.f, 3.f, 0.f});
	/// `cause` : « attack » (source donnée), « suffocation », « fall »… (cf. Health::lastCause).
	void Damage(ecs::Entity target, float amount, ecs::Entity source = ecs::Entity{}, const String &cause = String());
	void Kill(ecs::Entity target);
	bool Interact(ecs::Entity actor, ecs::Entity target);

	// ── Blocs ───────────────────────────────────────────────────────────────

	/// Temps pour casser un bloc avec l'objet tenu (< 0 : incassable).
	[[nodiscard]] float BreakTime(ecs::Entity actor, const BlockPos &pos);
	bool BreakBlock(ecs::Entity actor, const BlockPos &pos);
	/// Pose le bloc (orienté selon le regard de l'acteur) ; consomme l'objet
	/// (hors mode créatif). Refusé dans une entité ou sur un bloc non remplaçable.
	bool PlaceBlock(ecs::Entity actor, const BlockPos &pos, const String &block);
	/// Clic droit avec l'objet tenu : entité visée, bloc visé (script,
	/// pose), ou utilisation de l'objet.
	bool UseHeldItem(ecs::Entity actor);
	/// Visée de l'acteur : bloc puis entité, à portée.
	[[nodiscard]] Option<BlockHit> TargetBlock(ecs::Entity actor);
	[[nodiscard]] Option<EntityHit> TargetEntity(ecs::Entity actor);
	[[nodiscard]] ItemStack *HeldItem(ecs::Entity actor);

	// ── Blocs interactifs ───────────────────────────────────────────────────
	//
	// Un bloc peut occuper plusieurs cellules (« parts » / « partOf » : porte,
	// lit), basculer vers un autre au clic droit (« interact.toggle » : porte
	// ouverte ↔ fermée), servir de lit (« interact.action »: "sleep") ou de
	// rangement (« container.slots » : coffre, armoire). Clic droit sur un tel
	// bloc : il est utilisé plutôt que d'y poser l'objet tenu (sauf accroupi).

	/// Bloc principal d'un bloc en plusieurs cellules (`pos` lui-même sinon).
	[[nodiscard]] BlockPos MainPart(const BlockPos &pos);
	/// Clic droit sur le bloc : bascule, lit ou rangement ; faux s'il n'a pas d'action.
	bool UseBlock(ecs::Entity actor, const BlockPos &pos);
	/// Bascule le bloc et ses parties vers « interact.toggle » (orientation gardée).
	bool ToggleBlock(const BlockPos &pos, ecs::Entity actor = ecs::Entity{});
	/// Met le bloc (à états) dans l'état `state`, orientation gardée ; faux si
	/// le bloc n'a pas cet état.
	bool SetBlockState(const BlockPos &pos, const String &state, ChangeCause cause = ChangeCause::SCRIPT);
	/// Dort dans le lit : il devient le point de réapparition ; la nuit, passe
	/// au matin (sauf monstre proche ou dimension sans cycle du jour).
	bool Sleep(ecs::Entity player, const BlockPos &bed);
	/// Contenu du rangement en `pos` (dimension courante), lu dans la
	/// sauvegarde à la première demande ; nul si le bloc n'est pas un rangement.
	[[nodiscard]] Inventory *BlockInventory(const BlockPos &pos);

	// ── Structures (copier, coller, fichiers) ───────────────────────────────
	//
	// Fichiers : `exchange` faux → dossier du monde (<sauvegarde>/structures),
	// vrai → dossier d'échange entre mondes (GameOptions::structureDirectory).
	// Noms simples seulement (pas de chemin) ; sans extension : « .vxs.json ».

	/// Dossier des structures du monde ouvert, ou d'échange.
	[[nodiscard]] String StructuresDirectory(bool exchange = false) const;
	/// Copie [a, b] avec le contenu des rangements.
	[[nodiscard]] Schematic CaptureRegion(const BlockPos &a, const BlockPos &b, Option<BlockPos> anchor = NONE);
	/// Colle (rangements remplis) ; rend les cellules modifiées.
	int64_t PasteSchematic(const Schematic &schematic, const BlockPos &at, const Schematic::PasteOptions &options = {});
	/// Remplit [a, b] : « all », « hollow » (enveloppe, intérieur vidé),
	/// « walls » (côtés seulement), « outline » (arêtes), « keep » (l'air seulement).
	Result<int64_t, String> FillRegion(const BlockPos &a, const BlockPos &b, const String &block, const String &mode = String("all"));
	/// Remplace `from` (vide : tout bloc non-air) par `to` dans [a, b].
	Result<int64_t, String> ReplaceRegion(const BlockPos &a, const BlockPos &b, const String &from, const String &to);
	/// Presse-papiers nommés (en mémoire, toute la partie) : outils, annulations.
	[[nodiscard]] Schematic *Clipboard(const String &name);
	void SetClipboard(const String &name, Schematic schematic);
	bool ForgetClipboard(const String &name);
	[[nodiscard]] std::vector<String> ClipboardNames() const;
	/// Fichiers de structures (noms avec extension, triés).
	[[nodiscard]] std::vector<String> StructureFiles(bool exchange = false) const;
	/// Lit un fichier (noms inconnus résolus par alias ; `unknown` les reçoit).
	Result<Schematic, String> LoadStructureFile(const String &name, bool exchange = false, std::vector<String> *unknown = nullptr);
	/// Écrit ; rend le chemin. Le format suit l'extension.
	Result<String, String> SaveStructureFile(const Schematic &schematic, const String &name, bool exchange = false);
	Result<bool, String> DeleteStructureFile(const String &name, bool exchange = false);

	// ── Interface des mods ──────────────────────────────────────────────────

	/// Boîtes surlignées dans le monde (sélection d'un outil…) : l'application
	/// les dessine en fil de fer.
	struct Highlight {
		String id;
		BlockPos min, max; ///< bornes incluses
		std::array<uint8_t, 4> color{255, 255, 255, 255};
	};
	void SetHighlight(const String &id, const BlockPos &a, const BlockPos &b, std::array<uint8_t, 4> color);
	void ClearHighlight(const String &id);
	[[nodiscard]] const std::vector<Highlight> &Highlights() const noexcept { return m_highlights; }
	/// Formulaire d'un mod (UiRequest « form ») validé : événement de script
	/// « form_submit » (joueur, id du formulaire, valeurs).
	void SubmitForm(ecs::Entity player, const String &form, const NodePtr &values);

	// ── Console ─────────────────────────────────────────────────────────────

	void RegisterCommand(const String &name, const String &usage, const String &help, CommandFn fn);
	/// Exécute une ligne (« /give core:stone 64 ») ; rend le message à afficher.
	Result<String, String> Execute(const String &line, ecs::Entity sender = ecs::Entity{});
	[[nodiscard]] std::vector<const Command *> Commands() const;
	/// Complétion (noms de commandes, puis ids de contenu).
	[[nodiscard]] std::vector<String> Complete(const String &partial) const;
	/// Id complet d'un nom court de la console (« planks » → « basic:planks »).
	[[nodiscard]] String ResolveShortId(const String &id) const;

	// ── Temps et messages ───────────────────────────────────────────────────

	[[nodiscard]] double GameTime() const noexcept { return m_metadata.gameTime; }
	/// 0 : minuit, 0,25 : lever, 0,5 : midi, 0,75 : coucher.
	[[nodiscard]] float DayFraction() const noexcept;
	void SetGameTime(double t) noexcept { m_metadata.gameTime = t; }
	void Message(const String &text, const String &channel = String("game"));
	void PlaySound(const String &sound, const math::FVector3 &position, float volume = 1.f, bool positional = true);
	/// Ambiance ou musique sur un canal (non spatialisée) : remplace ce qui
	/// s'y joue, avec fondus de sortie et d'entrée ; le même son continue.
	struct AmbientOptions {
		String channel;
		float fadeIn = 0.f, fadeOut = 0.f;
		bool loop = false;
	};
	void PlayAmbient(const String &sound, float volume, const AmbientOptions &options);
	/// Éteint un canal d'ambiance en `fadeOut` secondes.
	void StopAmbient(const String &channel, float fadeOut);
	/// Canal de la musique des dimensions (« music » d'une DimensionDef).
	static constexpr const char *DIMENSION_CHANNEL = "dimension";
	/// Son d'un événement de bloc (« place », « break », « step »).
	void PlayBlockSound(BlockId block, const char *event, const BlockPos &pos);

	/// Journal (erreurs de scripts, avertissements de chargement…).
	[[nodiscard]] const std::vector<String> &Log() const noexcept { return m_log; }
	void LogLine(const String &line);

private:
	/// Plus haut bloc où l'on peut se tenir dans la colonne (x, z), entre
	/// `top` et `bottom` : solide, ni fluide ni feuillage, surmonté de deux
	/// blocs libres. Les chunks traversés sont générés au besoin.
	[[nodiscard]] Option<int> StandableY(int x, int z, int top, int bottom);
public:
	/// Même chose sans rien générer (chunks chargés seulement) ; scripts.
	[[nodiscard]] Option<int> LoadedGround(int x, int z, int top, int bottom) const;
private:
	/// Point d'apparition vérifié sur les blocs réellement générés.
	[[nodiscard]] math::FVector3 FindSpawn();
	/// Point d'arrivée de la dimension courante (celui du monde ordinaire :
	/// SpawnPoint), calculé une fois puis enregistré.
	[[nodiscard]] math::FVector3 DimensionSpawn();
	/// Crée le monde de `dimension` (générateur, graine dérivée, stockage).
	void CreateWorld(const String &dimension);
	void DoTravel(ecs::Entity player, const String &dimension, Option<math::FVector3> at, bool remember);
	/// Pose le joueur dans le terrain chargé (première place libre au-dessus).
	void SettlePlayer(ecs::Entity e);
	/// Portail de retour près du point d'arrivée (première visite).
	void PlaceArrivalPortal(const DimensionDef &def, const math::FVector3 &spawn);
	void StepPortals(float dt);
	[[nodiscard]] bool IsDefaultDimension(const String &dimension) const;
	void InstallSystems();
	void InstallCommands();
	void StepFallingBlocks();
	void StepItems(float dt);
	void StepHealth(float dt);
	void StepSurvival(float dt);
	void StepRandomTicks(float dt);
	/// Blocs à capteur (« sensor ») autour des joueurs : état « proche » ou « loin ».
	void StepSensors(float dt);
	/// Entités lumineuses (bloc qui tombe, objet au sol, créature) : sources
	/// dynamiques du monde (World::SetDynamicLights), cinq fois par seconde.
	void StepLights(float dt);
	void ApplySettings();
	void DispatchEvents();
	/// Rangements ouverts depuis le chargement → sauvegarde.
	void SaveContainers();
	/// Retire un rangement (bloc cassé, remplacé) ; rend son contenu.
	std::vector<ItemStack> TakeContainer(const BlockPos &pos);

	GameOptions m_options;
	ContentRegistry m_content;
	ModReport m_mods;
	std::unique_ptr<AssetManager> m_assets;
	std::unique_ptr<ScriptHost> m_scripts;
	std::unique_ptr<World> m_world;
	std::unique_ptr<FluidSimulation> m_fluids; ///< détruite avant le monde (connexions)
	std::unique_ptr<WorldSave> m_save;
	WorldMetadata m_metadata;
	ecs::ArchetypeRegistry m_registry;
	ecs::Schedule m_schedule;
	PhysicsSettings m_physics;
	ecs::Entity m_localPlayer{};
	std::map<std::string, Command> m_commands;
	std::vector<String> m_log;
	uint64_t m_contactCursor = 0, m_diedCursor = 0;
	uint64_t m_soundSerial = 0;
	GameMode m_mode = GameMode::SURVIVAL;
	Difficulty m_difficulty = Difficulty::NORMAL;
	std::vector<UiRequest> m_uiRequests;
	std::map<std::string, std::pair<BlockPos, Inventory>> m_containers; ///< « x,y,z » → rangement chargé
	std::map<std::string, Schematic> m_clipboards;
	std::vector<Highlight> m_highlights;
	float m_randomTickTimer = 0.f;
	float m_sensorTimer = 0.f;
	float m_lightTimer = 0.f;
	/// Blocs à capteur passés à l'état « proche » : surveillés même hors de
	/// portée des joueurs (un joueur téléporté loin les éteint aussi).
	std::vector<BlockPos> m_sensorWatch;
	std::map<uint32_t, float> m_stepDistance; ///< distance marchée depuis le dernier pas sonore
	// Dimensions
	String m_dimension;
	uint64_t m_worldSerial = 0;
	double m_dimensionEnter = 0.0;
	float m_baseGravity = 28.f;
	bool m_inTick = false;
	struct PendingTravel {
		ecs::Entity player;
		String dimension;
		Option<math::FVector3> at;
	};
	std::vector<PendingTravel> m_travels;
};

} // namespace voxel
