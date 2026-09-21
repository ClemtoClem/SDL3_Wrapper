#pragma once
#include <SDL3/SDL.h>
#include <functional>
#include <span>
#include <vector>

#include "../core/core.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

/**
 * The types of events that can be delivered.
 *
 * @since This enum is available since SDL 3.2.0.
 */
enum class EventType : Uint32 {
    FIRST = SDL_EVENT_FIRST, ///< Unused (do not remove)

    QUIT = SDL_EVENT_QUIT, ///< User-requested quit

    /**
     * The application is being terminated by the OS. This event must be handled in
     * a callback set with AddEventWatch(). Called on iOS in
     * applicationWillTerminate() Called on Android in onDestroy()
     */
    TERMINATING = SDL_EVENT_TERMINATING,

    /**
     * The application is low on memory, free memory if possible. This event must be
     * handled in a callback set with AddEventWatch(). Called on iOS in
     * applicationDidReceiveMemoryWarning() Called on Android in onTrimMemory()
     */
    LOW_MEMORY = SDL_EVENT_LOW_MEMORY,

    /**
     * The application is about to enter the background. This event must be handled
     * in a callback set with AddEventWatch(). Called on iOS in
     * applicationWillResignActive() Called on Android in onPause()
     */
    WILL_ENTER_BACKGROUND = SDL_EVENT_WILL_ENTER_BACKGROUND,

    /**
     * The application did enter the background and may not get CPU for some time.
     * This event must be handled in a callback set with AddEventWatch(). Called on
     * iOS in applicationDidEnterBackground() Called on Android in onPause()
     */
    DID_ENTER_BACKGROUND = SDL_EVENT_DID_ENTER_BACKGROUND,

    /**
     * The application is about to enter the foreground. This event must be handled
     * in a callback set with AddEventWatch(). Called on iOS in
     * applicationWillEnterForeground() Called on Android in onResume()
     */
    WILL_ENTER_FOREGROUND = SDL_EVENT_WILL_ENTER_FOREGROUND,

    /**
     * The application is now interactive. This event must be handled in a callback
     * set with AddEventWatch(). Called on iOS in applicationDidBecomeActive()
     * Called on Android in onResume()
     */
    DID_ENTER_FOREGROUND = SDL_EVENT_DID_ENTER_FOREGROUND,

    LOCALE_CHANGED = SDL_EVENT_LOCALE_CHANGED, ///< The user's locale preferences have changed.

    SYSTEM_THEME_CHANGED = SDL_EVENT_SYSTEM_THEME_CHANGED, ///< The system theme changed

    DISPLAY_ORIENTATION = SDL_EVENT_DISPLAY_ORIENTATION, ///< Display orientation has changed to data1

    DISPLAY_ADDED = SDL_EVENT_DISPLAY_ADDED, ///< Display has been added to the system

    DISPLAY_REMOVED = SDL_EVENT_DISPLAY_REMOVED, ///< Display has been removed from the system

    DISPLAY_MOVED = SDL_EVENT_DISPLAY_MOVED, ///< Display has changed position

    DISPLAY_DESKTOP_MODE_CHANGED = SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED, ///< Display has changed desktop mode

    DISPLAY_CURRENT_MODE_CHANGED = SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED, ///< Display has changed current mode

    DISPLAY_CONTENT_SCALE_CHANGED = SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED, ///< Display has changed content scale

#if SDL_VERSION_ATLEAST(3, 4, 0)

    DISPLAY_USABLE_BOUNDS_CHANGED = SDL_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED, ///< Display has changed usable bounds

#endif // SDL_VERSION_ATLEAST(3, 4, 0)

    DISPLAY_FIRST = SDL_EVENT_DISPLAY_FIRST, ///< DISPLAY_FIRST

    DISPLAY_LAST = SDL_EVENT_DISPLAY_LAST, ///< DISPLAY_LAST

    WINDOW_SHOWN = SDL_EVENT_WINDOW_SHOWN, ///< Window has been shown

    WINDOW_HIDDEN = SDL_EVENT_WINDOW_HIDDEN, ///< Window has been hidden

    /**
     * Window has been exposed and should be redrawn, and can be redrawn directly
     * from event watchers for this event. data1 is 1 for live-resize expose events,
     * 0 otherwise.
     */
    WINDOW_EXPOSED = SDL_EVENT_WINDOW_EXPOSED,

    WINDOW_MOVED = SDL_EVENT_WINDOW_MOVED, ///< Window has been moved to data1, data2

    WINDOW_RESIZED = SDL_EVENT_WINDOW_RESIZED, ///< Window has been resized to data1xdata2

    /// The pixel size of the window has changed to data1xdata2
    WINDOW_PIXEL_SIZE_CHANGED = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED,

    /// The pixel size of a Metal view associated with the window has changed
    WINDOW_METAL_VIEW_RESIZED = SDL_EVENT_WINDOW_METAL_VIEW_RESIZED,

    WINDOW_MINIMIZED = SDL_EVENT_WINDOW_MINIMIZED, ///< Window has been minimized

    WINDOW_MAXIMIZED = SDL_EVENT_WINDOW_MAXIMIZED, ///< Window has been maximized

    /// Window has been restored to normal size and position
    WINDOW_RESTORED = SDL_EVENT_WINDOW_RESTORED,

    WINDOW_MOUSE_ENTER = SDL_EVENT_WINDOW_MOUSE_ENTER, ///< Window has gained mouse focus

    WINDOW_MOUSE_LEAVE = SDL_EVENT_WINDOW_MOUSE_LEAVE, ///< Window has lost mouse focus

    WINDOW_FOCUS_GAINED = SDL_EVENT_WINDOW_FOCUS_GAINED, ///< Window has gained keyboard focus

    WINDOW_FOCUS_LOST = SDL_EVENT_WINDOW_FOCUS_LOST, ///< Window has lost keyboard focus

    /// The window manager requests that the window be closed
    WINDOW_CLOSE_REQUESTED = SDL_EVENT_WINDOW_CLOSE_REQUESTED,

    WINDOW_HIT_TEST = SDL_EVENT_WINDOW_HIT_TEST, ///< Window had a hit test that wasn't
                                                 ///< HITTEST_NORMAL

    /// The ICC profile of the window's display has changed
    WINDOW_ICCPROF_CHANGED = SDL_EVENT_WINDOW_ICCPROF_CHANGED,

    WINDOW_DISPLAY_CHANGED = SDL_EVENT_WINDOW_DISPLAY_CHANGED, ///< Window has been moved to display data1

    WINDOW_DISPLAY_SCALE_CHANGED = SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED, ///< Window display scale has been
                                                                           ///< changed

    WINDOW_SAFE_AREA_CHANGED = SDL_EVENT_WINDOW_SAFE_AREA_CHANGED, ///< The window safe area has been changed

    WINDOW_OCCLUDED = SDL_EVENT_WINDOW_OCCLUDED, ///< The window has been occluded

    WINDOW_ENTER_FULLSCREEN = SDL_EVENT_WINDOW_ENTER_FULLSCREEN, ///< The window has entered fullscreen mode

    WINDOW_LEAVE_FULLSCREEN = SDL_EVENT_WINDOW_LEAVE_FULLSCREEN, ///< The window has left fullscreen mode

    /**
     * The window with the associated ID is being or has been destroyed. If this
     * message is being handled in an event watcher, the window m_handle is still
     * valid and can still be used to retrieve any properties associated with the
     * window. Otherwise, the m_handle has already been destroyed and all resources
     * associated with it are invalid
     */
    WINDOW_DESTROYED = SDL_EVENT_WINDOW_DESTROYED,

    WINDOW_HDR_STATE_CHANGED = SDL_EVENT_WINDOW_HDR_STATE_CHANGED, ///< Window HDR properties have changed

    WINDOW_FIRST = SDL_EVENT_WINDOW_FIRST, ///< WINDOW_FIRST

    WINDOW_LAST = SDL_EVENT_WINDOW_LAST, ///< WINDOW_LAST

    KEY_DOWN = SDL_EVENT_KEY_DOWN, ///< Key pressed

    KEY_UP = SDL_EVENT_KEY_UP, ///< Key released

    TEXT_EDITING = SDL_EVENT_TEXT_EDITING, ///< Keyboard text editing (composition)

    TEXT_INPUT = SDL_EVENT_TEXT_INPUT, ///< Keyboard text input

    /**
     * Keymap changed due to a system event such as an input language or keyboard
     * layout change.
     */
    KEYMAP_CHANGED = SDL_EVENT_KEYMAP_CHANGED,

    KEYBOARD_ADDED = SDL_EVENT_KEYBOARD_ADDED, ///< A new keyboard has been inserted into the
                                               ///< system

    KEYBOARD_REMOVED = SDL_EVENT_KEYBOARD_REMOVED, ///< A keyboard has been removed

    TEXT_EDITING_CANDIDATES = SDL_EVENT_TEXT_EDITING_CANDIDATES, ///< Keyboard text editing candidates

#if SDL_VERSION_ATLEAST(3, 4, 0)

    SCREEN_KEYBOARD_SHOWN = SDL_EVENT_SCREEN_KEYBOARD_SHOWN, ///< The on-screen keyboard has been shown

    SCREEN_KEYBOARD_HIDDEN = SDL_EVENT_SCREEN_KEYBOARD_HIDDEN, ///< The on-screen keyboard has been hidden

#endif // SDL_VERSION_ATLEAST(3, 4, 0)

    MOUSE_MOTION = SDL_EVENT_MOUSE_MOTION, ///< Mouse moved

    MOUSE_BUTTON_DOWN = SDL_EVENT_MOUSE_BUTTON_DOWN, ///< Mouse button pressed

    MOUSE_BUTTON_UP = SDL_EVENT_MOUSE_BUTTON_UP, ///< Mouse button released

    MOUSE_WHEEL = SDL_EVENT_MOUSE_WHEEL, ///< Mouse wheel motion

    MOUSE_ADDED = SDL_EVENT_MOUSE_ADDED, ///< A new mouse has been inserted into the system

    MOUSE_REMOVED = SDL_EVENT_MOUSE_REMOVED, ///< A mouse has been removed

    JOYSTICK_AXIS_MOTION = SDL_EVENT_JOYSTICK_AXIS_MOTION, ///< Joystick axis motion

    JOYSTICK_BALL_MOTION = SDL_EVENT_JOYSTICK_BALL_MOTION, ///< Joystick trackball motion

    JOYSTICK_HAT_MOTION = SDL_EVENT_JOYSTICK_HAT_MOTION, ///< Joystick hat position change

    JOYSTICK_BUTTON_DOWN = SDL_EVENT_JOYSTICK_BUTTON_DOWN, ///< Joystick button pressed

    JOYSTICK_BUTTON_UP = SDL_EVENT_JOYSTICK_BUTTON_UP, ///< Joystick button released

    JOYSTICK_ADDED = SDL_EVENT_JOYSTICK_ADDED, ///< A new joystick has been inserted into the
                                               ///< system

    JOYSTICK_REMOVED = SDL_EVENT_JOYSTICK_REMOVED, ///< An opened joystick has been removed

    JOYSTICK_BATTERY_UPDATED = SDL_EVENT_JOYSTICK_BATTERY_UPDATED, ///< Joystick battery level change

    JOYSTICK_UPDATE_COMPLETE = SDL_EVENT_JOYSTICK_UPDATE_COMPLETE, ///< Joystick update is complete

    GAMEPAD_AXIS_MOTION = SDL_EVENT_GAMEPAD_AXIS_MOTION, ///< Gamepad axis motion

    GAMEPAD_BUTTON_DOWN = SDL_EVENT_GAMEPAD_BUTTON_DOWN, ///< Gamepad button pressed

    GAMEPAD_BUTTON_UP = SDL_EVENT_GAMEPAD_BUTTON_UP, ///< Gamepad button released

    GAMEPAD_ADDED = SDL_EVENT_GAMEPAD_ADDED, ///< A new gamepad has been inserted into the system

    GAMEPAD_REMOVED = SDL_EVENT_GAMEPAD_REMOVED, ///< A gamepad has been removed

    GAMEPAD_REMAPPED = SDL_EVENT_GAMEPAD_REMAPPED, ///< The gamepad mapping was updated

    GAMEPAD_TOUCHPAD_DOWN = SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN, ///< Gamepad touchpad was touched

    GAMEPAD_TOUCHPAD_MOTION = SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION, ///< Gamepad touchpad finger was moved

    GAMEPAD_TOUCHPAD_UP = SDL_EVENT_GAMEPAD_TOUCHPAD_UP, ///< Gamepad touchpad finger was lifted

    GAMEPAD_SENSOR_UPDATE = SDL_EVENT_GAMEPAD_SENSOR_UPDATE, ///< Gamepad sensor was updated

    GAMEPAD_UPDATE_COMPLETE = SDL_EVENT_GAMEPAD_UPDATE_COMPLETE, ///< Gamepad update is complete

    GAMEPAD_STEAM_HANDLE_UPDATED = SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED, ///< Gamepad Steam m_handle has changed

    FINGER_DOWN = SDL_EVENT_FINGER_DOWN, ///< FINGER_DOWN

    FINGER_UP = SDL_EVENT_FINGER_UP, ///< FINGER_UP

    FINGER_MOTION = SDL_EVENT_FINGER_MOTION, ///< FINGER_MOTION

    FINGER_CANCELED = SDL_EVENT_FINGER_CANCELED, ///< FINGER_CANCELED

#if SDL_VERSION_ATLEAST(3, 4, 0)

    PINCH_BEGIN = SDL_EVENT_PINCH_BEGIN, ///< Pinch gesture started

    PINCH_UPDATE = SDL_EVENT_PINCH_UPDATE, ///< Pinch gesture updated

    PINCH_END = SDL_EVENT_PINCH_END, ///< Pinch gesture ended

#endif // SDL_VERSION_ATLEAST(3, 4, 0)

    CLIPBOARD_UPDATE = SDL_EVENT_CLIPBOARD_UPDATE, ///< The clipboard changed

    DROP_FILE = SDL_EVENT_DROP_FILE, ///< The system requests a file open

    DROP_TEXT = SDL_EVENT_DROP_TEXT, ///< text/plain drag-and-drop event

    DROP_BEGIN = SDL_EVENT_DROP_BEGIN, ///< A new set of drops is beginning (NULL filename)

    /// Current set of drops is now complete (NULL filename)
    DROP_COMPLETE = SDL_EVENT_DROP_COMPLETE,

    DROP_POSITION = SDL_EVENT_DROP_POSITION, ///< Position while moving over the window

    AUDIO_DEVICE_ADDED = SDL_EVENT_AUDIO_DEVICE_ADDED, ///< A new audio device is available

    AUDIO_DEVICE_REMOVED = SDL_EVENT_AUDIO_DEVICE_REMOVED, ///< An audio device has been removed.

    /// An audio device's format has been changed by the system.
    AUDIO_DEVICE_FORMAT_CHANGED = SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED,

    SENSOR_UPDATE = SDL_EVENT_SENSOR_UPDATE, ///< A sensor was updated

    PEN_PROXIMITY_IN = SDL_EVENT_PEN_PROXIMITY_IN, ///< Pressure-sensitive pen has become available

    PEN_PROXIMITY_OUT = SDL_EVENT_PEN_PROXIMITY_OUT, ///< Pressure-sensitive pen has become
                                                     ///< unavailable

    PEN_DOWN = SDL_EVENT_PEN_DOWN, ///< Pressure-sensitive pen touched drawing surface

    /// Pressure-sensitive pen stopped touching drawing surface
    PEN_UP = SDL_EVENT_PEN_UP,

    PEN_BUTTON_DOWN = SDL_EVENT_PEN_BUTTON_DOWN, ///< Pressure-sensitive pen button pressed

    PEN_BUTTON_UP = SDL_EVENT_PEN_BUTTON_UP, ///< Pressure-sensitive pen button released

    PEN_MOTION = SDL_EVENT_PEN_MOTION, ///< Pressure-sensitive pen is moving on the tablet

    PEN_AXIS = SDL_EVENT_PEN_AXIS, ///< Pressure-sensitive pen angle/pressure/etc changed

    CAMERA_DEVICE_ADDED = SDL_EVENT_CAMERA_DEVICE_ADDED, ///< A new camera device is available

    CAMERA_DEVICE_REMOVED = SDL_EVENT_CAMERA_DEVICE_REMOVED, ///< A camera device has been removed.

    /// A camera device has been approved for use by the user.
    CAMERA_DEVICE_APPROVED = SDL_EVENT_CAMERA_DEVICE_APPROVED,

    /// A camera device has been denied for use by the user.
    CAMERA_DEVICE_DENIED = SDL_EVENT_CAMERA_DEVICE_DENIED,

    /// The render targets have been reset and their contents need to be updated
    RENDER_TARGETS_RESET = SDL_EVENT_RENDER_TARGETS_RESET,

    /// The device has been reset and all textures need to be recreated
    RENDER_DEVICE_RESET = SDL_EVENT_RENDER_DEVICE_RESET,

    RENDER_DEVICE_LOST = SDL_EVENT_RENDER_DEVICE_LOST, ///< The device has been lost and can't be
                                                       ///< recovered.

    PRIVATE0 = SDL_EVENT_PRIVATE0, ///< PRIVATE0

    PRIVATE1 = SDL_EVENT_PRIVATE1, ///< PRIVATE1

    PRIVATE2 = SDL_EVENT_PRIVATE2, ///< PRIVATE2

    PRIVATE3 = SDL_EVENT_PRIVATE3, ///< PRIVATE3

    POLL_SENTINEL = SDL_EVENT_POLL_SENTINEL, ///< Signals the end of an event poll cycle

    /**
     * Events EVENT_USER through EVENT_LAST are for your use, and should be
     * allocated with RegisterEvents()
     */
    USER = SDL_EVENT_USER,

    /// This last event is only for bounding internal arrays
    LAST = SDL_EVENT_LAST,

    ENUM_PADDING = SDL_EVENT_ENUM_PADDING, ///< ENUM_PADDING

};

// ============================================================================
// Event — full wrapper around SDL_Event with typed accessors and predicates
// ============================================================================

struct Event {
    SDL_Event raw{};

    // ── Common ────────────────────────────────────────────────────────────────

    [[nodiscard]] uint32_t Type() const noexcept { return raw.type; }
    [[nodiscard]] SDL_EventType EventType() const noexcept { return SDL_EventType(raw.type); }
    [[nodiscard]] uint64_t Timestamp() const noexcept { return raw.common.timestamp; }

    // ── Union accessors ───────────────────────────────────────────────────────

    [[nodiscard]] const SDL_DisplayEvent &Display() const noexcept { return raw.display; }
    [[nodiscard]] const SDL_WindowEvent &Window() const noexcept { return raw.window; }
    [[nodiscard]] const SDL_KeyboardDeviceEvent &KeyboardDevice() const noexcept { return raw.kdevice; }
    [[nodiscard]] const SDL_KeyboardEvent &Key() const noexcept { return raw.key; }
    [[nodiscard]] const SDL_TextEditingEvent &TextEditing() const noexcept { return raw.edit; }
    [[nodiscard]] const SDL_TextEditingCandidatesEvent &TextEditingCand() const noexcept { return raw.edit_candidates; }
    [[nodiscard]] const SDL_TextInputEvent &TextInput() const noexcept { return raw.text; }
    [[nodiscard]] const SDL_MouseDeviceEvent &MouseDevice() const noexcept { return raw.mdevice; }
    [[nodiscard]] const SDL_MouseMotionEvent &MouseMotion() const noexcept { return raw.motion; }
    [[nodiscard]] const SDL_MouseButtonEvent &MouseButton() const noexcept { return raw.button; }
    [[nodiscard]] const SDL_MouseWheelEvent &MouseWheel() const noexcept { return raw.wheel; }
    [[nodiscard]] const SDL_JoyDeviceEvent &JoyDevice() const noexcept { return raw.jdevice; }
    [[nodiscard]] const SDL_JoyAxisEvent &JoyAxis() const noexcept { return raw.jaxis; }
    [[nodiscard]] const SDL_JoyBallEvent &JoyBall() const noexcept { return raw.jball; }
    [[nodiscard]] const SDL_JoyHatEvent &JoyHat() const noexcept { return raw.jhat; }
    [[nodiscard]] const SDL_JoyButtonEvent &JoyButton() const noexcept { return raw.jbutton; }
    [[nodiscard]] const SDL_JoyBatteryEvent &JoyBattery() const noexcept { return raw.jbattery; }
    [[nodiscard]] const SDL_GamepadDeviceEvent &GamepadDevice() const noexcept { return raw.gdevice; }
    [[nodiscard]] const SDL_GamepadAxisEvent &GamepadAxis() const noexcept { return raw.gaxis; }
    [[nodiscard]] const SDL_GamepadButtonEvent &GamepadButton() const noexcept { return raw.gbutton; }
    [[nodiscard]] const SDL_GamepadTouchpadEvent &GamepadTouchpad() const noexcept { return raw.gtouchpad; }
    [[nodiscard]] const SDL_GamepadSensorEvent &GamepadSensor() const noexcept { return raw.gsensor; }
    [[nodiscard]] const SDL_AudioDeviceEvent &AudioDevice() const noexcept { return raw.adevice; }
    [[nodiscard]] const SDL_CameraDeviceEvent &CameraDevice() const noexcept { return raw.cdevice; }
    [[nodiscard]] const SDL_SensorEvent &Sensor() const noexcept { return raw.sensor; }
    [[nodiscard]] const SDL_QuitEvent &Quit() const noexcept { return raw.quit; }
    [[nodiscard]] const SDL_UserEvent &User() const noexcept { return raw.user; }
    [[nodiscard]] const SDL_TouchFingerEvent &Finger() const noexcept { return raw.tfinger; }
    [[nodiscard]] const SDL_PinchFingerEvent &Pinch() const noexcept { return raw.pinch; }
    [[nodiscard]] const SDL_PenProximityEvent &PenProximity() const noexcept { return raw.pproximity; }
    [[nodiscard]] const SDL_PenTouchEvent &PenTouch() const noexcept { return raw.ptouch; }
    [[nodiscard]] const SDL_PenMotionEvent &PenMotion() const noexcept { return raw.pmotion; }
    [[nodiscard]] const SDL_PenButtonEvent &PenButton() const noexcept { return raw.pbutton; }
    [[nodiscard]] const SDL_PenAxisEvent &PenAxis() const noexcept { return raw.paxis; }
    [[nodiscard]] const SDL_RenderEvent &Render() const noexcept { return raw.render; }
    [[nodiscard]] const SDL_DropEvent &Drop() const noexcept { return raw.drop; }
    [[nodiscard]] const SDL_ClipboardEvent &Clipboard() const noexcept { return raw.clipboard; }

    // ── Category predicates ───────────────────────────────────────────────────

    [[nodiscard]] bool IsQuit() const noexcept { return raw.type == uint32_t(EventType::QUIT); }
    [[nodiscard]] bool IsApp() const noexcept {
        return raw.type >= uint32_t(EventType::TERMINATING) && raw.type <= uint32_t(EventType::SYSTEM_THEME_CHANGED);
    }
    [[nodiscard]] bool IsDisplay() const noexcept {
        return raw.type >= uint32_t(EventType::DISPLAY_FIRST) && raw.type <= uint32_t(EventType::DISPLAY_LAST);
    }
    [[nodiscard]] bool IsWindow() const noexcept {
        return raw.type >= uint32_t(EventType::WINDOW_FIRST) && raw.type <= uint32_t(EventType::WINDOW_LAST);
    }
    [[nodiscard]] bool IsKeyboard() const noexcept {
        return raw.type >= uint32_t(EventType::KEY_DOWN) && raw.type <= uint32_t(EventType::SCREEN_KEYBOARD_HIDDEN);
    }
    [[nodiscard]] bool IsMouse() const noexcept {
        return raw.type >= uint32_t(EventType::MOUSE_MOTION) && raw.type <= uint32_t(EventType::MOUSE_REMOVED);
    }
    [[nodiscard]] bool IsJoystick() const noexcept {
        return raw.type >= uint32_t(EventType::JOYSTICK_AXIS_MOTION) &&
               raw.type <= uint32_t(EventType::JOYSTICK_UPDATE_COMPLETE);
    }
    [[nodiscard]] bool IsGamepad() const noexcept {
        return raw.type >= uint32_t(EventType::GAMEPAD_AXIS_MOTION) &&
               raw.type <= uint32_t(EventType::GAMEPAD_STEAM_HANDLE_UPDATED);
    }
    [[nodiscard]] bool IsFinger() const noexcept {
        return raw.type >= uint32_t(EventType::FINGER_DOWN) && raw.type <= uint32_t(EventType::FINGER_CANCELED);
    }
    [[nodiscard]] bool IsPinch() const noexcept {
        return raw.type >= uint32_t(EventType::PINCH_BEGIN) && raw.type <= uint32_t(EventType::PINCH_END);
    }
    [[nodiscard]] bool IsClipboard() const noexcept { return raw.type == uint32_t(EventType::CLIPBOARD_UPDATE); }
    [[nodiscard]] bool IsDrop() const noexcept {
        return raw.type >= uint32_t(EventType::DROP_FILE) && raw.type <= uint32_t(EventType::DROP_POSITION);
    }
    [[nodiscard]] bool IsAudio() const noexcept {
        return raw.type >= uint32_t(EventType::AUDIO_DEVICE_ADDED) &&
               raw.type <= uint32_t(EventType::AUDIO_DEVICE_FORMAT_CHANGED);
    }
    [[nodiscard]] bool IsSensor() const noexcept { return raw.type == uint32_t(EventType::SENSOR_UPDATE); }
    [[nodiscard]] bool IsPen() const noexcept {
        return raw.type >= uint32_t(EventType::PEN_PROXIMITY_IN) && raw.type <= uint32_t(EventType::PEN_AXIS);
    }
    [[nodiscard]] bool IsCamera() const noexcept {
        return raw.type >= uint32_t(EventType::CAMERA_DEVICE_ADDED) &&
               raw.type <= uint32_t(EventType::CAMERA_DEVICE_DENIED);
    }
    [[nodiscard]] bool IsRender() const noexcept {
        return raw.type >= uint32_t(EventType::RENDER_TARGETS_RESET) &&
               raw.type <= uint32_t(EventType::RENDER_DEVICE_LOST);
    }
    [[nodiscard]] bool IsUser() const noexcept {
        return raw.type >= uint32_t(EventType::USER) && raw.type < uint32_t(EventType::LAST);
    }

    // ── Keyboard predicates ───────────────────────────────────────────────────

    [[nodiscard]] bool IsKeyDown() const noexcept { return raw.type == uint32_t(EventType::KEY_DOWN); }
    [[nodiscard]] bool IsKeyUp() const noexcept { return raw.type == uint32_t(EventType::KEY_UP); }
    [[nodiscard]] bool IsKey() const noexcept {
        return raw.type == uint32_t(EventType::KEY_DOWN) || raw.type == uint32_t(EventType::KEY_UP);
    }
    [[nodiscard]] bool IsTextInput() const noexcept { return raw.type == uint32_t(EventType::TEXT_INPUT); }
    [[nodiscard]] bool IsTextEditing() const noexcept { return raw.type == uint32_t(EventType::TEXT_EDITING); }
    [[nodiscard]] bool IsKeyboardDevice() const noexcept {
        return raw.type == uint32_t(EventType::KEYBOARD_ADDED) || raw.type == uint32_t(EventType::KEYBOARD_REMOVED);
    }

    [[nodiscard]] bool IsKeyDown(SDL_Keycode code) const noexcept { return IsKeyDown() && raw.key.key == code; }
    [[nodiscard]] bool IsKeyUp(SDL_Keycode code) const noexcept { return IsKeyUp() && raw.key.key == code; }
    [[nodiscard]] bool IsKeyDown(SDL_Scancode scan) const noexcept { return IsKeyDown() && raw.key.scancode == scan; }
    [[nodiscard]] bool IsKeyUp(SDL_Scancode scan) const noexcept { return IsKeyUp() && raw.key.scancode == scan; }

    [[nodiscard]] SDL_Keycode Keycode() const noexcept { return raw.key.key; }
    [[nodiscard]] SDL_Scancode Scancode() const noexcept { return raw.key.scancode; }

    // ── Mouse predicates ──────────────────────────────────────────────────────

    [[nodiscard]] bool IsMouseMotion() const noexcept { return raw.type == uint32_t(EventType::MOUSE_MOTION); }
    [[nodiscard]] bool IsMouseDown() const noexcept { return raw.type == uint32_t(EventType::MOUSE_BUTTON_DOWN); }
    [[nodiscard]] bool IsMouseUp() const noexcept { return raw.type == uint32_t(EventType::MOUSE_BUTTON_UP); }
    [[nodiscard]] bool IsMouseWheel() const noexcept { return raw.type == uint32_t(EventType::MOUSE_WHEEL); }
    [[nodiscard]] bool IsMouseDevice() const noexcept {
        return raw.type == uint32_t(EventType::MOUSE_ADDED) || raw.type == uint32_t(EventType::MOUSE_REMOVED);
    }
    [[nodiscard]] bool IsMouseDown(uint8_t btn) const noexcept { return IsMouseDown() && raw.button.button == btn; }
    [[nodiscard]] bool IsMouseUp(uint8_t btn) const noexcept { return IsMouseUp() && raw.button.button == btn; }

    // ── Window predicates ─────────────────────────────────────────────────────

    [[nodiscard]] bool IsWindowShown() const noexcept { return raw.type == uint32_t(EventType::WINDOW_SHOWN); }
    [[nodiscard]] bool IsWindowHidden() const noexcept { return raw.type == uint32_t(EventType::WINDOW_HIDDEN); }
    [[nodiscard]] bool IsWindowExposed() const noexcept { return raw.type == uint32_t(EventType::WINDOW_EXPOSED); }
    [[nodiscard]] bool IsWindowMoved() const noexcept { return raw.type == uint32_t(EventType::WINDOW_MOVED); }
    [[nodiscard]] bool IsWindowResized() const noexcept { return raw.type == uint32_t(EventType::WINDOW_RESIZED); }
    [[nodiscard]] bool IsWindowMinimized() const noexcept { return raw.type == uint32_t(EventType::WINDOW_MINIMIZED); }
    [[nodiscard]] bool IsWindowMaximized() const noexcept { return raw.type == uint32_t(EventType::WINDOW_MAXIMIZED); }
    [[nodiscard]] bool IsWindowRestored() const noexcept { return raw.type == uint32_t(EventType::WINDOW_RESTORED); }
    [[nodiscard]] bool IsWindowMouseEnter() const noexcept {
        return raw.type == uint32_t(EventType::WINDOW_MOUSE_ENTER);
    }
    [[nodiscard]] bool IsWindowMouseLeave() const noexcept {
        return raw.type == uint32_t(EventType::WINDOW_MOUSE_LEAVE);
    }
    [[nodiscard]] bool IsWindowFocusGained() const noexcept {
        return raw.type == uint32_t(EventType::WINDOW_FOCUS_GAINED);
    }
    [[nodiscard]] bool IsWindowFocusLost() const noexcept { return raw.type == uint32_t(EventType::WINDOW_FOCUS_LOST); }
    [[nodiscard]] bool IsWindowClosed() const noexcept {
        return raw.type == uint32_t(EventType::WINDOW_CLOSE_REQUESTED);
    }
    [[nodiscard]] bool IsWindowDestroyed() const noexcept { return raw.type == uint32_t(EventType::WINDOW_DESTROYED); }
    [[nodiscard]] bool IsWindowFullscreen() const noexcept {
        return raw.type == uint32_t(EventType::WINDOW_ENTER_FULLSCREEN);
    }
    [[nodiscard]] bool IsWindowOccluded() const noexcept { return raw.type == uint32_t(EventType::WINDOW_OCCLUDED); }

    // ── Joystick predicates ───────────────────────────────────────────────────

    [[nodiscard]] bool IsJoyAxis() const noexcept { return raw.type == uint32_t(EventType::JOYSTICK_AXIS_MOTION); }
    [[nodiscard]] bool IsJoyBall() const noexcept { return raw.type == uint32_t(EventType::JOYSTICK_BALL_MOTION); }
    [[nodiscard]] bool IsJoyHat() const noexcept { return raw.type == uint32_t(EventType::JOYSTICK_HAT_MOTION); }
    [[nodiscard]] bool IsJoyButtonDown() const noexcept {
        return raw.type == uint32_t(EventType::JOYSTICK_BUTTON_DOWN);
    }
    [[nodiscard]] bool IsJoyButtonUp() const noexcept { return raw.type == uint32_t(EventType::JOYSTICK_BUTTON_UP); }
    [[nodiscard]] bool IsJoyDevice() const noexcept {
        return raw.type == uint32_t(EventType::JOYSTICK_ADDED) || raw.type == uint32_t(EventType::JOYSTICK_REMOVED);
    }
    [[nodiscard]] bool IsJoyBattery() const noexcept {
        return raw.type == uint32_t(EventType::JOYSTICK_BATTERY_UPDATED);
    }

    // ── Gamepad predicates ────────────────────────────────────────────────────

    [[nodiscard]] bool IsGamepadAxis() const noexcept { return raw.type == uint32_t(EventType::GAMEPAD_AXIS_MOTION); }
    [[nodiscard]] bool IsGamepadButtonDown() const noexcept {
        return raw.type == uint32_t(EventType::GAMEPAD_BUTTON_DOWN);
    }
    [[nodiscard]] bool IsGamepadButtonUp() const noexcept { return raw.type == uint32_t(EventType::GAMEPAD_BUTTON_UP); }
    [[nodiscard]] bool IsGamepadAdded() const noexcept { return raw.type == uint32_t(EventType::GAMEPAD_ADDED); }
    [[nodiscard]] bool IsGamepadRemoved() const noexcept { return raw.type == uint32_t(EventType::GAMEPAD_REMOVED); }
    [[nodiscard]] bool IsGamepadTouchpad() const noexcept {
        return raw.type >= uint32_t(EventType::GAMEPAD_TOUCHPAD_DOWN) &&
               raw.type <= uint32_t(EventType::GAMEPAD_TOUCHPAD_UP);
    }
    [[nodiscard]] bool IsGamepadSensor() const noexcept {
        return raw.type == uint32_t(EventType::GAMEPAD_SENSOR_UPDATE);
    }

    // ── Touch predicates ──────────────────────────────────────────────────────

    [[nodiscard]] bool IsFingerDown() const noexcept { return raw.type == uint32_t(EventType::FINGER_DOWN); }
    [[nodiscard]] bool IsFingerUp() const noexcept { return raw.type == uint32_t(EventType::FINGER_UP); }
    [[nodiscard]] bool IsFingerMotion() const noexcept { return raw.type == uint32_t(EventType::FINGER_MOTION); }
    [[nodiscard]] bool IsPinchBegin() const noexcept { return raw.type == uint32_t(EventType::PINCH_BEGIN); }
    [[nodiscard]] bool IsPinchUpdate() const noexcept { return raw.type == uint32_t(EventType::PINCH_UPDATE); }
    [[nodiscard]] bool IsPinchEnd() const noexcept { return raw.type == uint32_t(EventType::PINCH_END); }

    // ── Pen predicates ────────────────────────────────────────────────────────

    [[nodiscard]] bool IsPenProximityIn() const noexcept { return raw.type == uint32_t(EventType::PEN_PROXIMITY_IN); }
    [[nodiscard]] bool IsPenProximityOut() const noexcept { return raw.type == uint32_t(EventType::PEN_PROXIMITY_OUT); }
    [[nodiscard]] bool IsPenDown() const noexcept { return raw.type == uint32_t(EventType::PEN_DOWN); }
    [[nodiscard]] bool IsPenUp() const noexcept { return raw.type == uint32_t(EventType::PEN_UP); }
    [[nodiscard]] bool IsPenButtonDown() const noexcept { return raw.type == uint32_t(EventType::PEN_BUTTON_DOWN); }
    [[nodiscard]] bool IsPenButtonUp() const noexcept { return raw.type == uint32_t(EventType::PEN_BUTTON_UP); }
    [[nodiscard]] bool IsPenMotion() const noexcept { return raw.type == uint32_t(EventType::PEN_MOTION); }
    [[nodiscard]] bool IsPenAxis() const noexcept { return raw.type == uint32_t(EventType::PEN_AXIS); }

    // ── Drop predicates ───────────────────────────────────────────────────────

    [[nodiscard]] bool IsDropFile() const noexcept { return raw.type == uint32_t(EventType::DROP_FILE); }
    [[nodiscard]] bool IsDropText() const noexcept { return raw.type == uint32_t(EventType::DROP_TEXT); }
    [[nodiscard]] bool IsDropBegin() const noexcept { return raw.type == uint32_t(EventType::DROP_BEGIN); }
    [[nodiscard]] bool IsDropComplete() const noexcept { return raw.type == uint32_t(EventType::DROP_COMPLETE); }
    [[nodiscard]] bool IsDropPosition() const noexcept { return raw.type == uint32_t(EventType::DROP_POSITION); }

    // ── Audio predicates ──────────────────────────────────────────────────────

    [[nodiscard]] bool IsAudioAdded() const noexcept { return raw.type == uint32_t(EventType::AUDIO_DEVICE_ADDED); }
    [[nodiscard]] bool IsAudioRemoved() const noexcept { return raw.type == uint32_t(EventType::AUDIO_DEVICE_REMOVED); }

    // ── Window ID helper ─────────────────────────────────────────────────────

    // Returns the window ID for events associated with a window (0 if none).
    [[nodiscard]] uint32_t WindowId() const noexcept {
        const auto T = raw.type;
        if (T >= uint32_t(EventType::WINDOW_FIRST) && T <= uint32_t(EventType::WINDOW_LAST))
            return raw.window.windowID;
        if (T == uint32_t(EventType::KEY_DOWN) || T == uint32_t(EventType::KEY_UP) ||
            T == uint32_t(EventType::KEYBOARD_ADDED) || T == uint32_t(EventType::KEYBOARD_REMOVED))
            return raw.key.windowID;
        if (T == uint32_t(EventType::TEXT_EDITING) || T == uint32_t(EventType::TEXT_EDITING_CANDIDATES))
            return raw.edit.windowID;
        if (T == uint32_t(EventType::TEXT_INPUT))
            return raw.text.windowID;
        if (T == uint32_t(EventType::MOUSE_MOTION))
            return raw.motion.windowID;
        if (T == uint32_t(EventType::MOUSE_BUTTON_DOWN) || T == uint32_t(EventType::MOUSE_BUTTON_UP))
            return raw.button.windowID;
        if (T == uint32_t(EventType::MOUSE_WHEEL))
            return raw.wheel.windowID;
        if (T >= uint32_t(EventType::FINGER_DOWN) && T <= uint32_t(EventType::FINGER_CANCELED))
            return raw.tfinger.windowID;
        if (T >= uint32_t(EventType::DROP_FILE) && T <= uint32_t(EventType::DROP_POSITION))
            return raw.drop.windowID;
        if (T >= uint32_t(EventType::RENDER_TARGETS_RESET) && T <= uint32_t(EventType::RENDER_DEVICE_LOST))
            return raw.render.windowID;
        if (T >= uint32_t(EventType::PEN_PROXIMITY_IN) && T <= uint32_t(EventType::PEN_AXIS)) {
            // All pen events share windowID at the same offset
            return raw.pproximity.windowID;
        }
        return 0;
    }

    // Returns the SDL_Window* for this event, if applicable.
    [[nodiscard]] SDL_Window *EventWindow() const noexcept { return SDL_GetWindowFromEvent(&raw); }

    // ── Description (debug) ───────────────────────────────────────────────────

    [[nodiscard]] String Describe() const {
        char buf[512] = {};
        SDL_GetEventDescription(&raw, buf, int(sizeof(buf)));
        return String(buf);
    }
};

// ============================================================================
// EventWatch — RAII SDL_AddEventWatch / SDL_RemoveEventWatch
// Callback is called from the SDL event thread for each new event.
// ============================================================================

class EventWatch {
    using Fn = std::function<bool(const Event &)>;

    struct Ctx {
        Fn fn;
    };

    static bool SDLCALL Trampoline(void *ud, SDL_Event *ev) noexcept {
        Event e;
        e.raw = *ev;
        return static_cast<Ctx *>(ud)->fn(e);
    }

    Ctx *ctx = nullptr;

public:
    EventWatch() = default;

    template <typename F> explicit EventWatch(F &&fn) : ctx(new Ctx{std::forward<F>(fn)}) {
        SDL_AddEventWatch(Trampoline, ctx);
    }

    ~EventWatch() {
        if (ctx) {
            SDL_RemoveEventWatch(Trampoline, ctx);
            delete ctx;
        }
    }

    EventWatch(const EventWatch &) = delete;
    EventWatch &operator=(const EventWatch &) = delete;
    EventWatch(EventWatch &&o) noexcept : ctx(o.ctx) { o.ctx = nullptr; }
    EventWatch &operator=(EventWatch &&o) noexcept {
        if (this != &o) {
            if (ctx) {
                SDL_RemoveEventWatch(Trampoline, ctx);
                delete ctx;
            }
            ctx = o.ctx;
            o.ctx = nullptr;
        }
        return *this;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return ctx != nullptr; }
};

// ============================================================================
// Free event functions
// ============================================================================

// ── Pump / poll / wait ────────────────────────────────────────────────────────

inline void PumpEvents() noexcept { SDL_PumpEvents(); }

/// Non-blocking: dequeue the next event, or NONE if the queue is empty.
[[nodiscard]] inline Option<Event> PollEvent() noexcept {
    Event e;
    if (SDL_PollEvent(&e.raw))
        return Some(e);
    return NONE;
}

/// Blocking: wait up to `timeoutMs` ms for an event (-1 = wait forever).
[[nodiscard]] inline Option<Event> WaitEvent(int timeoutMs = -1) noexcept {
    Event e;
    if (SDL_WaitEventTimeout(&e.raw, timeoutMs))
        return Some(e);
    return NONE;
}

// ── Queue inspection ─────────────────────────────────────────────────────────

[[nodiscard]] inline bool HasEvent(uint32_t type) noexcept { return SDL_HasEvent(type); }
[[nodiscard]] inline bool HasEvent(SDL_EventType type) noexcept { return SDL_HasEvent(uint32_t(type)); }
[[nodiscard]] inline bool HasEvents(EventType minType = EventType::FIRST,
                                    EventType maxType = EventType::LAST) noexcept {
    return SDL_HasEvents(uint32_t(minType), uint32_t(maxType));
}

/// Peek at (copy without removing) up to `max` events of the given type range.
[[nodiscard]] inline std::vector<Event> PeekEvents(int max = 64, EventType minType = EventType::FIRST,
                                                   EventType maxType = EventType::LAST) {
    std::vector<SDL_Event> buf(max);
    int n = SDL_PeepEvents(buf.data(), max, SDL_PEEKEVENT, uint32_t(minType), uint32_t(maxType));
    if (n <= 0)
        return {};
    std::vector<Event> out(n);
    for (int i = 0; i < n; ++i)
        out[i].raw = buf[i];
    return out;
}

/// Remove and return up to `max` events of the given type range.
[[nodiscard]] inline std::vector<Event> GetEvents(int max = 64, EventType minType = EventType::FIRST,
                                                  EventType maxType = EventType::LAST) {
    std::vector<SDL_Event> buf(max);
    int n = SDL_PeepEvents(buf.data(), max, SDL_GETEVENT, uint32_t(minType), uint32_t(maxType));
    if (n <= 0)
        return {};
    std::vector<Event> out(n);
    for (int i = 0; i < n; ++i)
        out[i].raw = buf[i];
    return out;
}

// ── Push events ───────────────────────────────────────────────────────────────

/// Push a pre-built event onto the queue.
inline bool PushEvent(Event e) noexcept { return SDL_PushEvent(&e.raw); }

/// Push a user-defined event. `type` must be in [EventType::USER, EventType::LAST).
inline bool PushUserEvent(uint32_t type, int32_t code = 0, void *data1 = nullptr, void *data2 = nullptr) noexcept {
    SDL_Event e{};
    e.user.type = type;
    e.user.code = code;
    e.user.data1 = data1;
    e.user.data2 = data2;
    return SDL_PushEvent(&e);
}

// ── Flush ─────────────────────────────────────────────────────────────────────

inline void FlushEvent(EventType type) noexcept { SDL_FlushEvent(uint32_t(type)); }
inline void FlushEvent(uint32_t type) noexcept { SDL_FlushEvent(type); }

inline void FlushEvents(EventType minType = EventType::FIRST, EventType maxType = EventType::LAST) noexcept {
    SDL_FlushEvents(uint32_t(minType), uint32_t(maxType));
}

// ── Enable / disable ──────────────────────────────────────────────────────────

inline void SetEventEnabled(EventType type, bool enabled) noexcept { SDL_SetEventEnabled(uint32_t(type), enabled); }
inline void SetEventEnabled(uint32_t type, bool enabled) noexcept { SDL_SetEventEnabled(type, enabled); }
[[nodiscard]] inline bool IsEventEnabled(EventType type) noexcept { return SDL_EventEnabled(uint32_t(type)); }

// ── Custom event type registration ───────────────────────────────────────────

/// Reserve `count` custom event types; returns the first allocated type ID.
/// Use the returned value and subsequent IDs for pushUserEvent().
[[nodiscard]] inline uint32_t RegisterEvents(int count = 1) noexcept { return SDL_RegisterEvents(count); }

// ── Filter (one-shot) ─────────────────────────────────────────────────────────

/// Remove all events from the queue for which `fn` returns false.
/// Runs synchronously on the calling thread.
inline void FilterEvents(std::function<bool(const Event &)> fn) noexcept {
    struct Ctx {
        std::function<bool(const Event &)> &fn;
    };
    Ctx ctx{fn};
    SDL_FilterEvents(
        [](void *ud, SDL_Event *ev) -> bool {
            Ctx &c = *static_cast<Ctx *>(ud);
            Event e;
            e.raw = *ev;
            return c.fn(e);
        },
        &ctx);
}

} // namespace sdl3
