// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <QKeySequence>
#include <QSettings>
#include "common/file_util.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/hid/controllers/npad.h"
#include "input_common/main.h"
#include "input_common/udp/client.h"
#include "yuzu/configuration/config.h"

namespace FS = Common::FS;

Config::Config(const std::string& config_file, bool is_global) {
    // TODO: Don't hardcode the path; let the frontend decide where to put the config files.
    qt_config_loc = FS::GetUserPath(FS::UserPath::ConfigDir) + config_file;
    FS::CreateFullPath(qt_config_loc);
    qt_config =
        std::make_unique<QSettings>(QString::fromStdString(qt_config_loc), QSettings::IniFormat);
    global = is_global;
    Reload();
}

Config::~Config() {
    if (global) {
        Save();
    }
}

const std::array<int, Settings::NativeButton::NumButtons> Config::default_buttons = {
    Qt::Key_A, Qt::Key_S, Qt::Key_Z, Qt::Key_X, Qt::Key_3, Qt::Key_4, Qt::Key_Q,
    Qt::Key_W, Qt::Key_1, Qt::Key_2, Qt::Key_N, Qt::Key_M, Qt::Key_F, Qt::Key_T,
    Qt::Key_H, Qt::Key_G, Qt::Key_D, Qt::Key_C, Qt::Key_B, Qt::Key_V,
};

const std::array<int, Settings::NativeMotion::NumMotions> Config::default_motions = {
    Qt::Key_7,
    Qt::Key_8,
};

const std::array<std::array<int, 4>, Settings::NativeAnalog::NumAnalogs> Config::default_analogs{{
    {
        Qt::Key_Up,
        Qt::Key_Down,
        Qt::Key_Left,
        Qt::Key_Right,
    },
    {
        Qt::Key_I,
        Qt::Key_K,
        Qt::Key_J,
        Qt::Key_L,
    },
}};

const std::array<int, 2> Config::default_stick_mod = {
    Qt::Key_E,
    Qt::Key_R,
};

const std::array<int, Settings::NativeMouseButton::NumMouseButtons> Config::default_mouse_buttons =
    {
        Qt::Key_BracketLeft, Qt::Key_BracketRight, Qt::Key_Apostrophe, Qt::Key_Minus, Qt::Key_Equal,
};

const std::array<int, Settings::NativeKeyboard::NumKeyboardKeys> Config::default_keyboard_keys = {
    0,
    0,
    0,
    0,
    Qt::Key_A,
    Qt::Key_B,
    Qt::Key_C,
    Qt::Key_D,
    Qt::Key_E,
    Qt::Key_F,
    Qt::Key_G,
    Qt::Key_H,
    Qt::Key_I,
    Qt::Key_J,
    Qt::Key_K,
    Qt::Key_L,
    Qt::Key_M,
    Qt::Key_N,
    Qt::Key_O,
    Qt::Key_P,
    Qt::Key_Q,
    Qt::Key_R,
    Qt::Key_S,
    Qt::Key_T,
    Qt::Key_U,
    Qt::Key_V,
    Qt::Key_W,
    Qt::Key_X,
    Qt::Key_Y,
    Qt::Key_Z,
    Qt::Key_1,
    Qt::Key_2,
    Qt::Key_3,
    Qt::Key_4,
    Qt::Key_5,
    Qt::Key_6,
    Qt::Key_7,
    Qt::Key_8,
    Qt::Key_9,
    Qt::Key_0,
    Qt::Key_Enter,
    Qt::Key_Escape,
    Qt::Key_Backspace,
    Qt::Key_Tab,
    Qt::Key_Space,
    Qt::Key_Minus,
    Qt::Key_Equal,
    Qt::Key_BracketLeft,
    Qt::Key_BracketRight,
    Qt::Key_Backslash,
    Qt::Key_Dead_Tilde,
    Qt::Key_Semicolon,
    Qt::Key_Apostrophe,
    Qt::Key_Dead_Grave,
    Qt::Key_Comma,
    Qt::Key_Period,
    Qt::Key_Slash,
    Qt::Key_CapsLock,

    Qt::Key_F1,
    Qt::Key_F2,
    Qt::Key_F3,
    Qt::Key_F4,
    Qt::Key_F5,
    Qt::Key_F6,
    Qt::Key_F7,
    Qt::Key_F8,
    Qt::Key_F9,
    Qt::Key_F10,
    Qt::Key_F11,
    Qt::Key_F12,

    Qt::Key_SysReq,
    Qt::Key_ScrollLock,
    Qt::Key_Pause,
    Qt::Key_Insert,
    Qt::Key_Home,
    Qt::Key_PageUp,
    Qt::Key_Delete,
    Qt::Key_End,
    Qt::Key_PageDown,
    Qt::Key_Right,
    Qt::Key_Left,
    Qt::Key_Down,
    Qt::Key_Up,

    Qt::Key_NumLock,
    Qt::Key_Slash,
    Qt::Key_Asterisk,
    Qt::Key_Minus,
    Qt::Key_Plus,
    Qt::Key_Enter,
    Qt::Key_1,
    Qt::Key_2,
    Qt::Key_3,
    Qt::Key_4,
    Qt::Key_5,
    Qt::Key_6,
    Qt::Key_7,
    Qt::Key_8,
    Qt::Key_9,
    Qt::Key_0,
    Qt::Key_Period,

    0,
    0,
    Qt::Key_PowerOff,
    Qt::Key_Equal,

    Qt::Key_F13,
    Qt::Key_F14,
    Qt::Key_F15,
    Qt::Key_F16,
    Qt::Key_F17,
    Qt::Key_F18,
    Qt::Key_F19,
    Qt::Key_F20,
    Qt::Key_F21,
    Qt::Key_F22,
    Qt::Key_F23,
    Qt::Key_F24,

    Qt::Key_Open,
    Qt::Key_Help,
    Qt::Key_Menu,
    0,
    Qt::Key_Stop,
    Qt::Key_AudioRepeat,
    Qt::Key_Undo,
    Qt::Key_Cut,
    Qt::Key_Copy,
    Qt::Key_Paste,
    Qt::Key_Find,
    Qt::Key_VolumeMute,
    Qt::Key_VolumeUp,
    Qt::Key_VolumeDown,
    Qt::Key_CapsLock,
    Qt::Key_NumLock,
    Qt::Key_ScrollLock,
    Qt::Key_Comma,

    Qt::Key_ParenLeft,
    Qt::Key_ParenRight,
};

const std::array<int, Settings::NativeKeyboard::NumKeyboardMods> Config::default_keyboard_mods = {
    Qt::Key_Control, Qt::Key_Shift, Qt::Key_Alt,   Qt::Key_ApplicationLeft,
    Qt::Key_Control, Qt::Key_Shift, Qt::Key_AltGr, Qt::Key_ApplicationRight,
};

// This shouldn't have anything except static initializers (no functions). So
// QKeySequence(...).toString() is NOT ALLOWED HERE.
// This must be in alphabetical order according to action name as it must have the same order as
// UISetting::values.shortcuts, which is alphabetically ordered.
// clang-format off
const std::array<UISettings::Shortcut, 16> Config::default_hotkeys{{
    {QStringLiteral("Capture Screenshot"),       QStringLiteral("Main Window"), {QStringLiteral("Ctrl+P"), Qt::WidgetWithChildrenShortcut}},
    {QStringLiteral("Change Docked Mode"),       QStringLiteral("Main Window"), {QStringLiteral("F10"), Qt::ApplicationShortcut}},
    {QStringLiteral("Continue/Pause Emulation"), QStringLiteral("Main Window"), {QStringLiteral("F4"), Qt::WindowShortcut}},
    {QStringLiteral("Decrease Speed Limit"),     QStringLiteral("Main Window"), {QStringLiteral("-"), Qt::ApplicationShortcut}},
    {QStringLiteral("Exit Fullscreen"),          QStringLiteral("Main Window"), {QStringLiteral("Esc"), Qt::WindowShortcut}},
    {QStringLiteral("Exit yuzu"),                QStringLiteral("Main Window"), {QStringLiteral("Ctrl+Q"), Qt::WindowShortcut}},
    {QStringLiteral("Fullscreen"),               QStringLiteral("Main Window"), {QStringLiteral("F11"), Qt::WindowShortcut}},
    {QStringLiteral("Increase Speed Limit"),     QStringLiteral("Main Window"), {QStringLiteral("+"), Qt::ApplicationShortcut}},
    {QStringLiteral("Load Amiibo"),              QStringLiteral("Main Window"), {QStringLiteral("F2"), Qt::WidgetWithChildrenShortcut}},
    {QStringLiteral("Load File"),                QStringLiteral("Main Window"), {QStringLiteral("Ctrl+O"), Qt::WidgetWithChildrenShortcut}},
    {QStringLiteral("Mute Audio"),               QStringLiteral("Main Window"), {QStringLiteral("Ctrl+M"), Qt::WindowShortcut}},
    {QStringLiteral("Restart Emulation"),        QStringLiteral("Main Window"), {QStringLiteral("F6"), Qt::WindowShortcut}},
    {QStringLiteral("Stop Emulation"),           QStringLiteral("Main Window"), {QStringLiteral("F5"), Qt::WindowShortcut}},
    {QStringLiteral("Toggle Filter Bar"),        QStringLiteral("Main Window"), {QStringLiteral("Ctrl+F"), Qt::WindowShortcut}},
    {QStringLiteral("Toggle Speed Limit"),       QStringLiteral("Main Window"), {QStringLiteral("Ctrl+Z"), Qt::ApplicationShortcut}},
    {QStringLiteral("Toggle Status Bar"),        QStringLiteral("Main Window"), {QStringLiteral("Ctrl+S"), Qt::WindowShortcut}},
}};
// clang-format on

void Config::ReadPlayerValues() {
    for (std::size_t p = 0; p < Settings::values.players.size(); ++p) {
        auto& player = Settings::values.players[p];

        player.connected =
            ReadSetting(QStringLiteral("player_%1_connected").arg(p), false).toBool();

        player.controller_type = static_cast<Settings::ControllerType>(
            qt_config
                ->value(QStringLiteral("player_%1_type").arg(p),
                        static_cast<u8>(Settings::ControllerType::ProController))
                .toUInt());

        player.body_color_left = qt_config
                                     ->value(QStringLiteral("player_%1_body_color_left").arg(p),
                                             Settings::JOYCON_BODY_NEON_BLUE)
                                     .toUInt();
        player.body_color_right = qt_config
                                      ->value(QStringLiteral("player_%1_body_color_right").arg(p),
                                              Settings::JOYCON_BODY_NEON_RED)
                                      .toUInt();
        player.button_color_left = qt_config
                                       ->value(QStringLiteral("player_%1_button_color_left").arg(p),
                                               Settings::JOYCON_BUTTONS_NEON_BLUE)
                                       .toUInt();
        player.button_color_right =
            qt_config
                ->value(QStringLiteral("player_%1_button_color_right").arg(p),
                        Settings::JOYCON_BUTTONS_NEON_RED)
                .toUInt();

        for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
            const std::string default_param =
                InputCommon::GenerateKeyboardParam(default_buttons[i]);
            auto& player_buttons = player.buttons[i];

            player_buttons = qt_config
                                 ->value(QStringLiteral("player_%1_").arg(p) +
                                             QString::fromUtf8(Settings::NativeButton::mapping[i]),
                                         QString::fromStdString(default_param))
                                 .toString()
                                 .toStdString();
            if (player_buttons.empty()) {
                player_buttons = default_param;
            }
        }

        for (int i = 0; i < Settings::NativeMotion::NumMotions; ++i) {
            const std::string default_param =
                InputCommon::GenerateKeyboardParam(default_motions[i]);
            auto& player_motions = player.motions[i];

            player_motions = qt_config
                                 ->value(QStringLiteral("player_%1_").arg(p) +
                                             QString::fromUtf8(Settings::NativeMotion::mapping[i]),
                                         QString::fromStdString(default_param))
                                 .toString()
                                 .toStdString();
            if (player_motions.empty()) {
                player_motions = default_param;
            }
        }

        for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
            const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
                default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
                default_analogs[i][3], default_stick_mod[i], 0.5f);
            auto& player_analogs = player.analogs[i];

            player_analogs = qt_config
                                 ->value(QStringLiteral("player_%1_").arg(p) +
                                             QString::fromUtf8(Settings::NativeAnalog::mapping[i]),
                                         QString::fromStdString(default_param))
                                 .toString()
                                 .toStdString();
            if (player_analogs.empty()) {
                player_analogs = default_param;
            }
        }
    }
}

void Config::ReadDebugValues() {
    Settings::values.debug_pad_enabled =
        ReadSetting(QStringLiteral("debug_pad_enabled"), false).toBool();

    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        auto& debug_pad_buttons = Settings::values.debug_pad_buttons[i];

        debug_pad_buttons = qt_config
                                ->value(QStringLiteral("debug_pad_") +
                                            QString::fromUtf8(Settings::NativeButton::mapping[i]),
                                        QString::fromStdString(default_param))
                                .toString()
                                .toStdString();
        if (debug_pad_buttons.empty()) {
            debug_pad_buttons = default_param;
        }
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_stick_mod[i], 0.5f);
        auto& debug_pad_analogs = Settings::values.debug_pad_analogs[i];

        debug_pad_analogs = qt_config
                                ->value(QStringLiteral("debug_pad_") +
                                            QString::fromUtf8(Settings::NativeAnalog::mapping[i]),
                                        QString::fromStdString(default_param))
                                .toString()
                                .toStdString();
        if (debug_pad_analogs.empty()) {
            debug_pad_analogs = default_param;
        }
    }
}

void Config::ReadKeyboardValues() {
    Settings::values.keyboard_enabled =
        ReadSetting(QStringLiteral("keyboard_enabled"), false).toBool();

    std::transform(default_keyboard_keys.begin(), default_keyboard_keys.end(),
                   Settings::values.keyboard_keys.begin(), InputCommon::GenerateKeyboardParam);
    std::transform(default_keyboard_mods.begin(), default_keyboard_mods.end(),
                   Settings::values.keyboard_keys.begin() +
                       Settings::NativeKeyboard::LeftControlKey,
                   InputCommon::GenerateKeyboardParam);
    std::transform(default_keyboard_mods.begin(), default_keyboard_mods.end(),
                   Settings::values.keyboard_mods.begin(), InputCommon::GenerateKeyboardParam);
}

void Config::ReadMouseValues() {
    Settings::values.mouse_enabled = ReadSetting(QStringLiteral("mouse_enabled"), false).toBool();

    for (int i = 0; i < Settings::NativeMouseButton::NumMouseButtons; ++i) {
        const std::string default_param =
            InputCommon::GenerateKeyboardParam(default_mouse_buttons[i]);
        auto& mouse_buttons = Settings::values.mouse_buttons[i];

        mouse_buttons = qt_config
                            ->value(QStringLiteral("mouse_") +
                                        QString::fromUtf8(Settings::NativeMouseButton::mapping[i]),
                                    QString::fromStdString(default_param))
                            .toString()
                            .toStdString();
        if (mouse_buttons.empty()) {
            mouse_buttons = default_param;
        }
    }
}

void Config::ReadTouchscreenValues() {
    Settings::values.touchscreen.enabled =
        ReadSetting(QStringLiteral("touchscreen_enabled"), true).toBool();
    Settings::values.touchscreen.device =
        ReadSetting(QStringLiteral("touchscreen_device"), QStringLiteral("engine:emu_window"))
            .toString()
            .toStdString();

    Settings::values.touchscreen.finger =
        ReadSetting(QStringLiteral("touchscreen_finger"), 0).toUInt();
    Settings::values.touchscreen.rotation_angle =
        ReadSetting(QStringLiteral("touchscreen_angle"), 0).toUInt();
    Settings::values.touchscreen.diameter_x =
        ReadSetting(QStringLiteral("touchscreen_diameter_x"), 15).toUInt();
    Settings::values.touchscreen.diameter_y =
        ReadSetting(QStringLiteral("touchscreen_diameter_y"), 15).toUInt();
}

void Config::ReadAudioValues() {
    qt_config->beginGroup(QStringLiteral("Audio"));

    if (global) {
        Settings::values.sink_id =
            ReadSetting(QStringLiteral("output_engine"), QStringLiteral("auto"))
                .toString()
                .toStdString();
        Settings::values.audio_device_id =
            ReadSetting(QStringLiteral("output_device"), QStringLiteral("auto"))
                .toString()
                .toStdString();
    }
    ReadSettingGlobal(Settings::values.enable_audio_stretching,
                      QStringLiteral("enable_audio_stretching"), true);
    ReadSettingGlobal(Settings::values.volume, QStringLiteral("volume"), 1);

    qt_config->endGroup();
}

void Config::ReadControlValues() {
    qt_config->beginGroup(QStringLiteral("Controls"));

    ReadPlayerValues();
    ReadDebugValues();
    ReadKeyboardValues();
    ReadMouseValues();
    ReadTouchscreenValues();
    ReadMotionTouchValues();

    Settings::values.vibration_enabled =
        ReadSetting(QStringLiteral("vibration_enabled"), true).toBool();
    Settings::values.motion_enabled = ReadSetting(QStringLiteral("motion_enabled"), true).toBool();
    Settings::values.use_docked_mode =
        ReadSetting(QStringLiteral("use_docked_mode"), false).toBool();

    qt_config->endGroup();
}

void Config::ReadMotionTouchValues() {
    int num_touch_from_button_maps =
        qt_config->beginReadArray(QStringLiteral("touch_from_button_maps"));

    if (num_touch_from_button_maps > 0) {
        const auto append_touch_from_button_map = [this] {
            Settings::TouchFromButtonMap map;
            map.name = ReadSetting(QStringLiteral("name"), QStringLiteral("default"))
                           .toString()
                           .toStdString();
            const int num_touch_maps = qt_config->beginReadArray(QStringLiteral("entries"));
            map.buttons.reserve(num_touch_maps);
            for (int i = 0; i < num_touch_maps; i++) {
                qt_config->setArrayIndex(i);
                std::string touch_mapping =
                    ReadSetting(QStringLiteral("bind")).toString().toStdString();
                map.buttons.emplace_back(std::move(touch_mapping));
            }
            qt_config->endArray(); // entries
            Settings::values.touch_from_button_maps.emplace_back(std::move(map));
        };

        for (int i = 0; i < num_touch_from_button_maps; ++i) {
            qt_config->setArrayIndex(i);
            append_touch_from_button_map();
        }
    } else {
        Settings::values.touch_from_button_maps.emplace_back(
            Settings::TouchFromButtonMap{"default", {}});
        num_touch_from_button_maps = 1;
    }
    qt_config->endArray();

    Settings::values.motion_device =
        ReadSetting(QStringLiteral("motion_device"),
                    QStringLiteral("engine:motion_emu,update_period:100,sensitivity:0.01"))
            .toString()
            .toStdString();
    Settings::values.touch_device =
        ReadSetting(QStringLiteral("touch_device"), QStringLiteral("engine:emu_window"))
            .toString()
            .toStdString();
    Settings::values.use_touch_from_button =
        ReadSetting(QStringLiteral("use_touch_from_button"), false).toBool();
    Settings::values.touch_from_button_map_index =
        ReadSetting(QStringLiteral("touch_from_button_map"), 0).toInt();
    Settings::values.touch_from_button_map_index =
        std::clamp(Settings::values.touch_from_button_map_index, 0, num_touch_from_button_maps - 1);
    Settings::values.udp_input_address =
        ReadSetting(QStringLiteral("udp_input_address"),
                    QString::fromUtf8(InputCommon::CemuhookUDP::DEFAULT_ADDR))
            .toString()
            .toStdString();
    Settings::values.udp_input_port = static_cast<u16>(
        ReadSetting(QStringLiteral("udp_input_port"), InputCommon::CemuhookUDP::DEFAULT_PORT)
            .toInt());
    Settings::values.udp_pad_index =
        static_cast<u8>(ReadSetting(QStringLiteral("udp_pad_index"), 0).toUInt());
}

void Config::ReadCoreValues() {
    qt_config->beginGroup(QStringLiteral("Core"));

    ReadSettingGlobal(Settings::values.use_multi_core, QStringLiteral("use_multi_core"), false);

    qt_config->endGroup();
}

void Config::ReadDataStorageValues() {
    qt_config->beginGroup(QStringLiteral("Data Storage"));

    Settings::values.use_virtual_sd = ReadSetting(QStringLiteral("use_virtual_sd"), true).toBool();
    FS::GetUserPath(FS::UserPath::NANDDir,
                    qt_config
                        ->value(QStringLiteral("nand_directory"),
                                QString::fromStdString(FS::GetUserPath(FS::UserPath::NANDDir)))
                        .toString()
                        .toStdString());
    FS::GetUserPath(FS::UserPath::SDMCDir,
                    qt_config
                        ->value(QStringLiteral("sdmc_directory"),
                                QString::fromStdString(FS::GetUserPath(FS::UserPath::SDMCDir)))
                        .toString()
                        .toStdString());
    FS::GetUserPath(FS::UserPath::LoadDir,
                    qt_config
                        ->value(QStringLiteral("load_directory"),
                                QString::fromStdString(FS::GetUserPath(FS::UserPath::LoadDir)))
                        .toString()
                        .toStdString());
    FS::GetUserPath(FS::UserPath::DumpDir,
                    qt_config
                        ->value(QStringLiteral("dump_directory"),
                                QString::fromStdString(FS::GetUserPath(FS::UserPath::DumpDir)))
                        .toString()
                        .toStdString());
    FS::GetUserPath(FS::UserPath::CacheDir,
                    qt_config
                        ->value(QStringLiteral("cache_directory"),
                                QString::fromStdString(FS::GetUserPath(FS::UserPath::CacheDir)))
                        .toString()
                        .toStdString());
    Settings::values.gamecard_inserted =
        ReadSetting(QStringLiteral("gamecard_inserted"), false).toBool();
    Settings::values.gamecard_current_game =
        ReadSetting(QStringLiteral("gamecard_current_game"), false).toBool();
    Settings::values.gamecard_path =
        ReadSetting(QStringLiteral("gamecard_path"), QString{}).toString().toStdString();

    qt_config->endGroup();
}

void Config::ReadDebuggingValues() {
    qt_config->beginGroup(QStringLiteral("Debugging"));

    // Intentionally not using the QT default setting as this is intended to be changed in the ini
    Settings::values.record_frame_times =
        qt_config->value(QStringLiteral("record_frame_times"), false).toBool();
    Settings::values.use_gdbstub = ReadSetting(QStringLiteral("use_gdbstub"), false).toBool();
    Settings::values.gdbstub_port = ReadSetting(QStringLiteral("gdbstub_port"), 24689).toInt();
    Settings::values.program_args =
        ReadSetting(QStringLiteral("program_args"), QString{}).toString().toStdString();
    Settings::values.dump_exefs = ReadSetting(QStringLiteral("dump_exefs"), false).toBool();
    Settings::values.dump_nso = ReadSetting(QStringLiteral("dump_nso"), false).toBool();
    Settings::values.reporting_services =
        ReadSetting(QStringLiteral("reporting_services"), false).toBool();
    Settings::values.quest_flag = ReadSetting(QStringLiteral("quest_flag"), false).toBool();
    Settings::values.disable_macro_jit =
        ReadSetting(QStringLiteral("disable_macro_jit"), false).toBool();

    qt_config->endGroup();
}

void Config::ReadServiceValues() {
    qt_config->beginGroup(QStringLiteral("Services"));
    Settings::values.bcat_backend =
        ReadSetting(QStringLiteral("bcat_backend"), QStringLiteral("null"))
            .toString()
            .toStdString();
    Settings::values.bcat_boxcat_local =
        ReadSetting(QStringLiteral("bcat_boxcat_local"), false).toBool();
    qt_config->endGroup();
}

void Config::ReadDisabledAddOnValues() {
    const auto size = qt_config->beginReadArray(QStringLiteral("DisabledAddOns"));

    for (int i = 0; i < size; ++i) {
        qt_config->setArrayIndex(i);
        const auto title_id = ReadSetting(QStringLiteral("title_id"), 0).toULongLong();
        std::vector<std::string> out;
        const auto d_size = qt_config->beginReadArray(QStringLiteral("disabled"));
        for (int j = 0; j < d_size; ++j) {
            qt_config->setArrayIndex(j);
            out.push_back(ReadSetting(QStringLiteral("d"), QString{}).toString().toStdString());
        }
        qt_config->endArray();
        Settings::values.disabled_addons.insert_or_assign(title_id, out);
    }

    qt_config->endArray();
}

void Config::ReadMiscellaneousValues() {
    qt_config->beginGroup(QStringLiteral("Miscellaneous"));

    Settings::values.log_filter =
        ReadSetting(QStringLiteral("log_filter"), QStringLiteral("*:Info"))
            .toString()
            .toStdString();
    Settings::values.use_dev_keys = ReadSetting(QStringLiteral("use_dev_keys"), false).toBool();

    qt_config->endGroup();
}

void Config::ReadPathValues() {
    qt_config->beginGroup(QStringLiteral("Paths"));

    UISettings::values.roms_path = ReadSetting(QStringLiteral("romsPath")).toString();
    UISettings::values.symbols_path = ReadSetting(QStringLiteral("symbolsPath")).toString();
    UISettings::values.game_dir_deprecated =
        ReadSetting(QStringLiteral("gameListRootDir"), QStringLiteral(".")).toString();
    UISettings::values.game_dir_deprecated_deepscan =
        ReadSetting(QStringLiteral("gameListDeepScan"), false).toBool();
    const int gamedirs_size = qt_config->beginReadArray(QStringLiteral("gamedirs"));
    for (int i = 0; i < gamedirs_size; ++i) {
        qt_config->setArrayIndex(i);
        UISettings::GameDir game_dir;
        game_dir.path = ReadSetting(QStringLiteral("path")).toString();
        game_dir.deep_scan = ReadSetting(QStringLiteral("deep_scan"), false).toBool();
        game_dir.expanded = ReadSetting(QStringLiteral("expanded"), true).toBool();
        UISettings::values.game_dirs.append(game_dir);
    }
    qt_config->endArray();
    // create NAND and SD card directories if empty, these are not removable through the UI,
    // also carries over old game list settings if present
    if (UISettings::values.game_dirs.isEmpty()) {
        UISettings::GameDir game_dir;
        game_dir.path = QStringLiteral("SDMC");
        game_dir.expanded = true;
        UISettings::values.game_dirs.append(game_dir);
        game_dir.path = QStringLiteral("UserNAND");
        UISettings::values.game_dirs.append(game_dir);
        game_dir.path = QStringLiteral("SysNAND");
        UISettings::values.game_dirs.append(game_dir);
        if (UISettings::values.game_dir_deprecated != QStringLiteral(".")) {
            game_dir.path = UISettings::values.game_dir_deprecated;
            game_dir.deep_scan = UISettings::values.game_dir_deprecated_deepscan;
            UISettings::values.game_dirs.append(game_dir);
        }
    }
    UISettings::values.recent_files = ReadSetting(QStringLiteral("recentFiles")).toStringList();
    UISettings::values.language = ReadSetting(QStringLiteral("language"), QString{}).toString();

    qt_config->endGroup();
}

void Config::ReadCpuValues() {
    qt_config->beginGroup(QStringLiteral("Cpu"));

    if (global) {
        Settings::values.cpu_accuracy = static_cast<Settings::CPUAccuracy>(
            ReadSetting(QStringLiteral("cpu_accuracy"), 0).toInt());

        Settings::values.cpuopt_page_tables =
            ReadSetting(QStringLiteral("cpuopt_page_tables"), true).toBool();
        Settings::values.cpuopt_block_linking =
            ReadSetting(QStringLiteral("cpuopt_block_linking"), true).toBool();
        Settings::values.cpuopt_return_stack_buffer =
            ReadSetting(QStringLiteral("cpuopt_return_stack_buffer"), true).toBool();
        Settings::values.cpuopt_fast_dispatcher =
            ReadSetting(QStringLiteral("cpuopt_fast_dispatcher"), true).toBool();
        Settings::values.cpuopt_context_elimination =
            ReadSetting(QStringLiteral("cpuopt_context_elimination"), true).toBool();
        Settings::values.cpuopt_const_prop =
            ReadSetting(QStringLiteral("cpuopt_const_prop"), true).toBool();
        Settings::values.cpuopt_misc_ir =
            ReadSetting(QStringLiteral("cpuopt_misc_ir"), true).toBool();
        Settings::values.cpuopt_reduce_misalign_checks =
            ReadSetting(QStringLiteral("cpuopt_reduce_misalign_checks"), true).toBool();

        Settings::values.cpuopt_unsafe_unfuse_fma =
            ReadSetting(QStringLiteral("cpuopt_unsafe_unfuse_fma"), true).toBool();
        Settings::values.cpuopt_unsafe_reduce_fp_error =
            ReadSetting(QStringLiteral("cpuopt_unsafe_reduce_fp_error"), true).toBool();
    }

    qt_config->endGroup();
}

void Config::ReadRendererValues() {
    qt_config->beginGroup(QStringLiteral("Renderer"));

    ReadSettingGlobal(Settings::values.renderer_backend, QStringLiteral("backend"), 0);
    ReadSettingGlobal(Settings::values.renderer_debug, QStringLiteral("debug"), false);
    ReadSettingGlobal(Settings::values.vulkan_device, QStringLiteral("vulkan_device"), 0);
    ReadSettingGlobal(Settings::values.aspect_ratio, QStringLiteral("aspect_ratio"), 0);
    ReadSettingGlobal(Settings::values.max_anisotropy, QStringLiteral("max_anisotropy"), 0);
    ReadSettingGlobal(Settings::values.use_frame_limit, QStringLiteral("use_frame_limit"), true);
    ReadSettingGlobal(Settings::values.frame_limit, QStringLiteral("frame_limit"), 100);
    ReadSettingGlobal(Settings::values.use_disk_shader_cache,
                      QStringLiteral("use_disk_shader_cache"), true);
    ReadSettingGlobal(Settings::values.gpu_accuracy, QStringLiteral("gpu_accuracy"), 0);
    ReadSettingGlobal(Settings::values.use_asynchronous_gpu_emulation,
                      QStringLiteral("use_asynchronous_gpu_emulation"), false);
    ReadSettingGlobal(Settings::values.use_vsync, QStringLiteral("use_vsync"), true);
    ReadSettingGlobal(Settings::values.use_assembly_shaders, QStringLiteral("use_assembly_shaders"),
                      false);
    ReadSettingGlobal(Settings::values.use_asynchronous_shaders,
                      QStringLiteral("use_asynchronous_shaders"), false);
    ReadSettingGlobal(Settings::values.use_fast_gpu_time, QStringLiteral("use_fast_gpu_time"),
                      true);
    ReadSettingGlobal(Settings::values.bg_red, QStringLiteral("bg_red"), 0.0);
    ReadSettingGlobal(Settings::values.bg_green, QStringLiteral("bg_green"), 0.0);
    ReadSettingGlobal(Settings::values.bg_blue, QStringLiteral("bg_blue"), 0.0);

    qt_config->endGroup();
}

void Config::ReadScreenshotValues() {
    qt_config->beginGroup(QStringLiteral("Screenshots"));

    UISettings::values.enable_screenshot_save_as =
        ReadSetting(QStringLiteral("enable_screenshot_save_as"), true).toBool();
    FS::GetUserPath(
        FS::UserPath::ScreenshotsDir,
        qt_config
            ->value(QStringLiteral("screenshot_path"),
                    QString::fromStdString(FS::GetUserPath(FS::UserPath::ScreenshotsDir)))
            .toString()
            .toStdString());

    qt_config->endGroup();
}

void Config::ReadShortcutValues() {
    qt_config->beginGroup(QStringLiteral("Shortcuts"));

    for (const auto& [name, group, shortcut] : default_hotkeys) {
        const auto& [keyseq, context] = shortcut;
        qt_config->beginGroup(group);
        qt_config->beginGroup(name);
        // No longer using ReadSetting for shortcut.second as it innacurately returns a value of 1
        // for WidgetWithChildrenShortcut which is a value of 3. Needed to fix shortcuts the open
        // a file dialog in windowed mode
        UISettings::values.shortcuts.push_back(
            {name,
             group,
             {ReadSetting(QStringLiteral("KeySeq"), keyseq).toString(), shortcut.second}});
        qt_config->endGroup();
        qt_config->endGroup();
    }

    qt_config->endGroup();
}

void Config::ReadSystemValues() {
    qt_config->beginGroup(QStringLiteral("System"));

    ReadSettingGlobal(Settings::values.current_user, QStringLiteral("current_user"), 0);
    Settings::values.current_user =
        std::clamp<int>(Settings::values.current_user, 0, Service::Account::MAX_USERS - 1);

    ReadSettingGlobal(Settings::values.language_index, QStringLiteral("language_index"), 1);

    ReadSettingGlobal(Settings::values.region_index, QStringLiteral("region_index"), 1);

    ReadSettingGlobal(Settings::values.time_zone_index, QStringLiteral("time_zone_index"), 0);

    bool rng_seed_enabled;
    ReadSettingGlobal(rng_seed_enabled, QStringLiteral("rng_seed_enabled"), false);
    bool rng_seed_global =
        global || qt_config->value(QStringLiteral("rng_seed/use_global"), true).toBool();
    Settings::values.rng_seed.SetGlobal(rng_seed_global);
    if (global || !rng_seed_global) {
        if (rng_seed_enabled) {
            Settings::values.rng_seed.SetValue(
                ReadSetting(QStringLiteral("rng_seed"), 0).toULongLong());
        } else {
            Settings::values.rng_seed.SetValue(std::nullopt);
        }
    }

    bool custom_rtc_enabled;
    ReadSettingGlobal(custom_rtc_enabled, QStringLiteral("custom_rtc_enabled"), false);
    bool custom_rtc_global =
        global || qt_config->value(QStringLiteral("custom_rtc/use_global"), true).toBool();
    Settings::values.custom_rtc.SetGlobal(custom_rtc_global);
    if (global || !custom_rtc_global) {
        if (custom_rtc_enabled) {
            Settings::values.custom_rtc.SetValue(
                std::chrono::seconds(ReadSetting(QStringLiteral("custom_rtc"), 0).toULongLong()));
        } else {
            Settings::values.custom_rtc.SetValue(std::nullopt);
        }
    }

    ReadSettingGlobal(Settings::values.sound_index, QStringLiteral("sound_index"), 1);

    qt_config->endGroup();
}

void Config::ReadUIValues() {
    qt_config->beginGroup(QStringLiteral("UI"));

    UISettings::values.theme =
        ReadSetting(QStringLiteral("theme"), QString::fromUtf8(UISettings::themes[0].second))
            .toString();
    UISettings::values.enable_discord_presence =
        ReadSetting(QStringLiteral("enable_discord_presence"), true).toBool();
    UISettings::values.select_user_on_boot =
        ReadSetting(QStringLiteral("select_user_on_boot"), false).toBool();

    ReadUIGamelistValues();
    ReadUILayoutValues();
    ReadPathValues();
    ReadScreenshotValues();
    ReadShortcutValues();

    UISettings::values.single_window_mode =
        ReadSetting(QStringLiteral("singleWindowMode"), true).toBool();
    UISettings::values.fullscreen = ReadSetting(QStringLiteral("fullscreen"), false).toBool();
    UISettings::values.display_titlebar =
        ReadSetting(QStringLiteral("displayTitleBars"), true).toBool();
    UISettings::values.show_filter_bar =
        ReadSetting(QStringLiteral("showFilterBar"), true).toBool();
    UISettings::values.show_status_bar =
        ReadSetting(QStringLiteral("showStatusBar"), true).toBool();
    UISettings::values.confirm_before_closing =
        ReadSetting(QStringLiteral("confirmClose"), true).toBool();
    UISettings::values.first_start = ReadSetting(QStringLiteral("firstStart"), true).toBool();
    UISettings::values.callout_flags = ReadSetting(QStringLiteral("calloutFlags"), 0).toUInt();
    UISettings::values.show_console = ReadSetting(QStringLiteral("showConsole"), false).toBool();
    UISettings::values.pause_when_in_background =
        ReadSetting(QStringLiteral("pauseWhenInBackground"), false).toBool();
    UISettings::values.hide_mouse =
        ReadSetting(QStringLiteral("hideInactiveMouse"), false).toBool();

    qt_config->endGroup();
}

void Config::ReadUIGamelistValues() {
    qt_config->beginGroup(QStringLiteral("UIGameList"));

    UISettings::values.show_add_ons = ReadSetting(QStringLiteral("show_add_ons"), true).toBool();
    UISettings::values.icon_size = ReadSetting(QStringLiteral("icon_size"), 64).toUInt();
    UISettings::values.row_1_text_id = ReadSetting(QStringLiteral("row_1_text_id"), 3).toUInt();
    UISettings::values.row_2_text_id = ReadSetting(QStringLiteral("row_2_text_id"), 2).toUInt();
    UISettings::values.cache_game_list =
        ReadSetting(QStringLiteral("cache_game_list"), true).toBool();

    qt_config->endGroup();
}

void Config::ReadUILayoutValues() {
    qt_config->beginGroup(QStringLiteral("UILayout"));

    UISettings::values.geometry = ReadSetting(QStringLiteral("geometry")).toByteArray();
    UISettings::values.state = ReadSetting(QStringLiteral("state")).toByteArray();
    UISettings::values.renderwindow_geometry =
        ReadSetting(QStringLiteral("geometryRenderWindow")).toByteArray();
    UISettings::values.gamelist_header_state =
        ReadSetting(QStringLiteral("gameListHeaderState")).toByteArray();
    UISettings::values.microprofile_geometry =
        ReadSetting(QStringLiteral("microProfileDialogGeometry")).toByteArray();
    UISettings::values.microprofile_visible =
        ReadSetting(QStringLiteral("microProfileDialogVisible"), false).toBool();

    qt_config->endGroup();
}

void Config::ReadWebServiceValues() {
    qt_config->beginGroup(QStringLiteral("WebService"));

    Settings::values.enable_telemetry =
        ReadSetting(QStringLiteral("enable_telemetry"), true).toBool();
    Settings::values.web_api_url =
        ReadSetting(QStringLiteral("web_api_url"), QStringLiteral("https://api.yuzu-emu.org"))
            .toString()
            .toStdString();
    Settings::values.yuzu_username =
        ReadSetting(QStringLiteral("yuzu_username")).toString().toStdString();
    Settings::values.yuzu_token =
        ReadSetting(QStringLiteral("yuzu_token")).toString().toStdString();

    qt_config->endGroup();
}

void Config::ReadValues() {
    if (global) {
        ReadControlValues();
        ReadDataStorageValues();
        ReadDebuggingValues();
        ReadDisabledAddOnValues();
        ReadServiceValues();
        ReadUIValues();
        ReadWebServiceValues();
        ReadMiscellaneousValues();
    }
    ReadCoreValues();
    ReadCpuValues();
    ReadRendererValues();
    ReadAudioValues();
    ReadSystemValues();
}

void Config::SavePlayerValues() {
    for (std::size_t p = 0; p < Settings::values.players.size(); ++p) {
        const auto& player = Settings::values.players[p];

        WriteSetting(QStringLiteral("player_%1_connected").arg(p), player.connected, false);
        WriteSetting(QStringLiteral("player_%1_type").arg(p),
                     static_cast<u8>(player.controller_type),
                     static_cast<u8>(Settings::ControllerType::ProController));

        WriteSetting(QStringLiteral("player_%1_body_color_left").arg(p), player.body_color_left,
                     Settings::JOYCON_BODY_NEON_BLUE);
        WriteSetting(QStringLiteral("player_%1_body_color_right").arg(p), player.body_color_right,
                     Settings::JOYCON_BODY_NEON_RED);
        WriteSetting(QStringLiteral("player_%1_button_color_left").arg(p), player.button_color_left,
                     Settings::JOYCON_BUTTONS_NEON_BLUE);
        WriteSetting(QStringLiteral("player_%1_button_color_right").arg(p),
                     player.button_color_right, Settings::JOYCON_BUTTONS_NEON_RED);

        for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
            const std::string default_param =
                InputCommon::GenerateKeyboardParam(default_buttons[i]);
            WriteSetting(QStringLiteral("player_%1_").arg(p) +
                             QString::fromStdString(Settings::NativeButton::mapping[i]),
                         QString::fromStdString(player.buttons[i]),
                         QString::fromStdString(default_param));
        }
        for (int i = 0; i < Settings::NativeMotion::NumMotions; ++i) {
            const std::string default_param =
                InputCommon::GenerateKeyboardParam(default_motions[i]);
            WriteSetting(QStringLiteral("player_%1_").arg(p) +
                             QString::fromStdString(Settings::NativeMotion::mapping[i]),
                         QString::fromStdString(player.motions[i]),
                         QString::fromStdString(default_param));
        }
        for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
            const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
                default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
                default_analogs[i][3], default_stick_mod[i], 0.5f);
            WriteSetting(QStringLiteral("player_%1_").arg(p) +
                             QString::fromStdString(Settings::NativeAnalog::mapping[i]),
                         QString::fromStdString(player.analogs[i]),
                         QString::fromStdString(default_param));
        }
    }
}

void Config::SaveDebugValues() {
    WriteSetting(QStringLiteral("debug_pad_enabled"), Settings::values.debug_pad_enabled, false);
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        WriteSetting(QStringLiteral("debug_pad_") +
                         QString::fromStdString(Settings::NativeButton::mapping[i]),
                     QString::fromStdString(Settings::values.debug_pad_buttons[i]),
                     QString::fromStdString(default_param));
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_stick_mod[i], 0.5f);
        WriteSetting(QStringLiteral("debug_pad_") +
                         QString::fromStdString(Settings::NativeAnalog::mapping[i]),
                     QString::fromStdString(Settings::values.debug_pad_analogs[i]),
                     QString::fromStdString(default_param));
    }
}

void Config::SaveMouseValues() {
    WriteSetting(QStringLiteral("mouse_enabled"), Settings::values.mouse_enabled, false);

    for (int i = 0; i < Settings::NativeMouseButton::NumMouseButtons; ++i) {
        const std::string default_param =
            InputCommon::GenerateKeyboardParam(default_mouse_buttons[i]);
        WriteSetting(QStringLiteral("mouse_") +
                         QString::fromStdString(Settings::NativeMouseButton::mapping[i]),
                     QString::fromStdString(Settings::values.mouse_buttons[i]),
                     QString::fromStdString(default_param));
    }
}

void Config::SaveTouchscreenValues() {
    const auto& touchscreen = Settings::values.touchscreen;

    WriteSetting(QStringLiteral("touchscreen_enabled"), touchscreen.enabled, true);
    WriteSetting(QStringLiteral("touchscreen_device"), QString::fromStdString(touchscreen.device),
                 QStringLiteral("engine:emu_window"));

    WriteSetting(QStringLiteral("touchscreen_finger"), touchscreen.finger, 0);
    WriteSetting(QStringLiteral("touchscreen_angle"), touchscreen.rotation_angle, 0);
    WriteSetting(QStringLiteral("touchscreen_diameter_x"), touchscreen.diameter_x, 15);
    WriteSetting(QStringLiteral("touchscreen_diameter_y"), touchscreen.diameter_y, 15);
}

void Config::SaveMotionTouchValues() {
    WriteSetting(QStringLiteral("motion_device"),
                 QString::fromStdString(Settings::values.motion_device),
                 QStringLiteral("engine:motion_emu,update_period:100,sensitivity:0.01"));
    WriteSetting(QStringLiteral("touch_device"),
                 QString::fromStdString(Settings::values.touch_device),
                 QStringLiteral("engine:emu_window"));
    WriteSetting(QStringLiteral("use_touch_from_button"), Settings::values.use_touch_from_button,
                 false);
    WriteSetting(QStringLiteral("touch_from_button_map"),
                 Settings::values.touch_from_button_map_index, 0);
    WriteSetting(QStringLiteral("udp_input_address"),
                 QString::fromStdString(Settings::values.udp_input_address),
                 QString::fromUtf8(InputCommon::CemuhookUDP::DEFAULT_ADDR));
    WriteSetting(QStringLiteral("udp_input_port"), Settings::values.udp_input_port,
                 InputCommon::CemuhookUDP::DEFAULT_PORT);
    WriteSetting(QStringLiteral("udp_pad_index"), Settings::values.udp_pad_index, 0);

    qt_config->beginWriteArray(QStringLiteral("touch_from_button_maps"));
    for (std::size_t p = 0; p < Settings::values.touch_from_button_maps.size(); ++p) {
        qt_config->setArrayIndex(static_cast<int>(p));
        WriteSetting(QStringLiteral("name"),
                     QString::fromStdString(Settings::values.touch_from_button_maps[p].name),
                     QStringLiteral("default"));
        qt_config->beginWriteArray(QStringLiteral("entries"));
        for (std::size_t q = 0; q < Settings::values.touch_from_button_maps[p].buttons.size();
             ++q) {
            qt_config->setArrayIndex(static_cast<int>(q));
            WriteSetting(
                QStringLiteral("bind"),
                QString::fromStdString(Settings::values.touch_from_button_maps[p].buttons[q]));
        }
        qt_config->endArray();
    }
    qt_config->endArray();
}

void Config::SaveValues() {
    if (global) {
        SaveControlValues();
        SaveDataStorageValues();
        SaveDebuggingValues();
        SaveDisabledAddOnValues();
        SaveServiceValues();
        SaveUIValues();
        SaveWebServiceValues();
        SaveMiscellaneousValues();
    }
    SaveCoreValues();
    SaveCpuValues();
    SaveRendererValues();
    SaveAudioValues();
    SaveSystemValues();
}

void Config::SaveAudioValues() {
    qt_config->beginGroup(QStringLiteral("Audio"));

    if (global) {
        WriteSetting(QStringLiteral("output_engine"),
                     QString::fromStdString(Settings::values.sink_id), QStringLiteral("auto"));
        WriteSetting(QStringLiteral("output_device"),
                     QString::fromStdString(Settings::values.audio_device_id),
                     QStringLiteral("auto"));
    }
    WriteSettingGlobal(QStringLiteral("enable_audio_stretching"),
                       Settings::values.enable_audio_stretching, true);
    WriteSettingGlobal(QStringLiteral("volume"), Settings::values.volume, 1.0f);

    qt_config->endGroup();
}

void Config::SaveControlValues() {
    qt_config->beginGroup(QStringLiteral("Controls"));

    SavePlayerValues();
    SaveDebugValues();
    SaveMouseValues();
    SaveTouchscreenValues();
    SaveMotionTouchValues();

    WriteSetting(QStringLiteral("vibration_enabled"), Settings::values.vibration_enabled, true);
    WriteSetting(QStringLiteral("motion_enabled"), Settings::values.motion_enabled, true);
    WriteSetting(QStringLiteral("motion_device"),
                 QString::fromStdString(Settings::values.motion_device),
                 QStringLiteral("engine:motion_emu,update_period:100,sensitivity:0.01"));
    WriteSetting(QStringLiteral("touch_device"),
                 QString::fromStdString(Settings::values.touch_device),
                 QStringLiteral("engine:emu_window"));
    WriteSetting(QStringLiteral("keyboard_enabled"), Settings::values.keyboard_enabled, false);
    WriteSetting(QStringLiteral("use_docked_mode"), Settings::values.use_docked_mode, false);

    qt_config->endGroup();
}

void Config::SaveCoreValues() {
    qt_config->beginGroup(QStringLiteral("Core"));

    WriteSettingGlobal(QStringLiteral("use_multi_core"), Settings::values.use_multi_core, false);

    qt_config->endGroup();
}

void Config::SaveDataStorageValues() {
    qt_config->beginGroup(QStringLiteral("Data Storage"));

    WriteSetting(QStringLiteral("use_virtual_sd"), Settings::values.use_virtual_sd, true);
    WriteSetting(QStringLiteral("nand_directory"),
                 QString::fromStdString(FS::GetUserPath(FS::UserPath::NANDDir)),
                 QString::fromStdString(FS::GetUserPath(FS::UserPath::NANDDir)));
    WriteSetting(QStringLiteral("sdmc_directory"),
                 QString::fromStdString(FS::GetUserPath(FS::UserPath::SDMCDir)),
                 QString::fromStdString(FS::GetUserPath(FS::UserPath::SDMCDir)));
    WriteSetting(QStringLiteral("load_directory"),
                 QString::fromStdString(FS::GetUserPath(FS::UserPath::LoadDir)),
                 QString::fromStdString(FS::GetUserPath(FS::UserPath::LoadDir)));
    WriteSetting(QStringLiteral("dump_directory"),
                 QString::fromStdString(FS::GetUserPath(FS::UserPath::DumpDir)),
                 QString::fromStdString(FS::GetUserPath(FS::UserPath::DumpDir)));
    WriteSetting(QStringLiteral("cache_directory"),
                 QString::fromStdString(FS::GetUserPath(FS::UserPath::CacheDir)),
                 QString::fromStdString(FS::GetUserPath(FS::UserPath::CacheDir)));
    WriteSetting(QStringLiteral("gamecard_inserted"), Settings::values.gamecard_inserted, false);
    WriteSetting(QStringLiteral("gamecard_current_game"), Settings::values.gamecard_current_game,
                 false);
    WriteSetting(QStringLiteral("gamecard_path"),
                 QString::fromStdString(Settings::values.gamecard_path), QString{});

    qt_config->endGroup();
}

void Config::SaveDebuggingValues() {
    qt_config->beginGroup(QStringLiteral("Debugging"));

    // Intentionally not using the QT default setting as this is intended to be changed in the ini
    qt_config->setValue(QStringLiteral("record_frame_times"), Settings::values.record_frame_times);
    WriteSetting(QStringLiteral("use_gdbstub"), Settings::values.use_gdbstub, false);
    WriteSetting(QStringLiteral("gdbstub_port"), Settings::values.gdbstub_port, 24689);
    WriteSetting(QStringLiteral("program_args"),
                 QString::fromStdString(Settings::values.program_args), QString{});
    WriteSetting(QStringLiteral("dump_exefs"), Settings::values.dump_exefs, false);
    WriteSetting(QStringLiteral("dump_nso"), Settings::values.dump_nso, false);
    WriteSetting(QStringLiteral("quest_flag"), Settings::values.quest_flag, false);
    WriteSetting(QStringLiteral("disable_macro_jit"), Settings::values.disable_macro_jit, false);

    qt_config->endGroup();
}

void Config::SaveServiceValues() {
    qt_config->beginGroup(QStringLiteral("Services"));
    WriteSetting(QStringLiteral("bcat_backend"),
                 QString::fromStdString(Settings::values.bcat_backend), QStringLiteral("null"));
    WriteSetting(QStringLiteral("bcat_boxcat_local"), Settings::values.bcat_boxcat_local, false);
    qt_config->endGroup();
}

void Config::SaveDisabledAddOnValues() {
    qt_config->beginWriteArray(QStringLiteral("DisabledAddOns"));

    int i = 0;
    for (const auto& elem : Settings::values.disabled_addons) {
        qt_config->setArrayIndex(i);
        WriteSetting(QStringLiteral("title_id"), QVariant::fromValue<u64>(elem.first), 0);
        qt_config->beginWriteArray(QStringLiteral("disabled"));
        for (std::size_t j = 0; j < elem.second.size(); ++j) {
            qt_config->setArrayIndex(static_cast<int>(j));
            WriteSetting(QStringLiteral("d"), QString::fromStdString(elem.second[j]), QString{});
        }
        qt_config->endArray();
        ++i;
    }

    qt_config->endArray();
}

void Config::SaveMiscellaneousValues() {
    qt_config->beginGroup(QStringLiteral("Miscellaneous"));

    WriteSetting(QStringLiteral("log_filter"), QString::fromStdString(Settings::values.log_filter),
                 QStringLiteral("*:Info"));
    WriteSetting(QStringLiteral("use_dev_keys"), Settings::values.use_dev_keys, false);

    qt_config->endGroup();
}

void Config::SavePathValues() {
    qt_config->beginGroup(QStringLiteral("Paths"));

    WriteSetting(QStringLiteral("romsPath"), UISettings::values.roms_path);
    WriteSetting(QStringLiteral("symbolsPath"), UISettings::values.symbols_path);
    qt_config->beginWriteArray(QStringLiteral("gamedirs"));
    for (int i = 0; i < UISettings::values.game_dirs.size(); ++i) {
        qt_config->setArrayIndex(i);
        const auto& game_dir = UISettings::values.game_dirs[i];
        WriteSetting(QStringLiteral("path"), game_dir.path);
        WriteSetting(QStringLiteral("deep_scan"), game_dir.deep_scan, false);
        WriteSetting(QStringLiteral("expanded"), game_dir.expanded, true);
    }
    qt_config->endArray();
    WriteSetting(QStringLiteral("recentFiles"), UISettings::values.recent_files);
    WriteSetting(QStringLiteral("language"), UISettings::values.language, QString{});

    qt_config->endGroup();
}

void Config::SaveCpuValues() {
    qt_config->beginGroup(QStringLiteral("Cpu"));

    if (global) {
        WriteSetting(QStringLiteral("cpu_accuracy"),
                     static_cast<int>(Settings::values.cpu_accuracy), 0);

        WriteSetting(QStringLiteral("cpuopt_page_tables"), Settings::values.cpuopt_page_tables,
                     true);
        WriteSetting(QStringLiteral("cpuopt_block_linking"), Settings::values.cpuopt_block_linking,
                     true);
        WriteSetting(QStringLiteral("cpuopt_return_stack_buffer"),
                     Settings::values.cpuopt_return_stack_buffer, true);
        WriteSetting(QStringLiteral("cpuopt_fast_dispatcher"),
                     Settings::values.cpuopt_fast_dispatcher, true);
        WriteSetting(QStringLiteral("cpuopt_context_elimination"),
                     Settings::values.cpuopt_context_elimination, true);
        WriteSetting(QStringLiteral("cpuopt_const_prop"), Settings::values.cpuopt_const_prop, true);
        WriteSetting(QStringLiteral("cpuopt_misc_ir"), Settings::values.cpuopt_misc_ir, true);
        WriteSetting(QStringLiteral("cpuopt_reduce_misalign_checks"),
                     Settings::values.cpuopt_reduce_misalign_checks, true);

        WriteSetting(QStringLiteral("cpuopt_unsafe_unfuse_fma"),
                     Settings::values.cpuopt_unsafe_unfuse_fma, true);
        WriteSetting(QStringLiteral("cpuopt_unsafe_reduce_fp_error"),
                     Settings::values.cpuopt_unsafe_reduce_fp_error, true);
    }

    qt_config->endGroup();
}

void Config::SaveRendererValues() {
    qt_config->beginGroup(QStringLiteral("Renderer"));

    WriteSettingGlobal(QStringLiteral("backend"),
                       static_cast<int>(Settings::values.renderer_backend.GetValue(global)),
                       Settings::values.renderer_backend.UsingGlobal(), 0);
    WriteSetting(QStringLiteral("debug"), Settings::values.renderer_debug, false);
    WriteSettingGlobal(QStringLiteral("vulkan_device"), Settings::values.vulkan_device, 0);
    WriteSettingGlobal(QStringLiteral("aspect_ratio"), Settings::values.aspect_ratio, 0);
    WriteSettingGlobal(QStringLiteral("max_anisotropy"), Settings::values.max_anisotropy, 0);
    WriteSettingGlobal(QStringLiteral("use_frame_limit"), Settings::values.use_frame_limit, true);
    WriteSettingGlobal(QStringLiteral("frame_limit"), Settings::values.frame_limit, 100);
    WriteSettingGlobal(QStringLiteral("use_disk_shader_cache"),
                       Settings::values.use_disk_shader_cache, true);
    WriteSettingGlobal(QStringLiteral("gpu_accuracy"),
                       static_cast<int>(Settings::values.gpu_accuracy.GetValue(global)),
                       Settings::values.gpu_accuracy.UsingGlobal(), 0);
    WriteSettingGlobal(QStringLiteral("use_asynchronous_gpu_emulation"),
                       Settings::values.use_asynchronous_gpu_emulation, false);
    WriteSettingGlobal(QStringLiteral("use_vsync"), Settings::values.use_vsync, true);
    WriteSettingGlobal(QStringLiteral("use_assembly_shaders"),
                       Settings::values.use_assembly_shaders, false);
    WriteSettingGlobal(QStringLiteral("use_asynchronous_shaders"),
                       Settings::values.use_asynchronous_shaders, false);
    WriteSettingGlobal(QStringLiteral("use_fast_gpu_time"), Settings::values.use_fast_gpu_time,
                       true);
    // Cast to double because Qt's written float values are not human-readable
    WriteSettingGlobal(QStringLiteral("bg_red"), Settings::values.bg_red, 0.0);
    WriteSettingGlobal(QStringLiteral("bg_green"), Settings::values.bg_green, 0.0);
    WriteSettingGlobal(QStringLiteral("bg_blue"), Settings::values.bg_blue, 0.0);

    qt_config->endGroup();
}

void Config::SaveScreenshotValues() {
    qt_config->beginGroup(QStringLiteral("Screenshots"));

    WriteSetting(QStringLiteral("enable_screenshot_save_as"),
                 UISettings::values.enable_screenshot_save_as);
    WriteSetting(QStringLiteral("screenshot_path"),
                 QString::fromStdString(FS::GetUserPath(FS::UserPath::ScreenshotsDir)));

    qt_config->endGroup();
}

void Config::SaveShortcutValues() {
    qt_config->beginGroup(QStringLiteral("Shortcuts"));

    // Lengths of UISettings::values.shortcuts & default_hotkeys are same.
    // However, their ordering must also be the same.
    for (std::size_t i = 0; i < default_hotkeys.size(); i++) {
        const auto& [name, group, shortcut] = UISettings::values.shortcuts[i];
        const auto& default_hotkey = default_hotkeys[i].shortcut;

        qt_config->beginGroup(group);
        qt_config->beginGroup(name);
        WriteSetting(QStringLiteral("KeySeq"), shortcut.first, default_hotkey.first);
        WriteSetting(QStringLiteral("Context"), shortcut.second, default_hotkey.second);
        qt_config->endGroup();
        qt_config->endGroup();
    }

    qt_config->endGroup();
}

void Config::SaveSystemValues() {
    qt_config->beginGroup(QStringLiteral("System"));

    WriteSetting(QStringLiteral("current_user"), Settings::values.current_user, 0);
    WriteSettingGlobal(QStringLiteral("language_index"), Settings::values.language_index, 1);
    WriteSettingGlobal(QStringLiteral("region_index"), Settings::values.region_index, 1);
    WriteSettingGlobal(QStringLiteral("time_zone_index"), Settings::values.time_zone_index, 0);

    WriteSettingGlobal(QStringLiteral("rng_seed_enabled"),
                       Settings::values.rng_seed.GetValue(global).has_value(),
                       Settings::values.rng_seed.UsingGlobal(), false);
    WriteSettingGlobal(QStringLiteral("rng_seed"),
                       Settings::values.rng_seed.GetValue(global).value_or(0),
                       Settings::values.rng_seed.UsingGlobal(), 0);

    WriteSettingGlobal(QStringLiteral("custom_rtc_enabled"),
                       Settings::values.custom_rtc.GetValue(global).has_value(),
                       Settings::values.custom_rtc.UsingGlobal(), false);
    WriteSettingGlobal(
        QStringLiteral("custom_rtc"),
        QVariant::fromValue<long long>(
            Settings::values.custom_rtc.GetValue(global).value_or(std::chrono::seconds{}).count()),
        Settings::values.custom_rtc.UsingGlobal(), 0);

    WriteSettingGlobal(QStringLiteral("sound_index"), Settings::values.sound_index, 1);

    qt_config->endGroup();
}

void Config::SaveUIValues() {
    qt_config->beginGroup(QStringLiteral("UI"));

    WriteSetting(QStringLiteral("theme"), UISettings::values.theme,
                 QString::fromUtf8(UISettings::themes[0].second));
    WriteSetting(QStringLiteral("enable_discord_presence"),
                 UISettings::values.enable_discord_presence, true);
    WriteSetting(QStringLiteral("select_user_on_boot"), UISettings::values.select_user_on_boot,
                 false);

    SaveUIGamelistValues();
    SaveUILayoutValues();
    SavePathValues();
    SaveScreenshotValues();
    SaveShortcutValues();

    WriteSetting(QStringLiteral("singleWindowMode"), UISettings::values.single_window_mode, true);
    WriteSetting(QStringLiteral("fullscreen"), UISettings::values.fullscreen, false);
    WriteSetting(QStringLiteral("displayTitleBars"), UISettings::values.display_titlebar, true);
    WriteSetting(QStringLiteral("showFilterBar"), UISettings::values.show_filter_bar, true);
    WriteSetting(QStringLiteral("showStatusBar"), UISettings::values.show_status_bar, true);
    WriteSetting(QStringLiteral("confirmClose"), UISettings::values.confirm_before_closing, true);
    WriteSetting(QStringLiteral("firstStart"), UISettings::values.first_start, true);
    WriteSetting(QStringLiteral("calloutFlags"), UISettings::values.callout_flags, 0);
    WriteSetting(QStringLiteral("showConsole"), UISettings::values.show_console, false);
    WriteSetting(QStringLiteral("pauseWhenInBackground"),
                 UISettings::values.pause_when_in_background, false);
    WriteSetting(QStringLiteral("hideInactiveMouse"), UISettings::values.hide_mouse, false);

    qt_config->endGroup();
}

void Config::SaveUIGamelistValues() {
    qt_config->beginGroup(QStringLiteral("UIGameList"));

    WriteSetting(QStringLiteral("show_add_ons"), UISettings::values.show_add_ons, true);
    WriteSetting(QStringLiteral("icon_size"), UISettings::values.icon_size, 64);
    WriteSetting(QStringLiteral("row_1_text_id"), UISettings::values.row_1_text_id, 3);
    WriteSetting(QStringLiteral("row_2_text_id"), UISettings::values.row_2_text_id, 2);
    WriteSetting(QStringLiteral("cache_game_list"), UISettings::values.cache_game_list, true);

    qt_config->endGroup();
}

void Config::SaveUILayoutValues() {
    qt_config->beginGroup(QStringLiteral("UILayout"));

    WriteSetting(QStringLiteral("geometry"), UISettings::values.geometry);
    WriteSetting(QStringLiteral("state"), UISettings::values.state);
    WriteSetting(QStringLiteral("geometryRenderWindow"), UISettings::values.renderwindow_geometry);
    WriteSetting(QStringLiteral("gameListHeaderState"), UISettings::values.gamelist_header_state);
    WriteSetting(QStringLiteral("microProfileDialogGeometry"),
                 UISettings::values.microprofile_geometry);
    WriteSetting(QStringLiteral("microProfileDialogVisible"),
                 UISettings::values.microprofile_visible, false);

    qt_config->endGroup();
}

void Config::SaveWebServiceValues() {
    qt_config->beginGroup(QStringLiteral("WebService"));

    WriteSetting(QStringLiteral("enable_telemetry"), Settings::values.enable_telemetry, true);
    WriteSetting(QStringLiteral("web_api_url"),
                 QString::fromStdString(Settings::values.web_api_url),
                 QStringLiteral("https://api.yuzu-emu.org"));
    WriteSetting(QStringLiteral("yuzu_username"),
                 QString::fromStdString(Settings::values.yuzu_username));
    WriteSetting(QStringLiteral("yuzu_token"), QString::fromStdString(Settings::values.yuzu_token));

    qt_config->endGroup();
}

QVariant Config::ReadSetting(const QString& name) const {
    return qt_config->value(name);
}

QVariant Config::ReadSetting(const QString& name, const QVariant& default_value) const {
    QVariant result;
    if (qt_config->value(name + QStringLiteral("/default"), false).toBool()) {
        result = default_value;
    } else {
        result = qt_config->value(name, default_value);
    }
    return result;
}

template <typename Type>
void Config::ReadSettingGlobal(Settings::Setting<Type>& setting, const QString& name) {
    const bool use_global = qt_config->value(name + QStringLiteral("/use_global"), true).toBool();
    setting.SetGlobal(use_global);
    if (global || !use_global) {
        setting.SetValue(ReadSetting(name).value<Type>());
    }
}

template <typename Type>
void Config::ReadSettingGlobal(Settings::Setting<Type>& setting, const QString& name,
                               const QVariant& default_value) {
    const bool use_global = qt_config->value(name + QStringLiteral("/use_global"), true).toBool();
    setting.SetGlobal(use_global);
    if (global || !use_global) {
        setting.SetValue(ReadSetting(name, default_value).value<Type>());
    }
}

template <typename Type>
void Config::ReadSettingGlobal(Type& setting, const QString& name,
                               const QVariant& default_value) const {
    const bool use_global = qt_config->value(name + QStringLiteral("/use_global"), true).toBool();
    if (global || !use_global) {
        setting = ReadSetting(name, default_value).value<Type>();
    }
}

void Config::WriteSetting(const QString& name, const QVariant& value) {
    qt_config->setValue(name, value);
}

void Config::WriteSetting(const QString& name, const QVariant& value,
                          const QVariant& default_value) {
    qt_config->setValue(name + QStringLiteral("/default"), value == default_value);
    qt_config->setValue(name, value);
}

template <typename Type>
void Config::WriteSettingGlobal(const QString& name, const Settings::Setting<Type>& setting) {
    if (!global) {
        qt_config->setValue(name + QStringLiteral("/use_global"), setting.UsingGlobal());
    }
    if (global || !setting.UsingGlobal()) {
        qt_config->setValue(name, setting.GetValue(global));
    }
}

template <typename Type>
void Config::WriteSettingGlobal(const QString& name, const Settings::Setting<Type>& setting,
                                const QVariant& default_value) {
    if (!global) {
        qt_config->setValue(name + QStringLiteral("/use_global"), setting.UsingGlobal());
    }
    if (global || !setting.UsingGlobal()) {
        qt_config->setValue(name + QStringLiteral("/default"),
                            setting.GetValue(global) == default_value.value<Type>());
        qt_config->setValue(name, setting.GetValue(global));
    }
}

void Config::WriteSettingGlobal(const QString& name, const QVariant& value, bool use_global,
                                const QVariant& default_value) {
    if (!global) {
        qt_config->setValue(name + QStringLiteral("/use_global"), use_global);
    }
    if (global || !use_global) {
        qt_config->setValue(name + QStringLiteral("/default"), value == default_value);
        qt_config->setValue(name, value);
    }
}

void Config::Reload() {
    ReadValues();
    Settings::Sanitize();
    // To apply default value changes
    SaveValues();
    Settings::Apply();
}

void Config::Save() {
    Settings::Sanitize();
    SaveValues();
}
