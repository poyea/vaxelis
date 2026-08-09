// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/ecs/Archetype.hpp"

#include <cassert>

namespace vaxelis::ecs {

namespace {

/// Rows to allocate for a first insertion; after that capacity doubles.
constexpr size_t kInitialCapacity = 8;

/// Address of one component inside a column.
std::byte* element_at(std::byte* base, uint32_t stride, size_t row) {
    return base + static_cast<size_t>(stride) * row;
}

} // namespace

Archetype::Archetype(std::span<const ComponentId> ids) {
    m_ids.reserve(ids.size());
    m_columns.reserve(ids.size());
    for (const ComponentId id : ids) {
        if (m_signature.test(id))
            continue; // duplicate
        const ComponentInfo& info = component_info(id);
        if (info.size == 0 || info.default_construct == nullptr)
            continue; // never registered; nothing sensible to store
        m_signature.set(id);
        m_ids.push_back(id);
        m_columns.push_back(Column{.id = id, .stride = info.size, .data = nullptr});
    }
}

Archetype::~Archetype() {
    for (size_t row = 0; row < m_rows; ++row)
        destroy_row(row);
    for (Column& col : m_columns) {
        delete[] col.data;
        col.data = nullptr;
    }
    m_rows = 0;
    m_capacity = 0;
}

Archetype::Column* Archetype::find(ComponentId id) {
    for (Column& col : m_columns) {
        if (col.id == id)
            return &col;
    }
    return nullptr;
}

const Archetype::Column* Archetype::find(ComponentId id) const {
    for (const Column& col : m_columns) {
        if (col.id == id)
            return &col;
    }
    return nullptr;
}

std::byte* Archetype::column_bytes(ComponentId id) {
    Column* col = find(id);
    return col ? col->data : nullptr;
}

const std::byte* Archetype::column_bytes(ComponentId id) const {
    const Column* col = find(id);
    return col ? col->data : nullptr;
}

void Archetype::destroy_row(size_t row) {
    for (Column& col : m_columns) {
        const ComponentInfo& info = component_info(col.id);
        info.destroy(element_at(col.data, col.stride, row));
    }
}

void Archetype::reserve(size_t rows) {
    if (rows <= m_capacity)
        return;

    for (Column& col : m_columns) {
        const ComponentInfo& info = component_info(col.id);
        auto* fresh = new std::byte[col.stride * rows];
        // Components are only required to be movable, not trivially copyable,
        // so relocation goes through move-construct + destroy rather than a
        // memcpy of the whole column.
        for (size_t row = 0; row < m_rows; ++row) {
            std::byte* to = element_at(fresh, col.stride, row);
            std::byte* from = element_at(col.data, col.stride, row);
            info.move_construct(to, from);
            info.destroy(from);
        }
        delete[] col.data;
        col.data = fresh;
    }
    m_capacity = rows;
}

size_t Archetype::add_row() {
    if (m_rows == m_capacity)
        reserve(m_capacity == 0 ? kInitialCapacity : m_capacity * 2);

    const size_t row = m_rows;
    build_row(row, nullptr, 0);
    ++m_rows;
    return row;
}

size_t Archetype::remove_row(size_t row) {
    // Returning `row` here would be indistinguishable from the legitimate
    // "the row was already last" result, and World::unseat would pop an
    // entity that is still seated.
    assert(row < m_rows && "Archetype::remove_row out of range");
    if (row >= m_rows)
        return row;

    const size_t last = m_rows - 1;
    destroy_row(row);
    if (row != last) {
        for (Column& col : m_columns) {
            const ComponentInfo& info = component_info(col.id);
            std::byte* to = element_at(col.data, col.stride, row);
            std::byte* from = element_at(col.data, col.stride, last);
            info.move_construct(to, from);
            info.destroy(from);
        }
    }
    m_rows = last;
    return last;
}

size_t Archetype::move_row_from(Archetype& src, size_t src_row) {
    // Falling back to a default row would look like success and silently lose
    // the caller's data, so this is a precondition rather than a recovery.
    assert(src_row < src.m_rows && "Archetype::move_row_from out of range");
    if (src_row >= src.m_rows)
        return add_row();

    if (m_rows == m_capacity)
        reserve(m_capacity == 0 ? kInitialCapacity : m_capacity * 2);

    const size_t row = m_rows;
    build_row(row, &src, src_row);
    ++m_rows;
    return row;
}

void Archetype::build_row(size_t row, Archetype* src, size_t src_row) {
    // m_rows is not bumped until the row is whole, so a throwing default
    // constructor would leave the columns already built holding live objects
    // that nothing ever destroys -- ~Archetype only walks rows below m_rows.
    // Unwind them by hand and let the exception through.
    size_t built = 0;
    try {
        for (Column& col : m_columns) {
            const ComponentInfo& info = component_info(col.id);
            std::byte* to = element_at(col.data, col.stride, row);
            // Shared components carry their value across a structural change;
            // anything only this archetype has starts out default-constructed.
            Column* from_col = src != nullptr ? src->find(col.id) : nullptr;
            if (from_col != nullptr) {
                info.move_construct(to, element_at(from_col->data, from_col->stride, src_row));
            } else {
                info.default_construct(to);
            }
            ++built;
        }
    } catch (...) {
        for (size_t i = 0; i < built; ++i) {
            const Column& done = m_columns[i];
            component_info(done.id).destroy(element_at(done.data, done.stride, row));
        }
        throw;
    }
}

} // namespace vaxelis::ecs
