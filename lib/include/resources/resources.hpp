#pragma once
/**
 * resources:: — cache de ressources asynchrone générique (chargement sync/
 * async, pool nommé par type, worker pool sur sdl3::Thread).
 *
 * S'appuie sur core:: (String/StringView/Option/Result) pour son API
 * publique et sdl3:: (thread.hpp pour AsyncLoader, iostream.hpp pour
 * BytesResource) pour ses primitives de bas niveau — voir le README/mémoire
 * du projet pour l'historique de cette refonte (namespace `sdl`→`resources`,
 * std::string→String, shared_ptr nu→Option<shared_ptr<T>>).
 */
#include "resource.hpp"
#include "pool.hpp"
#include "cache.hpp"
#include "bytes_resource.hpp"
