#pragma once

/// @file
/// The archetype world: owns entities, the archetypes they live in, and the
/// mapping between the two.

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
#include "engine/ecs/Signature.hpp"

namespace vaxelis::ecs {

/// Entity storage on top of archetypes.
///
/// Every entity lives in exactly one archetype -- the one matching the set of
/// components it currently carries -- occupying one row. Adding or removing a
/// component is a structural change: the entity's row moves to the archetype
/// for its new signature. An entity with no components at all still has a row,
/// in the empty archetype, so it can be created before it is filled in.
///
/// References and pointers into component storage are invalidated by any
/// structural change, including one on a different entity, because it can grow
/// or reshuffle a column. Fetch them again after add(), remove() or destroy().
class World {
  public:
    /// Creates a world holding nothing but the empty archetype.
    World();
    /// Destroys every entity and archetype, running component destructors.
    ~World();

    /// Not copyable: entity records point straight at the archetypes.
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    /// Creates an entity with no components.
    Entity create();

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
            // Registration failed, so no archetype can ever hold this type and
            // migrating would land the row in a column that does not exist.
            report_stale("add");
            return nullptr;
        }
        if (!rec->table->archetype.has(id)) {
            Signature target = rec->table->archetype.signature();
            target.set(id);
            migrate(e, *rec, target);
        }
        T& slot = rec->table->archetype.get<T>(rec->row);
        slot = std::move(value);
        return &slot;
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
    /// This is what the column layout is for: each matching archetype hands
    /// over its columns once, and the loop then walks them in memory order with
    /// no per-entity lookup. Archetypes that lack any of `Ts` are skipped whole
    /// on a single mask test.
    ///
    /// Do not add, remove or destroy while iterating -- a structural change
    /// reshapes the very storage being walked. Collect the entities to change
    /// and apply afterwards.
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

    /// Finds the table for `sig`, creating it when this shape is new.
    Table& table_for(Signature sig, std::span<const ComponentId> ids);

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

    /// Logs use of a stale handle. Out of line so this header does not drag the
    /// logger into everything that includes it.
    static void report_stale(const char* op);

    /// The one query walk behind each() and each_entity(). Deducing `Self` lets
    /// the same body serve `World&` and `const World&` -- the archetype's own
    /// const overloads then decide whether the columns come back mutable. `fn`
    /// always takes the entity first; the wrappers that do not want it drop it.
    template <class... Ts, class Self, class Fn> static void visit_matching(Self& self, Fn&& fn) {
        const Signature query = signature_of<Ts...>();
        for (auto& [sig, table] : self.m_tables) {
            if (!sig.contains(query))
                continue;
            auto& arch = table->archetype;
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
};

} // namespace vaxelis::ecs
