#include "engine/rhi/vk/VkDevice.hpp"

namespace vaxelis::rhi::vk {

std::expected<std::unique_ptr<IDevice>, RhiError> create_vk_device() {
    return std::unexpected(RhiError::BackendUnavailable);
}

}  // namespace vaxelis::rhi::vk
