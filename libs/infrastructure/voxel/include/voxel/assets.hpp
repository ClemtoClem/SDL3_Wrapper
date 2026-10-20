#pragma once
/**
 * AssetManager — chargement, MISE EN CACHE et LIBÉRATION centralisés des
 * ressources du contenu, sur resources::ResourceCache (catalogue
 * d'identifiants stables, fabriques par nature, chargement synchrone ou en
 * arrière-plan, ressources manquantes recensées, `ReleaseUnused`).
 *
 *  - Textures (TextureImage) : pixels RGBA en mémoire, lus d'un fichier
 *    image (PNG…) ou GÉNÉRÉS d'après la définition ("procedural" :
 *    motif, couleurs, bruit — un mod peut se passer d'images).
 *  - Modèles (ModelResource) : maillages .obj, .b3d (Blitz3D, comme
 *    Minetest ; « .b3b » accepté) ou JSON (sommets/faces, ou boîtes).
 *  - Sons : seulement CATALOGUÉS ici (AssetType::AUDIO_CLIP + chemin) ;
 *    l'application enregistre la fabrique de son moteur audio.
 *
 * Tout est indépendant du GPU : l'application fabrique ses textures et
 * maillages d'affichage à partir de ces données (atlas, cf. meshing.hpp).
 */
#include <functional>
#include <memory>
#include <vector>

#include <resources/cache.hpp>

#include "content.hpp"

namespace voxel {

struct RigModel;

struct Image {
	int width = 0, height = 0;
	std::vector<uint8_t> rgba; ///< width × height × 4
	[[nodiscard]] bool IsEmpty() const noexcept { return rgba.empty(); }
	[[nodiscard]] const uint8_t *At(int x, int y) const { return &rgba[size_t((y * width + x) * 4)]; }
};

/// Texture générée d'après sa description (déterministe).
[[nodiscard]] Image GenerateTexture(const NodePtr &procedural, uint32_t seed);
/// Image lue d'un fichier (PNG, BMP…), convertie en RGBA.
[[nodiscard]] Result<Image, String> LoadImageFile(const String &path);

/// Chargement d'une image désignée par son nom dans une composition.
using ImageLoader = std::function<Result<Image, String>(const String &name)>;

/**
 * Composition de textures (dans l'esprit des modificateurs de Minetest) :
 *
 *   "(terre.png^herbe.png)^lighting(0.5)"
 *
 *   a^b             superpose b sur a (à la plus grande des deux tailles ;
 *                   transparence respectée)
 *   ( … )           groupement
 *   nom             fichier image (terre.png) ou id de texture (core:dirt)
 *   a^filtre(args)  applique un filtre à TOUT ce qui précède :
 *     lighting(f) (alias ligthing, light, brightness)   RVB × f
 *     darken(f)                       RVB × (1 − f)
 *     tint(r, g, b) (alias multiply)  multiplie par une couleur
 *     colorize(r, g, b [, part])      mélange vers une couleur (part 0..1, défaut 0.5)
 *     opacity(a)                      alpha × a
 *     grayscale()  invert()  noalpha()  flipx()  flipy()
 *     rotate(90 | 180 | 270)          sens horaire
 *     resize(l, h)                    au plus proche voisin
 *     crop(x, y, l, h)                sous-image
 *     frame(n, total)                 n-ième image d'une bande animée verticale
 *   color(r, g, b [, a])              aplat 16×16 utilisable comme image
 *
 * Couleurs en 0..255 (ou 0..1 si toutes les composantes sont ≤ 1).
 */
[[nodiscard]] Result<Image, String> ComposeTexture(const String &expression, const ImageLoader &load);

class TextureImage final : public resources::Resource {
public:
	/// Image calculée à la demande (composition).
	using Builder = std::function<Result<Image, String>()>;

	TextureImage(String key, String path, NodePtr procedural, uint32_t seed, Builder builder = {})
		: m_key(std::move(key)), m_path(std::move(path)), m_procedural(std::move(procedural)), m_seed(seed),
		  m_builder(std::move(builder)) {}
	[[nodiscard]] StringView Path() const noexcept override { return m_key.View(); }
	[[nodiscard]] const Image &Pixels() const noexcept { return m_image; }

protected:
	Result<bool, String> DoLoad() override;
	void DoUnload() override { m_image = Image{}; }

private:
	String m_key, m_path;
	NodePtr m_procedural;
	uint32_t m_seed;
	Builder m_builder;
	Image m_image;
};

/// Maillage de modèle en mémoire (coordonnées du modèle ; un bloc fait 1).
struct ModelMesh {
	std::vector<math::FVector3> positions, normals;
	std::vector<math::FVector2> uvs;
	std::vector<uint32_t> indices;
	/// Triangles [first, first + count[ texturés par `texture` (vide : la
	/// texture de la définition qui utilise le modèle).
	struct Group {
		uint32_t first = 0, count = 0;
		String texture;
	};
	std::vector<Group> groups;
	[[nodiscard]] Box Bounds() const;
	/// Ramené dans le cube unité (centré en x/z, posé en y = 0) si `fit`.
	void Normalize(bool fit);
};

[[nodiscard]] Result<ModelMesh, String> ParseObj(const String &text);
[[nodiscard]] Result<ModelMesh, String> ParseB3D(const std::vector<uint8_t> &bytes);
[[nodiscard]] Result<ModelMesh, String> ModelFromJson(const NodePtr &data);

class ModelResource final : public resources::Resource {
public:
	ModelResource(String key, String format, String path, NodePtr data)
		: m_key(std::move(key)), m_format(std::move(format)), m_path(std::move(path)), m_data(std::move(data)) {}
	[[nodiscard]] StringView Path() const noexcept override { return m_key.View(); }
	[[nodiscard]] const ModelMesh &Mesh() const noexcept { return m_mesh; }

protected:
	Result<bool, String> DoLoad() override;
	void DoUnload() override { m_mesh = ModelMesh{}; }

private:
	String m_key, m_format, m_path;
	NodePtr m_data;
	ModelMesh m_mesh;
};

class AssetManager {
public:
	explicit AssetManager(size_t threads = 1);

	/// Catalogue les textures, modèles et sons du contenu (rien n'est chargé).
	void Declare(const ContentRegistry &content);

	/// Texture prête (chargée au premier appel) ; nullptr si inconnue ou en échec.
	[[nodiscard]] std::shared_ptr<TextureImage> Texture(const String &id);
	/// Modèle articulé (ModelDef "rig", ou maillage .obj d'une partie) ;
	/// nullptr si inconnu ou en échec (l'erreur est notée dans Missing()).
	[[nodiscard]] std::shared_ptr<const RigModel> Rig(const String &id);
	/// Modèle par id de ModelDef OU par chemin de fichier.
	[[nodiscard]] std::shared_ptr<ModelResource> Model(const String &idOrPath);
	/// Identifiant de catalogue d'un son (pour le moteur audio) ; `variant` :
	/// fichier parmi ceux du SoundDef (« paths »).
	[[nodiscard]] resources::AssetId SoundId(const String &id, int variant = 0) const;
	[[nodiscard]] Option<String> SoundPath(const String &id) const;

	/// Libère ce que plus personne ne tient (rechargé à la demande).
	size_t ReleaseUnused();
	[[nodiscard]] size_t ResidentCount() const;
	[[nodiscard]] std::vector<resources::MissingAsset> Missing();
	[[nodiscard]] resources::ResourceCache &Cache() noexcept { return m_cache; }

	[[nodiscard]] static resources::AssetId TextureId(const String &id);
	[[nodiscard]] static resources::AssetId ModelId(const String &idOrPath);

	/// Pixels d'une TextureDef calculés directement (fichier, procédure ou
	/// composition), sans passer par le cache.
	[[nodiscard]] Result<Image, String> RenderTexture(const String &id, int depth = 0) const;

private:
	/// Nom d'une composition → image : id de texture, ou fichier cherché dans
	/// le mod (dossier de la définition, textures/, dossiers de ses textures).
	[[nodiscard]] Result<Image, String> ResolveImage(const String &name, const TextureDef &owner, int depth) const;

	resources::ResourceCache m_cache;
	const ContentRegistry *m_content = nullptr;
	std::map<std::string, std::shared_ptr<const RigModel>> m_rigs;
	std::vector<resources::MissingAsset> m_rigErrors;
};

} // namespace voxel

template <> struct resources::AssetTypeOf<voxel::TextureImage> {
	static constexpr AssetType value = AssetType::TEXTURE;
};
template <> struct resources::AssetTypeOf<voxel::ModelResource> {
	static constexpr AssetType value = AssetType::MESH;
};
