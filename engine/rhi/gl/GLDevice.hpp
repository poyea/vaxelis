#pragma once

#include "engine/rhi/Rhi.hpp"

namespace vaxelis::rhi::gl {

std::expected<std::unique_ptr<IDevice>, RhiError> create_gl_device();

}  // namespace vaxelis::rhi::gl
