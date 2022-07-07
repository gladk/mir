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

#include "platform.h"
#include "buffer_allocator.h"
#include "display.h"
#include "utils.h"

#include "mir/graphics/egl_error.h"

#include <boost/throw_exception.hpp>
#include <boost/exception/errinfo_file_name.hpp>
#include <sys/sysmacros.h>
#include <one_shot_device_observer.h>

namespace mg = mir::graphics;
namespace mge = mir::graphics::eglstream;
namespace mgc = mir::graphics::common;

mge::DisplayPlatform::DisplayPlatform(
    ConsoleServices& console,
    EGLDeviceEXT device,
    std::shared_ptr<mg::DisplayReport> display_report)
    : display_report{std::move(display_report)},
      display{EGL_NO_DISPLAY}
{
    using namespace std::literals;

    auto const devnum = devnum_for_device(device);
    drm_device = console.acquire_device(
        major(devnum), minor(devnum),
        std::make_unique<mgc::OneShotDeviceObserver>(drm_node))
            .get();

    if (drm_node == mir::Fd::invalid)
    {
        BOOST_THROW_EXCEPTION((
            std::runtime_error{"Failed to acquire DRM device node for device"}));
    }

    int const drm_node_attrib[] = {
        EGL_DRM_MASTER_FD_EXT, static_cast<int>(drm_node), EGL_NONE
    };
    display = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, device, drm_node_attrib);

    if (display == EGL_NO_DISPLAY)
    {
        BOOST_THROW_EXCEPTION(mg::egl_error("Failed to create EGLDisplay on the EGLDeviceEXT"));
    }

    EGLint major{1};
    EGLint minor{4};
    auto const required_egl_version_major = major;
    auto const required_egl_version_minor = minor;
    if (eglInitialize(display, &major, &minor) != EGL_TRUE)
    {
        BOOST_THROW_EXCEPTION(mg::egl_error("Failed to initialise EGL"));
    }
    if ((major < required_egl_version_major) ||
        (major == required_egl_version_major && minor < required_egl_version_minor))
    {
        BOOST_THROW_EXCEPTION((std::runtime_error{
            "Incompatible EGL version"s +
            "Wanted 1.4, got " + std::to_string(major) + "." + std::to_string(minor)}));
    }
}

mir::UniqueModulePtr<mg::Display> mge::DisplayPlatform::create_display(
    std::shared_ptr<DisplayConfigurationPolicy> const& configuration_policy,
    std::shared_ptr<GLConfig> const& gl_config)
{
    auto retval =
        mir::make_module_ptr<mge::Display>(
            drm_node,
            display,
            configuration_policy,
            *gl_config,
            display_report);
    return retval;
}

namespace
{
const auto mir_xwayland_option = "MIR_XWAYLAND_OPTION";
}

mge::RenderingPlatform::RenderingPlatform()
{
    setenv(mir_xwayland_option, "-eglstream", 1);
}

mge::RenderingPlatform::~RenderingPlatform()
{
    unsetenv(mir_xwayland_option);
}

mir::UniqueModulePtr<mg::GraphicBufferAllocator> mge::RenderingPlatform::create_buffer_allocator(
    mg::Display const& output)
{
    return mir::make_module_ptr<mge::BufferAllocator>(output);
}

auto mge::RenderingPlatform::maybe_create_interface(
    RendererInterfaceBase::Tag const& /*type_tag*/) -> std::shared_ptr<RendererInterfaceBase>
{
    return nullptr;
}
