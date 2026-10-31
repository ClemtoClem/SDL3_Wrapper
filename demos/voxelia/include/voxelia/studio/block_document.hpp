#pragma once
/**
 * Document de l'éditeur de blocs — la définition d'UN bloc TELLE QU'ÉCRITE
 * dans son mod (BlockDef::source : `states` compris), séparée du contenu
 * vivant : on l'édite, on la valide à blanc, puis on l'applique à chaud
 * (Session::ReloadDefinition) ou on l'enregistre dans son fichier.
 *
 * Le formulaire et l'onglet JSON partagent CE document : le formulaire écrit
 * des chemins pointés (`SetField("light.range", …)`), l'onglet JSON remplace
 * le tout (`SetJson`) — et chacun relit l'autre.
 *
 * Annulation par INSTANTANÉS (comme le sandbox) : BeginEdit / EndEdit
 * encadrent un geste (un glissé de curseur = une étape), 64 étapes au plus,
 * la plus ancienne oubliée au-delà.
 *
 * Modèle sans fenêtre ; la validation lit une COPIE du registre (aucune
 * modification du contenu vivant).
 */
#include <vector>

#include <dataio/node.hpp>
#include <voxel/content.hpp>

namespace voxelia::studio {

/// Diagnostic de validation (sévérité, message, chemin du champ fautif si connu).
struct BlockDiagnostic {
	enum class Severity : uint8_t { Warning, Error } severity = Severity::Warning;
	String message;
	String field; ///< « light.color », « interact.toggle »… (vide : bloc entier)
};

/// Un état tel que le moteur le matérialise (LoadBlockStates) : id, nom, et
/// les champs qui diffèrent de l'état par défaut.
struct MergedState {
	String name, id;
	bool isDefault = false;
	String json; ///< définition fusionnée complète (texte)
	std::vector<String> differences; ///< chemins qui diffèrent de l'état par défaut
};

class BlockDocument {
public:
	static constexpr size_t MAX_UNDO = 64;

	BlockDocument() = default;
	/// Document d'un bloc existant (n'importe lequel de ses états) ; NONE si
	/// le bloc est inconnu ou n'a pas de source.
	[[nodiscard]] static Option<BlockDocument> FromBlock(const voxel::ContentRegistry &content, const String &blockId);
	/// Nouveau bloc (cube de pierre) dans `mod`, enregistré dans `file`.
	[[nodiscard]] static BlockDocument New(const String &id, const String &mod, const String &baseDir, const String &file);

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

	// ── Contenu ─────────────────────────────────────────────────────────────
	/// Charge le document dans une COPIE de `live` puis la valide : erreurs
	/// de chargement et avertissements qui concernent ce bloc ou ses états.
	[[nodiscard]] std::vector<BlockDiagnostic> Validate(const voxel::ContentRegistry &live) const;
	/// États tels que le moteur les fusionnera (vide si le document ne se charge pas).
	[[nodiscard]] std::vector<MergedState> MergedStates(const voxel::ContentRegistry &live) const;
	/// Écrit le document dans son fichier : la définition de même id y est
	/// remplacée (fichier objet ou tableau), ajoutée sinon.
	Result<bool, String> Save();

private:
	void Commit(const dataio::NodePtr &before);

	String m_id, m_mod, m_baseDir, m_file;
	dataio::NodePtr m_json;
	std::vector<dataio::NodePtr> m_undo, m_redo;
	dataio::NodePtr m_editStart; ///< instantané au BeginEdit (nullptr : hors geste)
	int m_editDepth = 0;
	bool m_dirty = false;
};

/// Écrit `value` au chemin pointé de `root` (objets intermédiaires créés) ;
/// nullptr retire la clé.
void SetJsonPath(const dataio::NodePtr &root, const char *path, dataio::NodePtr value);

} // namespace voxelia::studio
