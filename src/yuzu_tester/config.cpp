// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <sstream>
#include <SDL.h>
#include <inih/cpp/INIReader.h>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/param_package.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/settings.h"
#include "input_common/main.h"
#include "yuzu_tester/config.h"
#include "yuzu_tester/default_ini.h"

namespace FS = Common::FS;

Config::Config() {
    // TODO: Don't hardcode the path; let the frontend decide where to put the config files.
    sdl2_config_loc = FS::GetUserPath(FS::UserPath::ConfigDir) + "sdl2-tester-config.ini";
    sdl2_config = std::make_unique<INIReader>(sdl2_config_loc);

    Reload();
}

Config::~Config() = default;

bool Config::LoadINI(const std::string& default_contents, bool retry) {
    const char* location = this->sdl2_config_loc.c_str();
    if (sdl2_config->ParseError() < 0) {
        if (retry) {
            LOG_WARNING(Config, "Failed to load {}. Creating file from defaults...", location);
            FS::CreateFullPath(location);
            FS::WriteStringToFile(true, default_contents, location);
            sdl2_config = std::make_unique<INIReader>(location); // Reopen file

            return LoadINI(default_contents, false);
        }
        LOG_ERROR(Config, "Failed.");
        return false;
    }
    LOG_INFO(Config, "Successfully loaded {}", location);
    return true;
}

void Config::ReadValues() {
    // Controls
    for (std::size_t p = 0; p < Settings::values.players.size(); ++p) {
        for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
            Settings::values.players[p].buttons[i] = "";
        }

        for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
            Settings::values.players[p].analogs[i] = "";
        }
    }

    Settings::values.mouse_enabled = false;
    for (int i = 0; i < Settings::NativeMouseButton::NumMouseButtons; ++i) {
        Settings::values.mouse_buttons[i] = "";
    }

    Settings::values.motion_device = "";

    Settings::values.keyboard_enabled = false;

    Settings::values.debug_pad_enabled = false;
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        Settings::values.debug_pad_buttons[i] = "";
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        Settings::values.debug_pad_analogs[i] = "";
    }

    Settings::values.vibration_enabled = true;
    Settings::values.motion_enabled = true;
    Settings::values.touchscreen.enabled = "";
    Settings::values.touchscreen.device = "";
    Settings::values.touchscreen.finger = 0;
    Settings::values.touchscreen.rotation_angle = 0;
    Settings::values.touchscreen.diameter_x = 15;
    Settings::values.touchscreen.diameter_y = 15;

    Settings::values.use_docked_mode =
        sdl2_config->GetBoolean("Controls", "use_docked_mode", false);

    // Data Storage
    Settings::values.use_virtual_sd =
        sdl2_config->GetBoolean("Data Storage", "use_virtual_sd", true);
    FS::GetUserPath(Common::FS::UserPath::NANDDir,
                    sdl2_config->Get("Data Storage", "nand_directory",
                                     Common::FS::GetUserPath(Common::FS::UserPath::NANDDir)));
    FS::GetUserPath(Common::FS::UserPath::SDMCDir,
                    sdl2_config->Get("Data Storage", "sdmc_directory",
                                     Common::FS::GetUserPath(Common::FS::UserPath::SDMCDir)));

    // System
    Settings::values.current_user = std::clamp<int>(
        sdl2_config->GetInteger("System", "current_user", 0), 0, Service::Account::MAX_USERS - 1);

    const auto rng_seed_enabled = sdl2_config->GetBoolean("System", "rng_seed_enabled", false);
    if (rng_seed_enabled) {
        Settings::values.rng_seed.SetValue(sdl2_config->GetInteger("System", "rng_seed", 0));
    } else {
        Settings::values.rng_seed.SetValue(std::nullopt);
    }

    const auto custom_rtc_enabled = sdl2_config->GetBoolean("System", "custom_rtc_enabled", false);
    if (custom_rtc_enabled) {
        Settings::values.custom_rtc.SetValue(
            std::chrono::seconds(sdl2_config->GetInteger("System", "custom_rtc", 0)));
    } else {
        Settings::values.custom_rtc.SetValue(std::nullopt);
    }

    // Core
    Settings::values.use_multi_core.SetValue(
        sdl2_config->GetBoolean("Core", "use_multi_core", false));

    // Renderer
    Settings::values.aspect_ratio.SetValue(
        static_cast<int>(sdl2_config->GetInteger("Renderer", "aspect_ratio", 0)));
    Settings::values.max_anisotropy.SetValue(
        static_cast<int>(sdl2_config->GetInteger("Renderer", "max_anisotropy", 0)));
    Settings::values.use_frame_limit.SetValue(false);
    Settings::values.frame_limit.SetValue(100);
    Settings::values.use_disk_shader_cache.SetValue(
        sdl2_config->GetBoolean("Renderer", "use_disk_shader_cache", false));
    const int gpu_accuracy_level = sdl2_config->GetInteger("Renderer", "gpu_accuracy", 0);
    Settings::values.gpu_accuracy.SetValue(static_cast<Settings::GPUAccuracy>(gpu_accuracy_level));
    Settings::values.use_asynchronous_gpu_emulation.SetValue(
        sdl2_config->GetBoolean("Renderer", "use_asynchronous_gpu_emulation", false));
    Settings::values.use_fast_gpu_time.SetValue(
        sdl2_config->GetBoolean("Renderer", "use_fast_gpu_time", true));

    Settings::values.bg_red.SetValue(
        static_cast<float>(sdl2_config->GetReal("Renderer", "bg_red", 0.0)));
    Settings::values.bg_green.SetValue(
        static_cast<float>(sdl2_config->GetReal("Renderer", "bg_green", 0.0)));
    Settings::values.bg_blue.SetValue(
        static_cast<float>(sdl2_config->GetReal("Renderer", "bg_blue", 0.0)));

    // Audio
    Settings::values.sink_id = "null";
    Settings::values.enable_audio_stretching.SetValue(false);
    Settings::values.audio_device_id = "auto";
    Settings::values.volume.SetValue(0);

    Settings::values.language_index.SetValue(
        sdl2_config->GetInteger("System", "language_index", 1));

    // Miscellaneous
    Settings::values.log_filter = sdl2_config->Get("Miscellaneous", "log_filter", "*:Trace");
    Settings::values.use_dev_keys = sdl2_config->GetBoolean("Miscellaneous", "use_dev_keys", false);

    // Debugging
    Settings::values.use_gdbstub = false;
    Settings::values.program_args = "";
    Settings::values.dump_exefs = sdl2_config->GetBoolean("Debugging", "dump_exefs", false);
    Settings::values.dump_nso = sdl2_config->GetBoolean("Debugging", "dump_nso", false);

    const auto title_list = sdl2_config->Get("AddOns", "title_ids", "");
    std::stringstream ss(title_list);
    std::string line;
    while (std::getline(ss, line, '|')) {
        const auto title_id = std::stoul(line, nullptr, 16);
        const auto disabled_list = sdl2_config->Get("AddOns", "disabled_" + line, "");

        std::stringstream inner_ss(disabled_list);
        std::string inner_line;
        std::vector<std::string> out;
        while (std::getline(inner_ss, inner_line, '|')) {
            out.push_back(inner_line);
        }

        Settings::values.disabled_addons.insert_or_assign(title_id, out);
    }

    // Web Service
    Settings::values.enable_telemetry =
        sdl2_config->GetBoolean("WebService", "enable_telemetry", true);
    Settings::values.web_api_url =
        sdl2_config->Get("WebService", "web_api_url", "https://api.yuzu-emu.org");
    Settings::values.yuzu_username = sdl2_config->Get("WebService", "yuzu_username", "");
    Settings::values.yuzu_token = sdl2_config->Get("WebService", "yuzu_token", "");
}

void Config::Reload() {
    LoadINI(DefaultINI::sdl2_config_file);
    ReadValues();
}
