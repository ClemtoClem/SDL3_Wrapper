// Définitions de sdl3/power.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "sdl3/sdl3.hpp"
#include "sdl3/power.hpp"

namespace sdl3 {

namespace power {

PowerStatus Info() noexcept {
    int seconds = -1, percent = -1;
    PowerState st = PowerState(SDL_GetPowerInfo(&seconds, &percent));

    PowerStatus out;
    out.state = st;
    out.secondsLeft = seconds >= 0 ? Some(seconds) : Option<int>(NONE);
    out.percent = percent >= 0 ? Some(percent) : Option<int>(NONE);
    return out;
}

} // namespace power

} // namespace sdl3
