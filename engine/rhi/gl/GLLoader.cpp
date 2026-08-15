// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/rhi/gl/GLLoader.hpp"

#include <SDL3/SDL_video.h>

#include "engine/core/Log.hpp"

namespace vaxelis::rhi::gl {

namespace {
GLApi g_api{};
bool g_loaded = false;

template <typename Fn> bool load_one(Fn& slot, const char* name) {
    auto ptr = SDL_GL_GetProcAddress(name);
    if (!ptr) {
        VX_ERROR("GL: failed to load {}", name);
        return false;
    }
    // Cast through a generic function-pointer type to silence MSVC C4191 and
    // avoid object-pointer ↔ function-pointer conversion.
    using generic_fn = void (*)();
    slot = reinterpret_cast<Fn>(reinterpret_cast<generic_fn>(ptr));
    return true;
}
} // namespace

const GLApi& gl() {
    return g_api;
}

bool load_gl() {
    if (g_loaded)
        return true;
    bool ok = true;
#define VX_LOAD(name) ok &= load_one(g_api.name, "gl" #name)
    VX_LOAD(Clear);
    VX_LOAD(ClearColor);
    VX_LOAD(Viewport);
    VX_LOAD(Enable);
    VX_LOAD(Disable);
    VX_LOAD(BlendFunc);
    VX_LOAD(PixelStorei);
    VX_LOAD(GenVertexArrays);
    VX_LOAD(BindVertexArray);
    VX_LOAD(DeleteVertexArrays);
    VX_LOAD(GenBuffers);
    VX_LOAD(BindBuffer);
    VX_LOAD(BufferData);
    VX_LOAD(BufferSubData);
    VX_LOAD(DeleteBuffers);
    VX_LOAD(GenTextures);
    VX_LOAD(BindTexture);
    VX_LOAD(TexImage2D);
    VX_LOAD(TexSubImage2D);
    VX_LOAD(TexParameteri);
    VX_LOAD(DeleteTextures);
    VX_LOAD(ActiveTexture);
    VX_LOAD(GenFramebuffers);
    VX_LOAD(BindFramebuffer);
    VX_LOAD(FramebufferTexture2D);
    VX_LOAD(CheckFramebufferStatus);
    VX_LOAD(DeleteFramebuffers);
    VX_LOAD(CreateShader);
    VX_LOAD(ShaderSource);
    VX_LOAD(CompileShader);
    VX_LOAD(GetShaderiv);
    VX_LOAD(GetShaderInfoLog);
    VX_LOAD(DeleteShader);
    VX_LOAD(CreateProgram);
    VX_LOAD(AttachShader);
    VX_LOAD(LinkProgram);
    VX_LOAD(GetProgramiv);
    VX_LOAD(GetProgramInfoLog);
    VX_LOAD(UseProgram);
    VX_LOAD(DeleteProgram);
    VX_LOAD(GetUniformLocation);
    VX_LOAD(UniformMatrix4fv);
    VX_LOAD(Uniform1i);
    VX_LOAD(VertexAttribPointer);
    VX_LOAD(EnableVertexAttribArray);
    VX_LOAD(DrawElements);
#undef VX_LOAD
    g_loaded = ok;
    return ok;
}

} // namespace vaxelis::rhi::gl
