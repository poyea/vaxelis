// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/ecs/World.hpp"

#include "engine/core/Log.hpp"

namespace vaxelis::ecs {

World::World() {
    // The empty archetype always exists: a freshly created entity lives there
    // until it is given its first component.
    m_empty = &table_for(Signature{}, {});
}

World::~World() = default;

void World::report_stale(const char* op) {
    VX_ERROR("ecs: World::{} called with a stale entity handle", op);
}

void World::report_unregistered(const char* op) {
    VX_ERROR("ecs: World::{} on a type that could not register; limit is {}", op, kMaxComponents);
}

World::Record* World::find(Entity e) {
    return alive(e) ? &m_records[e.index] : nullptr;
}

const World::Record* World::find(Entity e) const {
    return alive(e) ? &m_records[e.index] : nullptr;
}

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
    // Bail before touching `entities`: popping it while the archetype refused
    // the removal would desync the two permanently.
    if (row >= table.archetype.size())
        return;
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

void World::migrate(Entity e, Record& rec, Signature target) {
    Table& from = *rec.table;

    // The destination needs an id list, not just the mask: keep the source's
    // ids that survive, then pick up whichever id the change added.
    std::vector<ComponentId> ids;
    ids.reserve(from.archetype.component_ids().size() + 1);
    for (const ComponentId id : from.archetype.component_ids()) {
        if (target.test(id))
            ids.push_back(id);
    }
    for (ComponentId id = 0; id < registered_component_count(); ++id) {
        if (target.test(id) && !from.archetype.has(id))
            ids.push_back(id);
    }

    Table& to = table_for(target, ids);
    const size_t old_row = rec.row;
    rec.row = to.archetype.move_row_from(from.archetype, old_row);
    rec.table = &to;
    to.entities.push_back(e);

    // move_row_from deliberately leaves the source row behind; dropping it here
    // is what stops the entity from existing in two archetypes at once.
    unseat(from, old_row);
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
    // Generation 0 is reserved for kNoEntity, so a fresh slot starts at 1.
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

    // Outstanding handles now fail the generation check. On the four-billionth
    // reuse the counter wraps back to the generation this slot started on,
    // which would revive its very first handles, so retire the slot instead of
    // recycling it. Leaking one index is cheaper than resurrecting an entity.
    ++rec.generation;
    if (rec.generation == 0)
        rec.generation = 1;
    else
        m_free.push_back(e.index);

    --m_live;
}

} // namespace vaxelis::ecs
