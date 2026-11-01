#pragma once
/**
 * editor::CommandPalette — palette de commandes (IngeTrazo, VS Code) : toute
 * action de l'éditeur s'y trouve par quelques lettres, recherche floue
 * (sous-séquence, bonus aux débuts de mots et aux suites de lettres),
 * commandes récentes d'abord à score égal.
 *
 * Modèle sans fenêtre (`CommandPalette`) et vue (`CommandPaletteView` : une
 * modale, son champ et sa liste ; ↑/↓ choisissent, Entrée exécute, Échap
 * ferme).
 */
#include <functional>
#include <vector>

#include "theme.hpp"

namespace editor {

struct PaletteEntry {
	String id;		 ///< identifiant stable (« studio.pause »)
	String title;	 ///< libellé cherché et affiché
	String help{};	 ///< ligne d'aide (aussi cherchée)
	String shortcut{};
	std::function<void()> run{};
};

/// Score de `query` dans `text` (sous-séquence, casse ignorée), NONE si une
/// lettre manque ; plus haut = meilleur.
[[nodiscard]] Option<int> FuzzyScore(const String &query, const String &text);

class CommandPalette {
public:
	void Add(PaletteEntry entry);
	void Clear() noexcept;
	[[nodiscard]] size_t Size() const noexcept { return m_entries.size(); }
	[[nodiscard]] const PaletteEntry &Entry(size_t index) const { return m_entries[index]; }
	/// Indices des entrées qui répondent à `query`, du meilleur au moins bon.
	[[nodiscard]] std::vector<size_t> Search(const String &query, size_t limit = 60) const;
	/// Exécute l'entrée (et la note comme récente) ; false sans action.
	bool Run(size_t index);
	/// Ids exécutés, du plus récent au plus ancien.
	[[nodiscard]] const std::vector<String> &Recent() const noexcept { return m_recent; }

private:
	std::vector<PaletteEntry> m_entries;
	std::vector<String> m_recent;
};

class CommandPaletteView {
public:
	CommandPaletteView(ui::Ui &gui, CommandPalette &model) : m_gui(gui), m_model(model) {}
	CommandPaletteView(const CommandPaletteView &) = delete;
	CommandPaletteView &operator=(const CommandPaletteView &) = delete;

	/// Monte la modale (fermée) sous `parent` (la racine de l'interface).
	void Build(ui::Widget parent);
	void Open();
	void Close();
	[[nodiscard]] bool IsOpen() const;
	void SetQuery(const String &query);
	/// Exécute la ligne courante et ferme.
	bool Accept();
	void Move(int delta);
	[[nodiscard]] const std::vector<size_t> &Shown() const noexcept { return m_shown; }
	[[nodiscard]] int Current() const noexcept { return m_current; }

private:
	void Refresh();

	ui::Ui &m_gui;
	CommandPalette &m_model;
	ui::Widget m_modal, m_input, m_list;
	std::vector<ui::Widget> m_rows;
	std::vector<size_t> m_shown;
	String m_query;
	int m_current = 0;
};

} // namespace editor
