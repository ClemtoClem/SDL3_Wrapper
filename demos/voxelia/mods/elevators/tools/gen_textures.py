#!/usr/bin/env python3
"""Textures procédurales (64 × 64) ajoutées au mod « elevators » :

  - style « glass » : elevator_glass_{wall,floor,ceiling,lamp,door,door_glass,
    frame,panel,call,call_lit}, afficheurs elevator_glass_display_<0..19|dash|
    unknown|error>, sens elevator_glass_arrow_<up|down|none> ;
  - gaine vitrée : elevator_shaft_glass ;
  - portes doubles 2 × 2 (un battant par cellule) :
    elevator_<style>_door_wide_{left,right} pour modern, industrial, backrooms, glass ;
  - tableau électrique : elevator_switchboard{,_ok,_fault,_side}.

Les afficheurs, flèches, boutons et panneaux vitrés sont recolorés depuis le
style « modern » (mêmes segments) ; le reste est dessiné ici. Idempotent.

Usage (depuis la racine du dépôt) : python3 demos/voxelia/mods/elevators/tools/gen_textures.py
"""
import colorsys
import pathlib
import random

from PIL import Image, ImageDraw

TEX = pathlib.Path(__file__).resolve().parent.parent / "textures"
N = 64
STEEL = (168, 174, 182, 255)
STEEL_DARK = (96, 102, 110, 255)
STEEL_LIGHT = (214, 220, 226, 255)


def save(img, name):
    img.save(TEX / f"{name}.png")


def load(name):
    return Image.open(TEX / f"{name}.png").convert("RGBA")


def noise(img, amount, seed, box=None):
    """Grain léger (métal brossé, verre dépoli)."""
    rnd = random.Random(seed)
    px = img.load()
    x0, y0, x1, y1 = box or (0, 0, N, N)
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b, a = px[x, y]
            d = rnd.randint(-amount, amount)
            px[x, y] = (max(0, min(255, r + d)), max(0, min(255, g + d)), max(0, min(255, b + d)), a)


def glass_pane(img, box, tint=(196, 232, 246), alpha=70):
    """Vitre : teinte légère, reflets en diagonale."""
    x0, y0, x1, y1 = box
    size = (x1 - x0) + (y1 - y0)
    stripes = [(0.30, 0.07, 80), (0.42, 0.025, 60), (0.70, 0.04, 45)]  # (position, largeur, opacité)
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            t = ((x - x0) + (y1 - y)) / size
            r, g, b = tint
            a = alpha
            for pos, width, extra in stripes:
                if abs(t - pos) < width:
                    r, g, b, a = min(255, r + 45), min(255, g + 20), min(255, b + 9), min(255, a + extra)
            img.putpixel((x, y), (r, g, b, a))


def frame(img, width, color=STEEL, dark=STEEL_DARK):
    d = ImageDraw.Draw(img)
    for i in range(width):
        c = dark if i == 0 else color
        d.rectangle([i, i, N - 1 - i, N - 1 - i], outline=c)


def recolor(src, dst, hue_to=None, sat=None, light=1.0, keep_dark=True):
    """Recolore les pixels saturés (segments, flèches) vers une autre teinte."""
    img = load(src)
    px = img.load()
    for y in range(N):
        for x in range(N):
            r, g, b, a = px[x, y]
            h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
            if s > 0.25 and l > 0.08:
                if hue_to is not None:
                    h = hue_to
                if sat is not None:
                    s = min(1.0, s * sat)
                l = min(1.0, l * light)
                rr, gg, bb = colorsys.hls_to_rgb(h, l, s)
                px[x, y] = (int(rr * 255), int(gg * 255), int(bb * 255), a)
            elif not keep_dark:
                px[x, y] = (r, g, min(255, b + 8), a)
    save(img, dst)


# ── Style « glass » ─────────────────────────────────────────────────────
GLASS_HUE = 0.47  # vert d'eau : afficheurs et voyants des cabines vitrées


def glass_wall():
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    glass_pane(img, (3, 3, N - 4, N - 4))
    frame(img, 3)
    save(img, "elevator_glass_wall")


def glass_floor():
    img = Image.new("RGBA", (N, N), (38, 46, 52, 255))
    d = ImageDraw.Draw(img)
    for k in range(4):  # dalles de verre fumé
        x, y = (k % 2) * 32, (k // 2) * 32
        d.rectangle([x + 2, y + 2, x + 29, y + 29], fill=(52, 66, 74, 255), outline=(84, 104, 114, 255))
        d.line([x + 5, y + 26, x + 26, y + 5], fill=(78, 98, 108, 255), width=2)
    noise(img, 4, 3)
    frame(img, 2, STEEL_DARK, (60, 64, 70, 255))
    save(img, "elevator_glass_floor")


def glass_ceiling():
    img = Image.new("RGBA", (N, N), (230, 238, 242, 255))
    noise(img, 5, 4)
    d = ImageDraw.Draw(img)
    d.line([32, 2, 32, 61], fill=(200, 210, 216, 255))
    d.line([2, 32, 61, 32], fill=(200, 210, 216, 255))
    frame(img, 2, STEEL_LIGHT, STEEL)
    save(img, "elevator_glass_ceiling")


def glass_lamp():
    img = Image.new("RGBA", (N, N), (236, 248, 255, 255))
    d = ImageDraw.Draw(img)
    for y in range(8, N - 8, 8):
        d.line([6, y, N - 7, y], fill=(206, 230, 244, 255))
    frame(img, 4, STEEL_LIGHT, STEEL)
    save(img, "elevator_glass_lamp")


def glass_door(name, clear):
    """Porte vitrée à deux battants (porte simple) : seam au milieu."""
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    glass_pane(img, (4, 2, 30, N - 3), alpha=50 if clear else 80)
    glass_pane(img, (33, 2, N - 5, N - 3), alpha=50 if clear else 80)
    d = ImageDraw.Draw(img)
    width = 2 if clear else 4
    for i in range(width):
        d.rectangle([i, 0, N - 1 - i, N - 1], outline=STEEL if i else STEEL_DARK)
    d.rectangle([30, 0, 33, N - 1], fill=STEEL)
    d.line([31, 0, 31, N - 1], fill=STEEL_DARK)
    if not clear:
        d.rectangle([0, N - 8, N - 1, N - 1], fill=STEEL)  # plinthe
    save(img, name)


def glass_frame():
    img = Image.new("RGBA", (N, N), STEEL)
    for y in range(N):  # métal brossé horizontal
        c = 170 + (y * 7 % 11)
        for x in range(N):
            img.putpixel((x, y), (c, c + 6, c + 12, 255))
    noise(img, 3, 7)
    frame(img, 2, STEEL_LIGHT, STEEL_DARK)
    save(img, "elevator_glass_frame")


def glass_panel():
    img = load("elevator_modern_panel")
    px = img.load()
    for y in range(N):
        for x in range(N):
            r, g, b, a = px[x, y]
            if r > 120 and g > 120 and b > 120 and abs(r - b) < 30:  # tôle → verre fumé clair
                px[x, y] = (min(255, r - 10), min(255, g + 6), min(255, b + 14), a)
    save(img, "elevator_glass_panel")
    recolor("elevator_glass_panel", "elevator_glass_panel", hue_to=GLASS_HUE)


def glass_shaft():
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    glass_pane(img, (2, 2, N - 3, N - 3), tint=(180, 214, 226), alpha=60)
    d = ImageDraw.Draw(img)
    for i in range(3):
        d.rectangle([i, i, N - 1 - i, N - 1 - i], outline=(78, 84, 92, 255) if i == 0 else (120, 128, 136, 255))
    d.rectangle([0, 30, N - 1, 33], fill=(120, 128, 136, 255))  # traverse
    for x, y in [(5, 5), (N - 6, 5), (5, N - 6), (N - 6, N - 6), (5, 31), (N - 6, 31)]:
        d.ellipse([x - 1, y - 1, x + 1, y + 1], fill=(200, 206, 212, 255))
    save(img, "elevator_shaft_glass")


# ── Portes doubles (un battant par cellule) ─────────────────────────────
def wide_leaf(style):
    """Battant gauche : la moitié gauche de la porte simple du style, étirée, joint à droite."""
    src = load(f"elevator_{style}_door")
    leaf = src.crop((0, 0, N // 2 - 1, N)).resize((N, N), Image.NEAREST)
    d = ImageDraw.Draw(leaf)
    d.rectangle([N - 3, 0, N - 1, N - 1], fill=(24, 24, 26, 255) if style != "glass" else STEEL_DARK)  # joint central
    d.line([N - 4, 0, N - 4, N - 1], fill=(200, 204, 210, 200))  # arête lumineuse
    # Poignée encastrée verticale près du joint.
    d.rectangle([N - 10, N // 2 - 10, N - 8, N // 2 + 10], fill=(60, 62, 66, 255) if style != "glass" else STEEL)
    save(leaf, f"elevator_{style}_door_wide_left")
    save(leaf.transpose(Image.FLIP_LEFT_RIGHT), f"elevator_{style}_door_wide_right")


# ── Tableau électrique ──────────────────────────────────────────────────
def switchboard(state):
    img = Image.new("RGBA", (N, N), (120, 126, 120, 255))
    noise(img, 4, 11)
    d = ImageDraw.Draw(img)
    d.rectangle([3, 3, N - 4, N - 4], outline=(70, 74, 70, 255), width=2)  # porte de l'armoire
    d.rectangle([8, 8, N - 9, 18], fill=(34, 36, 38, 255))  # bandeau
    d.polygon([(12, 16), (16, 10), (15, 13), (19, 13), (14, 17), (15, 14)], fill=(250, 210, 40, 255))  # éclair
    d.rectangle([24, 11, 50, 15], fill=(200, 200, 190, 255))  # étiquette
    for row, y in enumerate((24, 40)):  # disjoncteurs
        d.rectangle([8, y, N - 9, y + 11], fill=(214, 216, 210, 255), outline=(90, 92, 90, 255))
        for k in range(6):
            x = 11 + k * 7
            d.rectangle([x, y + 2, x + 4, y + 9], fill=(40, 42, 44, 255))
            up = (k + row) % 3 != 0
            d.rectangle([x + 1, y + (3 if up else 6), x + 3, y + (5 if up else 8)], fill=(240, 240, 236, 255))
    lamps = {"idle": ((50, 70, 50), (70, 40, 36)), "ok": ((90, 255, 140), (70, 40, 36)), "fault": ((50, 70, 50), (255, 70, 50))}
    green, red = lamps[state]
    for (cx, color) in ((N - 22, green), (N - 13, red)):
        d.ellipse([cx - 3, 54, cx + 3, 60], fill=color + (255,), outline=(30, 30, 30, 255))
    d.rectangle([9, 55, 22, 58], fill=(40, 42, 44, 255))  # serrure
    suffix = "" if state == "idle" else f"_{state}"
    save(img, f"elevator_switchboard{suffix}")


def switchboard_side():
    img = Image.new("RGBA", (N, N), (112, 118, 112, 255))
    noise(img, 4, 12)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, N - 1, N - 1], outline=(70, 74, 70, 255), width=2)
    for y in range(14, 30, 4):  # ouïes d'aération
        d.line([16, y, N - 17, y], fill=(64, 68, 64, 255), width=2)
    for x, y in [(6, 6), (N - 7, 6), (6, N - 7), (N - 7, N - 7)]:
        d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(170, 174, 170, 255))
    save(img, "elevator_switchboard_side")


if __name__ == "__main__":
    glass_wall()
    glass_floor()
    glass_ceiling()
    glass_lamp()
    glass_door("elevator_glass_door", clear=False)
    glass_door("elevator_glass_door_glass", clear=True)
    glass_frame()
    glass_panel()
    glass_shaft()
    for name in ["call", "call_lit", "arrow_up", "arrow_down", "arrow_none", "display_dash", "display_unknown",
                 "display_error"] + [f"display_{i}" for i in range(20)]:
        recolor(f"elevator_modern_{name}", f"elevator_glass_{name}", hue_to=GLASS_HUE)
    for style in ["modern", "industrial", "backrooms", "glass"]:
        wide_leaf(style)
    for state in ("idle", "ok", "fault"):
        switchboard(state)
    switchboard_side()
    print("textures : à jour")
