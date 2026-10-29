#pragma once
/**
 * Références d'ASSETS : ce que les nœuds, les matériaux et les fichiers
 * écrivent pour désigner une ressource partagée.
 *
 * Une ressource (maillage, texture, matériau, shader, son…) est une donnée
 * réutilisable, indépendante de toute position dans le monde. Elle vit UNE
 * fois en mémoire (cf. assets::AssetManager) ; ce qui l'utilise ne garde
 * qu'un `AssetRef<T>` — un identifiant stable (Uuid), jamais un pointeur ni
 * une copie. Dans les fichiers, on écrit cet identifiant ; au chargement, on
 * le résout vers la ressource en mémoire, et une référence qui ne résout pas
 * est SIGNALÉE (asset manquant) au lieu de casser le chargement.
 *
 * Ce fichier ne dépend d'aucun type concret de ressource : `AssetRef<T>`
 * n'a besoin que du TRAIT `AssetTypeOf<T>`, spécialisé à côté de chaque
 * ressource concrète (render3d::MeshResource, audio::AudioClipResource…).
 * Les ressources elles-mêmes sont des `Resource` (resource.hpp), chargées et
 * résolues par `ResourceCache` (cache.hpp).
 */
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <core/core.hpp>
#include <dataio/node.hpp>

namespace resources {

/// Nature d'une ressource.
enum class AssetType : uint8_t {
	UNKNOWN,
	MESH,
	TEXTURE,
	MATERIAL,
	SHADER,
	AUDIO_CLIP,
	OBJECT_DEFINITION, ///< arbre de nœuds réutilisable (cf. scene/objects)
	BYTES,             ///< contenu brut d'un fichier (BytesResource)
};

[[nodiscard]] const char *AssetTypeName(AssetType type) noexcept;
[[nodiscard]] AssetType AssetTypeFromName(StringView name) noexcept;

/// Identifiant stable d'une ressource.
struct AssetId {
	Uuid uuid;

	/// Identifiant DÉTERMINISTE d'un fichier (chemin normalisé, relatif à la
	/// racine des ressources) : réimporter le même fichier redonne le même id.
	[[nodiscard]] static AssetId FromPath(StringView normalizedPath) noexcept;
	/// Identifiant déterministe d'une ressource GÉNÉRÉE à partir d'une clé
	/// canonique (« box 2 4.6 0.6 », « material:Pierre »…).
	[[nodiscard]] static AssetId FromKey(StringView key) noexcept;
	/// Identifiant aléatoire (ressource créée sans fichier ni clé).
	[[nodiscard]] static AssetId Generate() { return AssetId{Uuid::Generate()}; }

	[[nodiscard]] static Option<AssetId> Parse(StringView text) noexcept;
	[[nodiscard]] String ToString() const { return uuid.ToString(); }

	[[nodiscard]] bool IsValid() const noexcept { return !uuid.IsNil(); }
	[[nodiscard]] bool operator==(const AssetId &o) const noexcept { return uuid == o.uuid; }
	[[nodiscard]] bool operator!=(const AssetId &o) const noexcept { return uuid != o.uuid; }
	[[nodiscard]] bool operator<(const AssetId &o) const noexcept { return uuid < o.uuid; }
};

/// Trait : la nature d'un type de ressource. Spécialisé à côté de chaque type
/// (`template <> struct AssetTypeOf<render3d::TextureResource> { … TEXTURE; }`).
template <typename T> struct AssetTypeOf;

/// Référence typée vers une ressource : ne compile qu'avec le bon type
/// (un `AssetRef<Texture>` ne se passe pas là où un maillage est attendu).
template <typename T> struct AssetRef {
	AssetId id;

	[[nodiscard]] static constexpr AssetType Type() noexcept { return AssetTypeOf<T>::value; }
	[[nodiscard]] bool IsValid() const noexcept { return id.IsValid(); }
	[[nodiscard]] bool operator==(const AssetRef &o) const noexcept { return id == o.id; }
	[[nodiscard]] bool operator!=(const AssetRef &o) const noexcept { return id != o.id; }

	/// Écrite dans un fichier : l'identifiant seul (vide = aucune).
	[[nodiscard]] String ToString() const { return id.IsValid() ? id.ToString() : String(); }
	[[nodiscard]] static AssetRef FromString(StringView text) noexcept {
		auto parsed = AssetId::Parse(text);
		return parsed.IsSome() ? AssetRef{parsed.Unwrap()} : AssetRef{};
	}
};

/// Une entrée du catalogue : ce qui suffit pour retrouver et recharger une
/// ressource sans l'avoir en mémoire.
struct AssetRecord {
	AssetId id;
	AssetType type = AssetType::UNKNOWN;
	String name;   ///< affiché (« Pierre », « knight.gltf »)
	String path;   ///< fichier source, relatif à la racine (vide : générée)
	String key;    ///< clé canonique d'une ressource générée (« box 1 1 1 »)
	/// Paramètres propres au type, pour les ressources sans fichier
	/// (matériau : couleur, rugosité… ; primitive : dimensions).
	dataio::NodePtr params;
};

/// Une référence qui n'a pas pu être résolue (au chargement d'un fichier ou
/// à la première utilisation) : l'appelant l'affiche, rien ne s'arrête.
struct MissingAsset {
	AssetId id;
	AssetType type = AssetType::UNKNOWN;
	String context; ///< qui la demandait (« /scenes/Donjon/Joueur »)
	String reason;
};

/// Catalogue des ressources connues : id ↔ chemin/clé, sans les données.
/// Sérialisable (manifeste d'un projet, cache d'import).
class AssetCatalog {
public:
	/// Ajoute ou remplace (même id).
	const AssetRecord &Put(AssetRecord record);
	bool Remove(AssetId id);

	[[nodiscard]] const AssetRecord *Find(AssetId id) const noexcept;
	[[nodiscard]] const AssetRecord *FindByPath(StringView path) const noexcept;
	[[nodiscard]] const AssetRecord *FindByKey(StringView key) const noexcept;
	[[nodiscard]] std::vector<const AssetRecord *> OfType(AssetType type) const;
	[[nodiscard]] const std::vector<AssetRecord> &Records() const noexcept { return m_records; }
	[[nodiscard]] size_t Size() const noexcept { return m_records.size(); }

	/// `{"format": "assets.catalog", "version": 1, "assets": [...]}`.
	[[nodiscard]] dataio::NodePtr ToJson() const;
	[[nodiscard]] static Result<AssetCatalog, String> FromJson(const dataio::NodePtr &json);

	static constexpr int FORMAT_VERSION = 1;

private:
	struct IdHash {
		size_t operator()(const AssetId &id) const noexcept;
	};
	std::vector<AssetRecord> m_records;
	/// Index id → position dans m_records : Put/Find en temps constant
	/// (un catalogue de mods compte des milliers d'entrées).
	std::unordered_map<AssetId, size_t, IdHash> m_index;
};

} // namespace resources

template <> struct std::hash<resources::AssetId> {
	size_t operator()(const resources::AssetId &id) const noexcept { return std::hash<Uuid>{}(id.uuid); }
};
