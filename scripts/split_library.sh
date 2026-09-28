#!/usr/bin/env bash
#
# split_library.sh — découpe une bibliothèque « header-only » en en-têtes
# (déclarations) + sources (définitions), avec splitter.py.
#
# La source n'est JAMAIS modifiée : elle est copiée dans la destination, où
# le découpage a lieu. Étapes :
#
#   1. copie de SOURCE/include (et SOURCE/src s'il existe) vers DEST ;
#   2. (--rejoin) refusion des paires .hpp/.cpp laissées par l'ancien script
#      (reconnues à leur `#include "nom.hpp"` sans chemin), pour repartir
#      d'en-têtes complets ;
#   3. test d'autonomie de chaque en-tête : un en-tête qui ne compile pas seul
#      (il compte sur ce que son module a inclus avant lui) reçoit, dans son
#      .cpp, l'en-tête du module qui le rend compilable (sdl3/sdl3.hpp…) ;
#   4. découpage en parallèle (splitter.py) ; un en-tête qui a déjà un .cpp
#      écrit à la main n'est pas touché ;
#   5. (--check) compilation de chaque .cpp en -O3 (les avertissements de
#      l'optimiseur n'apparaissent qu'optimisé), puis édition de liens partielle
#      de tous les objets pour détecter les symboles définis deux fois.
#
# Usage :
#   ./split_library.sh --source DIR [--dest DIR] [options]
#
#   --source DIR    bibliothèque d'origine (contient include/, et src/ éventuel)
#   --dest DIR      bibliothèque produite (défaut : lib)
#   --replace       si DEST existe : le renommer en DEST.avant-DATE, puis refaire
#   --rejoin        refusionner d'abord les paires produites par l'ancien script
#   --only MOTIF    ne découper que les en-têtes dont le chemin contient MOTIF
#                   (répétable ; les autres sont copiés tels quels)
#   --check         compiler le résultat (recommandé)
#   --jobs N        tâches en parallèle (défaut : nombre de cœurs)
#   --cxx CXX       compilateur (défaut : g++)
#   --cxxflags "…"  options de compilation (défaut : C++23, -Wextra -Werror,
#                   options pkg-config de SDL3 et shaderc)
#
# Exemples :
#   ./split_library.sh --source ../SDL3_Wrapper/lib --dest lib --replace --rejoin --check
#   ./split_library.sh --source ../SDL3_Wrapper/lib --dest /tmp/essai --only sdl3 --check
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SPLITTER="$SCRIPT_DIR/splitter.py"

SOURCE=""
DEST="lib"
REPLACE=0
REJOIN=0
CHECK=0
JOBS="$(nproc 2>/dev/null || echo 4)"
CXX="g++"
CXXFLAGS_SPLIT=""
declare -a ONLY=()

usage() { sed -n '2,/^set -uo/p' "${BASH_SOURCE[0]}" | sed -e '$d' -e 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
	case "$1" in
		--source) SOURCE="$2"; shift 2 ;;
		--dest) DEST="$2"; shift 2 ;;
		--replace) REPLACE=1; shift ;;
		--rejoin) REJOIN=1; shift ;;
		--only) ONLY+=("$2"); shift 2 ;;
		--check) CHECK=1; shift ;;
		--jobs) JOBS="$2"; shift 2 ;;
		--cxx) CXX="$2"; shift 2 ;;
		--cxxflags) CXXFLAGS_SPLIT="$2"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "Argument inconnu : $1 (voir --help)" >&2; exit 1 ;;
	esac
done

die() { echo "Erreur : $*" >&2; exit 1; }

[[ -n "$SOURCE" ]] || die "--source est obligatoire (voir --help)"
[[ -d "$SOURCE/include" ]] || die "$SOURCE/include introuvable"
[[ -f "$SPLITTER" ]] || die "splitter.py introuvable à côté de ce script"
command -v python3 >/dev/null || die "python3 introuvable"
command -v "$CXX" >/dev/null || die "compilateur $CXX introuvable"
SOURCE="$(cd "$SOURCE" && pwd)"

if [[ -z "$CXXFLAGS_SPLIT" ]]; then
	PKG_FLAGS="$(pkg-config --cflags sdl3 sdl3-image sdl3-ttf sdl3-mixer sdl3-net shaderc 2>/dev/null)"
	CXXFLAGS_SPLIT="-std=c++23 -Wextra -Werror -Wsign-compare $PKG_FLAGS"
fi

# ── Destination ──────────────────────────────────────────────────────────────

STAMP="$(date +%Y%m%d_%H%M%S)"
if [[ -e "$DEST" ]]; then
	[[ $REPLACE -eq 1 ]] || die "$DEST existe déjà (--replace pour le remplacer, l'ancien est conservé)"
	mv "$DEST" "$DEST.avant-$STAMP"
	echo "Ancienne destination conservée : $DEST.avant-$STAMP"
fi
mkdir -p "$DEST"
cp -a "$SOURCE/include" "$DEST/include"
if [[ -d "$SOURCE/src" ]]; then cp -a "$SOURCE/src" "$DEST/src"; else mkdir -p "$DEST/src"; fi
DEST="$(cd "$DEST" && pwd)"
INCLUDE="$DEST/include"
SRC="$DEST/src"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
LOG_DIR="$PWD/split_logs"
mkdir -p "$LOG_DIR"
REPORT="$LOG_DIR/rapport_$STAMP.txt"
: > "$REPORT"
log() { echo "$@" | tee -a "$REPORT"; }

log "=== split_library.sh — $STAMP ==="
log "Source      : $SOURCE (non modifiée)"
log "Destination : $DEST"
log "Compilateur : $CXX $CXXFLAGS_SPLIT"
log ""

# ── 2. Refusion des paires de l'ancien script ────────────────────────────────

if [[ $REJOIN -eq 1 ]]; then
	log "── Refusion des paires de l'ancien script"
	while IFS= read -r -d '' cpp; do
		rel="${cpp#"$SRC"/}"
		hpp="$INCLUDE/${rel%.cpp}.hpp"
		base="$(basename "$rel" .cpp)"
		first_include="$(grep -m1 '^#include' "$cpp")"
		if [[ -f "$hpp" && "$first_include" == "#include \"$base.hpp\"" ]]; then
			if out="$(python3 "$SPLITTER" "$hpp" --join "$cpp" 2>&1)"; then
				log "  refusionné : ${rel%.cpp}"
			else
				log "  ÉCHEC      : ${rel%.cpp}"
				log "$(echo "$out" | sed 's/^/      /')"
			fi
		fi
	done < <(find "$SRC" -name '*.cpp' -print0 | sort -z)
	log ""
fi

# ── 3. Liste des en-têtes à découper ─────────────────────────────────────────

selected() {
	local rel="$1"
	[[ ${#ONLY[@]} -eq 0 ]] && return 0
	for pattern in "${ONLY[@]}"; do
		[[ "$rel" == *"$pattern"* ]] && return 0
	done
	return 1
}

: > "$WORK/todo"
manual=()
while IFS= read -r -d '' hpp; do
	rel="${hpp#"$INCLUDE"/}"
	selected "$rel" || continue
	if [[ -f "$SRC/${rel%.hpp}.cpp" ]]; then
		manual+=("$rel")
		continue
	fi
	printf '%s\0' "$rel" >> "$WORK/todo"
done < <(find "$INCLUDE" -name '*.hpp' -print0 | sort -z)

# ── 3-4. Autonomie, puis découpage (en parallèle) ─────────────────────────────

# Compile `#include` de chaque en-tête donné, seul, dans un fichier vide.
compiles() {
	local includes=""
	for h in "$@"; do includes+="#include \"$h\""$'\n'; done
	# shellcheck disable=SC2086
	printf '%s' "$includes" | "$CXX" $CXXFLAGS_SPLIT -I"$INCLUDE" -fsyntax-only -x c++ - >/dev/null 2>&1
}

# En-têtes de module candidats pour un en-tête non autonome, du plus proche
# au plus large : dossier/dossier.hpp, parent/dossier.hpp, …, prelude.hpp.
module_headers() {
	local rel="$1" dir
	dir="$(dirname "$rel")"
	while [[ "$dir" != "." ]]; do
		local name parent
		name="$(basename "$dir")"
		parent="$(dirname "$dir")"
		[[ -f "$INCLUDE/$dir/$name.hpp" ]] && echo "$dir/$name.hpp"
		if [[ "$parent" == "." ]]; then
			[[ -f "$INCLUDE/$name.hpp" ]] && echo "$name.hpp"
		else
			[[ -f "$INCLUDE/$parent/$name.hpp" ]] && echo "$parent/$name.hpp"
		fi
		dir="$parent"
	done
	for top in prelude.hpp; do [[ -f "$INCLUDE/$top" ]] && echo "$top"; done
}

# Phase A (lecture seule) : autonomie de chaque en-tête ORIGINAL. Elle doit
# être finie avant tout découpage, qui réécrit les en-têtes.
check_autonomy() {
	local rel="$1" key
	key="$(echo "$1" | tr '/' '_')"
	: > "$WORK/prelude/$key"
	compiles "$rel" && return 0
	local candidate note="non autonome, et aucun en-tête de module ne suffit"
	while IFS= read -r candidate; do
		[[ "$candidate" == "$rel" ]] && continue
		if compiles "$candidate" "$rel"; then
			echo "$candidate" > "$WORK/prelude/$key"
			note="non autonome : son .cpp inclut $candidate"
			break
		fi
	done < <(module_headers "$rel" | awk '!seen[$0]++')
	echo "$note" > "$WORK/note/$key"
}

# Phase B : découpage.
process_header() {
	local rel="$1" key
	key="$(echo "$1" | tr '/' '_')"
	local -a prelude=()
	[[ -s "$WORK/prelude/$key" ]] && prelude=(--prelude "$(cat "$WORK/prelude/$key")")
	local out rc
	out="$(python3 "$SPLITTER" "$INCLUDE/$rel" --include-root "$INCLUDE" --src-root "$SRC" "${prelude[@]}" 2>&1)"
	rc=$?
	printf '%s\n%s\n' "$rc" "$out" > "$WORK/results/$key"
}
export -f check_autonomy process_header compiles module_headers
export CXX CXXFLAGS_SPLIT INCLUDE SRC SPLITTER WORK
mkdir -p "$WORK/results" "$WORK/prelude" "$WORK/note"

total="$(tr -cd '\0' < "$WORK/todo" | wc -c)"
log "── Autonomie des $total en-têtes à découper"
xargs -0 -P "$JOBS" -I{} bash -c 'check_autonomy "$1"' _ {} < "$WORK/todo"
log "── Découpage ($JOBS tâches en parallèle)"
xargs -0 -P "$JOBS" -I{} bash -c 'process_header "$1"' _ {} < "$WORK/todo"

split_count=0; unchanged=0; failed=0; moved_total=0; kept_total=0
declare -a NOT_SELF_CONTAINED=() FAILED=()
while IFS= read -r -d '' rel; do
	result="$WORK/results/$(echo "$rel" | tr '/' '_')"
	rc="$(sed -n 1p "$result")"
	line="$(sed -n 2p "$result")"
	note_file="$WORK/note/$(echo "$rel" | tr '/' '_')"
	[[ -s "$note_file" ]] && NOT_SELF_CONTAINED+=("$rel — $(cat "$note_file")")
	if [[ "$rc" != "0" ]]; then
		failed=$((failed + 1))
		FAILED+=("$line")
		log "  ÉCHEC $line"
		continue
	fi
	moved="$(echo "$line" | grep -oE '[0-9]+ déplacées' | grep -oE '^[0-9]+')"
	kept="$(echo "$line" | grep -oE '[0-9]+ gardées' | grep -oE '^[0-9]+')"
	moved_total=$((moved_total + ${moved:-0}))
	kept_total=$((kept_total + ${kept:-0}))
	if [[ "${moved:-0}" -gt 0 ]]; then split_count=$((split_count + 1)); else unchanged=$((unchanged + 1)); fi
	echo "  $line" >> "$REPORT"
done < "$WORK/todo"

log ""
log "=== Résumé ==="
log "En-têtes découpés          : $split_count"
log "En-têtes laissés tels quels : $unchanged (rien à déplacer)"
log "En-têtes avec un .cpp écrit à la main (non touchés) : ${#manual[@]}"
log "Échecs d'analyse           : $failed"
log "Fonctions déplacées        : $moved_total ($kept_total gardées en ligne dans les en-têtes)"
if [[ ${#manual[@]} -gt 0 ]]; then
	log ""
	log "Non touchés (déjà un .cpp) :"
	for f in "${manual[@]}"; do log "  - $f"; done
fi
if [[ ${#NOT_SELF_CONTAINED[@]} -gt 0 ]]; then
	log ""
	log "En-têtes NON AUTONOMES (à corriger dans la bibliothèque : il leur manque des #include) :"
	for f in "${NOT_SELF_CONTAINED[@]}"; do log "  - $f"; done
fi

# ── 5. Vérification ──────────────────────────────────────────────────────────

check_failed=0
if [[ $CHECK -eq 1 ]]; then
	log ""
	log "── Vérification : compilation de chaque .cpp"
	OBJ="$WORK/obj"
	mkdir -p "$OBJ"
	compile_one() {
		local cpp="$1" obj="$OBJ/$(echo "${1#"$SRC"/}" | tr '/' '_').o"
		# shellcheck disable=SC2086
		"$CXX" $CXXFLAGS_SPLIT -O3 -I"$INCLUDE" -I"$SRC" -c "$cpp" -o "$obj" > "$obj.log" 2>&1 || echo "$cpp"
	}
	export -f compile_one
	export OBJ
	find "$SRC" -name '*.cpp' -print0 | sort -z |
		xargs -0 -P "$JOBS" -I{} bash -c 'compile_one "$1"' _ {} > "$WORK/compile_failures"
	count="$(find "$SRC" -name '*.cpp' | wc -l)"
	fails="$(wc -l < "$WORK/compile_failures")"
	log "  $count sources, $fails en échec"
	while IFS= read -r cpp; do
		check_failed=1
		log "  ÉCHEC ${cpp#"$DEST"/}"
		log "$(grep -m3 'error' "$OBJ/$(echo "${cpp#"$SRC"/}" | tr '/' '_').o.log" | sed 's/^/      /')"
	done < "$WORK/compile_failures"
	if [[ "$fails" -eq 0 ]]; then
		log "── Vérification : symboles définis deux fois"
		if "$CXX" -r -nostdlib "$OBJ"/*.o -o "$WORK/all.o" > "$WORK/link.log" 2>&1; then
			log "  aucun"
		else
			check_failed=1
			log "$(grep -m10 'multiple definition' "$WORK/link.log" | sed 's/^/  /')"
		fi
	fi
fi

log ""
log "Rapport : $REPORT"
[[ $failed -eq 0 && $check_failed -eq 0 ]] && exit 0
exit 2
