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

#ifndef MIR_TEST_FRAMEWORK_EXECUTABLE_PATH_H_
#define MIR_TEST_FRAMEWORK_EXECUTABLE_PATH_H_

#include <string>
namespace mir_test_framework
{
std::string executable_path();

std::string library_path();
std::string server_platform_path();
std::string test_data_path();
std::string server_platform(std::string const& name);
std::string server_input_platform(std::string const& name);
}
#endif /* MIR_TEST_FRAMEWORK_EXECUTABLE_PATH_H_ */
