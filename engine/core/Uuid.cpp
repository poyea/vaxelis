#include "engine/core/Uuid.hpp"

#include <array>
#include <format>
#include <random>

namespace vaxelis {

namespace {

// Per-thread PRNG, seeded once from a non-deterministic source.
std::mt19937_64& rng() {
    thread_local std::mt19937_64 engine{std::random_device{}()};
    return engine;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

} // namespace

Uuid generate_uuid() {
    std::uniform_int_distribution<uint64_t> dist;
    Uuid u{dist(rng()), dist(rng())};
    // RFC 4122 version 4 + variant bits, mostly for diagnosability of the text
    // form. Uniqueness comes from the 122 random bits, not these markers.
    u.hi = (u.hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
    u.lo = (u.lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
    return u;
}

std::string to_string(const Uuid& u) {
    return std::format("{:08x}-{:04x}-{:04x}-{:04x}-{:012x}", u.hi >> 32, (u.hi >> 16) & 0xFFFF,
                       u.hi & 0xFFFF, (u.lo >> 48) & 0xFFFF, u.lo & 0xFFFFFFFFFFFFull);
}

Uuid uuid_from_string(std::string_view s) {
    std::array<int, 32> nibbles{};
    size_t n = 0;
    for (char c : s) {
        if (c == '-')
            continue;
        if (n >= nibbles.size())
            return {}; // too many digits
        int v = hex_value(c);
        if (v < 0)
            return {}; // non-hex character
        nibbles[n++] = v;
    }
    if (n != nibbles.size())
        return {}; // too few digits

    Uuid u{};
    for (size_t i = 0; i < 16; ++i)
        u.hi = (u.hi << 4) | static_cast<uint64_t>(nibbles[i]);
    for (size_t i = 16; i < 32; ++i)
        u.lo = (u.lo << 4) | static_cast<uint64_t>(nibbles[i]);
    return u;
}

} // namespace vaxelis
