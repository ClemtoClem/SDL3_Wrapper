#pragma once
/**
 * dataio::gltf — ÉCRITURE de documents glTF 2.0 (`.gltf` JSON + `.bin` à
 * côté), le pendant de la lecture (gltf.hpp) : une scène décrite de façon
 * neutre (maillages, matériaux, hiérarchie de nœuds, animations) est
 * sérialisée telle que Blender, Blockbench ou un moteur glTF la relisent.
 *
 * @code{.cpp}
 * gltf::SceneDescription scene;
 * scene.meshes.push_back({"tete", positions, normals, uvs, indices, 0});
 * scene.materials.push_back({"skin", "skin.png"});
 * scene.nodes.push_back({"tete", 0, -1, {0, 1.5f, 0}});
 * gltf::AnimationDescription nod{"hochement"};
 * nod.channels.push_back({0, "rotation", times, quaternions, "LINEAR"});
 * scene.animations.push_back(nod);
 * if (auto error = gltf::SaveScene("modele.gltf", scene); error.IsSome()) …
 * @endcode
 *
 * Écrit : un tampon unique (`<nom>.bin`, aligné sur 4 octets), une vue et
 * un accesseur par tableau ; POSITION porte ses min / max (exigés par la
 * spécification), les temps d'animation aussi. Échantillonneur au plus proche
 * pour les textures (pixel art). Rien d'embarqué en base64.
 */
#include <vector>

#include <core/core.hpp>

namespace dataio::gltf {

struct MeshDescription {
	String name;
	std::vector<float> positions; ///< x, y, z par sommet
	std::vector<float> normals;   ///< facultatif (x, y, z)
	std::vector<float> uvs;       ///< facultatif (u, v ; v vers le BAS, convention glTF)
	std::vector<uint32_t> indices;
	int material = -1;
};

struct MaterialDescription {
	String name;
	String baseColorUri; ///< image (relative au .gltf) ; vide : couleur seule
	float baseColor[4] = {1.f, 1.f, 1.f, 1.f};
	bool doubleSided = true;
	bool alphaMask = true; ///< pixels transparents découpés (skins)
};

struct NodeDescription {
	String name;
	int mesh = -1;
	int parent = -1; ///< indice d'un nœud PRÉCÉDENT ; -1 : racine
	float translation[3] = {0.f, 0.f, 0.f};
	float rotation[4] = {0.f, 0.f, 0.f, 1.f}; ///< quaternion x, y, z, w
	float scale[3] = {1.f, 1.f, 1.f};
};

struct ChannelDescription {
	int node = -1;
	String path;                ///< « translation », « rotation », « scale »
	std::vector<float> times;   ///< secondes, croissants
	std::vector<float> values;  ///< 3 (translation, scale) ou 4 (rotation) flottants par temps
	String interpolation = "LINEAR"; ///< « LINEAR » ou « STEP »
};

struct AnimationDescription {
	String name;
	std::vector<ChannelDescription> channels;
};

struct SceneDescription {
	String generator = "dataio::gltf";
	std::vector<MeshDescription> meshes;
	std::vector<MaterialDescription> materials;
	std::vector<NodeDescription> nodes;
	std::vector<AnimationDescription> animations;
};

/// Vérifie la cohérence (indices de maillage / matériau / parent / nœud,
/// tailles des tableaux) ; NONE si la scène est valide.
[[nodiscard]] Option<String> ValidateScene(const SceneDescription &scene);

/// Écrit `path` (.gltf) et son tampon (même nom, extension .bin) ; NONE si
/// tout est écrit, sinon le message d'erreur.
[[nodiscard]] Option<String> SaveScene(const String &path, const SceneDescription &scene);

} // namespace dataio::gltf
