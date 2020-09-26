// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "common/common_types.h"
#include "input_common/settings.h"

namespace Settings {

enum class RendererBackend {
    OpenGL = 0,
    Vulkan = 1,
};

enum class GPUAccuracy : u32 {
    Normal = 0,
    High = 1,
    Extreme = 2,
};

enum class CPUAccuracy {
    Accurate = 0,
    Unsafe = 1,
    DebugMode = 2,
};

extern bool configuring_global;

template <typename Type>
class Setting final {
public:
    Setting() = default;
    explicit Setting(Type val) : global{val} {}
    ~Setting() = default;
    void SetGlobal(bool to_global) {
        use_global = to_global;
    }
    bool UsingGlobal() const {
        return use_global;
    }
    Type GetValue(bool need_global = false) const {
        if (use_global || need_global) {
            return global;
        }
        return local;
    }
    void SetValue(const Type& value) {
        if (use_global) {
            global = value;
        } else {
            local = value;
        }
    }

private:
    bool use_global = true;
    Type global{};
    Type local{};
};

struct TouchFromButtonMap {
    std::string name;
    std::vector<std::string> buttons;
};

struct Values {
    // Audio
    std::string audio_device_id;
    std::string sink_id;
    bool audio_muted;
    Setting<bool> enable_audio_stretching;
    Setting<float> volume;

    // Core
    Setting<bool> use_multi_core;

    // Cpu
    CPUAccuracy cpu_accuracy;

    bool cpuopt_page_tables;
    bool cpuopt_block_linking;
    bool cpuopt_return_stack_buffer;
    bool cpuopt_fast_dispatcher;
    bool cpuopt_context_elimination;
    bool cpuopt_const_prop;
    bool cpuopt_misc_ir;
    bool cpuopt_reduce_misalign_checks;

    bool cpuopt_unsafe_unfuse_fma;
    bool cpuopt_unsafe_reduce_fp_error;

    // Renderer
    Setting<RendererBackend> renderer_backend;
    bool renderer_debug;
    Setting<int> vulkan_device;

    Setting<u16> resolution_factor = Setting(static_cast<u16>(1));
    Setting<int> aspect_ratio;
    Setting<int> max_anisotropy;
    Setting<bool> use_frame_limit;
    Setting<u16> frame_limit;
    Setting<bool> use_disk_shader_cache;
    Setting<GPUAccuracy> gpu_accuracy;
    Setting<bool> use_asynchronous_gpu_emulation;
    Setting<bool> use_vsync;
    Setting<bool> use_assembly_shaders;
    Setting<bool> use_asynchronous_shaders;
    Setting<bool> use_fast_gpu_time;

    Setting<float> bg_red;
    Setting<float> bg_green;
    Setting<float> bg_blue;

    // System
    Setting<std::optional<u32>> rng_seed;
    // Measured in seconds since epoch
    Setting<std::optional<std::chrono::seconds>> custom_rtc;
    // Set on game boot, reset on stop. Seconds difference between current time and `custom_rtc`
    std::chrono::seconds custom_rtc_differential;

    s32 current_user;
    Setting<s32> language_index;
    Setting<s32> region_index;
    Setting<s32> time_zone_index;
    Setting<s32> sound_index;

    // Controls
    std::array<PlayerInput, 10> players;

    bool use_docked_mode;

    bool mouse_enabled;
    std::string mouse_device;
    MouseButtonsRaw mouse_buttons;

    bool keyboard_enabled;
    KeyboardKeysRaw keyboard_keys;
    KeyboardModsRaw keyboard_mods;

    bool debug_pad_enabled;
    ButtonsRaw debug_pad_buttons;
    AnalogsRaw debug_pad_analogs;

    bool vibration_enabled;

    bool motion_enabled;
    std::string motion_device;
    std::string touch_device;
    TouchscreenInput touchscreen;
    std::atomic_bool is_device_reload_pending{true};
    bool use_touch_from_button;
    int touch_from_button_map_index;
    std::string udp_input_address;
    u16 udp_input_port;
    u8 udp_pad_index;
    std::vector<TouchFromButtonMap> touch_from_button_maps;

    // Data Storage
    bool use_virtual_sd;
    bool gamecard_inserted;
    bool gamecard_current_game;
    std::string gamecard_path;

    // Debugging
    bool record_frame_times;
    bool use_gdbstub;
    u16 gdbstub_port;
    std::string program_args;
    bool dump_exefs;
    bool dump_nso;
    bool reporting_services;
    bool quest_flag;
    bool disable_macro_jit;

    // Misceallaneous
    std::string log_filter;
    bool use_dev_keys;

    // Services
    std::string bcat_backend;
    bool bcat_boxcat_local;

    // WebService
    bool enable_telemetry;
    std::string web_api_url;
    std::string yuzu_username;
    std::string yuzu_token;

    // Add-Ons
    std::map<u64, std::vector<std::string>> disabled_addons;
} extern values;

float Volume();

bool IsGPULevelExtreme();
bool IsGPULevelHigh();

std::string GetTimeZoneString();

void Apply();
void LogSettings();

// Restore the global state of all applicable settings in the Values struct
void RestoreGlobalState();

// Fixes settings that are known to cause issues with the emulator
void Sanitize();

} // namespace Settings
