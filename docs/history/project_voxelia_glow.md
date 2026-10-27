---
name: project-voxelia-glow
description: "Blocs lumineux de voxelia (2026-10-27) : dessinés sans éclairage, pleine luminosité, et textures « lueur » au contraste rehaussé."
metadata:
  type: project
---

Constat (captures du 2026-10-27) : les sources de lumière (néons des
backrooms, torche) paraissaient ternes — leurs faces subissaient l'ombre du
ciel, l'occlusion ambiante et l'éclairage de la scène comme un bloc ordinaire.

- `ChunkMesh::emissive` : les blocs « light » > 0 (sauf translucides, et sauf
  `"graphics": {"glow": false}`) y vont, avec leur seule teinte (ni ombre du
  ciel ni occlusion). Voxelia les dessine avec un matériau NON éclairé
  (`m_emissive`) : pleine luminosité, de jour comme de nuit. Même matériau pour
  les entités et objets qui montrent un tel bloc (torche au sol, lave qui tombe).
- `TextureAtlas::Glow` : variante « lueur » des textures de ces blocs (et de
  leur texture qui coule) — luminance étirée entre ses 4e et 98e centiles, la
  teinte conservée, couleurs ravivées (×1,1), éclaircies (γ 0,75) ; première
  image des bandes animées. `LookupGlow` / `GlowId`.
- La porte d'ascenseur des backrooms éclaire sans briller (`glow: false`).
- Tests : `Atlas_LightBlocksGetABrighterHigherContrastTexture`,
  `Mesher_LightBlocksAreFullBrightInTheirOwnBuffer`.
