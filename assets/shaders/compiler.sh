#!/bin/bash
# ============================================================
#  compiler.sh
#  Compile tous les shaders GLSL de shaders/src/ vers les formats
#  attendus par SDL_GPU :
#    - SPIR-V (.spv)   via glslc (ou glslangValidator en repli)  — Vulkan
#    - MSL    (.msl)   via spirv-cross depuis le SPIR-V          — Metal
#    - DXIL   (.dxil)  via spirv-cross (HLSL) puis dxc            — Direct3D 12
#
#  SPIR-V et MSL sont toujours produits si spirv-cross est disponible.
#  DXIL nécessite en plus `dxc` (DirectX Shader Compiler) — s'il est
#  absent, cette étape est ignorée avec un avertissement plutôt que de
#  faire échouer la compilation (voir README, section Shaders).
#
#  Sortie : assets/shaders/bin/<nom>.{vert,frag}.{spv,msl,dxil}
# ============================================================

set -e

SRC_DIR="$(dirname "$0")/src"
OUT_DIR="$(dirname "$0")/bin"

mkdir -p "$OUT_DIR"

# --- SPIR-V : glslc (préféré) ou glslangValidator (repli) -----------------

if command -v glslc &>/dev/null; then
    SPIRV_COMPILER="glslc"
    COMPILE_SPIRV() { glslc -O "$1" -o "$2"; }
elif command -v glslangValidator &>/dev/null; then
    SPIRV_COMPILER="glslangValidator"
    COMPILE_SPIRV() { glslangValidator -V "$1" -o "$2"; }
else
    echo "ERREUR : ni glslc ni glslangValidator trouvés dans le PATH."
    echo "Installez shaderc (https://github.com/google/shaderc) ou"
    echo "glslang (https://github.com/KhronosGroup/glslang)."
    exit 1
fi

echo "Compilateur SPIR-V : $SPIRV_COMPILER"

HAVE_SPIRV_CROSS=0
if command -v spirv-cross &>/dev/null; then
    HAVE_SPIRV_CROSS=1
    echo "Cross-compilation MSL/HLSL : spirv-cross disponible"
else
    echo "AVERTISSEMENT : spirv-cross introuvable — MSL/DXIL non générés (SPIR-V seul)."
fi

HAVE_DXC=0
if command -v dxc &>/dev/null; then
    HAVE_DXC=1
    echo "Compilation DXIL : dxc disponible"
else
    echo "AVERTISSEMENT : dxc introuvable — étape DXIL ignorée (voir README, section Shaders)."
fi

COMPILED=0
FAILED=0

for shader in "$SRC_DIR"/*.{vert,frag,comp}; do
    [ -f "$shader" ] || continue
    base=$(basename "$shader")
    ext="${base##*.}"

    spv_out="$OUT_DIR/$base.spv"
    echo "  Compilation $base -> SPIR-V ..."

    if ! COMPILE_SPIRV "$shader" "$spv_out"; then
        echo "    ÉCHEC (SPIR-V) : $base"
        ((FAILED++)) || true
        continue
    fi
    echo "    -> $spv_out"
    ((COMPILED++)) || true

    if [ "$HAVE_SPIRV_CROSS" -eq 1 ]; then
        msl_out="$OUT_DIR/$base.msl"
        if spirv-cross "$spv_out" --msl --msl-version 20300 --output "$msl_out" 2>/dev/null; then
            echo "    -> $msl_out"
        else
            echo "    ÉCHEC (MSL) : $base — ignoré"
        fi

        if [ "$HAVE_DXC" -eq 1 ]; then
            hlsl_out="$OUT_DIR/$base.hlsl"
            dxil_out="$OUT_DIR/$base.dxil"

            if spirv-cross "$spv_out" --hlsl --shader-model 60 --output "$hlsl_out" 2>/dev/null; then
                case "$ext" in
                    vert) profile="vs_6_0" ;;
                    frag) profile="ps_6_0" ;;
                    comp) profile="cs_6_0" ;;
                    *) profile="" ;;
                esac

                if [ -n "$profile" ] && dxc -T "$profile" -E main "$hlsl_out" -Fo "$dxil_out" &>/dev/null; then
                    echo "    -> $dxil_out"
                else
                    echo "    ÉCHEC (DXIL) : $base — ignoré"
                fi

                rm -f "$hlsl_out"
            else
                echo "    ÉCHEC (HLSL intermédiaire) : $base — DXIL ignoré"
            fi
        fi
    fi
done

echo ""
echo "Terminé. Compilés (SPIR-V) : $COMPILED   Échoués : $FAILED"
[ "$FAILED" -eq 0 ] || exit 1
