---
name: project-voxelia-edit
description: "Mod « edit » de voxelia (2026-10-27) : éditer, copier, enregistrer, importer et exporter des structures, formes et formules mathématiques ; moteur : Schematic, Formula, dossiers de structures par monde, formulaires et surlignages des mods."
metadata:
  type: project
---

Inspiré des mods Minetest « edit » (outils de sélection, copier / coller /
remplir, fichiers .we et .mts) et « mathplot » (constructions d'après une
formule).

**Moteur (libs/infrastructure/voxel)**
- `formula.hpp` — `Formula::Compile(texte, variables)` puis `Eval` : nombres,
  variables, + - * / % ^, comparaisons (avec tolérance), and / or / not,
  produits implicites (« 2x », « u cos(v) », « (x+1)(x-1) »), fonctions
  usuelles, if(c, a, b), pi / e / tau. Erreurs lisibles avec la position.
  Aucun accès au jeu : sûr pour les formules tapées par le joueur.
- `schematic.hpp` — `Schematic` : boîte de cellules VIDES ou bloc + orientation,
  point d'ancrage, contenu des rangements. `Capture`, `Paste` (rotation, fusion
  de l'air, air seulement), `Bounds`, `Rotated`, `Mirrored` (orientations
  corrigées), `Hollow` (aussi pour les formes plates), `Replace`, `Counts`.
  Formats : `.vxs.json` (natif, palette textuelle + plages), `.json`
  (définition « structure » de mod, orientations en 5e valeur — chargées par
  `ContentRegistry` et `PlaceStructure`), `.we` (WorldEdit, versions 1 à 5),
  `.mts` (Minetest, zlib via archive::Deflate/Inflate). Minetest : +z au nord
  et façade 0 au sud — z inversé et orientation (f + 2) % 4 à l'import comme à
  l'export. `ResolveNames` : alias « structures.aliases » de la configuration
  (le mod edit fournit ceux de Minetest Game), puis « mod:nom » → nom d'un mod
  chargé ; les inconnus sont signalés et vidés.
- `Plot(PlotSpec)` : relation implicite (condition → région pleine ; expression
  → coquille d'un bloc : entre deux voisins de signes opposés, le plus proche
  de zéro), courbe / surface / solide paramétrés (segments et triangles
  pleins, sans trous). Convention mathématique (z vers le haut) ou du jeu,
  échelle, enveloppe, taille maximale et temps de calcul limités.
- `Game` : `StructuresDirectory()` (<sauvegarde>/structures, et dossier
  d'échange `GameOptions::structureDirectory`), `Save/Load/DeleteStructureFile`
  (noms simples seulement), `CaptureRegion` / `PasteSchematic` (rangements
  copiés et remplis), `FillRegion` (all, hollow, walls, outline, keep),
  `ReplaceRegion`, presse-papiers nommés, `SetHighlight` (boîtes dessinées en
  fil de fer par l'application), `SubmitForm` → événement « form_submit ».
- Scripts : `structure.*` (copy, paste, bounds, info, rotate, mirror, hollow,
  replace, set_anchor, box, from_content, duplicate, save, load, list, delete,
  directory, plot), `formula.check` / `formula.eval`, `world.fill`,
  `world.replace`, `world.count`, `game.highlight` / `game.clear_highlight`,
  `entity.target`. Les fonctions d'un mod (commandes, événements, systèmes,
  comportements) s'exécutent désormais avec CE mod pour contexte —
  `voxel.setting` fonctionnait seulement pendant le chargement.

**Voxelia** : `--structures=DIR` (défaut : à côté des sauvegardes,
data/voxelia/structures) ; surlignages (arêtes d'épaisseur constante) ;
écran de formulaire générique (« form » : text, number, slider, bool, choice,
block avec les blocs de l'inventaire, label ; visible_if ; boutons d'action).

**Mod edit** (demos/voxelia/mods/edit) : 11 outils (baguette, copier / couper,
coller avec aperçu et options, déplacer, remplir, effacer, remplacer, annuler /
rétablir, formes, formule, bibliothèque) et la commande `/edit …`. Formes :
ligne, cercle, ellipse, rectangle, parallélépipède, sphère, ellipsoïde,
cylindre, cône, pyramide, dôme, tore (pleines ou creuses, tailles paires
exactes, posées sur le sol). Réglages du mod (par monde) : taille maximale
X / Y / Z de la zone copiable et d'une forme, annulations, temps de calcul,
réservé au créatif / aux commandes, aperçu. Icônes : mod Minetest edit (MIT).

**Tests** : voxel `test_structures.cpp` (formules, transformations, quatre
formats, tracés, dossiers, rangements, API) ; voxelia
`Mods_EditBuildsShapesFormulasAndStoresStructures`.
