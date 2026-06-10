#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "engine/core/Expected.hpp"
#include "engine/math/Math.hpp"

namespace vaxelis::rhi {

/// Rendering backend selection for create_device().
enum class Backend { OpenGL, Vulkan };

/// Failure reasons surfaced by device creation and resource calls.
enum class RhiError {
    BackendUnavailable,
    InvalidHandle,
    ShaderCompileFailed,
    ProgramLinkFailed,
    UnsupportedFormat,
    OutOfMemory,
};

/// Human-readable name for an RhiError.
std::string_view to_string(RhiError);

/// Opaque texture handle. Trivially copyable, cheap to pass around. 0 == null.
struct TextureHandle {
    uint32_t id{0};
    constexpr bool valid() const { return id != 0; }
};
/// Opaque shader-program handle. Trivially copyable, cheap to pass around. 0 == null.
struct ShaderHandle {
    uint32_t id{0};
    constexpr bool valid() const { return id != 0; }
};
/// Opaque buffer handle. Trivially copyable, cheap to pass around. 0 == null.
struct BufferHandle {
    uint32_t id{0};
    constexpr bool valid() const { return id != 0; }
};

/// Pixel formats supported by create_texture().
enum class TextureFormat { RGBA8 };

/// Creation parameters for a 2D texture.
struct TextureDesc {
    uint32_t width{0};
    uint32_t height{0};
    TextureFormat format{TextureFormat::RGBA8};
    const void* initial_data{nullptr}; ///< tightly packed; size = width*height*bpp
};

/// In-place update of a sub-rectangle. `data` is tightly packed RGBA8,
/// size = width*height*bpp. The region must lie within the texture bounds.
struct TextureUpdate {
    uint32_t x{0};
    uint32_t y{0};
    uint32_t width{0};
    uint32_t height{0};
    const void* data{nullptr};
};

/// How a buffer will be bound.
enum class BufferUsage { Vertex, Index, Uniform };
/// Update frequency hint: Static = filled once, Dynamic = updated per frame.
enum class BufferAccess { Static, Dynamic };

/// Creation parameters for a GPU buffer.
struct BufferDesc {
    BufferUsage usage{BufferUsage::Vertex};
    BufferAccess access{BufferAccess::Static};
    size_t size_bytes{0};
    const void* initial_data{nullptr};
};

/// GLSL source pair for a shader program.
struct ShaderDesc {
    std::string_view vertex_src;
    std::string_view fragment_src;
};

/// Abstract GPU device: resource creation/destruction, uploads, and frame
/// submission. One implementation per Backend.
class IDevice {
  public:
    virtual ~IDevice() = default;

    virtual vaxelis::expected<TextureHandle, RhiError> create_texture(const TextureDesc&) = 0;
    virtual vaxelis::expected<ShaderHandle, RhiError> create_shader(const ShaderDesc&) = 0;
    virtual vaxelis::expected<BufferHandle, RhiError> create_buffer(const BufferDesc&) = 0;

    virtual void destroy(TextureHandle) = 0;
    virtual void destroy(ShaderHandle) = 0;
    virtual void destroy(BufferHandle) = 0;

    /// Uploads `data` into the buffer starting at `offset_bytes`.
    virtual void update_buffer(BufferHandle, std::span<const std::byte> data,
                               size_t offset_bytes = 0) = 0;

    /// In-place upload into an existing texture (glTexSubImage2D). No-op on an
    /// invalid handle or out-of-bounds region. Keeps the handle and GPU object
    /// stable so dependents don't need rebinding.
    virtual void update_texture(TextureHandle, const TextureUpdate&) = 0;

    /// Clears the backbuffer and sets the viewport for this frame.
    virtual void begin_frame(vec4 clear_color, uint32_t fb_width, uint32_t fb_height) = 0;
    virtual void end_frame() = 0;

    /// Batched sprite draw. Vertex layout (interleaved, stride 32 bytes):
    /// @code
    ///   layout(location=0) vec2 a_pos
    ///   layout(location=1) vec2 a_uv
    ///   layout(location=2) vec4 a_color
    /// @endcode
    /// Indices are u16. `proj` is the projection matrix; per-quad transforms
    /// are pre-baked into vertex positions on the CPU side.
    virtual void draw_sprite_batch(ShaderHandle, BufferHandle vb, BufferHandle ib,
                                   uint32_t index_count, TextureHandle, const mat4& proj) = 0;
};

/// Factory. Returns BackendUnavailable for stubs (e.g. Vulkan in M1).
vaxelis::expected<std::unique_ptr<IDevice>, RhiError> create_device(Backend);

// Sanity: handles must remain trivially copyable so they can move around freely.
static_assert(std::is_trivially_copyable_v<TextureHandle>);
static_assert(std::is_trivially_copyable_v<ShaderHandle>);
static_assert(std::is_trivially_copyable_v<BufferHandle>);

} // namespace vaxelis::rhi
