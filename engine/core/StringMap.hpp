#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace vaxelis {

// Transparent hash for string-keyed containers: heterogeneous lookup lets
// find()/contains() take a string_view (or char*) without allocating a
// temporary std::string key.
struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

template <class T>
using StringMap = std::unordered_map<std::string, T, StringHash, std::equal_to<>>;

} // namespace vaxelis
