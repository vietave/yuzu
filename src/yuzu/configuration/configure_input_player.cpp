// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <memory>
#include <utility>
#include <QGridLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include "common/param_package.h"
#include "core/core.h"
#include "core/hle/service/hid/controllers/npad.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/sm/sm.h"
#include "input_common/gcadapter/gc_poller.h"
#include "input_common/main.h"
#include "input_common/udp/udp.h"
#include "ui_configure_input_player.h"
#include "yuzu/configuration/config.h"
#include "yuzu/configuration/configure_input_player.h"

constexpr std::size_t HANDHELD_INDEX = 8;

const std::array<std::string, ConfigureInputPlayer::ANALOG_SUB_BUTTONS_NUM>
    ConfigureInputPlayer::analog_sub_buttons{{
        "up",
        "down",
        "left",
        "right",
    }};

namespace {

void UpdateController(Settings::ControllerType controller_type, std::size_t npad_index,
                      bool connected) {
    Core::System& system{Core::System::GetInstance()};
    if (!system.IsPoweredOn()) {
        return;
    }
    Service::SM::ServiceManager& sm = system.ServiceManager();

    auto& npad =
        sm.GetService<Service::HID::Hid>("hid")
            ->GetAppletResource()
            ->GetController<Service::HID::Controller_NPad>(Service::HID::HidController::NPad);

    npad.UpdateControllerAt(npad.MapSettingsTypeToNPad(controller_type), npad_index, connected);
}

/// Maps the controller type combobox index to Controller Type enum
constexpr Settings::ControllerType GetControllerTypeFromIndex(int index) {
    switch (index) {
    case 0:
    default:
        return Settings::ControllerType::ProController;
    case 1:
        return Settings::ControllerType::DualJoyconDetached;
    case 2:
        return Settings::ControllerType::LeftJoycon;
    case 3:
        return Settings::ControllerType::RightJoycon;
    case 4:
        return Settings::ControllerType::Handheld;
    }
}

/// Maps the Controller Type enum to controller type combobox index
constexpr int GetIndexFromControllerType(Settings::ControllerType type) {
    switch (type) {
    case Settings::ControllerType::ProController:
    default:
        return 0;
    case Settings::ControllerType::DualJoyconDetached:
        return 1;
    case Settings::ControllerType::LeftJoycon:
        return 2;
    case Settings::ControllerType::RightJoycon:
        return 3;
    case Settings::ControllerType::Handheld:
        return 4;
    }
}

QString GetKeyName(int key_code) {
    switch (key_code) {
    case Qt::LeftButton:
        return QObject::tr("Click 0");
    case Qt::RightButton:
        return QObject::tr("Click 1");
    case Qt::MiddleButton:
        return QObject::tr("Click 2");
    case Qt::BackButton:
        return QObject::tr("Click 3");
    case Qt::ForwardButton:
        return QObject::tr("Click 4");
    case Qt::Key_Shift:
        return QObject::tr("Shift");
    case Qt::Key_Control:
        return QObject::tr("Ctrl");
    case Qt::Key_Alt:
        return QObject::tr("Alt");
    case Qt::Key_Meta:
        return {};
    default:
        return QKeySequence(key_code).toString();
    }
}

void SetAnalogParam(const Common::ParamPackage& input_param, Common::ParamPackage& analog_param,
                    const std::string& button_name) {
    // The poller returned a complete axis, so set all the buttons
    if (input_param.Has("axis_x") && input_param.Has("axis_y")) {
        analog_param = input_param;
        return;
    }
    // Check if the current configuration has either no engine or an axis binding.
    // Clears out the old binding and adds one with analog_from_button.
    if (!analog_param.Has("engine") || analog_param.Has("axis_x") || analog_param.Has("axis_y")) {
        analog_param = {
            {"engine", "analog_from_button"},
        };
    }
    analog_param.Set(button_name, input_param.Serialize());
}

QString ButtonToText(const Common::ParamPackage& param) {
    if (!param.Has("engine")) {
        return QObject::tr("[not set]");
    }

    if (param.Get("engine", "") == "keyboard") {
        return GetKeyName(param.Get("code", 0));
    }

    if (param.Get("engine", "") == "gcpad") {
        if (param.Has("axis")) {
            const QString axis_str = QString::fromStdString(param.Get("axis", ""));
            const QString direction_str = QString::fromStdString(param.Get("direction", ""));

            return QObject::tr("GC Axis %1%2").arg(axis_str, direction_str);
        }
        if (param.Has("button")) {
            const QString button_str = QString::number(int(std::log2(param.Get("button", 0))));
            return QObject::tr("GC Button %1").arg(button_str);
        }
        return GetKeyName(param.Get("code", 0));
    }

    if (param.Get("engine", "") == "cemuhookudp") {
        if (param.Has("pad_index")) {
            const QString motion_str = QString::fromStdString(param.Get("pad_index", ""));
            return QObject::tr("Motion %1").arg(motion_str);
        }
        return GetKeyName(param.Get("code", 0));
    }

    if (param.Get("engine", "") == "sdl") {
        if (param.Has("hat")) {
            const QString hat_str = QString::fromStdString(param.Get("hat", ""));
            const QString direction_str = QString::fromStdString(param.Get("direction", ""));

            return QObject::tr("Hat %1 %2").arg(hat_str, direction_str);
        }

        if (param.Has("axis")) {
            const QString axis_str = QString::fromStdString(param.Get("axis", ""));
            const QString direction_str = QString::fromStdString(param.Get("direction", ""));

            return QObject::tr("Axis %1%2").arg(axis_str, direction_str);
        }

        if (param.Has("button")) {
            const QString button_str = QString::fromStdString(param.Get("button", ""));

            return QObject::tr("Button %1").arg(button_str);
        }

        return {};
    }

    return QObject::tr("[unknown]");
}

QString AnalogToText(const Common::ParamPackage& param, const std::string& dir) {
    if (!param.Has("engine")) {
        return QObject::tr("[not set]");
    }

    if (param.Get("engine", "") == "analog_from_button") {
        return ButtonToText(Common::ParamPackage{param.Get(dir, "")});
    }

    if (param.Get("engine", "") == "sdl") {
        if (dir == "modifier") {
            return QObject::tr("[unused]");
        }

        if (dir == "left" || dir == "right") {
            const QString axis_x_str = QString::fromStdString(param.Get("axis_x", ""));

            return QObject::tr("Axis %1").arg(axis_x_str);
        }

        if (dir == "up" || dir == "down") {
            const QString axis_y_str = QString::fromStdString(param.Get("axis_y", ""));

            return QObject::tr("Axis %1").arg(axis_y_str);
        }

        return {};
    }

    if (param.Get("engine", "") == "gcpad") {
        if (dir == "modifier") {
            return QObject::tr("[unused]");
        }

        if (dir == "left" || dir == "right") {
            const QString axis_x_str = QString::fromStdString(param.Get("axis_x", ""));

            return QObject::tr("GC Axis %1").arg(axis_x_str);
        }

        if (dir == "up" || dir == "down") {
            const QString axis_y_str = QString::fromStdString(param.Get("axis_y", ""));

            return QObject::tr("GC Axis %1").arg(axis_y_str);
        }

        return {};
    }
    return QObject::tr("[unknown]");
}
} // namespace

ConfigureInputPlayer::ConfigureInputPlayer(QWidget* parent, std::size_t player_index,
                                           QWidget* bottom_row,
                                           InputCommon::InputSubsystem* input_subsystem_,
                                           bool debug)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureInputPlayer>()), player_index(player_index),
      debug(debug), input_subsystem{input_subsystem_}, timeout_timer(std::make_unique<QTimer>()),
      poll_timer(std::make_unique<QTimer>()), bottom_row(bottom_row) {
    ui->setupUi(this);

    setFocusPolicy(Qt::ClickFocus);

    button_map = {
        ui->buttonA,        ui->buttonB,      ui->buttonX,         ui->buttonY,
        ui->buttonLStick,   ui->buttonRStick, ui->buttonL,         ui->buttonR,
        ui->buttonZL,       ui->buttonZR,     ui->buttonPlus,      ui->buttonMinus,
        ui->buttonDpadLeft, ui->buttonDpadUp, ui->buttonDpadRight, ui->buttonDpadDown,
        ui->buttonSL,       ui->buttonSR,     ui->buttonHome,      ui->buttonScreenshot,
    };

    mod_buttons = {
        ui->buttonLStickMod,
        ui->buttonRStickMod,
    };

    analog_map_buttons = {{
        {
            ui->buttonLStickUp,
            ui->buttonLStickDown,
            ui->buttonLStickLeft,
            ui->buttonLStickRight,
        },
        {
            ui->buttonRStickUp,
            ui->buttonRStickDown,
            ui->buttonRStickLeft,
            ui->buttonRStickRight,
        },
    }};

    motion_map = {
        ui->buttonMotionLeft,
        ui->buttonMotionRight,
    };

    analog_map_deadzone_label = {ui->labelLStickDeadzone, ui->labelRStickDeadzone};
    analog_map_deadzone_slider = {ui->sliderLStickDeadzone, ui->sliderRStickDeadzone};
    analog_map_modifier_groupbox = {ui->buttonLStickModGroup, ui->buttonRStickModGroup};
    analog_map_modifier_label = {ui->labelLStickModifierRange, ui->labelRStickModifierRange};
    analog_map_modifier_slider = {ui->sliderLStickModifierRange, ui->sliderRStickModifierRange};
    analog_map_range_groupbox = {ui->buttonLStickRangeGroup, ui->buttonRStickRangeGroup};
    analog_map_range_spinbox = {ui->spinboxLStickRange, ui->spinboxRStickRange};

    const auto ConfigureButtonClick = [&](QPushButton* button, Common::ParamPackage* param,
                                          int default_val, InputCommon::Polling::DeviceType type) {
        connect(button, &QPushButton::clicked, [=, this] {
            HandleClick(
                button,
                [=, this](Common::ParamPackage params) {
                    // Workaround for ZL & ZR for analog triggers like on XBOX
                    // controllers. Analog triggers (from controllers like the XBOX
                    // controller) would not work due to a different range of their
                    // signals (from 0 to 255 on analog triggers instead of -32768 to
                    // 32768 on analog joysticks). The SDL driver misinterprets analog
                    // triggers as analog joysticks.
                    // TODO: reinterpret the signal range for analog triggers to map the
                    // values correctly. This is required for the correct emulation of
                    // the analog triggers of the GameCube controller.
                    if (button == ui->buttonZL || button == ui->buttonZR) {
                        params.Set("direction", "+");
                        params.Set("threshold", "0.5");
                    }
                    *param = std::move(params);
                },
                type);
        });
    };

    for (int button_id = 0; button_id < Settings::NativeButton::NumButtons; ++button_id) {
        auto* const button = button_map[button_id];

        if (button == nullptr) {
            continue;
        }

        ConfigureButtonClick(button_map[button_id], &buttons_param[button_id],
                             Config::default_buttons[button_id],
                             InputCommon::Polling::DeviceType::Button);

        button->setContextMenuPolicy(Qt::CustomContextMenu);

        connect(button, &QPushButton::customContextMenuRequested,
                [=, this](const QPoint& menu_location) {
                    QMenu context_menu;
                    context_menu.addAction(tr("Clear"), [&] {
                        buttons_param[button_id].Clear();
                        button_map[button_id]->setText(tr("[not set]"));
                    });
                    context_menu.exec(button_map[button_id]->mapToGlobal(menu_location));
                });
    }

    for (int motion_id = 0; motion_id < Settings::NativeMotion::NumMotions; ++motion_id) {
        auto* const button = motion_map[motion_id];
        if (button == nullptr) {
            continue;
        }

        ConfigureButtonClick(motion_map[motion_id], &motions_param[motion_id],
                             Config::default_motions[motion_id],
                             InputCommon::Polling::DeviceType::Motion);

        button->setContextMenuPolicy(Qt::CustomContextMenu);

        connect(button, &QPushButton::customContextMenuRequested,
                [=, this](const QPoint& menu_location) {
                    QMenu context_menu;
                    context_menu.addAction(tr("Clear"), [&] {
                        motions_param[motion_id].Clear();
                        motion_map[motion_id]->setText(tr("[not set]"));
                    });
                    context_menu.exec(motion_map[motion_id]->mapToGlobal(menu_location));
                });
    }

    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; ++analog_id) {
        for (int sub_button_id = 0; sub_button_id < ANALOG_SUB_BUTTONS_NUM; ++sub_button_id) {
            auto* const analog_button = analog_map_buttons[analog_id][sub_button_id];

            if (analog_button == nullptr) {
                continue;
            }

            connect(analog_button, &QPushButton::clicked, [=, this] {
                HandleClick(
                    analog_map_buttons[analog_id][sub_button_id],
                    [=, this](const Common::ParamPackage& params) {
                        SetAnalogParam(params, analogs_param[analog_id],
                                       analog_sub_buttons[sub_button_id]);
                    },
                    InputCommon::Polling::DeviceType::AnalogPreferred);
            });

            analog_button->setContextMenuPolicy(Qt::CustomContextMenu);

            connect(analog_button, &QPushButton::customContextMenuRequested,
                    [=, this](const QPoint& menu_location) {
                        QMenu context_menu;
                        context_menu.addAction(tr("Clear"), [&] {
                            analogs_param[analog_id].Clear();
                            analog_map_buttons[analog_id][sub_button_id]->setText(tr("[not set]"));
                        });
                        context_menu.exec(analog_map_buttons[analog_id][sub_button_id]->mapToGlobal(
                            menu_location));
                    });
        }

        // Handle clicks for the modifier buttons as well.
        ConfigureButtonClick(mod_buttons[analog_id], &stick_mod_param[analog_id],
                             Config::default_stick_mod[analog_id],
                             InputCommon::Polling::DeviceType::Button);

        mod_buttons[analog_id]->setContextMenuPolicy(Qt::CustomContextMenu);

        connect(mod_buttons[analog_id], &QPushButton::customContextMenuRequested,
                [=, this](const QPoint& menu_location) {
                    QMenu context_menu;
                    context_menu.addAction(tr("Clear"), [&] {
                        stick_mod_param[analog_id].Clear();
                        mod_buttons[analog_id]->setText(tr("[not set]"));
                    });
                    context_menu.exec(mod_buttons[analog_id]->mapToGlobal(menu_location));
                });

        connect(analog_map_range_spinbox[analog_id], qOverload<int>(&QSpinBox::valueChanged),
                [=, this] {
                    const auto spinbox_value = analog_map_range_spinbox[analog_id]->value();
                    analogs_param[analog_id].Set("range", spinbox_value / 100.0f);
                });

        connect(analog_map_deadzone_slider[analog_id], &QSlider::valueChanged, [=, this] {
            const auto slider_value = analog_map_deadzone_slider[analog_id]->value();
            analog_map_deadzone_label[analog_id]->setText(tr("Deadzone: %1%").arg(slider_value));
            analogs_param[analog_id].Set("deadzone", slider_value / 100.0f);
        });

        connect(analog_map_modifier_slider[analog_id], &QSlider::valueChanged, [=, this] {
            const auto slider_value = analog_map_modifier_slider[analog_id]->value();
            analog_map_modifier_label[analog_id]->setText(
                tr("Modifier Range: %1%").arg(slider_value));
            analogs_param[analog_id].Set("modifier_scale", slider_value / 100.0f);
        });
    }

    // Player Connected checkbox
    connect(ui->groupConnectedController, &QGroupBox::toggled,
            [this](bool checked) { emit Connected(checked); });

    // Set up controller type. Only Player 1 can choose Handheld.
    ui->comboControllerType->clear();

    QStringList controller_types = {
        tr("Pro Controller"),
        tr("Dual Joycons"),
        tr("Left Joycon"),
        tr("Right Joycon"),
    };

    if (player_index == 0) {
        controller_types.append(tr("Handheld"));
        connect(ui->comboControllerType, qOverload<int>(&QComboBox::currentIndexChanged),
                [this](int index) {
                    emit HandheldStateChanged(GetControllerTypeFromIndex(index) ==
                                              Settings::ControllerType::Handheld);
                });
    }

    // The Debug Controller can only choose the Pro Controller.
    if (debug) {
        ui->buttonScreenshot->setEnabled(false);
        ui->buttonHome->setEnabled(false);
        ui->groupConnectedController->setCheckable(false);
        QStringList debug_controller_types = {
            tr("Pro Controller"),
        };
        ui->comboControllerType->addItems(debug_controller_types);
    } else {
        ui->comboControllerType->addItems(controller_types);
    }

    UpdateControllerIcon();
    UpdateControllerAvailableButtons();
    UpdateMotionButtons();
    connect(ui->comboControllerType, qOverload<int>(&QComboBox::currentIndexChanged), [this](int) {
        UpdateControllerIcon();
        UpdateControllerAvailableButtons();
        UpdateMotionButtons();
    });

    connect(ui->comboDevices, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &ConfigureInputPlayer::UpdateMappingWithDefaults);

    ui->buttonRefreshDevices->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    UpdateInputDevices();
    connect(ui->buttonRefreshDevices, &QPushButton::clicked,
            [this] { emit RefreshInputDevices(); });

    timeout_timer->setSingleShot(true);
    connect(timeout_timer.get(), &QTimer::timeout, [this] { SetPollingResult({}, true); });

    connect(poll_timer.get(), &QTimer::timeout, [this] {
        Common::ParamPackage params;
        if (input_subsystem->GetGCButtons()->IsPolling()) {
            params = input_subsystem->GetGCButtons()->GetNextInput();
            if (params.Has("engine")) {
                SetPollingResult(params, false);
                return;
            }
        }
        if (input_subsystem->GetGCAnalogs()->IsPolling()) {
            params = input_subsystem->GetGCAnalogs()->GetNextInput();
            if (params.Has("engine")) {
                SetPollingResult(params, false);
                return;
            }
        }
        if (input_subsystem->GetUDPMotions()->IsPolling()) {
            params = input_subsystem->GetUDPMotions()->GetNextInput();
            if (params.Has("engine")) {
                SetPollingResult(params, false);
                return;
            }
        }
        for (auto& poller : device_pollers) {
            params = poller->GetNextInput();
            if (params.Has("engine")) {
                SetPollingResult(params, false);
                return;
            }
        }
    });

    LoadConfiguration();

    // TODO(wwylele): enable this when we actually emulate it
    ui->buttonHome->setEnabled(false);
}

ConfigureInputPlayer::~ConfigureInputPlayer() = default;

void ConfigureInputPlayer::ApplyConfiguration() {
    auto& player = Settings::values.players[player_index];
    auto& buttons = debug ? Settings::values.debug_pad_buttons : player.buttons;
    auto& analogs = debug ? Settings::values.debug_pad_analogs : player.analogs;

    std::transform(buttons_param.begin(), buttons_param.end(), buttons.begin(),
                   [](const Common::ParamPackage& param) { return param.Serialize(); });
    std::transform(analogs_param.begin(), analogs_param.end(), analogs.begin(),
                   [](const Common::ParamPackage& param) { return param.Serialize(); });

    if (debug) {
        return;
    }

    auto& motions = player.motions;
    std::transform(motions_param.begin(), motions_param.end(), motions.begin(),
                   [](const Common::ParamPackage& param) { return param.Serialize(); });

    player.controller_type =
        static_cast<Settings::ControllerType>(ui->comboControllerType->currentIndex());
    player.connected = ui->groupConnectedController->isChecked();

    // Player 2-8
    if (player_index != 0) {
        UpdateController(player.controller_type, player_index, player.connected);
        return;
    }

    // Player 1 and Handheld
    auto& handheld = Settings::values.players[HANDHELD_INDEX];
    // If Handheld is selected, copy all the settings from Player 1 to Handheld.
    if (player.controller_type == Settings::ControllerType::Handheld) {
        handheld = player;
        handheld.connected = ui->groupConnectedController->isChecked();
        player.connected = false; // Disconnect Player 1
    } else {
        player.connected = ui->groupConnectedController->isChecked();
        handheld.connected = false; // Disconnect Handheld
    }

    UpdateController(player.controller_type, player_index, player.connected);
    UpdateController(Settings::ControllerType::Handheld, HANDHELD_INDEX, handheld.connected);
}

void ConfigureInputPlayer::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureInputPlayer::RetranslateUI() {
    ui->retranslateUi(this);
    UpdateUI();
}

void ConfigureInputPlayer::LoadConfiguration() {
    auto& player = Settings::values.players[player_index];
    if (debug) {
        std::transform(Settings::values.debug_pad_buttons.begin(),
                       Settings::values.debug_pad_buttons.end(), buttons_param.begin(),
                       [](const std::string& str) { return Common::ParamPackage(str); });
        std::transform(Settings::values.debug_pad_analogs.begin(),
                       Settings::values.debug_pad_analogs.end(), analogs_param.begin(),
                       [](const std::string& str) { return Common::ParamPackage(str); });
    } else {
        std::transform(player.buttons.begin(), player.buttons.end(), buttons_param.begin(),
                       [](const std::string& str) { return Common::ParamPackage(str); });
        std::transform(player.analogs.begin(), player.analogs.end(), analogs_param.begin(),
                       [](const std::string& str) { return Common::ParamPackage(str); });
        std::transform(player.motions.begin(), player.motions.end(), motions_param.begin(),
                       [](const std::string& str) { return Common::ParamPackage(str); });
    }

    UpdateUI();

    if (debug) {
        return;
    }

    ui->comboControllerType->setCurrentIndex(static_cast<int>(player.controller_type));
    ui->groupConnectedController->setChecked(
        player.connected ||
        (player_index == 0 && Settings::values.players[HANDHELD_INDEX].connected));
}

void ConfigureInputPlayer::UpdateInputDevices() {
    input_devices = input_subsystem->GetInputDevices();
    ui->comboDevices->clear();
    for (auto device : input_devices) {
        ui->comboDevices->addItem(QString::fromStdString(device.Get("display", "Unknown")), {});
    }
}

void ConfigureInputPlayer::RestoreDefaults() {
    // Reset Buttons
    for (int button_id = 0; button_id < Settings::NativeButton::NumButtons; ++button_id) {
        buttons_param[button_id] = Common::ParamPackage{
            InputCommon::GenerateKeyboardParam(Config::default_buttons[button_id])};
    }

    // Reset Analogs and Modifier Buttons
    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; ++analog_id) {
        for (int sub_button_id = 0; sub_button_id < ANALOG_SUB_BUTTONS_NUM; ++sub_button_id) {
            Common::ParamPackage params{InputCommon::GenerateKeyboardParam(
                Config::default_analogs[analog_id][sub_button_id])};
            SetAnalogParam(params, analogs_param[analog_id], analog_sub_buttons[sub_button_id]);
        }

        stick_mod_param[analog_id] = Common::ParamPackage(
            InputCommon::GenerateKeyboardParam(Config::default_stick_mod[analog_id]));
    }

    for (int motion_id = 0; motion_id < Settings::NativeMotion::NumMotions; ++motion_id) {
        motions_param[motion_id] = Common::ParamPackage{
            InputCommon::GenerateKeyboardParam(Config::default_motions[motion_id])};
    }

    UpdateUI();
    UpdateInputDevices();
    ui->comboControllerType->setCurrentIndex(0);
}

void ConfigureInputPlayer::ClearAll() {
    for (int button_id = 0; button_id < Settings::NativeButton::NumButtons; ++button_id) {
        const auto* const button = button_map[button_id];
        if (button == nullptr) {
            continue;
        }

        buttons_param[button_id].Clear();
    }

    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; ++analog_id) {
        for (int sub_button_id = 0; sub_button_id < ANALOG_SUB_BUTTONS_NUM; ++sub_button_id) {
            const auto* const analog_button = analog_map_buttons[analog_id][sub_button_id];
            if (analog_button == nullptr) {
                continue;
            }

            analogs_param[analog_id].Clear();
        }

        stick_mod_param[analog_id].Clear();
    }

    for (int motion_id = 0; motion_id < Settings::NativeMotion::NumMotions; ++motion_id) {
        const auto* const motion_button = motion_map[motion_id];
        if (motion_button == nullptr) {
            continue;
        }

        motions_param[motion_id].Clear();
    }

    UpdateUI();
    UpdateInputDevices();
}

void ConfigureInputPlayer::UpdateUI() {
    for (int button = 0; button < Settings::NativeButton::NumButtons; ++button) {
        button_map[button]->setText(ButtonToText(buttons_param[button]));
    }

    for (int motion_id = 0; motion_id < Settings::NativeMotion::NumMotions; ++motion_id) {
        motion_map[motion_id]->setText(ButtonToText(motions_param[motion_id]));
    }

    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; ++analog_id) {
        for (int sub_button_id = 0; sub_button_id < ANALOG_SUB_BUTTONS_NUM; ++sub_button_id) {
            auto* const analog_button = analog_map_buttons[analog_id][sub_button_id];

            if (analog_button == nullptr) {
                continue;
            }

            analog_button->setText(
                AnalogToText(analogs_param[analog_id], analog_sub_buttons[sub_button_id]));
        }

        mod_buttons[analog_id]->setText(ButtonToText(stick_mod_param[analog_id]));

        const auto deadzone_label = analog_map_deadzone_label[analog_id];
        const auto deadzone_slider = analog_map_deadzone_slider[analog_id];
        const auto modifier_groupbox = analog_map_modifier_groupbox[analog_id];
        const auto modifier_label = analog_map_modifier_label[analog_id];
        const auto modifier_slider = analog_map_modifier_slider[analog_id];
        const auto range_groupbox = analog_map_range_groupbox[analog_id];
        const auto range_spinbox = analog_map_range_spinbox[analog_id];

        int slider_value;
        auto& param = analogs_param[analog_id];
        const bool is_controller =
            param.Get("engine", "") == "sdl" || param.Get("engine", "") == "gcpad";

        if (is_controller) {
            if (!param.Has("deadzone")) {
                param.Set("deadzone", 0.1f);
            }
            slider_value = static_cast<int>(param.Get("deadzone", 0.1f) * 100);
            deadzone_label->setText(tr("Deadzone: %1%").arg(slider_value));
            deadzone_slider->setValue(slider_value);
            if (!param.Has("range")) {
                param.Set("range", 1.0f);
            }
            range_spinbox->setValue(static_cast<int>(param.Get("range", 1.0f) * 100));
        } else {
            if (!param.Has("modifier_scale")) {
                param.Set("modifier_scale", 0.5f);
            }
            slider_value = static_cast<int>(param.Get("modifier_scale", 0.5f) * 100);
            modifier_label->setText(tr("Modifier Range: %1%").arg(slider_value));
            modifier_slider->setValue(slider_value);
        }

        deadzone_label->setVisible(is_controller);
        deadzone_slider->setVisible(is_controller);
        modifier_groupbox->setVisible(!is_controller);
        modifier_label->setVisible(!is_controller);
        modifier_slider->setVisible(!is_controller);
        range_groupbox->setVisible(is_controller);
    }
}

void ConfigureInputPlayer::UpdateMappingWithDefaults() {
    if (ui->comboDevices->currentIndex() < 2) {
        return;
    }
    const auto& device = input_devices[ui->comboDevices->currentIndex()];
    auto button_mapping = input_subsystem->GetButtonMappingForDevice(device);
    auto analog_mapping = input_subsystem->GetAnalogMappingForDevice(device);
    for (std::size_t i = 0; i < buttons_param.size(); ++i) {
        buttons_param[i] = button_mapping[static_cast<Settings::NativeButton::Values>(i)];
    }
    for (std::size_t i = 0; i < analogs_param.size(); ++i) {
        analogs_param[i] = analog_mapping[static_cast<Settings::NativeAnalog::Values>(i)];
    }

    UpdateUI();
}

void ConfigureInputPlayer::HandleClick(
    QPushButton* button, std::function<void(const Common::ParamPackage&)> new_input_setter,
    InputCommon::Polling::DeviceType type) {
    if (button == ui->buttonMotionLeft || button == ui->buttonMotionRight) {
        button->setText(tr("Shake!"));
    } else {
        button->setText(tr("[waiting]"));
    }
    button->setFocus();

    // The first two input devices are always Any and Keyboard/Mouse. If the user filtered to a
    // controller, then they don't want keyboard/mouse input
    want_keyboard_mouse = ui->comboDevices->currentIndex() < 2;

    input_setter = new_input_setter;

    device_pollers = input_subsystem->GetPollers(type);

    for (auto& poller : device_pollers) {
        poller->Start();
    }

    QWidget::grabMouse();
    QWidget::grabKeyboard();

    if (type == InputCommon::Polling::DeviceType::Button) {
        input_subsystem->GetGCButtons()->BeginConfiguration();
    } else {
        input_subsystem->GetGCAnalogs()->BeginConfiguration();
    }

    if (type == InputCommon::Polling::DeviceType::Motion) {
        input_subsystem->GetUDPMotions()->BeginConfiguration();
    }

    timeout_timer->start(2500); // Cancel after 2.5 seconds
    poll_timer->start(50);      // Check for new inputs every 50ms
}

void ConfigureInputPlayer::SetPollingResult(const Common::ParamPackage& params, bool abort) {
    timeout_timer->stop();
    poll_timer->stop();
    for (auto& poller : device_pollers) {
        poller->Stop();
    }

    QWidget::releaseMouse();
    QWidget::releaseKeyboard();

    input_subsystem->GetGCButtons()->EndConfiguration();
    input_subsystem->GetGCAnalogs()->EndConfiguration();

    input_subsystem->GetUDPMotions()->EndConfiguration();

    if (!abort) {
        (*input_setter)(params);
    }

    UpdateUI();
    input_setter = std::nullopt;
}

void ConfigureInputPlayer::mousePressEvent(QMouseEvent* event) {
    if (!input_setter || !event) {
        return;
    }

    if (want_keyboard_mouse) {
        SetPollingResult(Common::ParamPackage{InputCommon::GenerateKeyboardParam(event->button())},
                         false);
    } else {
        // We don't want any mouse buttons, so don't stop polling
        return;
    }

    SetPollingResult({}, true);
}

void ConfigureInputPlayer::keyPressEvent(QKeyEvent* event) {
    if (!input_setter || !event) {
        return;
    }

    if (event->key() != Qt::Key_Escape) {
        if (want_keyboard_mouse) {
            SetPollingResult(Common::ParamPackage{InputCommon::GenerateKeyboardParam(event->key())},
                             false);
        } else {
            // Escape key wasn't pressed and we don't want any keyboard keys, so don't stop polling
            return;
        }
    }

    SetPollingResult({}, true);
}

void ConfigureInputPlayer::UpdateControllerIcon() {
    // We aren't using Qt's built in theme support here since we aren't drawing an icon (and its
    // "nonstandard" to use an image through the icon support)
    const QString stylesheet = [this] {
        switch (GetControllerTypeFromIndex(ui->comboControllerType->currentIndex())) {
        case Settings::ControllerType::ProController:
            return QStringLiteral("image: url(:/controller/pro_controller%0)");
        case Settings::ControllerType::DualJoyconDetached:
            return QStringLiteral("image: url(:/controller/dual_joycon%0)");
        case Settings::ControllerType::LeftJoycon:
            return QStringLiteral("image: url(:/controller/single_joycon_left_vertical%0)");
        case Settings::ControllerType::RightJoycon:
            return QStringLiteral("image: url(:/controller/single_joycon_right_vertical%0)");
        case Settings::ControllerType::Handheld:
            return QStringLiteral("image: url(:/controller/handheld%0)");
        default:
            return QString{};
        }
    }();

    const QString theme = [this] {
        if (QIcon::themeName().contains(QStringLiteral("dark"))) {
            return QStringLiteral("_dark");
        } else if (QIcon::themeName().contains(QStringLiteral("midnight"))) {
            return QStringLiteral("_midnight");
        } else {
            return QString{};
        }
    }();

    ui->controllerFrame->setStyleSheet(stylesheet.arg(theme));
}

void ConfigureInputPlayer::UpdateControllerAvailableButtons() {
    auto layout = GetControllerTypeFromIndex(ui->comboControllerType->currentIndex());
    if (debug) {
        layout = Settings::ControllerType::ProController;
    }

    // List of all the widgets that will be hidden by any of the following layouts that need
    // "unhidden" after the controller type changes
    const std::array<QWidget*, 9> layout_show = {
        ui->buttonShoulderButtonsSLSR,
        ui->horizontalSpacerShoulderButtonsWidget,
        ui->horizontalSpacerShoulderButtonsWidget2,
        ui->buttonShoulderButtonsLeft,
        ui->buttonMiscButtonsMinusScreenshot,
        ui->bottomLeft,
        ui->buttonShoulderButtonsRight,
        ui->buttonMiscButtonsPlusHome,
        ui->bottomRight,
    };

    for (auto* widget : layout_show) {
        widget->show();
    }

    std::vector<QWidget*> layout_hidden;
    switch (layout) {
    case Settings::ControllerType::ProController:
    case Settings::ControllerType::DualJoyconDetached:
    case Settings::ControllerType::Handheld:
        layout_hidden = {
            ui->buttonShoulderButtonsSLSR,
            ui->horizontalSpacerShoulderButtonsWidget2,
        };
        break;
    case Settings::ControllerType::LeftJoycon:
        layout_hidden = {
            ui->horizontalSpacerShoulderButtonsWidget2,
            ui->buttonShoulderButtonsRight,
            ui->buttonMiscButtonsPlusHome,
            ui->bottomRight,
        };
        break;
    case Settings::ControllerType::RightJoycon:
        layout_hidden = {
            ui->horizontalSpacerShoulderButtonsWidget,
            ui->buttonShoulderButtonsLeft,
            ui->buttonMiscButtonsMinusScreenshot,
            ui->bottomLeft,
        };
        break;
    }

    for (auto* widget : layout_hidden) {
        widget->hide();
    }
}

void ConfigureInputPlayer::UpdateMotionButtons() {
    if (debug) {
        // Motion isn't used with the debug controller, hide both groupboxes.
        ui->buttonMotionLeftGroup->hide();
        ui->buttonMotionRightGroup->hide();
        return;
    }

    // Show/hide the "Motion 1/2" groupboxes depending on the currently selected controller.
    switch (GetControllerTypeFromIndex(ui->comboControllerType->currentIndex())) {
    case Settings::ControllerType::ProController:
    case Settings::ControllerType::LeftJoycon:
    case Settings::ControllerType::Handheld:
        // Show "Motion 1" and hide "Motion 2".
        ui->buttonMotionLeftGroup->show();
        ui->buttonMotionRightGroup->hide();
        break;
    case Settings::ControllerType::RightJoycon:
        // Show "Motion 2" and hide "Motion 1".
        ui->buttonMotionLeftGroup->hide();
        ui->buttonMotionRightGroup->show();
        break;
    case Settings::ControllerType::DualJoyconDetached:
    default:
        // Show both "Motion 1/2".
        ui->buttonMotionLeftGroup->show();
        ui->buttonMotionRightGroup->show();
        break;
    }
}

void ConfigureInputPlayer::showEvent(QShowEvent* event) {
    if (bottom_row == nullptr) {
        return;
    }
    QWidget::showEvent(event);
    ui->main->addWidget(bottom_row);
}

void ConfigureInputPlayer::ConnectPlayer(bool connected) {
    ui->groupConnectedController->setChecked(connected);
}
