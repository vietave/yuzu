// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <string>
#include "common/common_types.h"

namespace Settings {
namespace NativeButton {
enum Values : int {
    A,
    B,
    X,
    Y,
    LStick,
    RStick,
    L,
    R,
    ZL,
    ZR,
    Plus,
    Minus,

    DLeft,
    DUp,
    DRight,
    DDown,

    SL,
    SR,

    Home,
    Screenshot,

    NumButtons,
};

constexpr int BUTTON_HID_BEGIN = A;
constexpr int BUTTON_NS_BEGIN = Home;

constexpr int BUTTON_HID_END = BUTTON_NS_BEGIN;
constexpr int BUTTON_NS_END = NumButtons;

constexpr int NUM_BUTTONS_HID = BUTTON_HID_END - BUTTON_HID_BEGIN;
constexpr int NUM_BUTTONS_NS = BUTTON_NS_END - BUTTON_NS_BEGIN;

extern const std::array<const char*, NumButtons> mapping;

} // namespace NativeButton

namespace NativeAnalog {
enum Values : int {
    LStick,
    RStick,

    NumAnalogs,
};

constexpr int STICK_HID_BEGIN = LStick;
constexpr int STICK_HID_END = NumAnalogs;
constexpr int NUM_STICKS_HID = NumAnalogs;

extern const std::array<const char*, NumAnalogs> mapping;
} // namespace NativeAnalog

namespace NativeMotion {
enum Values : int {
    MOTIONLEFT,
    MOTIONRIGHT,

    NumMotions,
};

constexpr int MOTION_HID_BEGIN = MOTIONLEFT;
constexpr int MOTION_HID_END = NumMotions;
constexpr int NUM_MOTION_HID = NumMotions;

extern const std::array<const char*, NumMotions> mapping;
} // namespace NativeMotion

namespace NativeMouseButton {
enum Values {
    Left,
    Right,
    Middle,
    Forward,
    Back,

    NumMouseButtons,
};

constexpr int MOUSE_HID_BEGIN = Left;
constexpr int MOUSE_HID_END = NumMouseButtons;
constexpr int NUM_MOUSE_HID = NumMouseButtons;

extern const std::array<const char*, NumMouseButtons> mapping;
} // namespace NativeMouseButton

namespace NativeKeyboard {
enum Keys {
    None,
    Error,

    A = 4,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    N1,
    N2,
    N3,
    N4,
    N5,
    N6,
    N7,
    N8,
    N9,
    N0,
    Enter,
    Escape,
    Backspace,
    Tab,
    Space,
    Minus,
    Equal,
    LeftBrace,
    RightBrace,
    Backslash,
    Tilde,
    Semicolon,
    Apostrophe,
    Grave,
    Comma,
    Dot,
    Slash,
    CapsLockKey,

    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    SystemRequest,
    ScrollLockKey,
    Pause,
    Insert,
    Home,
    PageUp,
    Delete,
    End,
    PageDown,
    Right,
    Left,
    Down,
    Up,

    NumLockKey,
    KPSlash,
    KPAsterisk,
    KPMinus,
    KPPlus,
    KPEnter,
    KP1,
    KP2,
    KP3,
    KP4,
    KP5,
    KP6,
    KP7,
    KP8,
    KP9,
    KP0,
    KPDot,

    Key102,
    Compose,
    Power,
    KPEqual,

    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,

    Open,
    Help,
    Properties,
    Front,
    Stop,
    Repeat,
    Undo,
    Cut,
    Copy,
    Paste,
    Find,
    Mute,
    VolumeUp,
    VolumeDown,
    CapsLockActive,
    NumLockActive,
    ScrollLockActive,
    KPComma,

    KPLeftParenthesis,
    KPRightParenthesis,

    LeftControlKey = 0xE0,
    LeftShiftKey,
    LeftAltKey,
    LeftMetaKey,
    RightControlKey,
    RightShiftKey,
    RightAltKey,
    RightMetaKey,

    MediaPlayPause,
    MediaStopCD,
    MediaPrevious,
    MediaNext,
    MediaEject,
    MediaVolumeUp,
    MediaVolumeDown,
    MediaMute,
    MediaWebsite,
    MediaBack,
    MediaForward,
    MediaStop,
    MediaFind,
    MediaScrollUp,
    MediaScrollDown,
    MediaEdit,
    MediaSleep,
    MediaCoffee,
    MediaRefresh,
    MediaCalculator,

    NumKeyboardKeys,
};

static_assert(NumKeyboardKeys == 0xFC, "Incorrect number of keyboard keys.");

enum Modifiers {
    LeftControl,
    LeftShift,
    LeftAlt,
    LeftMeta,
    RightControl,
    RightShift,
    RightAlt,
    RightMeta,
    CapsLock,
    ScrollLock,
    NumLock,

    NumKeyboardMods,
};

constexpr int KEYBOARD_KEYS_HID_BEGIN = None;
constexpr int KEYBOARD_KEYS_HID_END = NumKeyboardKeys;
constexpr int NUM_KEYBOARD_KEYS_HID = NumKeyboardKeys;

constexpr int KEYBOARD_MODS_HID_BEGIN = LeftControl;
constexpr int KEYBOARD_MODS_HID_END = NumKeyboardMods;
constexpr int NUM_KEYBOARD_MODS_HID = NumKeyboardMods;

} // namespace NativeKeyboard

using ButtonsRaw = std::array<std::string, NativeButton::NumButtons>;
using AnalogsRaw = std::array<std::string, NativeAnalog::NumAnalogs>;
using MotionRaw = std::array<std::string, NativeMotion::NumMotions>;
using MouseButtonsRaw = std::array<std::string, NativeMouseButton::NumMouseButtons>;
using KeyboardKeysRaw = std::array<std::string, NativeKeyboard::NumKeyboardKeys>;
using KeyboardModsRaw = std::array<std::string, NativeKeyboard::NumKeyboardMods>;

constexpr u32 JOYCON_BODY_NEON_RED = 0xFF3C28;
constexpr u32 JOYCON_BUTTONS_NEON_RED = 0x1E0A0A;
constexpr u32 JOYCON_BODY_NEON_BLUE = 0x0AB9E6;
constexpr u32 JOYCON_BUTTONS_NEON_BLUE = 0x001E1E;

enum class ControllerType {
    ProController,
    DualJoyconDetached,
    LeftJoycon,
    RightJoycon,
    Handheld,
};

struct PlayerInput {
    bool connected;
    ControllerType controller_type;
    ButtonsRaw buttons;
    AnalogsRaw analogs;
    MotionRaw motions;
    std::string lstick_mod;
    std::string rstick_mod;

    u32 body_color_left;
    u32 body_color_right;
    u32 button_color_left;
    u32 button_color_right;
};

struct TouchscreenInput {
    bool enabled;
    std::string device;

    u32 finger;
    u32 diameter_x;
    u32 diameter_y;
    u32 rotation_angle;
};
} // namespace Settings
