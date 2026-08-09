// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

struct SDL_Window;
union SDL_Event;

namespace vaxelis {

/// Dear ImGui integration for the SDL3 + OpenGL backends.
class ImGuiLayer {
  public:
    /// gl_context is an SDL_GLContext (void*).
    void init(SDL_Window* window, void* gl_context);
    void shutdown();

    /// Forwards an SDL event to ImGui.
    void process_event(const SDL_Event& ev);
    /// Starts a new ImGui frame.
    void begin_frame();
    /// Ends the frame and submits ImGui draw data on the current GL context.
    void end_frame();
};

} // namespace vaxelis
