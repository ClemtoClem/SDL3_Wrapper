# Mod « elevators » — ascenseurs réalistes

Ascenseurs construits bloc par bloc : gaine de 5 × 5 à 9 × 9, moteur, cabine
aux parois libres qui se déplace réellement
(entité mobile qui emporte ses passagers), portes coulissantes de cabine et de
palier, boutons d'appel, panneau de cabine, afficheurs d'étage et de sens,
sons synchronisés, sauvegarde et reprise après arrêt du jeu. Indépendant : il
fonctionne seul ; d'autres mods (comme `backrooms`) y ajoutent des
destinations dans d'autres dimensions par simple déclaration JSON.

## Installation

Copier le dossier `elevators/` dans `demos/voxelia/mods/` (dépendance :
`core`). Il est activé avec les autres mods (écran « Mods »). Réglages
(écran du mod) : vitesse, accélération, durée d'ouverture des portes, durée
d'un mouvement de porte, hauteur maximale entre la cabine et le moteur (30 par
défaut), plus petite et plus grande gaine acceptées (5 et 9), écart minimal
entre deux portes palières (4), hauteur des cabines posées automatiquement (4
ou 5), configuration réservée au constructeur. Un réglage modifié en cours de
partie s'applique aussitôt.

## Construire un ascenseur

Commande d'exemple : `/elevator build [modern|industrial|backrooms|glass] [étages]
[5|7|9] [4|5] [wide|multi]` (alias `/ascenseur demo …`) bâtit à côté de vous un
ascenseur complet : gaine de la taille donnée (7 par défaut), cabine de 4 de
haut (2 blocs libres) ou de 5 (3 blocs libres ; aussi `h4` / `h5`, sinon le
réglage `cabin_height`), `wide` : portes doubles 2 × 2, `multi` : une seconde
face de portes desservie un étage sur deux.

Afficheur des cabines posées automatiquement (commande, kit, gaines générées) :
au-dessus de la porte dans une cabine de 5 ; dans une cabine de 4, le haut de la
porte touche le plafond, il va donc sur une paroi latérale, au rang haut, contre
la paroi de la porte.

Rien n'est fixé : le mod lit ce qui est construit.

| Gaine (extérieur) | Intérieur = cabine | Place utile | Portes sur une paroi |
| --- | --- | --- | --- |
| 5 × 5 | 3 × 3 | 1 × 1 | 1 simple |
| 7 × 7 | 5 × 5 | 3 × 3 | jusqu'à 3 simples, ou 1 double + 1 simple |
| 9 × 9 | 7 × 7 | 5 × 5 | jusqu'à 5 simples, ou 2 doubles |

Le côté est **impair** (le moteur tombe au centre exact) ; les bornes se
règlent (`shaft_min`, `shaft_max`).

1. **La gaine** : un puits carré, murs pleins (`Paroi de gaine`, `Paroi de
   gaine vitrée`, ou n'importe quel bloc ; `Rail de guidage` pour le décor).
   Prévoir sous l'étage le plus bas une **fosse** et au-dessus du plus haut une
   **réserve** (6 blocs chacune) si l'ascenseur doit mener vers une autre
   dimension.
2. **La cabine** remplit l'intérieur de la gaine :
   - un **plancher** : tous ses blocs portent le tag `cabin_floor` (planchers
     de cabine des quatre styles, ou ceux d'un autre mod) ;
   - un **plafond** 3 à 7 blocs plus haut (cabine de 4 à 8 de haut) : tag
     `cabin_ceiling` (plafonds et éclairages de cabine) ; un afficheur peut y
     prendre place ;
   - des **parois libres** : n'importe quels blocs (verre, bois, cabine
     d'un autre style…) ;
   - une ou plusieurs **portes de cabine** (simples 1 × 2 ou doubles 2 × 2),
     juste au-dessus du plancher, sur n'importe quelle face, hors des angles ;
   - un **panneau de commande** et des **afficheurs**, où l'on veut dans la
     cabine (conseillés, pas obligatoires) : chaque afficheur indique l'étage,
     même posé, déplacé ou cassé après la mise en service.

   Le *kit de cabine* (clic droit sur le fond de la gaine) la pose d'un coup à
   la taille de la gaine, avec des portes en face des portes palières déjà
   posées à ce niveau (sinon une porte tournée vers vous).
3. **Les paliers** : à chaque étage, une ou plusieurs **portes palières** dans
   les murs de la gaine, sur n'importe quelle face. Le plancher de la cabine à
   cet arrêt est le bloc sous leur seuil. **4 blocs au moins** entre deux
   étages (`floor_gap`). Boutons d'appel, afficheurs et indicateurs de sens :
   dans les murs de la gaine, dans les 3 rangs au-dessus du plancher de
   l'étage. Le *kit de palier* pose porte, encadrement, bouton, afficheur et
   sens au point visé (clic droit sur le mur, depuis l'étage).
4. **Sécurité** : chaque porte palière d'un étage doit avoir une **porte de
   cabine alignée** (même face, même place, même largeur) ; sinon
   l'ascenseur reste hors service. À l'arrêt, seules les portes de cabine en
   face d'une porte palière de l'étage s'ouvrent.
5. **Le moteur**, au-dessus du centre de la gaine, à `max_height` blocs au plus
   au-dessus de la cabine, plus haut que le plafond de la cabine au dernier
   étage. Il n'a pas d'interface : tout passe par le tableau électrique.
6. **Le tableau électrique** : posé tout seul par la construction automatique
   (commande, gaines générées, kiosque) à l'étage le plus bas, dans le mur de
   la gaine, à droite de l'encadrement de la porte palière. À la main, on le
   pose à 8 blocs au plus de la gaine ou du moteur. « Mettre en service » :
   le mod vérifie tout et dit ce qui manque.

Contrepoids, poulies et câbles sont des blocs de décor pour la machinerie.

## Utiliser

- **Bouton d'appel** (palier) : la cabine vient ; ses portes s'ouvrent à
  l'arrêt seulement. Les appels successifs sont mis en file : en marche, les
  arrêts dans le sens du trajet d'abord (algorithme de l'ascenseur), sans
  doublon.
- **Panneau** (cabine) : un bouton par étage, les destinations extérieures,
  « Ouvrir » (garde les portes ouvertes plus longtemps), « Fermer ».
- **Portes** : clic droit sur une porte quand la cabine est là = appel. Une
  porte palière ne s'ouvre jamais sans la cabine ; quelqu'un dans
  l'embrasure : les portes se rouvrent.

## Tableau électrique (commande et configuration)

Le bloc `Tableau électrique d'ascenseur` est le seul poste de commande. Il
garde le moteur qu'il commande (données du monde) : si ce moteur disparaît,
il le dit. Clic droit : un formulaire avec

- un **rapport** (zone de texte) : moteur présent ou absent, installation
  (gaine, plancher, plafond, portes alignées, écart des étages, obstacles, murs
  percés), état de marche (panne, maintenance, portes palières ouvertes sans
  cabine, cabine hors de l'arrêt enregistré), puis un **verdict** : l'ascenseur
  peut-il fonctionner ? Voyant vert : en ordre ; rouge : défaillance ;
- **Diagnostic**, **Mettre en service** (ou remettre en service après une
  réparation), **Numérotation** (étages recherchés et numérotés de 0 en bas,
  noms gardés) ;
- la **commande** : ▼ Descendre / ▲ Monter (arrêt voisin), un bouton par
  arrêt, ◀▶ Ouvrir / ▶◀ Fermer les portes ;
- la **configuration** : pour chaque arrêt un nom (« Atelier ») et le numéro
  affiché (0 à 19, `--`, sinon `??`), la durée d'ouverture des portes, la
  vitesse, les destinations extérieures (cases à cocher), la maintenance (hors
  service) ; bouton **Enregistrer**. Avec le réglage « configuration réservée
  au constructeur », seul celui qui l'a configurée la première fois (ou le mode
  créatif) peut la modifier. Tout est enregistré avec le monde.

Un trajet suit un profil trapézoïdal : accélération, croisière,
décélération, arrêt précis à l'étage.

## Machine à états

`idle → waiting_departure → moving_up | moving_down → decelerating → stopping
→ arrived → doors_opening → doors_open → doors_closing → idle`, plus
`transfer` (changement de dimension), `maintenance`, `failed`. Les transitions
non prévues sont refusées (`el_set_state` rend faux) : impossible de partir
portes ouvertes, d'ouvrir une porte palière sans cabine.

## Destinations extérieures (intégration avec un autre mod)

Aucun code à appeler : un mod déclare ses destinations dans sa configuration
JSON, le mod « elevators » les lit (`content.config("elevators")`) :

```json
{"type": "config", "values": {"elevators": {"destinations": {
  "mon_mod_cave": {
    "label": "La cave",            "display": "-1",
    "dimension": "mon_mod:cave",   "mode": "elevator",
    "motor": [10, 18, 3],           "floor": 0,
    "direction": "down",            "style": "industrial",
    "from": ["core:overworld"]
  },
  "mon_mod_sortie": {
    "label": "Sortie", "dimension": "core:overworld", "mode": "surface",
    "direction": "up", "style": "modern", "from": ["mon_mod:cave"]
  }
}}}}
```

- `mode: "elevator"` : l'ascenseur d'arrivée, désigné par son moteur, existe
  dans la dimension cible (bâti ou généré). S'il n'a pas encore de cabine, le
  mod en pose une au palier le plus bas et le met en service. `"motor": "auto"` :
  la gaine que génère le labyrinthe de la dimension (`world.maze_shaft`), à la
  hauteur que donne son nombre d'étages ; `"shaft": "id"` choisit la gaine
  quand il y en a plusieurs.
- `"fromShaft": "id"` : la destination n'est proposée que par cette gaine
  générée de la dimension de départ (mise en service toute seule elle aussi),
  pas par les autres ascenseurs de la dimension.
- `mode: "surface"` : un kiosque d'ascenseur est bâti là où le joueur arrive
  (point d'apparition de la dimension) ; il reste, et permet de repartir.
- `from` : dimensions où la destination est proposée automatiquement (`"*"` :
  partout) ; sinon on la coche dans la configuration d'un ascenseur.
- À l'arrivée, l'ascenseur propose aussi **Retour** (là d'où l'on vient).

### Transfert invisible

Le trajet est un seul profil sur D1 + D2 : D1 dans la gaine de départ (la
place libre au-delà de la cabine, 6 blocs au plus), D2 (6 blocs) dans celle
d'arrivée. Quand la cabine a parcouru D1, sa copie est reposée à son étage de
départ, le joueur change de dimension à la même place **relative** dans une
cabine identique qui reprend le même profil au même instant (même vitesse), et
le grondement du moteur continue sans coupure (canal sonore propre). Portes
fermées, on ne voit que l'intérieur de la cabine pendant que la destination
se charge (le moteur de jeu la prépare autour du point d'arrivée avant de
rendre la main).

Limites : voxelia n'a qu'une dimension chargée à la fois et un joueur local ;
seul le joueur est transféré (les objets posés et les créatures dans la
cabine restent dans la dimension de départ). Le moteur affiche son message
« Vous entrez dans : … » au changement de dimension.

## Fonctions (API des scripts, globales)

| Fonction | Rôle |
| --- | --- |
| `el_register(x, y, z, quiet)` | vérifie et met en service l'ascenseur du moteur ; rend l'ascenseur ou `nil` |
| `el_scan(x, y, z)` | vérification seule : `{record, problems, warnings, info}` |
| `el_diagnose(moteur)` | rapport complet (tableau électrique) : `{text, ok}` |
| `el_renumber(moteur)` | étages recherchés et numérotés de 0 |
| `el_request(e, i)` | demande l'arrêt d'indice `i` (appel ou panneau) |
| `el_request_external(e, id)` | part vers une destination extérieure |
| `el_external_for(e)` | destinations proposées par `e` |
| `el_build_cabin(coin, style, half, h, portes)` | pose une cabine (blocs) ; `portes` : `[{side, u, width}]` |
| `el_place_landing_door(x, y, z, côté, style, largeur, vitré)` | pose un palier (porte simple ou double) |
| `el_place_door(x, y, z, côté, bloc)` | pose une porte fermée (2 ou 4 cellules) |
| `el_set_state(e, état)` | transition (refusée si non permise) |
| `EL.records`, `EL.settings` | ascenseurs de la dimension, réglages |

Un ascenseur garde sa géométrie : `half` (demi-côté de cabine : gaine
`2·half + 3`), `h` (hauteur de cabine), `cabin.doors` et, par étage,
`floors[i].doors`, `calls`, `displays`, `arrows` (voir `scripts/10_registry`).

Côté moteur de jeu (C++), utilisés par le mod : `world.spawn_structure`
(assemblage de blocs mobile qui emporte ses passagers, composant `Carrier`),
`world.structure_set` (une cellule d'un assemblage en marche : l'afficheur
de la cabine), `game.ambient` sur canal (grondement continu),
`world.maze_shaft` (moteur, fond `bottom`, côté `size` et paliers `side` d'une
gaine générée), champ `textarea` des formulaires (`game.open_ui`).

### Gaines générées (labyrinthes)

Dans un générateur « maze », `"elevator": {…, "size": 5 | 7 | 9}` choisit le
côté de la gaine (7 par défaut). Le mod en déduit la cabine (taille, portes en
face des paliers générés) et explore toute la hauteur de la gaine, quel que
soit le réglage `max_height`.

## Textures et sons

- `textures/` : générées (procédurales) pour les quatre styles —
  `elevator_<style>_{wall,floor,ceiling,lamp,door,door_glass,frame,panel,call,call_lit}`,
  portes doubles `elevator_<style>_door_wide_{left,right}` (un battant par
  cellule), afficheurs à 7 segments `elevator_<style>_display_<0..19|dash|unknown|error>`,
  sens `elevator_<style>_arrow_<up|down|none>`, machinerie commune
  (`elevator_{rail,cable,pulley,counterweight,motor,motor_top,shaft,shaft_glass}`),
  tableau électrique `elevator_switchboard{,_ok,_fault,_side}`.
  `tools/gen_textures.py` refait celles du style « glass », des portes
  doubles et du tableau ; `tools/gen_content.py` complète `blocks.json` et
  `items.json` (style « glass », portes doubles, tableau, tags). Ajouter un
  style : ses textures sous ces noms, son nom dans `EL.STYLES` (init.script) et
  dans `STYLES` de `tools/gen_content.py`.
- `sounds/` : synthétisés — grondement en boucle (normal, étouffé pour les
  backrooms), carillon d'arrivée (normal, désaccordé), tic d'afficheur,
  verrouillage des portes, arrêt, grincements de câbles. Le mod emploie aussi
  ceux du mod core : bouton, portes industrielles, démarrage hydraulique.

## Diagnostic

Le tableau électrique donne le rapport complet ; en bref :

- « aucun plancher de cabine » : moteur pas au-dessus de la gaine, ou cabine
  à plus de `max_height` blocs sous lui.
- « plancher de cabine décentré » : le moteur n'est pas au-dessus du centre
  exact de la gaine.
- « plancher / plafond de cabine incomplet » : un bloc sans le tag
  `cabin_floor` / `cabin_ceiling`.
- « la cabine n'a pas de porte », « porte … dans un angle » : une porte de
  cabine au-dessus du plancher, au milieu d'une paroi.
- « porte palière … sans porte de cabine alignée en face » : sécurité, voir
  « Construire un ascenseur ».
- « étages … trop proches » : `floor_gap` blocs au moins entre deux portes
  palières.
- « obstacle dans la gaine à y = … » : un bloc dans l'emprise de la cabine
  entre deux étages (rails à mettre dans le mur, pas dans la gaine).
- « hors service » : maintenance cochée, ou un composant cassé (« Mettre en
  service » au tableau, une fois réparé).
- `/elevator list`, `/elevator verify`.
