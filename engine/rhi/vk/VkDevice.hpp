#pragma once

#include "engine/rhi/Rhi.hpp"

namespace vaxelis::rhi::vk {

// Stub. Vulkan backend is not implemented in M1; this always returns
// BackendUnavailable. The interface exists so callsites compile and tests can
// assert the expected error.
vaxelis::expected<std::unique_ptr<IDevice>, RhiError> create_vk_device();

}  // namespace vaxelis::rhi::vk
