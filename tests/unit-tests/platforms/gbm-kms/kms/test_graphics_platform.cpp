/*
 * Copyright © 2015 Canonical Ltd.
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

#include "mir/graphics/platform.h"
#include "mir/graphics/graphic_buffer_allocator.h"
#include "mir/graphics/buffer_properties.h"
#include "mir/test/doubles/mock_egl.h"
#include "mir/test/doubles/mock_gl.h"
#include "mir/test/doubles/mock_option.h"
#include "mir/test/doubles/null_emergency_cleanup.h"
#include "src/server/report/null_report_factory.h"
#include "mir/test/doubles/stub_console_services.h"
#include "mir/options/program_option.h"
#include "mir/test/doubles/mock_drm.h"
#include "mir/test/doubles/mock_gbm.h"
#include "mir_test_framework/udev_environment.h"
#include "src/platforms/gbm-kms/server/kms/platform.h"
#include "src/platforms/gbm-kms/server/kms/quirks.h"

#include "mir/logging/dumb_console_logger.h"

#include <gtest/gtest.h>

namespace mg = mir::graphics;
namespace mgg = mg::gbm;
namespace ml = mir::logging;
namespace geom = mir::geometry;
namespace mtd = mir::test::doubles;
namespace mo = mir::options;
namespace mtf = mir_test_framework;

class GraphicsPlatform : public ::testing::Test
{
public:
    GraphicsPlatform() : logger(std::make_shared<ml::DumbConsoleLogger>())
    {
        using namespace testing;

        ON_CALL(mock_gbm, gbm_bo_get_width(_))
        .WillByDefault(Return(320));

        ON_CALL(mock_gbm, gbm_bo_get_height(_))
        .WillByDefault(Return(240));

        // FIXME: This format needs to match Mesa's first supported pixel
        //        format or tests will fail. The coupling is presently loose.
        ON_CALL(mock_gbm, gbm_bo_get_format(_))
        .WillByDefault(Return(GBM_FORMAT_ARGB8888));

        ON_CALL(mock_egl, eglChooseConfig(_,_,_,1,_))
            .WillByDefault(DoAll(SetArgPointee<2>(mock_egl.fake_configs[0]),
                                 SetArgPointee<4>(1),
                                 Return(EGL_TRUE)));

        ON_CALL(mock_egl, eglGetConfigAttrib(_, mock_egl.fake_configs[0], EGL_NATIVE_VISUAL_ID, _))
            .WillByDefault(
                DoAll(
                    SetArgPointee<3>(GBM_FORMAT_XRGB8888),
                    Return(EGL_TRUE)));

        mock_egl.provide_egl_extensions();
        mock_gl.provide_gles_extensions();


        fake_devices.add_standard_device("standard-drm-devices");
    }

    std::shared_ptr<mg::Platform> create_platform()
    {
        mir::udev::Context ctx;
        // Caution: non-local state!
        // This works because standard-drm-devices contains a udev device with 226:0 and devnode /dev/dri/card0
        auto device = ctx.char_device_from_devnum(makedev(226, 0));
       
        return std::make_shared<mgg::Platform>(
            *device,
            mir::report::null_display_report(),
            std::make_shared<mtd::StubConsoleServices>(),
            *std::make_shared<mtd::NullEmergencyCleanup>(),
            mgg::BypassOption::allowed);
    }

    std::shared_ptr<ml::Logger> logger;

    ::testing::NiceMock<mtd::MockEGL> mock_egl;
    ::testing::NiceMock<mtd::MockGL> mock_gl;
    ::testing::NiceMock<mtd::MockDRM> mock_drm;
    ::testing::NiceMock<mtd::MockGBM> mock_gbm;
    mtf::UdevEnvironment fake_devices;
};

#include "../../test_graphics_platform.h"
