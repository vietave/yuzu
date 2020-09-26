// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>

#include "common/common_types.h"

namespace Core::Frontend {

using BorderColor = std::array<u8, 4>;
using ExplainText = std::array<char, 0x81>;

struct ControllerParameters {
    s8 min_players{};
    s8 max_players{};
    bool keep_controllers_connected{};
    bool enable_single_mode{};
    bool enable_border_color{};
    std::vector<BorderColor> border_colors{};
    bool enable_explain_text{};
    std::vector<ExplainText> explain_text{};
    bool allow_pro_controller{};
    bool allow_handheld{};
    bool allow_dual_joycons{};
    bool allow_left_joycon{};
    bool allow_right_joycon{};
};

class ControllerApplet {
public:
    virtual ~ControllerApplet();

    virtual void ReconfigureControllers(std::function<void()> callback,
                                        ControllerParameters parameters) const = 0;
};

class DefaultControllerApplet final : public ControllerApplet {
public:
    ~DefaultControllerApplet() override;

    void ReconfigureControllers(std::function<void()> callback,
                                ControllerParameters parameters) const override;
};

} // namespace Core::Frontend
