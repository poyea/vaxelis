// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

/// @file
/// Frame-based sprite animation driven off SpriteComponent::uv_rect.

#include <cstdint>
#include <vector>

#include "engine/math/Math.hpp"

namespace vaxelis {

class Scene;

/// How an animation behaves once it reaches the last frame.
enum class AnimationMode : uint8_t {
    /// Restart from the first frame.
    Loop,
    /// Hold the last frame and stop advancing.
    Once,
    /// Walk back down to the first frame, then up again.
    PingPong,
};

/// Per-entity animation state. Frames are uv sub-rects into whatever texture
/// the entity's SpriteComponent already uses, in the same
/// (min_u, min_v, max_u, max_v) convention as SpriteBatch, so a strip and an
/// irregular atlas are both expressible.
///
/// The system writes the current frame into SpriteComponent::uv_rect; nothing
/// else touches it.
struct AnimationComponent {
    std::vector<vec4> frames;
    /// Frames per second. Zero or negative freezes on the current frame.
    float fps{12.0f};
    AnimationMode mode{AnimationMode::Loop};
    bool playing{true};

    /// Frame currently shown.
    uint32_t frame{0};
    /// Seconds accumulated toward the next frame.
    float elapsed{0.0f};
    /// PingPong only: true while walking back toward the first frame.
    bool reversing{false};

    /// True once a Once animation has reached its last frame.
    bool finished() const {
        return mode == AnimationMode::Once && !frames.empty() && frame + 1 == frames.size() &&
               !playing;
    }
};

/// Builds evenly spaced frames across a `columns` x `rows` grid, in reading
/// order, for the common case of a sprite strip or a uniform atlas page.
///
/// Pass the atlas size in pixels to inset each frame by half a texel. Without
/// it the frames meet exactly on shared cell edges, where a sample can pick up
/// the neighbouring frame. Sizes of zero skip the inset, which is only right
/// when the grid is the whole texture. A cell smaller than two texels collapses
/// to its centre rather than inverting.
std::vector<vec4> animation_frames(uint32_t columns, uint32_t rows, uint32_t count = 0,
                                   uint32_t texture_width = 0, uint32_t texture_height = 0);

/// Advances every playing AnimationComponent and writes the current frame into
/// the entity's SpriteComponent. Entities without a SpriteComponent are
/// skipped. Call once per frame with the frame delta.
void update_animations(Scene& scene, float dt);

} // namespace vaxelis
