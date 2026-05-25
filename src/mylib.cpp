#include "cpp_package_boilerplate/package.hpp"

#include <string>

namespace cpp_package_boilerplate {

std::string greet(std::string_view name) {
    return "hello, " + std::string(name) + " from cpp-package-boilerplate";
}

} // namespace cpp_package_boilerplate
