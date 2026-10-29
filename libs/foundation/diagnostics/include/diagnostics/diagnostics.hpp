#pragma once
/// diagnostics:: — module Foundation : journalisation, assertions et
/// instrumentation, sans aucune dépendance (ni SDL, ni core).
///   log.hpp       Logger, niveaux/catégories, sinks (console, fichier, mémoire)
///   assert.hpp    DIAG_ASSERT / DIAG_VERIFY, gestionnaire d'échec remplaçable
///   profiler.hpp  Profiler, ScopedTimer, DIAG_PROFILE_SCOPE, compteurs
#include "assert.hpp"
#include "log.hpp"
#include "profiler.hpp"
