#pragma once
/// events:: — module Foundation : événements génériques et mécanismes de
/// distribution, sans aucune dépendance (ni SDL, ni core).
///   signal.hpp     Signal<Args...>, Connection, ScopedConnection
///   event_bus.hpp  EventBus : abonnement par type, publication immédiate ou différée
#include "event_bus.hpp"
#include "signal.hpp"
