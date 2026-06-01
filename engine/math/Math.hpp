#pragma once

// Y-up, right-handed. Forward-declared wrapper around glm for engine code.

#define GLM_FORCE_RADIANS
#define GLM_ENABLE_EXPERIMENTAL
// NOTE: NDC z is [-1, 1] (GL default). When Vulkan backend lands, set
// GLM_FORCE_DEPTH_ZERO_TO_ONE here and use glClipControl on the GL side.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace vaxelis {

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat4 = glm::mat4;

struct AABB2 {
    vec2 min;
    vec2 max;
};

} // namespace vaxelis
