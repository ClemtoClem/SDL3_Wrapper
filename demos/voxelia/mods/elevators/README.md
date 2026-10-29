# Mod « elevators » — ascenseurs réalistes

Ascenseurs constructibles : gaine, moteur, cabine qui se déplace réellement
(entité mobile qui emporte ses passagers), portes coulissantes de cabine et de
palier, boutons d'appel, panneau de cabine, afficheurs d'étage et de sens,
sons synchronisés, sauvegarde et reprise après arrêt du jeu. Indépendant : il
fonctionne seul ; d'autres mods (comme `backrooms`) y ajoutent des
destinations dans d'autres dimensions par simple déclaration JSON.

## Installation

Copier le dossier `elevators/` dans `demos/voxelia/mods/` (dépendance :
`core`). Il est activé avec les autres mods (écran « Mods »). Réglages
(écran du mod) : vitesse, accélération, durée d'ouverture des portes, durée
d'un mouvement de porte, hauteur maximale d'une gaine, configuration réservée
au constructeur.

## Construire un ascenseur

Commande d'exemple : `/ascenseur demo [modern|industrial|backrooms] [2..6]`
bâtit à côté de vous un ascenseur complet de N étages.

À la main :

1. **La gaine** : un puits vertical de **7 × 7** blocs (intérieur **5 × 5**),
   murs pleins (`Paroi de gaine`, ou n'importe quel bloc ; `Paroi de gaine à
   rail` pour les rails de guidage). Prévoir sous l'étage le plus bas une
   **fosse** et au-dessus du plus haut une **réserve** (6 blocs chacune) si
   l'ascenseur doit mener vers une autre dimension.
2. **La cabine** : *kit de cabine* (clic droit sur le fond de la gaine, centré :
   la porte est tournée vers vous). Elle occupe 5 × 4 × 5 : plancher, parois
   (avec la porte au milieu d'une paroi et le panneau à sa droite), plafond,
   éclairage au centre, afficheur au-dessus de la porte. On peut aussi la poser
   bloc par bloc (même disposition).
3. **Les paliers** : à chaque étage, *kit de palier* (ou *kit de palier vitré*)
   en clic droit sur le mur de la gaine **depuis l'étage**, du côté de la porte
   de cabine, au centre du mur. Il pose l'encadrement, la porte palière (2 de
   haut), le bouton d'appel (à droite), l'afficheur d'étage (au-dessus) et
   l'indicateur de sens. Le plancher de la cabine à cet arrêt sera le bloc sous
   le seuil de la porte.
4. **Le moteur**, au-dessus du centre de la gaine (plus haut que le plafond de
   la cabine au dernier étage). **Clic droit sur le moteur** : le mod vérifie
   tout et dit ce qui manque (cabine, porte de cabine, paliers, obstacle dans la
   gaine à telle hauteur, moteur trop bas…), puis met l'ascenseur en service et
   ouvre sa configuration.

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

## Configurer (clic droit sur le moteur)

Pour chaque arrêt : un **nom** (« Atelier ») et le **numéro affiché** (0 à 19,
`--`, sinon `??`) ; la **durée d'ouverture** des portes ; la **vitesse** ; les
**destinations extérieures** proposées (cases à cocher) ; **maintenance**
(hors service). Avec le réglage « configuration réservée au constructeur »,
seul celui qui l'a configuré la première fois (ou le mode créatif) peut la
modifier. Tout est enregistré avec le monde.

Régler les durées : réglages du mod (vitesse de croisière, accélération,
portes) ou, par ascenseur, la configuration du moteur. Un trajet suit un profil
trapézoïdal : accélération, croisière, décélération, arrêt précis à l'étage.

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
| `el_scan(x, y, z)` | vérification seule : `{record, problems}` |
| `el_request(e, i)` | demande l'arrêt d'indice `i` (appel ou panneau) |
| `el_request_external(e, id)` | part vers une destination extérieure |
| `el_external_for(e)` | destinations proposées par `e` |
| `el_build_cabin(coin, côté, style)` | pose une cabine (blocs) |
| `el_place_landing_cells(x, y, z, côté, style, vitré)` | pose un palier |
| `el_set_state(e, état)` | transition (refusée si non permise) |
| `EL.records`, `EL.settings` | ascenseurs de la dimension, réglages |

Côté moteur de jeu (C++), utilisés par le mod : `world.spawn_structure`
(assemblage de blocs mobile qui emporte ses passagers, composant `Carrier`),
`world.structure_set` (une cellule d'un assemblage en marche : l'afficheur
de la cabine), `game.ambient` sur canal (grondement continu).

## Textures et sons

- `textures/` : générées (procédurales) pour les trois styles —
  `elevator_<style>_{wall,floor,ceiling,lamp,door,door_glass,frame,panel,call,call_lit}`,
  afficheurs à 7 segments `elevator_<style>_display_<0..19|dash|unknown|error>`,
  sens `elevator_<style>_arrow_<up|down|none>`, machinerie commune
  (`elevator_{rail,cable,pulley,counterweight,motor,motor_top,shaft}`).
  Ajouter un style : ses textures sous ces noms, puis son nom dans `EL.STYLES`
  (init.script) et dans le générateur de `blocks.json` (README du dépôt,
  docs/history/project_voxelia_elevators.md).
- `sounds/` : synthétisés — grondement en boucle (normal, étouffé pour les
  backrooms), carillon d'arrivée (normal, désaccordé), tic d'afficheur,
  verrouillage des portes, arrêt, grincements de câbles. Le mod emploie aussi
  ceux du mod core : bouton, portes industrielles, démarrage hydraulique.

## Diagnostic

- « aucune cabine sous le moteur » : moteur pas centré sur la gaine, ou
  cabine trop bas (plus de `max_height` blocs).
- « la cabine n'a pas de porte » : porte de cabine absente ou pas au milieu
  d'une paroi.
- « obstacle dans la gaine à y = … » : un bloc dans l'emprise 5 × 5 entre deux
  étages (rails à mettre dans le mur, pas dans la gaine).
- « la cabine n'est à la hauteur d'aucun palier » : le plancher de cabine doit
  être un bloc sous le seuil de la porte palière.
- « hors service » : maintenance cochée, ou un composant cassé (clic droit sur
  le moteur pour le détail).
- `/ascenseur liste`, `/ascenseur verifier`.
