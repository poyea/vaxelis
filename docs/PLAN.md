# Vaxelis — Implementation Plan

Vaxelis is a C++23 hybrid 2D/3D game engine. v1 ships a playable 2D platformer
demo on Windows; WASM is a post-v1 stretch.

## Stack
- C++23, CMake ≥3.28, git submodules under `third_party/`.
- SDL3 (window/input), glm, Dear ImGui (docking), spdlog, stb_image. Catch2 for tests.
- RHI: thin abstraction. OpenGL 4.5 core implemented; Vulkan stubbed.
- Scene tree (authoring) over ECS (entt) under the hood. *(deferred to M3)*
- Box2D (2D) + Jolt (3D) physics. *(deferred to M4)*
- Lua 5.4 + sol2 scripting. *(deferred to M4)*
- miniaudio for audio. *(deferred to M2)*

## Milestones
- **M0** — Project rename, engine layout, third_party submodules. ✅
- **M1** — Hello Sprite: window + GL 4.5 RHI + textured quad + ImGui overlay. ✅
- **M2** — Sprite batcher, fixed-timestep loop, input action map, audio. ✅
- **M3** — Scene tree + entt + JSON serialization + ImGui inspector. ✅
- **M4** — Box2D + Lua/sol2 + RigidBody2D component. ✅
- **M5** — Asset pipeline + hot reload (PNG, Lua, TMJ). ✅ (PNG+Lua; TMJ in M6)
- **M6** — Tiled (.tmj) level loading + camera.
- **M7** — Platformer demo: 3 levels, enemies, audio, win screen. **v1 done.**
- **M8** — WASM port (Emscripten + WebGL2 backend variant).

## RHI design
- Opaque integer handles (`TextureHandle { uint32_t id; }`). Device owns resources.
- `std::expected<T, RhiError>` for fallible creation; assert+log otherwise.
- Shaders embedded as `constexpr std::string_view`.
- Backend selected via `rhi::create_device(rhi::Backend::OpenGL)`.
