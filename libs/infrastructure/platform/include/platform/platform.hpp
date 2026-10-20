#pragma once
/// platform:: — module Infrastructure : fenêtre, entrées, temps et cycle de
/// vie, au-dessus de sdl3::, events:: et diagnostics::.
///   clock.hpp        FixedStepClock (pas fixe, interpolation) — logique pure
///   input_state.hpp  InputState (clavier/souris, transitions d'image) — logique pure
///   events.hpp       événements de plate-forme + TranslateEvent(SDL → bus)
///   sdl_log.hpp      pont SDL_Log → diagnostics::Logger
///   application.hpp  Application : boucle principale à surcharger
#include "application.hpp"
#include "clock.hpp"
#include "events.hpp"
#include "input_state.hpp"
#include "sdl_log.hpp"
