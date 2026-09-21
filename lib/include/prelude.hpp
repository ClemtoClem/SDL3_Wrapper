#pragma once
/**
 * lib/prelude.hpp — en-tête d'agrégation précompilé (PCH) pour accélérer la
 * compilation des exemples/tests.
 *
 * Englobe les modules communément inclus par la majorité des exemples/tests
 * (~39.5k lignes cumulées) : core::/sdl3::/ui::/ecs::/data::. Précompilé une
 * fois en `build/prelude.hpp.gch` par le Makefile (cible dédiée), puis
 * réutilisé par chaque binaire exemple/test via `-include prelude.hpp` — voir
 * les règles `PCH_FLAGS`/`$(BUILDDIR)/prelude.hpp.gch` dans le Makefile pour
 * le mécanisme exact (ordre de recherche `-I$(BUILDDIR)` avant `-I$(LIBDIR)`,
 * flags identiques entre génération et consommation, faute de quoi GCC rejette
 * silencieusement le PCH et retombe en compilation normale — aucune erreur
 * visible dans ce cas, seulement une compilation plus lente que prévu).
 *
 * Exclut DÉLIBÉRÉMENT `resources/` et `audio/` : modules actifs du chantier
 * en cours (compilation+threading+resources+exemples audio, cf.
 * `mossy-forging-fog.md`), leur inclusion ici invaliderait ce PCH partagé à
 * chaque sauvegarde pendant que ces modules sont en cours d'écriture — et ils
 * ne sont de toute façon pas sur le chemin chaud de la majorité des
 * exemples/tests existants.
 *
 * Les binaires qui n'ont besoin que d'un sous-ensemble très ciblé (rare)
 * peuvent continuer d'inclure directement les en-têtes précis sans passer par
 * ce fichier — le PCH est une accélération, pas une obligation d'usage.
 */
#include "core/core.hpp"
#include "sdl3/sdl3.hpp"
#include "math/math.hpp"
#include "ecs/ecs.hpp"
#include "ui/ui.hpp"
#include "data/data.hpp"
