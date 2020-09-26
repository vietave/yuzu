// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <atomic>
#include <chrono>

#include "core/settings.h"
#include "video_core/renderer_vulkan/vk_device.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/renderer_vulkan/wrapper.h"

namespace Vulkan {

using namespace std::chrono_literals;

MasterSemaphore::MasterSemaphore(const VKDevice& device) {
    static constexpr VkSemaphoreTypeCreateInfoKHR semaphore_type_ci{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR,
        .initialValue = 0,
    };
    static constexpr VkSemaphoreCreateInfo semaphore_ci{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &semaphore_type_ci,
        .flags = 0,
    };
    semaphore = device.GetLogical().CreateSemaphore(semaphore_ci);

    if (!Settings::values.renderer_debug) {
        return;
    }
    // Validation layers have a bug where they fail to track resource usage when using timeline
    // semaphores and synchronizing with GetSemaphoreCounterValueKHR. To workaround this issue, have
    // a separate thread waiting for each timeline semaphore value.
    debug_thread = std::thread([this] {
        u64 counter = 0;
        while (!shutdown) {
            if (semaphore.Wait(counter, 10'000'000)) {
                ++counter;
            }
        }
    });
}

MasterSemaphore::~MasterSemaphore() {
    shutdown = true;

    // This thread might not be started
    if (debug_thread.joinable()) {
        debug_thread.join();
    }
}

} // namespace Vulkan
