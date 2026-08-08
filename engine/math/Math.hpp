#pragma once

/// @file
/// Y-up, right-handed. Forward-declared wrapper around glm for engine code.

#define GLM_FORCE_RADIANS
#define GLM_ENABLE_EXPERIMENTAL
// NOTE: NDC z is [-1, 1] (GL default). When Vulkan backend lands, set
// GLM_FORCE_DEPTH_ZERO_TO_ONE here and use glClipControl on the GL side.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace vaxelis {

/// 2D vector. Doubles as a screen/world position, a size, or a UV pair.
using vec2 = glm::vec2;
/// 3D vector.
using vec3 = glm::vec3;
/// 4D vector. Also used for RGBA colours and for (min_u, min_v, max_u, max_v)
/// uv rectangles.
using vec4 = glm::vec4;
/// 4x4 matrix, used for transforms and projections even in the 2D paths.
using mat4 = glm::mat4;

/// Axis-aligned 2D bounding box.
struct AABB2 {
    vec2 min;
    vec2 max;
};

} // namespace vaxelis
