// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <vector>

#include <boost/container/static_vector.hpp>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/settings.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "video_core/renderer_vulkan/maxwell_to_vk.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/renderer_vulkan/vk_buffer_cache.h"
#include "video_core/renderer_vulkan/vk_compute_pass.h"
#include "video_core/renderer_vulkan/vk_compute_pipeline.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_device.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_renderpass_cache.h"
#include "video_core/renderer_vulkan/vk_sampler_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/renderer_vulkan/wrapper.h"
#include "video_core/shader_cache.h"

namespace Vulkan {

using Maxwell = Tegra::Engines::Maxwell3D::Regs;

MICROPROFILE_DEFINE(Vulkan_WaitForWorker, "Vulkan", "Wait for worker", MP_RGB(255, 192, 192));
MICROPROFILE_DEFINE(Vulkan_Drawing, "Vulkan", "Record drawing", MP_RGB(192, 128, 128));
MICROPROFILE_DEFINE(Vulkan_Compute, "Vulkan", "Record compute", MP_RGB(192, 128, 128));
MICROPROFILE_DEFINE(Vulkan_Clearing, "Vulkan", "Record clearing", MP_RGB(192, 128, 128));
MICROPROFILE_DEFINE(Vulkan_Geometry, "Vulkan", "Setup geometry", MP_RGB(192, 128, 128));
MICROPROFILE_DEFINE(Vulkan_ConstBuffers, "Vulkan", "Setup constant buffers", MP_RGB(192, 128, 128));
MICROPROFILE_DEFINE(Vulkan_GlobalBuffers, "Vulkan", "Setup global buffers", MP_RGB(192, 128, 128));
MICROPROFILE_DEFINE(Vulkan_RenderTargets, "Vulkan", "Setup render targets", MP_RGB(192, 128, 128));
MICROPROFILE_DEFINE(Vulkan_Textures, "Vulkan", "Setup textures", MP_RGB(192, 128, 128));
MICROPROFILE_DEFINE(Vulkan_Images, "Vulkan", "Setup images", MP_RGB(192, 128, 128));
MICROPROFILE_DEFINE(Vulkan_PipelineCache, "Vulkan", "Pipeline cache", MP_RGB(192, 128, 128));

namespace {

constexpr auto ComputeShaderIndex = static_cast<std::size_t>(Tegra::Engines::ShaderType::Compute);

VkViewport GetViewportState(const VKDevice& device, const Maxwell& regs, std::size_t index) {
    const auto& src = regs.viewport_transform[index];
    const float width = src.scale_x * 2.0f;
    const float height = src.scale_y * 2.0f;
    const float reduce_z = regs.depth_mode == Maxwell::DepthMode::MinusOneToOne ? 1.0f : 0.0f;

    VkViewport viewport{
        .x = src.translate_x - src.scale_x,
        .y = src.translate_y - src.scale_y,
        .width = width != 0.0f ? width : 1.0f,
        .height = height != 0.0f ? height : 1.0f,
        .minDepth = src.translate_z - src.scale_z * reduce_z,
        .maxDepth = src.translate_z + src.scale_z,
    };

    if (!device.IsExtDepthRangeUnrestrictedSupported()) {
        viewport.minDepth = std::clamp(viewport.minDepth, 0.0f, 1.0f);
        viewport.maxDepth = std::clamp(viewport.maxDepth, 0.0f, 1.0f);
    }

    return viewport;
}

VkRect2D GetScissorState(const Maxwell& regs, std::size_t index) {
    const auto& src = regs.scissor_test[index];
    VkRect2D scissor;
    if (src.enable) {
        scissor.offset.x = static_cast<s32>(src.min_x);
        scissor.offset.y = static_cast<s32>(src.min_y);
        scissor.extent.width = src.max_x - src.min_x;
        scissor.extent.height = src.max_y - src.min_y;
    } else {
        scissor.offset.x = 0;
        scissor.offset.y = 0;
        scissor.extent.width = std::numeric_limits<s32>::max();
        scissor.extent.height = std::numeric_limits<s32>::max();
    }
    return scissor;
}

std::array<GPUVAddr, Maxwell::MaxShaderProgram> GetShaderAddresses(
    const std::array<Shader*, Maxwell::MaxShaderProgram>& shaders) {
    std::array<GPUVAddr, Maxwell::MaxShaderProgram> addresses;
    for (std::size_t i = 0; i < std::size(addresses); ++i) {
        addresses[i] = shaders[i] ? shaders[i]->GetGpuAddr() : 0;
    }
    return addresses;
}

void TransitionImages(const std::vector<ImageView>& views, VkPipelineStageFlags pipeline_stage,
                      VkAccessFlags access) {
    for (auto& [view, layout] : views) {
        view->Transition(*layout, pipeline_stage, access);
    }
}

template <typename Engine, typename Entry>
Tegra::Texture::FullTextureInfo GetTextureInfo(const Engine& engine, const Entry& entry,
                                               std::size_t stage, std::size_t index = 0) {
    const auto stage_type = static_cast<Tegra::Engines::ShaderType>(stage);
    if constexpr (std::is_same_v<Entry, SamplerEntry>) {
        if (entry.is_separated) {
            const u32 buffer_1 = entry.buffer;
            const u32 buffer_2 = entry.secondary_buffer;
            const u32 offset_1 = entry.offset;
            const u32 offset_2 = entry.secondary_offset;
            const u32 handle_1 = engine.AccessConstBuffer32(stage_type, buffer_1, offset_1);
            const u32 handle_2 = engine.AccessConstBuffer32(stage_type, buffer_2, offset_2);
            return engine.GetTextureInfo(handle_1 | handle_2);
        }
    }
    if (entry.is_bindless) {
        const auto tex_handle = engine.AccessConstBuffer32(stage_type, entry.buffer, entry.offset);
        return engine.GetTextureInfo(tex_handle);
    }
    const auto& gpu_profile = engine.AccessGuestDriverProfile();
    const u32 entry_offset = static_cast<u32>(index * gpu_profile.GetTextureHandlerSize());
    const u32 offset = entry.offset + entry_offset;
    if constexpr (std::is_same_v<Engine, Tegra::Engines::Maxwell3D>) {
        return engine.GetStageTexture(stage_type, offset);
    } else {
        return engine.GetTexture(offset);
    }
}

/// @brief Determine if an attachment to be updated has to preserve contents
/// @param is_clear True when a clear is being executed
/// @param regs 3D registers
/// @return True when the contents have to be preserved
bool HasToPreserveColorContents(bool is_clear, const Maxwell& regs) {
    if (!is_clear) {
        return true;
    }
    // First we have to make sure all clear masks are enabled.
    if (!regs.clear_buffers.R || !regs.clear_buffers.G || !regs.clear_buffers.B ||
        !regs.clear_buffers.A) {
        return true;
    }
    // If scissors are disabled, the whole screen is cleared
    if (!regs.clear_flags.scissor) {
        return false;
    }
    // Then we have to confirm scissor testing clears the whole image
    const std::size_t index = regs.clear_buffers.RT;
    const auto& scissor = regs.scissor_test[0];
    return scissor.min_x > 0 || scissor.min_y > 0 || scissor.max_x < regs.rt[index].width ||
           scissor.max_y < regs.rt[index].height;
}

/// @brief Determine if an attachment to be updated has to preserve contents
/// @param is_clear True when a clear is being executed
/// @param regs 3D registers
/// @return True when the contents have to be preserved
bool HasToPreserveDepthContents(bool is_clear, const Maxwell& regs) {
    // If we are not clearing, the contents have to be preserved
    if (!is_clear) {
        return true;
    }
    // For depth stencil clears we only have to confirm scissor test covers the whole image
    if (!regs.clear_flags.scissor) {
        return false;
    }
    // Make sure the clear cover the whole image
    const auto& scissor = regs.scissor_test[0];
    return scissor.min_x > 0 || scissor.min_y > 0 || scissor.max_x < regs.zeta_width ||
           scissor.max_y < regs.zeta_height;
}

template <std::size_t N>
std::array<VkDeviceSize, N> ExpandStrides(const std::array<u16, N>& strides) {
    std::array<VkDeviceSize, N> expanded;
    std::copy(strides.begin(), strides.end(), expanded.begin());
    return expanded;
}

} // Anonymous namespace

class BufferBindings final {
public:
    void AddVertexBinding(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, u32 stride) {
        vertex.buffers[vertex.num_buffers] = buffer;
        vertex.offsets[vertex.num_buffers] = offset;
        vertex.sizes[vertex.num_buffers] = size;
        vertex.strides[vertex.num_buffers] = static_cast<u16>(stride);
        ++vertex.num_buffers;
    }

    void SetIndexBinding(VkBuffer buffer, VkDeviceSize offset, VkIndexType type) {
        index.buffer = buffer;
        index.offset = offset;
        index.type = type;
    }

    void Bind(const VKDevice& device, VKScheduler& scheduler) const {
        // Use this large switch case to avoid dispatching more memory in the record lambda than
        // what we need. It looks horrible, but it's the best we can do on standard C++.
        switch (vertex.num_buffers) {
        case 0:
            return BindStatic<0>(device, scheduler);
        case 1:
            return BindStatic<1>(device, scheduler);
        case 2:
            return BindStatic<2>(device, scheduler);
        case 3:
            return BindStatic<3>(device, scheduler);
        case 4:
            return BindStatic<4>(device, scheduler);
        case 5:
            return BindStatic<5>(device, scheduler);
        case 6:
            return BindStatic<6>(device, scheduler);
        case 7:
            return BindStatic<7>(device, scheduler);
        case 8:
            return BindStatic<8>(device, scheduler);
        case 9:
            return BindStatic<9>(device, scheduler);
        case 10:
            return BindStatic<10>(device, scheduler);
        case 11:
            return BindStatic<11>(device, scheduler);
        case 12:
            return BindStatic<12>(device, scheduler);
        case 13:
            return BindStatic<13>(device, scheduler);
        case 14:
            return BindStatic<14>(device, scheduler);
        case 15:
            return BindStatic<15>(device, scheduler);
        case 16:
            return BindStatic<16>(device, scheduler);
        case 17:
            return BindStatic<17>(device, scheduler);
        case 18:
            return BindStatic<18>(device, scheduler);
        case 19:
            return BindStatic<19>(device, scheduler);
        case 20:
            return BindStatic<20>(device, scheduler);
        case 21:
            return BindStatic<21>(device, scheduler);
        case 22:
            return BindStatic<22>(device, scheduler);
        case 23:
            return BindStatic<23>(device, scheduler);
        case 24:
            return BindStatic<24>(device, scheduler);
        case 25:
            return BindStatic<25>(device, scheduler);
        case 26:
            return BindStatic<26>(device, scheduler);
        case 27:
            return BindStatic<27>(device, scheduler);
        case 28:
            return BindStatic<28>(device, scheduler);
        case 29:
            return BindStatic<29>(device, scheduler);
        case 30:
            return BindStatic<30>(device, scheduler);
        case 31:
            return BindStatic<31>(device, scheduler);
        case 32:
            return BindStatic<32>(device, scheduler);
        }
        UNREACHABLE();
    }

private:
    // Some of these fields are intentionally left uninitialized to avoid initializing them twice.
    struct {
        std::size_t num_buffers = 0;
        std::array<VkBuffer, Maxwell::NumVertexArrays> buffers;
        std::array<VkDeviceSize, Maxwell::NumVertexArrays> offsets;
        std::array<VkDeviceSize, Maxwell::NumVertexArrays> sizes;
        std::array<u16, Maxwell::NumVertexArrays> strides;
    } vertex;

    struct {
        VkBuffer buffer = nullptr;
        VkDeviceSize offset;
        VkIndexType type;
    } index;

    template <std::size_t N>
    void BindStatic(const VKDevice& device, VKScheduler& scheduler) const {
        if (device.IsExtExtendedDynamicStateSupported()) {
            if (index.buffer) {
                BindStatic<N, true, true>(scheduler);
            } else {
                BindStatic<N, false, true>(scheduler);
            }
        } else {
            if (index.buffer) {
                BindStatic<N, true, false>(scheduler);
            } else {
                BindStatic<N, false, false>(scheduler);
            }
        }
    }

    template <std::size_t N, bool is_indexed, bool has_extended_dynamic_state>
    void BindStatic(VKScheduler& scheduler) const {
        static_assert(N <= Maxwell::NumVertexArrays);
        if constexpr (N == 0) {
            return;
        }

        std::array<VkBuffer, N> buffers;
        std::array<VkDeviceSize, N> offsets;
        std::copy(vertex.buffers.begin(), vertex.buffers.begin() + N, buffers.begin());
        std::copy(vertex.offsets.begin(), vertex.offsets.begin() + N, offsets.begin());

        if constexpr (has_extended_dynamic_state) {
            // With extended dynamic states we can specify the length and stride of a vertex buffer
            std::array<VkDeviceSize, N> sizes;
            std::array<u16, N> strides;
            std::copy(vertex.sizes.begin(), vertex.sizes.begin() + N, sizes.begin());
            std::copy(vertex.strides.begin(), vertex.strides.begin() + N, strides.begin());

            if constexpr (is_indexed) {
                scheduler.Record(
                    [buffers, offsets, sizes, strides, index = index](vk::CommandBuffer cmdbuf) {
                        cmdbuf.BindIndexBuffer(index.buffer, index.offset, index.type);
                        cmdbuf.BindVertexBuffers2EXT(0, static_cast<u32>(N), buffers.data(),
                                                     offsets.data(), sizes.data(),
                                                     ExpandStrides(strides).data());
                    });
            } else {
                scheduler.Record([buffers, offsets, sizes, strides](vk::CommandBuffer cmdbuf) {
                    cmdbuf.BindVertexBuffers2EXT(0, static_cast<u32>(N), buffers.data(),
                                                 offsets.data(), sizes.data(),
                                                 ExpandStrides(strides).data());
                });
            }
            return;
        }

        if constexpr (is_indexed) {
            // Indexed draw
            scheduler.Record([buffers, offsets, index = index](vk::CommandBuffer cmdbuf) {
                cmdbuf.BindIndexBuffer(index.buffer, index.offset, index.type);
                cmdbuf.BindVertexBuffers(0, static_cast<u32>(N), buffers.data(), offsets.data());
            });
        } else {
            // Array draw
            scheduler.Record([buffers, offsets](vk::CommandBuffer cmdbuf) {
                cmdbuf.BindVertexBuffers(0, static_cast<u32>(N), buffers.data(), offsets.data());
            });
        }
    }
};

void RasterizerVulkan::DrawParameters::Draw(vk::CommandBuffer cmdbuf) const {
    if (is_indexed) {
        cmdbuf.DrawIndexed(num_vertices, num_instances, 0, base_vertex, base_instance);
    } else {
        cmdbuf.Draw(num_vertices, num_instances, base_vertex, base_instance);
    }
}

RasterizerVulkan::RasterizerVulkan(Core::Frontend::EmuWindow& emu_window, Tegra::GPU& gpu_,
                                   Tegra::MemoryManager& gpu_memory_,
                                   Core::Memory::Memory& cpu_memory, VKScreenInfo& screen_info_,
                                   const VKDevice& device_, VKMemoryManager& memory_manager_,
                                   StateTracker& state_tracker_, VKScheduler& scheduler_)
    : RasterizerAccelerated(cpu_memory), gpu(gpu_), gpu_memory(gpu_memory_),
      maxwell3d(gpu.Maxwell3D()), kepler_compute(gpu.KeplerCompute()), screen_info(screen_info_),
      device(device_), memory_manager(memory_manager_), state_tracker(state_tracker_),
      scheduler(scheduler_), staging_pool(device, memory_manager, scheduler),
      descriptor_pool(device, scheduler_), update_descriptor_queue(device, scheduler),
      renderpass_cache(device),
      quad_array_pass(device, scheduler, descriptor_pool, staging_pool, update_descriptor_queue),
      quad_indexed_pass(device, scheduler, descriptor_pool, staging_pool, update_descriptor_queue),
      uint8_pass(device, scheduler, descriptor_pool, staging_pool, update_descriptor_queue),
      texture_cache(*this, maxwell3d, gpu_memory, device, memory_manager, scheduler, staging_pool),
      pipeline_cache(*this, gpu, maxwell3d, kepler_compute, gpu_memory, device, scheduler,
                     descriptor_pool, update_descriptor_queue, renderpass_cache),
      buffer_cache(*this, gpu_memory, cpu_memory, device, memory_manager, scheduler, staging_pool),
      sampler_cache(device), query_cache(*this, maxwell3d, gpu_memory, device, scheduler),
      fence_manager(*this, gpu, gpu_memory, texture_cache, buffer_cache, query_cache, device,
                    scheduler),
      wfi_event(device.GetLogical().CreateEvent()), async_shaders(emu_window) {
    scheduler.SetQueryCache(query_cache);
    if (device.UseAsynchronousShaders()) {
        async_shaders.AllocateWorkers();
    }
}

RasterizerVulkan::~RasterizerVulkan() = default;

void RasterizerVulkan::Draw(bool is_indexed, bool is_instanced) {
    MICROPROFILE_SCOPE(Vulkan_Drawing);

    SCOPE_EXIT({ gpu.TickWork(); });
    FlushWork();

    query_cache.UpdateCounters();

    GraphicsPipelineCacheKey key;
    key.fixed_state.Fill(maxwell3d.regs, device.IsExtExtendedDynamicStateSupported());

    buffer_cache.Map(CalculateGraphicsStreamBufferSize(is_indexed));

    BufferBindings buffer_bindings;
    const DrawParameters draw_params =
        SetupGeometry(key.fixed_state, buffer_bindings, is_indexed, is_instanced);

    update_descriptor_queue.Acquire();
    sampled_views.clear();
    image_views.clear();

    const auto shaders = pipeline_cache.GetShaders();
    key.shaders = GetShaderAddresses(shaders);
    SetupShaderDescriptors(shaders);

    buffer_cache.Unmap();

    const Texceptions texceptions = UpdateAttachments(false);
    SetupImageTransitions(texceptions, color_attachments, zeta_attachment);

    key.renderpass_params = GetRenderPassParams(texceptions);
    key.padding = 0;

    auto* pipeline = pipeline_cache.GetGraphicsPipeline(key, async_shaders);
    if (pipeline == nullptr || pipeline->GetHandle() == VK_NULL_HANDLE) {
        // Async graphics pipeline was not ready.
        return;
    }

    scheduler.BindGraphicsPipeline(pipeline->GetHandle());

    const auto renderpass = pipeline->GetRenderPass();
    const auto [framebuffer, render_area] = ConfigureFramebuffers(renderpass);
    scheduler.RequestRenderpass(renderpass, framebuffer, render_area);

    UpdateDynamicStates();

    buffer_bindings.Bind(device, scheduler);

    BeginTransformFeedback();

    const auto pipeline_layout = pipeline->GetLayout();
    const auto descriptor_set = pipeline->CommitDescriptorSet();
    scheduler.Record([pipeline_layout, descriptor_set, draw_params](vk::CommandBuffer cmdbuf) {
        if (descriptor_set) {
            cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
                                      DESCRIPTOR_SET, descriptor_set, {});
        }
        draw_params.Draw(cmdbuf);
    });

    EndTransformFeedback();
}

void RasterizerVulkan::Clear() {
    MICROPROFILE_SCOPE(Vulkan_Clearing);

    if (!maxwell3d.ShouldExecute()) {
        return;
    }

    sampled_views.clear();
    image_views.clear();

    query_cache.UpdateCounters();

    const auto& regs = maxwell3d.regs;
    const bool use_color = regs.clear_buffers.R || regs.clear_buffers.G || regs.clear_buffers.B ||
                           regs.clear_buffers.A;
    const bool use_depth = regs.clear_buffers.Z;
    const bool use_stencil = regs.clear_buffers.S;
    if (!use_color && !use_depth && !use_stencil) {
        return;
    }

    [[maybe_unused]] const auto texceptions = UpdateAttachments(true);
    DEBUG_ASSERT(texceptions.none());
    SetupImageTransitions(0, color_attachments, zeta_attachment);

    const VkRenderPass renderpass = renderpass_cache.GetRenderPass(GetRenderPassParams(0));
    const auto [framebuffer, render_area] = ConfigureFramebuffers(renderpass);
    scheduler.RequestRenderpass(renderpass, framebuffer, render_area);

    VkClearRect clear_rect;
    clear_rect.baseArrayLayer = regs.clear_buffers.layer;
    clear_rect.layerCount = 1;
    clear_rect.rect = GetScissorState(regs, 0);
    clear_rect.rect.extent.width = std::min(clear_rect.rect.extent.width, render_area.width);
    clear_rect.rect.extent.height = std::min(clear_rect.rect.extent.height, render_area.height);

    if (use_color) {
        VkClearValue clear_value;
        std::memcpy(clear_value.color.float32, regs.clear_color, sizeof(regs.clear_color));

        const u32 color_attachment = regs.clear_buffers.RT;
        scheduler.Record([color_attachment, clear_value, clear_rect](vk::CommandBuffer cmdbuf) {
            const VkClearAttachment attachment{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .colorAttachment = color_attachment,
                .clearValue = clear_value,
            };
            cmdbuf.ClearAttachments(attachment, clear_rect);
        });
    }

    if (!use_depth && !use_stencil) {
        return;
    }
    VkImageAspectFlags aspect_flags = 0;
    if (use_depth) {
        aspect_flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (use_stencil) {
        aspect_flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    scheduler.Record([clear_depth = regs.clear_depth, clear_stencil = regs.clear_stencil,
                      clear_rect, aspect_flags](vk::CommandBuffer cmdbuf) {
        VkClearAttachment attachment;
        attachment.aspectMask = aspect_flags;
        attachment.colorAttachment = 0;
        attachment.clearValue.depthStencil.depth = clear_depth;
        attachment.clearValue.depthStencil.stencil = clear_stencil;
        cmdbuf.ClearAttachments(attachment, clear_rect);
    });
}

void RasterizerVulkan::DispatchCompute(GPUVAddr code_addr) {
    MICROPROFILE_SCOPE(Vulkan_Compute);
    update_descriptor_queue.Acquire();
    sampled_views.clear();
    image_views.clear();

    query_cache.UpdateCounters();

    const auto& launch_desc = kepler_compute.launch_description;
    auto& pipeline = pipeline_cache.GetComputePipeline({
        .shader = code_addr,
        .shared_memory_size = launch_desc.shared_alloc,
        .workgroup_size =
            {
                launch_desc.block_dim_x,
                launch_desc.block_dim_y,
                launch_desc.block_dim_z,
            },
    });

    // Compute dispatches can't be executed inside a renderpass
    scheduler.RequestOutsideRenderPassOperationContext();

    buffer_cache.Map(CalculateComputeStreamBufferSize());

    const auto& entries = pipeline.GetEntries();
    SetupComputeConstBuffers(entries);
    SetupComputeGlobalBuffers(entries);
    SetupComputeUniformTexels(entries);
    SetupComputeTextures(entries);
    SetupComputeStorageTexels(entries);
    SetupComputeImages(entries);

    buffer_cache.Unmap();

    TransitionImages(sampled_views, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_SHADER_READ_BIT);
    TransitionImages(image_views, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

    scheduler.Record([grid_x = launch_desc.grid_dim_x, grid_y = launch_desc.grid_dim_y,
                      grid_z = launch_desc.grid_dim_z, pipeline_handle = pipeline.GetHandle(),
                      layout = pipeline.GetLayout(),
                      descriptor_set = pipeline.CommitDescriptorSet()](vk::CommandBuffer cmdbuf) {
        cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_handle);
        cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, layout, DESCRIPTOR_SET,
                                  descriptor_set, {});
        cmdbuf.Dispatch(grid_x, grid_y, grid_z);
    });
}

void RasterizerVulkan::ResetCounter(VideoCore::QueryType type) {
    query_cache.ResetCounter(type);
}

void RasterizerVulkan::Query(GPUVAddr gpu_addr, VideoCore::QueryType type,
                             std::optional<u64> timestamp) {
    query_cache.Query(gpu_addr, type, timestamp);
}

void RasterizerVulkan::FlushAll() {}

void RasterizerVulkan::FlushRegion(VAddr addr, u64 size) {
    if (addr == 0 || size == 0) {
        return;
    }
    texture_cache.FlushRegion(addr, size);
    buffer_cache.FlushRegion(addr, size);
    query_cache.FlushRegion(addr, size);
}

bool RasterizerVulkan::MustFlushRegion(VAddr addr, u64 size) {
    if (!Settings::IsGPULevelHigh()) {
        return buffer_cache.MustFlushRegion(addr, size);
    }
    return texture_cache.MustFlushRegion(addr, size) || buffer_cache.MustFlushRegion(addr, size);
}

void RasterizerVulkan::InvalidateRegion(VAddr addr, u64 size) {
    if (addr == 0 || size == 0) {
        return;
    }
    texture_cache.InvalidateRegion(addr, size);
    pipeline_cache.InvalidateRegion(addr, size);
    buffer_cache.InvalidateRegion(addr, size);
    query_cache.InvalidateRegion(addr, size);
}

void RasterizerVulkan::OnCPUWrite(VAddr addr, u64 size) {
    if (addr == 0 || size == 0) {
        return;
    }
    texture_cache.OnCPUWrite(addr, size);
    pipeline_cache.OnCPUWrite(addr, size);
    buffer_cache.OnCPUWrite(addr, size);
}

void RasterizerVulkan::SyncGuestHost() {
    texture_cache.SyncGuestHost();
    buffer_cache.SyncGuestHost();
    pipeline_cache.SyncGuestHost();
}

void RasterizerVulkan::SignalSemaphore(GPUVAddr addr, u32 value) {
    if (!gpu.IsAsync()) {
        gpu_memory.Write<u32>(addr, value);
        return;
    }
    fence_manager.SignalSemaphore(addr, value);
}

void RasterizerVulkan::SignalSyncPoint(u32 value) {
    if (!gpu.IsAsync()) {
        gpu.IncrementSyncPoint(value);
        return;
    }
    fence_manager.SignalSyncPoint(value);
}

void RasterizerVulkan::ReleaseFences() {
    if (!gpu.IsAsync()) {
        return;
    }
    fence_manager.WaitPendingFences();
}

void RasterizerVulkan::FlushAndInvalidateRegion(VAddr addr, u64 size) {
    if (Settings::IsGPULevelExtreme()) {
        FlushRegion(addr, size);
    }
    InvalidateRegion(addr, size);
}

void RasterizerVulkan::WaitForIdle() {
    // Everything but wait pixel operations. This intentionally includes FRAGMENT_SHADER_BIT because
    // fragment shaders can still write storage buffers.
    VkPipelineStageFlags flags =
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
        VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
        VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (device.IsExtTransformFeedbackSupported()) {
        flags |= VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT;
    }

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([event = *wfi_event, flags](vk::CommandBuffer cmdbuf) {
        cmdbuf.SetEvent(event, flags);
        cmdbuf.WaitEvents(event, flags, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, {}, {}, {});
    });
}

void RasterizerVulkan::FlushCommands() {
    if (draw_counter > 0) {
        draw_counter = 0;
        scheduler.Flush();
    }
}

void RasterizerVulkan::TickFrame() {
    draw_counter = 0;
    update_descriptor_queue.TickFrame();
    buffer_cache.TickFrame();
    staging_pool.TickFrame();
}

bool RasterizerVulkan::AccelerateSurfaceCopy(const Tegra::Engines::Fermi2D::Regs::Surface& src,
                                             const Tegra::Engines::Fermi2D::Regs::Surface& dst,
                                             const Tegra::Engines::Fermi2D::Config& copy_config) {
    texture_cache.DoFermiCopy(src, dst, copy_config);
    return true;
}

bool RasterizerVulkan::AccelerateDisplay(const Tegra::FramebufferConfig& config,
                                         VAddr framebuffer_addr, u32 pixel_stride) {
    if (!framebuffer_addr) {
        return false;
    }

    const auto surface{texture_cache.TryFindFramebufferSurface(framebuffer_addr)};
    if (!surface) {
        return false;
    }

    // Verify that the cached surface is the same size and format as the requested framebuffer
    const auto& params{surface->GetSurfaceParams()};
    ASSERT_MSG(params.width == config.width, "Framebuffer width is different");
    ASSERT_MSG(params.height == config.height, "Framebuffer height is different");

    screen_info.image = &surface->GetImage();
    screen_info.width = params.width;
    screen_info.height = params.height;
    screen_info.is_srgb = surface->GetSurfaceParams().srgb_conversion;
    return true;
}

void RasterizerVulkan::FlushWork() {
    static constexpr u32 DRAWS_TO_DISPATCH = 4096;

    // Only check multiples of 8 draws
    static_assert(DRAWS_TO_DISPATCH % 8 == 0);
    if ((++draw_counter & 7) != 7) {
        return;
    }

    if (draw_counter < DRAWS_TO_DISPATCH) {
        // Send recorded tasks to the worker thread
        scheduler.DispatchWork();
        return;
    }

    // Otherwise (every certain number of draws) flush execution.
    // This submits commands to the Vulkan driver.
    scheduler.Flush();
    draw_counter = 0;
}

RasterizerVulkan::Texceptions RasterizerVulkan::UpdateAttachments(bool is_clear) {
    MICROPROFILE_SCOPE(Vulkan_RenderTargets);

    const auto& regs = maxwell3d.regs;
    auto& dirty = maxwell3d.dirty.flags;
    const bool update_rendertargets = dirty[VideoCommon::Dirty::RenderTargets];
    dirty[VideoCommon::Dirty::RenderTargets] = false;

    texture_cache.GuardRenderTargets(true);

    Texceptions texceptions;
    for (std::size_t rt = 0; rt < Maxwell::NumRenderTargets; ++rt) {
        if (update_rendertargets) {
            const bool preserve_contents = HasToPreserveColorContents(is_clear, regs);
            color_attachments[rt] = texture_cache.GetColorBufferSurface(rt, preserve_contents);
        }
        if (color_attachments[rt] && WalkAttachmentOverlaps(*color_attachments[rt])) {
            texceptions[rt] = true;
        }
    }

    if (update_rendertargets) {
        const bool preserve_contents = HasToPreserveDepthContents(is_clear, regs);
        zeta_attachment = texture_cache.GetDepthBufferSurface(preserve_contents);
    }
    if (zeta_attachment && WalkAttachmentOverlaps(*zeta_attachment)) {
        texceptions[ZETA_TEXCEPTION_INDEX] = true;
    }

    texture_cache.GuardRenderTargets(false);

    return texceptions;
}

bool RasterizerVulkan::WalkAttachmentOverlaps(const CachedSurfaceView& attachment) {
    bool overlap = false;
    for (auto& [view, layout] : sampled_views) {
        if (!attachment.IsSameSurface(*view)) {
            continue;
        }
        overlap = true;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
    }
    return overlap;
}

std::tuple<VkFramebuffer, VkExtent2D> RasterizerVulkan::ConfigureFramebuffers(
    VkRenderPass renderpass) {
    FramebufferCacheKey key{
        .renderpass = renderpass,
        .width = std::numeric_limits<u32>::max(),
        .height = std::numeric_limits<u32>::max(),
        .layers = std::numeric_limits<u32>::max(),
        .views = {},
    };

    const auto try_push = [&key](const View& view) {
        if (!view) {
            return false;
        }
        key.views.push_back(view->GetAttachment());
        key.width = std::min(key.width, view->GetWidth());
        key.height = std::min(key.height, view->GetHeight());
        key.layers = std::min(key.layers, view->GetNumLayers());
        return true;
    };

    const auto& regs = maxwell3d.regs;
    const std::size_t num_attachments = static_cast<std::size_t>(regs.rt_control.count);
    for (std::size_t index = 0; index < num_attachments; ++index) {
        if (try_push(color_attachments[index])) {
            texture_cache.MarkColorBufferInUse(index);
        }
    }
    if (try_push(zeta_attachment)) {
        texture_cache.MarkDepthBufferInUse();
    }

    const auto [fbentry, is_cache_miss] = framebuffer_cache.try_emplace(key);
    auto& framebuffer = fbentry->second;
    if (is_cache_miss) {
        framebuffer = device.GetLogical().CreateFramebuffer({
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .renderPass = key.renderpass,
            .attachmentCount = static_cast<u32>(key.views.size()),
            .pAttachments = key.views.data(),
            .width = key.width,
            .height = key.height,
            .layers = key.layers,
        });
    }

    return {*framebuffer, VkExtent2D{key.width, key.height}};
}

RasterizerVulkan::DrawParameters RasterizerVulkan::SetupGeometry(FixedPipelineState& fixed_state,
                                                                 BufferBindings& buffer_bindings,
                                                                 bool is_indexed,
                                                                 bool is_instanced) {
    MICROPROFILE_SCOPE(Vulkan_Geometry);

    const auto& regs = maxwell3d.regs;

    SetupVertexArrays(buffer_bindings);

    const u32 base_instance = regs.vb_base_instance;
    const u32 num_instances = is_instanced ? maxwell3d.mme_draw.instance_count : 1;
    const u32 base_vertex = is_indexed ? regs.vb_element_base : regs.vertex_buffer.first;
    const u32 num_vertices = is_indexed ? regs.index_array.count : regs.vertex_buffer.count;

    DrawParameters params{base_instance, num_instances, base_vertex, num_vertices, is_indexed};
    SetupIndexBuffer(buffer_bindings, params, is_indexed);

    return params;
}

void RasterizerVulkan::SetupShaderDescriptors(
    const std::array<Shader*, Maxwell::MaxShaderProgram>& shaders) {
    texture_cache.GuardSamplers(true);

    for (std::size_t stage = 0; stage < Maxwell::MaxShaderStage; ++stage) {
        // Skip VertexA stage
        Shader* const shader = shaders[stage + 1];
        if (!shader) {
            continue;
        }
        const auto& entries = shader->GetEntries();
        SetupGraphicsConstBuffers(entries, stage);
        SetupGraphicsGlobalBuffers(entries, stage);
        SetupGraphicsUniformTexels(entries, stage);
        SetupGraphicsTextures(entries, stage);
        SetupGraphicsStorageTexels(entries, stage);
        SetupGraphicsImages(entries, stage);
    }
    texture_cache.GuardSamplers(false);
}

void RasterizerVulkan::SetupImageTransitions(
    Texceptions texceptions, const std::array<View, Maxwell::NumRenderTargets>& color_attachments,
    const View& zeta_attachment) {
    TransitionImages(sampled_views, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_SHADER_READ_BIT);
    TransitionImages(image_views, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

    for (std::size_t rt = 0; rt < std::size(color_attachments); ++rt) {
        const auto color_attachment = color_attachments[rt];
        if (color_attachment == nullptr) {
            continue;
        }
        const auto image_layout =
            texceptions[rt] ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment->Transition(image_layout, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                     VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    }

    if (zeta_attachment != nullptr) {
        const auto image_layout = texceptions[ZETA_TEXCEPTION_INDEX]
                                      ? VK_IMAGE_LAYOUT_GENERAL
                                      : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        zeta_attachment->Transition(image_layout, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }
}

void RasterizerVulkan::UpdateDynamicStates() {
    auto& regs = maxwell3d.regs;
    UpdateViewportsState(regs);
    UpdateScissorsState(regs);
    UpdateDepthBias(regs);
    UpdateBlendConstants(regs);
    UpdateDepthBounds(regs);
    UpdateStencilFaces(regs);
    if (device.IsExtExtendedDynamicStateSupported()) {
        UpdateCullMode(regs);
        UpdateDepthBoundsTestEnable(regs);
        UpdateDepthTestEnable(regs);
        UpdateDepthWriteEnable(regs);
        UpdateDepthCompareOp(regs);
        UpdateFrontFace(regs);
        UpdatePrimitiveTopology(regs);
        UpdateStencilOp(regs);
        UpdateStencilTestEnable(regs);
    }
}

void RasterizerVulkan::BeginTransformFeedback() {
    const auto& regs = maxwell3d.regs;
    if (regs.tfb_enabled == 0) {
        return;
    }
    if (!device.IsExtTransformFeedbackSupported()) {
        LOG_ERROR(Render_Vulkan, "Transform feedbacks used but not supported");
        return;
    }

    UNIMPLEMENTED_IF(regs.IsShaderConfigEnabled(Maxwell::ShaderProgram::TesselationControl) ||
                     regs.IsShaderConfigEnabled(Maxwell::ShaderProgram::TesselationEval) ||
                     regs.IsShaderConfigEnabled(Maxwell::ShaderProgram::Geometry));

    UNIMPLEMENTED_IF(regs.tfb_bindings[1].buffer_enable);
    UNIMPLEMENTED_IF(regs.tfb_bindings[2].buffer_enable);
    UNIMPLEMENTED_IF(regs.tfb_bindings[3].buffer_enable);

    const auto& binding = regs.tfb_bindings[0];
    UNIMPLEMENTED_IF(binding.buffer_enable == 0);
    UNIMPLEMENTED_IF(binding.buffer_offset != 0);

    const GPUVAddr gpu_addr = binding.Address();
    const VkDeviceSize size = static_cast<VkDeviceSize>(binding.buffer_size);
    const auto info = buffer_cache.UploadMemory(gpu_addr, size, 4, true);

    scheduler.Record([buffer = info.handle, offset = info.offset, size](vk::CommandBuffer cmdbuf) {
        cmdbuf.BindTransformFeedbackBuffersEXT(0, 1, &buffer, &offset, &size);
        cmdbuf.BeginTransformFeedbackEXT(0, 0, nullptr, nullptr);
    });
}

void RasterizerVulkan::EndTransformFeedback() {
    const auto& regs = maxwell3d.regs;
    if (regs.tfb_enabled == 0) {
        return;
    }
    if (!device.IsExtTransformFeedbackSupported()) {
        return;
    }

    scheduler.Record(
        [](vk::CommandBuffer cmdbuf) { cmdbuf.EndTransformFeedbackEXT(0, 0, nullptr, nullptr); });
}

void RasterizerVulkan::SetupVertexArrays(BufferBindings& buffer_bindings) {
    const auto& regs = maxwell3d.regs;

    for (std::size_t index = 0; index < Maxwell::NumVertexArrays; ++index) {
        const auto& vertex_array = regs.vertex_array[index];
        if (!vertex_array.IsEnabled()) {
            continue;
        }
        const GPUVAddr start{vertex_array.StartAddress()};
        const GPUVAddr end{regs.vertex_array_limit[index].LimitAddress()};

        ASSERT(end >= start);
        const std::size_t size = end - start;
        if (size == 0) {
            buffer_bindings.AddVertexBinding(DefaultBuffer(), 0, DEFAULT_BUFFER_SIZE, 0);
            continue;
        }
        const auto info = buffer_cache.UploadMemory(start, size);
        buffer_bindings.AddVertexBinding(info.handle, info.offset, size, vertex_array.stride);
    }
}

void RasterizerVulkan::SetupIndexBuffer(BufferBindings& buffer_bindings, DrawParameters& params,
                                        bool is_indexed) {
    if (params.num_vertices == 0) {
        return;
    }
    const auto& regs = maxwell3d.regs;
    switch (regs.draw.topology) {
    case Maxwell::PrimitiveTopology::Quads: {
        if (!params.is_indexed) {
            const auto [buffer, offset] =
                quad_array_pass.Assemble(params.num_vertices, params.base_vertex);
            buffer_bindings.SetIndexBinding(buffer, offset, VK_INDEX_TYPE_UINT32);
            params.base_vertex = 0;
            params.num_vertices = params.num_vertices * 6 / 4;
            params.is_indexed = true;
            break;
        }
        const GPUVAddr gpu_addr = regs.index_array.IndexStart();
        const auto info = buffer_cache.UploadMemory(gpu_addr, CalculateIndexBufferSize());
        VkBuffer buffer = info.handle;
        u64 offset = info.offset;
        std::tie(buffer, offset) = quad_indexed_pass.Assemble(
            regs.index_array.format, params.num_vertices, params.base_vertex, buffer, offset);

        buffer_bindings.SetIndexBinding(buffer, offset, VK_INDEX_TYPE_UINT32);
        params.num_vertices = (params.num_vertices / 4) * 6;
        params.base_vertex = 0;
        break;
    }
    default: {
        if (!is_indexed) {
            break;
        }
        const GPUVAddr gpu_addr = regs.index_array.IndexStart();
        const auto info = buffer_cache.UploadMemory(gpu_addr, CalculateIndexBufferSize());
        VkBuffer buffer = info.handle;
        u64 offset = info.offset;

        auto format = regs.index_array.format;
        const bool is_uint8 = format == Maxwell::IndexFormat::UnsignedByte;
        if (is_uint8 && !device.IsExtIndexTypeUint8Supported()) {
            std::tie(buffer, offset) = uint8_pass.Assemble(params.num_vertices, buffer, offset);
            format = Maxwell::IndexFormat::UnsignedShort;
        }

        buffer_bindings.SetIndexBinding(buffer, offset, MaxwellToVK::IndexFormat(device, format));
        break;
    }
    }
}

void RasterizerVulkan::SetupGraphicsConstBuffers(const ShaderEntries& entries, std::size_t stage) {
    MICROPROFILE_SCOPE(Vulkan_ConstBuffers);
    const auto& shader_stage = maxwell3d.state.shader_stages[stage];
    for (const auto& entry : entries.const_buffers) {
        SetupConstBuffer(entry, shader_stage.const_buffers[entry.GetIndex()]);
    }
}

void RasterizerVulkan::SetupGraphicsGlobalBuffers(const ShaderEntries& entries, std::size_t stage) {
    MICROPROFILE_SCOPE(Vulkan_GlobalBuffers);
    const auto& cbufs{maxwell3d.state.shader_stages[stage]};

    for (const auto& entry : entries.global_buffers) {
        const auto addr = cbufs.const_buffers[entry.GetCbufIndex()].address + entry.GetCbufOffset();
        SetupGlobalBuffer(entry, addr);
    }
}

void RasterizerVulkan::SetupGraphicsUniformTexels(const ShaderEntries& entries, std::size_t stage) {
    MICROPROFILE_SCOPE(Vulkan_Textures);
    for (const auto& entry : entries.uniform_texels) {
        const auto image = GetTextureInfo(maxwell3d, entry, stage).tic;
        SetupUniformTexels(image, entry);
    }
}

void RasterizerVulkan::SetupGraphicsTextures(const ShaderEntries& entries, std::size_t stage) {
    MICROPROFILE_SCOPE(Vulkan_Textures);
    for (const auto& entry : entries.samplers) {
        for (std::size_t i = 0; i < entry.size; ++i) {
            const auto texture = GetTextureInfo(maxwell3d, entry, stage, i);
            SetupTexture(texture, entry);
        }
    }
}

void RasterizerVulkan::SetupGraphicsStorageTexels(const ShaderEntries& entries, std::size_t stage) {
    MICROPROFILE_SCOPE(Vulkan_Textures);
    for (const auto& entry : entries.storage_texels) {
        const auto image = GetTextureInfo(maxwell3d, entry, stage).tic;
        SetupStorageTexel(image, entry);
    }
}

void RasterizerVulkan::SetupGraphicsImages(const ShaderEntries& entries, std::size_t stage) {
    MICROPROFILE_SCOPE(Vulkan_Images);
    for (const auto& entry : entries.images) {
        const auto tic = GetTextureInfo(maxwell3d, entry, stage).tic;
        SetupImage(tic, entry);
    }
}

void RasterizerVulkan::SetupComputeConstBuffers(const ShaderEntries& entries) {
    MICROPROFILE_SCOPE(Vulkan_ConstBuffers);
    const auto& launch_desc = kepler_compute.launch_description;
    for (const auto& entry : entries.const_buffers) {
        const auto& config = launch_desc.const_buffer_config[entry.GetIndex()];
        const std::bitset<8> mask = launch_desc.const_buffer_enable_mask.Value();
        Tegra::Engines::ConstBufferInfo buffer;
        buffer.address = config.Address();
        buffer.size = config.size;
        buffer.enabled = mask[entry.GetIndex()];
        SetupConstBuffer(entry, buffer);
    }
}

void RasterizerVulkan::SetupComputeGlobalBuffers(const ShaderEntries& entries) {
    MICROPROFILE_SCOPE(Vulkan_GlobalBuffers);
    const auto& cbufs{kepler_compute.launch_description.const_buffer_config};
    for (const auto& entry : entries.global_buffers) {
        const auto addr{cbufs[entry.GetCbufIndex()].Address() + entry.GetCbufOffset()};
        SetupGlobalBuffer(entry, addr);
    }
}

void RasterizerVulkan::SetupComputeUniformTexels(const ShaderEntries& entries) {
    MICROPROFILE_SCOPE(Vulkan_Textures);
    for (const auto& entry : entries.uniform_texels) {
        const auto image = GetTextureInfo(kepler_compute, entry, ComputeShaderIndex).tic;
        SetupUniformTexels(image, entry);
    }
}

void RasterizerVulkan::SetupComputeTextures(const ShaderEntries& entries) {
    MICROPROFILE_SCOPE(Vulkan_Textures);
    for (const auto& entry : entries.samplers) {
        for (std::size_t i = 0; i < entry.size; ++i) {
            const auto texture = GetTextureInfo(kepler_compute, entry, ComputeShaderIndex, i);
            SetupTexture(texture, entry);
        }
    }
}

void RasterizerVulkan::SetupComputeStorageTexels(const ShaderEntries& entries) {
    MICROPROFILE_SCOPE(Vulkan_Textures);
    for (const auto& entry : entries.storage_texels) {
        const auto image = GetTextureInfo(kepler_compute, entry, ComputeShaderIndex).tic;
        SetupStorageTexel(image, entry);
    }
}

void RasterizerVulkan::SetupComputeImages(const ShaderEntries& entries) {
    MICROPROFILE_SCOPE(Vulkan_Images);
    for (const auto& entry : entries.images) {
        const auto tic = GetTextureInfo(kepler_compute, entry, ComputeShaderIndex).tic;
        SetupImage(tic, entry);
    }
}

void RasterizerVulkan::SetupConstBuffer(const ConstBufferEntry& entry,
                                        const Tegra::Engines::ConstBufferInfo& buffer) {
    if (!buffer.enabled) {
        // Set values to zero to unbind buffers
        update_descriptor_queue.AddBuffer(DefaultBuffer(), 0, DEFAULT_BUFFER_SIZE);
        return;
    }

    // Align the size to avoid bad std140 interactions
    const std::size_t size =
        Common::AlignUp(CalculateConstBufferSize(entry, buffer), 4 * sizeof(float));
    ASSERT(size <= MaxConstbufferSize);

    const auto info =
        buffer_cache.UploadMemory(buffer.address, size, device.GetUniformBufferAlignment());
    update_descriptor_queue.AddBuffer(info.handle, info.offset, size);
}

void RasterizerVulkan::SetupGlobalBuffer(const GlobalBufferEntry& entry, GPUVAddr address) {
    const u64 actual_addr = gpu_memory.Read<u64>(address);
    const u32 size = gpu_memory.Read<u32>(address + 8);

    if (size == 0) {
        // Sometimes global memory pointers don't have a proper size. Upload a dummy entry
        // because Vulkan doesn't like empty buffers.
        // Note: Do *not* use DefaultBuffer() here, storage buffers can be written breaking the
        // default buffer.
        static constexpr std::size_t dummy_size = 4;
        const auto info = buffer_cache.GetEmptyBuffer(dummy_size);
        update_descriptor_queue.AddBuffer(info.handle, info.offset, dummy_size);
        return;
    }

    const auto info = buffer_cache.UploadMemory(
        actual_addr, size, device.GetStorageBufferAlignment(), entry.IsWritten());
    update_descriptor_queue.AddBuffer(info.handle, info.offset, size);
}

void RasterizerVulkan::SetupUniformTexels(const Tegra::Texture::TICEntry& tic,
                                          const UniformTexelEntry& entry) {
    const auto view = texture_cache.GetTextureSurface(tic, entry);
    ASSERT(view->IsBufferView());

    update_descriptor_queue.AddTexelBuffer(view->GetBufferView());
}

void RasterizerVulkan::SetupTexture(const Tegra::Texture::FullTextureInfo& texture,
                                    const SamplerEntry& entry) {
    auto view = texture_cache.GetTextureSurface(texture.tic, entry);
    ASSERT(!view->IsBufferView());

    const VkImageView image_view = view->GetImageView(texture.tic.x_source, texture.tic.y_source,
                                                      texture.tic.z_source, texture.tic.w_source);
    const auto sampler = sampler_cache.GetSampler(texture.tsc);
    update_descriptor_queue.AddSampledImage(sampler, image_view);

    VkImageLayout* const image_layout = update_descriptor_queue.LastImageLayout();
    *image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sampled_views.push_back(ImageView{std::move(view), image_layout});
}

void RasterizerVulkan::SetupStorageTexel(const Tegra::Texture::TICEntry& tic,
                                         const StorageTexelEntry& entry) {
    const auto view = texture_cache.GetImageSurface(tic, entry);
    ASSERT(view->IsBufferView());

    update_descriptor_queue.AddTexelBuffer(view->GetBufferView());
}

void RasterizerVulkan::SetupImage(const Tegra::Texture::TICEntry& tic, const ImageEntry& entry) {
    auto view = texture_cache.GetImageSurface(tic, entry);

    if (entry.is_written) {
        view->MarkAsModified(texture_cache.Tick());
    }

    UNIMPLEMENTED_IF(tic.IsBuffer());

    const VkImageView image_view =
        view->GetImageView(tic.x_source, tic.y_source, tic.z_source, tic.w_source);
    update_descriptor_queue.AddImage(image_view);

    VkImageLayout* const image_layout = update_descriptor_queue.LastImageLayout();
    *image_layout = VK_IMAGE_LAYOUT_GENERAL;
    image_views.push_back(ImageView{std::move(view), image_layout});
}

void RasterizerVulkan::UpdateViewportsState(Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!state_tracker.TouchViewports()) {
        return;
    }
    const std::array viewports{
        GetViewportState(device, regs, 0),  GetViewportState(device, regs, 1),
        GetViewportState(device, regs, 2),  GetViewportState(device, regs, 3),
        GetViewportState(device, regs, 4),  GetViewportState(device, regs, 5),
        GetViewportState(device, regs, 6),  GetViewportState(device, regs, 7),
        GetViewportState(device, regs, 8),  GetViewportState(device, regs, 9),
        GetViewportState(device, regs, 10), GetViewportState(device, regs, 11),
        GetViewportState(device, regs, 12), GetViewportState(device, regs, 13),
        GetViewportState(device, regs, 14), GetViewportState(device, regs, 15)};
    scheduler.Record([viewports](vk::CommandBuffer cmdbuf) { cmdbuf.SetViewport(0, viewports); });
}

void RasterizerVulkan::UpdateScissorsState(Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!state_tracker.TouchScissors()) {
        return;
    }
    const std::array scissors = {
        GetScissorState(regs, 0),  GetScissorState(regs, 1),  GetScissorState(regs, 2),
        GetScissorState(regs, 3),  GetScissorState(regs, 4),  GetScissorState(regs, 5),
        GetScissorState(regs, 6),  GetScissorState(regs, 7),  GetScissorState(regs, 8),
        GetScissorState(regs, 9),  GetScissorState(regs, 10), GetScissorState(regs, 11),
        GetScissorState(regs, 12), GetScissorState(regs, 13), GetScissorState(regs, 14),
        GetScissorState(regs, 15)};
    scheduler.Record([scissors](vk::CommandBuffer cmdbuf) { cmdbuf.SetScissor(0, scissors); });
}

void RasterizerVulkan::UpdateDepthBias(Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!state_tracker.TouchDepthBias()) {
        return;
    }
    scheduler.Record([constant = regs.polygon_offset_units, clamp = regs.polygon_offset_clamp,
                      factor = regs.polygon_offset_factor](vk::CommandBuffer cmdbuf) {
        cmdbuf.SetDepthBias(constant, clamp, factor / 2.0f);
    });
}

void RasterizerVulkan::UpdateBlendConstants(Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!state_tracker.TouchBlendConstants()) {
        return;
    }
    const std::array blend_color = {regs.blend_color.r, regs.blend_color.g, regs.blend_color.b,
                                    regs.blend_color.a};
    scheduler.Record(
        [blend_color](vk::CommandBuffer cmdbuf) { cmdbuf.SetBlendConstants(blend_color.data()); });
}

void RasterizerVulkan::UpdateDepthBounds(Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!state_tracker.TouchDepthBounds()) {
        return;
    }
    scheduler.Record([min = regs.depth_bounds[0], max = regs.depth_bounds[1]](
                         vk::CommandBuffer cmdbuf) { cmdbuf.SetDepthBounds(min, max); });
}

void RasterizerVulkan::UpdateStencilFaces(Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!state_tracker.TouchStencilProperties()) {
        return;
    }
    if (regs.stencil_two_side_enable) {
        // Separate values per face
        scheduler.Record(
            [front_ref = regs.stencil_front_func_ref, front_write_mask = regs.stencil_front_mask,
             front_test_mask = regs.stencil_front_func_mask, back_ref = regs.stencil_back_func_ref,
             back_write_mask = regs.stencil_back_mask,
             back_test_mask = regs.stencil_back_func_mask](vk::CommandBuffer cmdbuf) {
                // Front face
                cmdbuf.SetStencilReference(VK_STENCIL_FACE_FRONT_BIT, front_ref);
                cmdbuf.SetStencilWriteMask(VK_STENCIL_FACE_FRONT_BIT, front_write_mask);
                cmdbuf.SetStencilCompareMask(VK_STENCIL_FACE_FRONT_BIT, front_test_mask);

                // Back face
                cmdbuf.SetStencilReference(VK_STENCIL_FACE_BACK_BIT, back_ref);
                cmdbuf.SetStencilWriteMask(VK_STENCIL_FACE_BACK_BIT, back_write_mask);
                cmdbuf.SetStencilCompareMask(VK_STENCIL_FACE_BACK_BIT, back_test_mask);
            });
    } else {
        // Front face defines both faces
        scheduler.Record([ref = regs.stencil_back_func_ref, write_mask = regs.stencil_back_mask,
                          test_mask = regs.stencil_back_func_mask](vk::CommandBuffer cmdbuf) {
            cmdbuf.SetStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK, ref);
            cmdbuf.SetStencilWriteMask(VK_STENCIL_FACE_FRONT_AND_BACK, write_mask);
            cmdbuf.SetStencilCompareMask(VK_STENCIL_FACE_FRONT_AND_BACK, test_mask);
        });
    }
}

void RasterizerVulkan::UpdateCullMode(Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!state_tracker.TouchCullMode()) {
        return;
    }
    scheduler.Record(
        [enabled = regs.cull_test_enabled, cull_face = regs.cull_face](vk::CommandBuffer cmdbuf) {
            cmdbuf.SetCullModeEXT(enabled ? MaxwellToVK::CullFace(cull_face) : VK_CULL_MODE_NONE);
        });
}

void RasterizerVulkan::UpdateDepthBoundsTestEnable(Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!state_tracker.TouchDepthBoundsTestEnable()) {
        return;
    }
    scheduler.Record([enable = regs.depth_bounds_enable](vk::CommandBuffer cmdbuf) {
        cmdbuf.SetDepthBoundsTestEnableEXT(enable);
    });
}

void RasterizerVulkan::UpdateDepthTestEnable(Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!state_tracker.TouchDepthTestEnable()) {
        return;
    }
    scheduler.Record([enable = regs.depth_test_enable](vk::CommandBuffer cmdbuf) {
        cmdbuf.SetDepthTestEnableEXT(enable);
    });
}

void RasterizerVulkan::UpdateDepthWriteEnable(Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!state_tracker.TouchDepthWriteEnable()) {
        return;
    }
    scheduler.Record([enable = regs.depth_write_enabled](vk::CommandBuffer cmdbuf) {
        cmdbuf.SetDepthWriteEnableEXT(enable);
    });
}

void RasterizerVulkan::UpdateDepthCompareOp(Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!state_tracker.TouchDepthCompareOp()) {
        return;
    }
    scheduler.Record([func = regs.depth_test_func](vk::CommandBuffer cmdbuf) {
        cmdbuf.SetDepthCompareOpEXT(MaxwellToVK::ComparisonOp(func));
    });
}

void RasterizerVulkan::UpdateFrontFace(Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!state_tracker.TouchFrontFace()) {
        return;
    }

    VkFrontFace front_face = MaxwellToVK::FrontFace(regs.front_face);
    if (regs.screen_y_control.triangle_rast_flip != 0) {
        front_face = front_face == VK_FRONT_FACE_CLOCKWISE ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                                           : VK_FRONT_FACE_CLOCKWISE;
    }
    scheduler.Record(
        [front_face](vk::CommandBuffer cmdbuf) { cmdbuf.SetFrontFaceEXT(front_face); });
}

void RasterizerVulkan::UpdatePrimitiveTopology(Tegra::Engines::Maxwell3D::Regs& regs) {
    const Maxwell::PrimitiveTopology primitive_topology = regs.draw.topology.Value();
    if (!state_tracker.ChangePrimitiveTopology(primitive_topology)) {
        return;
    }
    scheduler.Record([this, primitive_topology](vk::CommandBuffer cmdbuf) {
        cmdbuf.SetPrimitiveTopologyEXT(MaxwellToVK::PrimitiveTopology(device, primitive_topology));
    });
}

void RasterizerVulkan::UpdateStencilOp(Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!state_tracker.TouchStencilOp()) {
        return;
    }
    const Maxwell::StencilOp fail = regs.stencil_front_op_fail;
    const Maxwell::StencilOp zfail = regs.stencil_front_op_zfail;
    const Maxwell::StencilOp zpass = regs.stencil_front_op_zpass;
    const Maxwell::ComparisonOp compare = regs.stencil_front_func_func;
    if (regs.stencil_two_side_enable) {
        scheduler.Record([fail, zfail, zpass, compare](vk::CommandBuffer cmdbuf) {
            cmdbuf.SetStencilOpEXT(VK_STENCIL_FACE_FRONT_AND_BACK, MaxwellToVK::StencilOp(fail),
                                   MaxwellToVK::StencilOp(zpass), MaxwellToVK::StencilOp(zfail),
                                   MaxwellToVK::ComparisonOp(compare));
        });
    } else {
        const Maxwell::StencilOp back_fail = regs.stencil_back_op_fail;
        const Maxwell::StencilOp back_zfail = regs.stencil_back_op_zfail;
        const Maxwell::StencilOp back_zpass = regs.stencil_back_op_zpass;
        const Maxwell::ComparisonOp back_compare = regs.stencil_back_func_func;
        scheduler.Record([fail, zfail, zpass, compare, back_fail, back_zfail, back_zpass,
                          back_compare](vk::CommandBuffer cmdbuf) {
            cmdbuf.SetStencilOpEXT(VK_STENCIL_FACE_FRONT_BIT, MaxwellToVK::StencilOp(fail),
                                   MaxwellToVK::StencilOp(zpass), MaxwellToVK::StencilOp(zfail),
                                   MaxwellToVK::ComparisonOp(compare));
            cmdbuf.SetStencilOpEXT(VK_STENCIL_FACE_BACK_BIT, MaxwellToVK::StencilOp(back_fail),
                                   MaxwellToVK::StencilOp(back_zpass),
                                   MaxwellToVK::StencilOp(back_zfail),
                                   MaxwellToVK::ComparisonOp(back_compare));
        });
    }
}

void RasterizerVulkan::UpdateStencilTestEnable(Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!state_tracker.TouchStencilTestEnable()) {
        return;
    }
    scheduler.Record([enable = regs.stencil_enable](vk::CommandBuffer cmdbuf) {
        cmdbuf.SetStencilTestEnableEXT(enable);
    });
}

std::size_t RasterizerVulkan::CalculateGraphicsStreamBufferSize(bool is_indexed) const {
    std::size_t size = CalculateVertexArraysSize();
    if (is_indexed) {
        size = Common::AlignUp(size, 4) + CalculateIndexBufferSize();
    }
    size += Maxwell::MaxConstBuffers * (MaxConstbufferSize + device.GetUniformBufferAlignment());
    return size;
}

std::size_t RasterizerVulkan::CalculateComputeStreamBufferSize() const {
    return Tegra::Engines::KeplerCompute::NumConstBuffers *
           (Maxwell::MaxConstBufferSize + device.GetUniformBufferAlignment());
}

std::size_t RasterizerVulkan::CalculateVertexArraysSize() const {
    const auto& regs = maxwell3d.regs;

    std::size_t size = 0;
    for (u32 index = 0; index < Maxwell::NumVertexArrays; ++index) {
        // This implementation assumes that all attributes are used in the shader.
        const GPUVAddr start{regs.vertex_array[index].StartAddress()};
        const GPUVAddr end{regs.vertex_array_limit[index].LimitAddress()};
        DEBUG_ASSERT(end >= start);

        size += (end - start) * regs.vertex_array[index].enable;
    }
    return size;
}

std::size_t RasterizerVulkan::CalculateIndexBufferSize() const {
    return static_cast<std::size_t>(maxwell3d.regs.index_array.count) *
           static_cast<std::size_t>(maxwell3d.regs.index_array.FormatSizeInBytes());
}

std::size_t RasterizerVulkan::CalculateConstBufferSize(
    const ConstBufferEntry& entry, const Tegra::Engines::ConstBufferInfo& buffer) const {
    if (entry.IsIndirect()) {
        // Buffer is accessed indirectly, so upload the entire thing
        return buffer.size;
    } else {
        // Buffer is accessed directly, upload just what we use
        return entry.GetSize();
    }
}

RenderPassParams RasterizerVulkan::GetRenderPassParams(Texceptions texceptions) const {
    const auto& regs = maxwell3d.regs;
    const std::size_t num_attachments = static_cast<std::size_t>(regs.rt_control.count);

    RenderPassParams params;
    params.color_formats = {};
    std::size_t color_texceptions = 0;

    std::size_t index = 0;
    for (std::size_t rt = 0; rt < num_attachments; ++rt) {
        const auto& rendertarget = regs.rt[rt];
        if (rendertarget.Address() == 0 || rendertarget.format == Tegra::RenderTargetFormat::NONE) {
            continue;
        }
        params.color_formats[index] = static_cast<u8>(rendertarget.format);
        color_texceptions |= (texceptions[rt] ? 1ULL : 0ULL) << index;
        ++index;
    }
    params.num_color_attachments = static_cast<u8>(index);
    params.texceptions = static_cast<u8>(color_texceptions);

    params.zeta_format = regs.zeta_enable ? static_cast<u8>(regs.zeta.format) : 0;
    params.zeta_texception = texceptions[ZETA_TEXCEPTION_INDEX];
    return params;
}

VkBuffer RasterizerVulkan::DefaultBuffer() {
    if (default_buffer) {
        return *default_buffer;
    }

    default_buffer = device.GetLogical().CreateBuffer({
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = DEFAULT_BUFFER_SIZE,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    });
    default_buffer_commit = memory_manager.Commit(default_buffer, false);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([buffer = *default_buffer](vk::CommandBuffer cmdbuf) {
        cmdbuf.FillBuffer(buffer, 0, DEFAULT_BUFFER_SIZE, 0);
    });
    return *default_buffer;
}

} // namespace Vulkan
