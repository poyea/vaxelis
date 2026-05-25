#pragma once

#include <string>
#include <string_view>

namespace cpp_package_boilerplate {

[[nodiscard]] constexpr int add(int left, int right) noexcept {
    return left + right;
}

[[nodiscard]] std::string greet(std::string_view name);

} // namespace cpp_package_boilerplate

#ifdef CPP_PACKAGE_BOILERPLATE_HEADER_ONLY
namespace cpp_package_boilerplate {

inline std::string greet(std::string_view name) {
    return "hello, " + std::string(name) + " from cpp-package-boilerplate";
}

} // namespace cpp_package_boilerplate
#endif
