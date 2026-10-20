#pragma once
/**
 * render3d — les ressources de rendu en `resources::Resource` : maillages,
 * textures, matériaux, shaders.
 *
 * Elles ENVELOPPENT les types existants du module (Mesh, Material,
 * ShaderKind) pour leur donner ce qui manque à une ressource partagée :
 *  - une identité stable (AssetId) et une entrée de catalogue ;
 *  - un chargement asynchrone (Load/DoLoad sur l'AsyncLoader du
 *    ResourceCache), avec états Unloaded → Loading → Ready / Failed ;
 *  - le partage : un MeshNode (composant ECS) ne garde qu'un
 *    `AssetRef<MeshResource>`, deux nœuds du même maillage lisent la même
 *    géométrie.
 *
 * `RegisterResourceFactories(cache)` apprend au cache à recréer chacune
 * depuis son entrée de catalogue (chemin, clé de primitive, paramètres de
 * matériau). Les fonctions `Import*`, `Primitive`, `CreateMaterial`… les
 * cataloguent et rendent leur référence.
 */
#include <resources/cache.hpp>
#include "material.hpp"
#include "mesh.hpp"

namespace render3d {

using resources::AssetId;
using resources::AssetRecord;
using resources::AssetRef;
using resources::AssetType;
using resources::LoadPolicy;
using resources::ResourceCache;

// ============================================================================
// MeshResource — géométrie partagée
// ============================================================================

class MeshResource final : public resources::Resource {
public:
	/// Lue d'un fichier glTF (`diskPath`), cataloguée sous `source`.
	[[nodiscard]] static std::shared_ptr<MeshResource> FromFile(String source, String diskPath);
	/// Construite depuis une primitive paramétrée.
	[[nodiscard]] static std::shared_ptr<MeshResource> FromPrimitive(const PrimitiveSpec &spec);
	/// Fournie par le programme (prête d'emblée).
	[[nodiscard]] static std::shared_ptr<MeshResource> FromGeometry(String name, Mesh geometry);

	[[nodiscard]] StringView Path() const noexcept override { return m_source.View(); }
	/// Géométrie — significative seulement quand IsReady().
	[[nodiscard]] const Mesh &Geometry() const noexcept { return *m_geometry; }
	/// La même, à PARTAGER avec les Shape qui la montrent (render3d::Shape
	/// garde sa référence : décharger la ressource ne retire rien de l'écran).
	/// Fil principal, une fois IsReady() (le rendu y crée les tampons GPU).
	[[nodiscard]] std::shared_ptr<Mesh> SharedGeometry() const noexcept { return m_geometry; }
	[[nodiscard]] const Option<PrimitiveSpec> &Primitive() const noexcept { return m_primitive; }

protected:
	Result<bool, String> DoLoad() override;
	void DoUnload() override;

private:
	MeshResource() = default;

	String m_source;
	String m_diskPath;
	Option<PrimitiveSpec> m_primitive = NONE;
	bool m_provided = false;
	std::shared_ptr<Mesh> m_geometry = std::make_shared<Mesh>();
};

// ============================================================================
// TextureResource — pixels en mémoire (l'envoi au GPU est l'affaire du rendu)
// ============================================================================

class TextureResource final : public resources::Resource {
public:
	[[nodiscard]] static std::shared_ptr<TextureResource> FromFile(String source, String diskPath);
	/// Pixels RGBA fournis par le programme (prêts d'emblée).
	[[nodiscard]] static std::shared_ptr<TextureResource> FromPixels(String name, int width, int height,
																	 std::vector<uint8_t> rgba);

	[[nodiscard]] StringView Path() const noexcept override { return m_source.View(); }
	[[nodiscard]] int Width() const noexcept { return m_width; }
	[[nodiscard]] int Height() const noexcept { return m_height; }
	/// RGBA 8 bits par canal, ligne par ligne.
	[[nodiscard]] const std::vector<uint8_t> &Pixels() const noexcept { return m_rgba; }

protected:
	Result<bool, String> DoLoad() override;
	void DoUnload() override;

private:
	TextureResource() = default;

	String m_source;
	String m_diskPath;
	bool m_provided = false;
	int m_width = 0;
	int m_height = 0;
	std::vector<uint8_t> m_rgba;
};

// ============================================================================
// ShaderResource — programme de rendu
// ============================================================================

class ShaderResource final : public resources::Resource {
public:
	/// Programme intégré pour un modèle d'éclairage.
	[[nodiscard]] static std::shared_ptr<ShaderResource> Builtin(ShaderKind kind);
	/// Sources sur disque (vertex + fragment).
	[[nodiscard]] static std::shared_ptr<ShaderResource> FromFiles(String name, String vertexPath, String fragmentPath);

	[[nodiscard]] StringView Path() const noexcept override { return m_source.View(); }
	[[nodiscard]] ShaderKind Kind() const noexcept { return m_kind; }
	[[nodiscard]] bool IsBuiltin() const noexcept { return m_vertexPath.IsEmpty(); }
	[[nodiscard]] const String &VertexSource() const noexcept { return m_vertexSource; }
	[[nodiscard]] const String &FragmentSource() const noexcept { return m_fragmentSource; }

	[[nodiscard]] static const char *KindName(ShaderKind kind) noexcept;

protected:
	Result<bool, String> DoLoad() override;
	void DoUnload() override;

private:
	ShaderResource() = default;

	String m_source;
	ShaderKind m_kind = ShaderKind::PHONG;
	String m_vertexPath, m_fragmentPath;
	String m_vertexSource, m_fragmentSource;
};

// ============================================================================
// MaterialResource — paramètres de surface + RÉFÉRENCES (texture, shader)
// ============================================================================

class MaterialResource final : public resources::Resource {
public:
	/// Matériau défini par ses paramètres (prêt d'emblée). `material.albedo`
	/// (texture GPU) est ignoré : la texture est une RÉFÉRENCE (`albedo`),
	/// résolue par le rendu.
	[[nodiscard]] static std::shared_ptr<MaterialResource> Create(String name, Material material,
																  AssetRef<TextureResource> albedo = {},
																  AssetRef<ShaderResource> shader = {});

	[[nodiscard]] StringView Path() const noexcept override { return m_source.View(); }
	[[nodiscard]] const Material &Parameters() const noexcept { return m_material; }
	[[nodiscard]] AssetRef<TextureResource> Albedo() const noexcept { return m_albedo; }
	[[nodiscard]] AssetRef<ShaderResource> Shader() const noexcept { return m_shader; }

	/// Modifier un matériau partagé : tous ceux qui le référencent le voient.
	/// Fil principal uniquement (le rendu le lit à chaque image).
	void Set(Material material, AssetRef<TextureResource> albedo, AssetRef<ShaderResource> shader);

	/// Paramètres + références en JSON (entrée de catalogue).
	[[nodiscard]] dataio::NodePtr ToJson() const;

protected:
	Result<bool, String> DoLoad() override { return Ok(true); }
	void DoUnload() override {}

private:
	MaterialResource() = default;

	String m_source;
	Material m_material;
	AssetRef<TextureResource> m_albedo;
	AssetRef<ShaderResource> m_shader;
};

/// Paramètres d'un matériau (sans texture GPU) en JSON, et retour.
[[nodiscard]] dataio::NodePtr MaterialToJson(const Material &material);
[[nodiscard]] Material MaterialFromJson(const dataio::NodePtr &json);

// ============================================================================
// Catalogue : fabriques et raccourcis
// ============================================================================

/// Apprend à `cache` à recréer maillages, textures, matériaux et shaders
/// depuis leur entrée de catalogue.
void RegisterResourceFactories(ResourceCache &cache);

/// Catalogue un fichier glTF sous un id tiré de son chemin (relatif à la
/// base du cache) et lance son chargement. Le même fichier rend le même id.
AssetRef<MeshResource> ImportMesh(ResourceCache &cache, const String &path, LoadPolicy policy = LoadPolicy::ASYNC);
/// Primitive partagée (même spec → même ressource).
AssetRef<MeshResource> Primitive(ResourceCache &cache, const PrimitiveSpec &spec, LoadPolicy policy = LoadPolicy::SYNC);
/// Maillage généré par le programme (id aléatoire).
AssetRef<MeshResource> AddMesh(ResourceCache &cache, String name, Mesh geometry);

AssetRef<TextureResource> ImportTexture(ResourceCache &cache, const String &path, LoadPolicy policy = LoadPolicy::ASYNC);
AssetRef<TextureResource> AddTexture(ResourceCache &cache, String name, int width, int height, std::vector<uint8_t> rgba);

/// Matériau nommé (id tiré du nom) : créé, ou remplacé s'il existe.
AssetRef<MaterialResource> CreateMaterial(ResourceCache &cache, String name, Material material,
										  AssetRef<TextureResource> albedo = {}, AssetRef<ShaderResource> shader = {});

AssetRef<ShaderResource> BuiltinShader(ResourceCache &cache, ShaderKind kind);

} // namespace render3d

template <> struct resources::AssetTypeOf<render3d::MeshResource> {
	static constexpr AssetType value = AssetType::MESH;
};
template <> struct resources::AssetTypeOf<render3d::TextureResource> {
	static constexpr AssetType value = AssetType::TEXTURE;
};
template <> struct resources::AssetTypeOf<render3d::ShaderResource> {
	static constexpr AssetType value = AssetType::SHADER;
};
template <> struct resources::AssetTypeOf<render3d::MaterialResource> {
	static constexpr AssetType value = AssetType::MATERIAL;
};
