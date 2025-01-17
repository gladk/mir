/*
 * Copyright © 2013 Canonical Ltd.
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

#ifndef MIR_TEST_DOUBLES_NULL_DISPLAY_CONFIGURATION_H_
#define MIR_TEST_DOUBLES_NULL_DISPLAY_CONFIGURATION_H_

#include "mir/graphics/display_configuration.h"

namespace mir
{
namespace test
{
namespace doubles
{
class NullDisplayConfiguration : public graphics::DisplayConfiguration
{
    void for_each_output(std::function<void(graphics::DisplayConfigurationOutput const&)>) const override
    {
    }
    void for_each_output(std::function<void(graphics::UserDisplayConfigurationOutput&)>) override
    {
    }
    std::unique_ptr<graphics::DisplayConfiguration> clone() const override
    {
        return std::make_unique<NullDisplayConfiguration>();
    }
};
}
}
}
#endif /* MIR_TEST_DOUBLES_NULL_DISPLAY_CONFIGURATION_H_ */
