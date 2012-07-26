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
 * Authored by: Alan Griffiths <alan@octopull.co.uk>
 *              Thomas Voss <thomas.voss@canonical.com>
 */

#include "display_server_test_fixture.h"
#include "mir/frontend/protobuf_asio_communicator.h"

#include <boost/asio.hpp>

#include <chrono>
#include <cstdio>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>


namespace mir
{

bool detect_server(
        const std::string& socket_file,
        std::chrono::milliseconds const& timeout)
{
    std::chrono::time_point<std::chrono::system_clock> limit
        =  std::chrono::system_clock::now()+timeout;
    namespace ba = boost::asio;
    namespace bal = boost::asio::local;
    namespace bs = boost::system;

    ba::io_service io_service;
    bal::stream_protocol::endpoint endpoint(socket_file);
    bal::stream_protocol::socket socket(io_service);

    bs::error_code error;

    do
    {
        socket.connect(endpoint, error);
    }
    while (!error && std::chrono::system_clock::now() < limit);

    return !error;
}

}

TEST_F(BespokeDisplayServerTestFixture, server_announces_itself_on_startup)
{
    const std::string socket_file{"/tmp/mir_socket_test"};
    ASSERT_FALSE(mir::detect_server(socket_file, std::chrono::milliseconds(100)));

    struct ServerConfig : TestingServerConfiguration
    {
        ServerConfig(std::string const& file) : socket_file(file)
        {
        }

        std::shared_ptr<mir::frontend::Communicator> make_communicator()
        {
            return std::make_shared<mir::frontend::ProtobufAsioCommunicator>(socket_file);
        }

        void exec(mir::DisplayServer *)
        {
        }
        std::string const socket_file;
    } server_config(socket_file);

    launch_server_process(server_config);

    struct ClientConfig : TestingClientConfiguration
    {
        ClientConfig(std::string const& socket_file) : socket_file(socket_file)
        {
        }

        void exec()
        {
            EXPECT_TRUE(mir::detect_server(socket_file, std::chrono::milliseconds(100)));
        }
        std::string const socket_file;
    } client_config(socket_file);

    launch_client_process(client_config);
}
