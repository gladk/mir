/*
 * Copyright © 2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "mir/graphics/graphic_buffer_allocator.h"
#include "mir/frontend/buffer_sink.h"
#include "buffer_map.h"
#include <boost/throw_exception.hpp>
#include <algorithm>

namespace mc = mir::compositor;
namespace mf = mir::frontend;
namespace mg = mir::graphics;

namespace mir
{
namespace compositor
{
enum class BufferMap::Owner
{
    server,
    client
};
}
}

mc::BufferMap::BufferMap(
    std::shared_ptr<mf::BufferSink> const& sink,
    std::shared_ptr<mg::GraphicBufferAllocator> const& allocator) :
    sink(sink),
    allocator(allocator)
{
}

mg::BufferID mc::BufferMap::add_buffer(mg::BufferProperties const& properties)
{
    std::unique_lock<decltype(mutex)> lk(mutex);
    auto buffer = allocator->alloc_buffer(properties);
    buffers[buffer->id()] = {buffer, Owner::client};
    sink->add_buffer(*buffer);
    return buffer->id();
}

void mc::BufferMap::remove_buffer(mg::BufferID id)
{
    std::unique_lock<decltype(mutex)> lk(mutex);
    auto it = checked_buffers_find(id, lk);
    sink->remove_buffer(*it->second.buffer);
    buffers.erase(it); 
}

void mc::BufferMap::send_buffer(mg::BufferID id)
{
    std::unique_lock<decltype(mutex)> lk(mutex);
    auto it = buffers.find(id);
    if (it != buffers.end())
    {
        auto buffer = it->second.buffer;
        it->second.owner = Owner::client;
        lk.unlock();
        sink->update_buffer(*buffer);
    }
}

void mc::BufferMap::receive_buffer(graphics::BufferID id)
{
    std::unique_lock<decltype(mutex)> lk(mutex);
    auto it = buffers.find(id);
    if (it != buffers.end())
        it->second.owner = Owner::server;
}

std::shared_ptr<mg::Buffer>& mc::BufferMap::operator[](mg::BufferID id)
{
    std::unique_lock<decltype(mutex)> lk(mutex);
    return checked_buffers_find(id, lk)->second.buffer;
}

mc::BufferMap::Map::iterator mc::BufferMap::checked_buffers_find(
    mg::BufferID id, std::unique_lock<std::mutex> const&)
{
    auto it = buffers.find(id);
    if (it == buffers.end())
        BOOST_THROW_EXCEPTION(std::logic_error("cannot find buffer by id"));
    return it;
}

size_t mc::BufferMap::client_owned_buffer_count() const
{
    std::unique_lock<decltype(mutex)> lk(mutex);
    return std::count_if(buffers.begin(), buffers.end(),
        [](std::pair<mg::BufferID, MapEntry> const& entry) { return entry.second.owner == Owner::client; });
}
