#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>

#include "engine/math/Math.hpp"

namespace vaxelis::rhi {

enum class Backend { OpenGL, Vulkan };

enum class RhiError {
    BackendUnavailable,
    InvalidHandle,
    ShaderCompileFailed,
    ProgramLinkFailed,
    UnsupportedFormat,
    OutOfMemory,
};

std::string_view to_string(RhiError);

// Opaque handles. Trivially copyable, cheap to pass around. 0 == null.
struct TextureHandle { uint32_t id{0}; constexpr bool valid() const { return id != 0; } };
struct ShaderHandle  { uint32_t id{0}; constexpr bool valid() const { return id != 0; } };
struct BufferHandle  { uint32_t id{0}; constexpr bool valid() const { return id != 0; } };

enum class TextureFormat { RGBA8 };

struct TextureDesc {
    uint32_t width{0};
    uint32_t height{0};
    TextureFormat format{TextureFormat::RGBA8};
    const void* initial_data{nullptr};  // tightly packed; size = width*height*bpp
};

enum class BufferUsage { Vertex, Index, Uniform };
enum class BufferAccess { Static, Dynamic };

struct BufferDesc {
    BufferUsage usage{BufferUsage::Vertex};
    BufferAccess access{BufferAccess::Static};
    size_t size_bytes{0};
    const void* initial_data{nullptr};
};

struct ShaderDesc {
    std::string_view vertex_src;
    std::string_view fragment_src;
};

class IDevice {
public:
    virtual ~IDevice() = default;

    virtual std::expected<TextureHandle, RhiError> create_texture(const TextureDesc&) = 0;
    virtual std::expected<ShaderHandle, RhiError>  create_shader(const ShaderDesc&) = 0;
    virtual std::expected<BufferHandle, RhiError>  create_buffer(const BufferDesc&) = 0;

    virtual void destroy(TextureHandle) = 0;
    virtual void destroy(ShaderHandle) = 0;
    virtual void destroy(BufferHandle) = 0;

    virtual void update_buffer(BufferHandle, std::span<const std::byte> data, size_t offset_bytes = 0) = 0;

    virtual void begin_frame(vec4 clear_color, uint32_t fb_width, uint32_t fb_height) = 0;
    virtual void end_frame() = 0;

    // Batched sprite draw. Vertex layout (interleaved, stride 32 bytes):
    //   layout(location=0) vec2 a_pos
    //   layout(location=1) vec2 a_uv
    //   layout(location=2) vec4 a_color
    // Indices are u16. `proj` is the projection matrix; per-quad transforms
    // are pre-baked into vertex positions on the CPU side.
    virtual void draw_sprite_batch(ShaderHandle, BufferHandle vb, BufferHandle ib,
                                   uint32_t index_count, TextureHandle, const mat4& proj) = 0;
};

// Factory. Returns BackendUnavailable for stubs (e.g. Vulkan in M1).
std::expected<std::unique_ptr<IDevice>, RhiError> create_device(Backend);

// Sanity: handles must remain trivially copyable so they can move around freely.
static_assert(std::is_trivially_copyable_v<TextureHandle>);
static_assert(std::is_trivially_copyable_v<ShaderHandle>);
static_assert(std::is_trivially_copyable_v<BufferHandle>);

}  // namespace vaxelis::rhi
