#pragma once
#include <SDL3/SDL.h>
#include <span>
#include <vector>

#include "../core/core.hpp"
#include "render.hpp"
#include "stdinc.hpp"

namespace sdl3 {

// ============================================================================
// Keyboard
// ============================================================================

namespace keyboard {

enum class Key : Uint32 {
    UNKNOWN = SDLK_UNKNOWN,
    RETURN = SDLK_RETURN,
    ESCAPE = SDLK_ESCAPE,
    BACKSPACE = SDLK_BACKSPACE,
    TAB = SDLK_TAB,
    SPACE = SDLK_SPACE,
    EXCLAIM = SDLK_EXCLAIM,
    DBLAPOSTROPHE = SDLK_DBLAPOSTROPHE,
    HASH = SDLK_HASH,
    DOLLAR = SDLK_DOLLAR,
    PERCENT = SDLK_PERCENT,
    AMPERSAND = SDLK_AMPERSAND,
    APOSTROPHE = SDLK_APOSTROPHE,
    LEFTPAREN = SDLK_LEFTPAREN,
    RIGHTPAREN = SDLK_RIGHTPAREN,
    ASTERISK = SDLK_ASTERISK,
    PLUS = SDLK_PLUS,
    COMMA = SDLK_COMMA,
    MINUS = SDLK_MINUS,
    PERIOD = SDLK_PERIOD,
    SLASH = SDLK_SLASH,
    _0 = SDLK_0,
    _1 = SDLK_1,
    _2 = SDLK_2,
    _3 = SDLK_3,
    _4 = SDLK_4,
    _5 = SDLK_5,
    _6 = SDLK_6,
    _7 = SDLK_7,
    _8 = SDLK_8,
    _9 = SDLK_9,
    COLON = SDLK_COLON,
    SEMICOLON = SDLK_SEMICOLON,
    LESS = SDLK_LESS,
    EQUALS = SDLK_EQUALS,
    GREATER = SDLK_GREATER,
    QUESTION = SDLK_QUESTION,
    AT = SDLK_AT,
    LEFTBRACKET = SDLK_LEFTBRACKET,
    BACKSLASH = SDLK_BACKSLASH,
    RIGHTBRACKET = SDLK_RIGHTBRACKET,
    CARET = SDLK_CARET,
    UNDERSCORE = SDLK_UNDERSCORE,
    GRAVE = SDLK_GRAVE,
    A = SDLK_A,
    B = SDLK_B,
    C = SDLK_C,
    D = SDLK_D,
    E = SDLK_E,
    F = SDLK_F,
    G = SDLK_G,
    H = SDLK_H,
    I = SDLK_I,
    J = SDLK_J,
    K = SDLK_K,
    L = SDLK_L,
    M = SDLK_M,
    N = SDLK_N,
    O = SDLK_O,
    P = SDLK_P,
    Q = SDLK_Q,
    R = SDLK_R,
    S = SDLK_S,
    T = SDLK_T,
    U = SDLK_U,
    V = SDLK_V,
    W = SDLK_W,
    X = SDLK_X,
    Y = SDLK_Y,
    Z = SDLK_Z,
    LEFTBRACE = SDLK_LEFTBRACE,
    PIPE = SDLK_PIPE,
    RIGHTBRACE = SDLK_RIGHTBRACE,
    TILDE = SDLK_TILDE,
    DELETE = SDLK_DELETE,
    PLUSMINUS = SDLK_PLUSMINUS,
    CAPSLOCK = SDLK_CAPSLOCK,
    F1 = SDLK_F1,
    F2 = SDLK_F2,
    F3 = SDLK_F3,
    F4 = SDLK_F4,
    F5 = SDLK_F5,
    F6 = SDLK_F6,
    F7 = SDLK_F7,
    F8 = SDLK_F8,
    F9 = SDLK_F9,
    F10 = SDLK_F10,
    F11 = SDLK_F11,
    F12 = SDLK_F12,
    F13 = SDLK_F13,
    F14 = SDLK_F14,
    F15 = SDLK_F15,
    F16 = SDLK_F16,
    F17 = SDLK_F17,
    F18 = SDLK_F18,
    F19 = SDLK_F19,
    F20 = SDLK_F20,
    F21 = SDLK_F21,
    F22 = SDLK_F22,
    F23 = SDLK_F23,
    F24 = SDLK_F24,
    PRINTSCREEN = SDLK_PRINTSCREEN,
    SCROLLLOCK = SDLK_SCROLLLOCK,
    PAUSE = SDLK_PAUSE,
    INSERT = SDLK_INSERT,
    HOME = SDLK_HOME,
    PAGEUP = SDLK_PAGEUP,
    End = SDLK_END,
    PAGEDOWN = SDLK_PAGEDOWN,
    Right = SDLK_RIGHT,
    Left = SDLK_LEFT,
    DOWN = SDLK_DOWN,
    UP = SDLK_UP,
    NUMLOCKCLEAR = SDLK_NUMLOCKCLEAR,
    KP_DIVIDE = SDLK_KP_DIVIDE,
    KP_MULTIPLY = SDLK_KP_MULTIPLY,
    KP_MINUS = SDLK_KP_MINUS,
    KP_PLUS = SDLK_KP_PLUS,
    KP_ENTER = SDLK_KP_ENTER,
    KP_1 = SDLK_KP_1,
    KP_2 = SDLK_KP_2,
    KP_3 = SDLK_KP_3,
    KP_4 = SDLK_KP_4,
    KP_5 = SDLK_KP_5,
    KP_6 = SDLK_KP_6,
    KP_7 = SDLK_KP_7,
    KP_8 = SDLK_KP_8,
    KP_9 = SDLK_KP_9,
    KP_0 = SDLK_KP_0,
    KP_PERIOD = SDLK_KP_PERIOD,
    APPLICATION = SDLK_APPLICATION,
    POWER = SDLK_POWER,
    KP_EQUALS = SDLK_KP_EQUALS,
    EXECUTE = SDLK_EXECUTE,
    HELP = SDLK_HELP,
    MENU = SDLK_MENU,
    SELECT = SDLK_SELECT,
    STOP = SDLK_STOP,
    AGAIN = SDLK_AGAIN,
    UNDO = SDLK_UNDO,
    CUT = SDLK_CUT,
    COPY = SDLK_COPY,
    PASTE = SDLK_PASTE,
    FIND = SDLK_FIND,
    MUTE = SDLK_MUTE,
    VOLUMEUP = SDLK_VOLUMEUP,
    VOLUMEDOWN = SDLK_VOLUMEDOWN,
    KP_COMMA = SDLK_KP_COMMA,
    KP_EQUALSAS400 = SDLK_KP_EQUALSAS400,
    ALTERASE = SDLK_ALTERASE,
    SYSREQ = SDLK_SYSREQ,
    CANCEL = SDLK_CANCEL,
    CLEAR = SDLK_CLEAR,
    PRIOR = SDLK_PRIOR,
    RETURN2 = SDLK_RETURN2,
    SEPARATOR = SDLK_SEPARATOR,
    OUT = SDLK_OUT,
    OPER = SDLK_OPER,
    CLEARAGAIN = SDLK_CLEARAGAIN,
    CRSEL = SDLK_CRSEL,
    EXSEL = SDLK_EXSEL,
    KP_00 = SDLK_KP_00,
    KP_000 = SDLK_KP_000,
    THOUSANDS_SEPARATOR = SDLK_THOUSANDSSEPARATOR,
    DECIMAL_SEPARATOR = SDLK_DECIMALSEPARATOR,
    CURRENCY_UNIT = SDLK_CURRENCYUNIT,
    CURRENCY_SUBUNIT = SDLK_CURRENCYSUBUNIT,
    KP_LEFTPAREN = SDLK_KP_LEFTPAREN,
    KP_RIGHTPAREN = SDLK_KP_RIGHTPAREN,
    KP_LEFTBRACE = SDLK_KP_LEFTBRACE,
    KP_RIGHTBRACE = SDLK_KP_RIGHTBRACE,
    KP_TAB = SDLK_KP_TAB,
    KP_BACKSPACE = SDLK_KP_BACKSPACE,
    KP_A = SDLK_KP_A,
    KP_B = SDLK_KP_B,
    KP_C = SDLK_KP_C,
    KP_D = SDLK_KP_D,
    KP_E = SDLK_KP_E,
    KP_F = SDLK_KP_F,
    KP_XOR = SDLK_KP_XOR,
    KP_POWER = SDLK_KP_POWER,
    KP_PERCENT = SDLK_KP_PERCENT,
    KP_LESS = SDLK_KP_LESS,
    KP_GREATER = SDLK_KP_GREATER,
    KP_AMPERSAND = SDLK_KP_AMPERSAND,
    KP_DBLAMPERSAND = SDLK_KP_DBLAMPERSAND,
    KP_VERTICALBAR = SDLK_KP_VERTICALBAR,
    KP_DBLVERTICALBAR = SDLK_KP_DBLVERTICALBAR,
    KP_COLON = SDLK_KP_COLON,
    KP_HASH = SDLK_KP_HASH,
    KP_SPACE = SDLK_KP_SPACE,
    KP_AT = SDLK_KP_AT,
    KP_EXCLAM = SDLK_KP_EXCLAM,
    KP_MEMSTORE = SDLK_KP_MEMSTORE,
    KP_MEMRECALL = SDLK_KP_MEMRECALL,
    KP_MEMCLEAR = SDLK_KP_MEMCLEAR,
    KP_MEMADD = SDLK_KP_MEMADD,
    KP_MEMSUBTRACT = SDLK_KP_MEMSUBTRACT,
    KP_MEMMULTIPLY = SDLK_KP_MEMMULTIPLY,
    KP_MEMDIVIDE = SDLK_KP_MEMDIVIDE,
    KP_PLUSMINUS = SDLK_KP_PLUSMINUS,
    KP_CLEAR = SDLK_KP_CLEAR,
    KP_CLEARENTRY = SDLK_KP_CLEARENTRY,
    KP_BINARY = SDLK_KP_BINARY,
    KP_OCTAL = SDLK_KP_OCTAL,
    KP_DECIMAL = SDLK_KP_DECIMAL,
    KP_HEXADECIMAL = SDLK_KP_HEXADECIMAL,
    LCTRL = SDLK_LCTRL,
    LSHIFT = SDLK_LSHIFT,
    LALT = SDLK_LALT,
    LGUI = SDLK_LGUI,
    RCTRL = SDLK_RCTRL,
    RSHIFT = SDLK_RSHIFT,
    RALT = SDLK_RALT,
    RGUI = SDLK_RGUI,
    MODE = SDLK_MODE,
    SLEEP = SDLK_SLEEP,
    WAKE = SDLK_WAKE,
    CHANNEL_INCREMENT = SDLK_CHANNEL_INCREMENT,
    CHANNEL_DECREMENT = SDLK_CHANNEL_DECREMENT,
    MEDIA_PLAY = SDLK_MEDIA_PLAY,
    MEDIA_PAUSE = SDLK_MEDIA_PAUSE,
    MEDIA_RECORD = SDLK_MEDIA_RECORD,
    MEDIA_FAST_FORWARD = SDLK_MEDIA_FAST_FORWARD,
    MEDIA_REWIND = SDLK_MEDIA_REWIND,
    MEDIA_NEXT_TRACK = SDLK_MEDIA_NEXT_TRACK,
    MEDIA_PREVIOUS_TRACK = SDLK_MEDIA_PREVIOUS_TRACK,
    MEDIA_STOP = SDLK_MEDIA_STOP,
    MEDIA_EJECT = SDLK_MEDIA_EJECT,
    MEDIA_PLAY_PAUSE = SDLK_MEDIA_PLAY_PAUSE,
    MEDIA_SELECT = SDLK_MEDIA_SELECT,
    AC_NEW = SDLK_AC_NEW,
    AC_OPEN = SDLK_AC_OPEN,
    AC_CLOSE = SDLK_AC_CLOSE,
    AC_EXIT = SDLK_AC_EXIT,
    AC_SAVE = SDLK_AC_SAVE,
    AC_PRINT = SDLK_AC_PRINT,
    AC_PROPERTIES = SDLK_AC_PROPERTIES,
    AC_SEARCH = SDLK_AC_SEARCH,
    AC_HOME = SDLK_AC_HOME,
    AC_BACK = SDLK_AC_BACK,
    AC_FORWARD = SDLK_AC_FORWARD,
    AC_STOP = SDLK_AC_STOP,
    AC_REFRESH = SDLK_AC_REFRESH,
    AC_BOOKMARKS = SDLK_AC_BOOKMARKS,
    SOFTLEFT = SDLK_SOFTLEFT,
    SOFTRIGHT = SDLK_SOFTRIGHT,
    CALL = SDLK_CALL,
    ENDCALL = SDLK_ENDCALL,
    LEFT_TAB = SDLK_LEFT_TAB,
    LEVEL5_SHIFT = SDLK_LEVEL5_SHIFT,
    MULTI_KEY_COMPOSE = SDLK_MULTI_KEY_COMPOSE,
    LMETA = SDLK_LMETA,
    RMETA = SDLK_RMETA,
    LHYPER = SDLK_LHYPER,
    RHYPER = SDLK_RHYPER
};

enum class Keymod : Uint16 {
    NONE = SDL_KMOD_NONE,
    LSHIFT = SDL_KMOD_LSHIFT,
    RSHIFT = SDL_KMOD_RSHIFT,
    LEVEL5 = SDL_KMOD_LEVEL5,
    LCTRL = SDL_KMOD_LCTRL,
    RCTRL = SDL_KMOD_RCTRL,
    LALT = SDL_KMOD_LALT,
    RALT = SDL_KMOD_RALT,
    LGUI = SDL_KMOD_LGUI,
    RGUI = SDL_KMOD_RGUI,
    NUM = SDL_KMOD_NUM,
    CAPS = SDL_KMOD_CAPS,
    MODE = SDL_KMOD_MODE,
    SCROLL = SDL_KMOD_SCROLL,
    CTRL = SDL_KMOD_CTRL,
    SHIFT = SDL_KMOD_SHIFT,
    ALT = SDL_KMOD_ALT,
    GUI = SDL_KMOD_GUI
};

/// Returns the current key state for all scancodes.
/// The returned span is valid until the next call to SDL_PumpEvents().
[[nodiscard]] inline std::span<const bool> State() noexcept {
    int n = 0;
    const bool *s = SDL_GetKeyboardState(&n);
    return {s, size_t(n)};
}

/// État d'une touche. Rend `false` — plutôt que de déréférencer un pointeur
/// nul — quand le sous-système d'évènements n'est pas initialisé
/// (SDL_GetKeyboardState rend alors NULL) ou quand le scancode est hors
/// bornes. Cas réellement rencontré : une application en mode « sans écran »
/// qui exécute la même logique de jeu sans initialiser la vidéo.
[[nodiscard]] inline bool IsPressed(SDL_Scancode sc) noexcept {
    std::span<const bool> state = State();
    size_t index = size_t(sc);
    return state.data() != nullptr && index < state.size() && state[index];
}

[[nodiscard]] inline bool IsPressed(SDL_Keycode kc) noexcept { return IsPressed(SDL_GetScancodeFromKey(kc, nullptr)); }

/// Force a re-read of the keyboard state from the OS.
inline void Pump() noexcept { SDL_PumpEvents(); }

[[nodiscard]] inline SDL_Keymod Mods() noexcept { return SDL_GetModState(); }
inline void SetMods(SDL_Keymod m) noexcept { SDL_SetModState(m); }

[[nodiscard]] inline const char *KeyName(SDL_Keycode kc) noexcept { return SDL_GetKeyName(kc); }
[[nodiscard]] inline const char *ScancodeName(SDL_Scancode sc) noexcept { return SDL_GetScancodeName(sc); }

inline void StartTextInput(SDL_Window *win) noexcept { SDL_StartTextInput(win); }
inline void StopTextInput(SDL_Window *win) noexcept { SDL_StopTextInput(win); }

} // namespace keyboard

// ============================================================================
// Mouse state
// ============================================================================

struct MouseState {
    float x = 0.f, y = 0.f;
    SDL_MouseButtonFlags buttons = 0;

    [[nodiscard]] bool Left() const noexcept { return buttons & SDL_BUTTON_LMASK; }
    [[nodiscard]] bool Right() const noexcept { return buttons & SDL_BUTTON_RMASK; }
    [[nodiscard]] bool Middle() const noexcept { return buttons & SDL_BUTTON_MMASK; }
    [[nodiscard]] bool Btn(int b) const noexcept { return buttons & SDL_BUTTON_MASK(b); }

    [[nodiscard]] FPoint Pos() const noexcept { return {x, y}; }
};

namespace mouse {

[[nodiscard]] inline MouseState State() noexcept {
    MouseState ms;
    ms.buttons = SDL_GetMouseState(&ms.x, &ms.y);
    return ms;
}

[[nodiscard]] inline MouseState RelativeState() noexcept {
    MouseState ms;
    ms.buttons = SDL_GetRelativeMouseState(&ms.x, &ms.y);
    return ms;
}

inline bool SetRelative(SDL_Window *win, bool enabled) noexcept { return SDL_SetWindowRelativeMouseMode(win, enabled); }
[[nodiscard]] inline bool IsRelative(SDL_Window *win) noexcept { return SDL_GetWindowRelativeMouseMode(win); }

inline bool Capture(bool on) noexcept { return SDL_CaptureMouse(on); }

inline void Warp(SDL_Window *win, float x, float y) noexcept { SDL_WarpMouseInWindow(win, x, y); }

inline bool Show() noexcept { return SDL_ShowCursor(); }
inline bool Hide() noexcept { return SDL_HideCursor(); }
[[nodiscard]] inline bool Visible() noexcept { return SDL_CursorVisible(); }

} // namespace mouse

// ============================================================================
// Cursor — RAII SDL_Cursor
// ============================================================================

enum class SystemCursor : int {
    DEFAULT = SDL_SYSTEM_CURSOR_DEFAULT,     /**< Default cursor. Usually an arrow. */
    TEXT = SDL_SYSTEM_CURSOR_TEXT,           /**< Text selection. Usually an I-beam. */
    WAIT = SDL_SYSTEM_CURSOR_WAIT,           /**< Wait. Usually an hourglass or watch or spinning ball. */
    CROSSHAIR = SDL_SYSTEM_CURSOR_CROSSHAIR, /**< Crosshair. */
    PROGRESS =
        SDL_SYSTEM_CURSOR_PROGRESS, /**< Program is busy but still interactive. Usually it's WAIT with an arrow. */
    SIZE_NWSE = SDL_SYSTEM_CURSOR_NWSE_RESIZE,   /**< Double arrow pointing northwest and southeast. */
    SIZE_NESW = SDL_SYSTEM_CURSOR_NESW_RESIZE,   /**< Double arrow pointing northeast and southwest. */
    SIZE_EW = SDL_SYSTEM_CURSOR_EW_RESIZE,       /**< Double arrow pointing west and east. */
    SIZE_NS = SDL_SYSTEM_CURSOR_NS_RESIZE,       /**< Double arrow pointing north and south. */
    MOVE = SDL_SYSTEM_CURSOR_MOVE,              /**< Four pointed arrow pointing north, south, east, and west. */
    NOT_ALLOWED = SDL_SYSTEM_CURSOR_NOT_ALLOWED, /**< Not permitted. Usually a slashed circle or crossbones. */
    POINTER = SDL_SYSTEM_CURSOR_POINTER,        /**< Pointer that indicates a link. Usually a pointing hand. */
    NW_RESIZE = SDL_SYSTEM_CURSOR_NW_RESIZE, /**< Window resize top-left. This may be a single arrow or a double arrow
                                               like NWSE_RESIZE. */
    N_RESIZE = SDL_SYSTEM_CURSOR_N_RESIZE,   /**< Window resize top. May be NS_RESIZE. */
    NE_RESIZE = SDL_SYSTEM_CURSOR_NE_RESIZE, /**< Window resize top-right. May be NESW_RESIZE. */
    E_RESIZE = SDL_SYSTEM_CURSOR_E_RESIZE,   /**< Window resize right. May be EW_RESIZE. */
    SE_RESIZE = SDL_SYSTEM_CURSOR_SE_RESIZE, /**< Window resize bottom-right. May be NWSE_RESIZE. */
    S_RESIZE = SDL_SYSTEM_CURSOR_S_RESIZE,   /**< Window resize bottom. May be NS_RESIZE. */
    SW_RESIZE = SDL_SYSTEM_CURSOR_SW_RESIZE, /**< Window resize bottom-left. May be NESW_RESIZE. */
    W_RESIZE = SDL_SYSTEM_CURSOR_W_RESIZE,   /**< Window resize left. May be EW_RESIZE. */
    COUNT = SDL_SYSTEM_CURSOR_COUNT
};

class Cursor : public Wrapper<SDL_Cursor, SDL_DestroyCursor> {
public:
    using Wrapper::Wrapper;

    [[nodiscard]] static Result<Cursor, StringView> FromSystem(SystemCursor id) {
        auto *c = SDL_CreateSystemCursor(SDL_SystemCursor(id));
        if (!c)
            return Err(GetError());
        return Ok(Cursor(c));
    }

    [[nodiscard]] static Result<Cursor, StringView> FromSurface(Ref<Surface> surf, int hotX, int hotY) {
        auto *c = SDL_CreateColorCursor(surf->Get(), hotX, hotY);
        if (!c)
            return Err(GetError());
        return Ok(Cursor(c));
    }

    bool Set() const noexcept { return m_handle && SDL_SetCursor(m_handle); }

    /// Returns the active cursor (non-owning raw pointer — do NOT destroy it).
    [[nodiscard]] static SDL_Cursor *Current() noexcept { return SDL_GetCursor(); }
};

// ============================================================================
// Gamepad — RAII SDL_Gamepad
// ============================================================================

using JoystickID = SDL_JoystickID;

enum class GamepadType : int {
    UNKNOWN = SDL_GAMEPAD_TYPE_UNKNOWN,
    STANDARD = SDL_GAMEPAD_TYPE_STANDARD,
    XBOX360 = SDL_GAMEPAD_TYPE_XBOX360,
    XBOXONE = SDL_GAMEPAD_TYPE_XBOXONE,
    PS3 = SDL_GAMEPAD_TYPE_PS3,
    PS4 = SDL_GAMEPAD_TYPE_PS4,
    PS5 = SDL_GAMEPAD_TYPE_PS5,
    NINTENDO_SWITCH_PRO = SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO,
    NINTENDO_SWITCH_JOYCON_LEFT = SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT,
    NINTENDO_SWITCH_JOYCON_RIGHT = SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT,
    NINTENDO_SWITCH_JOYCON_PAIR = SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR,
    GAMECUBE = SDL_GAMEPAD_TYPE_GAMECUBE,
    COUNT = SDL_GAMEPAD_TYPE_COUNT
};

enum class GamepadButton : int {
    INVALID = SDL_GAMEPAD_BUTTON_INVALID,
    SOUTH = SDL_GAMEPAD_BUTTON_SOUTH,
    EAST = SDL_GAMEPAD_BUTTON_EAST,
    WEST = SDL_GAMEPAD_BUTTON_WEST,
    NORTH = SDL_GAMEPAD_BUTTON_NORTH,
    BACK = SDL_GAMEPAD_BUTTON_BACK,
    GUIDE = SDL_GAMEPAD_BUTTON_GUIDE,
    Start = SDL_GAMEPAD_BUTTON_START,
    LEFT_STICK = SDL_GAMEPAD_BUTTON_LEFT_STICK,
    RIGHT_STICK = SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    LEFT_SHOULDER = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    RIGHT_SHOULDER = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    D_PAD_UP = SDL_GAMEPAD_BUTTON_DPAD_UP,
    D_PAD_DOWN = SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    D_PAD_LEFT = SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    D_PAD_RIGHT = SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    MISC1 = SDL_GAMEPAD_BUTTON_MISC1,
    RIGHT_PADDLE1 = SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1,
    LEFT_PADDLE1 = SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
    RIGHT_PADDLE2 = SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
    LEFT_PADDLE2 = SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,
    TOUCHPAD = SDL_GAMEPAD_BUTTON_TOUCHPAD,
    MISC2 = SDL_GAMEPAD_BUTTON_MISC2,
    MISC3 = SDL_GAMEPAD_BUTTON_MISC3,
    MISC4 = SDL_GAMEPAD_BUTTON_MISC4,
    MISC5 = SDL_GAMEPAD_BUTTON_MISC5,
    MISC6 = SDL_GAMEPAD_BUTTON_MISC6,
    COUNT = SDL_GAMEPAD_BUTTON_COUNT
};

enum class GamepadButtonLabel : int {
    UNKNOWN = SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN,
    A = SDL_GAMEPAD_BUTTON_LABEL_A,
    B = SDL_GAMEPAD_BUTTON_LABEL_B,
    X = SDL_GAMEPAD_BUTTON_LABEL_X,
    Y = SDL_GAMEPAD_BUTTON_LABEL_Y,
    CROSS = SDL_GAMEPAD_BUTTON_LABEL_CROSS,
    CIRCLE = SDL_GAMEPAD_BUTTON_LABEL_CIRCLE,
    SQUARE = SDL_GAMEPAD_BUTTON_LABEL_SQUARE,
    TRIANGLE = SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE
};

enum class GamepadAxis : int {
    INVALID = SDL_GAMEPAD_AXIS_INVALID,
    LEFT_X = SDL_GAMEPAD_AXIS_LEFTX,
    LEFT_Y = SDL_GAMEPAD_AXIS_LEFTY,
    RIGHT_X = SDL_GAMEPAD_AXIS_RIGHTX,
    RIGHT_Y = SDL_GAMEPAD_AXIS_RIGHTY,
    LEFT_TRIGGER = SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
    RIGHT_TRIGGER = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
    COUNT = SDL_GAMEPAD_AXIS_COUNT
};

enum class GamepadBindingType : int {
    NONE = SDL_GAMEPAD_BINDTYPE_NONE,
    BUTTON = SDL_GAMEPAD_BINDTYPE_BUTTON,
    AXIS = SDL_GAMEPAD_BINDTYPE_AXIS,
    HAT = SDL_GAMEPAD_BINDTYPE_HAT
};

class Gamepad : public Wrapper<SDL_Gamepad, SDL_CloseGamepad> {
public:
    using Wrapper::Wrapper;

    [[nodiscard]] static Result<Gamepad, StringView> Open(JoystickID id) {
        auto *g = SDL_OpenGamepad(id);
        if (!g)
            return Err(GetError());
        return Ok(Gamepad(g));
    }

    /// Returns the list of connected gamepad instance IDs.
    [[nodiscard]] static std::vector<JoystickID> Enumerated() {
        int count = 0;
        JoystickID *ids = SDL_GetGamepads(&count);
        if (!ids)
            return {};
        std::vector<JoystickID> v(ids, ids + count);
        SDL_free(ids);
        return v;
    }

    [[nodiscard]] static bool Any() noexcept { return SDL_HasGamepad(); }

    // ── Queries ──────────────────────────────────────────────────────────────

    [[nodiscard]] Sint16 Axis(GamepadAxis a) const noexcept {
        return m_handle ? SDL_GetGamepadAxis(m_handle, SDL_GamepadAxis(a)) : 0;
    }
    [[nodiscard]] bool Button(GamepadButton b) const noexcept {
        return m_handle && SDL_GetGamepadButton(m_handle, SDL_GamepadButton(b));
    }

    [[nodiscard]] const char *Name() const noexcept { return m_handle ? SDL_GetGamepadName(m_handle) : ""; }

    [[nodiscard]] GamepadType Type() const noexcept {
        return m_handle ? GamepadType(SDL_GetGamepadType(m_handle)) : GamepadType::UNKNOWN;
    }

    [[nodiscard]] JoystickID GetId() const noexcept { return m_handle ? SDL_GetGamepadID(m_handle) : 0; }

    // ── Rumble ───────────────────────────────────────────────────────────────

    bool Rumble(uint16_t lowHz, uint16_t highHz, uint32_t durationMs) noexcept {
        return m_handle && SDL_RumbleGamepad(m_handle, lowHz, highHz, durationMs);
    }
    bool RumbleTriggers(uint16_t left, uint16_t right, uint32_t durationMs) noexcept {
        return m_handle && SDL_RumbleGamepadTriggers(m_handle, left, right, durationMs);
    }

    // ── Convenient axis helpers (normalised to [-1, 1]) ───────────────────────

    [[nodiscard]] float AxisNorm(GamepadAxis a) const noexcept { return Axis(a) / 32767.f; }
    [[nodiscard]] float LeftX() const noexcept { return AxisNorm(GamepadAxis::LEFT_X); }
    [[nodiscard]] float LeftY() const noexcept { return AxisNorm(GamepadAxis::LEFT_Y); }
    [[nodiscard]] float RightX() const noexcept { return AxisNorm(GamepadAxis::RIGHT_X); }
    [[nodiscard]] float RightY() const noexcept { return AxisNorm(GamepadAxis::RIGHT_Y); }
    [[nodiscard]] float TriggerLeft() const noexcept { return AxisNorm(GamepadAxis::LEFT_TRIGGER); }
    [[nodiscard]] float TriggerRight() const noexcept { return AxisNorm(GamepadAxis::RIGHT_TRIGGER); }
};

} // namespace sdl3
