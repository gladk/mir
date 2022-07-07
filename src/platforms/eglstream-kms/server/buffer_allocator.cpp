/*
 * Copyright © 2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <epoxy/egl.h>

#include "buffer_allocator.h"
#include "mir/anonymous_shm_file.h"
#include "shm_buffer.h"
#include "buffer_from_wl_shm.h"
#include "mir/graphics/buffer_properties.h"
#include "mir/renderer/gl/context_source.h"
#include "mir/renderer/gl/context.h"
#include "mir/graphics/display.h"
#include "wayland-eglstream-controller.h"
#include "mir/graphics/egl_error.h"
#include "mir/graphics/texture.h"
#include "mir/graphics/program_factory.h"
#include "mir/graphics/program.h"
#include "mir/graphics/display.h"
#include "mir/renderer/gl/context_source.h"
#include "mir/renderer/gl/context.h"
#include "mir/raii.h"
#include "mir/wayland/wayland_base.h"
#include "mir/renderer/gl/gl_surface.h"
#include "mir/graphics/display_buffer.h"

#define MIR_LOG_COMPONENT "platform-eglstream-kms"
#include "mir/log.h"

#include <wayland-server-core.h>

#include <mutex>

#include <boost/throw_exception.hpp>
#include <boost/exception/errinfo_errno.hpp>

#include <system_error>
#include <cassert>


namespace mg  = mir::graphics;
namespace mge = mg::eglstream;
namespace mgc = mg::common;
namespace geom = mir::geometry;

#ifndef EGL_WL_wayland_eglstream
#define EGL_WL_wayland_eglstream 1
#define EGL_WAYLAND_EGLSTREAM_WL              0x334B
#endif /* EGL_WL_wayland_eglstream */

mge::BufferAllocator::BufferAllocator(std::unique_ptr<renderer::gl::Context> ctx)
    : wayland_ctx{ctx->make_share_context()},
      egl_delegate{
          std::make_shared<mgc::EGLContextExecutor>(std::move(ctx))}
{
}

mge::BufferAllocator::~BufferAllocator() = default;

std::shared_ptr<mg::Buffer> mge::BufferAllocator::alloc_software_buffer(geom::Size size, MirPixelFormat format)
{
    if (!mgc::MemoryBackedShmBuffer::supports(format))
    {
        BOOST_THROW_EXCEPTION(
            std::runtime_error(
                "Trying to create SHM buffer with unsupported pixel format"));
    }

    return std::make_shared<mgc::MemoryBackedShmBuffer>(size, format, egl_delegate);
}

std::vector<MirPixelFormat> mge::BufferAllocator::supported_pixel_formats()
{
    // Lazy
    return {mir_pixel_format_argb_8888, mir_pixel_format_xrgb_8888};
}

namespace
{

GLuint gen_texture_handle()
{
    GLuint tex;
    glGenTextures(1, &tex);
    return tex;
}

struct EGLStreamTextureConsumer
{
    EGLStreamTextureConsumer(std::shared_ptr<mir::renderer::gl::Context> ctx, EGLStreamKHR&& stream)
        : dpy{eglGetCurrentDisplay()},
          stream{std::move(stream)},
          texture{gen_texture_handle()},
          ctx{std::move(ctx)}
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture);
        // GL_NO_ERROR is 0, so this works.
        if (auto err = glGetError())
        {
            BOOST_THROW_EXCEPTION(mg::gl_error(err, "Failed to bind texture?!"));
        }

        if (eglStreamConsumerGLTextureExternalKHR(dpy, stream) != EGL_TRUE)
        {
            BOOST_THROW_EXCEPTION(
                mg::egl_error("Failed to bind client EGLStream to a texture consumer"));
        }
    }

    ~EGLStreamTextureConsumer()
    {
        bool const need_context = eglGetCurrentContext() == EGL_NO_CONTEXT;
        if (need_context)
        {
            ctx->make_current();
        }
        eglDestroyStreamKHR(dpy, stream);
        glDeleteTextures(1, &texture);
        if (need_context)
        {
            ctx->release_current();
        }
    }

    EGLDisplay const dpy;
    EGLStreamKHR const stream;
    GLuint const texture;
    std::shared_ptr<mir::renderer::gl::Context> const ctx;
};

struct Sync
{
    /*
     * The reserve_sync/set_consumer_sync dance is magical!
     */
    void reserve_sync()
    {
        sync_mutex.lock();
    }

    void set_consumer_sync(GLsync syncpoint)
    {
        if (sync)
        {
            glDeleteSync(sync);
        }
        sync = syncpoint;
        sync_mutex.unlock();
    }

    std::mutex sync_mutex;
    GLsync sync{nullptr};
};

struct BoundEGLStream
{
    static void associate_stream(wl_resource* buffer, std::shared_ptr<mir::renderer::gl::Context> ctx, EGLStreamKHR stream)
    {
        BoundEGLStream* me;
        if (auto notifier = wl_resource_get_destroy_listener(buffer, &on_buffer_destroyed))
        {
            /* We're associating a buffer which has an existing stream with a new stream?
             * The protocol is unclear whether this is an expected behaviour, but the obvious
             * thing to do is to destroy the old stream and associate the new one.
             */
            me = wl_container_of(notifier, me, destruction_listener);
        }
        else
        {
            me = new BoundEGLStream;
            me->destruction_listener.notify = &on_buffer_destroyed;
            wl_resource_add_destroy_listener(buffer, &me->destruction_listener);
        }

        me->producer = std::make_shared<EGLStreamTextureConsumer>(std::move(ctx), std::move(stream));
    }

    class TextureHandle
    {
    public:
        void bind()
        {
            glBindTexture(GL_TEXTURE_EXTERNAL_OES, provider->texture);
        }

        void reserve_sync()
        {
            sync->reserve_sync();
        }

        void set_consumer_sync(GLsync sync)
        {
            this->sync->set_consumer_sync(sync);
        }

        TextureHandle(TextureHandle&&) = default;
    private:
        friend class BoundEGLStream;

        TextureHandle(
            std::shared_ptr<Sync> syncpoint,
            std::shared_ptr<EGLStreamTextureConsumer const> producer)
            : sync{std::move(syncpoint)},
              provider{std::move(producer)}
        {
            bind();
            /*
             * Isn't this a fun dance!
             *
             * We must insert a glWaitSync here to ensure that the texture is not
             * modified while the commands from the render thread are still executing.
             *
             * We need to lock until *after* eglStreamConsumerAcquireKHR because,
             * once that has executed, it's guaranteed that glBindTexture() in the
             * render thread will bind the new texture (ie: there's some implicit syncpoint
             * action happening)
             */
            std::lock_guard lock{sync->sync_mutex};
            if (sync->sync)
            {
                glWaitSync(sync->sync, 0, GL_TIMEOUT_IGNORED);
                sync->sync = nullptr;
            }
            if (eglStreamConsumerAcquireKHR(provider->dpy, provider->stream) != EGL_TRUE)
            {
                BOOST_THROW_EXCEPTION(
                    mg::egl_error("Failed to latch texture from client EGLStream"));
            }
        }

        TextureHandle(TextureHandle const&) = delete;
        TextureHandle& operator=(TextureHandle const&) = delete;

        std::shared_ptr<Sync> const sync;
        std::shared_ptr<EGLStreamTextureConsumer const> provider;
    };

    static TextureHandle texture_for_buffer(wl_resource* buffer)
    {
        if (auto notifier = wl_resource_get_destroy_listener(buffer, &on_buffer_destroyed))
        {
            BoundEGLStream* me;
            me = wl_container_of(notifier, me, destruction_listener);
            return TextureHandle{me->consumer_sync, me->producer};
        }
        BOOST_THROW_EXCEPTION((std::runtime_error{"Buffer does not have an associated EGLStream"}));
    }
private:
    static void on_buffer_destroyed(wl_listener* listener, void*)
    {
        static_assert(
            std::is_standard_layout<BoundEGLStream>::value,
            "BoundEGLStream must be Standard Layout for wl_container_of to be defined behaviour");

        BoundEGLStream* me;
        me = wl_container_of(listener, me, destruction_listener);
        delete me;
    }

    std::shared_ptr<Sync> const consumer_sync{std::make_shared<Sync>()};
    std::shared_ptr<EGLStreamTextureConsumer const> producer;
    wl_listener destruction_listener;
};
}

void mge::BufferAllocator::create_buffer_eglstream_resource(
    wl_client* client,
    wl_resource* eglstream_controller_resource,
    wl_resource* /*surface*/,
    wl_resource* buffer)
try
{
    auto const allocator = static_cast<mge::BufferAllocator*>(
        wl_resource_get_user_data(eglstream_controller_resource));

    EGLAttrib const attribs[] = {
        EGL_WAYLAND_EGLSTREAM_WL, (EGLAttrib)buffer,
        EGL_NONE
    };

    allocator->wayland_ctx->make_current();
    auto dpy = eglGetCurrentDisplay();

    auto stream = allocator->nv_extensions(dpy).eglCreateStreamAttribNV(dpy, attribs);

    if (stream == EGL_NO_STREAM_KHR)
    {
        BOOST_THROW_EXCEPTION((mg::egl_error("Failed to create EGLStream from Wayland buffer")));
    }

    BoundEGLStream::associate_stream(buffer, allocator->wayland_ctx, stream);

    allocator->wayland_ctx->release_current();
}
catch (std::exception const& err)
{
    mir::wayland::internal_error_processing_request(client,  "create_buffer_eglstream_resource");
}

struct wl_eglstream_controller_interface const mge::BufferAllocator::impl{
    create_buffer_eglstream_resource
};

void mge::BufferAllocator::bind_eglstream_controller(
    wl_client* client,
    void* ctx,
    uint32_t version,
    uint32_t id)
{
    auto resource = wl_resource_create(client, &wl_eglstream_controller_interface, version, id);

    if (resource == nullptr)
    {
        wl_client_post_no_memory(client);
        mir::log_warning("Failed to create client eglstream-controller resource");
        return;
    }

    wl_resource_set_implementation(
        resource,
        &impl,
        ctx,
        nullptr);
}


void mir::graphics::eglstream::BufferAllocator::bind_display(
    wl_display* display,
    std::shared_ptr<Executor>)
{
    if (!wl_global_create(
        display,
        &wl_eglstream_controller_interface,
        1,
        this,
        &bind_eglstream_controller))
    {
        BOOST_THROW_EXCEPTION((std::runtime_error{"Failed to publish wayland-eglstream-controller global"}));
    }

    auto context_guard = mir::raii::paired_calls(
        [this]() { wayland_ctx->make_current(); },
        [this]() { wayland_ctx->release_current(); });

    auto dpy = eglGetCurrentDisplay();

    if (extensions(dpy).eglBindWaylandDisplayWL(dpy, display) != EGL_TRUE)
    {
        BOOST_THROW_EXCEPTION((mg::egl_error("Failed to bind Wayland EGL display")));
    }

    std::vector<char const*> missing_extensions;
    for (char const* extension : {
        "EGL_KHR_stream_consumer_gltexture",
        "EGL_NV_stream_attrib"})
    {
        if (!epoxy_has_egl_extension(dpy, extension))
        {
            missing_extensions.push_back(extension);
        }
    }

    if (!missing_extensions.empty())
    {
        std::stringstream message;
        message << "Missing required extension" << (missing_extensions.size() > 1 ? "s:" : ":");
        for (auto missing_extension : missing_extensions)
        {
            message << " " << missing_extension;
        }

        BOOST_THROW_EXCEPTION((std::runtime_error{message.str()}));
    }

    mir::log_info("Bound EGLStreams-backed Wayland display");
}

void mir::graphics::eglstream::BufferAllocator::unbind_display(wl_display* display)
{
    auto context_guard = mir::raii::paired_calls(
        [this]() { wayland_ctx->make_current(); },
        [this]() { wayland_ctx->release_current(); });
    auto dpy = eglGetCurrentDisplay();

    if (extensions(dpy).eglUnbindWaylandDisplayWL(dpy, display) != EGL_TRUE)
    {
        BOOST_THROW_EXCEPTION((mg::egl_error("Failed to unbind Wayland EGL display")));
    }
}

namespace
{
class EGLStreamBuffer :
    public mg::BufferBasic,
    public mg::NativeBufferBase,
    public mg::gl::Texture
{
public:
    EGLStreamBuffer(
        BoundEGLStream::TextureHandle tex,
        std::function<void()>&& on_consumed,
        MirPixelFormat format,
        geom::Size size,
        Layout layout)
        : size_{size},
          layout_{layout},
          format{format},
          tex{std::move(tex)},
          on_consumed{std::move(on_consumed)}
    {
    }

    mir::geometry::Size size() const override
    {
        return size_;
    }

    MirPixelFormat pixel_format() const override
    {
        return format;
    }

    NativeBufferBase* native_buffer_base() override
    {
        return this;
    }

    mg::gl::Program const& shader(mg::gl::ProgramFactory& cache) const override
    {
        static int shader_id{0};
        return cache.compile_fragment_shader(
            &shader_id,
            "#ifdef GL_ES\n"
            "#extension GL_OES_EGL_image_external : require\n"
            "#endif\n",
            "uniform samplerExternalOES tex;\n"
            "vec4 sample_to_rgba(in vec2 texcoord)\n"
            "{\n"
            "    return texture2D(tex, texcoord);\n"
            "}\n");
    }

    void bind() override
    {
        tex.reserve_sync();
        tex.bind();
    }

    void add_syncpoint() override
    {
        tex.set_consumer_sync(glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
        // TODO: We're going to flush an *awful* lot; try and work out a way to batch this.
        glFlush();
        on_consumed();
    }

    Layout layout() const override
    {
        return layout_;
    }

private:
    mir::geometry::Size const size_;
    Layout const layout_;
    MirPixelFormat const format;
    BoundEGLStream::TextureHandle tex;
    std::function<void()> on_consumed;
};
}

std::shared_ptr<mir::graphics::Buffer>
mir::graphics::eglstream::BufferAllocator::buffer_from_resource(
    wl_resource* buffer,
    std::function<void()>&& on_consumed,
    std::function<void()>&& /*on_release*/)
{
    auto context_guard = mir::raii::paired_calls(
        [this]() { wayland_ctx->make_current(); },
        [this]() { wayland_ctx->release_current(); });
    auto dpy = eglGetCurrentDisplay();

    EGLint width, height;
    if (extensions(dpy).eglQueryWaylandBufferWL(dpy, buffer, EGL_WIDTH, &width) != EGL_TRUE)
    {
        BOOST_THROW_EXCEPTION(mg::egl_error("Failed to query Wayland buffer width"));
    }
    if (extensions(dpy).eglQueryWaylandBufferWL(dpy, buffer, EGL_HEIGHT, &height) != EGL_TRUE)
    {
        BOOST_THROW_EXCEPTION(mg::egl_error("Failed to query Wayland buffer height"));
    }
    mg::gl::Texture::Layout const layout =
        [&]()
        {
            EGLint y_inverted;
            if (extensions(dpy).eglQueryWaylandBufferWL(dpy, buffer, EGL_WAYLAND_Y_INVERTED_WL, &y_inverted) != EGL_TRUE)
            {
                // If querying Y_INVERTED fails, we must have the default, GL, layout
                return mg::gl::Texture::Layout::GL;
            }
            if (y_inverted)
            {
                return mg::gl::Texture::Layout::GL;
            }
            else
            {
                return mg::gl::Texture::Layout::TopRowFirst;
            }
        }();

    return std::make_shared<EGLStreamBuffer>(
        BoundEGLStream::texture_for_buffer(buffer),
        std::move(on_consumed),
        mir_pixel_format_argb_8888,
        geom::Size{width, height},
        layout);
}

auto mge::BufferAllocator::buffer_from_shm(
    wl_resource* buffer,
    std::shared_ptr<Executor> wayland_executor,
    std::function<void()>&& on_consumed) -> std::shared_ptr<Buffer>
{
    return mg::wayland::buffer_from_wl_shm(
        buffer,
        std::move(wayland_executor),
        egl_delegate,
        std::move(on_consumed));
}

namespace
{
// libepoxy replaces the GL symbols with resolved-on-first-use function pointers
template<void (**allocator)(GLsizei, GLuint*), void (** deleter)(GLsizei, GLuint const*)>
class GLHandle
{
public:
    GLHandle()
    {
        (**allocator)(1, &id);
    }

    ~GLHandle()
    {
        if (id)
            (**deleter)(1, &id);
    }

    GLHandle(GLHandle const&) = delete;
    GLHandle& operator=(GLHandle const&) = delete;

    GLHandle(GLHandle&& from)
        : id{from.id}
    {
        from.id = 0;
    }

    operator GLuint() const
    {
        return id;
    }

private:
    GLuint id;
};

using RenderbufferHandle = GLHandle<&glGenRenderbuffers, &glDeleteRenderbuffers>;
using FramebufferHandle = GLHandle<&glGenFramebuffers, &glDeleteFramebuffers>;


class CPUCopyOutputSurface : public mg::gl::OutputSurface
{
public:
    CPUCopyOutputSurface(
        std::shared_ptr<mir::renderer::gl::Context> ctx,
        std::unique_ptr<mg::DumbDisplayProvider::Allocator> allocator,
        geom::Size size)
        : allocator{std::move(allocator)},
          ctx{std::move(ctx)},
          size{std::move(size)}
    {
        glBindRenderbuffer(GL_RENDERBUFFER, colour_buffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, size.width.as_int(), size.height.as_int());

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colour_buffer);

        auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            switch (status)
            {
                case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
                    BOOST_THROW_EXCEPTION((
                        std::runtime_error{"FBO is incomplete: GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT"}
                        ));
                case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
                    // Somehow we've managed to attach buffers with mismatched sizes?
                    BOOST_THROW_EXCEPTION((
                        std::logic_error{"FBO is incomplete: GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS"}
                        ));
                case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
                    BOOST_THROW_EXCEPTION((
                        std::logic_error{"FBO is incomplete: GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT"}
                        ));
                case GL_FRAMEBUFFER_UNSUPPORTED:
                    // This is the only one that isn't necessarily a programming error
                    BOOST_THROW_EXCEPTION((
                        std::runtime_error{"FBO is incomplete: formats selected are not supported by this GL driver"}
                        ));
                case 0:
                    BOOST_THROW_EXCEPTION((
                        mg::gl_error("Failed to verify GL Framebuffer completeness")));
            }
            BOOST_THROW_EXCEPTION((
                std::runtime_error{
                    std::string{"Unknown GL framebuffer error code: "} + std::to_string(status)}));
        }
    }

    void bind() override
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    }

    void make_current() override
    {
        ctx->make_current();
    }

    auto commit() -> std::unique_ptr<mg::Framebuffer> override
    {
        auto fb = allocator->acquire();
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        {
            auto mapping = fb->map_writeable();
            /*
             * TODO: This introduces a pipeline stall; GL must wait for all previous rendering commands
             * to complete before glReadPixels returns. We could instead do something fancy with
             * pixel buffer objects to defer this cost.
             */
            /*
             * TODO: We are assuming that the framebuffer pixel format is RGBX
             */
            glReadPixels(
                0, 0,
                size.width.as_int(), size.height.as_int(),
                GL_RGBA, GL_UNSIGNED_BYTE, mapping->data());
        }
        return fb;
    }

    auto layout() const -> Layout override
    {
        return Layout::TopRowFirst;
    }

private:
    std::unique_ptr<mg::DumbDisplayProvider::Allocator> const allocator;
    std::shared_ptr<mir::renderer::gl::Context> const ctx;
    geom::Size const size;
    RenderbufferHandle const colour_buffer;
    FramebufferHandle const fbo;
};
}

mge::GLRenderingProvider::GLRenderingProvider(std::unique_ptr<mir::renderer::gl::Context> ctx)
    : ctx{std::move(ctx)}
{
}

auto mir::graphics::eglstream::GLRenderingProvider::as_texture(std::shared_ptr<Buffer> buffer)
    -> std::shared_ptr<gl::Texture>
{
    return std::dynamic_pointer_cast<gl::Texture>(std::move(buffer));
}

auto mge::GLRenderingProvider::surface_for_output(mg::DisplayBuffer& db) -> std::unique_ptr<gl::OutputSurface>
{
    auto dumb_display = DisplayPlatform::acquire_interface<DumbDisplayProvider>(db.owner());

    auto fb_context = ctx->make_share_context();
    fb_context->make_current();
    return std::make_unique<CPUCopyOutputSurface>(
        std::move(fb_context),
        dumb_display->allocator_for_db(db),
        db.view_area().size);
}

auto mge::GLRenderingProvider::make_framebuffer_provider(mir::graphics::DisplayBuffer const& /*target*/)
    -> std::unique_ptr<FramebufferProvider>
{
    // TODO: *Can* we provide overlay support?
    class NullFramebufferProvider : public FramebufferProvider
    {
    public:
        auto buffer_to_framebuffer(std::shared_ptr<Buffer>) -> std::unique_ptr<Framebuffer> override
        {
            // It is safe to return nullptr; this will be treated as “this buffer cannot be used as
            // a framebuffer”.
            return {};
        }
    };
    return std::make_unique<NullFramebufferProvider>();
}

auto mge::GLRenderingProvider::make_framebuffer_provider(mir::graphics::DisplayBuffer const& /*target*/)
    -> std::unique_ptr<FramebufferProvider>
{
    // TODO: *Can* we provide overlay support?
    class NullFramebufferProvider : public FramebufferProvider
    {
    public:
        auto buffer_to_framebuffer(std::shared_ptr<Buffer>) -> std::unique_ptr<Framebuffer> override
        {
            // It is safe to return nullptr; this will be treated as “this buffer cannot be used as
            // a framebuffer”.
            return {};
        }
    };
    return std::make_unique<NullFramebufferProvider>();
}
