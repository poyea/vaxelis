#pragma once

/// @file
/// The archetype world: owns entities, the archetypes they live in, and the
/// mapping between the two.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
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
class World {
  public:
    World();
    ~World();

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

    std::unordered_map<Signature, std::unique_ptr<Table>> m_tables;
    std::vector<Record> m_records;
    std::vector<uint32_t> m_free;
    Table* m_empty{nullptr};
    size_t m_live{0};
};

} // namespace vaxelis::ecs
