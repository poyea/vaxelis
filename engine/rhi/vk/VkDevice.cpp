// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/rhi/vk/VkDevice.hpp"

namespace vaxelis::rhi::vk {

vaxelis::expected<std::unique_ptr<IDevice>, RhiError> create_vk_device() {
    return vaxelis::unexpected(RhiError::BackendUnavailable);
}

} // namespace vaxelis::rhi::vk
