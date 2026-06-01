#pragma once

struct SDL_Window;
union SDL_Event;

namespace vaxelis {

class ImGuiLayer {
  public:
    // gl_context is an SDL_GLContext (void*).
    void init(SDL_Window* window, void* gl_context);
    void shutdown();

    void process_event(const SDL_Event& ev);
    void begin_frame();
    void end_frame(); // submits ImGui draw data on current GL context
};

} // namespace vaxelis
