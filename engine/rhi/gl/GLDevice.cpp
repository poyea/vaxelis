#include "engine/rhi/gl/GLDevice.hpp"

#include <cassert>
#include <unordered_map>
#include <vector>

#include "engine/core/Log.hpp"
#include "engine/rhi/gl/GLLoader.hpp"

namespace vaxelis::rhi::gl {

namespace {

struct GLTexture { GLuint name; };
struct GLShader  { GLuint program; GLint mvp_loc; GLint tex_loc; };
struct GLBuffer  { GLuint name; GLenum target; GLenum usage; size_t size; };

class GLDevice final : public IDevice {
public:
    GLDevice() {
        gl().GenVertexArrays(1, &vao_);
        gl().BindVertexArray(vao_);
        VX_INFO("GLDevice: created (VAO={})", vao_);
    }

    ~GLDevice() override {
        // Tear down any handles the user forgot to destroy, but assert in debug
        // so leaks are noisy.
        assert(textures_.empty() && "GLDevice: texture handles leaked");
        assert(shaders_.empty()  && "GLDevice: shader handles leaked");
        assert(buffers_.empty()  && "GLDevice: buffer handles leaked");
        for (auto& [_, t] : textures_) gl().DeleteTextures(1, &t.name);
        for (auto& [_, s] : shaders_)  gl().DeleteProgram(s.program);
        for (auto& [_, b] : buffers_)  gl().DeleteBuffers(1, &b.name);
        gl().DeleteVertexArrays(1, &vao_);
        VX_INFO("GLDevice: destroyed");
    }

    vaxelis::expected<TextureHandle, RhiError> create_texture(const TextureDesc& d) override {
        if (d.format != TextureFormat::RGBA8) return vaxelis::unexpected(RhiError::UnsupportedFormat);
        GLuint name = 0;
        gl().GenTextures(1, &name);
        gl().BindTexture(GL_TEXTURE_2D, name);
        gl().PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl().TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8),
                        static_cast<GLsizei>(d.width), static_cast<GLsizei>(d.height),
                        0, GL_RGBA, GL_UNSIGNED_BYTE, d.initial_data);
        gl().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        TextureHandle h{next_handle_++};
        textures_.emplace(h.id, GLTexture{name});
        return h;
    }

    vaxelis::expected<ShaderHandle, RhiError> create_shader(const ShaderDesc& d) override {
        auto compile = [](GLenum stage, std::string_view src) -> vaxelis::expected<GLuint, RhiError> {
            GLuint s = gl().CreateShader(stage);
            const GLchar* str = src.data();
            const GLint   len = static_cast<GLint>(src.size());
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

        auto vs = compile(GL_VERTEX_SHADER, d.vertex_src);
        if (!vs) return vaxelis::unexpected(vs.error());
        auto fs = compile(GL_FRAGMENT_SHADER, d.fragment_src);
        if (!fs) { gl().DeleteShader(*vs); return vaxelis::unexpected(fs.error()); }

        GLuint prog = gl().CreateProgram();
        gl().AttachShader(prog, *vs);
        gl().AttachShader(prog, *fs);
        gl().LinkProgram(prog);
        gl().DeleteShader(*vs);
        gl().DeleteShader(*fs);
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
        ShaderHandle h{next_handle_++};
        shaders_.emplace(h.id, GLShader{prog,
                                       gl().GetUniformLocation(prog, "u_mvp"),
                                       gl().GetUniformLocation(prog, "u_tex")});
        return h;
    }

    vaxelis::expected<BufferHandle, RhiError> create_buffer(const BufferDesc& d) override {
        GLenum target = (d.usage == BufferUsage::Index) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
        GLenum usage  = (d.access == BufferAccess::Dynamic) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
        GLuint name = 0;
        gl().GenBuffers(1, &name);
        gl().BindBuffer(target, name);
        gl().BufferData(target, static_cast<GLsizeiptr>(d.size_bytes), d.initial_data, usage);
        BufferHandle h{next_handle_++};
        buffers_.emplace(h.id, GLBuffer{name, target, usage, d.size_bytes});
        return h;
    }

    void destroy(TextureHandle h) override {
        auto it = textures_.find(h.id);
        if (it == textures_.end()) return;
        gl().DeleteTextures(1, &it->second.name);
        textures_.erase(it);
    }
    void destroy(ShaderHandle h) override {
        auto it = shaders_.find(h.id);
        if (it == shaders_.end()) return;
        gl().DeleteProgram(it->second.program);
        shaders_.erase(it);
    }
    void destroy(BufferHandle h) override {
        auto it = buffers_.find(h.id);
        if (it == buffers_.end()) return;
        gl().DeleteBuffers(1, &it->second.name);
        buffers_.erase(it);
    }

    void update_buffer(BufferHandle h, std::span<const std::byte> data, size_t offset_bytes) override {
        auto it = buffers_.find(h.id);
        if (it == buffers_.end()) return;
        gl().BindBuffer(it->second.target, it->second.name);
        gl().BufferSubData(it->second.target, static_cast<GLintptr>(offset_bytes),
                           static_cast<GLsizeiptr>(data.size_bytes()), data.data());
    }

    void begin_frame(vec4 c, uint32_t w, uint32_t h) override {
        gl().Viewport(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
        gl().Disable(GL_DEPTH_TEST);
        gl().Enable(GL_BLEND);
        gl().BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl().ClearColor(c.r, c.g, c.b, c.a);
        gl().Clear(GL_COLOR_BUFFER_BIT);
    }

    void end_frame() override { /* swap happens at the SDL window level */ }

    void draw_sprite_batch(ShaderHandle sh, BufferHandle vb, BufferHandle ib,
                           uint32_t index_count, TextureHandle tex,
                           const mat4& proj) override {
        if (index_count == 0) return;
        auto s_it = shaders_.find(sh.id);
        auto v_it = buffers_.find(vb.id);
        auto i_it = buffers_.find(ib.id);
        auto t_it = textures_.find(tex.id);
        if (s_it == shaders_.end() || v_it == buffers_.end() ||
            i_it == buffers_.end() || t_it == textures_.end()) {
            VX_ERROR("draw_sprite_batch: invalid handle");
            return;
        }
        gl().UseProgram(s_it->second.program);
        gl().UniformMatrix4fv(s_it->second.mvp_loc, 1, GL_FALSE, &proj[0][0]);
        gl().Uniform1i(s_it->second.tex_loc, 0);
        gl().ActiveTexture(GL_TEXTURE0);
        gl().BindTexture(GL_TEXTURE_2D, t_it->second.name);
        gl().BindVertexArray(vao_);
        gl().BindBuffer(GL_ARRAY_BUFFER, v_it->second.name);
        // pos(vec2)+uv(vec2)+color(vec4), interleaved, stride = 32
        gl().VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(0));
        gl().EnableVertexAttribArray(0);
        gl().VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(8));
        gl().EnableVertexAttribArray(1);
        gl().VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(16));
        gl().EnableVertexAttribArray(2);
        gl().BindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_it->second.name);
        gl().DrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count),
                          GL_UNSIGNED_SHORT, nullptr);
    }

private:
    GLuint vao_{0};
    uint32_t next_handle_{1};
    std::unordered_map<uint32_t, GLTexture> textures_;
    std::unordered_map<uint32_t, GLShader>  shaders_;
    std::unordered_map<uint32_t, GLBuffer>  buffers_;
};

}  // namespace

vaxelis::expected<std::unique_ptr<IDevice>, RhiError> create_gl_device() {
    if (!load_gl()) return vaxelis::unexpected(RhiError::BackendUnavailable);
    return std::unique_ptr<IDevice>(new GLDevice());
}

}  // namespace vaxelis::rhi::gl
