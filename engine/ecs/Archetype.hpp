// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

/// @file
/// Column-major storage for the entities sharing one component signature.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/ecs/ComponentType.hpp"
#include "engine/ecs/Signature.hpp"

namespace vaxelis::ecs {

/// Storage for every entity carrying exactly one signature, laid out one
/// contiguous array per component type. A query matching this archetype walks
/// those arrays in memory order with no per-entity indirection, which is the
/// whole reason to arrange data this way rather than as sparse sets.
///
/// Rows are addressed by index. Removing a row moves the last row into the gap
/// (swap and pop) so removal is O(1) and columns never develop holes; the
/// caller owns whatever maps an entity to its row and must patch it up using
/// the index remove_row() reports.
///
/// Chunking -- splitting each column into fixed-size blocks -- is deliberately
/// not here yet. It changes the allocation strategy behind these methods, not
/// the methods themselves.
class Archetype {
  public:
    /// Builds empty columns for `ids`; duplicates are ignored. The signature is
    /// derived from the ids given.
    explicit Archetype(std::span<const ComponentId> ids);
    /// Destroys every live row, then releases the columns.
    ~Archetype();

    /// Not copyable: the columns are raw allocations this object owns.
    Archetype(const Archetype&) = delete;
    Archetype& operator=(const Archetype&) = delete;

    /// The component set every row here carries.
    Signature signature() const { return m_signature; }
    /// Number of live rows.
    size_t size() const { return m_rows; }
    /// True when no rows are stored.
    bool empty() const { return m_rows == 0; }
    /// Rows that fit before the next reallocation.
    size_t capacity() const { return m_capacity; }
    /// True when this archetype stores `id`.
    bool has(ComponentId id) const { return m_signature.test(id); }

    /// Appends a row with every component default-constructed.
    /// @return the new row's index.
    size_t add_row();

    /// Destroys `row`, then fills the gap by moving the last row into it.
    /// @return the index the moved row came from, which equals the new size().
    /// When `row` was already last nothing moved and the result is `row`.
    size_t remove_row(size_t row);

    /// Appends a row built from `src`'s row `src_row`: components both
    /// archetypes hold are moved across, the rest are default-constructed.
    /// `src`'s row is left moved-from and is *not* removed, so the caller can
    /// decide when to drop it.
    /// @return the new row's index in this archetype.
    size_t move_row_from(Archetype& src, size_t src_row);

    /// Grows capacity to at least `rows`, relocating live rows if it must.
    void reserve(size_t rows);

    /// Base address of the column for `id`, or nullptr when absent.
    std::byte* column_bytes(ComponentId id);
    const std::byte* column_bytes(ComponentId id) const;

    /// Typed view over one column, covering the live rows. Empty when `T` is
    /// not stored here.
    template <class T> std::span<T> column() {
        std::byte* base = column_bytes(component_id<T>());
        return base ? std::span<T>(reinterpret_cast<T*>(base), m_rows) : std::span<T>{};
    }

    /// Read-only view over one column. See column().
    template <class T> std::span<const T> column() const {
        const std::byte* base = column_bytes(component_id<T>());
        return base ? std::span<const T>(reinterpret_cast<const T*>(base), m_rows)
                    : std::span<const T>{};
    }

    /// One component of one row. The row must exist and hold `T`.
    template <class T> T& get(size_t row) {
        const std::span<T> col = column<T>();
        assert(row < col.size() && "Archetype::get on a missing column or row");
        return col[row];
    }

    /// Read-only access to one component of one row. See get().
    template <class T> const T& get(size_t row) const {
        const std::span<const T> col = column<T>();
        assert(row < col.size() && "Archetype::get on a missing column or row");
        return col[row];
    }

    /// Component ids stored here, in registration order.
    std::span<const ComponentId> component_ids() const { return m_ids; }

  private:
    /// One component type's array. `stride` is cached from ComponentInfo so the
    /// hot paths do not chase the metadata table.
    struct Column {
        ComponentId id{0};
        uint32_t stride{0};
        std::byte* data{nullptr};
    };

    Column* find(ComponentId id);
    const Column* find(ComponentId id) const;
    void destroy_row(size_t row);

    /// Fills every column of `row`, moving from `src`'s `src_row` where the two
    /// share a component and default-constructing the rest. `src` may be null
    /// to default-construct throughout. Strongly exception safe: if a component
    /// constructor throws, the columns already built are unwound.
    void build_row(size_t row, Archetype* src, size_t src_row);

    std::vector<ComponentId> m_ids;
    std::vector<Column> m_columns;
    Signature m_signature;
    size_t m_rows{0};
    size_t m_capacity{0};
};

} // namespace vaxelis::ecs
