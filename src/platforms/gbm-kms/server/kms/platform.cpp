/*
 * Copyright © 2012 Canonical Ltd.
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

#include "platform.h"
#include "buffer_allocator.h"
#include "display.h"
#include "mir/console_services.h"
#include "mir/emergency_cleanup_registry.h"
#include "mir/graphics/platform.h"
#include "mir/renderer/gl/context.h"
#include "mir/udev/wrapper.h"
#include "kms_framebuffer.h"
#include "mir/graphics/egl_error.h"
#include "mir/graphics/egl_extensions.h"
#include "one_shot_device_observer.h"
#include <gbm.h>

#define MIR_LOG_COMPONENT "platform-graphics-gbm-kms"
#include "mir/log.h"

#include <fcntl.h>
#include <boost/exception/all.hpp>

namespace mg = mir::graphics;
namespace mgg = mg::gbm;

namespace
{
auto master_fd_for_device(mir::udev::Device const& device, mir::ConsoleServices& vt) -> std::tuple<std::unique_ptr<mir::Device>, mir::Fd>
{
    mir::Fd drm_fd;
    auto device_handle = vt.acquire_device(
        major(device.devnum()), minor(device.devnum()),
        std::make_unique<mg::common::OneShotDeviceObserver>(drm_fd))
            .get();

    if (drm_fd == mir::Fd::invalid)
    {
        BOOST_THROW_EXCEPTION((std::runtime_error{"Failed to acquire DRM fd"}));
    }
    
    return std::make_tuple(std::move(device_handle), std::move(drm_fd));
}
}

mgg::Platform::Platform(
    udev::Device const& device,
    std::shared_ptr<DisplayReport> const& listener,
    std::shared_ptr<ConsoleServices> const& vt,
    EmergencyCleanupRegistry& registry,
    BypassOption bypass_option)
    : Platform(master_fd_for_device(device, *vt), listener, vt, registry, bypass_option)
{
}

mgg::Platform::Platform(
    std::tuple<std::unique_ptr<Device>, mir::Fd> drm,
    std::shared_ptr<DisplayReport> const& listener,
    std::shared_ptr<ConsoleServices> const& vt,
    EmergencyCleanupRegistry&,
    BypassOption bypass_option)
    : udev{std::make_shared<mir::udev::Context>()},
      listener{listener},
      vt{vt},
      device_handle{std::move(std::get<0>(drm))},
      drm_fd{std::move(std::get<1>(drm))},
      bypass_option_{bypass_option}
{
    if (drm_fd == mir::Fd::invalid)
    {
        BOOST_THROW_EXCEPTION((std::runtime_error{"WTF?"}));
    }
}

namespace
{
auto gbm_device_for_udev_device(
    mir::udev::Device const& device,
    std::vector<std::shared_ptr<mg::DisplayPlatform>> const& displays)
     -> std::variant<std::shared_ptr<mg::GBMDisplayProvider>, std::shared_ptr<gbm_device>>
{
    /* First check to see whether our device exactly matches a display device.
     * If so, we should use its GBM device
     */
    for(auto const& display_device : displays)
    {
        if (auto gbm_display = mg::DisplayPlatform::acquire_interface<mg::GBMDisplayProvider>(display_device))
        {
            if (gbm_display->is_same_device(device))
            {
                return gbm_display;
            }
        }
    }

    // We don't match any display HW, create our own GBM device
    if (auto node = device.devnode())
    {
        auto fd = mir::Fd{open(node, O_RDWR | O_CLOEXEC)};
        if (fd < 0)
        {
            BOOST_THROW_EXCEPTION((
                std::system_error{
                    errno,
                    std::system_category(),
                    "Failed to open DRM device"}));
        }
        std::shared_ptr<gbm_device> gbm{
            gbm_create_device(fd),
            [fd = std::move(fd)](gbm_device* device)
            {
                if (device)
                {
                    gbm_device_destroy(device);
                }
            }};
        if (!gbm)
        {
            BOOST_THROW_EXCEPTION((std::runtime_error{"Failed to create GBM device"}));
        }
        return gbm;
    }

    BOOST_THROW_EXCEPTION((
        std::runtime_error{"Attempt to create GBM device from UDev device with no device node?!"}));
}

/**
 * Initialise an EGLDisplay and return the initialised display
 */
auto initialise_egl(EGLDisplay dpy, int minimum_major_version, int minimum_minor_version) -> EGLDisplay
{
    EGLint major, minor;

    if (eglInitialize(dpy, &major, &minor) == EGL_FALSE)
        BOOST_THROW_EXCEPTION(mg::egl_error("Failed to initialize EGL display"));

    if ((major < minimum_major_version) ||
        (major == minimum_major_version && minor < minimum_minor_version))
    {
        std::stringstream msg;
        msg << "Incompatible EGL version. Requested: "
            << minimum_major_version << "." << minimum_minor_version
            << " got: " << major << "." << minor;
        BOOST_THROW_EXCEPTION((std::runtime_error{msg.str()}));
    }

    return dpy;
}

auto dpy_for_gbm_device(gbm_device* device) -> EGLDisplay
{
    mg::EGLExtensions::PlatformBaseEXT platform_ext;

    auto const egl_display = platform_ext.eglGetPlatformDisplay(
        EGL_PLATFORM_GBM_KHR,      // EGL_PLATFORM_GBM_MESA has the same value.
        static_cast<EGLNativeDisplayType>(device),
        nullptr);
    if (egl_display == EGL_NO_DISPLAY)
        BOOST_THROW_EXCEPTION(mg::egl_error("Failed to get EGL display"));

    return egl_display;
}

auto make_share_only_context(EGLDisplay dpy) -> EGLContext
{
    eglBindAPI(EGL_OPENGL_ES_API);

    static const EGLint context_attr[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    EGLint const config_attr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLConfig cfg;
    EGLint num_configs;
    
    if (eglChooseConfig(dpy, config_attr, &cfg, 1, &num_configs) != EGL_TRUE || num_configs != 1)
    {
        BOOST_THROW_EXCEPTION((mg::egl_error("Failed to find any matching EGL config")));
    }
    
    auto ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, context_attr);
    if (ctx == EGL_NO_CONTEXT)
    {
        BOOST_THROW_EXCEPTION(mg::egl_error("Failed to create EGL context"));
    }
    return ctx;
}

struct display_provider_or_nothing
{
    auto operator()(std::shared_ptr<mg::GBMDisplayProvider> provider) { return provider; }
    auto operator()(std::shared_ptr<gbm_device>) { return std::shared_ptr<mg::GBMDisplayProvider>{}; }
};

struct gbm_device_from_hw
{
    auto operator()(std::shared_ptr<mg::GBMDisplayProvider> const& provider) { return provider->gbm_device(); }
    auto operator()(std::shared_ptr<gbm_device> device) { return device; }    
};
}

mgg::RenderingPlatform::RenderingPlatform(
    mir::udev::Device const& device,
    std::vector<std::shared_ptr<mg::DisplayPlatform>> const& displays)
    : RenderingPlatform(device.clone(), gbm_device_for_udev_device(device, displays))
{
}

mgg::RenderingPlatform::RenderingPlatform(
    std::unique_ptr<mir::udev::Device> udev_device,
    std::variant<std::shared_ptr<mg::GBMDisplayProvider>, std::shared_ptr<gbm_device>> hw)
    : udev_device{std::move(udev_device)},
      device{std::visit(gbm_device_from_hw{}, hw)},
      bound_display{std::visit(display_provider_or_nothing{}, hw)},
      dpy{initialise_egl(dpy_for_gbm_device(device.get()), 1, 4)},
      share_ctx{make_share_only_context(dpy)}
{
}


mir::UniqueModulePtr<mg::GraphicBufferAllocator> mgg::RenderingPlatform::create_buffer_allocator(
    mg::Display const&)
{
    return make_module_ptr<mgg::BufferAllocator>(dpy, share_ctx);
}

auto mgg::RenderingPlatform::maybe_create_interface(
    RendererInterfaceBase::Tag const& type_tag) -> std::shared_ptr<RendererInterfaceBase>
{
    if (dynamic_cast<GLRenderingProvider::Tag const*>(&type_tag))
    {
        return std::make_shared<mgg::GLRenderingProvider>(*udev_device, bound_display, dpy, share_ctx);
    }
    return nullptr;
}

mir::UniqueModulePtr<mg::Display> mgg::Platform::create_display(
    std::shared_ptr<DisplayConfigurationPolicy> const& initial_conf_policy, std::shared_ptr<GLConfig> const&)
{
    return make_module_ptr<mgg::Display>(
        shared_from_this(),
        drm_fd,
        vt,
        bypass_option_,
        initial_conf_policy,
        listener);
}

mgg::BypassOption mgg::Platform::bypass_option() const
{
    return bypass_option_;
}

auto mgg::Platform::maybe_create_interface(DisplayInterfaceBase::Tag const& type_tag)
    -> std::shared_ptr<DisplayInterfaceBase>
{
    if (dynamic_cast<GBMDisplayProvider::Tag const*>(&type_tag))
    {
        mir::log_debug("Using GBMDisplayProvider");
        return std::make_shared<mgg::GBMDisplayProvider>(drm_fd, this);
    }
    if (dynamic_cast<DumbDisplayProvider::Tag const*>(&type_tag))
    {
        mir::log_debug("Using DumbDisplayProvider");
        return std::make_shared<mgg::DumbDisplayProvider>();
    }
    return {};
}
