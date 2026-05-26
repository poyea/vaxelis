#include "engine/core/Log.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace vaxelis {

void init_logging() {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("vaxelis", sink);
    logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    logger->set_level(spdlog::level::trace);
    spdlog::set_default_logger(logger);
}

}  // namespace vaxelis
