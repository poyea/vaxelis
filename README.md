# vaxelis [![ci](https://github.com/poyea/vaxelis/actions/workflows/ci.yml/badge.svg)](https://github.com/poyea/vaxelis/actions/workflows/ci.yml)

A C++23 hybrid, cross-platform 2D/3D game engine.

## Stack

SDL3 · OpenGL 4.5 · glm · Dear ImGui · spdlog · stb · miniaudio · entt · nlohmann/json · Box2D · Lua 5.4 · sol2

## Build

```bash
git clone --recursive https://github.com/poyea/vaxelis.git
cd vaxelis
cmake -S . -B build -G Ninja
cmake --build build
./build/runtime/vaxelis_runtime
```

If you forgot `--recursive`:

```bash
git submodule update --init --recursive
```

**Toolchain:** GCC 13+, Clang 18+, AppleClang 16+, or MSVC 19.38+ (VS 2022 17.8). CMake 3.28+, Ninja.

**Linux deps:** `libasound2-dev libpulse-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxkbcommon-dev libwayland-dev libgl-dev libdbus-1-dev libudev-dev`

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Layout

```
engine/    # the engine library (RHI, scene, physics, scripting, …)
runtime/   # platformer demo + assets
tests/     # Catch2 unit tests
```
