// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <limits>

#include <glad/glad.h>

#include "common/common_types.h"
#include "core/core.h"
#include "video_core/dirty_flags.h"
#include "video_core/engines/maxwell_3d.h"

namespace Tegra {
class GPU;
}

namespace OpenGL {

namespace Dirty {

enum : u8 {
    First = VideoCommon::Dirty::LastCommonEntry,

    VertexFormats,
    VertexFormat0,
    VertexFormat31 = VertexFormat0 + 31,

    VertexBuffers,
    VertexBuffer0,
    VertexBuffer31 = VertexBuffer0 + 31,

    VertexInstances,
    VertexInstance0,
    VertexInstance31 = VertexInstance0 + 31,

    ViewportTransform,
    Viewports,
    Viewport0,
    Viewport15 = Viewport0 + 15,

    Scissors,
    Scissor0,
    Scissor15 = Scissor0 + 15,

    ColorMaskCommon,
    ColorMasks,
    ColorMask0,
    ColorMask7 = ColorMask0 + 7,

    BlendColor,
    BlendIndependentEnabled,
    BlendStates,
    BlendState0,
    BlendState7 = BlendState0 + 7,

    Shaders,
    ClipDistances,

    PolygonModes,
    PolygonModeFront,
    PolygonModeBack,

    ColorMask,
    FrontFace,
    CullTest,
    DepthMask,
    DepthTest,
    StencilTest,
    AlphaTest,
    PrimitiveRestart,
    PolygonOffset,
    MultisampleControl,
    RasterizeEnable,
    FramebufferSRGB,
    LogicOp,
    FragmentClampColor,
    PointSize,
    LineWidth,
    ClipControl,
    DepthClampEnabled,

    Last
};
static_assert(Last <= std::numeric_limits<u8>::max());

} // namespace Dirty

class StateTracker {
public:
    explicit StateTracker(Tegra::GPU& gpu);

    void BindIndexBuffer(GLuint new_index_buffer) {
        if (index_buffer == new_index_buffer) {
            return;
        }
        index_buffer = new_index_buffer;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, new_index_buffer);
    }

    void NotifyScreenDrawVertexArray() {
        flags[OpenGL::Dirty::VertexFormats] = true;
        flags[OpenGL::Dirty::VertexFormat0 + 0] = true;
        flags[OpenGL::Dirty::VertexFormat0 + 1] = true;

        flags[OpenGL::Dirty::VertexBuffers] = true;
        flags[OpenGL::Dirty::VertexBuffer0] = true;

        flags[OpenGL::Dirty::VertexInstances] = true;
        flags[OpenGL::Dirty::VertexInstance0 + 0] = true;
        flags[OpenGL::Dirty::VertexInstance0 + 1] = true;
    }

    void NotifyPolygonModes() {
        flags[OpenGL::Dirty::PolygonModes] = true;
        flags[OpenGL::Dirty::PolygonModeFront] = true;
        flags[OpenGL::Dirty::PolygonModeBack] = true;
    }

    void NotifyViewport0() {
        flags[OpenGL::Dirty::Viewports] = true;
        flags[OpenGL::Dirty::Viewport0] = true;
    }

    void NotifyScissor0() {
        flags[OpenGL::Dirty::Scissors] = true;
        flags[OpenGL::Dirty::Scissor0] = true;
    }

    void NotifyColorMask0() {
        flags[OpenGL::Dirty::ColorMasks] = true;
        flags[OpenGL::Dirty::ColorMask0] = true;
    }

    void NotifyBlend0() {
        flags[OpenGL::Dirty::BlendStates] = true;
        flags[OpenGL::Dirty::BlendState0] = true;
    }

    void NotifyFramebuffer() {
        flags[VideoCommon::Dirty::RenderTargets] = true;
    }

    void NotifyFrontFace() {
        flags[OpenGL::Dirty::FrontFace] = true;
    }

    void NotifyCullTest() {
        flags[OpenGL::Dirty::CullTest] = true;
    }

    void NotifyDepthMask() {
        flags[OpenGL::Dirty::DepthMask] = true;
    }

    void NotifyDepthTest() {
        flags[OpenGL::Dirty::DepthTest] = true;
    }

    void NotifyStencilTest() {
        flags[OpenGL::Dirty::StencilTest] = true;
    }

    void NotifyPolygonOffset() {
        flags[OpenGL::Dirty::PolygonOffset] = true;
    }

    void NotifyRasterizeEnable() {
        flags[OpenGL::Dirty::RasterizeEnable] = true;
    }

    void NotifyFramebufferSRGB() {
        flags[OpenGL::Dirty::FramebufferSRGB] = true;
    }

    void NotifyLogicOp() {
        flags[OpenGL::Dirty::LogicOp] = true;
    }

    void NotifyClipControl() {
        flags[OpenGL::Dirty::ClipControl] = true;
    }

    void NotifyAlphaTest() {
        flags[OpenGL::Dirty::AlphaTest] = true;
    }

private:
    Tegra::Engines::Maxwell3D::DirtyState::Flags& flags;

    GLuint index_buffer = 0;
};

} // namespace OpenGL
