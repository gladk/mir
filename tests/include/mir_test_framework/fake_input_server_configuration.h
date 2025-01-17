/*
 * Copyright © 2013-2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MIR_TEST_FAKE_INPUT_SERVER_CONFIGURATION_H_
#define MIR_TEST_FAKE_INPUT_SERVER_CONFIGURATION_H_

#include "mir_test_framework/testing_server_configuration.h"
#include "mir_test_framework/temporary_environment_value.h"
#include "mir_test_framework/executable_path.h"

namespace mir_test_framework
{

class FakeInputServerConfiguration : public TestingServerConfiguration
{
public:
    FakeInputServerConfiguration();
    explicit FakeInputServerConfiguration(std::vector<mir::geometry::Rectangle> const& display_rects);
    ~FakeInputServerConfiguration();

    std::shared_ptr<mir::input::InputManager> the_input_manager() override;
    std::shared_ptr<mir::input::InputDispatcher> the_input_dispatcher() override;
    std::shared_ptr<mir::shell::InputTargeter> the_input_targeter() override;
};

}

#endif /* MIR_TEST_FAKE_INPUT_SERVER_CONFIGURATION_H_ */
