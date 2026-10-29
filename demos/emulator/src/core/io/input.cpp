#include <emulator/core/io/input.hpp>

#include <emulator/core/core.hpp>

namespace emulator {


void Input::pressKey(int key) {
    if (key < 10)
        keyInput = static_cast<uint16_t>(keyInput & ~BIT(key));
    else if (key < 12)
        extKeyIn = static_cast<uint16_t>(extKeyIn & ~BIT(key - 10));
}


void Input::releaseKey(int key) {
    if (key < 10)
        keyInput |= BIT(key);
    else if (key < 12)
        extKeyIn |= BIT(key - 10);
}


void Input::pressScreen() {
    extKeyIn = static_cast<uint16_t>(extKeyIn & ~BIT(6)); // bit tactile enfoncé
}


void Input::releaseScreen() {
    extKeyIn |= BIT(6); // relâché
}

} // namespace emulator

