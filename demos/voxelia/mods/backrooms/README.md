# Mod « backrooms »

Chaque niveau des Backrooms est une **dimension** à part, avec son générateur
de labyrinthe (`kind: "maze"`), plusieurs **étages** reliés par des
**escaliers**, et un **ascenseur** (mod `elevators`) qui mène au niveau
suivant. Dépendances : `core`, `elevators`.

| Dimension | Niveau | Ascenseur |
| --- | --- | --- |
| `backrooms:backrooms` | 0 — couloirs jaunes (on y arrive aussi en mourant) | style backrooms |
| `backrooms:level_2` | 2 — parking | industrial |
| `backrooms:level_4` | 4 — hôpital (étages : hall et salles de soins en alternance) | modern |
| `backrooms:level_5` | 5 — hôtel | backrooms |
| `backrooms:level_37` | 37 — piscines (bassins au rez-de-chaussée) | modern ; + la sortie, au dernier étage |

## Nombre d'étages

Réglages du mod (écran « Mods ») :

- **Nombre d'étages par niveau** (`number_of_floors_per_level`, 3 à 16, défaut 4) : tous les niveaux ;
- **Par niveau** (`floors_level_0`, `_2`, `_4`, `_5`, `_37`, 0 à 16) : remplace le réglage général pour ce niveau ; 0 = réglage général.

Les gaines d'ascenseur traversent tous les étages et leurs destinations suivent. Un monde garde les valeurs du moment où il les a lues pour la première fois (elles sont enregistrées avec lui) : changer le réglage vaut pour les mondes créés ensuite, pas pour les niveaux déjà générés.

Parcours : 0 → 2 → 4 → 5 → 37 → surface (`core:overworld`). Chaque arrivée
propose aussi « Retour ».

La sortie : au niveau 37, une seconde gaine (cellule [−1, 0], à l'ouest du
point d'arrivée) ne dessert que le **dernier étage** ; il faut donc monter
les escaliers depuis l'ascenseur d'arrivée. Son ascenseur, mis en service
tout seul, mène à la surface de la dimension normale : un kiosque
d'ascenseur y est bâti au sol là où l'on arrive, et permet de redescendre
(« Retour ») jusqu'au dernier étage.

Usure : plafond (et celui de l'hôtel), papier peint et moquette du niveau 0
sont des textures à variantes (`lobby_*_worn` dans `world.json`) tirées d'après
la position ; les variantes abîmées (tache d'eau, plâtre tombé, coulure, lé
décollé, moquette humide) y sont rares parce que la texture saine est répétée
dans la liste.

## Déclarer un niveau (`world.json`)

1. Une dimension (`"type": "dimension"`) qui nomme son générateur.
2. Un générateur `"kind": "maze"` :

```jsonc
"maze": {
  "floor": 0, "height": 5, "cell": 7, "region": 96, "doorWidth": 2,
  // Étages : un nombre, la liste des niveaux de chaque étage, ou un nombre et
  // une liste répétée ; une valeur peut venir d'un réglage du mod.
  "floors": {"count": {"setting": "floors_level_4", "fallback": {"setting": "number_of_floors_per_level"}},
             "levels": ["hospital", "hospital_ward"]},
  "stairs": {"block": "core:white_concrete_stair", "fill": "hospital_wall_tiles", "chance": 0.06},
  "elevator": {"cell": [1, 0], "side": "s", "pit": 6, "overrun": 7,
               "blocks": {"wall": "elevators:shaft_wall", "rail": "elevators:rail",
                          "motor": "elevators:motor", "door": "elevators:modern_landing_door",
                          "doorTop": "elevators:modern_landing_door_top",
                          "frame": "elevators:modern_frame", "call": "elevators:modern_call",
                          "display": "elevators:modern_display", "arrow": "elevators:modern_arrow"}}
}
```

- **Étages** (16 au plus) : l'étage `k` a son sol en `floor + k × (height + 1)` ;
  chacun a son propre tracé (graine décalée). Une liste donne le « level »
  (biome, matériaux) de chaque étage ; un nombre répète le premier ;
  `{"count", "levels"}` répète la liste sur `count` étages.
- **Réglages** : partout dans un générateur, `{"setting": "id"}` vaut le
  réglage `id` du mod (mod.json), `"fallback"` quand il vaut 0 ou manque.
- **Grilles de ventilation** (dans chaque « level ») : `"vent": "ventilation"`
  et `"ventChance"` (part des cellules) — un diffuseur encastré dans le
  plafond (modèle `models/ventilation.obj` : collerette, lames concentriques
  en gradins, moyeu, gaine et ventilateur ; une texture par partie,
  `backrooms_vent_*`), hors des murs, escaliers, gaines et néons.
- **Néons encastrés** (`fluorescent`, états on / off / half / auto) : un
  modèle par aspect (`models/fluorescent_{on,off,half}.obj` : cadre à fleur,
  réflecteurs inclinés, fond, capot de ballast, six tubes et leurs supports,
  boîtier en tôle galvanisée) ; textures `backrooms_lamp_*` tirées des photos
  `backrooms_ceil_lamp_*.png` (tubes, fond dépoli, cadre), dos et flancs du
  boîtier dessinés.
- **Escaliers** : une proportion `chance` des cellules reçoit une volée
  montante (marches d'un bloc, vers +x) en `block`, le mur sous la volée en
  `fill`, et une trémie dans la dalle au-dessus. Absent : pas d'escalier.
- **Ascenseur** : une gaine 7 × 7 dans la cellule `cell`, palier côté `side`
  à chaque étage, fosse `pit` sous le bas, réserve `overrun` au-dessus du haut,
  moteur au sommet. Le mod `elevators` y pose la cabine et met l'ascenseur en
  service. Plusieurs gaines : `"elevators": [{…}, {…}]`, chacune avec son
  `"id"` (« main » par défaut) et ses `"floors"` : `"all"` (défaut),
  `"first"`, `"last"`, ou `[de, à]` (−1 : le dernier étage). Hors de ses
  étages, la cellule de la gaine est une salle ordinaire.
3. La destination dans la configuration `elevators.destinations` (fin de
   `world.json`) : `"motor": "auto"` (le moteur de la gaine générée, à la
   hauteur que donne le nombre d'étages ; `"shaft"` : laquelle), `from` = le
   niveau précédent. La sortie : `"mode": "surface"`, `"from"` le dernier
   niveau et `"fromShaft": "surface"` (proposée par cette gaine seulement).

Voir `demos/voxelia/mods/elevators/README.md` pour le format complet des
destinations et le transfert invisible.
