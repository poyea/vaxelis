// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

// Minimal GL 4.5 core loader. Declares only the entry points used by the
// engine; loaded at GLDevice construction via SDL_GL_GetProcAddress.
//
// We intentionally do NOT pull in <GL/gl.h> on Windows because it only
// exposes GL 1.1. All types and tokens we need are redeclared here.

#include <cstddef>
#include <cstdint>

namespace vaxelis::rhi::gl {

// --- GL types (subset) ---
using GLenum = unsigned int;
using GLbitfield = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;
using GLboolean = unsigned char;
using GLfloat = float;
using GLchar = char;
using GLubyte = unsigned char;
using GLvoid = void;

// --- Tokens (subset) ---
inline constexpr GLenum GL_FALSE = 0;
inline constexpr GLenum GL_TRUE = 1;
inline constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;
inline constexpr GLenum GL_DEPTH_BUFFER_BIT = 0x00000100;
inline constexpr GLenum GL_TRIANGLES = 0x0004;
inline constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
inline constexpr GLenum GL_UNSIGNED_SHORT = 0x1403;
inline constexpr GLenum GL_FLOAT = 0x1406;
inline constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
inline constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
inline constexpr GLenum GL_STATIC_DRAW = 0x88E4;
inline constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;
inline constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
inline constexpr GLenum GL_TEXTURE0 = 0x84C0;
inline constexpr GLenum GL_RGBA = 0x1908;
inline constexpr GLenum GL_RGBA8 = 0x8058;
inline constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
inline constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
inline constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
inline constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
inline constexpr GLenum GL_NEAREST = 0x2600;
inline constexpr GLenum GL_LINEAR = 0x2601;
inline constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
inline constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
inline constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
inline constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
inline constexpr GLenum GL_LINK_STATUS = 0x8B82;
inline constexpr GLenum GL_INFO_LOG_LENGTH = 0x8B84;
inline constexpr GLenum GL_BLEND = 0x0BE2;
inline constexpr GLenum GL_SRC_ALPHA = 0x0302;
inline constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
inline constexpr GLenum GL_DEPTH_TEST = 0x0B71;
inline constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;

// --- Function pointer table ---
struct GLApi {
    void (*Clear)(GLbitfield);
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*Enable)(GLenum);
    void (*Disable)(GLenum);
    void (*BlendFunc)(GLenum, GLenum);
    void (*PixelStorei)(GLenum, GLint);

    void (*GenVertexArrays)(GLsizei, GLuint*);
    void (*BindVertexArray)(GLuint);
    void (*DeleteVertexArrays)(GLsizei, const GLuint*);

    void (*GenBuffers)(GLsizei, GLuint*);
    void (*BindBuffer)(GLenum, GLuint);
    void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
    void (*BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
    void (*DeleteBuffers)(GLsizei, const GLuint*);

    void (*GenTextures)(GLsizei, GLuint*);
    void (*BindTexture)(GLenum, GLuint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                          const void*);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*DeleteTextures)(GLsizei, const GLuint*);
    void (*ActiveTexture)(GLenum);

    GLuint (*CreateShader)(GLenum);
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void (*CompileShader)(GLuint);
    void (*GetShaderiv)(GLuint, GLenum, GLint*);
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void (*DeleteShader)(GLuint);

    GLuint (*CreateProgram)();
    void (*AttachShader)(GLuint, GLuint);
    void (*LinkProgram)(GLuint);
    void (*GetProgramiv)(GLuint, GLenum, GLint*);
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void (*UseProgram)(GLuint);
    void (*DeleteProgram)(GLuint);

    GLint (*GetUniformLocation)(GLuint, const GLchar*);
    void (*UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*Uniform1i)(GLint, GLint);

    void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    void (*EnableVertexAttribArray)(GLuint);

    void (*DrawElements)(GLenum, GLsizei, GLenum, const void*);
};

// Loaded singleton. Call load_gl() once after a current GL context exists.
const GLApi& gl();

// Returns true on success. Logs and returns false if any required entry is missing.
bool load_gl();

} // namespace vaxelis::rhi::gl
