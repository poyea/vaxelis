// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

/// @file
/// A device that keeps whatever is submitted to it instead of touching a GPU,
/// so renderer behaviour can be asserted on the CPU.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "engine/rhi/Rhi.hpp"

namespace vaxelis::testing {

/// Mirrors the vertex layout Rhi.hpp documents for draw_sprite_batch:
/// interleaved position, uv and colour at a 32-byte stride.
struct RecordedVertex {
    float x{0.0f};
    float y{0.0f};
    float u{0.0f};
    float v{0.0f};
    float r{0.0f};
    float g{0.0f};
    float b{0.0f};
    float a{0.0f};
};
static_assert(sizeof(RecordedVertex) == 32, "must match the RHI's sprite vertex layout");

/// Records buffer uploads and draw calls. Handles are handed out in sequence
/// and never point at anything real.
class RecordingDevice final : public rhi::IDevice {
  public:
    /// Contents of the most recent update_buffer.
    std::vector<RecordedVertex> vertices;
    /// How many times draw_sprite_batch was called.
    int draw_calls{0};

    expected<rhi::TextureHandle, rhi::RhiError> create_texture(const rhi::TextureDesc&) override {
        return rhi::TextureHandle{++m_next_id};
    }
    expected<rhi::ShaderHandle, rhi::RhiError> create_shader(const rhi::ShaderDesc&) override {
        return rhi::ShaderHandle{++m_next_id};
    }
    expected<rhi::BufferHandle, rhi::RhiError> create_buffer(const rhi::BufferDesc&) override {
        return rhi::BufferHandle{++m_next_id};
    }

    void destroy(rhi::TextureHandle) override {}
    void destroy(rhi::ShaderHandle) override {}
    void destroy(rhi::BufferHandle) override {}

    void update_buffer(rhi::BufferHandle, std::span<const std::byte> data, size_t) override {
        vertices.resize(data.size() / sizeof(RecordedVertex));
        if (!data.empty())
            std::memcpy(vertices.data(), data.data(), data.size());
    }
    void update_texture(rhi::TextureHandle, const rhi::TextureUpdate&) override {}

    void begin_frame(vec4, uint32_t, uint32_t) override {}
    void end_frame() override {}

    void draw_sprite_batch(rhi::ShaderHandle, rhi::BufferHandle, rhi::BufferHandle, uint32_t,
                           rhi::TextureHandle, const mat4&) override {
        ++draw_calls;
    }

  private:
    uint32_t m_next_id{0};
};

} // namespace vaxelis::testing
