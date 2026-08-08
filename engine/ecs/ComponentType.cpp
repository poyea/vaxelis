#include "engine/ecs/ComponentType.hpp"

#include <vector>

#include "engine/core/Log.hpp"

namespace vaxelis::ecs {

namespace {

/// Function-local so the table is initialised on first use, whichever
/// translation unit registers a component first.
std::vector<ComponentInfo>& table() {
    static std::vector<ComponentInfo> entries;
    return entries;
}

const ComponentInfo& null_info() {
    static const ComponentInfo none{};
    return none;
}

} // namespace

namespace detail {

ComponentId register_component(ComponentInfo info) {
    auto& entries = table();
    const auto id = static_cast<ComponentId>(entries.size());
    if (id >= kMaxComponents) {
        // Signature is one 64-bit word; going past it would silently drop bits.
        VX_ERROR("ecs: component limit of {} reached, '{}' will not be usable", kMaxComponents,
                 info.name);
        return kMaxComponents;
    }
    info.id = id;
    entries.push_back(info);
    return id;
}

} // namespace detail

const ComponentInfo& component_info(ComponentId id) {
    const auto& entries = table();
    return id < entries.size() ? entries[id] : null_info();
}

uint32_t registered_component_count() {
    return static_cast<uint32_t>(table().size());
}

} // namespace vaxelis::ecs
