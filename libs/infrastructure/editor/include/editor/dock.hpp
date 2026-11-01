#pragma once
/**
 * editor::DockPanel — panneau à onglets façon éditeur de jeu : bandeau
 * d'onglets (fermables ou non), zone de boutons à droite des onglets, bouton
 * ≡ (menu du panneau), pages empilées dont une seule est visible.
 *
 * Le panneau ne décide pas de sa place : `Build` reçoit de quoi le
 * dimensionner. Reconstruire (changement de thème) repart de zéro.
 */
#include <functional>
#include <vector>

#include "theme.hpp"

namespace editor {

class DockPanel {
public:
	explicit DockPanel(ui::Ui &gui) : m_gui(gui) {}
	DockPanel(const DockPanel &) = delete;
	DockPanel &operator=(const DockPanel &) = delete;

	/// Construit le cadre sous `parent` ; `size` pose les dimensions.
	ui::Widget Build(ui::Widget parent, const std::function<void(ui::WidgetBuilder &)> &size);

	/// Ajoute une page et l'active ; rend son conteneur. `scroll` : contenu
	/// défilable (listes, inspecteur) ; sinon la page remplit la zone (vue
	/// 3D, éditeur de code).
	ui::Widget AddPage(const String &title, bool scroll, bool closable = false, float pad = 6.f);
	void RemovePage(int index);
	void SetActive(int index);
	void SetTitle(int index, const String &title);
	/// Page dont le titre est `title`, -1 sinon.
	[[nodiscard]] int Find(const String &title) const;

	[[nodiscard]] int Active() const noexcept { return m_active; }
	[[nodiscard]] int Count() const noexcept { return int(m_pages.size()); }
	[[nodiscard]] ui::Widget Page(int index) const;
	[[nodiscard]] const String &Title(int index) const;
	[[nodiscard]] ui::Widget Frame() const noexcept { return m_frame; }
	/// Zone du bandeau, à droite des onglets, pour les boutons du panneau.
	[[nodiscard]] ui::Widget HeaderExtra() const noexcept { return m_extra; }

	/// Une page devient active (clic d'onglet ou programme).
	std::function<void(int)> onActivate;
	/// Clic sur le × d'un onglet fermable (la fermeture revient à l'appelant).
	std::function<void(int)> onClose;
	/// Clic sur ≡, avec le coin bas-gauche du bouton.
	std::function<void(float, float)> onMenu;

	static constexpr float HEADER_HEIGHT = 30.f;

private:
	struct PageInfo {
		String title;
		ui::Widget content;
		bool closable = false;
	};

	void RebuildTabs();

	ui::Ui &m_gui;
	ui::Widget m_frame, m_header, m_tabs, m_extra, m_menuButton, m_body;
	std::vector<PageInfo> m_pages;
	int m_active = -1;
};

} // namespace editor
