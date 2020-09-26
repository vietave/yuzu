// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <SDL.h>
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "core/core.h"
#include "core/perf_stats.h"
#include "input_common/keyboard.h"
#include "input_common/main.h"
#include "input_common/motion_emu.h"
#include "input_common/sdl/sdl.h"
#include "yuzu_cmd/emu_window/emu_window_sdl2.h"

EmuWindow_SDL2::EmuWindow_SDL2(InputCommon::InputSubsystem* input_subsystem_)
    : input_subsystem{input_subsystem_} {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        LOG_CRITICAL(Frontend, "Failed to initialize SDL2! Exiting...");
        exit(1);
    }
    input_subsystem->Initialize();
    SDL_SetMainReady();
}

EmuWindow_SDL2::~EmuWindow_SDL2() {
    input_subsystem->Shutdown();
    SDL_Quit();
}

void EmuWindow_SDL2::OnMouseMotion(s32 x, s32 y) {
    TouchMoved((unsigned)std::max(x, 0), (unsigned)std::max(y, 0));
    input_subsystem->GetMotionEmu()->Tilt(x, y);
}

void EmuWindow_SDL2::OnMouseButton(u32 button, u8 state, s32 x, s32 y) {
    if (button == SDL_BUTTON_LEFT) {
        if (state == SDL_PRESSED) {
            TouchPressed((unsigned)std::max(x, 0), (unsigned)std::max(y, 0));
        } else {
            TouchReleased();
        }
    } else if (button == SDL_BUTTON_RIGHT) {
        if (state == SDL_PRESSED) {
            input_subsystem->GetMotionEmu()->BeginTilt(x, y);
        } else {
            input_subsystem->GetMotionEmu()->EndTilt();
        }
    }
}

std::pair<unsigned, unsigned> EmuWindow_SDL2::TouchToPixelPos(float touch_x, float touch_y) const {
    int w, h;
    SDL_GetWindowSize(render_window, &w, &h);

    touch_x *= w;
    touch_y *= h;

    return {static_cast<unsigned>(std::max(std::round(touch_x), 0.0f)),
            static_cast<unsigned>(std::max(std::round(touch_y), 0.0f))};
}

void EmuWindow_SDL2::OnFingerDown(float x, float y) {
    // TODO(NeatNit): keep track of multitouch using the fingerID and a dictionary of some kind
    // This isn't critical because the best we can do when we have that is to average them, like the
    // 3DS does

    const auto [px, py] = TouchToPixelPos(x, y);
    TouchPressed(px, py);
}

void EmuWindow_SDL2::OnFingerMotion(float x, float y) {
    const auto [px, py] = TouchToPixelPos(x, y);
    TouchMoved(px, py);
}

void EmuWindow_SDL2::OnFingerUp() {
    TouchReleased();
}

void EmuWindow_SDL2::OnKeyEvent(int key, u8 state) {
    if (state == SDL_PRESSED) {
        input_subsystem->GetKeyboard()->PressKey(key);
    } else if (state == SDL_RELEASED) {
        input_subsystem->GetKeyboard()->ReleaseKey(key);
    }
}

bool EmuWindow_SDL2::IsOpen() const {
    return is_open;
}

bool EmuWindow_SDL2::IsShown() const {
    return is_shown;
}

void EmuWindow_SDL2::OnResize() {
    int width, height;
    SDL_GetWindowSize(render_window, &width, &height);
    UpdateCurrentFramebufferLayout(width, height);
}

void EmuWindow_SDL2::Fullscreen() {
    if (SDL_SetWindowFullscreen(render_window, SDL_WINDOW_FULLSCREEN) == 0) {
        return;
    }

    LOG_ERROR(Frontend, "Fullscreening failed: {}", SDL_GetError());

    // Try a different fullscreening method
    LOG_INFO(Frontend, "Attempting to use borderless fullscreen...");
    if (SDL_SetWindowFullscreen(render_window, SDL_WINDOW_FULLSCREEN_DESKTOP) == 0) {
        return;
    }

    LOG_ERROR(Frontend, "Borderless fullscreening failed: {}", SDL_GetError());

    // Fallback algorithm: Maximise window.
    // Works on all systems (unless something is seriously wrong), so no fallback for this one.
    LOG_INFO(Frontend, "Falling back on a maximised window...");
    SDL_MaximizeWindow(render_window);
}

void EmuWindow_SDL2::PollEvents() {
    SDL_Event event;

    // SDL_PollEvent returns 0 when there are no more events in the event queue
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_WINDOWEVENT:
            switch (event.window.event) {
            case SDL_WINDOWEVENT_SIZE_CHANGED:
            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_MAXIMIZED:
            case SDL_WINDOWEVENT_RESTORED:
                OnResize();
                break;
            case SDL_WINDOWEVENT_MINIMIZED:
            case SDL_WINDOWEVENT_EXPOSED:
                is_shown = event.window.event == SDL_WINDOWEVENT_EXPOSED;
                OnResize();
                break;
            case SDL_WINDOWEVENT_CLOSE:
                is_open = false;
                break;
            }
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            OnKeyEvent(static_cast<int>(event.key.keysym.scancode), event.key.state);
            break;
        case SDL_MOUSEMOTION:
            // ignore if it came from touch
            if (event.button.which != SDL_TOUCH_MOUSEID)
                OnMouseMotion(event.motion.x, event.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            // ignore if it came from touch
            if (event.button.which != SDL_TOUCH_MOUSEID) {
                OnMouseButton(event.button.button, event.button.state, event.button.x,
                              event.button.y);
            }
            break;
        case SDL_FINGERDOWN:
            OnFingerDown(event.tfinger.x, event.tfinger.y);
            break;
        case SDL_FINGERMOTION:
            OnFingerMotion(event.tfinger.x, event.tfinger.y);
            break;
        case SDL_FINGERUP:
            OnFingerUp();
            break;
        case SDL_QUIT:
            is_open = false;
            break;
        default:
            break;
        }
    }

    const u32 current_time = SDL_GetTicks();
    if (current_time > last_time + 2000) {
        const auto results = Core::System::GetInstance().GetAndResetPerfStats();
        const auto title =
            fmt::format("yuzu {} | {}-{} | FPS: {:.0f} ({:.0f}%)", Common::g_build_fullname,
                        Common::g_scm_branch, Common::g_scm_desc, results.game_fps,
                        results.emulation_speed * 100.0);
        SDL_SetWindowTitle(render_window, title.c_str());
        last_time = current_time;
    }
}

void EmuWindow_SDL2::OnMinimalClientAreaChangeRequest(std::pair<unsigned, unsigned> minimal_size) {
    SDL_SetWindowMinimumSize(render_window, minimal_size.first, minimal_size.second);
}
