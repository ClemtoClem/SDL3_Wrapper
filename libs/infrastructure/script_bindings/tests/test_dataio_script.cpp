#include <script/script.hpp>
#include <script_bindings/script_dataio.hpp>

static String = R"SCRIPT(

-- Round-trip texte → arbre → texte, sous namespace `dataio`
let cfg = dataio.parse("yaml", "nom: Éric\nâge: 42\n")
print(cfg.nom, cfg.âge)             -- Éric 42

-- Poignée Document : plus efficace pour répéter des opérations
let yml = dataio.document("yaml")
if yml.decode("a: 1\nb: 2\n") {
    yml.root.a = 10                  -- mutation sur la valeur (NodeFromValue)
    print(yml.encode())              -- "a: 10\nb: 2\n"
} else {
    print("erreur :", yml.error)
}
yml.save("out.yaml")

-- Clonage profond (utile avant mutation en place)
let original = dataio.parse("json", '{"liste": [1, 2, 3]}')
let copie = dataio.clone(original)
copie.liste[0] = 999
print(original.liste[0])             -- 1 (non modifié)
print(copie.liste[0])                -- 999

-- Lire un glTF et extraire sa géométrie
let g = dataio.gltf.load("modèle.gltf")
if g {
    print(g.version, g.generator)
    for (m in g.meshes) {
        for (p in m.primitives) {
            let pos = g.read_floats(p.attributes.POSITION)
            let idx = g.read_indices(p.indices)
            print(len(pos) / 3, "sommets,", len(idx) / 3, "triangles")
        }
    }
}

-- Exporter un maillage (par exemple depuis g, ou un cube codé à la main)
dataio.mesh.save("cube.obj", {
    positions = [
        -0.5,-0.5,-0.5,  0.5,-0.5,-0.5,  0.5,0.5,-0.5, -0.5,0.5,-0.5,
        -0.5,-0.5, 0.5,  0.5,-0.5, 0.5,  0.5,0.5, 0.5, -0.5,0.5, 0.5,
    ],
    indices = [
        0,1,2, 0,2,3, 4,6,5, 4,7,6,
        0,4,5, 0,5,1, 1,5,6, 1,6,2,
        2,6,7, 2,7,3, 3,7,4, 3,4,0,
    ],
    colors = [
        1,0,0, 1,0,0, 1,0,0, 1,0,0,
        0,1,0, 0,1,0, 0,1,0, 0,1,0,
    ],
    comment = "cube bicolore",
})

-- Énumération
print(dataio.formats())              -- [json, yaml, toml, xml, html, ini, csv, css]
)SCRIPT";