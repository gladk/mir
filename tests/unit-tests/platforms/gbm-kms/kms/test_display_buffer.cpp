/*
 * Copyright © 2014 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "mir/test/doubles/null_emergency_cleanup.h"
#include "src/server/report/null_report_factory.h"
#include "src/platforms/gbm-kms/server/kms/platform.h"
#include "src/platforms/gbm-kms/server/kms/display_buffer.h"
#include "mir/graphics/dmabuf_buffer.h"
#include "mir/test/doubles/mock_egl.h"
#include "mir/test/doubles/mock_gl.h"
#include "mir/test/doubles/mock_drm.h"
#include "mir/test/doubles/mock_buffer.h"
#include "mir/test/doubles/mock_gbm.h"
#include "mir/test/doubles/stub_gl_config.h"
#include "mir_test_framework/udev_environment.h"
#include "mir/test/doubles/fake_renderable.h"
#include "mir/graphics/transformation.h"
#include "mock_kms_output.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <gbm.h>

using namespace testing;
using namespace mir;
using namespace std;
using namespace mir::test;
using namespace mir::test::doubles;
using namespace mir_test_framework;
using namespace mir::graphics;
using namespace mir::graphics::gbm;
using mir::report::null_display_report;

namespace
{
class MockDMABufBuffer : public DMABufBuffer
{
public:
    MOCK_CONST_METHOD0(drm_fourcc, uint32_t());
    MOCK_CONST_METHOD0(modifier, std::optional<uint64_t>());
    MOCK_CONST_METHOD0(planes, std::vector<PlaneDescriptor> const&());
    MOCK_CONST_METHOD0(size, geometry::Size());
};
}

class MesaDisplayBufferTest : public Test
{
public:
    int const mock_refresh_rate = 60;

    MesaDisplayBufferTest()
        : identity(1)
        , mock_bypassable_buffer{std::make_shared<NiceMock<MockBuffer>>()}
        , mock_software_buffer{std::make_shared<NiceMock<MockBuffer>>()}
        , fake_bypassable_renderable{
             std::make_shared<FakeRenderable>(display_area)}
        , fake_software_renderable{
             std::make_shared<FakeRenderable>(display_area)}
        , bypass_framebuffer{nullptr}
        , bypassable_list{
            mir::graphics::DisplayElement{
                display_area,
                {
                    {display_area.top_left.x.as_value(), display_area.top_left.y.as_value()},
                    {display_area.size.width.as_value(), display_area.size.height.as_value()}
                },
                bypass_framebuffer}
            }
        , parent_platform{nullptr}
    {
        ON_CALL(mock_egl, eglChooseConfig(_,_,_,1,_))
            .WillByDefault(DoAll(SetArgPointee<2>(mock_egl.fake_configs[0]),
                                 SetArgPointee<4>(1),
                                 Return(EGL_TRUE)));

        mock_egl.provide_egl_extensions();
        mock_gl.provide_gles_extensions();

        fake_bo = reinterpret_cast<gbm_bo*>(123);
        ON_CALL(mock_gbm, gbm_surface_lock_front_buffer(_))
            .WillByDefault(Return(fake_bo));
        fake_handle.u32 = 123;
        ON_CALL(mock_gbm, gbm_bo_get_handle(_))
            .WillByDefault(Return(fake_handle));
        ON_CALL(mock_gbm, gbm_bo_get_stride(_))
            .WillByDefault(Return(456));

        fake_devices.add_standard_device("standard-drm-devices");

        mock_kms_output = std::make_shared<NiceMock<MockKMSOutput>>();
        ON_CALL(*mock_kms_output, set_crtc_thunk(_))
            .WillByDefault(Return(true));
        ON_CALL(*mock_kms_output, schedule_page_flip_thunk(_))
            .WillByDefault(Return(true));
        ON_CALL(*mock_kms_output, max_refresh_rate())
            .WillByDefault(Return(mock_refresh_rate));
        ON_CALL(*mock_kms_output, fb_for(A<gbm_bo*>()))
            .WillByDefault(Return(
                std::shared_ptr<FBHandle const>{
                    reinterpret_cast<FBHandle const*>(0x12ad),
                    [](auto) {}}));
        ON_CALL(*mock_kms_output, fb_for(A<DMABufBuffer const&>()))
            .WillByDefault(Return(
                std::shared_ptr<FBHandle const>{
                    reinterpret_cast<FBHandle const*>(0xe0e0),
                    [](auto) {}}));
        ON_CALL(*mock_kms_output, buffer_requires_migration(_))
            .WillByDefault(Return(false));

        ON_CALL(*mock_bypassable_buffer, size())
            .WillByDefault(Return(display_area.size));
        ON_CALL(*mock_bypassable_buffer, native_buffer_base())
            .WillByDefault(Return(&mock_dmabuf_buffer));
        fake_bypassable_renderable->set_buffer(mock_bypassable_buffer);

        ON_CALL(mock_drm, drmModeAddFB2WithModifiers(_,_,_,_,_,_,_,_,_,_))
            .WillByDefault(Return(0));

        ON_CALL(*mock_software_buffer, size())
            .WillByDefault(Return(display_area.size));
        fake_software_renderable->set_buffer(mock_software_buffer);
    }

protected:
    int const width{56};
    int const height{78};
    mir::geometry::Rectangle const display_area{{12,34}, {width,height}};
    glm::mat2 const identity;
    mir::Fd drm_fd{mir::IntOwnedFd{5}};
    NiceMock<MockGBM> mock_gbm;
    NiceMock<MockEGL> mock_egl;
    NiceMock<MockGL>  mock_gl;
    NiceMock<MockDRM> mock_drm; 
    std::shared_ptr<MockBuffer> mock_bypassable_buffer;
    NiceMock<MockDMABufBuffer> mock_dmabuf_buffer;
    std::shared_ptr<MockBuffer> mock_software_buffer;
    std::shared_ptr<FakeRenderable> fake_bypassable_renderable;
    std::shared_ptr<FakeRenderable> fake_software_renderable;
    gbm_bo*           fake_bo;
    gbm_bo_handle     fake_handle;
    UdevEnvironment   fake_devices;
    std::shared_ptr<MockKMSOutput> mock_kms_output;
    StubGLConfig gl_config;
    std::shared_ptr<mir::graphics::Framebuffer> const bypass_framebuffer;
    std::vector<mir::graphics::DisplayElement> const bypassable_list;
    std::shared_ptr<mir::graphics::DisplayPlatform> const parent_platform;
};

TEST_F(MesaDisplayBufferTest, unrotated_view_area_is_untouched)
{
    graphics::gbm::DisplayBuffer db(
        parent_platform,
        drm_fd,
        graphics::gbm::BypassOption::allowed,
        null_display_report(),
        {mock_kms_output},
        display_area,
        identity);

    EXPECT_EQ(display_area, db.view_area());
}

TEST_F(MesaDisplayBufferTest, predictive_bypass_is_throttled)
{
    graphics::gbm::DisplayBuffer db(
        parent_platform,
        drm_fd,
        graphics::gbm::BypassOption::allowed,
        null_display_report(),
        {mock_kms_output},
        display_area,
        identity);

    for (int frame = 0; frame < 5; ++frame)
    {
        ASSERT_TRUE(db.overlay(bypassable_list));
        db.post();

        // Cast to a simple int type so that test failures are readable
        int milliseconds_per_frame = 1000 / mock_refresh_rate;
        ASSERT_THAT(db.recommended_sleep().count(),
                    Ge(milliseconds_per_frame/2));
    }
}

TEST_F(MesaDisplayBufferTest, frames_requiring_gl_are_not_throttled)
{
    std::vector<mir::graphics::DisplayElement> const non_bypassable_list{
        {
            {{12, 34}, {1, 1}},
            {{12, 34}, {1, 1}},
            nullptr
        }
    };

    graphics::gbm::DisplayBuffer db(
        parent_platform,
        drm_fd,
        graphics::gbm::BypassOption::allowed,
        null_display_report(),
        {mock_kms_output},
        display_area,
        identity);

    for (int frame = 0; frame < 5; ++frame)
    {
        ASSERT_FALSE(db.overlay(non_bypassable_list));
        db.post();

        // Cast to a simple int type so that test failures are readable
        ASSERT_EQ(0, db.recommended_sleep().count());
    }
}

TEST_F(MesaDisplayBufferTest, bypass_buffer_only_referenced_once_by_db)
{
    graphics::gbm::DisplayBuffer db(
        parent_platform,
        drm_fd,
        graphics::gbm::BypassOption::allowed,
        null_display_report(),
        {mock_kms_output},
        display_area,
        identity);

    auto original_count = mock_bypassable_buffer.use_count();

    EXPECT_TRUE(db.overlay(bypassable_list));
    EXPECT_EQ(original_count+1, mock_bypassable_buffer.use_count());

    db.post();

    // Bypass buffer still held by DB only one ref above the original
    EXPECT_EQ(original_count+1, mock_bypassable_buffer.use_count());
}

TEST_F(MesaDisplayBufferTest, untransformed_with_bypassable_list_can_bypass)
{
    graphics::gbm::DisplayBuffer db(
        parent_platform,
        drm_fd,
        graphics::gbm::BypassOption::allowed,
        null_display_report(),
        {mock_kms_output},
        display_area,
        identity);

    EXPECT_TRUE(db.overlay(bypassable_list));
}

namespace
{
template<typename T>
auto fake_shared_ptr(intptr_t value) -> std::shared_ptr<T>
{
    return std::shared_ptr<T>{reinterpret_cast<T*>(value), [](auto) {}};
}
}

TEST_F(MesaDisplayBufferTest, failed_bypass_falls_back_gracefully)
{  // Regression test for LP: #1398296
    EXPECT_CALL(*mock_kms_output, fb_for(A<gbm_bo*>()))
        .WillOnce(Return(fake_shared_ptr<FBHandle>(0xaabb)));  // During the DisplayBuffer constructor
    EXPECT_CALL(*mock_kms_output, fb_for(A<DMABufBuffer const&>()))
        .WillOnce(Return(nullptr)) // Fail first bypass attempt
        .WillOnce(Return(fake_shared_ptr<FBHandle>(0xbbcc))); // Succeed second bypass attempt

    graphics::gbm::DisplayBuffer db(
        parent_platform,
        drm_fd,
        graphics::gbm::BypassOption::allowed,
        null_display_report(),
        {mock_kms_output},
        display_area,
        identity);

    EXPECT_FALSE(db.overlay(bypassable_list));
    // And then we recover. DRM finds enough resources to AddFB ...
    EXPECT_TRUE(db.overlay(bypassable_list));
}

/*
TEST_F(MesaDisplayBufferTest, skips_bypass_because_of_lagging_resize)
{  // Another regression test for LP: #1398296
    auto fullscreen = std::make_shared<FakeRenderable>(display_area);
    auto nonbypassable = std::make_shared<testing::NiceMock<MockBuffer>>();
    ON_CALL(*nonbypassable, native_buffer_base())
        .WillByDefault(Return(&mock_dmabuf_buffer));
    ON_CALL(*nonbypassable, size())
        .WillByDefault(Return(mir::geometry::Size{12,34}));

    fullscreen->set_buffer(nonbypassable);
    graphics::RenderableList list{fullscreen};

    graphics::gbm::DisplayBuffer db(
        parent_platform,
        graphics::gbm::BypassOption::allowed,
        null_display_report(),
        {mock_kms_output},
        make_output_surface(),
        display_area,
        identity);

    EXPECT_FALSE(db.overlay(list));
}
*/

TEST_F(MesaDisplayBufferTest, rotated_cannot_bypass)
{
    graphics::gbm::DisplayBuffer db(
        parent_platform,
        drm_fd,
        graphics::gbm::BypassOption::allowed,
        null_display_report(),
        {mock_kms_output},
        display_area,
        transformation(mir_orientation_right));

    EXPECT_FALSE(db.overlay(bypassable_list));
}

/*
TEST_F(MesaDisplayBufferTest, fullscreen_software_buffer_cannot_bypass)
{
    graphics::RenderableList const list{fake_software_renderable};

    // Passes the bypass candidate test:
    EXPECT_EQ(fake_software_renderable->buffer()->size(), display_area.size);

    graphics::gbm::DisplayBuffer db(
        parent_platform,
        graphics::gbm::BypassOption::allowed,
        null_display_report(),
        {mock_kms_output},
        make_output_surface(),
        display_area,
        identity);

    EXPECT_FALSE(db.overlay(list));
}

TEST_F(MesaDisplayBufferTest, fullscreen_software_buffer_not_used_as_gbm_bo)
{   // Also checks it doesn't crash (LP: #1493721)
    graphics::RenderableList const list{fake_software_renderable};

    // Passes the bypass candidate test:
    EXPECT_EQ(fake_software_renderable->buffer()->size(), display_area.size);

    graphics::gbm::DisplayBuffer db(
        parent_platform,
        graphics::gbm::BypassOption::allowed,
        null_display_report(),
        {mock_kms_output},
        make_output_surface(),
        display_area,
        identity);

    // If you find yourself using gbm_ functions on a Shm buffer then you're
    // asking for a crash (LP: #1493721) ...
    EXPECT_CALL(mock_gbm, gbm_bo_get_user_data(_)).Times(0);
    db.overlay(list);
}
*/

TEST_F(MesaDisplayBufferTest, transformation_not_implemented_internally)
{
    glm::mat2 const rotate_left = transformation(mir_orientation_left);

    graphics::gbm::DisplayBuffer db(
        parent_platform,
        drm_fd,
        graphics::gbm::BypassOption::allowed,
        null_display_report(),
        {mock_kms_output},
        display_area,
        rotate_left);

    EXPECT_EQ(rotate_left, db.transformation());
}

/*
TEST_F(MesaDisplayBufferTest, skips_bypass_because_of_incompatible_list)
{
    graphics::RenderableList list{
        std::make_shared<FakeRenderable>(display_area),
        std::make_shared<FakeRenderable>(geometry::Rectangle{{12, 34}, {1, 1}})
    };

    graphics::gbm::DisplayBuffer db(
        parent_platform,
        graphics::gbm::BypassOption::allowed,
        null_display_report(),
        {mock_kms_output},
        make_output_surface(),
        display_area,
        identity);

    EXPECT_FALSE(db.overlay(list));
}
*/
