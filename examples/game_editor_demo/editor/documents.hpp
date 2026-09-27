#pragma once
/**
 * game_editor::DocumentArea — la zone centrale à onglets.
 *
 * Le premier onglet est la vue 3D (construite par l'éditeur, jamais
 * fermable) ; les suivants sont des DOCUMENTS de code, ouverts depuis le
 * navigateur de ressources, l'inspecteur ou les menus :
 *
 *  - un script de la bibliothèque (`torchlight.script`) ;
 *  - le script de jeu d'une scène (`Donjon.main.script`) ;
 *  - un composant — ou les propriétés libres — d'un nœud, en JSON (la
 *    maquette 6 : les réglages d'une torche, édités comme du texte) ;
 *  - un fichier texte du disque (shader, JSON, glTF), en lecture seule :
 *    l'éditeur ne réécrit jamais une ressource du dépôt par surprise ;
 *  - la console de script de l'éditeur (Ctrl+Entrée pour exécuter).
 *
 * Chaque document garde le texte ENREGISTRÉ pour savoir s'il est modifié
 * (« • » dans l'onglet) ; Ctrl+S enregistre le document actif en le
 * vérifiant : la première erreur de compilation s'affiche avec sa ligne, et
 * le texte est gardé quand même (on ne perd jamais un travail en cours pour
 * une faute de frappe).
 */
#include <functional>
#include <vector>

#include "core/core.hpp"
#include "data/json.hpp"
#include "ui/ui.hpp"

#include "kit.hpp"
#include "../document/project.hpp"
#include "../engine/runtime.hpp"

namespace game_editor {

enum class DocumentKind : uint8_t { LIBRARY_SCRIPT, SCENE_SCRIPT, COMPONENT_JSON, FILE, TOOL_CONSOLE };

struct CodeDocument {
	DocumentKind kind = DocumentKind::LIBRARY_SCRIPT;
	String target;       ///< nom du script, de la scène, ou chemin du fichier
	scene::NodeId node;  ///< COMPONENT_JSON : le nœud
	String component;    ///< COMPONENT_JSON : le type (vide = propriétés libres)
	String title;
	ecs::Entity page{}, area{}, status{}, cursor{};
	String saved;        ///< texte au dernier enregistrement
	bool readOnly = false;
};

class DocumentArea {
public:
	explicit DocumentArea(UiContext &ctx) : m_ctx(ctx), m_dock(ctx) {}
	DocumentArea(const DocumentArea &) = delete;
	DocumentArea &operator=(const DocumentArea &) = delete;

	std::function<void(const String &)> onStatus;
	/// « Enregistrer sous… » d'un script : le nom du script de bibliothèque,
	/// ou `@Scène` pour un script de jeu (le texte est d'abord enregistré dans
	/// le projet, cf. SaveActive).
	std::function<void(const String &key)> onSaveScriptAs;

	[[nodiscard]] kit::DockPanel &Dock() noexcept { return m_dock; }

	void Build(ecs::Entity parent, const std::function<void(ui::WidgetBuilder &)> &size);

	void Teardown() { m_documents.clear(); }

	/// Page 0 : la vue 3D (non fermable) — l'éditeur y met son contenu.
	ecs::Entity AddViewportPage(const String &title) { return m_dock.AddPage(title, false, false, 0.f); }

	// ── Ouverture ────────────────────────────────────────────────────────────

	void OpenLibraryScript(const String &name);

	void OpenSceneScript(const String &sceneName);

	/// Un composant (ou, `type` vide, les propriétés libres) d'un nœud en JSON.
	void OpenComponentJson(scene::NodeId id, const String &type);

	/// Fichier texte du disque, en LECTURE SEULE (cf. en-tête).
	void OpenFile(const String &path);

	void OpenToolConsole();

	// ── Actions sur le document actif ────────────────────────────────────────

	/// Le document de la page active, ou nul (vue 3D).
	[[nodiscard]] CodeDocument *Active();

	[[nodiscard]] bool IsViewportActive() const noexcept { return m_dock.Active() == 0; }
	[[nodiscard]] size_t DocumentCount() const noexcept { return m_documents.size(); }
	[[nodiscard]] const std::vector<CodeDocument> &Documents() const noexcept { return m_documents; }

	/// Enregistre le document actif. Faux s'il n'y en a pas (la vue 3D est
	/// active : Ctrl+S enregistre alors le projet, cf. EditorUi).
	bool SaveActive();

	/// Exécute la console de script (ou, pour un script, vérifie).
	void RunActive();

	void CheckActive();

	/// Ferme la page `page` (ses modifications non enregistrées sont
	/// perdues : c'est le × qui le dit, et le journal le rappelle).
	void Close(int page);

	/// Titres (« • » si modifié) et position du curseur — une fois par image.
	void Tick();

	static constexpr const char *TOOL_CONSOLE_SNIPPET =
		"# Console de script de l'éditeur — Ctrl+Entrée pour exécuter.\n"
		"# Toute l'API est là : editor.*, scene.*, object.*, node.*, light.*…\n"
		"for (i in range(0, 4)) {\n"
		"    scene.spawn({\n"
		"        name: \"Essai \" .. i,\n"
		"        shape: \"sphere\",\n"
		"        size: [1, 1, 1],\n"
		"        pos: [i * 2 - 3, 4, 0],\n"
		"        color: [240, 160 - i * 30, 90],\n"
		"        body: \"dynamic\"\n"
		"    })\n"
		"}\n"
		"editor.log(\"scène : \" .. scene.count() .. \" objets\")\n";

private:
	static constexpr size_t MAX_FILE_BYTES = 256 * 1024;

	void Status(const String &text);

	/// Ouvre (ou ramène au premier plan s'il l'est déjà) un document.
	void Open(CodeDocument doc, const String &text);

	[[nodiscard]] static String DescriptionOf(const CodeDocument &doc);

	[[nodiscard]] String TextOf(const CodeDocument &doc) const;

	void SetDocStatus(CodeDocument &doc, const String &text, bool error);

	/// Vérifie sans enregistrer : première erreur avec sa ligne.
	void Check(CodeDocument &doc, const String &text);

	void Save(CodeDocument &doc);

	UiContext &m_ctx;
	kit::DockPanel m_dock;
	std::vector<CodeDocument> m_documents;
	int m_cursorLine = 0, m_cursorColumn = 0;
};

} // namespace game_editor
