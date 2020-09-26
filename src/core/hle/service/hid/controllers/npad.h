// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "common/bit_field.h"
#include "common/common_types.h"
#include "core/frontend/input.h"
#include "core/hle/kernel/object.h"
#include "core/hle/kernel/writable_event.h"
#include "core/hle/service/hid/controllers/controller_base.h"
#include "core/settings.h"

namespace Service::HID {

constexpr u32 NPAD_HANDHELD = 32;
constexpr u32 NPAD_UNKNOWN = 16; // TODO(ogniK): What is this?

class Controller_NPad final : public ControllerBase {
public:
    explicit Controller_NPad(Core::System& system);
    ~Controller_NPad() override;

    // Called when the controller is initialized
    void OnInit() override;

    // When the controller is released
    void OnRelease() override;

    // When the controller is requesting an update for the shared memory
    void OnUpdate(const Core::Timing::CoreTiming& core_timing, u8* data, std::size_t size) override;

    // Called when input devices should be loaded
    void OnLoadInputDevices() override;

    struct NPadType {
        union {
            u32_le raw{};

            BitField<0, 1, u32> pro_controller;
            BitField<1, 1, u32> handheld;
            BitField<2, 1, u32> joycon_dual;
            BitField<3, 1, u32> joycon_left;
            BitField<4, 1, u32> joycon_right;

            BitField<6, 1, u32> pokeball; // TODO(ogniK): Confirm when possible
        };
    };
    static_assert(sizeof(NPadType) == 4, "NPadType is an invalid size");

    struct Vibration {
        f32 amp_low;
        f32 freq_low;
        f32 amp_high;
        f32 freq_high;
    };
    static_assert(sizeof(Vibration) == 0x10, "Vibration is an invalid size");

    enum class GyroscopeZeroDriftMode : u32 {
        Loose = 0,
        Standard = 1,
        Tight = 2,
    };

    enum class NpadHoldType : u64 {
        Vertical = 0,
        Horizontal = 1,
    };

    enum class NPadAssignments : u32_le {
        Dual = 0,
        Single = 1,
    };

    enum class NpadHandheldActivationMode : u64 {
        Dual = 0,
        Single = 1,
        None = 2,
    };

    enum class NPadControllerType {
        None,
        ProController,
        Handheld,
        JoyDual,
        JoyLeft,
        JoyRight,
        Pokeball,
    };

    struct LedPattern {
        explicit LedPattern(u64 light1, u64 light2, u64 light3, u64 light4) {
            position1.Assign(light1);
            position2.Assign(light2);
            position3.Assign(light3);
            position4.Assign(light4);
        }
        union {
            u64 raw{};
            BitField<0, 1, u64> position1;
            BitField<1, 1, u64> position2;
            BitField<2, 1, u64> position3;
            BitField<3, 1, u64> position4;
        };
    };

    void SetSupportedStyleSet(NPadType style_set);
    NPadType GetSupportedStyleSet() const;

    void SetSupportedNPadIdTypes(u8* data, std::size_t length);
    void GetSupportedNpadIdTypes(u32* data, std::size_t max_length);
    std::size_t GetSupportedNPadIdTypesSize() const;

    void SetHoldType(NpadHoldType joy_hold_type);
    NpadHoldType GetHoldType() const;

    void SetNpadHandheldActivationMode(NpadHandheldActivationMode activation_mode);
    NpadHandheldActivationMode GetNpadHandheldActivationMode() const;

    void SetNpadMode(u32 npad_id, NPadAssignments assignment_mode);

    void VibrateController(const std::vector<u32>& controller_ids,
                           const std::vector<Vibration>& vibrations);

    Vibration GetLastVibration() const;

    std::shared_ptr<Kernel::ReadableEvent> GetStyleSetChangedEvent(u32 npad_id) const;
    void SignalStyleSetChangedEvent(u32 npad_id) const;

    // Adds a new controller at an index.
    void AddNewControllerAt(NPadControllerType controller, std::size_t npad_index);
    // Adds a new controller at an index with connection status.
    void UpdateControllerAt(NPadControllerType controller, std::size_t npad_index, bool connected);

    void DisconnectNPad(u32 npad_id);
    void DisconnectNPadAtIndex(std::size_t index);

    void SetGyroscopeZeroDriftMode(GyroscopeZeroDriftMode drift_mode);
    GyroscopeZeroDriftMode GetGyroscopeZeroDriftMode() const;
    bool IsSixAxisSensorAtRest() const;
    void SetSixAxisEnabled(bool six_axis_status);
    LedPattern GetLedPattern(u32 npad_id);
    void SetVibrationEnabled(bool can_vibrate);
    bool IsVibrationEnabled() const;
    void ClearAllConnectedControllers();
    void DisconnectAllConnectedControllers();
    void ConnectAllDisconnectedControllers();
    void ClearAllControllers();

    void MergeSingleJoyAsDualJoy(u32 npad_id_1, u32 npad_id_2);
    void StartLRAssignmentMode();
    void StopLRAssignmentMode();
    bool SwapNpadAssignment(u32 npad_id_1, u32 npad_id_2);

    // Logical OR for all buttons presses on all controllers
    // Specifically for cheat engine and other features.
    u32 GetAndResetPressState();

    static Controller_NPad::NPadControllerType MapSettingsTypeToNPad(Settings::ControllerType type);
    static Settings::ControllerType MapNPadToSettingsType(Controller_NPad::NPadControllerType type);
    static std::size_t NPadIdToIndex(u32 npad_id);
    static u32 IndexToNPad(std::size_t index);

private:
    struct CommonHeader {
        s64_le timestamp;
        s64_le total_entry_count;
        s64_le last_entry_index;
        s64_le entry_count;
    };
    static_assert(sizeof(CommonHeader) == 0x20, "CommonHeader is an invalid size");

    struct ControllerColor {
        u32_le body_color;
        u32_le button_color;
    };
    static_assert(sizeof(ControllerColor) == 8, "ControllerColor is an invalid size");

    struct ControllerPadState {
        union {
            u64_le raw{};
            // Button states
            BitField<0, 1, u64> a;
            BitField<1, 1, u64> b;
            BitField<2, 1, u64> x;
            BitField<3, 1, u64> y;
            BitField<4, 1, u64> l_stick;
            BitField<5, 1, u64> r_stick;
            BitField<6, 1, u64> l;
            BitField<7, 1, u64> r;
            BitField<8, 1, u64> zl;
            BitField<9, 1, u64> zr;
            BitField<10, 1, u64> plus;
            BitField<11, 1, u64> minus;

            // D-Pad
            BitField<12, 1, u64> d_left;
            BitField<13, 1, u64> d_up;
            BitField<14, 1, u64> d_right;
            BitField<15, 1, u64> d_down;

            // Left JoyStick
            BitField<16, 1, u64> l_stick_left;
            BitField<17, 1, u64> l_stick_up;
            BitField<18, 1, u64> l_stick_right;
            BitField<19, 1, u64> l_stick_down;

            // Right JoyStick
            BitField<20, 1, u64> r_stick_left;
            BitField<21, 1, u64> r_stick_up;
            BitField<22, 1, u64> r_stick_right;
            BitField<23, 1, u64> r_stick_down;

            // Not always active?
            BitField<24, 1, u64> left_sl;
            BitField<25, 1, u64> left_sr;

            BitField<26, 1, u64> right_sl;
            BitField<27, 1, u64> right_sr;
        };
    };
    static_assert(sizeof(ControllerPadState) == 8, "ControllerPadState is an invalid size");

    struct AnalogPosition {
        s32_le x;
        s32_le y;
    };
    static_assert(sizeof(AnalogPosition) == 8, "AnalogPosition is an invalid size");

    struct ConnectionState {
        union {
            u32_le raw{};
            BitField<0, 1, u32> IsConnected;
            BitField<1, 1, u32> IsWired;
            BitField<2, 1, u32> IsLeftJoyConnected;
            BitField<3, 1, u32> IsLeftJoyWired;
            BitField<4, 1, u32> IsRightJoyConnected;
            BitField<5, 1, u32> IsRightJoyWired;
        };
    };
    static_assert(sizeof(ConnectionState) == 4, "ConnectionState is an invalid size");

    struct ControllerPad {
        ControllerPadState pad_states;
        AnalogPosition l_stick;
        AnalogPosition r_stick;
    };
    static_assert(sizeof(ControllerPad) == 0x18, "ControllerPad is an invalid size");

    struct GenericStates {
        s64_le timestamp;
        s64_le timestamp2;
        ControllerPad pad;
        ConnectionState connection_status;
    };
    static_assert(sizeof(GenericStates) == 0x30, "NPadGenericStates is an invalid size");

    struct NPadGeneric {
        CommonHeader common;
        std::array<GenericStates, 17> npad;
    };
    static_assert(sizeof(NPadGeneric) == 0x350, "NPadGeneric is an invalid size");

    struct SixAxisStates {
        s64_le timestamp{};
        INSERT_PADDING_WORDS(2);
        s64_le timestamp2{};
        Common::Vec3f accel{};
        Common::Vec3f gyro{};
        Common::Vec3f rotation{};
        std::array<Common::Vec3f, 3> orientation{};
        s64_le always_one{1};
    };
    static_assert(sizeof(SixAxisStates) == 0x68, "SixAxisStates is an invalid size");

    struct SixAxisGeneric {
        CommonHeader common{};
        std::array<SixAxisStates, 17> sixaxis{};
    };
    static_assert(sizeof(SixAxisGeneric) == 0x708, "SixAxisGeneric is an invalid size");

    enum class ColorReadError : u32_le {
        ReadOk = 0,
        ColorDoesntExist = 1,
        NoController = 2,
    };

    struct NPadProperties {
        union {
            s64_le raw{};
            BitField<11, 1, s64> is_vertical;
            BitField<12, 1, s64> is_horizontal;
            BitField<13, 1, s64> use_plus;
            BitField<14, 1, s64> use_minus;
        };
    };

    struct NPadDevice {
        union {
            u32_le raw{};
            BitField<0, 1, s32> pro_controller;
            BitField<1, 1, s32> handheld;
            BitField<2, 1, s32> handheld_left;
            BitField<3, 1, s32> handheld_right;
            BitField<4, 1, s32> joycon_left;
            BitField<5, 1, s32> joycon_right;
            BitField<6, 1, s32> pokeball;
        };
    };

    struct MotionDevice {
        Common::Vec3f accel;
        Common::Vec3f gyro;
        Common::Vec3f rotation;
        std::array<Common::Vec3f, 3> orientation;
    };

    struct NPadEntry {
        NPadType joy_styles;
        NPadAssignments pad_assignment;

        ColorReadError single_color_error;
        ControllerColor single_color;

        ColorReadError dual_color_error;
        ControllerColor left_color;
        ControllerColor right_color;

        NPadGeneric main_controller_states;
        NPadGeneric handheld_states;
        NPadGeneric dual_states;
        NPadGeneric left_joy_states;
        NPadGeneric right_joy_states;
        NPadGeneric pokeball_states;
        NPadGeneric libnx; // TODO(ogniK): Find out what this actually is, libnx seems to only be
                           // relying on this for the time being
        SixAxisGeneric sixaxis_full;
        SixAxisGeneric sixaxis_handheld;
        SixAxisGeneric sixaxis_dual_left;
        SixAxisGeneric sixaxis_dual_right;
        SixAxisGeneric sixaxis_left;
        SixAxisGeneric sixaxis_right;
        NPadDevice device_type;
        NPadProperties properties;
        INSERT_PADDING_WORDS(1);
        std::array<u32, 3> battery_level;
        INSERT_PADDING_BYTES(0x5c);
        INSERT_PADDING_BYTES(0xdf8);
    };
    static_assert(sizeof(NPadEntry) == 0x5000, "NPadEntry is an invalid size");

    struct ControllerHolder {
        NPadControllerType type;
        bool is_connected;
    };

    void InitNewlyAddedController(std::size_t controller_idx);
    bool IsControllerSupported(NPadControllerType controller) const;
    void RequestPadStateUpdate(u32 npad_id);

    u32 press_state{};

    NPadType style{};
    std::array<NPadEntry, 10> shared_memory_entries{};
    using ButtonArray = std::array<
        std::array<std::unique_ptr<Input::ButtonDevice>, Settings::NativeButton::NUM_BUTTONS_HID>,
        10>;
    using StickArray = std::array<
        std::array<std::unique_ptr<Input::AnalogDevice>, Settings::NativeAnalog::NUM_STICKS_HID>,
        10>;
    using MotionArray = std::array<
        std::array<std::unique_ptr<Input::MotionDevice>, Settings::NativeMotion::NUM_MOTION_HID>,
        10>;
    ButtonArray buttons;
    StickArray sticks;
    MotionArray motions;
    std::vector<u32> supported_npad_id_types{};
    NpadHoldType hold_type{NpadHoldType::Vertical};
    NpadHandheldActivationMode handheld_activation_mode{NpadHandheldActivationMode::Dual};
    // Each controller should have their own styleset changed event
    std::array<Kernel::EventPair, 10> styleset_changed_events;
    Vibration last_processed_vibration{};
    std::array<ControllerHolder, 10> connected_controllers{};
    GyroscopeZeroDriftMode gyroscope_zero_drift_mode{GyroscopeZeroDriftMode::Standard};
    bool can_controllers_vibrate{true};
    bool sixaxis_sensors_enabled{true};
    bool sixaxis_at_rest{true};
    std::array<ControllerPad, 10> npad_pad_states{};
    bool is_in_lr_assignment_mode{false};
    Core::System& system;
};
} // namespace Service::HID
