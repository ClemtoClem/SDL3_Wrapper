#pragma once
/**
 * Document de l'éditeur d'entités — la définition d'une créature (ou entité,
 * projectile, véhicule…) telle qu'écrite dans son mod, éditée comme les
 * blocs (DefinitionDocument : champs, annulation, enregistrement) :
 *
 *  - comportements (IA) dans l'ordre où ils s'exécutent : ajout, retrait,
 *    réordonnancement, paramètres (un comportement écrit « "wander" » devient
 *    « {"id": "wander", …} » au premier paramètre) ;
 *  - butin, skins du modèle articulé ;
 *  - validation À BLANC (copie du contenu, le contenu vivant n'est jamais
 *    touché) : erreurs de chargement, valeurs physiques, références (modèle,
 *    skins, sons, objets, biomes, blocs d'apparition), comportements inconnus
 *    (ni intégrés, ni définis, ni déclarés par un script) ;
 *  - composants que recevra une entité apparue (SpawnFromDef), résumés.
 */
#include <utility>
#include <vector>

#include "definition_document.hpp"

namespace voxelia::studio {

struct EntityDiagnostic {
	enum class Severity : uint8_t { Warning, Error } severity = Severity::Warning;
	String message;
	String field; ///< chemin du champ (vide : la définition entière)
};

struct EntityBehavior {
	String id;
	dataio::NodePtr params; ///< objet complet (id compris) ; nullptr : écrit sans paramètre
};

class EntityDocument : public DefinitionDocument {
public:
	EntityDocument() = default;
	/// Entité existante ; NONE si inconnue ou sans source.
	[[nodiscard]] static Option<EntityDocument> FromEntity(const voxel::ContentRegistry &content, const String &entityId);
	/// Nouvelle créature : boîte 0,6 × 1,8 × 0,6, 10 PV, « wander ».
	[[nodiscard]] static EntityDocument New(const String &id, const String &mod, const String &baseDir, const String &file,
											const String &kind = String("creature"));

	[[nodiscard]] String Kind() const { return FieldText("type", String("creature")); }

	// ── Comportements ───────────────────────────────────────────────────────
	[[nodiscard]] std::vector<EntityBehavior> Behaviors() const;
	Result<bool, String> AddBehavior(const String &id);
	Result<bool, String> RemoveBehavior(size_t index);
	Result<bool, String> MoveBehavior(size_t from, size_t to);
	/// `value` nullptr : le paramètre est retiré.
	Result<bool, String> SetBehaviorParam(size_t index, const String &key, dataio::NodePtr value);

	// ── Butin, skins ────────────────────────────────────────────────────────
	[[nodiscard]] std::vector<std::pair<String, int>> Drops() const;
	Result<bool, String> AddDrop(const String &item, int count);
	Result<bool, String> RemoveDrop(size_t index);
	[[nodiscard]] std::vector<String> Skins() const;
	Result<bool, String> AddSkin(const String &texture);
	Result<bool, String> RemoveSkin(size_t index);

	// ── Contrôles ───────────────────────────────────────────────────────────
	/// `scriptBehaviors` : comportements déclarés par les scripts (ScriptHost::BehaviorIds).
	[[nodiscard]] std::vector<EntityDiagnostic> Validate(const voxel::ContentRegistry &live,
														 const std::vector<String> &scriptBehaviors) const;
	/// Composants d'une entité apparue (nom, résumé), dans l'ordre de SpawnFromDef.
	[[nodiscard]] std::vector<std::pair<String, String>> Components(const voxel::ContentRegistry &live) const;
};

} // namespace voxelia::studio
