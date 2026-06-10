#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_scancode.h>

#include "engine/core/StringMap.hpp"

union SDL_Event;

namespace vaxelis {

// Polled input + named action bindings. Call begin_frame() once per frame
// before SDL_PollEvent, on_event() for each SDL event. Then query.
//
// Edge queries (pressed/released) compare a snapshot taken in begin_frame()
// against the live state mutated by on_event(), so they're stable across all
// queries within a single frame.
class Input {
  public:
    void begin_frame();
    void on_event(const SDL_Event& ev);

    // An action maps to one or more scancodes; ANY bound key satisfies the query.
    void bind_action(std::string name, SDL_Scancode key);
    void bind_action(std::string name, std::initializer_list<SDL_Scancode> keys);
    void clear_actions();

    // Raw key queries (scancode-based).
    bool down(SDL_Scancode) const;
    bool pressed(SDL_Scancode) const;  // went down this frame
    bool released(SDL_Scancode) const; // went up this frame

    // Named-action queries.
    bool down(std::string_view name) const;
    bool pressed(std::string_view name) const;
    bool released(std::string_view name) const;

  private:
    static constexpr size_t kNumKeys = SDL_SCANCODE_COUNT;

    struct Binding {
        std::vector<SDL_Scancode> keys;
    };

    // Transparent hash: the string_view queries above look up without
    // allocating a temporary key.
    StringMap<Binding> actions_;
    std::array<bool, kNumKeys> curr_{};
    std::array<bool, kNumKeys> prev_{};
};

} // namespace vaxelis
