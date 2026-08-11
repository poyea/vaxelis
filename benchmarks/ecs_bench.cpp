// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

/// @file
/// Headless ECS benchmark: the same workloads over entt's sparse sets and over
/// the archetype world, with no window, device or audio, so CI can run it.
///
/// Reports the best of several repetitions, not the mean: on a shared runner
/// the mean mostly measures the neighbours. A ratio below 1.00 means the
/// archetype path was faster.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include <entt/entt.hpp>

#include "engine/ecs/World.hpp"

namespace {

using SteadyClock = std::chrono::steady_clock;

/// Entity counts to sweep, so a crossover shows up rather than one data point.
constexpr int kCounts[] = {1000, 20000, 200000};
constexpr int kReps = 5;

struct Position {
    float x{0.0f};
    float y{0.0f};
};

struct Velocity {
    float dx{1.0f};
    float dy{1.0f};
};

struct Health {
    int hp{100};
};

/// Accumulated so the optimiser cannot delete the loops it is timing.
double g_sink = 0.0;

float ms_since(SteadyClock::time_point t0) {
    return std::chrono::duration<float, std::milli>(SteadyClock::now() - t0).count();
}

/// Best wall time over `kReps` runs of `fn`, in milliseconds.
template <class Fn> double best_of(Fn&& fn) {
    double best = 1e30;
    for (int rep = 0; rep < kReps; ++rep) {
        const auto t0 = SteadyClock::now();
        fn();
        best = std::min(best, static_cast<double>(ms_since(t0)));
    }
    return best;
}

void row(const char* workload, int count, double entt_ms, double arch_ms) {
    const double ratio = entt_ms > 0.0 ? arch_ms / entt_ms : 0.0;
    std::printf("| %-22s | %7d | %9.3f | %9.3f | %6.2fx |\n", workload, count, entt_ms, arch_ms,
                ratio);
}

// --- workloads ------------------------------------------------------------
// Each pair does identical arithmetic on identical data; only the storage
// underneath differs.

void bench_sweep_one(int count) {
    entt::registry reg;
    for (int i = 0; i < count; ++i)
        reg.emplace<Position>(reg.create());

    vaxelis::ecs::World world;
    for (int i = 0; i < count; ++i)
        world.add<Position>(world.create());

    const double entt_ms = best_of([&] {
        reg.view<Position>().each([](Position& p) { p.x += 1.0f; });
    });
    const double arch_ms = best_of([&] {
        world.each<Position>([](Position& p) { p.x += 1.0f; });
    });

    reg.view<Position>().each([](const Position& p) { g_sink += p.x; });
    row("sweep 1 component", count, entt_ms, arch_ms);
}

void bench_sweep_two(int count) {
    entt::registry reg;
    for (int i = 0; i < count; ++i) {
        const auto e = reg.create();
        reg.emplace<Position>(e);
        reg.emplace<Velocity>(e);
    }

    vaxelis::ecs::World world;
    for (int i = 0; i < count; ++i) {
        const auto e = world.create();
        world.add<Position>(e);
        world.add<Velocity>(e);
    }

    const double entt_ms = best_of([&] {
        reg.view<Position, Velocity>().each([](Position& p, Velocity& v) {
            p.x += v.dx;
            p.y += v.dy;
        });
    });
    const double arch_ms = best_of([&] {
        world.each<Position, Velocity>([](Position& p, Velocity& v) {
            p.x += v.dx;
            p.y += v.dy;
        });
    });

    reg.view<Position>().each([](const Position& p) { g_sink += p.x; });
    row("sweep 2 components", count, entt_ms, arch_ms);
}

/// Construction, which is where archetypes are expected to lose: every add()
/// migrates the row to a new shape.
void bench_create(int count) {
    const double entt_ms = best_of([&] {
        entt::registry reg;
        for (int i = 0; i < count; ++i) {
            const auto e = reg.create();
            reg.emplace<Position>(e);
            reg.emplace<Velocity>(e);
        }
        g_sink += static_cast<double>(reg.storage<Position>().size());
    });
    const double arch_ms = best_of([&] {
        vaxelis::ecs::World world;
        for (int i = 0; i < count; ++i) {
            const auto e = world.create();
            world.add<Position>(e);
            world.add<Velocity>(e);
        }
        g_sink += static_cast<double>(world.size());
    });
    row("create + 2 components", count, entt_ms, arch_ms);
}

/// Structural churn: the archetype worst case, one migration per operation.
void bench_add_remove(int count) {
    entt::registry reg;
    std::vector<entt::entity> ents;
    ents.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const auto e = reg.create();
        reg.emplace<Position>(e);
        ents.push_back(e);
    }

    vaxelis::ecs::World world;
    std::vector<vaxelis::ecs::Entity> handles;
    handles.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const auto e = world.create();
        world.add<Position>(e);
        handles.push_back(e);
    }

    const double entt_ms = best_of([&] {
        for (const auto e : ents)
            reg.emplace<Health>(e);
        for (const auto e : ents)
            reg.erase<Health>(e);
    });
    const double arch_ms = best_of([&] {
        for (const auto e : handles)
            world.add<Health>(e);
        for (const auto e : handles)
            world.remove<Health>(e);
    });
    row("add + remove component", count, entt_ms, arch_ms);
}

} // namespace

int main() {
    std::printf("| workload               |   count |   entt ms |   arch ms |  ratio |\n");
    std::printf("| ---------------------- | ------: | --------: | --------: | -----: |\n");
    for (const int count : kCounts) {
        bench_sweep_one(count);
        bench_sweep_two(count);
        bench_create(count);
        bench_add_remove(count);
    }
    // Printed so the accumulator is observably used; the value itself is noise.
    std::printf("\nchecksum %.0f (ignore; keeps the loops from being optimised away)\n", g_sink);
    return 0;
}
