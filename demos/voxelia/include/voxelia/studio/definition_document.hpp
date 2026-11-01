#pragma once
/**
 * Document d'UNE définition de mod (bloc, modèle…) telle qu'écrite dans son
 * fichier, séparée du contenu vivant — la base commune des éditeurs du studio.
 *
 *  - lecture / écriture par chemins pointés (`SetField("light.range", …)`),
 *    ou du document entier (`SetJson`) : formulaire et onglet JSON partagent
 *    le même document ;
 *  - annulation par INSTANTANÉS (comme le sandbox) : BeginEdit / EndEdit
 *    encadrent un geste (un glissé = une étape), 64 étapes au plus, la plus
 *    ancienne oubliée au-delà ; un geste sans effet n'ajoute rien ;
 *  - enregistrement dans le fichier du mod : la définition de même id y est
 *    remplacée (fichier objet ou tableau), ajoutée sinon — en texte : le
 *    reste du fichier (commentaires, mise en forme, autres définitions) est
 *    gardé octet pour octet.
 *
 * Modèle sans fenêtre.
 */
#include <functional>
#include <vector>

#include <dataio/node.hpp>
#include <voxel/content.hpp>

namespace voxelia::studio {

class DefinitionDocument {
public:
	static constexpr size_t MAX_UNDO = 64;

	[[nodiscard]] const String &Id() const noexcept { return m_id; }
	[[nodiscard]] const String &Mod() const noexcept { return m_mod; }
	[[nodiscard]] const String &SourceFile() const noexcept { return m_file; }
	[[nodiscard]] const dataio::NodePtr &Json() const noexcept { return m_json; }
	[[nodiscard]] voxel::ContentRegistry::LoadContext Context() const;
	/// Modifié depuis le chargement / le dernier enregistrement.
	[[nodiscard]] bool Dirty() const noexcept { return m_dirty; }

	// ── Lecture / écriture ──────────────────────────────────────────────────
	[[nodiscard]] String Text() const; ///< JSON indenté
	/// Remplace tout le document (onglet JSON) ; refusé sans perte si le
	/// texte est invalide ou n'est pas un objet. Une étape d'annulation.
	Result<bool, String> SetJson(const String &text);
	[[nodiscard]] dataio::NodePtr Field(const char *path) const;
	[[nodiscard]] String FieldText(const char *path, const String &fallback = String()) const;
	[[nodiscard]] double FieldNumber(const char *path, double fallback = 0.0) const;
	[[nodiscard]] bool FieldBool(const char *path, bool fallback = false) const;
	/// Écrit un champ (objets intermédiaires créés) ; nullptr : le retire.
	/// Hors BeginEdit/EndEdit, une étape d'annulation à lui seul.
	void SetField(const char *path, dataio::NodePtr value);

	// ── Annulation ──────────────────────────────────────────────────────────
	void BeginEdit();
	/// Clôt le geste ; sans changement réel, aucune étape n'est ajoutée.
	void EndEdit();
	bool Undo();
	bool Redo();
	[[nodiscard]] size_t UndoDepth() const noexcept { return m_undo.size(); }
	[[nodiscard]] size_t RedoDepth() const noexcept { return m_redo.size(); }

	/// Écrit le document dans son fichier.
	Result<bool, String> Save();

protected:
	DefinitionDocument() = default;
	/// Modification structurelle (opérations des éditeurs) : `change` reçoit
	/// le document et l'écrit ; une étape d'annulation (hors geste).
	void Mutate(const std::function<void(const dataio::NodePtr &)> &change);
	void Commit(const dataio::NodePtr &before);

	String m_id, m_mod, m_baseDir, m_file;
	dataio::NodePtr m_json;
	bool m_dirty = false;

private:
	std::vector<dataio::NodePtr> m_undo, m_redo;
	dataio::NodePtr m_editStart; ///< instantané au BeginEdit (nullptr : hors geste)
	int m_editDepth = 0;
};

/// Écrit `value` au chemin pointé de `root` (objets intermédiaires créés) ;
/// nullptr retire la clé.
void SetJsonPath(const dataio::NodePtr &root, const char *path, dataio::NodePtr value);

/// JSON sur une ligne (champs de formulaire) : retours et indentation ôtés.
[[nodiscard]] String CompactJson(const String &text);

} // namespace voxelia::studio
