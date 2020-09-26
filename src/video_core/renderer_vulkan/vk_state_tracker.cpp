// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstddef>
#include <iterator>

#include "common/common_types.h"
#include "core/core.h"
#include "video_core/dirty_flags.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/gpu.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"

#define OFF(field_name) MAXWELL3D_REG_INDEX(field_name)
#define NUM(field_name) (sizeof(Maxwell3D::Regs::field_name) / sizeof(u32))

namespace Vulkan {

namespace {

using namespace Dirty;
using namespace VideoCommon::Dirty;
using Tegra::Engines::Maxwell3D;
using Regs = Maxwell3D::Regs;
using Tables = Maxwell3D::DirtyState::Tables;
using Table = Maxwell3D::DirtyState::Table;
using Flags = Maxwell3D::DirtyState::Flags;

Flags MakeInvalidationFlags() {
    Flags flags{};
    flags[Viewports] = true;
    flags[Scissors] = true;
    flags[DepthBias] = true;
    flags[BlendConstants] = true;
    flags[DepthBounds] = true;
    flags[StencilProperties] = true;
    flags[CullMode] = true;
    flags[DepthBoundsEnable] = true;
    flags[DepthTestEnable] = true;
    flags[DepthWriteEnable] = true;
    flags[DepthCompareOp] = true;
    flags[FrontFace] = true;
    flags[StencilOp] = true;
    flags[StencilTestEnable] = true;
    return flags;
}

void SetupDirtyViewports(Tables& tables) {
    FillBlock(tables[0], OFF(viewport_transform), NUM(viewport_transform), Viewports);
    FillBlock(tables[0], OFF(viewports), NUM(viewports), Viewports);
    tables[0][OFF(viewport_transform_enabled)] = Viewports;
}

void SetupDirtyScissors(Tables& tables) {
    FillBlock(tables[0], OFF(scissor_test), NUM(scissor_test), Scissors);
}

void SetupDirtyDepthBias(Tables& tables) {
    auto& table = tables[0];
    table[OFF(polygon_offset_units)] = DepthBias;
    table[OFF(polygon_offset_clamp)] = DepthBias;
    table[OFF(polygon_offset_factor)] = DepthBias;
}

void SetupDirtyBlendConstants(Tables& tables) {
    FillBlock(tables[0], OFF(blend_color), NUM(blend_color), BlendConstants);
}

void SetupDirtyDepthBounds(Tables& tables) {
    FillBlock(tables[0], OFF(depth_bounds), NUM(depth_bounds), DepthBounds);
}

void SetupDirtyStencilProperties(Tables& tables) {
    auto& table = tables[0];
    table[OFF(stencil_two_side_enable)] = StencilProperties;
    table[OFF(stencil_front_func_ref)] = StencilProperties;
    table[OFF(stencil_front_mask)] = StencilProperties;
    table[OFF(stencil_front_func_mask)] = StencilProperties;
    table[OFF(stencil_back_func_ref)] = StencilProperties;
    table[OFF(stencil_back_mask)] = StencilProperties;
    table[OFF(stencil_back_func_mask)] = StencilProperties;
}

void SetupDirtyCullMode(Tables& tables) {
    auto& table = tables[0];
    table[OFF(cull_face)] = CullMode;
    table[OFF(cull_test_enabled)] = CullMode;
}

void SetupDirtyDepthBoundsEnable(Tables& tables) {
    tables[0][OFF(depth_bounds_enable)] = DepthBoundsEnable;
}

void SetupDirtyDepthTestEnable(Tables& tables) {
    tables[0][OFF(depth_test_enable)] = DepthTestEnable;
}

void SetupDirtyDepthWriteEnable(Tables& tables) {
    tables[0][OFF(depth_write_enabled)] = DepthWriteEnable;
}

void SetupDirtyDepthCompareOp(Tables& tables) {
    tables[0][OFF(depth_test_func)] = DepthCompareOp;
}

void SetupDirtyFrontFace(Tables& tables) {
    auto& table = tables[0];
    table[OFF(front_face)] = FrontFace;
    table[OFF(screen_y_control)] = FrontFace;
}

void SetupDirtyStencilOp(Tables& tables) {
    auto& table = tables[0];
    table[OFF(stencil_front_op_fail)] = StencilOp;
    table[OFF(stencil_front_op_zfail)] = StencilOp;
    table[OFF(stencil_front_op_zpass)] = StencilOp;
    table[OFF(stencil_front_func_func)] = StencilOp;
    table[OFF(stencil_back_op_fail)] = StencilOp;
    table[OFF(stencil_back_op_zfail)] = StencilOp;
    table[OFF(stencil_back_op_zpass)] = StencilOp;
    table[OFF(stencil_back_func_func)] = StencilOp;

    // Table 0 is used by StencilProperties
    tables[1][OFF(stencil_two_side_enable)] = StencilOp;
}

void SetupDirtyStencilTestEnable(Tables& tables) {
    tables[0][OFF(stencil_enable)] = StencilTestEnable;
}

} // Anonymous namespace

StateTracker::StateTracker(Tegra::GPU& gpu)
    : flags{gpu.Maxwell3D().dirty.flags}, invalidation_flags{MakeInvalidationFlags()} {
    auto& tables = gpu.Maxwell3D().dirty.tables;
    SetupDirtyRenderTargets(tables);
    SetupDirtyViewports(tables);
    SetupDirtyScissors(tables);
    SetupDirtyDepthBias(tables);
    SetupDirtyBlendConstants(tables);
    SetupDirtyDepthBounds(tables);
    SetupDirtyStencilProperties(tables);
    SetupDirtyCullMode(tables);
    SetupDirtyDepthBoundsEnable(tables);
    SetupDirtyDepthTestEnable(tables);
    SetupDirtyDepthWriteEnable(tables);
    SetupDirtyDepthCompareOp(tables);
    SetupDirtyFrontFace(tables);
    SetupDirtyStencilOp(tables);
    SetupDirtyStencilTestEnable(tables);
}

} // namespace Vulkan
