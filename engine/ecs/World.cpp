#include "engine/ecs/World.hpp"

namespace vaxelis::ecs {

World::World() {
    // The empty archetype always exists: a freshly created entity lives there
    // until it is given its first component.
    m_empty = &table_for(Signature{}, {});
}

World::~World() = default;

World::Table& World::table_for(Signature sig, std::span<const ComponentId> ids) {
    if (auto it = m_tables.find(sig); it != m_tables.end())
        return *it->second;
    auto [it, _] = m_tables.emplace(sig, std::make_unique<Table>(ids));
    return *it->second;
}

void World::seat(Entity e, Table& table) {
    Record& rec = m_records[e.index];
    rec.table = &table;
    rec.row = table.archetype.add_row();
    table.entities.push_back(e);
}

void World::unseat(Table& table, size_t row) {
    const size_t vacated = table.archetype.remove_row(row);
    if (vacated != row) {
        // remove_row moved the last row into the gap, so the entity that owned
        // it now lives at `row` and its record has to say so.
        const Entity moved = table.entities[vacated];
        table.entities[row] = moved;
        m_records[moved.index].row = row;
    }
    table.entities.pop_back();
}

Entity World::create() {
    uint32_t index = 0;
    if (!m_free.empty()) {
        index = m_free.back();
        m_free.pop_back();
    } else {
        index = static_cast<uint32_t>(m_records.size());
        m_records.emplace_back();
    }

    Record& rec = m_records[index];
    // Generation 0 is reserved for kNoEntity, so a recycled slot skips it.
    if (rec.generation == 0)
        rec.generation = 1;
    rec.alive = true;

    const Entity e{.index = index, .generation = rec.generation};
    seat(e, *m_empty);
    ++m_live;
    return e;
}

bool World::alive(Entity e) const {
    if (e.generation == 0 || e.index >= m_records.size())
        return false;
    const Record& rec = m_records[e.index];
    return rec.alive && rec.generation == e.generation;
}

void World::destroy(Entity e) {
    if (!alive(e))
        return;

    Record& rec = m_records[e.index];
    unseat(*rec.table, rec.row);
    rec.table = nullptr;
    rec.row = 0;
    rec.alive = false;
    // Outstanding handles to this entity now fail the generation check. The
    // counter wrapping would take 4 billion reuses of one slot.
    ++rec.generation;
    if (rec.generation == 0)
        rec.generation = 1;

    m_free.push_back(e.index);
    --m_live;
}

} // namespace vaxelis::ecs
