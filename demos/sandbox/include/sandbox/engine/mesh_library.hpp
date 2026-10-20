#pragma once
/**
 * MeshLibrary — les maillages MODÉLISÉS d'un projet (ShapeKind::MESH).
 *
 * Chaque maillage est un fichier `meshes/<nom>.mesh` du projet (format
 * modeling::, JSON lisible). La bibliothèque les charge à la demande, les
 * garde en mémoire pendant l'édition, construit leur maillage d'affichage
 * render3d (reconstruit seulement quand le modèle a changé) et enregistre
 * ceux qui ont été modifiés.
 *
 * Les chemins sont RELATIFS au dossier du projet (la racine) : un
 * « Enregistrer sous » vers un autre dossier change la racine et réécrit
 * tous les maillages au nouvel endroit.
 */
#include <map>
#include <memory>
#include <string>

#include <core/core.hpp>
#include <modeling/model.hpp>
#include <render3d/mesh.hpp>

namespace sandbox {

class MeshLibrary {
public:
	/// Dossier du projet (vide = chemins relatifs au dossier courant).
	void SetRoot(const String &directory);
	[[nodiscard]] const String &Root() const noexcept { return m_root; }
	/// Chemin complet d'une source relative.
	[[nodiscard]] String Resolve(const String &source) const;

	/// Modèle d'une source, chargé au premier appel (un fichier absent donne
	/// un modèle vide, créé à l'enregistrement). Err si le fichier est illisible.
	Result<modeling::Model *, String> Get(const String &source);
	[[nodiscard]] bool IsLoaded(const String &source) const;

	/// Le modèle a été modifié : affichage à reconstruire, fichier à réécrire.
	void MarkDirty(const String &source);
	[[nodiscard]] bool IsDirty(const String &source) const;
	/// Remplace le modèle (annulation, chargement d'un état).
	void Replace(const String &source, modeling::Model model);

	/// Maillage d'affichage (couleurs des matériaux en couleurs de sommets).
	[[nodiscard]] std::shared_ptr<render3d::Mesh> RenderMesh(const String &source);

	/// Écrit une source ; NONE = succès.
	Option<String> Save(const String &source);
	/// Écrit toutes les sources modifiées ; rend la liste des erreurs.
	std::vector<String> SaveAll();
	/// Marque tout comme modifié (changement de racine) — chargés seulement.
	void MarkAllDirty();
	void Clear();

	/// Nom de fichier libre `meshes/<base>.mesh`, `meshes/<base>_2.mesh`…
	[[nodiscard]] String UniqueSource(const String &baseName) const;

	/// Maillage d'affichage d'un modèle (sans cache).
	[[nodiscard]] static render3d::Mesh ToRenderMesh(const modeling::Model &model);
	/// Modèle éditable d'après une primitive render3d (boîte, sphère…) —
	/// pas de double implémentation des formes de base.
	[[nodiscard]] static modeling::Model FromPrimitive(const render3d::PrimitiveSpec &spec);

private:
	struct Entry {
		modeling::Model model;
		std::shared_ptr<render3d::Mesh> mesh;
		uint64_t builtRevision = ~uint64_t(0);
		uint64_t savedRevision = 0;
		bool dirty = false;
	};

	std::map<std::string, Entry> m_entries;
	String m_root;
};

} // namespace sandbox
