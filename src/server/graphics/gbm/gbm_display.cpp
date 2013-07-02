/*
 * Copyright © 2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#include "gbm_display.h"
#include "gbm_cursor.h"
#include "gbm_platform.h"
#include "gbm_display_buffer.h"
#include "kms_display_configuration.h"
#include "kms_output.h"
#include "kms_page_flipper.h"
#include "virtual_terminal.h"

#include "mir/main_loop.h"
#include "mir/graphics/display_report.h"
#include "mir/graphics/gl_context.h"
#include "mir/geometry/rectangle.h"

#include <boost/throw_exception.hpp>
#include <boost/exception/get_error_info.hpp>
#include <boost/exception/errinfo_errno.hpp>

#include <stdexcept>

namespace mgg = mir::graphics::gbm;
namespace mg = mir::graphics;
namespace geom = mir::geometry;

namespace
{

int errno_from_exception(std::exception const& e)
{
    auto errno_ptr = boost::get_error_info<boost::errinfo_errno>(e);
    return (errno_ptr != nullptr) ? *errno_ptr : -1;
}

class GBMGLContext : public mg::GLContext
{
public:
    GBMGLContext(mgg::helpers::GBMHelper const& gbm, EGLContext shared_context)
    {
        egl.setup(gbm, shared_context);
    }

    void make_current()
    {
        egl.make_current();
    }

    void release_current()
    {
        egl.release_current();
    }

private:
    mgg::helpers::EGLHelper egl;
};

}

mgg::GBMDisplay::GBMDisplay(std::shared_ptr<GBMPlatform> const& platform,
                            std::shared_ptr<DisplayReport> const& listener)
    : platform(platform),
      listener(listener),
      output_container{platform->drm.fd,
                       std::make_shared<KMSPageFlipper>(platform->drm.fd)}
{
    platform->vt->set_graphics_mode();

    shared_egl.setup(platform->gbm);

    configure(*configuration());

    shared_egl.make_current();
}

// please don't remove this empty destructor, it's here for the
// unique ptr!! if you accidentally remove it you will get a not
// so relevant linker error about some missing headers
mgg::GBMDisplay::~GBMDisplay()
{
}

geom::Rectangle mgg::GBMDisplay::view_area() const
{
    return display_buffers[0]->view_area();
}

void mgg::GBMDisplay::for_each_display_buffer(std::function<void(DisplayBuffer&)> const& f)
{
    for (auto& db_ptr : display_buffers)
        f(*db_ptr);
}

std::shared_ptr<mg::DisplayConfiguration> mgg::GBMDisplay::configuration()
{
    return std::make_shared<mgg::KMSDisplayConfiguration>(platform->drm.fd);
}

void mgg::GBMDisplay::configure(mg::DisplayConfiguration const& conf)
{
    std::vector<std::shared_ptr<KMSOutput>> enabled_outputs;

    /* Create or reset the KMS outputs */
    conf.for_each_output([&](DisplayConfigurationOutput const& conf_output)
    {
        auto const& kms_conf = static_cast<KMSDisplayConfiguration const&>(conf);
        uint32_t const connector_id = kms_conf.get_kms_connector_id(conf_output.id);

        auto output = output_container.get_kms_output_for(connector_id);

        if (conf_output.connected)
            enabled_outputs.push_back(output);
    });

    geom::Size max_size;

    /* Find the size of the largest enabled output... */
    for (auto const& output : enabled_outputs)
    {
        if (output->size().width > max_size.width)
            max_size.width = output->size().width;
        if (output->size().height > max_size.height)
            max_size.height = output->size().height;
    }

    /* ...and create a scanout surface with that size */
    auto surface = platform->gbm.create_scanout_surface(max_size.width.as_uint32_t(),
                                                        max_size.height.as_uint32_t());

    /* Create a single DisplayBuffer that displays the surface on all the outputs */
    std::unique_ptr<GBMDisplayBuffer> db{new GBMDisplayBuffer{platform, listener, enabled_outputs,
                                                              std::move(surface), max_size,
                                                              shared_egl.context()}};

    /* 
     * TODO: Investigate why we have to destroy the previous display buffers and
     * their contexts after creating the new ones to avoid a crash in Mesa.
     */
    display_buffers.clear();
    display_buffers.push_back(std::move(db));
}

void mgg::GBMDisplay::register_configuration_change_handler(
    MainLoop& main_loop,
    DisplayConfigurationChangeHandler const& conf_change_handler)
{
    auto monitor = std::shared_ptr<udev_monitor>(
                       udev_monitor_new_from_netlink(platform->udev.ctx, "udev"),
                       [](udev_monitor* m) { if (m) udev_monitor_unref(m); });
    if (!monitor)
        BOOST_THROW_EXCEPTION(std::runtime_error("Failed to create udev_monitor"));

    udev_monitor_filter_add_match_subsystem_devtype(monitor.get(), "drm", "drm_minor");

    main_loop.register_fd_handler(
        {udev_monitor_get_fd(monitor.get())},
        [conf_change_handler, monitor](int)
        {
            auto dev = std::unique_ptr<udev_device,std::function<void(udev_device*)>>(
                           udev_monitor_receive_device(monitor.get()),
                           [](udev_device* d) { if (d) udev_device_unref(d); });
            if (dev)
                conf_change_handler();
        });

    udev_monitor_enable_receiving(monitor.get());
}

void mgg::GBMDisplay::register_pause_resume_handlers(
    MainLoop& main_loop,
    DisplayPauseHandler const& pause_handler,
    DisplayResumeHandler const& resume_handler)
{
    platform->vt->register_switch_handlers(main_loop, pause_handler, resume_handler);
}

void mgg::GBMDisplay::pause()
{
    try
    {
        if (cursor) cursor->hide();
        platform->drm.drop_master();
    }
    catch(std::runtime_error const& e)
    {
        listener->report_drm_master_failure(errno_from_exception(e));
        throw;
    }
}

void mgg::GBMDisplay::resume()
{
    try
    {
        platform->drm.set_master();
        if (cursor) cursor->show_at_last_known_position();
    }
    catch(std::runtime_error const& e)
    {
        listener->report_drm_master_failure(errno_from_exception(e));
        throw;
    }

    for (auto& db_ptr : display_buffers)
        db_ptr->schedule_set_crtc();
}

auto mgg::GBMDisplay::the_cursor() -> std::weak_ptr<Cursor>
{
    if (!cursor) cursor = std::make_shared<GBMCursor>(platform, output_container);
    return cursor;
}

std::unique_ptr<mg::GLContext> mgg::GBMDisplay::create_gl_context()
{
    return std::unique_ptr<GBMGLContext>{
        new GBMGLContext{platform->gbm, shared_egl.context()}};
}
