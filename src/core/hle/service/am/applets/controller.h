// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <vector>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "core/hle/result.h"
#include "core/hle/service/am/applets/applets.h"

namespace Core {
class System;
}

namespace Service::AM::Applets {

using IdentificationColor = std::array<u8, 4>;
using ExplainText = std::array<char, 0x81>;

enum class LibraryAppletVersion : u32_le {
    Version3 = 0x3, // 1.0.0 - 2.3.0
    Version4 = 0x4, // 3.0.0 - 5.1.0
    Version5 = 0x5, // 6.0.0 - 7.0.1
    Version7 = 0x7, // 8.0.0+
};

enum class ControllerSupportMode : u8 {
    ShowControllerSupport = 0,
    ShowControllerStrapGuide = 1,
    ShowControllerFirmwareUpdate = 2,
};

enum class ControllerSupportCaller : u8 {
    Application = 0,
    System = 1,
};

struct ControllerSupportArgPrivate {
    u32 arg_private_size{};
    u32 arg_size{};
    bool flag_0{};
    bool flag_1{};
    ControllerSupportMode mode{};
    ControllerSupportCaller caller{};
    u32 style_set{};
    u32 joy_hold_type{};
};
static_assert(sizeof(ControllerSupportArgPrivate) == 0x14,
              "ControllerSupportArgPrivate has incorrect size.");

struct ControllerSupportArgHeader {
    s8 player_count_min{};
    s8 player_count_max{};
    bool enable_take_over_connection{};
    bool enable_left_justify{};
    bool enable_permit_joy_dual{};
    bool enable_single_mode{};
    bool enable_identification_color{};
};
static_assert(sizeof(ControllerSupportArgHeader) == 0x7,
              "ControllerSupportArgHeader has incorrect size.");

// LibraryAppletVersion 0x3, 0x4, 0x5
struct ControllerSupportArgOld {
    ControllerSupportArgHeader header{};
    std::array<IdentificationColor, 4> identification_colors{};
    bool enable_explain_text{};
    std::array<ExplainText, 4> explain_text{};
};
static_assert(sizeof(ControllerSupportArgOld) == 0x21C,
              "ControllerSupportArgOld has incorrect size.");

// LibraryAppletVersion 0x7
struct ControllerSupportArgNew {
    ControllerSupportArgHeader header{};
    std::array<IdentificationColor, 8> identification_colors{};
    bool enable_explain_text{};
    std::array<ExplainText, 8> explain_text{};
};
static_assert(sizeof(ControllerSupportArgNew) == 0x430,
              "ControllerSupportArgNew has incorrect size.");

struct ControllerSupportResultInfo {
    s8 player_count{};
    INSERT_PADDING_BYTES(3);
    u32 selected_id{};
    u32 result{};
};
static_assert(sizeof(ControllerSupportResultInfo) == 0xC,
              "ControllerSupportResultInfo has incorrect size.");

class Controller final : public Applet {
public:
    explicit Controller(Core::System& system_, const Core::Frontend::ControllerApplet& frontend_);
    ~Controller() override;

    void Initialize() override;

    bool TransactionComplete() const override;
    ResultCode GetStatus() const override;
    void ExecuteInteractive() override;
    void Execute() override;

    void ConfigurationComplete();

private:
    const Core::Frontend::ControllerApplet& frontend;

    LibraryAppletVersion library_applet_version;
    ControllerSupportArgPrivate controller_private_arg;
    ControllerSupportArgOld controller_user_arg_old;
    ControllerSupportArgNew controller_user_arg_new;
    bool complete{false};
    ResultCode status{RESULT_SUCCESS};
    bool is_single_mode{false};
    std::vector<u8> out_data;
};

} // namespace Service::AM::Applets
