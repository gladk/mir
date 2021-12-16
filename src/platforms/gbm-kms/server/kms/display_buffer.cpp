/*
 * Copyright © 2013 Canonical Ltd.
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

#include "display_buffer.h"
#include "kms_output.h"
#include "dumb_fb.h"
#include "mir/fd.h"
#include "mir/graphics/display_report.h"
#include "mir/graphics/transformation.h"
#include "bypass.h"
#include "mir/fatal.h"
#include "mir/log.h"
#include "native_buffer.h"
#include "display_helpers.h"
#include "egl_helper.h"
#include "mir/graphics/egl_error.h"
#include "mir/graphics/gl_config.h"
#include "mir/graphics/dmabuf_buffer.h"

#include <boost/throw_exception.hpp>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>

#include <sstream>
#include <stdexcept>
#include <chrono>
#include <algorithm>

namespace mgg = mir::graphics::gbm;
namespace geom = mir::geometry;

mgg::DisplayBuffer::DisplayBuffer(
    std::shared_ptr<DisplayPlatform> owner,
    mir::Fd drm_fd,
    mgg::BypassOption,
    std::shared_ptr<DisplayReport> const& listener,
    std::vector<std::shared_ptr<KMSOutput>> const& outputs,
    geom::Rectangle const& area,
    glm::mat2 const& transformation)
    : owner_{std::move(owner)},
      listener(listener),
      outputs(outputs),
      area(area),
      transform{transformation},
      needs_set_crtc{false},
      page_flips_pending{false}
{
    listener->report_successful_setup_of_native_resources();

    auto initial_fb = std::make_shared<mgg::DumbFB>(std::move(drm_fd), false, area.size);

    auto mapping = initial_fb->map_writeable();
    ::memset(mapping->data(), 24, mapping->len());

    visible_fb = std::move(initial_fb);
    for (auto& output : outputs)
    {
        output->set_crtc(*visible_fb);
    }
/*    make_current();

    listener->report_successful_egl_make_current_on_construction();

    glClear(GL_COLOR_BUFFER_BIT);

    surface.swap_buffers();

    listener->report_successful_egl_buffer_swap_on_construction();

    auto temporary_front = surface.lock_front();
    if (!temporary_front)
        fatal_error("Failed to get frontbuffer");

    if (needs_bounce_buffer(*outputs.front(), temporary_front))
    {
        mir::log_info("Hybrid GPU setup detected; DisplayBuffer using EGL buffer copies for migration");
        get_front_buffer = std::bind(
            std::mem_fn(&EGLBufferCopier::copy_front_buffer_from),
            std::make_shared<EGLBufferCopier>(
                mir::Fd{mir::IntOwnedFd{outputs.front()->drm_fd()}},
                surface.size().width.as_int(),
                surface.size().height.as_int(),
                GBM_FORMAT_XRGB8888),
            std::placeholders::_1);
    }
    else
    {
        mir::log_info("Detected single-GPU DisplayBuffer. Rendering will be sent directly to output");
        get_front_buffer = [](auto&& fb) { return std::move(fb); };
    }

    visible_composite_frame = get_front_buffer(std::move(temporary_front));
*/
    /*
     * Check that our (possibly bounced) front buffer is usable on *all* the
     * outputs we've been asked to output on.
     */
/*    for (auto const& output : outputs)
    {
        if (output->buffer_requires_migration(visible_composite_frame))
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument(
                "Attempted to create a DisplayBuffer spanning multiple GPU memory domains"));
        }
    }

    set_crtc(*outputs.front()->fb_for(visible_composite_frame));

    release_current();
*/
    listener->report_successful_drm_mode_set_crtc_on_construction();
    listener->report_successful_display_construction();
}

mgg::DisplayBuffer::~DisplayBuffer()
{
}

geom::Rectangle mgg::DisplayBuffer::view_area() const
{
    return area;
}

glm::mat2 mgg::DisplayBuffer::transformation() const
{
    return transform;
}

void mgg::DisplayBuffer::set_transformation(glm::mat2 const& t, geometry::Rectangle const& a)
{
    transform = t;
    area = a;
}

bool mgg::DisplayBuffer::overlay(std::vector<DisplayElement> const& renderable_list)
{
    // TODO: implement more than the most basic case.
    if (renderable_list.size() != 1)
    {
        return false;
    }

    if (renderable_list[0].screen_positon != view_area())
    {
        return false;
    }

    if (renderable_list[0].source_position.top_left != geom::PointF {0,0} ||
        renderable_list[0].source_position.size.width.as_value() != view_area().size.width.as_int() ||
        renderable_list[0].source_position.size.height.as_value() != view_area().size.height.as_int())
    {
        return false;
    }

    if (auto fb = std::dynamic_pointer_cast<mgg::FBHandle>(renderable_list[0].buffer))
    {
        scheduled_fb = std::move(fb);
        return true;
    }
    return false;
}

void mgg::DisplayBuffer::for_each_display_buffer(
    std::function<void(graphics::DisplayBuffer&)> const& f)
{
    f(*this);
}

void mgg::DisplayBuffer::set_crtc(FBHandle const& forced_frame)
{
    for (auto& output : outputs)
    {
        /*
         * Note that failure to set the CRTC is not a fatal error. This can
         * happen under normal conditions when resizing VirtualBox (which
         * actually removes and replaces the virtual output each time so
         * sometimes it's really not there). Xorg often reports similar
         * errors, and it's not fatal.
         */
        if (!output->set_crtc(forced_frame))
            mir::log_error("Failed to set DRM CRTC. "
                "Screen contents may be incomplete. "
                "Try plugging the monitor in again.");
    }
}

void mgg::DisplayBuffer::post()
{
    /*
     * We might not have waited for the previous frame to page flip yet.
     * This is good because it maximizes the time available to spend rendering
     * each frame. Just remember wait_for_page_flip() must be called at some
     * point before the next schedule_page_flip().
     */
    wait_for_page_flip();

    /*
     * Try to schedule a page flip as first preference to avoid tearing.
     * [will complete in a background thread]
     */
    if (!needs_set_crtc && !schedule_page_flip(*scheduled_fb))
        needs_set_crtc = true;

    /*
     * Fallback blitting: Not pretty, since it may tear. VirtualBox seems
     * to need to do this on every frame. [will complete in this thread]
     */
    if (needs_set_crtc)
    {
        set_crtc(*scheduled_fb);
        // SetCrtc is immediate, so the FB is now visible and we have nothing pending
        visible_fb = std::move(scheduled_fb);
        scheduled_fb = nullptr;

        needs_set_crtc = false;
    }

    using namespace std::chrono_literals;  // For operator""ms()

    // Predicted worst case render time for the next frame...
    auto predicted_render_time = 50ms;

    if (holding_client_buffers)
    {
        /*
         * For composited frames we defer wait_for_page_flip till just before
         * the next frame, but not for bypass frames. Deferring the flip of
         * bypass frames would increase the time we held
         * visible_bypass_frame unacceptably, resulting in client stuttering
         * unless we allocate more buffers (which I'm trying to avoid).
         * Also, bypass does not need the deferred page flip because it has
         * no compositing/rendering step for which to save time for.
         */
        wait_for_page_flip();

        // It's very likely the next frame will be bypassed like this one so
        // we only need time for kernel page flip scheduling...
        predicted_render_time = 5ms;
    }
    else
    {
        /*
         * Not in clone mode? We can afford to wait for the page flip then,
         * making us double-buffered (noticeably less laggy than the triple
         * buffering that clone mode requires).
         */
        if (outputs.size() == 1)
            wait_for_page_flip();

        /*
         * TODO: If you're optimistic about your GPU performance and/or
         *       measure it carefully you may wish to set predicted_render_time
         *       to a lower value here for lower latency.
         *
         *predicted_render_time = 9ms; // e.g. about the same as Weston
         */
    }

    recommend_sleep = 0ms;
    if (outputs.size() == 1)
    {
        auto const& output = outputs.front();
        auto const min_frame_interval = 1000ms / output->max_refresh_rate();
        if (predicted_render_time < min_frame_interval)
            recommend_sleep = min_frame_interval - predicted_render_time;
    }
}

std::chrono::milliseconds mgg::DisplayBuffer::recommended_sleep() const
{
    return recommend_sleep;
}

bool mgg::DisplayBuffer::schedule_page_flip(FBHandle const& bufobj)
{
    /*
     * Schedule the current front buffer object for display. Note that
     * the page flip is asynchronous and synchronized with vertical refresh.
     */
    for (auto& output : outputs)
    {
        if (output->schedule_page_flip(bufobj))
            page_flips_pending = true;
    }

    return page_flips_pending;
}

void mgg::DisplayBuffer::wait_for_page_flip()
{
    if (page_flips_pending)
    {
        for (auto& output : outputs)
            output->wait_for_page_flip();

        // The previously-scheduled FB has been page-flipped, and is now visible
        visible_fb = std::move(scheduled_fb);
        scheduled_fb = nullptr;

        page_flips_pending = false;
    }
}

void mgg::DisplayBuffer::schedule_set_crtc()
{
    needs_set_crtc = true;
}

auto mir::graphics::gbm::DisplayBuffer::owner() const -> std::shared_ptr<DisplayPlatform>
{
    return owner_;
}

auto mgg::DisplayBuffer::drm_fd() const -> mir::Fd
{
    return mir::Fd{mir::IntOwnedFd{outputs.front()->drm_fd()}};
}

void mir::graphics::gbm::DisplayBuffer::set_next_image(std::unique_ptr<Framebuffer> content)
{
    std::vector<DisplayElement> const single_buffer = {
        DisplayElement {
            view_area(),
            geom::RectangleF{
                {0, 0},
                {view_area().size.width.as_value(), view_area().size.height.as_value()}},
            std::move(content)
        }
    };
    overlay(single_buffer);
}
