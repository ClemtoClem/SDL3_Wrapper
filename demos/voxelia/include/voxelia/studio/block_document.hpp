#pragma once
/**
 * Document de l'éditeur de blocs — la définition d'UN bloc TELLE QU'ÉCRITE
 * dans son mod (BlockDef::source : `states` compris), séparée du contenu
 * vivant : on l'édite, on la valide à blanc, puis on l'applique à chaud
 * (Session::ReloadDefinition) ou on l'enregistre dans son fichier.
 *
 * Édition, annulation et enregistrement : DefinitionDocument. La validation
 * lit une COPIE du registre (aucune modification du contenu vivant).
 */
#include <array>
#include <vector>

#include "definition_document.hpp"

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

class BlockDocument : public DefinitionDocument {
public:
	BlockDocument() = default;
	/// Document d'un bloc existant (n'importe lequel de ses états) ; NONE si
	/// le bloc est inconnu ou n'a pas de source.
	[[nodiscard]] static Option<BlockDocument> FromBlock(const voxel::ContentRegistry &content, const String &blockId);
	/// Nouveau bloc (cube de pierre) dans `mod`, enregistré dans `file`.
	[[nodiscard]] static BlockDocument New(const String &id, const String &mod, const String &baseDir, const String &file);

	/// Charge le document dans une COPIE de `live` puis la valide : erreurs
	/// de chargement et avertissements qui concernent ce bloc ou ses états.
	[[nodiscard]] std::vector<BlockDiagnostic> Validate(const voxel::ContentRegistry &live) const;
	/// États tels que le moteur les fusionnera (vide si le document ne se charge pas).
	[[nodiscard]] std::vector<MergedState> MergedStates(const voxel::ContentRegistry &live) const;

	// ── Forme nodebox (atelier de formes) ───────────────────────────────────
	/// Boîte en PIXELS du bloc : x1, y1, z1, x2, y2, z2 dans [0, 16] (le JSON
	/// les écrit dans la convention du moteur, −0,5 … 0,5).
	using NodeBox = std::array<float, 6>;
	/// Boîtes de la portée (`state` vide : le bloc commun ; sinon un état,
	/// qui hérite de celles du bloc commun s'il n'en a pas).
	[[nodiscard]] std::vector<NodeBox> NodeBoxes(const String &state = String()) const;
	/// Écrit les boîtes de la portée (« nodebox ») et fait du bloc une
	/// nodebox. Refusé si une boîte est vide ou hors du bloc.
	Result<bool, String> SetNodeBoxes(const std::vector<NodeBox> &boxes, const String &state = String());
	Result<bool, String> AddNodeBox(const NodeBox &box, const String &state = String());
	Result<bool, String> SetNodeBox(size_t index, const NodeBox &box, const String &state = String());
	Result<bool, String> RemoveNodeBox(size_t index, const String &state = String());
	/// Ajoute la copie symétrique (axe 0 : x, 1 : y, 2 : z) d'une boîte.
	Result<bool, String> MirrorNodeBox(size_t index, int axis, const String &state = String());
	/// Tourne toutes les boîtes d'un quart de tour autour de l'axe vertical.
	Result<bool, String> RotateNodeBoxes(const String &state = String());
	/// Le bloc (ou l'état) tel que le moteur le chargera ; NONE s'il ne se charge pas.
	[[nodiscard]] Option<voxel::BlockDef> Loaded(const voxel::ContentRegistry &live, const String &state = String()) const;
};

} // namespace voxelia::studio
