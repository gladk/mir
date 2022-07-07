/*
 * Copyright © 2021 Canonical Ltd.
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
 *
 * Authored by: Christopher James Halse Rogers <christopher.halse.rogers@canonical.com>
 */

#include "dumb_fb.h"

#include "mir/log.h"

#include <sys/mman.h>
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <drm_fourcc.h>

namespace mg = mir::graphics;
namespace mgg = mg::gbm;

class mgg::DumbFB::DumbBuffer : public mir::renderer::software::RWMappableBuffer
{
    template<typename T>
    class Mapping : public mir::renderer::software::Mapping<T>
    {
    public:
        Mapping(
            uint32_t width, uint32_t height,
            uint32_t pitch,
            T* data,
            size_t len)
            : size_{width, height},
              stride_{pitch},
              data_{data},
              len_{len}
        {
        }

        ~Mapping()
        {
            if (::munmap(const_cast<typename std::remove_const<T>::type *>(data_), len_) == -1)
            {
                // It's unclear how this could happen, but tell *someone* about it if it does!
                mir::log_error("Failed to unmap CPU buffer: %s (%i)", strerror(errno), errno);
            }
        }

        [[nodiscard]]
        auto format() const -> MirPixelFormat
        {
            return mir_pixel_format_xrgb_8888;
        }

        [[nodiscard]]
        auto stride() const -> mir::geometry::Stride
        {
            return stride_;
        }

        [[nodiscard]]
        auto size() const -> mir::geometry::Size
        {
            return size_;
        }

        [[nodiscard]]
        auto data() -> T*
        {
            return data_;
        }

        [[nodiscard]]
        auto len() const -> size_t
        {
            return len_;
        }

    private:
        mir::geometry::Size const size_;
        mir::geometry::Stride const stride_;
        T* const data_;
        size_t const len_;
    };

public:
    ~DumbBuffer()
    {
        struct drm_mode_destroy_dumb params = { gem_handle };

        if (auto const err = drmIoctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &params))
        {
            mir::log_error("Failed destroy CPU-accessible buffer: %s (%i)", strerror(-err), -err);
        }
    }

    static auto create_dumb_buffer(mir::Fd drm_fd,  mir::geometry::Size const& size) ->
        std::unique_ptr<DumbBuffer>
    {
        struct drm_mode_create_dumb params = {};

        params.bpp = 32;
        params.width = size.width.as_uint32_t();
        params.height = size.height.as_uint32_t();

        if (auto const err = drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &params))
        {
            BOOST_THROW_EXCEPTION((
                std::system_error{
                    -err,
                    std::system_category(),
                    "Failed to allocate CPU-accessible buffer"}));
        }

        return std::unique_ptr<DumbBuffer>{
            new DumbBuffer{std::move(drm_fd), params}};
    }

    auto map_writeable() -> std::unique_ptr<mir::renderer::software::Mapping<unsigned char>>
    {
        auto const data = mmap_buffer(PROT_WRITE);
        return std::make_unique<Mapping<unsigned char>>(
            width(), height(),
            pitch(),
            static_cast<unsigned char*>(data),
            size);
    }
    auto map_readable() -> std::unique_ptr<mir::renderer::software::Mapping<unsigned char const>>
    {
        auto const data = mmap_buffer(PROT_READ);
        return std::make_unique<Mapping<unsigned char const>>(
            width(), height(),pitch(),
            static_cast<unsigned char const*>(data),
            size);
    }

    auto map_rw() -> std::unique_ptr<mir::renderer::software::Mapping<unsigned char>>
    {
        auto const data = mmap_buffer(PROT_READ | PROT_WRITE);
        return std::make_unique<Mapping<unsigned char>>(
            width(), height(),
            pitch(),
            static_cast<unsigned char*>(data),
            size);
    }

    [[nodiscard]]
    auto handle() const -> uint32_t
    {
        return gem_handle;
    }
    [[nodiscard]]
    auto pitch() const -> uint32_t
    {
        return pitch_;
    }
    [[nodiscard]]
    auto width() const -> uint32_t
    {
        return width_;
    }
    [[nodiscard]]
    auto height() const -> uint32_t
    {
        return height_;
    }
private:
    DumbBuffer(
        mir::Fd fd,
        struct drm_mode_create_dumb const& params)
        : drm_fd{std::move(fd)},
          width_{params.width},
          height_{params.height},
          pitch_{params.pitch},
          gem_handle{params.handle},
          size{params.size}
    {
    }

    auto mmap_buffer(int access_mode) -> void*
    {
        struct drm_mode_map_dumb map_request = {};

        map_request.handle = gem_handle;

        if (auto err = drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_request))
        {
            BOOST_THROW_EXCEPTION((
                std::system_error{
                    -err,
                    std::system_category(),
                    "Failed to map buffer for CPU-access"}));
        }

        auto map = mmap(0, size, access_mode, MAP_SHARED, drm_fd, map_request.offset);
        if (map == MAP_FAILED)
        {
            BOOST_THROW_EXCEPTION((
                std::system_error{
                    errno,
                    std::system_category(),
                    "Failed to mmap() buffer"}));
        }

        return map;
    }

    mir::Fd const drm_fd;
    uint32_t const width_;
    uint32_t const height_;
    uint32_t const pitch_;
    uint32_t const gem_handle;
    size_t const size;
};

mgg::DumbFB::DumbFB(mir::Fd const& drm_fd, bool supports_modifiers, mir::geometry::Size const& size)
    : DumbFB(drm_fd, supports_modifiers, DumbBuffer::create_dumb_buffer(drm_fd, size))
{
}

mgg::DumbFB::~DumbFB()
{
    drmModeRmFB(drm_fd, fb_id);
}

mgg::DumbFB::DumbFB(
    mir::Fd drm_fd,
    bool supports_modifiers,
    std::unique_ptr<DumbBuffer> buffer)
    : drm_fd{std::move(drm_fd)},
      fb_id{fb_id_for_buffer(this->drm_fd, supports_modifiers, *buffer)},
      buffer{std::move(buffer)}
{
}

auto mgg::DumbFB::map_writeable() -> std::unique_ptr<mir::renderer::software::Mapping<unsigned char>>
{
    return buffer->map_writeable();
}

mgg::DumbFB::operator uint32_t() const
{
    return fb_id;
}

auto mgg::DumbFB::fb_id_for_buffer(mir::Fd const &drm_fd, bool supports_modifiers, DumbBuffer const& buf) -> uint32_t
{
    uint32_t fb_id;
    uint32_t const pitches[4] = { buf.pitch(), 0, 0, 0 };
    uint32_t const handles[4] = { buf.handle(), 0, 0, 0 };
    uint32_t const offsets[4] = { 0, 0, 0, 0 };
    uint64_t const modifiers[4] = { DRM_FORMAT_MOD_LINEAR, 0, 0, 0 };
    if (supports_modifiers)
    {
        if (auto err = drmModeAddFB2WithModifiers(
            drm_fd,
            buf.width(),
            buf.height(),
            DRM_FORMAT_XRGB8888,
            handles,
            pitches,
            offsets,
            modifiers,
            &fb_id,
            DRM_MODE_FB_MODIFIERS))
        {
            BOOST_THROW_EXCEPTION((std::system_error{
                -err,
                std::system_category(),
                "Failed to create DRM framebuffer from CPU-accessible buffer"}));
        }
    }
    else
    {
        if (auto err = drmModeAddFB2(
            drm_fd,
            buf.width(),
            buf.height(),
            DRM_FORMAT_XRGB8888,
            handles,
            pitches,
            offsets,
            &fb_id,
            0))
        {
            BOOST_THROW_EXCEPTION((std::system_error{
                -err,
                std::system_category(),
                "Failed to create DRM framebuffer from CPU-accessible buffer"}));
        }
    
    
    }
    return fb_id;
}
