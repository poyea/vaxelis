#pragma once

#include <spdlog/spdlog.h>

namespace vaxelis {

void init_logging();

inline auto& log() {
    return *spdlog::default_logger_raw();
}

} // namespace vaxelis

#define VX_TRACE(...) ::vaxelis::log().trace(__VA_ARGS__)
#define VX_INFO(...) ::vaxelis::log().info(__VA_ARGS__)
#define VX_WARN(...) ::vaxelis::log().warn(__VA_ARGS__)
#define VX_ERROR(...) ::vaxelis::log().error(__VA_ARGS__)
