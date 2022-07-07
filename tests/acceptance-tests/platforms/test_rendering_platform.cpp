/*
 * Copyright © 2021 Canonical Ltd.
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

#include "test_rendering_platform.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <boost/throw_exception.hpp>

#include "mir/graphics/platform.h"
#include "mir/options/program_option.h"
#include "mir/emergency_cleanup_registry.h"

namespace mg = mir::graphics;

RenderingPlatformTest::RenderingPlatformTest()
{
    GetParam()->setup();
}

RenderingPlatformTest::~RenderingPlatformTest()
{
    GetParam()->teardown();
}

TEST_P(RenderingPlatformTest, has_render_platform_entrypoints)
{
    auto const module = GetParam()->platform_module();

    try
    {
        module->load_function<mg::DescribeModule>(
            "describe_graphics_module",
            MIR_SERVER_GRAPHICS_PLATFORM_VERSION);
    }
    catch(std::runtime_error const& err)
    {
        FAIL()
            << "Failed to find describe_graphics_module (version " << MIR_SERVER_GRAPHICS_PLATFORM_VERSION << "): "
            << err.what();
    }

    try
    {
        module->load_function<mg::CreateRenderPlatform>(
            "create_rendering_platform",
            MIR_SERVER_GRAPHICS_PLATFORM_VERSION);
    }
    catch(std::runtime_error const& err)
    {
        FAIL()
            << "Failed to find create_rendering_platform (version " << MIR_SERVER_GRAPHICS_PLATFORM_VERSION << "): "
            << err.what();
    }

    try
    {
        module->load_function<mg::PlatformProbe>(
            "probe_rendering_platform",
            MIR_SERVER_GRAPHICS_PLATFORM_VERSION);
    }
    catch(std::runtime_error const& err)
    {
        FAIL()
            << "Failed to find probe_rendering_platform (version " << MIR_SERVER_GRAPHICS_PLATFORM_VERSION << "): "
            << err.what();
    }
}

namespace
{
class NullEmergencyCleanup : public mir::EmergencyCleanupRegistry
{
public:
    void add(mir::EmergencyCleanupHandler const&) override
    {
    }

    void add(mir::ModuleEmergencyCleanupHandler) override
    {
    }
};
}

TEST_P(RenderingPlatformTest, supports_gl_rendering)
{
    auto const module = GetParam()->platform_module();

    auto const platform_loader = module->load_function<mg::CreateRenderPlatform>(
        "create_rendering_platform",
        MIR_SERVER_GRAPHICS_PLATFORM_VERSION);

    mir::options::ProgramOption empty_options{};
    NullEmergencyCleanup emergency_cleanup{};

/*    std::shared_ptr<mg::RenderingPlatform> const platform = platform_loader(
        empty_options,
        emergency_cleanup);
*/
    auto const gl_interface = mg::RenderingPlatform::acquire_interface<mg::GLRenderingProvider>(platform);

    EXPECT_THAT(gl_interface, testing::NotNull());
}
