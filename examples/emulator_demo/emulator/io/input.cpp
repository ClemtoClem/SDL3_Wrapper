#include "emulator/io/input.hpp"

#include "emulator/core.hpp"

namespace emulator_demo {


void Input::pressKey(int key) {
    if (key < 10)
        keyInput &= ~BIT(key);
    else if (key < 12)
        extKeyIn &= ~BIT(key - 10);
}


void Input::releaseKey(int key) {
    if (key < 10)
        keyInput |= BIT(key);
    else if (key < 12)
        extKeyIn |= BIT(key - 10);
}


void Input::pressScreen() {
    extKeyIn &= ~BIT(6); // bit tactile enfoncé
}


void Input::releaseScreen() {
    extKeyIn |= BIT(6); // relâché
}

} // namespace emulator_demo

