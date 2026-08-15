// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/input/Input.hpp"

#include <SDL3/SDL_events.h>

namespace vaxelis {

void Input::begin_frame() {
    m_prev = m_curr;
}

void Input::latch_edges() {
    for (size_t i = 0; i < kNumKeys; ++i) {
        // OR rather than assign: a press must survive frames that run no fixed
        // step at all, which is most of them above 60Hz.
        m_latched_pressed[i] = m_latched_pressed[i] || (m_curr[i] && !m_prev[i]);
        m_latched_released[i] = m_latched_released[i] || (!m_curr[i] && m_prev[i]);
    }
}

void Input::begin_fixed_step() {
    m_step_pressed = m_latched_pressed;
    m_step_released = m_latched_released;
    m_latched_pressed.fill(false);
    m_latched_released.fill(false);
}

bool Input::step_pressed(SDL_Scancode k) const {
    auto i = static_cast<size_t>(k);
    return i < kNumKeys && m_step_pressed[i];
}
bool Input::step_released(SDL_Scancode k) const {
    auto i = static_cast<size_t>(k);
    return i < kNumKeys && m_step_released[i];
}
bool Input::step_pressed(std::string_view name) const {
    auto it = m_actions.find(name);
    if (it == m_actions.end())
        return false;
    for (auto k : it->second.keys)
        if (step_pressed(k))
            return true;
    return false;
}
bool Input::step_released(std::string_view name) const {
    auto it = m_actions.find(name);
    if (it == m_actions.end())
        return false;
    for (auto k : it->second.keys)
        if (step_released(k))
            return true;
    return false;
}

void Input::on_event(const SDL_Event& ev) {
    if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
        const auto sc = ev.key.scancode;
        if (sc >= 0 && static_cast<size_t>(sc) < kNumKeys) {
            m_curr[static_cast<size_t>(sc)] = (ev.type == SDL_EVENT_KEY_DOWN);
        }
    }
}

void Input::bind_action(std::string name, SDL_Scancode key) {
    m_actions[std::move(name)].keys.push_back(key);
}

void Input::bind_action(std::string name, std::initializer_list<SDL_Scancode> keys) {
    auto& b = m_actions[std::move(name)];
    for (auto k : keys)
        b.keys.push_back(k);
}

void Input::clear_actions() {
    m_actions.clear();
}

bool Input::down(SDL_Scancode k) const {
    auto i = static_cast<size_t>(k);
    return i < kNumKeys && m_curr[i];
}
bool Input::pressed(SDL_Scancode k) const {
    auto i = static_cast<size_t>(k);
    return i < kNumKeys && m_curr[i] && !m_prev[i];
}
bool Input::released(SDL_Scancode k) const {
    auto i = static_cast<size_t>(k);
    return i < kNumKeys && !m_curr[i] && m_prev[i];
}

bool Input::down(std::string_view name) const {
    auto it = m_actions.find(name);
    if (it == m_actions.end())
        return false;
    for (auto k : it->second.keys)
        if (down(k))
            return true;
    return false;
}
bool Input::pressed(std::string_view name) const {
    auto it = m_actions.find(name);
    if (it == m_actions.end())
        return false;
    for (auto k : it->second.keys)
        if (pressed(k))
            return true;
    return false;
}
bool Input::released(std::string_view name) const {
    auto it = m_actions.find(name);
    if (it == m_actions.end())
        return false;
    for (auto k : it->second.keys)
        if (released(k))
            return true;
    return false;
}

} // namespace vaxelis
