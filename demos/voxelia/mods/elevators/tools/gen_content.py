#!/usr/bin/env python3
"""Complète blocks.json et items.json du mod « elevators » (idempotent).

Ajoute, à partir des définitions existantes du style « modern » :
  - les tags de détection « cabin_floor » / « cabin_ceiling » (planchers,
    plafonds et éclairages de cabine, tous styles) ;
  - le style « glass » (cabine, portes, paliers, afficheurs vitrés) et la
    paroi de gaine vitrée « shaft_wall_glass » ;
  - les portes doubles larges (2 × 2) de cabine et de palier, pour chaque style ;
  - le tableau électrique « switchboard » ;
  - les kits, objets et recettes correspondants.

Usage (depuis la racine du dépôt) :
    python3 demos/voxelia/mods/elevators/tools/gen_content.py
"""
import copy
import json
import pathlib

MOD = pathlib.Path(__file__).resolve().parent.parent
STYLES = ["modern", "industrial", "backrooms", "glass"]
STYLE_NAMES = {"modern": "moderne", "industrial": "industrielle", "backrooms": "des backrooms", "glass": "en verre"}
HEADER = ("// Mod « elevators » : blocs (cabine, paliers, machinerie), objets (kits) et recettes.\n"
          "// Généré par docs : voir README.md (« Ajouter un style ») ; complété par tools/gen_content.py.\n")


def load(name):
    text = (MOD / name).read_text(encoding="utf-8")
    body = "\n".join(line for line in text.split("\n") if not line.lstrip().startswith("//"))
    return json.loads(body)


def save(name, data):
    (MOD / name).write_text(HEADER + json.dumps(data, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")


def generated(entry_id):
    """Entrées produites par ce script (retirées puis refaites à chaque passage)."""
    return (entry_id.startswith("glass_") or "_door_wide" in entry_id or entry_id.startswith("switchboard")
            or entry_id in ("shaft_wall_glass",))


def restyle(node, old, new):
    """Copie profonde où « old » devient « new » dans les ids, références et textures."""
    if isinstance(node, dict):
        return {k: restyle(v, old, new) for k, v in node.items()}
    if isinstance(node, list):
        return [restyle(v, old, new) for v in node]
    if isinstance(node, str):
        return (node.replace(f"elevator_{old}_", f"elevator_{new}_").replace(f"elevator_{old}", f"elevator_{new}")
                .replace(f"{old}_", f"{new}_"))
    return node


def add_tag(block, tag):
    tags = block.setdefault("tags", [])
    if tag not in tags:
        tags.append(tag)


# ── Portes ────────────────────────────────────────────────────────────────
# Une porte double occupe 2 × 2 cellules : le battant gauche (bloc principal,
# en bas, puis son haut) et le droit (seconde moitié, à +x avant rotation).
# Chaque battant glisse vers l'extérieur ; nodebox en coordonnées centrées.
T = 0.06
LEAF = {
    "left": {"closed": [-0.5, 0.5], "half": [-0.5, 0.02], "open": [-0.5, -0.42]},
    "right": {"closed": [-0.5, 0.5], "half": [-0.02, 0.5], "open": [0.42, 0.5]},
}
WIDE_PARTS = [("_top", [0, 1, 0]), ("_right", [1, 0, 0]), ("_right_top", [1, 1, 0])]
PART_NAMES = {"": "", "_top": " (haut)", "_right": " (battant droit)", "_right_top": " (battant droit, haut)"}


def leaf_box(hand, state):
    x0, x1 = LEAF[hand][state]
    return [[x0, -0.5, -T, x1, 0.5, T]]


def wide_door(style, kind, single):
    """Porte double de cabine (kind = « cabin ») ou de palier (« landing »), d'après la porte simple."""
    base = f"{style}_{kind}_door_wide"
    graphics = copy.deepcopy(single["graphics"])
    hard = single.get("gameplay", {}).get("hardness", 4)
    tags = [t for t in single.get("tags", []) if t != "elevator_door"] + ["elevator_door", "elevator_door_wide"]
    who = "de cabine" if kind == "cabin" else "palière"
    out = []

    def cell(suffix, hand, part_of=None):
        g = copy.deepcopy(graphics)
        g["texture"] = f"elevators:elevator_{style}_door_wide_{hand}"
        d = {
            "id": base + suffix,
            "displayName": f"Porte {who} double {STYLE_NAMES[style]}" + PART_NAMES[suffix],
            "type": "nodebox",
            "tags": tags,
            "sounds": single.get("sounds", "core:metal"),
            "graphics": g,
            "gameplay": {"hardness": hard, "tool": "pickaxe"},
            "defaultState": "closed",
            "states": {},
        }
        if suffix == "":
            d["description"] = ("Porte coulissante 2 × 2 pour grande cabine (gaine 7 × 7 ou 9 × 9) : deux battants qui s'effacent "
                                "vers les côtés. Sa porte " + ("palière" if kind == "cabin" else "de cabine") +
                                " doit lui faire face, à la même place.")
        for state in ("closed", "half", "open"):
            st = {"nodebox": leaf_box(hand, state), "physics": {"solid": state != "open"}}
            if suffix == "":
                st["parts"] = [{"offset": off, "block": base + suf + ("" if state == "closed" else f"_{state}")}
                               for suf, off in WIDE_PARTS]
            d["states"][state] = st
        if part_of is not None:
            d["partOf"] = part_of
            d["item"] = False
            d["gameplay"]["drops"] = ""
        out.append(d)

    cell("", "left")
    cell("_top", "left", [0, -1, 0])
    cell("_right", "right", [-1, 0, 0])
    cell("_right_top", "right", [-1, -1, 0])
    return out


# ── Style « glass » ─────────────────────────────────────────────────────
GLASS_NAMES = {
    "glass_cabin_floor": "Plancher de cabine vitrée",
    "glass_cabin_wall": "Paroi de cabine en verre",
    "glass_cabin_ceiling": "Plafond de cabine vitrée",
    "glass_cabin_lamp": "Éclairage de cabine vitrée",
    "glass_cabin_panel": "Panneau de commande (verre)",
    "glass_display": "Afficheur d'étage (verre)",
    "glass_arrow": "Indicateur de sens (verre)",
    "glass_cabin_door": "Porte de cabine en verre",
    "glass_cabin_door_top": "Porte de cabine en verre (haut)",
    "glass_landing_door": "Porte palière en verre",
    "glass_landing_door_top": "Porte palière en verre (haut)",
    "glass_landing_door_glass": "Porte palière tout verre",
    "glass_landing_door_glass_top": "Porte palière tout verre (haut)",
    "glass_frame": "Encadrement de porte palière (verre)",
    "glass_call": "Bouton d'appel (verre)",
}
GLASS_TRANSLUCENT = {"glass_cabin_wall", "glass_cabin_door", "glass_cabin_door_top", "glass_landing_door",
                     "glass_landing_door_top", "glass_landing_door_glass", "glass_landing_door_glass_top"}


def glass_style(blocks):
    out = []
    for b in blocks:
        if not b.get("id", "").startswith("modern_") or b.get("type") == "texture":
            continue
        g = restyle(b, "modern", "glass")
        g["displayName"] = GLASS_NAMES.get(g["id"], g["displayName"].replace("moderne", "en verre"))
        g["tags"] = [("elevator_glass" if t == "elevator_modern" else t) for t in g.get("tags", [])]
        if g["id"] in GLASS_TRANSLUCENT:
            g["graphics"]["material"] = "TRANSLUCENT"
            g["sounds"] = "core:glass"
        if g["id"] == "glass_cabin_wall":
            g["description"] = "Paroi de cabine transparente : on voit défiler la gaine (vitrée elle aussi : « Paroi de gaine vitrée »)."
        if g["id"] == "glass_cabin_lamp" and "light" in g:
            g["light"]["color"] = [215, 240, 255]
        out.append(g)
    return out


def switchboard():
    def look(front):
        return {"textures": {"south": f"elevators:{front}", "side": "elevators:elevator_switchboard_side",
                             "top": "elevators:elevator_switchboard_side", "bottom": "elevators:elevator_switchboard_side"}}
    return {
        "id": "switchboard",
        "displayName": "Tableau électrique d'ascenseur",
        "description": ("Près de la gaine ou du moteur (8 blocs au plus). Clic droit : diagnostic de l'installation, "
                        "numérotation des étages, essais de montée et de descente. Voyant vert : en ordre ; rouge : défaillance."),
        "tags": ["elevator", "elevator_switchboard"],
        "sounds": "core:metal",
        "graphics": look("elevator_switchboard"),
        "gameplay": {"hardness": 3, "tool": "pickaxe"},
        "defaultState": "idle",
        "states": {
            "idle": {},
            "ok": {"graphics": look("elevator_switchboard_ok"),
                   "light": {"type": "point", "color": [90, 255, 140], "intensity": 0.35, "range": 3}},
            "fault": {"graphics": look("elevator_switchboard_fault"),
                      "light": {"type": "point", "color": [255, 70, 50], "intensity": 0.45, "range": 3}},
        },
    }


def shaft_wall_glass():
    return {
        "id": "shaft_wall_glass",
        "displayName": "Paroi de gaine vitrée",
        "description": "Mur de gaine transparent (ossature métallique) : la cabine se voit monter et descendre.",
        "tags": ["elevator"],
        "sounds": "core:glass",
        "graphics": {"texture": "elevators:elevator_shaft_glass", "material": "TRANSLUCENT"},
        "gameplay": {"hardness": 3, "tool": "pickaxe"},
    }


def build_blocks():
    blocks = [b for b in load("blocks.json") if not generated(b.get("id", ""))]
    for b in blocks:
        bid = b.get("id", "")
        if bid.endswith("_cabin_floor"):
            add_tag(b, "cabin_floor")
        if bid.endswith("_cabin_ceiling") or bid.endswith("_cabin_lamp"):
            add_tag(b, "cabin_ceiling")
    glass = glass_style(blocks)
    for g in glass:
        if g["id"].endswith("_cabin_floor"):
            add_tag(g, "cabin_floor")
        if g["id"].endswith("_cabin_ceiling") or g["id"].endswith("_cabin_lamp"):
            add_tag(g, "cabin_ceiling")
    # Les blocs machinerie (moteur, rails…) d'abord, puis tout le neuf.
    by_id = {b["id"]: b for b in blocks + glass if "id" in b}
    extra = glass + [shaft_wall_glass()]
    for style in STYLES:
        for kind in ("cabin", "landing"):
            extra += wide_door(style, kind, by_id[f"{style}_{kind}_door"])
    extra.append(switchboard())
    return blocks + extra


# ── Objets et recettes ──────────────────────────────────────────────────
def recipe(rid, pattern, key, count=1):
    return {"type": "recipe", "id": rid, "category": "machines", "pattern": pattern, "key": key,
            "result": {"item": rid, "count": count}}


def build_items():
    items = [i for i in load("items.json") if not generated(i.get("id", ""))]
    extra = [
        {"type": "item", "id": "glass_cabin_kit", "displayName": "Kit de cabine d'ascenseur en verre",
         "icon": "elevators:elevator_glass_panel", "maxStack": 4, "tags": ["elevator_kit"],
         "description": "Clic droit sur le fond de la gaine : pose une cabine vitrée à la taille de la gaine (5 × 5 à 9 × 9)."},
        {"type": "item", "id": "glass_landing_kit", "displayName": "Kit de palier d'ascenseur en verre",
         "icon": "elevators:elevator_glass_door", "maxStack": 16, "tags": ["elevator_kit"],
         "description": "Clic droit sur le mur de la gaine (depuis le palier) : encadrement, porte, bouton, afficheur."},
        {"type": "item", "id": "glass_landing_kit_glass", "displayName": "Kit de palier tout verre",
         "icon": "elevators:elevator_glass_door_glass", "maxStack": 16, "tags": ["elevator_kit"],
         "description": "Comme le kit de palier en verre, porte entièrement vitrée."},
        recipe("glass_cabin_kit", ["GGG", "GLG", "III"], {"G": "core:glass", "L": "core:glowstone", "I": "core:iron_ingot"}),
        recipe("glass_landing_kit", ["IG", "IG", "IG"], {"I": "core:iron_ingot", "G": "core:glass"}, 2),
        recipe("glass_landing_kit_glass", ["GG", "GG", "IG"], {"I": "core:iron_ingot", "G": "core:glass"}, 2),
        recipe("shaft_wall_glass", ["IGI", "GGG", "IGI"], {"I": "core:iron_ingot", "G": "core:glass"}, 8),
        recipe("switchboard", ["IRI", "RGR", "ICI"],
               {"I": "core:iron_ingot", "R": "core:redstone", "G": "core:glass", "C": "core:gold_ingot"}),
    ]
    # Porte double : deux portes simples du même style, côte à côte.
    for style in STYLES:
        for kind in ("cabin", "landing"):
            extra.append(recipe(f"{style}_{kind}_door_wide", ["DD"], {"D": f"{style}_{kind}_door"}, 1))
    return items + extra


if __name__ == "__main__":
    save("blocks.json", build_blocks())
    save("items.json", build_items())
    print("blocks.json, items.json : à jour")
