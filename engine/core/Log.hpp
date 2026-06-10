#pragma once

#include <spdlog/spdlog.h>

namespace vaxelis {

/// Installs the engine's default spdlog logger (color stdout, trace level).
/// Call once at startup.
void init_logging();

/// The engine-wide spdlog logger.
inline auto& log() {
    return *spdlog::default_logger_raw();
}

} // namespace vaxelis

/// Logs a trace-level message via the engine logger (fmt-style arguments).
#define VX_TRACE(...) ::vaxelis::log().trace(__VA_ARGS__)
/// Logs an info-level message via the engine logger.
#define VX_INFO(...) ::vaxelis::log().info(__VA_ARGS__)
/// Logs a warn-level message via the engine logger.
#define VX_WARN(...) ::vaxelis::log().warn(__VA_ARGS__)
/// Logs an error-level message via the engine logger.
#define VX_ERROR(...) ::vaxelis::log().error(__VA_ARGS__)
