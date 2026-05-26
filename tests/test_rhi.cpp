#include <catch2/catch_test_macros.hpp>

#include "engine/rhi/Rhi.hpp"

using namespace vaxelis::rhi;

TEST_CASE("Vulkan backend is unavailable in M1") {
    auto dev = create_device(Backend::Vulkan);
    REQUIRE_FALSE(dev.has_value());
    REQUIRE(dev.error() == RhiError::BackendUnavailable);
}

TEST_CASE("Handles are trivially copyable and null-by-default") {
    static_assert(std::is_trivially_copyable_v<TextureHandle>);
    static_assert(std::is_trivially_copyable_v<ShaderHandle>);
    static_assert(std::is_trivially_copyable_v<BufferHandle>);

    TextureHandle t{};
    REQUIRE_FALSE(t.valid());
    REQUIRE(t.id == 0);
}

TEST_CASE("to_string covers all errors") {
    REQUIRE(to_string(RhiError::BackendUnavailable) == "BackendUnavailable");
    REQUIRE(to_string(RhiError::ShaderCompileFailed) == "ShaderCompileFailed");
}
