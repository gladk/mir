/*
 * Copyright © 2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
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
 * Authored by: Cemil Azizoglu <cemil.azizoglu@canonical.com>
 *
 */

#include "shm_file.h"
#include "shm_buffer.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <boost/throw_exception.hpp>

#include <stdexcept>

#include <string.h>

namespace mgx = mir::graphics::X;
namespace geom = mir::geometry;

mgx::ShmBuffer::ShmBuffer(
    std::shared_ptr<ShmFile> const& shm_file,
    geom::Size const& size,
    MirPixelFormat const& pixel_format)
    : shm_file{shm_file},
      size_{size},
      pixel_format_{pixel_format},
      stride_{MIR_BYTES_PER_PIXEL(pixel_format_) * size_.width.as_uint32_t()},
      pixels{shm_file->base_ptr()}
{
}

mgx::ShmBuffer::~ShmBuffer() noexcept
{
}

geom::Size mgx::ShmBuffer::size() const
{
    return size_;
}

geom::Stride mgx::ShmBuffer::stride() const
{
    return stride_;
}

MirPixelFormat mgx::ShmBuffer::pixel_format() const
{
    return pixel_format_;
}

void mgx::ShmBuffer::gl_bind_to_texture()
{
    glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT,
                 size_.width.as_int(), size_.height.as_int(),
                 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE,
                 pixels);
}

std::shared_ptr<MirNativeBuffer> mgx::ShmBuffer::native_buffer_handle() const
{
    auto native_buffer = std::make_shared<MirNativeBuffer>();

    native_buffer->fd_items = 1;
    native_buffer->fd[0] = shm_file->fd();
    native_buffer->stride = stride().as_uint32_t();
    native_buffer->flags = 0;

    auto const& dim = size();
    native_buffer->width = dim.width.as_int();
    native_buffer->height = dim.height.as_int();

    return native_buffer;
}

void mgx::ShmBuffer::write(unsigned char const* data, size_t data_size)
{
    if (data_size != stride_.as_uint32_t()*size().height.as_uint32_t())
        BOOST_THROW_EXCEPTION(std::logic_error("Size is not equal to number of pixels in buffer"));
    memcpy(pixels, data, data_size);
}

void mgx::ShmBuffer::read(std::function<void(unsigned char const*)> const& do_with_pixels)
{
    do_with_pixels(static_cast<unsigned char const*>(pixels));
}
