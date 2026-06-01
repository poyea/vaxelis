#include "engine/input/Input.hpp"

#include <SDL3/SDL_events.h>

namespace vaxelis {

void Input::begin_frame() {
    prev_ = curr_;
}

void Input::on_event(const SDL_Event& ev) {
    if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
        const auto sc = ev.key.scancode;
        if (sc >= 0 && static_cast<size_t>(sc) < kNumKeys) {
            curr_[static_cast<size_t>(sc)] = (ev.type == SDL_EVENT_KEY_DOWN);
        }
    }
}

void Input::bind_action(std::string name, SDL_Scancode key) {
    actions_[std::move(name)].keys.push_back(key);
}

void Input::bind_action(std::string name, std::initializer_list<SDL_Scancode> keys) {
    auto& b = actions_[std::move(name)];
    for (auto k : keys)
        b.keys.push_back(k);
}

void Input::clear_actions() {
    actions_.clear();
}

bool Input::down(SDL_Scancode k) const {
    auto i = static_cast<size_t>(k);
    return i < kNumKeys && curr_[i];
}
bool Input::pressed(SDL_Scancode k) const {
    auto i = static_cast<size_t>(k);
    return i < kNumKeys && curr_[i] && !prev_[i];
}
bool Input::released(SDL_Scancode k) const {
    auto i = static_cast<size_t>(k);
    return i < kNumKeys && !curr_[i] && prev_[i];
}

bool Input::down(std::string_view name) const {
    // unordered_map heterogeneous lookup is C++20 only with a transparent hash,
    // so we accept the std::string copy here — query rate is low.
    auto it = actions_.find(std::string(name));
    if (it == actions_.end())
        return false;
    for (auto k : it->second.keys)
        if (down(k))
            return true;
    return false;
}
bool Input::pressed(std::string_view name) const {
    auto it = actions_.find(std::string(name));
    if (it == actions_.end())
        return false;
    for (auto k : it->second.keys)
        if (pressed(k))
            return true;
    return false;
}
bool Input::released(std::string_view name) const {
    auto it = actions_.find(std::string(name));
    if (it == actions_.end())
        return false;
    for (auto k : it->second.keys)
        if (released(k))
            return true;
    return false;
}

} // namespace vaxelis
