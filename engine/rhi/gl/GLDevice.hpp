#pragma once

#include "engine/rhi/Rhi.hpp"

namespace vaxelis::rhi::gl {

vaxelis::expected<std::unique_ptr<IDevice>, RhiError> create_gl_device();

} // namespace vaxelis::rhi::gl
