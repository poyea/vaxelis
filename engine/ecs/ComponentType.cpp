// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/ecs/ComponentType.hpp"

#include <vector>

#include "engine/core/Log.hpp"

namespace vaxelis::ecs {

namespace {

/// Function-local so the table is initialised on first use, whichever
/// translation unit registers a component first.
///
/// Deliberately never freed: an Archetype with static storage would otherwise
/// out-live it and read a destroyed vector while destroying its rows.
std::vector<ComponentInfo>& table() {
    // Reserved to the cap so a later registration never reallocates: callers
    // hold `const ComponentInfo&` across user code that may itself register.
    static std::vector<ComponentInfo>* entries = [] {
        auto* v = new std::vector<ComponentInfo>();
        v->reserve(kMaxComponents);
        return v;
    }();
    return *entries;
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
        // Signature is one 64-bit word, so there is no bit left to represent
        // this type. Returning an in-range id would let it flow into a mask
        // that silently drops it; kInvalidComponent is rejected at every entry
        // point instead.
        VX_ERROR("ecs: component limit of {} reached, '{}' cannot be registered", kMaxComponents,
                 info.name);
        return kInvalidComponent;
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
