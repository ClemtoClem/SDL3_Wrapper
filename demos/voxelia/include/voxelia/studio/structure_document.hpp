#pragma once
/**
 * Document de l'éditeur de structures — une structure (voxel::Schematic) en
 * cours d'édition : capturée dans le monde, lue d'un fichier (.vxs.json,
 * .json de mod, .we, .mts), prise d'un presse-papiers ou tracée d'après une
 * formule (voxel::Plot).
 *
 *  - Chaque opération (rotation, miroir, enveloppe, remplacement, cellule,
 *    ancre, tracé, remplacement complet) est UNE étape d'annulation : un
 *    instantané de la structure, 64 au plus — et au plus MAX_UNDO_CELLS
 *    cellules gardées en tout (les plus anciennes partent d'abord).
 *  - Les formules se vérifient sans tracer (CheckFormula : erreur avec sa
 *    position) ; le tracé respecte les limites de PlotSpec (taille, temps).
 *  - Lecture : les noms inconnus passent par les alias (« structures.aliases »
 *    de la configuration), puis « mod:nom » → un mod chargé ; ceux qui
 *    restent inconnus sont rendus (cellules vidées).
 *
 * Modèle sans fenêtre : le panneau (StructureEditor) et la console s'en servent.
 */
#include <deque>
#include <utility>
#include <vector>

#include <voxel/schematic.hpp>

namespace voxelia::studio {

class StructureDocument {
public:
	static constexpr size_t MAX_UNDO = 64;
	static constexpr int64_t MAX_UNDO_CELLS = 64ll * 64 * 64 * 16;

	StructureDocument() = default;
	StructureDocument(voxel::Schematic schematic, String name);

	[[nodiscard]] const voxel::Schematic &Schematic() const noexcept { return m_schematic; }
	[[nodiscard]] const String &Name() const noexcept { return m_name; }
	void SetName(String name) { m_name = std::move(name); }
	[[nodiscard]] bool Dirty() const noexcept { return m_dirty; }
	void MarkSaved() noexcept { m_dirty = false; }
	/// Avance à chaque changement (aperçus, panneaux).
	[[nodiscard]] uint64_t Version() const noexcept { return m_version; }

	// ── Annulation ──────────────────────────────────────────────────────────
	bool Undo();
	bool Redo();
	[[nodiscard]] size_t UndoDepth() const noexcept { return m_undo.size(); }
	[[nodiscard]] size_t RedoDepth() const noexcept { return m_redo.size(); }

	// ── Opérations ──────────────────────────────────────────────────────────
	/// Remplace toute la structure (capture, tracé, collage d'un presse-papiers).
	void Replace(voxel::Schematic schematic);
	void Rotate(int quarters);
	Result<bool, String> Mirror(int axis);
	/// Enveloppe ; `flatAxis` (0, 1, 2) : forme plate, -1 : volume.
	Result<bool, String> Hollow(int flatAxis = -1);
	/// Remplace `from` (vide : tout bloc non-air) par `to` (vide : cellule vidée).
	Result<int64_t, String> ReplaceBlock(const String &from, const String &to);
	/// Cellule (dans la boîte) ; `block` vide : cellule vidée.
	Result<bool, String> SetCell(const voxel::BlockPos &p, const String &block, uint8_t param = 0);
	Result<bool, String> SetAnchor(const voxel::BlockPos &p);
	/// Blocs et nombre de cellules, du plus fréquent au plus rare.
	[[nodiscard]] std::vector<std::pair<String, int64_t>> Counts() const;

	// ── Formules ────────────────────────────────────────────────────────────
	/// Variables d'un genre de tracé : x y z (implicite), t (courbe), u v
	/// (surface), u v w (solide).
	[[nodiscard]] static std::vector<String> FormulaVariables(voxel::PlotSpec::Kind kind);
	/// Compile sans tracer (toutes les expressions du genre) ; erreur
	/// préfixée de l'expression fautive (« y : position 4 : … »).
	[[nodiscard]] static Result<bool, String> CheckFormula(const voxel::PlotSpec &spec);
	/// Trace et remplace la structure ; rend le nombre de cellules pleines.
	Result<int64_t, String> Plot(const voxel::PlotSpec &spec);

	// ── Fichiers ────────────────────────────────────────────────────────────
	/// Lit `path` (format de l'extension), noms résolus ; `unknown` reçoit
	/// ceux qui restent inconnus.
	[[nodiscard]] static Result<StructureDocument, String> Load(const String &path, const voxel::ContentRegistry &content,
															   std::vector<String> *unknown = nullptr);
	/// Écrit dans le format de l'extension (« .json » : définition de mod
	/// « structure » d'id Name()).
	Result<String, String> Save(const String &path);

private:
	void Commit(); ///< instantané avant un changement
	void Changed();

	voxel::Schematic m_schematic;
	String m_name;
	std::deque<voxel::Schematic> m_undo, m_redo;
	bool m_dirty = false;
	uint64_t m_version = 0;
};

} // namespace voxelia::studio
