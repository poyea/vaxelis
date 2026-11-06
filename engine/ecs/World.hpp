// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

/// @file
/// The archetype world: owns entities, the archetypes they live in, and the
/// mapping between the two.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/ecs/Archetype.hpp"
#include "engine/ecs/Entity.hpp"
#include "engine/ecs/Signal.hpp"
#include "engine/ecs/Signature.hpp"

namespace vaxelis::ecs {

/// Entity storage on top of archetypes.
///
/// Every entity occupies one row of the archetype matching its component set.
/// Adding or removing a component migrates that row to the archetype for the
/// new signature. An entity with no components lives in the empty archetype.
///
/// Any structural change invalidates pointers into component storage, even one
/// made to a different entity: it can grow or reshuffle a column. Re-fetch
/// after add(), remove() or destroy().
class World {
  public:
    /// Creates a world holding nothing but the empty archetype.
    World();
    /// Destroys every entity and archetype, running component destructors.
    ~World();

    /// Not copyable: entity records point straight at the archetypes.
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    /// Movable. Archetypes live behind unique_ptr, so they keep their addresses
    /// and the Table* in every record survives the move untouched. The source
    /// is left as a valid empty world -- not merely destructible -- because
    /// Scene is held and reassigned by value (`m_scene = Scene{}`) and anything
    /// embedding a World inherits that.
    World(World&& other) noexcept;
    World& operator=(World&& other) noexcept;

    /// Listeners for `T` coming into existence on an entity: a variadic
    /// create(), or an add() that the entity did not already have. An add()
    /// that overwrites an existing `T` is not a construction and does not
    /// publish. Nothing is published for the components an archetype migration
    /// carries across -- the component did not come or go, only its address.
    template <class T> Sink on_construct() { return Sink{signal_for<T>(m_on_construct)}; }

    /// Listeners for `T` ceasing to exist: remove<T>(), or destroy() taking the
    /// whole entity with it. Published *before* the data goes, so a listener
    /// can still read the component it is being told about.
    ///
    /// destroy() announces every component the entity ends up carrying, so a
    /// component a listener adds on the way out is announced too rather than
    /// being dropped silently. Each is announced once; the order is the
    /// archetype's, which is to say unspecified.
    ///
    /// Not published by ~World(), matching entt: teardown is not deletion, and
    /// a subsystem whose own destructor is already running does not want the
    /// call. Release what an entity owns while the world is still alive.
    template <class T> Sink on_destroy() { return Sink{signal_for<T>(m_on_destroy)}; }

    /// A token that expires when this world is destroyed.
    ///
    /// A subscriber holds one beside its `World*` so that, at its own
    /// destruction, it can tell whether the world it subscribed to is still
    /// there. Subscriptions die with the world, so touching the pointer
    /// afterwards -- even to unsubscribe -- is use-after-free, and a subsystem
    /// cannot assume it outlives the scene: declaration order decides, and in
    /// a test the scene is often the later local.
    ///
    /// Deliberately not carried across a move: it tracks this object, not its
    /// contents, so each world keeps its own from construction.
    std::weak_ptr<const void> alive_token() const { return m_alive; }

    /// Creates an entity with no components.
    Entity create();

    /// Creates an entity already carrying `values`.
    ///
    /// The component set is a compile-time pack, so the destination archetype
    /// is resolved once and the row seated straight into it. Building the same
    /// entity with create() then N add() calls instead walks N intermediate
    /// archetypes, migrating the row at every step.
    template <class... Ts> Entity create(Ts&&... values) {
        using Ids = std::array<ComponentId, sizeof...(Ts)>;
        const Ids ids{component_id<std::remove_cvref_t<Ts>>()...};
        Signature sig;
        for (const ComponentId id : ids)
            sig.set(id);

        // Materialised before seating. An argument can be a reference into a
        // column -- `create(*try_get<Pos>(other))` clones one -- and seating
        // grows that column, freeing the buffer the reference points into.
        // Reading it afterwards is use-after-free. One extra move per
        // component buys that away.
        std::tuple<std::remove_cvref_t<Ts>...> staged{std::forward<Ts>(values)...};

        const Entity e = allocate_slot();
        seat(e, table_for(sig, ids));
        ++m_live;
        // seat() default-constructed the columns; drop the staged values over
        // them in one pass, still without a migration.
        std::apply(
            [&](auto&... staged_values) {
                (assign_value<std::remove_cvref_t<decltype(staged_values)>>(
                     e, std::move(staged_values)),
                 ...);
            },
            staged);
        // Published only once every component holds its value, so a listener
        // sees a finished entity rather than a half-built one. A listener is
        // free to destroy the entity or take a component off it, so the state
        // is rechecked before each call rather than trusted from before the
        // first one.
        for (const ComponentId id : ids) {
            const Record* rec = find(e);
            if (rec == nullptr)
                break;
            if (rec->table->archetype.has(id))
                publish(m_on_construct, id, e);
        }
        return e;
    }

    /// Destroys `e` and its components. Stale handles are ignored, so calling
    /// this twice is harmless.
    void destroy(Entity e);

    /// True while `e` refers to a live entity. False for a handle whose entity
    /// has been destroyed, even if the slot has since been reused.
    bool alive(Entity e) const;

    /// Number of live entities.
    size_t size() const { return m_live; }

    /// Number of distinct archetypes, including the empty one. Mostly of
    /// interest to tests and tooling: it counts the shapes in play, not the
    /// entities.
    size_t archetype_count() const { return m_tables.size(); }

    /// True when `e` currently carries `T`.
    template <class T> bool has(Entity e) const {
        const Record* rec = find(e);
        return rec != nullptr && rec->table->archetype.has(component_id<T>());
    }

    /// Gives `e` a `T`, moving it to the archetype for its new shape.
    ///
    /// Adding a component the entity already has overwrites it in place rather
    /// than migrating. Returns nullptr for a stale handle, which is logged: a
    /// caller that knows the entity is alive can dereference without checking.
    template <class T> T* add(Entity e, T value = T{}) {
        // The overwrite path below assigns over a default-constructed slot, a
        // stronger requirement than registration checks. Say so here rather
        // than letting the failure surface as "no viable operator=".
        static_assert(std::is_move_assignable_v<T>,
                      "World::add overwrites the component in place, so it must be "
                      "move-assignable as well as movable");
        Record* rec = find(e);
        if (rec == nullptr) {
            report_stale("add");
            return nullptr;
        }
        const ComponentId id = component_id<T>();
        if (id == kInvalidComponent) {
            // Registration failed, so no archetype can hold this type and
            // migrating would land the row in a column that does not exist.
            report_unregistered("add");
            return nullptr;
        }
        const bool fresh = !rec->table->archetype.has(id);
        if (fresh) {
            Signature target = rec->table->archetype.signature();
            target.set(id);
            migrate(e, *rec, target);
        }
        rec->table->archetype.get<T>(rec->row) = std::move(value);
        if (fresh) {
            publish(m_on_construct, id, e);
            // A listener is free to make structural changes, which would move
            // the row out from under a pointer taken before the call.
            return try_get<T>(e);
        }
        return &rec->table->archetype.get<T>(rec->row);
    }

    /// Takes `T` away from `e`. A no-op when the entity is stale or does not
    /// have the component.
    template <class T> void remove(Entity e) {
        Record* rec = find(e);
        if (rec == nullptr)
            return;
        const ComponentId id = component_id<T>();
        if (!rec->table->archetype.has(id))
            return;
        // Before the migration drops the column, so a listener can still read
        // what it is being told about.
        publish(m_on_destroy, id, e);
        // The listener may have moved the row, or taken the component itself.
        rec = find(e);
        if (rec == nullptr || !rec->table->archetype.has(id))
            return;
        Signature target = rec->table->archetype.signature();
        target.reset(id);
        migrate(e, *rec, target);
    }

    /// Pointer to `e`'s `T`, or nullptr when the entity is stale or lacks it.
    template <class T> T* try_get(Entity e) {
        Record* rec = find(e);
        if (rec == nullptr || !rec->table->archetype.has(component_id<T>()))
            return nullptr;
        return &rec->table->archetype.get<T>(rec->row);
    }

    /// Read-only pointer to `e`'s `T`, or nullptr. See try_get().
    template <class T> const T* try_get(Entity e) const {
        const Record* rec = find(e);
        if (rec == nullptr || !rec->table->archetype.has(component_id<T>()))
            return nullptr;
        return &rec->table->archetype.get<T>(rec->row);
    }

    /// Calls `fn(Ts&...)` for every entity carrying all of `Ts`.
    ///
    /// Each matching archetype hands over its columns once and the loop walks
    /// them in memory order; archetypes lacking any of `Ts` are skipped on a
    /// single mask test.
    ///
    /// Do not add, remove or destroy while iterating: a structural change
    /// reshapes the storage being walked. Collect and apply afterwards.
    template <class... Ts, class Fn> void each(Fn&& fn) {
        visit_matching<Ts...>(*this, [&fn](Entity, auto&... cols) { fn(cols...); });
    }

    /// Read-only each(); the callback receives `const Ts&`.
    template <class... Ts, class Fn> void each(Fn&& fn) const {
        visit_matching<Ts...>(*this, [&fn](Entity, auto&... cols) { fn(cols...); });
    }

    /// As each(), but the callback also receives the entity: `fn(Entity, Ts&...)`.
    template <class... Ts, class Fn> void each_entity(Fn&& fn) {
        visit_matching<Ts...>(*this, std::forward<Fn>(fn));
    }

    /// Read-only each_entity(); the callback receives `const Ts&`.
    template <class... Ts, class Fn> void each_entity(Fn&& fn) const {
        visit_matching<Ts...>(*this, std::forward<Fn>(fn));
    }

  private:
    /// One archetype plus the entity owning each of its rows, kept parallel so
    /// a swap-and-pop removal can repoint the entity that moved.
    struct Table {
        explicit Table(std::span<const ComponentId> ids) : archetype(ids) {}

        Archetype archetype;
        std::vector<Entity> entities;
    };

    /// Where one entity slot currently lives.
    struct Record {
        Table* table{nullptr};
        size_t row{0};
        uint32_t generation{0};
        bool alive{false};
    };

    /// Takes a free entity slot, or grows the record table for a new one.
    Entity allocate_slot();

    /// Overwrites `e`'s `T` if it has one. Used by the variadic create().
    template <class T, class U> void assign_value(Entity e, U&& value) {
        if (T* slot = try_get<T>(e))
            *slot = std::forward<U>(value);
    }

    /// Finds the table for `sig`, creating it when this shape is new.
    Table& table_for(Signature sig, std::span<const ComponentId> ids);

    /// The archetype for the empty signature, created on demand. Cached because
    /// create() hits it on every call; the lazy path is what lets a moved-from
    /// world hand its tables away and still serve a later create().
    Table& empty_table();

    /// Appends `e` to `table`, keeping the row map in step.
    void seat(Entity e, Table& table);

    /// Removes `row` from `table` and repoints whichever entity got swapped
    /// into the gap.
    void unseat(Table& table, size_t row);

    /// Moves `e`'s row to the archetype for `target`, carrying across every
    /// component the two shapes share and default-constructing the rest, then
    /// dropping the old row. `rec` is updated to the new home.
    void migrate(Entity e, Record& rec, Signature target);

    /// The record for a live entity, or nullptr when the handle is stale.
    Record* find(Entity e);
    const Record* find(Entity e) const;

    /// Misuse reports. Out of line so this header does not drag in the logger.
    static void report_stale(const char* op);
    static void report_unregistered(const char* op);

    using SignalTable = std::array<Signal, kMaxComponents>;

    /// The signal for `T` in `table`, or a shared bin when the type could not
    /// register: a subscription that can never fire beats indexing out of
    /// bounds, and add() already reports the registration failure.
    template <class T> Signal& signal_for(SignalTable& table) {
        const ComponentId id = component_id<T>();
        if (id == kInvalidComponent) {
            report_unregistered("on_construct/on_destroy");
            return m_discarded;
        }
        return table[id];
    }

    /// Fires `table`'s listeners for `id`. Inline so an unwatched component
    /// type pays one empty() test per structural change.
    void publish(SignalTable& table, ComponentId id, Entity e) {
        if (id == kInvalidComponent)
            return;
        Signal& signal = table[id];
        if (!signal.empty())
            signal.publish(*this, e);
    }

    /// The one query walk behind each() and each_entity(). Deducing `Self` lets
    /// the same body serve `World&` and `const World&` -- the archetype's own
    /// const overloads then decide whether the columns come back mutable. `fn`
    /// always takes the entity first; the wrappers that do not want it drop it.
    template <class... Ts, class Self, class Fn> static void visit_matching(Self& self, Fn&& fn) {
        // A type that could not register has no bit in the mask, so the query
        // would be empty, match every archetype, and then index a column that
        // does not exist. Refusing to walk beats reading out of bounds.
        if (((component_id<Ts>() == kInvalidComponent) || ...)) {
            report_unregistered("each");
            return;
        }
        const Signature query = signature_of<Ts...>();
        for (auto& [sig, table] : self.m_tables) {
            if (!sig.contains(query))
                continue;
            // The tables are held by unique_ptr, whose operator-> hands back a
            // mutable pointer however const the map is. Without this the const
            // overloads of each()/each_entity() would quietly serve mutable
            // references and the documented contract would be a lie.
            using TableRef = std::conditional_t<std::is_const_v<Self>, const Table&, Table&>;
            TableRef bound = *table;
            auto& arch = bound.archetype;
            const size_t rows = arch.size();
            if (rows == 0)
                continue;
            // One column lookup per archetype, not per row.
            auto columns = std::tuple{arch.template column<Ts>()...};
            for (size_t row = 0; row < rows; ++row) {
                const Entity owner = table->entities[row];
                std::apply([&](auto&... cols) { fn(owner, cols[row]...); }, columns);
            }
        }
    }

    std::unordered_map<Signature, std::unique_ptr<Table>> m_tables;
    std::vector<Record> m_records;
    std::vector<uint32_t> m_free;
    Table* m_empty{nullptr};
    size_t m_live{0};

    /// Indexed by ComponentId. Flat rather than a map: the whole point of a
    /// signal is that publishing is cheap, and most ids have no listeners.
    SignalTable m_on_construct;
    SignalTable m_on_destroy;
    /// Where subscriptions for an unregisterable type go to be ignored.
    Signal m_discarded;
    /// Backs alive_token(); nothing reads through it. See that function for why
    /// the move operations leave it alone.
    std::shared_ptr<const void> m_alive{std::make_shared<char>()};
};

} // namespace vaxelis::ecs
