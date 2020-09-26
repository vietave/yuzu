// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstring>

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/frontend/applets/controller.h"
#include "core/hle/result.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applets/controller.h"
#include "core/hle/service/hid/controllers/npad.h"

namespace Service::AM::Applets {

// This error code (0x183ACA) is thrown when the applet fails to initialize.
[[maybe_unused]] constexpr ResultCode ERR_CONTROLLER_APPLET_3101{ErrorModule::HID, 3101};
// This error code (0x183CCA) is thrown when the u32 result in ControllerSupportResultInfo is 2.
[[maybe_unused]] constexpr ResultCode ERR_CONTROLLER_APPLET_3102{ErrorModule::HID, 3102};

static Core::Frontend::ControllerParameters ConvertToFrontendParameters(
    ControllerSupportArgPrivate private_arg, ControllerSupportArgHeader header, bool enable_text,
    std::vector<IdentificationColor> identification_colors, std::vector<ExplainText> text) {
    HID::Controller_NPad::NPadType npad_style_set;
    npad_style_set.raw = private_arg.style_set;

    return {
        .min_players = std::max(s8(1), header.player_count_min),
        .max_players = header.player_count_max,
        .keep_controllers_connected = header.enable_take_over_connection,
        .enable_single_mode = header.enable_single_mode,
        .enable_border_color = header.enable_identification_color,
        .border_colors = identification_colors,
        .enable_explain_text = enable_text,
        .explain_text = text,
        .allow_pro_controller = npad_style_set.pro_controller == 1,
        .allow_handheld = npad_style_set.handheld == 1,
        .allow_dual_joycons = npad_style_set.joycon_dual == 1,
        .allow_left_joycon = npad_style_set.joycon_left == 1,
        .allow_right_joycon = npad_style_set.joycon_right == 1,
    };
}

Controller::Controller(Core::System& system_, const Core::Frontend::ControllerApplet& frontend_)
    : Applet{system_.Kernel()}, frontend(frontend_) {}

Controller::~Controller() = default;

void Controller::Initialize() {
    Applet::Initialize();

    LOG_INFO(Service_HID, "Initializing Controller Applet.");

    LOG_DEBUG(Service_HID,
              "Initializing Applet with common_args: arg_version={}, lib_version={}, "
              "play_startup_sound={}, size={}, system_tick={}, theme_color={}",
              common_args.arguments_version, common_args.library_version,
              common_args.play_startup_sound, common_args.size, common_args.system_tick,
              common_args.theme_color);

    library_applet_version = LibraryAppletVersion{common_args.library_version};

    const auto private_arg_storage = broker.PopNormalDataToApplet();
    ASSERT(private_arg_storage != nullptr);

    const auto& private_arg = private_arg_storage->GetData();
    ASSERT(private_arg.size() == sizeof(ControllerSupportArgPrivate));

    std::memcpy(&controller_private_arg, private_arg.data(), sizeof(ControllerSupportArgPrivate));
    ASSERT_MSG(controller_private_arg.arg_private_size == sizeof(ControllerSupportArgPrivate),
               "Unknown ControllerSupportArgPrivate revision={} with size={}",
               library_applet_version, controller_private_arg.arg_private_size);

    switch (controller_private_arg.mode) {
    case ControllerSupportMode::ShowControllerSupport: {
        const auto user_arg_storage = broker.PopNormalDataToApplet();
        ASSERT(user_arg_storage != nullptr);

        const auto& user_arg = user_arg_storage->GetData();
        switch (library_applet_version) {
        case LibraryAppletVersion::Version3:
        case LibraryAppletVersion::Version4:
        case LibraryAppletVersion::Version5:
            ASSERT(user_arg.size() == sizeof(ControllerSupportArgOld));
            std::memcpy(&controller_user_arg_old, user_arg.data(), sizeof(ControllerSupportArgOld));
            break;
        case LibraryAppletVersion::Version7:
            ASSERT(user_arg.size() == sizeof(ControllerSupportArgNew));
            std::memcpy(&controller_user_arg_new, user_arg.data(), sizeof(ControllerSupportArgNew));
            break;
        default:
            UNIMPLEMENTED_MSG("Unknown ControllerSupportArg revision={} with size={}",
                              library_applet_version, controller_private_arg.arg_size);
            ASSERT(user_arg.size() >= sizeof(ControllerSupportArgNew));
            std::memcpy(&controller_user_arg_new, user_arg.data(), sizeof(ControllerSupportArgNew));
            break;
        }
        break;
    }
    case ControllerSupportMode::ShowControllerStrapGuide:
    case ControllerSupportMode::ShowControllerFirmwareUpdate:
    default: {
        UNIMPLEMENTED_MSG("Unimplemented ControllerSupportMode={}", controller_private_arg.mode);
        break;
    }
    }
}

bool Controller::TransactionComplete() const {
    return complete;
}

ResultCode Controller::GetStatus() const {
    return status;
}

void Controller::ExecuteInteractive() {
    UNREACHABLE_MSG("Attempted to call interactive execution on non-interactive applet.");
}

void Controller::Execute() {
    switch (controller_private_arg.mode) {
    case ControllerSupportMode::ShowControllerSupport: {
        const auto parameters = [this] {
            switch (library_applet_version) {
            case LibraryAppletVersion::Version3:
            case LibraryAppletVersion::Version4:
            case LibraryAppletVersion::Version5:
                return ConvertToFrontendParameters(
                    controller_private_arg, controller_user_arg_old.header,
                    controller_user_arg_old.enable_explain_text,
                    std::vector<IdentificationColor>(
                        controller_user_arg_old.identification_colors.begin(),
                        controller_user_arg_old.identification_colors.end()),
                    std::vector<ExplainText>(controller_user_arg_old.explain_text.begin(),
                                             controller_user_arg_old.explain_text.end()));
            case LibraryAppletVersion::Version7:
            default:
                return ConvertToFrontendParameters(
                    controller_private_arg, controller_user_arg_new.header,
                    controller_user_arg_new.enable_explain_text,
                    std::vector<IdentificationColor>(
                        controller_user_arg_new.identification_colors.begin(),
                        controller_user_arg_new.identification_colors.end()),
                    std::vector<ExplainText>(controller_user_arg_new.explain_text.begin(),
                                             controller_user_arg_new.explain_text.end()));
            }
        }();

        is_single_mode = parameters.enable_single_mode;

        LOG_DEBUG(Service_HID,
                  "Controller Parameters: min_players={}, max_players={}, "
                  "keep_controllers_connected={}, enable_single_mode={}, enable_border_color={}, "
                  "enable_explain_text={}, allow_pro_controller={}, allow_handheld={}, "
                  "allow_dual_joycons={}, allow_left_joycon={}, allow_right_joycon={}",
                  parameters.min_players, parameters.max_players,
                  parameters.keep_controllers_connected, parameters.enable_single_mode,
                  parameters.enable_border_color, parameters.enable_explain_text,
                  parameters.allow_pro_controller, parameters.allow_handheld,
                  parameters.allow_dual_joycons, parameters.allow_left_joycon,
                  parameters.allow_right_joycon);

        frontend.ReconfigureControllers([this] { ConfigurationComplete(); }, parameters);
        break;
    }
    case ControllerSupportMode::ShowControllerStrapGuide:
    case ControllerSupportMode::ShowControllerFirmwareUpdate:
    default: {
        ConfigurationComplete();
        break;
    }
    }
}

void Controller::ConfigurationComplete() {
    ControllerSupportResultInfo result_info{};

    const auto& players = Settings::values.players;

    // If enable_single_mode is enabled, player_count is 1 regardless of any other parameters.
    // Otherwise, only count connected players from P1-P8.
    result_info.player_count =
        is_single_mode ? 1
                       : static_cast<s8>(std::count_if(
                             players.begin(), players.end() - 2,
                             [](Settings::PlayerInput player) { return player.connected; }));

    result_info.selected_id = HID::Controller_NPad::IndexToNPad(
        std::distance(players.begin(),
                      std::find_if(players.begin(), players.end(),
                                   [](Settings::PlayerInput player) { return player.connected; })));

    result_info.result = 0;

    LOG_DEBUG(Service_HID, "Result Info: player_count={}, selected_id={}, result={}",
              result_info.player_count, result_info.selected_id, result_info.result);

    complete = true;
    out_data = std::vector<u8>(sizeof(ControllerSupportResultInfo));
    std::memcpy(out_data.data(), &result_info, out_data.size());
    broker.PushNormalDataFromApplet(std::make_shared<IStorage>(std::move(out_data)));
    broker.SignalStateChanged();
}

} // namespace Service::AM::Applets
