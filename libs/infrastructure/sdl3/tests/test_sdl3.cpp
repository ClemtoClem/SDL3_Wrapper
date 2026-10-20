/* @file tests/test_sdl3.cpp */
// (remplace le stub généré `sdl3::add(2,3)` qui n'existe pas : test réel,
//  sans initialisation SDL — sdl3::SetError/GetError utilisent la TLS de
//  la bibliothèque et n'exigent aucun SDL_Init.)

#include "test.hpp"
#include <sdl3/sdl3.hpp>

TEST(error_roundtrip) {
    CHECK(sdl3::ClearError());
    CHECK(sdl3::GetError().IsEmpty());        // ""

    CHECK(sdl3::SetError("oops %d", 42));
    StringView msg = sdl3::GetError();
    CHECK(msg.Contains("oops"));
    CHECK(msg.Contains("42"));

    CHECK(sdl3::ClearError());
    CHECK(sdl3::GetError().IsEmpty());
}

TEST(error_set_error_formats) {
    CHECK(sdl3::SetError("code %s-%d", "net", 7));
    CHECK(sdl3::GetError().Contains("net-7"));
    CHECK(sdl3::ClearError());
}