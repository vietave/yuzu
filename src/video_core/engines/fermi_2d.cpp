// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/logging/log.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"

namespace Tegra::Engines {

Fermi2D::Fermi2D() = default;

Fermi2D::~Fermi2D() = default;

void Fermi2D::BindRasterizer(VideoCore::RasterizerInterface& rasterizer_) {
    rasterizer = &rasterizer_;
}

void Fermi2D::CallMethod(u32 method, u32 method_argument, bool is_last_call) {
    ASSERT_MSG(method < Regs::NUM_REGS,
               "Invalid Fermi2D register, increase the size of the Regs structure");

    regs.reg_array[method] = method_argument;

    switch (method) {
    // Trigger the surface copy on the last register write. This is blit_src_y, but this is 64-bit,
    // so trigger on the second 32-bit write.
    case FERMI2D_REG_INDEX(blit_src_y) + 1: {
        HandleSurfaceCopy();
        break;
    }
    }
}

void Fermi2D::CallMultiMethod(u32 method, const u32* base_start, u32 amount, u32 methods_pending) {
    for (std::size_t i = 0; i < amount; i++) {
        CallMethod(method, base_start[i], methods_pending - static_cast<u32>(i) <= 1);
    }
}

static std::pair<u32, u32> DelimitLine(u32 src_1, u32 src_2, u32 dst_1, u32 dst_2, u32 src_line) {
    const u32 line_a = src_2 - src_1;
    const u32 line_b = dst_2 - dst_1;
    const u32 excess = std::max<s32>(0, line_a - src_line + src_1);
    return {line_b - (excess * line_b) / line_a, excess};
}

void Fermi2D::HandleSurfaceCopy() {
    LOG_DEBUG(HW_GPU, "Requested a surface copy with operation {}",
              static_cast<u32>(regs.operation));

    // TODO(Subv): Only raw copies are implemented.
    ASSERT(regs.operation == Operation::SrcCopy);

    const u32 src_blit_x1{static_cast<u32>(regs.blit_src_x >> 32)};
    const u32 src_blit_y1{static_cast<u32>(regs.blit_src_y >> 32)};
    u32 src_blit_x2, src_blit_y2;
    if (regs.blit_control.origin == Origin::Corner) {
        src_blit_x2 =
            static_cast<u32>((regs.blit_src_x + (regs.blit_du_dx * regs.blit_dst_width)) >> 32);
        src_blit_y2 =
            static_cast<u32>((regs.blit_src_y + (regs.blit_dv_dy * regs.blit_dst_height)) >> 32);
    } else {
        src_blit_x2 = static_cast<u32>((regs.blit_src_x >> 32) + regs.blit_dst_width);
        src_blit_y2 = static_cast<u32>((regs.blit_src_y >> 32) + regs.blit_dst_height);
    }
    u32 dst_blit_x2 = regs.blit_dst_x + regs.blit_dst_width;
    u32 dst_blit_y2 = regs.blit_dst_y + regs.blit_dst_height;
    const auto [new_dst_w, src_excess_x] =
        DelimitLine(src_blit_x1, src_blit_x2, regs.blit_dst_x, dst_blit_x2, regs.src.width);
    const auto [new_dst_h, src_excess_y] =
        DelimitLine(src_blit_y1, src_blit_y2, regs.blit_dst_y, dst_blit_y2, regs.src.height);
    dst_blit_x2 = new_dst_w + regs.blit_dst_x;
    src_blit_x2 = src_blit_x2 - src_excess_x;
    dst_blit_y2 = new_dst_h + regs.blit_dst_y;
    src_blit_y2 = src_blit_y2 - src_excess_y;
    const auto [new_src_w, dst_excess_x] =
        DelimitLine(regs.blit_dst_x, dst_blit_x2, src_blit_x1, src_blit_x2, regs.dst.width);
    const auto [new_src_h, dst_excess_y] =
        DelimitLine(regs.blit_dst_y, dst_blit_y2, src_blit_y1, src_blit_y2, regs.dst.height);
    src_blit_x2 = new_src_w + src_blit_x1;
    dst_blit_x2 = dst_blit_x2 - dst_excess_x;
    src_blit_y2 = new_src_h + src_blit_y1;
    dst_blit_y2 = dst_blit_y2 - dst_excess_y;
    const Common::Rectangle<u32> src_rect{src_blit_x1, src_blit_y1, src_blit_x2, src_blit_y2};
    const Common::Rectangle<u32> dst_rect{regs.blit_dst_x, regs.blit_dst_y, dst_blit_x2,
                                          dst_blit_y2};
    const Config copy_config{
        .operation = regs.operation,
        .filter = regs.blit_control.filter,
        .src_rect = src_rect,
        .dst_rect = dst_rect,
    };
    if (!rasterizer->AccelerateSurfaceCopy(regs.src, regs.dst, copy_config)) {
        UNIMPLEMENTED();
    }
}

} // namespace Tegra::Engines
