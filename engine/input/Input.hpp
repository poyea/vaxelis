// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_scancode.h>

#include "engine/core/StringMap.hpp"

union SDL_Event;

namespace vaxelis {

/// Polled input + named action bindings. Call begin_frame() once per frame
/// before SDL_PollEvent, on_event() for each SDL event. Then query.
///
/// Edge queries (pressed/released) compare a snapshot taken in begin_frame()
/// against the live state mutated by on_event(), so they're stable across all
/// queries within a single frame.
class Input {
  public:
    /// Snapshots key state for this frame's edge queries.
    void begin_frame();

    /// Records the edges this frame produced so a fixed step can still see
    /// them. Call once after event polling.
    void latch_edges();

    /// Hands the latched edges to one fixed step and clears them. Call before
    /// each on_fixed_update.
    void begin_fixed_step();
    /// Feeds one SDL event into the live key state.
    void on_event(const SDL_Event& ev);

    /// An action maps to one or more scancodes; ANY bound key satisfies the query.
    void bind_action(std::string name, SDL_Scancode key);
    /// Binds several scancodes to one action in a single call.
    void bind_action(std::string name, std::initializer_list<SDL_Scancode> keys);
    /// Removes all action bindings.
    void clear_actions();

    /// Raw key query (scancode-based): true while the key is held.
    bool down(SDL_Scancode) const;
    bool pressed(SDL_Scancode) const;  ///< went down this frame
    bool released(SDL_Scancode) const; ///< went up this frame

    /// Named-action variant of down().
    bool down(std::string_view name) const;
    /// Named-action variant of pressed().
    bool pressed(std::string_view name) const;
    /// Named-action variant of released().
    bool released(std::string_view name) const;

    /// Edge queries for fixed-timestep code.
    ///
    /// pressed()/released() are scoped to the frame, which is wrong inside
    /// on_fixed_update: that runs 0..N times per frame, so above 60Hz a press
    /// can be cleared before any step observes it, and on a frame running two
    /// steps the same press fires twice. These consume a latch instead, so one
    /// press is seen exactly once by exactly one step.
    bool step_pressed(SDL_Scancode) const;
    bool step_released(SDL_Scancode) const;
    /// Named-action variant of step_pressed().
    bool step_pressed(std::string_view name) const;
    /// Named-action variant of step_released().
    bool step_released(std::string_view name) const;

  private:
    static constexpr size_t kNumKeys = SDL_SCANCODE_COUNT;

    struct Binding {
        std::vector<SDL_Scancode> keys;
    };

    // Transparent hash: the string_view queries above look up without
    // allocating a temporary key.
    StringMap<Binding> m_actions;
    std::array<bool, kNumKeys> m_curr{};
    std::array<bool, kNumKeys> m_prev{};
    // Edges awaiting a fixed step, and the set handed to the step running now.
    std::array<bool, kNumKeys> m_latched_pressed{};
    std::array<bool, kNumKeys> m_latched_released{};
    std::array<bool, kNumKeys> m_step_pressed{};
    std::array<bool, kNumKeys> m_step_released{};
};

} // namespace vaxelis
