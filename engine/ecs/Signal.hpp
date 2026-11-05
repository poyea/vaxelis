// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

/// @file
/// Component construct/destroy notification for the archetype world.
///
/// Deliberately the same shape as entt's signal/sink pair, because the engine's
/// subsystems are already written against it: a port that changed the
/// subscription style would rewrite Physics2D and ScriptHost for no reason.

#include <cstddef>
#include <vector>

#include "engine/ecs/Entity.hpp"

namespace vaxelis::ecs {

class World;

/// A bound callback: an instance plus a stamped-out function that calls the
/// member through it. Trivially copyable and allocation-free, so a signal with
/// listeners costs one indirect call per structural change, not a heap visit.
class Listener {
  public:
    using Invoke = void (*)(void*, World&, Entity);

    Listener() = default;
    Listener(Invoke invoke, void* instance) : m_invoke(invoke), m_instance(instance) {}

    void operator()(World& world, Entity e) const { m_invoke(m_instance, world, e); }

    /// The subscriber this listener calls into. Used to disconnect by instance.
    const void* instance() const { return m_instance; }
    /// The stamped-out trampoline, which identifies *which* member is bound.
    Invoke target() const { return m_invoke; }

  private:
    Invoke m_invoke{nullptr};
    void* m_instance{nullptr};
};

/// The listener list behind one signal. Owned by World; subscribers reach it
/// through a Sink.
class Signal {
  public:
    /// Calls every listener in subscription order.
    ///
    /// Indexed rather than iterated so a listener that subscribes during the
    /// call cannot invalidate the walk. Disconnecting a *different* listener
    /// from inside one shifts the remainder and may skip an entry; don't.
    void publish(World& world, Entity e) {
        for (size_t i = 0; i < m_listeners.size(); ++i)
            m_listeners[i](world, e);
    }

    /// True when nothing is subscribed, which is the common case and the one
    /// worth keeping branch-cheap.
    bool empty() const { return m_listeners.empty(); }

    void add(Listener listener) { m_listeners.push_back(listener); }

    /// Drops the one listener bound to `instance` through `target`.
    void remove(Listener::Invoke target, const void* instance) {
        for (size_t i = 0; i < m_listeners.size(); ++i) {
            if (m_listeners[i].target() == target && m_listeners[i].instance() == instance) {
                m_listeners.erase(m_listeners.begin() + static_cast<ptrdiff_t>(i));
                return;
            }
        }
    }

    /// Drops every listener bound to `instance`, whichever member each calls.
    void remove_all(const void* instance) {
        for (size_t i = m_listeners.size(); i > 0; --i) {
            if (m_listeners[i - 1].instance() == instance)
                m_listeners.erase(m_listeners.begin() + static_cast<ptrdiff_t>(i - 1));
        }
    }

  private:
    std::vector<Listener> m_listeners;
};

/// Subscription face for a Signal. Mirrors entt::sink, so a call site reads
/// identically whichever storage it is talking to.
///
/// @code
///   world.on_destroy<RigidBody2D>().connect<&Physics2D::on_rb_destroyed>(*this);
/// @endcode
class Sink {
  public:
    explicit Sink(Signal& signal) : m_signal(&signal) {}

    /// Subscribes `Candidate`, a member function taking (World&, Entity),
    /// called on `instance`. Connecting the same pair twice subscribes twice.
    template <auto Candidate, class Instance> void connect(Instance& instance) {
        m_signal->add(Listener{trampoline<Candidate, Instance>(), &instance});
    }

    /// Removes the subscription `connect<Candidate>(instance)` made.
    template <auto Candidate, class Instance> void disconnect(Instance& instance) {
        m_signal->remove(trampoline<Candidate, Instance>(), &instance);
    }

    /// Removes every subscription naming `instance`. The overload a subscriber
    /// uses in its destructor, or when it has been moved.
    void disconnect(const void* instance) { m_signal->remove_all(instance); }

  private:
    /// The function the listener actually stores: one per (member, type) pair,
    /// so comparing it identifies the bound member.
    template <auto Candidate, class Instance> static Listener::Invoke trampoline() {
        return +[](void* payload, World& world, Entity e) {
            (static_cast<Instance*>(payload)->*Candidate)(world, e);
        };
    }

    Signal* m_signal;
};

} // namespace vaxelis::ecs
