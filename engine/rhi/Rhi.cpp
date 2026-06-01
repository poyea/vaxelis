#include "engine/rhi/Rhi.hpp"

#include <utility>

#include "engine/rhi/gl/GLDevice.hpp"
#include "engine/rhi/vk/VkDevice.hpp"

namespace vaxelis::rhi {

std::string_view to_string(RhiError e) {
    switch (e) {
    case RhiError::BackendUnavailable:
        return "BackendUnavailable";
    case RhiError::InvalidHandle:
        return "InvalidHandle";
    case RhiError::ShaderCompileFailed:
        return "ShaderCompileFailed";
    case RhiError::ProgramLinkFailed:
        return "ProgramLinkFailed";
    case RhiError::UnsupportedFormat:
        return "UnsupportedFormat";
    case RhiError::OutOfMemory:
        return "OutOfMemory";
    }
    std::unreachable();
}

vaxelis::expected<std::unique_ptr<IDevice>, RhiError> create_device(Backend b) {
    switch (b) {
    case Backend::OpenGL:
        return gl::create_gl_device();
    case Backend::Vulkan:
        return vk::create_vk_device();
    }
    return vaxelis::unexpected(RhiError::BackendUnavailable);
}

} // namespace vaxelis::rhi
