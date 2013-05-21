/*
 * Copyright © 2012 Canonical Ltd.
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

#include "src/server/graphics/android/buffer.h"
#include "mir_test_doubles/mock_alloc_adaptor.h"

#include <hardware/gralloc.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

namespace mc = mir::compositor;
namespace mg = mir::graphics;
namespace mga = mir::graphics::android;
namespace geom = mir::geometry;
namespace mtd = mir::test::doubles;

class AndroidGraphicBufferBasic : public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        using namespace testing;
        mock_buffer_handle = std::make_shared<ANativeWindowBuffer>();
        mock_buffer_handle->width = 44;
        mock_buffer_handle->height = 45;
        mock_buffer_handle->stride = 46;
        mock_buffer_handle->format = HAL_PIXEL_FORMAT_RGBA8888;

        mock_alloc_device = std::make_shared<NiceMock<mtd::MockAllocAdaptor>>();
        ON_CALL(*mock_alloc_device, alloc_buffer(_,_,_))
            .WillByDefault(Return(mock_buffer_handle));
 
        default_use = mga::BufferUsage::use_hardware;
        pf = geom::PixelFormat::abgr_8888;
        size = geom::Size{geom::Width{300}, geom::Height{200}};
    }

    std::shared_ptr<mtd::MockAllocAdaptor> mock_alloc_device;
    std::shared_ptr<mtd::MockBufferHandle> mock_buffer_handle;
    geom::PixelFormat pf;
    geom::Size size;
    mga::BufferUsage default_use;
};


TEST_F(AndroidGraphicBufferBasic, basic_allocation_uses_alloc_device)
{
    using namespace testing;

    EXPECT_CALL(*mock_alloc_device, alloc_buffer(size, pf, default_use));
    mga::Buffer buffer(mock_alloc_device, size, pf, default_use);
}

TEST_F(AndroidGraphicBufferBasic, size_query_test)
{
    using namespace testing;

    EXPECT_CALL(*mock_alloc_device, alloc_buffer(_,_,_));
    mga::Buffer buffer(mock_alloc_device, size, pf, default_use);

    geom::Size expected_size{geom::Width{mock_buffer_handle->width},
                             geom::Height{mock_buffer_handle->height}};
    EXPECT_EQ(expected_size, buffer.size());
}

TEST_F(AndroidGraphicBufferBasic, format_query_test)
{
    using namespace testing;

    mga::Buffer buffer(mock_alloc_device, size, pf, default_use);
    EXPECT_EQ(geom::PixelFormat::abgr_8888, buffer.pixel_format());
}

TEST_F(AndroidGraphicBufferBasic, queries_native_window_for_native_handle)
{
    using namespace testing;

    mga::Buffer buffer(mock_alloc_device, size, pf, default_use);
    EXPECT_EQ(mock_buffer_handle, buffer.native_buffer_handle());
}

TEST_F(AndroidGraphicBufferBasic, queries_native_window_for_stride)
{
    using namespace testing;

    geom::Stride expected_stride{mock_buffer_handle->stride};
    mga::Buffer buffer(mock_alloc_device, size, pf, default_use);
    EXPECT_EQ(expected_stride, buffer.stride());
}
