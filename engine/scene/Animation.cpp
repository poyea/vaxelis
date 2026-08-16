// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/scene/Animation.hpp"

#include <algorithm>

#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"

namespace vaxelis {

std::vector<vec4> animation_frames(uint32_t columns, uint32_t rows, uint32_t count,
                                   uint32_t texture_width, uint32_t texture_height) {
    std::vector<vec4> frames;
    if (columns == 0 || rows == 0)
        return frames;

    const uint32_t total = count == 0 ? columns * rows : std::min(count, columns * rows);
    const float du = 1.0f / static_cast<float>(columns);
    const float dv = 1.0f / static_cast<float>(rows);
    // Half a texel, but never more than half the cell. Without the atlas size
    // there is nothing to inset by; with more cells than texels the plain half
    // texel would reach past the cell's own centre and inverted the rect, so
    // clamping collapses it to that centre the way a one-texel cell already did.
    const float iu =
        texture_width > 0 ? std::min(0.5f / static_cast<float>(texture_width), 0.5f * du) : 0.0f;
    const float iv =
        texture_height > 0 ? std::min(0.5f / static_cast<float>(texture_height), 0.5f * dv) : 0.0f;
    frames.reserve(total);
    for (uint32_t i = 0; i < total; ++i) {
        const auto col = static_cast<float>(i % columns);
        const auto row = static_cast<float>(i / columns);
        frames.push_back(
            {col * du + iu, row * dv + iv, (col + 1.0f) * du - iu, (row + 1.0f) * dv - iv});
    }
    return frames;
}

namespace {

/// Moves to the next frame according to `mode`, updating direction and the
/// playing flag as the mode requires.
void step(AnimationComponent& a) {
    const auto last = static_cast<uint32_t>(a.frames.size() - 1);
    switch (a.mode) {
    case AnimationMode::Loop:
        a.frame = a.frame == last ? 0 : a.frame + 1;
        break;
    case AnimationMode::Once:
        if (a.frame == last)
            a.playing = false; // hold the last frame
        else
            ++a.frame;
        break;
    case AnimationMode::PingPong:
        if (a.reversing) {
            if (a.frame == 0) {
                a.reversing = false;
                a.frame = last == 0 ? 0 : 1;
            } else {
                --a.frame;
            }
        } else if (a.frame == last) {
            a.reversing = true;
            a.frame = last == 0 ? 0 : last - 1;
        } else {
            ++a.frame;
        }
        break;
    }
}

} // namespace

void update_animations(Scene& scene, float dt) {
    auto& reg = scene.registry();
    auto view = reg.view<AnimationComponent, SpriteComponent>();
    for (auto e : view) {
        auto& anim = view.get<AnimationComponent>(e);
        if (anim.frames.empty())
            continue;

        if (anim.playing && anim.fps > 0.0f) {
            const float per_frame = 1.0f / anim.fps;
            anim.elapsed += dt;
            // A long dt must not skip frames silently, so drain it a frame at
            // a time rather than jumping straight to the end.
            while (anim.elapsed >= per_frame && anim.playing) {
                anim.elapsed -= per_frame;
                step(anim);
            }
        }

        if (anim.frame < anim.frames.size())
            view.get<SpriteComponent>(e).uv_rect = anim.frames[anim.frame];
    }
}

} // namespace vaxelis
