/*
 * Copyright © 2013 Canonical Ltd.
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

#include "mir/graphics/android/sync_fence.h"
#include "src/platform/graphics/android/framebuffer_bundle.h"
#include "src/platform/graphics/android/hwc_device.h"
#include "src/platform/graphics/android/hwc_layerlist.h"
#include "src/platform/graphics/android/gl_context.h"
#include "mir_test_doubles/mock_hwc_composer_device_1.h"
#include "mir_test_doubles/mock_android_native_buffer.h"
#include "mir_test_doubles/mock_hwc_vsync_coordinator.h"
#include "mir_test_doubles/stub_renderable.h"
#include "mir_test_doubles/mock_framebuffer_bundle.h"
#include "mir_test_doubles/stub_buffer.h"
#include "mir_test_doubles/mock_hwc_device_wrapper.h"
#include "mir_test/fake_shared.h"
#include "hwc_struct_helpers.h"
#include "mir_test_doubles/mock_swapping_gl_context.h"
#include "mir_test_doubles/stub_swapping_gl_context.h"
#include "mir_test_doubles/stub_renderable_list_compositor.h"
#include "mir_test_doubles/mock_renderable_list_compositor.h"
#include "mir_test_doubles/mock_renderable.h"
#include "mir_test_doubles/stub_renderable.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>

namespace mg=mir::graphics;
namespace mga=mir::graphics::android;
namespace mtd=mir::test::doubles;
namespace geom=mir::geometry;
namespace mt=mir::test;

namespace
{
struct MockFileOps : public mga::SyncFileOps
{
    MOCK_METHOD3(ioctl, int(int,int,void*));
    MOCK_METHOD1(dup, int(int));
    MOCK_METHOD1(close, int(int));
};

void fill_hwc_layer(
    hwc_layer_1_t& layer,
    hwc_rect_t* visible_rect,
    geom::Rectangle const& position,
    mg::Buffer* buffer,
    int type, int flags)
{
    //TODO: clean up so we take a Buffer&
    if (buffer)
    {
        layer.handle = buffer->native_buffer_handle()->handle();
        *visible_rect = {0, 0, buffer->size().width.as_int(), buffer->size().height.as_int()};
    }
    else
    {
        layer.handle = nullptr;
        *visible_rect = {0, 0, 0, 0};
    }
    layer.compositionType = type;
    layer.hints = 0;
    layer.flags = flags;
    layer.transform = 0;
    layer.blending = HWC_BLENDING_NONE;
    layer.sourceCrop = *visible_rect;
    layer.displayFrame = {
        position.top_left.x.as_int(),
        position.top_left.y.as_int(),
        position.bottom_right().x.as_int(),
        position.bottom_right().y.as_int()
    };
    layer.visibleRegionScreen = {1, visible_rect};
    layer.acquireFenceFd = -1;
    layer.releaseFenceFd = -1;
    layer.planeAlpha = std::numeric_limits<decltype(hwc_layer_1_t::planeAlpha)>::max();
}

struct HwcDevice : public ::testing::Test
{
    HwcDevice() :
        mock_native_buffer1(std::make_shared<testing::NiceMock<mtd::MockAndroidNativeBuffer>>(size1)),
        mock_native_buffer2(std::make_shared<testing::NiceMock<mtd::MockAndroidNativeBuffer>>(size2)),
        mock_native_buffer3(std::make_shared<testing::NiceMock<mtd::MockAndroidNativeBuffer>>(size3)),
        mock_device(std::make_shared<testing::NiceMock<mtd::MockHWCComposerDevice1>>()),
        mock_vsync(std::make_shared<testing::NiceMock<mtd::MockVsyncCoordinator>>()),
        mock_file_ops(std::make_shared<MockFileOps>()),
        stub_buffer1(std::make_shared<mtd::StubBuffer>(mock_native_buffer1, size1)),
        stub_buffer2(std::make_shared<mtd::StubBuffer>(mock_native_buffer2, size2)),
        stub_fb_buffer(std::make_shared<mtd::StubBuffer>(mock_native_buffer3, size3)),
        stub_renderable1(std::make_shared<mtd::StubRenderable>(stub_buffer1, position1)),
        stub_renderable2(std::make_shared<mtd::StubRenderable>(stub_buffer2, position2)),
        mock_hwc_device_wrapper(std::make_shared<testing::NiceMock<mtd::MockHWCDeviceWrapper>>()),
        stub_context{stub_fb_buffer},
        renderlist({stub_renderable1, stub_renderable2})
    {
        fill_hwc_layer(layer, &comp_rect, position1, stub_buffer1.get(), HWC_FRAMEBUFFER, 0);
        fill_hwc_layer(layer2, &comp2_rect, position2, stub_buffer2.get(), HWC_FRAMEBUFFER, 0);
        fill_hwc_layer(target_layer, &target_rect, fb_position, stub_fb_buffer.get(), HWC_FRAMEBUFFER_TARGET, 0);
        fill_hwc_layer(skip_layer, &skip_rect, fb_position, stub_fb_buffer.get(), HWC_FRAMEBUFFER, HWC_SKIP_LAYER);

        //FIXME: remove unset_layers, they could be set
        fill_hwc_layer(
            unset_skip_layer, &unset_skip_rect, {{0,0},{0,0}}, nullptr, HWC_FRAMEBUFFER, HWC_SKIP_LAYER);
        fill_hwc_layer(
            unset_target_layer, &unset_target_rect, {{0,0},{0,0}}, nullptr, HWC_FRAMEBUFFER_TARGET, 0);

        set_all_layers_to_overlay = [&](hwc_display_contents_1_t& contents)
        {
            for(auto i = 0u; i < contents.numHwLayers - 1; i++) //-1 because the last layer is the target
                contents.hwLayers[i].compositionType = HWC_OVERLAY;
        };

    }

    hwc_rect_t skip_rect;
    hwc_rect_t target_rect;
    hwc_rect_t unset_skip_rect;
    hwc_rect_t unset_target_rect;
    hwc_rect_t comp_rect;
    hwc_rect_t comp2_rect;
    hwc_layer_1_t skip_layer;
    hwc_layer_1_t target_layer;
    hwc_layer_1_t unset_skip_layer;
    hwc_layer_1_t unset_target_layer;
    hwc_layer_1_t layer;
    hwc_layer_1_t layer2;

    geom::Size const size1{111, 222};
    geom::Size const size2{333, 444};
    geom::Size const size3{555, 666};
    geom::Rectangle const position1{{44,1},size1};
    geom::Rectangle const position2{{92,293},size2};
    geom::Rectangle const fb_position{{0,0},size3};
    mtd::StubRenderableListCompositor stub_compositor;
    std::function<void(hwc_display_contents_1_t&)> set_all_layers_to_overlay;

    std::shared_ptr<mtd::MockAndroidNativeBuffer> const mock_native_buffer1;
    std::shared_ptr<mtd::MockAndroidNativeBuffer> const mock_native_buffer2;
    std::shared_ptr<mtd::MockAndroidNativeBuffer> const mock_native_buffer3;
    std::shared_ptr<mtd::MockHWCComposerDevice1> const mock_device;
    std::shared_ptr<mtd::MockVsyncCoordinator> const mock_vsync;
    std::shared_ptr<MockFileOps> const mock_file_ops;
    std::shared_ptr<mtd::StubBuffer> const stub_buffer1;
    std::shared_ptr<mtd::StubBuffer> const stub_buffer2;
    std::shared_ptr<mtd::StubBuffer> const stub_fb_buffer;
    std::shared_ptr<mtd::StubRenderable> const stub_renderable1;
    std::shared_ptr<mtd::StubRenderable> const stub_renderable2;
    std::shared_ptr<mtd::MockHWCDeviceWrapper> const mock_hwc_device_wrapper;
    mtd::StubSwappingGLContext stub_context;
    mg::RenderableList renderlist;
};
}

TEST_F(HwcDevice, prepares_a_skip_and_target_layer_by_default)
{
    using namespace testing;
    std::list<hwc_layer_1_t*> expected_list
    {
        &unset_skip_layer,
        &unset_target_layer
    };

    EXPECT_CALL(*mock_hwc_device_wrapper, prepare(MatchesList(expected_list)))
        .Times(1);

    mga::HwcDevice device(mock_device, mock_hwc_device_wrapper, mock_vsync, mock_file_ops);
    device.post_gl(stub_context);
}

TEST_F(HwcDevice, calls_backup_compositor_when_overlay_rejected)
{
    using namespace testing;
    mtd::MockRenderableListCompositor mock_compositor;

    mg::RenderableList expected_renderable_list({
        stub_renderable2
    });

    std::list<hwc_layer_1_t*> expected_prepare_list
    {
        &layer,
        &layer2,
        &unset_target_layer
    };

    Sequence seq;
    EXPECT_CALL(*mock_hwc_device_wrapper, prepare(MatchesList(expected_prepare_list)))
        .InSequence(seq)
        .WillOnce(Invoke([&](hwc_display_contents_1_t& contents)
        {
            ASSERT_EQ(contents.numHwLayers, 3);
            contents.hwLayers[0].compositionType = HWC_OVERLAY;
            contents.hwLayers[1].compositionType = HWC_FRAMEBUFFER;
            contents.hwLayers[2].compositionType = HWC_FRAMEBUFFER_TARGET;
        }));

    EXPECT_CALL(mock_compositor, render(expected_renderable_list,Ref(stub_context)))
        .InSequence(seq);

    mga::HwcDevice device(mock_device, mock_hwc_device_wrapper, mock_vsync, mock_file_ops);
    EXPECT_TRUE(device.post_overlays(stub_context, renderlist, mock_compositor));
}

TEST_F(HwcDevice, resets_layers_when_prepare_gl_called)
{
    using namespace testing;
    std::list<hwc_layer_1_t*> expected_list1
    {
        &layer,
        &layer2,
        &unset_target_layer
    };

    std::list<hwc_layer_1_t*> expected_list2
    {
        &unset_skip_layer,
        &unset_target_layer
    };

    Sequence seq;
    EXPECT_CALL(*mock_hwc_device_wrapper, prepare(MatchesList(expected_list1)))
        .InSequence(seq);
    EXPECT_CALL(*mock_hwc_device_wrapper, prepare(MatchesList(expected_list2)))
        .InSequence(seq);
    mga::HwcDevice device(mock_device, mock_hwc_device_wrapper, mock_vsync, mock_file_ops);

    EXPECT_TRUE(device.post_overlays(stub_context, renderlist, stub_compositor));
    device.post_gl(stub_context);
}

TEST_F(HwcDevice, sets_and_updates_fences)
{
    using namespace testing;
    int fb_release_fence = 94;
    int hwc_retire_fence = 74;
    auto set_fences_fn = [&](hwc_display_contents_1_t& contents)
    {
        ASSERT_EQ(contents.numHwLayers, 2);
        contents.hwLayers[1].releaseFenceFd = fb_release_fence;
        contents.retireFenceFd = hwc_retire_fence;
    };

    std::list<hwc_layer_1_t*> expected_list
    {
        &skip_layer,
        &target_layer
    };

    Sequence seq;
    EXPECT_CALL(*mock_hwc_device_wrapper, set(MatchesList(expected_list)))
        .InSequence(seq)
        .WillOnce(Invoke(set_fences_fn));
    EXPECT_CALL(*mock_native_buffer3, update_fence(fb_release_fence))
        .InSequence(seq);
    EXPECT_CALL(*mock_file_ops, close(hwc_retire_fence))
        .InSequence(seq);

    mga::HwcDevice device(mock_device, mock_hwc_device_wrapper, mock_vsync, mock_file_ops);
    device.post_gl(stub_context);
}

TEST_F(HwcDevice, sets_proper_overlay_lists)
{
    using namespace testing;
    int overlay_acquire_fence1 = 80;
    int overlay_acquire_fence2 = 81;
    int fb_acquire_fence = 82;
    int release_fence1 = 381;
    int release_fence2 = 382;
    int release_fence3 = 383;

    auto set_fences_fn = [&](hwc_display_contents_1_t& contents)
    {
        ASSERT_EQ(contents.numHwLayers, 3);
        contents.hwLayers[0].releaseFenceFd = release_fence1;
        contents.hwLayers[1].releaseFenceFd = release_fence2;
        contents.hwLayers[2].releaseFenceFd = release_fence3;
        contents.retireFenceFd = -1;
    };

    layer.compositionType = HWC_OVERLAY;
    layer.acquireFenceFd = overlay_acquire_fence1;

    layer2.compositionType = HWC_OVERLAY;
    layer2.acquireFenceFd = overlay_acquire_fence2;

    //should be -1
    target_layer.acquireFenceFd = fb_acquire_fence;

    std::list<hwc_layer_1_t*> expected_list
    {
        &layer,
        &layer2,
        &target_layer
    };

    mga::HwcDevice device(mock_device, mock_hwc_device_wrapper, mock_vsync, mock_file_ops);

    Sequence seq; 
    EXPECT_CALL(*mock_hwc_device_wrapper, prepare(_))
        .InSequence(seq)
        .WillOnce(Invoke(set_all_layers_to_overlay));

    //copy all fb fences for OVERLAY or FRAMEBUFFER_TARGET in preparation of set
    EXPECT_CALL(*mock_native_buffer1, copy_fence())
        .InSequence(seq)
        .WillOnce(Return(overlay_acquire_fence1));
    EXPECT_CALL(*mock_native_buffer2, copy_fence())
        .InSequence(seq)
        .WillOnce(Return(overlay_acquire_fence2));
    EXPECT_CALL(*mock_native_buffer3, copy_fence())
        .InSequence(seq)
        .WillOnce(Return(fb_acquire_fence));

    //set
    EXPECT_CALL(*mock_hwc_device_wrapper, set(MatchesList(expected_list)))
        .InSequence(seq)
        .WillOnce(Invoke(set_fences_fn));
    EXPECT_CALL(*mock_native_buffer1, update_fence(release_fence1))
        .InSequence(seq);
    EXPECT_CALL(*mock_native_buffer2, update_fence(release_fence2))
        .InSequence(seq);
    EXPECT_CALL(*mock_native_buffer3, update_fence(release_fence3))
        .InSequence(seq);

    EXPECT_TRUE(device.post_overlays(stub_context, renderlist, stub_compositor));
}

TEST_F(HwcDevice, discards_second_set_if_all_overlays_and_nothing_has_changed)
{
    using namespace testing;

    ON_CALL(*mock_hwc_device_wrapper, prepare(_))
        .WillByDefault(Invoke(set_all_layers_to_overlay));
    EXPECT_CALL(*mock_hwc_device_wrapper, set(_))
        .Times(1);

    mga::HwcDevice device(mock_device, mock_hwc_device_wrapper, mock_vsync, mock_file_ops);
    EXPECT_TRUE(device.post_overlays(stub_context, renderlist, stub_compositor));
    EXPECT_FALSE(device.post_overlays(stub_context, renderlist, stub_compositor));
}

TEST_F(HwcDevice, submits_every_time_if_at_least_one_layer_is_gl_rendered)
{
    using namespace testing;
    mga::HwcDevice device(mock_device, mock_hwc_device_wrapper, mock_vsync, mock_file_ops);

    ON_CALL(*mock_hwc_device_wrapper, prepare(_))
        .WillByDefault(Invoke([&](hwc_display_contents_1_t& contents)
        {
            ASSERT_EQ(contents.numHwLayers, 3);
            contents.hwLayers[0].compositionType = HWC_OVERLAY;
            contents.hwLayers[1].compositionType = HWC_FRAMEBUFFER;
            contents.hwLayers[2].compositionType = HWC_FRAMEBUFFER_TARGET;
        }));

    EXPECT_CALL(*mock_hwc_device_wrapper, set(_))
        .Times(2);

    EXPECT_TRUE(device.post_overlays(stub_context, renderlist, stub_compositor));
    EXPECT_TRUE(device.post_overlays(stub_context, renderlist, stub_compositor));
}

TEST_F(HwcDevice, resets_composition_type_with_prepare) //lp:1314399
{
    using namespace testing;
    mg::RenderableList renderlist({stub_renderable1});
    mg::RenderableList renderlist2({stub_renderable2});
    mga::HwcDevice device(mock_device, mock_hwc_device_wrapper, mock_vsync, mock_file_ops);

    std::list<hwc_layer_1_t*> expected_list1 { &layer, &unset_target_layer };
    std::list<hwc_layer_1_t*> expected_list2 { &layer2, &target_layer };

    Sequence seq; 
    EXPECT_CALL(*mock_hwc_device_wrapper, prepare(MatchesList(expected_list1)))
        .InSequence(seq)
        .WillOnce(Invoke(set_all_layers_to_overlay));
    EXPECT_CALL(*mock_hwc_device_wrapper, prepare(MatchesList(expected_list2)))
        .InSequence(seq);

    EXPECT_TRUE(device.post_overlays(stub_context, renderlist, stub_compositor));
    EXPECT_TRUE(device.post_overlays(stub_context, renderlist2, stub_compositor));
}

//note: HWC models overlay layer buffers as owned by the display hardware until a subsequent set.
TEST_F(HwcDevice, owns_overlay_buffers_until_next_set)
{
    using namespace testing;
    EXPECT_CALL(*mock_hwc_device_wrapper, prepare(_))
        .WillOnce(Invoke(set_all_layers_to_overlay))
        .WillOnce(Return());

    mga::HwcDevice device(mock_device, mock_hwc_device_wrapper, mock_vsync, mock_file_ops);

    auto use_count_before = stub_buffer1.use_count();
    EXPECT_TRUE(device.post_overlays(stub_context, {stub_renderable1}, stub_compositor));
    EXPECT_THAT(stub_buffer1.use_count(), Gt(use_count_before));

    EXPECT_TRUE(device.post_overlays(stub_context, {stub_renderable2}, stub_compositor));
    EXPECT_THAT(stub_buffer1.use_count(), Eq(use_count_before));
}

TEST_F(HwcDevice, does_not_own_framebuffer_buffers_past_set)
{
    using namespace testing;
    EXPECT_CALL(*mock_hwc_device_wrapper, prepare(_))
       .WillOnce(Invoke([&](hwc_display_contents_1_t& contents)
        {
            ASSERT_THAT(contents.numHwLayers, Ge(2));
            contents.hwLayers[0].compositionType = HWC_FRAMEBUFFER;
            contents.hwLayers[1].compositionType = HWC_FRAMEBUFFER_TARGET;
        }));

    mga::HwcDevice device(mock_device, mock_hwc_device_wrapper, mock_vsync, mock_file_ops);

    auto use_count_before = stub_buffer1.use_count();
    EXPECT_TRUE(device.post_overlays(stub_context, {stub_renderable1}, stub_compositor));
    EXPECT_THAT(stub_buffer1.use_count(), Eq(use_count_before));
}

TEST_F(HwcDevice, rejects_empty_list)
{
    mga::HwcDevice device(mock_device, mock_hwc_device_wrapper, mock_vsync, mock_file_ops);

    std::list<std::shared_ptr<mg::Renderable>> renderlist{};
    EXPECT_FALSE(device.post_overlays(stub_context, renderlist, stub_compositor));
}

//TODO: we could accept a 90 degree transform
TEST_F(HwcDevice, rejects_list_containing_transformed)
{
    mga::HwcDevice device(mock_device, mock_hwc_device_wrapper, mock_vsync, mock_file_ops);

    auto renderable = std::make_shared<mtd::StubTransformedRenderable>();
    mg::RenderableList renderlist{renderable};
    EXPECT_FALSE(device.post_overlays(stub_context, renderlist, stub_compositor));
}

//TODO: support plane alpha for hwc 1.2 and later
TEST_F(HwcDevice, rejects_list_containing_plane_alpha)
{
    using namespace testing;

    mga::HwcDevice device(mock_device, mock_hwc_device_wrapper, mock_vsync, mock_file_ops);

    mg::RenderableList renderlist{std::make_shared<mtd::PlaneAlphaRenderable>()};
    EXPECT_FALSE(device.post_overlays(stub_context, renderlist, stub_compositor));
}
