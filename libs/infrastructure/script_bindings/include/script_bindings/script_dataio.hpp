#pragma once
/**
 * script — le module `dataio` (documents multi-formats, glTF, export de
 * maillages) vu depuis le langage `Script`, espace de noms `dataio`.
 *
 * Le module `dataio::` est autonome (aucune dépendance à un device, à un
 * monde ECS ou à une fenêtre), donc ces bindings n'ont pas besoin d'une
 * configuration hôte — un simple drapeau suffit.
 *
 * ── Ce qui vient d'`InstallDataLibrary` (script.hpp) ─────────────────────
 * Les codecs texte comme fonctions GLOBALES (`parse`, `encode`, `load`,
 * `read_file`, `write_file`). Ces bindings ne les dupliquent pas : ils
 * ajoutent sous le namespace `dataio.` les mêmes fonctions (cohérence avec
 * le reste de `script_bindings`) ET la surface « objet » qui manquait.
 *
 * ── Nouveautés exposées ici ──────────────────────────────────────────────
 *   dataio.formats()          → [string] : noms des codecs enregistrés
 *   dataio.clone(v)           → copie PROFONDE d'une valeur (listes/maps)
 *   dataio.parse(fmt, text)   → valeur (alias de `parse`)
 *   dataio.encode(fmt, v)     → string (alias de `encode`)
 *   dataio.document(fmt)      → poignée `dataio.doc` réutilisable
 *   dataio.gltf.load(path)    → poignée `dataio.gltf_doc`
 *   dataio.mesh.save(path, {positions, indices, …}) → bool
 *
 * ── `dataio.doc` ────────────────────────────────────────────────────────
 *   let d = dataio.document("yaml")
 *   if d.decode("nom: Éric") {
 *       print(d.root.nom)         -- Éric (via ValueFromNode)
 *   } else {
 *       print(d.error)            -- message positionné
 *   }
 *   d.root.age = 42
 *   d.save("profil.yaml")
 *
 *   .decode(text) → bool     (d.error est renseigné en cas d'échec)
 *   .encode()     → string
 *   .load(path)   → bool     (lecture + décodage)
 *   .save(path)   → bool
 *   .clear()                 (réinitialise, format conservé)
 *   .root         ↔ value    (converti via NodeFromValue / ValueFromNode)
 *   .error        → string   (vide si la dernière opération a réussi)
 *   .format       → string
 *
 * ── `dataio.gltf_doc` ───────────────────────────────────────────────────
 *   let g = dataio.gltf.load("modèle.gltf")
 *   if g {
 *       print(g.generator, g.version)
 *       let pos = g.read_floats(g.meshes[0].primitives[0].attributes.POSITION)
 *       let idx = g.read_indices(g.meshes[0].primitives[0].indices)
 *   }
 *
 *   Propriétés : .version, .generator, .default_scene
 *   Listes     : .meshes, .nodes, .scenes, .materials, .images, .textures
 *   Méthodes   : .read_floats(i), .read_indices(i), .load_buffers(),
 *                .resolve_uri(s)
 *
 * ── `dataio.mesh.save` ──────────────────────────────────────────────────
 *   dataio.mesh.save("cube.obj", {
 *       positions = [x0,y0,z0, x1,y1,z1, …],
 *       indices   = [0,1,2, 0,2,3, …],
 *       normals   = […],        -- facultatif
 *       uvs       = […],        -- facultatif
 *       colors    = […],        -- facultatif (r,g,b par sommet, 0..1)
 *       materials = [{name, r, g, b, a, first_index, index_count}],
 *       comment   = "mon cube",
 *   })
 */
#include <script/script.hpp>

namespace script {

/// Installe l'espace de noms `dataio` (documents, glTF, export de
/// maillages). Idempotent côté interpréteur : les éventuelles collisions
/// avec des noms déjà enregistrés suivent les règles normales des globals
/// (le dernier écrase).
void InstallDataioLibrary(Interpreter &vm);

} // namespace script