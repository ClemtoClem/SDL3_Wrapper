#!/usr/bin/env python3
"""Génère procedural_sky.hdr : ciel procédural équirectangulaire 256 × 128 au
format Radiance RGBE (scanlines plates, orientation « -Y 128 +X 256 »), lu par
render3d::LoadHdrEquirectangular.

Contenu (valeurs linéaires, HDR) :
  - dégradé du zénith (bleu) à l'horizon (blanc bleuté) ;
  - soleil (disque à ~40, halo) dans la direction opposée à la lumière
    directionnelle de examples/render3d_shadows_ibl_showcase ;
  - sol brun-gris sous l'horizon, assombri vers le nadir.

Usage, depuis la racine du dépôt :
  python3 assets/textures/equirectangularmaps/generate_procedural_sky.py
"""
import math
import os
import struct

WIDTH, HEIGHT = 256, 128
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "procedural_sky.hdr")

# sun.direction = {0.3, -0.7, 0.4} dans l'exemple : le soleil est à l'opposé.
_sx, _sy, _sz = -0.3, 0.7, -0.4
_n = math.sqrt(_sx * _sx + _sy * _sy + _sz * _sz)
SUN = (_sx / _n, _sy / _n, _sz / _n)


def mix(a, b, t):
	return tuple(x + (y - x) * t for x, y in zip(a, b))


def radiance(dx, dy, dz):
	if dy >= 0.0:
		# Ciel : dégradé vertical, horizon plus clair.
		t = math.pow(1.0 - dy, 3.0)
		col = mix((0.18, 0.36, 0.85), (0.85, 0.90, 1.00), t)
		col = tuple(c * 1.2 for c in col)
	else:
		# Sol : brun-gris, plus sombre vers le nadir.
		t = min(1.0, -dy * 4.0)
		col = mix((0.35, 0.33, 0.30), (0.10, 0.09, 0.08), t)
	# Soleil : disque très lumineux et halo doux (n'éclaire que le ciel visible).
	cos_a = dx * SUN[0] + dy * SUN[1] + dz * SUN[2]
	if cos_a > 0.0:
		disc = 40.0 if cos_a > 0.9995 else 0.0
		halo = 2.0 * math.pow(cos_a, 256.0) + 0.3 * math.pow(cos_a, 16.0)
		col = (col[0] + disc + halo * 1.00, col[1] + disc + halo * 0.92, col[2] + disc + halo * 0.75)
	return col


def rgbe(r, g, b):
	v = max(r, g, b)
	if v < 1e-32:
		return b"\x00\x00\x00\x00"
	mantissa, exponent = math.frexp(v)
	scale = mantissa * 256.0 / v
	return struct.pack("BBBB", int(r * scale), int(g * scale), int(b * scale), exponent + 128)


def main():
	data = bytearray()
	data += b"#?RADIANCE\n# procedural sky - generate_procedural_sky.py\nFORMAT=32-bit_rle_rgbe\n\n"
	data += f"-Y {HEIGHT} +X {WIDTH}\n".encode("ascii")
	for y in range(HEIGHT):
		lat = math.pi * (0.5 - (y + 0.5) / HEIGHT)  # +π/2 en haut (zénith)
		for x in range(WIDTH):
			lon = 2.0 * math.pi * ((x + 0.5) / WIDTH - 0.5)
			dx = math.cos(lat) * math.sin(lon)
			dy = math.sin(lat)
			dz = math.cos(lat) * math.cos(lon)
			data += rgbe(*radiance(dx, dy, dz))
	with open(OUT, "wb") as f:
		f.write(data)
	print(f"{OUT} : {WIDTH}x{HEIGHT}, {len(data)} octets")


if __name__ == "__main__":
	main()
