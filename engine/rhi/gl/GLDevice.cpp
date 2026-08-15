// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/rhi/gl/GLDevice.hpp"

#include <cassert>
#include <unordered_map>
#include <vector>

#include "engine/core/Log.hpp"
#include "engine/rhi/gl/GLLoader.hpp"

namespace vaxelis::rhi::gl {

namespace {

struct GLTexture {
    GLuint name;
    uint32_t width;
    uint32_t height;
};
struct GLShader {
    GLuint program;
    GLint mvp_loc;
    GLint tex_loc;
};
struct GLRenderTarget {
    GLuint fbo;
    TextureHandle color;
    uint32_t width;
    uint32_t height;
};
struct GLBuffer {
    GLuint name;
    GLenum target;
    GLenum usage;
    size_t size;
};

class GLDevice final : public IDevice {
  public:
    GLDevice() {
        gl().GenVertexArrays(1, &m_vao);
        gl().BindVertexArray(m_vao);
        VX_INFO("GLDevice: created (VAO={})", m_vao);
    }

    ~GLDevice() override {
        // Tear down any handles the user forgot to destroy, but assert in debug
        // so leaks are noisy.
        assert(m_textures.empty() && "GLDevice: texture handles leaked");
        assert(m_shaders.empty() && "GLDevice: shader handles leaked");
        assert(m_buffers.empty() && "GLDevice: buffer handles leaked");
        assert(m_targets.empty() && "GLDevice: render target handles leaked");
        for (auto& [_, t] : m_textures)
            gl().DeleteTextures(1, &t.name);
        for (auto& [_, s] : m_shaders)
            gl().DeleteProgram(s.program);
        for (auto& [_, b] : m_buffers)
            gl().DeleteBuffers(1, &b.name);
        for (auto& [_, r] : m_targets)
            gl().DeleteFramebuffers(1, &r.fbo);
        gl().DeleteVertexArrays(1, &m_vao);
        VX_INFO("GLDevice: destroyed");
    }

    vaxelis::expected<TextureHandle, RhiError> create_texture(const TextureDesc& d) override {
        if (d.format != TextureFormat::RGBA8)
            return vaxelis::unexpected(RhiError::UnsupportedFormat);
        GLuint name = 0;
        gl().GenTextures(1, &name);
        gl().BindTexture(GL_TEXTURE_2D, name);
        gl().PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl().TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8),
                        static_cast<GLsizei>(d.width), static_cast<GLsizei>(d.height), 0, GL_RGBA,
                        GL_UNSIGNED_BYTE, d.initial_data);
        gl().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        TextureHandle h{m_next_handle++};
        m_textures.emplace(h.id, GLTexture{name, d.width, d.height});
        return h;
    }

    vaxelis::expected<ShaderHandle, RhiError> create_shader(const ShaderDesc& d) override {
        auto compile = [](GLenum stage,
                          std::string_view src) -> vaxelis::expected<GLuint, RhiError> {
            GLuint s = gl().CreateShader(stage);
            const GLchar* str = src.data();
            const GLint len = static_cast<GLint>(src.size());
            gl().ShaderSource(s, 1, &str, &len);
            gl().CompileShader(s);
            GLint ok = 0;
            gl().GetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                GLint log_len = 0;
                gl().GetShaderiv(s, GL_INFO_LOG_LENGTH, &log_len);
                std::vector<char> log(static_cast<size_t>(log_len) + 1, 0);
                gl().GetShaderInfoLog(s, log_len, nullptr, log.data());
                VX_ERROR("GL shader compile failed: {}", log.data());
                gl().DeleteShader(s);
                return vaxelis::unexpected(RhiError::ShaderCompileFailed);
            }
            return s;
        };

        // Monadic chain: a compile failure short-circuits and propagates its
        // error; transform_error releases the vertex stage when the fragment
        // stage fails mid-chain.
        return compile(GL_VERTEX_SHADER, d.vertex_src).and_then([&](GLuint vs) {
            return compile(GL_FRAGMENT_SHADER, d.fragment_src)
                .transform_error([&](RhiError e) {
                    gl().DeleteShader(vs);
                    return e;
                })
                .and_then([&](GLuint fs) -> vaxelis::expected<ShaderHandle, RhiError> {
                    GLuint prog = gl().CreateProgram();
                    gl().AttachShader(prog, vs);
                    gl().AttachShader(prog, fs);
                    gl().LinkProgram(prog);
                    gl().DeleteShader(vs);
                    gl().DeleteShader(fs);
                    GLint ok = 0;
                    gl().GetProgramiv(prog, GL_LINK_STATUS, &ok);
                    if (!ok) {
                        GLint log_len = 0;
                        gl().GetProgramiv(prog, GL_INFO_LOG_LENGTH, &log_len);
                        std::vector<char> log(static_cast<size_t>(log_len) + 1, 0);
                        gl().GetProgramInfoLog(prog, log_len, nullptr, log.data());
                        VX_ERROR("GL program link failed: {}", log.data());
                        gl().DeleteProgram(prog);
                        return vaxelis::unexpected(RhiError::ProgramLinkFailed);
                    }
                    ShaderHandle h{m_next_handle++};
                    m_shaders.emplace(h.id, GLShader{prog, gl().GetUniformLocation(prog, "u_mvp"),
                                                     gl().GetUniformLocation(prog, "u_tex")});
                    return h;
                });
        });
    }

    vaxelis::expected<BufferHandle, RhiError> create_buffer(const BufferDesc& d) override {
        GLenum target = (d.usage == BufferUsage::Index) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
        GLenum usage = (d.access == BufferAccess::Dynamic) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
        GLuint name = 0;
        gl().GenBuffers(1, &name);
        gl().BindBuffer(target, name);
        gl().BufferData(target, static_cast<GLsizeiptr>(d.size_bytes), d.initial_data, usage);
        BufferHandle h{m_next_handle++};
        m_buffers.emplace(h.id, GLBuffer{name, target, usage, d.size_bytes});
        return h;
    }

    vaxelis::expected<RenderTargetHandle, RhiError>
    create_render_target(const RenderTargetDesc& d) override {
        if (d.format != TextureFormat::RGBA8)
            return vaxelis::unexpected(RhiError::UnsupportedFormat);
        if (d.width == 0 || d.height == 0)
            return vaxelis::unexpected(RhiError::OutOfMemory);

        // The colour attachment is an ordinary texture, so everything that can
        // sample a texture can sample a target without special-casing it.
        auto color = create_texture({.width = d.width, .height = d.height, .format = d.format});
        if (!color)
            return vaxelis::unexpected(color.error());

        GLuint fbo = 0;
        gl().GenFramebuffers(1, &fbo);
        gl().BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl().FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                  m_textures.at(color->id).name, 0);
        const GLenum status = gl().CheckFramebufferStatus(GL_FRAMEBUFFER);
        // Put back whatever was bound before, rather than assuming the
        // backbuffer: creating a target during a pass must not silently
        // redirect the rest of that pass.
        restore_binding();
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            VX_ERROR("GLDevice: framebuffer incomplete (status 0x{:X})", status);
            gl().DeleteFramebuffers(1, &fbo);
            destroy(*color);
            return vaxelis::unexpected(RhiError::UnsupportedFormat);
        }

        RenderTargetHandle h{m_next_handle++};
        m_targets.emplace(h.id, GLRenderTarget{fbo, *color, d.width, d.height});
        return h;
    }

    void destroy(RenderTargetHandle h) override {
        auto it = m_targets.find(h.id);
        if (it == m_targets.end())
            return;
        // Unbind first: deleting the bound framebuffer would leave the GL
        // context pointing at a deleted object.
        if (m_bound_target.id == h.id)
            set_render_target({}, vec4{0.0f});
        gl().DeleteFramebuffers(1, &it->second.fbo);
        const TextureHandle color = it->second.color;
        m_targets.erase(it);
        destroy(color);
    }

    TextureHandle render_target_texture(RenderTargetHandle h) const override {
        const auto it = m_targets.find(h.id);
        return it != m_targets.end() ? it->second.color : TextureHandle{};
    }

    /// Rebinds whatever set_render_target last selected, without clearing it.
    /// Used after temporarily binding a framebuffer for setup.
    void restore_binding() {
        const auto it = m_targets.find(m_bound_target.id);
        gl().BindFramebuffer(GL_FRAMEBUFFER, it != m_targets.end() ? it->second.fbo : 0);
    }

    void set_render_target(RenderTargetHandle target, vec4 clear_color) override {
        if (!target.valid()) {
            gl().BindFramebuffer(GL_FRAMEBUFFER, 0);
            // Before the first begin_frame the backbuffer size is unknown; a
            // 0x0 viewport would silently discard everything drawn afterwards,
            // so leave whatever GL already had.
            if (m_fb_width > 0 && m_fb_height > 0) {
                gl().Viewport(0, 0, static_cast<GLsizei>(m_fb_width),
                              static_cast<GLsizei>(m_fb_height));
            }
            m_bound_target = {};
            return;
        }

        const auto it = m_targets.find(target.id);
        if (it == m_targets.end()) {
            VX_ERROR("GLDevice: set_render_target on an unknown handle");
            return;
        }
        gl().BindFramebuffer(GL_FRAMEBUFFER, it->second.fbo);
        gl().Viewport(0, 0, static_cast<GLsizei>(it->second.width),
                      static_cast<GLsizei>(it->second.height));
        gl().ClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
        gl().Clear(GL_COLOR_BUFFER_BIT);
        m_bound_target = target;
    }

    void destroy(TextureHandle h) override {
        auto it = m_textures.find(h.id);
        if (it == m_textures.end())
            return;
        gl().DeleteTextures(1, &it->second.name);
        m_textures.erase(it);
    }
    void destroy(ShaderHandle h) override {
        auto it = m_shaders.find(h.id);
        if (it == m_shaders.end())
            return;
        gl().DeleteProgram(it->second.program);
        m_shaders.erase(it);
    }
    void destroy(BufferHandle h) override {
        auto it = m_buffers.find(h.id);
        if (it == m_buffers.end())
            return;
        gl().DeleteBuffers(1, &it->second.name);
        m_buffers.erase(it);
    }

    void update_buffer(BufferHandle h, std::span<const std::byte> data,
                       size_t offset_bytes) override {
        auto it = m_buffers.find(h.id);
        if (it == m_buffers.end())
            return;
        gl().BindBuffer(it->second.target, it->second.name);
        gl().BufferSubData(it->second.target, static_cast<GLintptr>(offset_bytes),
                           static_cast<GLsizeiptr>(data.size_bytes()), data.data());
    }

    void update_texture(TextureHandle h, const TextureUpdate& u) override {
        auto it = m_textures.find(h.id);
        if (it == m_textures.end())
            return;
        if (u.data == nullptr || u.width == 0 || u.height == 0)
            return;
        if (u.x + u.width > it->second.width || u.y + u.height > it->second.height) {
            VX_ERROR("update_texture: region ({},{} {}x{}) out of bounds for {}x{} texture", u.x,
                     u.y, u.width, u.height, it->second.width, it->second.height);
            return;
        }
        gl().BindTexture(GL_TEXTURE_2D, it->second.name);
        gl().PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl().TexSubImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(u.x), static_cast<GLint>(u.y),
                           static_cast<GLsizei>(u.width), static_cast<GLsizei>(u.height), GL_RGBA,
                           GL_UNSIGNED_BYTE, u.data);
    }

    void begin_frame(vec4 c, uint32_t w, uint32_t h) override {
        // Remembered so returning from an offscreen pass can restore the
        // backbuffer viewport.
        m_fb_width = w;
        m_fb_height = h;
        gl().Viewport(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
        gl().Disable(GL_DEPTH_TEST);
        gl().Enable(GL_BLEND);
        gl().BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl().ClearColor(c.r, c.g, c.b, c.a);
        gl().Clear(GL_COLOR_BUFFER_BIT);
    }

    void end_frame() override { /* swap happens at the SDL window level */ }

    void draw_sprite_batch(ShaderHandle sh, BufferHandle vb, BufferHandle ib, uint32_t index_count,
                           TextureHandle tex, const mat4& proj) override {
        if (index_count == 0)
            return;
        auto s_it = m_shaders.find(sh.id);
        auto v_it = m_buffers.find(vb.id);
        auto i_it = m_buffers.find(ib.id);
        auto t_it = m_textures.find(tex.id);
        if (s_it == m_shaders.end() || v_it == m_buffers.end() || i_it == m_buffers.end() ||
            t_it == m_textures.end()) {
            VX_ERROR("draw_sprite_batch: invalid handle");
            return;
        }
        gl().UseProgram(s_it->second.program);
        gl().UniformMatrix4fv(s_it->second.mvp_loc, 1, GL_FALSE, &proj[0][0]);
        gl().Uniform1i(s_it->second.tex_loc, 0);
        gl().ActiveTexture(GL_TEXTURE0);
        gl().BindTexture(GL_TEXTURE_2D, t_it->second.name);
        gl().BindVertexArray(m_vao);
        gl().BindBuffer(GL_ARRAY_BUFFER, v_it->second.name);
        // pos(vec2)+uv(vec2)+color(vec4), interleaved, stride = 32
        gl().VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(0));
        gl().EnableVertexAttribArray(0);
        gl().VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(8));
        gl().EnableVertexAttribArray(1);
        gl().VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(16));
        gl().EnableVertexAttribArray(2);
        gl().BindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_it->second.name);
        gl().DrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count), GL_UNSIGNED_SHORT,
                          nullptr);
    }

  private:
    GLuint m_vao{0};
    uint32_t m_next_handle{1};
    std::unordered_map<uint32_t, GLTexture> m_textures;
    std::unordered_map<uint32_t, GLShader> m_shaders;
    std::unordered_map<uint32_t, GLBuffer> m_buffers;
    std::unordered_map<uint32_t, GLRenderTarget> m_targets;
    RenderTargetHandle m_bound_target{};
    uint32_t m_fb_width{0};
    uint32_t m_fb_height{0};
};

} // namespace

vaxelis::expected<std::unique_ptr<IDevice>, RhiError> create_gl_device() {
    if (!load_gl())
        return vaxelis::unexpected(RhiError::BackendUnavailable);
    return std::unique_ptr<IDevice>(new GLDevice());
}

} // namespace vaxelis::rhi::gl
