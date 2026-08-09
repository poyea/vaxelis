#pragma once

/// @file
/// Runtime type information for components, in the form archetype storage
/// needs: a dense id per type plus the handful of operations required to move a
/// component around without knowing what it is.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace vaxelis::ecs {

/// Dense runtime id for a component type, assigned on first use and stable for
/// the life of the process. Dense from zero so it can index a bitmask directly.
using ComponentId = uint32_t;

/// Upper bound on distinct component types, chosen so a Signature fits in one
/// 64-bit word and a signature comparison is a single integer compare.
inline constexpr uint32_t kMaxComponents = 64;

/// The id handed back when a type could not be registered. Never addresses a
/// bit in a Signature, and every entry point rejects it rather than storing it.
inline constexpr ComponentId kInvalidComponent = ~ComponentId{0};

/// The type-erased operations column storage needs.
///
/// A component must be default-constructible, nothrow-move-constructible and
/// destructible. Adding or removing a component relocates an entity's whole
/// row between archetypes, and that relocation must not fail halfway.
struct ComponentInfo {
    /// The id this metadata was registered under.
    ComponentId id{0};
    /// Bytes one component occupies; the stride of its column.
    uint32_t size{0};
    /// Alignment the type requires.
    uint32_t alignment{0};
    /// Human-readable type name, for logs and tooling.
    std::string_view name;
    /// Default-constructs one component at `at`.
    void (*default_construct)(std::byte* at){nullptr};
    /// Move-constructs one component at `to` from `from`, leaving `from` alive
    /// but moved-from; the caller destroys it afterwards.
    void (*move_construct)(std::byte* to, std::byte* from){nullptr};
    /// Runs one component's destructor.
    void (*destroy)(std::byte* at){nullptr};
};

namespace detail {

/// The three operations above, generated once per component type.
template <class T> struct ComponentOps {
    static void default_construct(std::byte* at) { std::construct_at(reinterpret_cast<T*>(at)); }

    static void move_construct(std::byte* to, std::byte* from) {
        std::construct_at(reinterpret_cast<T*>(to), std::move(*reinterpret_cast<T*>(from)));
    }

    static void destroy(std::byte* at) { std::destroy_at(reinterpret_cast<T*>(at)); }
};

/// Adds `info` to the process-wide table and returns its assigned id, or
/// kInvalidComponent once kMaxComponents types are already registered. Not
/// thread safe: ids are expected to be minted during startup, which is what
/// happens naturally when component_id<T>() is first called.
ComponentId register_component(ComponentInfo info);

/// Registration keyed on the bare type. Callers reach this through
/// component_id(), which strips cv-qualifiers and references first.
template <class T> ComponentId component_id_for() {
    static_assert(std::is_object_v<T>, "components must be object types");
    static_assert(std::is_default_constructible_v<T>,
                  "components must be default-constructible: a fresh row default-fills");
    static_assert(std::is_nothrow_move_constructible_v<T>,
                  "components must be nothrow-movable: structural changes relocate rows");
    static_assert(std::is_destructible_v<T>, "components must be destructible");
    static_assert(alignof(T) <= alignof(std::max_align_t),
                  "over-aligned components are not supported by the column allocator");

    static const ComponentId id = register_component({
        .size = static_cast<uint32_t>(sizeof(T)),
        .alignment = static_cast<uint32_t>(alignof(T)),
        .name = typeid(T).name(),
        .default_construct = &ComponentOps<T>::default_construct,
        .move_construct = &ComponentOps<T>::move_construct,
        .destroy = &ComponentOps<T>::destroy,
    });
    return id;
}

} // namespace detail

/// Metadata for an id previously handed out by component_id(). Unknown ids,
/// including kInvalidComponent, read back as a zeroed record.
const ComponentInfo& component_info(ComponentId id);

/// How many component types have been registered so far.
uint32_t registered_component_count();

/// The id for `T`, assigning one on first use.
///
/// Qualifiers are stripped, so `T`, `const T` and `T&` all name the same
/// component. Without that, `each<const Pos>` would build a query mask holding
/// a bit no archetype carries, and would silently visit nothing.
template <class T> ComponentId component_id() {
    return detail::component_id_for<std::remove_cvref_t<T>>();
}

} // namespace vaxelis::ecs
