
#ifndef DEFINES_H
#define DEFINES_H
#include <cstdio>
#include <SDL3/SDL.h>
#include "settings.hpp"

namespace emulator_demo {

#define LOG(fmt, ...) SDL_Log("%s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
// Same as LOG(), but gated behind Settings::getVerboseLog() (off by
// default). Use for "unknown register/opcode" style diagnostics that can
// fire on every interpreted instruction in a busy-wait loop — logging those
// unconditionally (SDL_Log's formatting + locking + syscall cost, times
// potentially millions of hits per emulated frame) can collapse emulation
// to ~1 FPS, which from the outside just looks like "the ROM doesn't
// launch". Matches upstream NooDS's LOG_WARN, which is compiled out
// entirely unless LOG_LEVEL is raised at build time; this fork's version is
// runtime-toggleable instead, to stay debuggable without a recompile.
#define LOG_VERBOSE(fmt, ...) \
    do { if (Settings::getVerboseLog()) LOG(fmt, ##__VA_ARGS__); } while (0)
#define FORCE_INLINE inline __attribute__((always_inline))
#define BIT(i) (1 << (i))
#define SWAP(a, b) {  \
        auto c = a;   \
        a = b;        \
        b = c;        \
    }
#define U8TO16(data, index) ((data)[index] | ((data)[(index) + 1] << 8))
#define U8TO32(data, index) \
    ((data)[index] | ((data)[(index) + 1] << 8) | ((data)[(index) + 2] << 16) | ((data)[(index) + 3] << 24))
#define U8TO64(data, index) ((uint64_t)U8TO32(data, (index) + 4) << 32) | (uint32_t)U8TO32(data, index)
#define U32TO8(data, index, value)                  \
    (data)[(index) + 0] = (uint8_t)((value) >> 0);  \
    (data)[(index) + 1] = (uint8_t)((value) >> 8);  \
    (data)[(index) + 2] = (uint8_t)((value) >> 16); \
    (data)[(index) + 3] = (uint8_t)((value) >> 24);
} // namespace emulator_demo

#endif
