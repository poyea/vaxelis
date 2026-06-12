#include <gtest/gtest.h>

#include "engine/rhi/Rhi.hpp"

using namespace vaxelis::rhi;

TEST(Rhi, VulkanBackendIsUnavailableInM1) {
    auto dev = create_device(Backend::Vulkan);
    ASSERT_FALSE(dev.has_value());
    EXPECT_EQ(dev.error(), RhiError::BackendUnavailable);
}

TEST(Rhi, HandlesAreTriviallyCopyableAndNullByDefault) {
    static_assert(std::is_trivially_copyable_v<TextureHandle>);
    static_assert(std::is_trivially_copyable_v<ShaderHandle>);
    static_assert(std::is_trivially_copyable_v<BufferHandle>);

    TextureHandle t{};
    EXPECT_FALSE(t.valid());
    EXPECT_EQ(t.id, 0u);
}

TEST(Rhi, ToStringCoversAllErrors) {
    EXPECT_EQ(to_string(RhiError::BackendUnavailable), "BackendUnavailable");
    EXPECT_EQ(to_string(RhiError::ShaderCompileFailed), "ShaderCompileFailed");
}
