/*
 * Copyright © 2012-2014 Canonical Ltd.
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
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#include "mir/compositor/buffer_stream.h"
#include "src/server/frontend/session_mediator.h"
#include "src/server/report/null_report_factory.h"
#include "src/server/frontend/resource_cache.h"
#include "src/server/scene/application_session.h"
#include "src/server/frontend/event_sender.h"
#include "src/server/frontend/protobuf_buffer_packer.h"
#include "mir/graphics/display.h"
#include "mir/graphics/display_configuration.h"
#include "mir/graphics/platform.h"
#include "mir/graphics/platform_ipc_package.h"
#include "mir/graphics/buffer_ipc_message.h"
#include "mir/graphics/platform_operation_message.h"
#include "mir/input/cursor_images.h"
#include "mir/graphics/platform_ipc_operations.h"
#include "src/server/scene/basic_surface.h"
#include "mir/test/doubles/mock_display.h"
#include "mir/test/doubles/mock_display_changer.h"
#include "mir/test/doubles/null_display.h"
#include "mir/test/doubles/null_event_sink.h"
#include "mir/test/doubles/null_event_sink_factory.h"
#include "mir/test/doubles/null_display_changer.h"
#include "mir/test/doubles/mock_buffer_stream.h"
#include "mir/test/doubles/mock_display.h"
#include "mir/test/doubles/mock_shell.h"
#include "mir/test/doubles/mock_frontend_surface.h"
#include "mir/test/doubles/mock_event_sink.h"
#include "mir/test/doubles/mock_event_sink_factory.h"
#include "mir/test/doubles/stub_buffer.h"
#include "mir/test/doubles/mock_buffer.h"
#include "mir/test/doubles/stub_session.h"
#include "mir/test/doubles/stub_display_configuration.h"
#include "mir/test/doubles/stub_buffer_allocator.h"
#include "mir/test/doubles/null_screencast.h"
#include "mir/test/doubles/null_application_not_responding_detector.h"
#include "mir/test/doubles/mock_platform_ipc_operations.h"
#include "mir/test/doubles/null_message_sender.h"
#include "mir/test/doubles/mock_message_sender.h"
#include "mir/test/doubles/mock_input_device_hub.h"
#include "mir/test/doubles/stub_input_device.h"
#include "mir/test/display_config_matchers.h"
#include "mir/test/input_devices_matcher.h"
#include "mir/test/fake_shared.h"
#include "mir/test/signal.h"
#include "mir/frontend/connector.h"
#include "mir/frontend/event_sink.h"
#include "mir/cookie/authority.h"
#include "mir_protobuf.pb.h"
#include "mir_protobuf_wire.pb.h"

#include "gmock_set_arg.h"
#include <boost/exception/errinfo_errno.hpp>
#include <boost/throw_exception.hpp>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <stdexcept>
#include <algorithm>

namespace mf = mir::frontend;
namespace mg = mir::graphics;
namespace mi = mir::input;
namespace mc = mir::compositor;
namespace ms = mir::scene;
namespace geom = mir::geometry;
namespace mp = mir::protobuf;
namespace msh = mir::shell;
namespace mt = mir::test;
namespace mtd = mt::doubles;
namespace mr = mir::report;

namespace
{

struct MockResourceCache : public mf::MessageResourceCache
{
    MOCK_METHOD2(save_resource, void(google::protobuf::MessageLite*, std::shared_ptr<void> const&));
    MOCK_METHOD2(save_fd, void(google::protobuf::MessageLite*, mir::Fd const&));
    MOCK_METHOD1(free_resource, void(google::protobuf::MessageLite*));
};

struct MockConnector : public mf::Connector
{
public:
    void start() override {}
    void stop() override {}

    int client_socket_fd() const override { return 0; }

    MOCK_CONST_METHOD1(client_socket_fd, int (std::function<void(std::shared_ptr<mf::Session> const&)> const&));
};

class StubbedSession : public mtd::StubSession
{
public:
    StubbedSession() :
        last_surface_id{0}
    {
    }

    std::shared_ptr<mf::Surface> get_surface(mf::SurfaceId surface) const
    {
        if (mock_surfaces.find(surface) == mock_surfaces.end())
            BOOST_THROW_EXCEPTION(std::logic_error("Invalid SurfaceId"));
        return mock_surfaces.at(surface);
    }

    std::shared_ptr<mf::BufferStream> get_buffer_stream(mf::BufferStreamId stream) const override
    {
        if (mock_streams.find(stream) == mock_streams.end())
            BOOST_THROW_EXCEPTION(std::logic_error("Invalid SurfaceId"));
        return mock_streams.at(stream);
    }

    std::shared_ptr<mtd::MockFrontendSurface> mock_surface_at(mf::SurfaceId id)
    {
        if (mock_surfaces.end() == mock_surfaces.find(id))
            return create_mock_surface(id); 
        return mock_surfaces.at(id);
    }

    std::shared_ptr<mtd::MockBufferStream> mock_primary_stream_at(mf::SurfaceId id)
    {
        if (mock_surfaces.end() == mock_surfaces.find(id))
            create_mock_surface(id);
        return mock_streams.at(mf::BufferStreamId(id.as_value()));
    }

    std::shared_ptr<mtd::MockFrontendSurface> create_mock_surface(mf::SurfaceId id)
    {
        using namespace testing;
        auto surface = std::make_shared<testing::NiceMock<mtd::MockFrontendSurface>>(testing_client_input_fd);
        auto stream = std::make_shared<testing::NiceMock<mtd::MockBufferStream>>();

        auto buffer1 = std::make_shared<mtd::StubBuffer>();
        auto buffer2 = std::make_shared<mtd::StubBuffer>();
        ON_CALL(*stream, swap_buffers(testing::_,testing::_))
            .WillByDefault(testing::Invoke(
            [buffer1, buffer2](mg::Buffer* b, std::function<void(mg::Buffer* new_buffer)> complete)
            {
                if ((!b) || (b == buffer1.get()))
                    complete(buffer2.get());
                if (b == buffer2.get())
                    complete(buffer1.get()); 
            }));

        ON_CALL(*surface, primary_buffer_stream())
            .WillByDefault(Return(stream));


        mock_surfaces[id] = surface;
        mock_streams[mf::BufferStreamId(id.as_value())] = stream;
        return surface;
    }

    std::shared_ptr<mtd::MockBufferStream> create_mock_stream(mf::BufferStreamId id)
    {
        mock_streams[id] = std::make_shared<testing::NiceMock<mtd::MockBufferStream>>();
        return mock_streams[id];
    }

    mf::SurfaceId create_surface(
        ms::SurfaceCreationParameters const&,
        std::shared_ptr<mf::EventSink> const&)
    {
        mf::SurfaceId id{last_surface_id};
        if (mock_surfaces.end() == mock_surfaces.find(id))
            create_mock_surface(id);
        last_surface_id++;
        return id;
    }

    mf::BufferStreamId create_buffer_stream(mg::BufferProperties const&)
    {
        mf::BufferStreamId id{last_surface_id};
        if (mock_streams.end() == mock_streams.find(id))
            create_mock_stream(id);
        last_surface_id++;
        return id;
    }

    void destroy_surface(mf::SurfaceId surface)
    {
        mock_surfaces.erase(surface);
    }

    
    mg::BufferID create_buffer(mg::BufferProperties const&)
    {
        buffer_count++;
        return mg::BufferID{3};
    }

    void destroy_buffer(mg::BufferID)
    {
        destroy_buffers++;
    }

    int num_alloc_requests()
    {
        return buffer_count;
    }

    int num_destroy_requests()
    {
        return destroy_buffers;
    }

    std::map<mf::BufferStreamId, std::shared_ptr<mtd::MockBufferStream>> mock_streams;
    std::map<mf::SurfaceId, std::shared_ptr<mtd::MockFrontendSurface>> mock_surfaces;
    static int const testing_client_input_fd;
    int last_surface_id;
    int buffer_count = 0;
    int destroy_buffers = 0;
};

int const StubbedSession::testing_client_input_fd{11};

struct StubScreencast : mtd::NullScreencast
{
    std::shared_ptr<mg::Buffer> capture(mf::ScreencastSessionId)
    {
        return mt::fake_shared(stub_buffer);
    }

    mtd::StubBuffer stub_buffer;
};

struct SessionMediator : public ::testing::Test
{
    SessionMediator()
        : shell{std::make_shared<testing::NiceMock<mtd::MockShell>>()},
          graphics_changer{std::make_shared<mtd::NullDisplayChanger>()},
          surface_pixel_formats{mir_pixel_format_argb_8888, mir_pixel_format_xrgb_8888},
          report{mr::null_session_mediator_report()},
          resource_cache{std::make_shared<mf::ResourceCache>()},
          stub_screencast{std::make_shared<StubScreencast>()},
          stubbed_session{std::make_shared<StubbedSession>()},
          null_callback{google::protobuf::NewPermanentCallback(google::protobuf::DoNothing)},
          mediator{
            shell, mt::fake_shared(mock_ipc_operations), graphics_changer,
            surface_pixel_formats, report,
            std::make_shared<mtd::NullEventSinkFactory>(),
            std::make_shared<mtd::NullMessageSender>(),
            resource_cache, stub_screencast, &connector, nullptr, nullptr,
            std::make_shared<mtd::NullANRDetector>(),
            mir::cookie::Authority::create(),
            mt::fake_shared(mock_hub)}
    {
        using namespace ::testing;

        ON_CALL(*shell, open_session(_, _, _)).WillByDefault(Return(stubbed_session));

        ON_CALL(*shell, create_surface( _, _, _)).WillByDefault(
            WithArgs<1, 2>(Invoke(stubbed_session.get(), &StubbedSession::create_surface)));

        ON_CALL(*shell, destroy_surface( _, _)).WillByDefault(
            WithArg<1>(Invoke(stubbed_session.get(), &StubbedSession::destroy_surface)));
    }


    std::shared_ptr<mf::SessionMediator> create_session_mediator_with_display_changer(
        std::shared_ptr<mf::DisplayChanger> const& display_changer)
    {
        return std::make_shared<mf::SessionMediator>(
            shell, mt::fake_shared(mock_ipc_operations), display_changer,
            surface_pixel_formats, report,
            std::make_shared<mtd::NullEventSinkFactory>(),
            std::make_shared<mtd::NullMessageSender>(),
            resource_cache, std::make_shared<mtd::NullScreencast>(),
            nullptr, nullptr, nullptr,
            std::make_shared<mtd::NullANRDetector>(),
            mir::cookie::Authority::create(),
            mt::fake_shared(mock_hub));
    }

    MockConnector connector;
    testing::NiceMock<mtd::MockPlatformIpcOperations> mock_ipc_operations;
    testing::NiceMock<mtd::MockInputDeviceHub> mock_hub;
    std::shared_ptr<testing::NiceMock<mtd::MockShell>> const shell;
    std::shared_ptr<mf::DisplayChanger> const graphics_changer;
    std::vector<MirPixelFormat> const surface_pixel_formats;
    std::shared_ptr<mf::SessionMediatorReport> const report;
    std::shared_ptr<mf::ResourceCache> const resource_cache;
    std::shared_ptr<StubScreencast> const stub_screencast;
    std::shared_ptr<StubbedSession> const stubbed_session;
    std::unique_ptr<google::protobuf::Closure> null_callback;
    mf::SessionMediator mediator;

    mp::ConnectParameters connect_parameters;
    mp::Connection connection;
    mp::SurfaceParameters surface_parameters;
    mp::Surface surface_response;
    mp::Void void_response;
    mp::SurfaceId surface_id_request;
    mp::Buffer buffer_response;
    mp::BufferRequest buffer_request;
};
}

TEST_F(SessionMediator, disconnect_releases_session)
{
    using namespace ::testing;
    EXPECT_CALL(*shell, close_session(_))
        .Times(1);

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.disconnect(nullptr, nullptr, null_callback.get());
}

TEST_F(SessionMediator, connect_calls_connect_handler)
{
    using namespace ::testing;
    int connects_handled_count = 0;

    mf::ConnectionContext const context
    {
        [&](std::shared_ptr<mf::Session> const&) { ++connects_handled_count; },
        nullptr
    };

    mf::SessionMediator mediator{
        shell, mt::fake_shared(mock_ipc_operations), graphics_changer,
        surface_pixel_formats, report,
        std::make_shared<mtd::NullEventSinkFactory>(),
        std::make_shared<mtd::NullMessageSender>(),
        resource_cache, stub_screencast, context, nullptr, nullptr,
        std::make_shared<mtd::NullANRDetector>(),
        mir::cookie::Authority::create(),
        mt::fake_shared(mock_hub)};

    EXPECT_THAT(connects_handled_count, Eq(0));

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    EXPECT_THAT(connects_handled_count, Eq(1));

    mediator.disconnect(nullptr, nullptr, null_callback.get());
    EXPECT_THAT(connects_handled_count, Eq(1));
}

TEST_F(SessionMediator, calling_methods_before_connect_throws)
{
    EXPECT_THROW({
        mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
    }, std::logic_error);

    EXPECT_THROW({
        mediator.next_buffer(&surface_id_request, &buffer_response, null_callback.get());
    }, std::logic_error);

    EXPECT_THROW({
        mediator.exchange_buffer(&buffer_request, &buffer_response, null_callback.get());
    }, std::logic_error);

    EXPECT_THROW({
        mediator.release_surface(&surface_id_request, nullptr, null_callback.get());
    }, std::logic_error);

    EXPECT_THROW({
        mediator.disconnect(nullptr, nullptr, null_callback.get());
    }, std::logic_error);
}

TEST_F(SessionMediator, calling_methods_after_connect_works)
{
    mediator.connect(&connect_parameters, &connection, null_callback.get());

    EXPECT_NO_THROW({
        mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
        *buffer_request.mutable_buffer() = surface_response.buffer_stream().buffer();
        buffer_request.mutable_id()->set_value(surface_response.id().value());
        mediator.next_buffer(&surface_id_request, &buffer_response, null_callback.get());
        mediator.exchange_buffer(&buffer_request, &buffer_response, null_callback.get());
        mediator.release_surface(&surface_id_request, nullptr, null_callback.get());
    });

    mediator.disconnect(nullptr, nullptr, null_callback.get());
}

TEST_F(SessionMediator, calling_methods_after_disconnect_throws)
{
    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.disconnect(nullptr, nullptr, null_callback.get());

    EXPECT_THROW({
        mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
    }, std::logic_error);

    EXPECT_THROW({
        mediator.next_buffer(&surface_id_request, &buffer_response, null_callback.get());
    }, std::logic_error);

    EXPECT_THROW({
        mediator.exchange_buffer(&buffer_request, &buffer_response, null_callback.get());
    }, std::logic_error);

    EXPECT_THROW({
        mediator.release_surface(&surface_id_request, nullptr, null_callback.get());
    }, std::logic_error);

    EXPECT_THROW({
        mediator.disconnect(nullptr, nullptr, null_callback.get());
    }, std::logic_error);
}

//How does this test fail? consider removal
TEST_F(SessionMediator, can_reconnect_after_disconnect)
{
    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.disconnect(nullptr, nullptr, null_callback.get());
    mediator.connect(&connect_parameters, &connection, null_callback.get());
}

TEST_F(SessionMediator, connect_packs_display_configuration)
{
    using namespace testing;
    mtd::StubDisplayConfig config;
    auto mock_display_changer = std::make_shared<NiceMock<mtd::MockDisplayChanger>>();
    ON_CALL(*mock_display_changer, base_configuration())
        .WillByDefault(Return(mt::fake_shared(config)));

    auto const mediator = create_session_mediator_with_display_changer(mock_display_changer);
    mediator->connect(&connect_parameters, &connection, null_callback.get());

    EXPECT_THAT(connection.display_configuration(), mt::DisplayConfigMatches(std::cref(config)));
}

TEST_F(SessionMediator, creating_surface_packs_response_with_input_fds)
{
    mediator.connect(&connect_parameters, &connection, null_callback.get());

    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
    ASSERT_THAT(surface_response.fd().size(), testing::Ge(1));
    EXPECT_EQ(StubbedSession::testing_client_input_fd, surface_response.fd(0));

    mediator.disconnect(nullptr, nullptr, null_callback.get());
}

TEST_F(SessionMediator, no_input_channel_returns_no_fds)
{
    using namespace testing;

    auto surface = stubbed_session->mock_surface_at(mf::SurfaceId{0});
    EXPECT_CALL(*surface, supports_input())
        .WillOnce(Return(false));
    EXPECT_CALL(*surface, client_input_fd())
        .Times(0);

    mediator.connect(&connect_parameters, &connection, null_callback.get());

    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
    EXPECT_THAT(surface_response.fd().size(), Eq(0));

    mediator.disconnect(nullptr, nullptr, null_callback.get());
}

TEST_F(SessionMediator, session_only_sends_mininum_information_for_buffers)
{
    using namespace testing;
    mf::SurfaceId surf_id{0};
    mtd::StubBuffer buffer1;
    mtd::StubBuffer buffer2;
    auto stream = stubbed_session->mock_primary_stream_at(surf_id);
    ON_CALL(*stream, swap_buffers(nullptr,_))
        .WillByDefault(InvokeArgument<1>(&buffer2));
    ON_CALL(*stream, swap_buffers(&buffer1,_))
        .WillByDefault(InvokeArgument<1>(&buffer2));
    ON_CALL(*stream, swap_buffers(&buffer2,_))
        .WillByDefault(InvokeArgument<1>(&buffer1));

    //create
    Sequence seq;
    EXPECT_CALL(*stream, swap_buffers(_, _))
        .InSequence(seq)
        .WillOnce(InvokeArgument<1>(&buffer2));
    EXPECT_CALL(mock_ipc_operations, pack_buffer(_, Ref(buffer2), mg::BufferIpcMsgType::full_msg))
        .InSequence(seq);
    //swap1
    EXPECT_CALL(*stream, swap_buffers(&buffer2, _))
        .InSequence(seq)
        .WillOnce(InvokeArgument<1>(&buffer1));
    EXPECT_CALL(mock_ipc_operations, pack_buffer(_, Ref(buffer1), mg::BufferIpcMsgType::full_msg))
        .InSequence(seq);
    //swap2
    EXPECT_CALL(*stream, swap_buffers(&buffer1, _))
        .InSequence(seq)
        .WillOnce(InvokeArgument<1>(&buffer2));
    EXPECT_CALL(mock_ipc_operations, pack_buffer(_, Ref(buffer2), mg::BufferIpcMsgType::update_msg))
        .InSequence(seq);
    //swap3
    EXPECT_CALL(*stream, swap_buffers(&buffer2, _))
        .InSequence(seq)
        .WillOnce(InvokeArgument<1>(&buffer1));
    EXPECT_CALL(mock_ipc_operations, pack_buffer(_, Ref(buffer1), mg::BufferIpcMsgType::update_msg))
        .InSequence(seq);

    mediator.connect(&connect_parameters, &connection, null_callback.get());

    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
    surface_id_request = surface_response.id();
    mediator.next_buffer(&surface_id_request, &buffer_response, null_callback.get());
    mediator.next_buffer(&surface_id_request, &buffer_response, null_callback.get());
    mediator.next_buffer(&surface_id_request, &buffer_response, null_callback.get());
    mediator.disconnect(nullptr, nullptr, null_callback.get());
}

TEST_F(SessionMediator, session_with_multiple_surfaces_only_sends_needed_buffers)
{
    using namespace testing;
    EXPECT_CALL(mock_ipc_operations, pack_buffer(_,_,mg::BufferIpcMsgType::full_msg))
        .Times(4);
    EXPECT_CALL(mock_ipc_operations, pack_buffer(_,_,mg::BufferIpcMsgType::update_msg))
        .Times(4);

    mediator.connect(&connect_parameters, &connection, null_callback.get());

    mp::Surface surface_response[2];
    mp::SurfaceId buffer_request[2];
    mediator.create_surface(&surface_parameters, &surface_response[0], null_callback.get());
    mediator.create_surface(&surface_parameters, &surface_response[1], null_callback.get());
    buffer_request[0] = surface_response[0].id();
    buffer_request[1] = surface_response[1].id();

    mediator.next_buffer(&buffer_request[0], &buffer_response, null_callback.get());
    mediator.next_buffer(&buffer_request[1], &buffer_response, null_callback.get());
    mediator.next_buffer(&buffer_request[0], &buffer_response, null_callback.get());
    mediator.next_buffer(&buffer_request[1], &buffer_response, null_callback.get());
    mediator.next_buffer(&buffer_request[0], &buffer_response, null_callback.get());
    mediator.next_buffer(&buffer_request[1], &buffer_response, null_callback.get());

    mediator.disconnect(nullptr, nullptr, null_callback.get());
}

TEST_F(SessionMediator, destroys_tracker_associated_with_destroyed_surface)
{
    using namespace testing;
    mf::SurfaceId first_id{0};
    mp::Surface surface_response;

    EXPECT_CALL(mock_ipc_operations, pack_buffer(_,_,mg::BufferIpcMsgType::full_msg))
        .Times(2);

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
    surface_id_request.set_value(first_id.as_value());
    mediator.release_surface(&surface_id_request, nullptr, null_callback.get());

    stubbed_session->last_surface_id = first_id.as_value();

    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
    surface_id_request.set_value(first_id.as_value());
    mediator.release_surface(&surface_id_request, nullptr, null_callback.get());
    mediator.disconnect(nullptr, nullptr, null_callback.get());
}

TEST_F(SessionMediator, buffer_resource_for_surface_unaffected_by_other_surfaces)
{
    using namespace testing;
    mtd::StubBuffer buffer;
    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mp::SurfaceParameters surface_request;
    mp::Surface surface_response;

    auto stream1 = stubbed_session->mock_primary_stream_at(mf::SurfaceId{0});
    ON_CALL(*stream1, swap_buffers(_,_))
        .WillByDefault(InvokeArgument<1>(&buffer));

    mediator.create_surface(&surface_request, &surface_response, null_callback.get());
    mp::SurfaceId our_surface{surface_response.id()};

    /* Creating a new surface should not affect our surfaces' buffers */
    EXPECT_CALL(*stream1, swap_buffers(_, _)).Times(0);
    mediator.create_surface(&surface_request, &surface_response, null_callback.get());
    Mock::VerifyAndClearExpectations(stream1.get());

    mp::SurfaceId new_surface{surface_response.id()};
    mp::Buffer buffer_response;

    /* Getting the next buffer of new surface should not affect our surfaces' buffers */
    mediator.next_buffer(&new_surface, &buffer_response, null_callback.get());

    /* Getting the next buffer of our surface should post the original */
    EXPECT_CALL(*stream1, swap_buffers(Eq(&buffer),_)).Times(1);

    mediator.next_buffer(&our_surface, &buffer_response, null_callback.get());
    mediator.disconnect(nullptr, nullptr, null_callback.get());
}

TEST_F(SessionMediator, display_config_request)
{
    using namespace testing;
    mp::ConnectParameters connect_parameters;
    mp::Connection connection;

    bool used0 = false, used1 = true;
    geom::Point pt0{44,22}, pt1{3,2};
    size_t mode_index0 = 1, mode_index1 = 3;
    MirPixelFormat format0{mir_pixel_format_invalid};
    MirPixelFormat format1{mir_pixel_format_argb_8888};
    mg::DisplayConfigurationOutputId id0{6}, id1{3};

    mtd::StubDisplayConfig stub_display_config;
    auto mock_display_changer = std::make_shared<mtd::MockDisplayChanger>();

    Sequence seq;
    EXPECT_CALL(*mock_display_changer, base_configuration())
        .InSequence(seq)
        .WillOnce(Return(mt::fake_shared(stub_display_config)));
    EXPECT_CALL(*mock_display_changer, base_configuration())
        .InSequence(seq)
        .WillOnce(Return(mt::fake_shared(stub_display_config)));
    EXPECT_CALL(*mock_display_changer, configure(_,_))
        .InSequence(seq);
    EXPECT_CALL(*mock_display_changer, base_configuration())
        .InSequence(seq)
        .WillOnce(Return(mt::fake_shared(stub_display_config)));

    auto const mediator = create_session_mediator_with_display_changer(mock_display_changer);

    mediator->connect(&connect_parameters, &connection, null_callback.get());

    mp::DisplayConfiguration configuration_response;
    mp::DisplayConfiguration configuration;
    auto disp0 = configuration.add_display_output();
    disp0->set_output_id(id0.as_value());
    disp0->set_used(used0);
    disp0->set_position_x(pt0.x.as_uint32_t());
    disp0->set_position_y(pt0.y.as_uint32_t());
    disp0->set_current_mode(mode_index0);
    disp0->set_current_format(format0);
    disp0->set_power_mode(static_cast<uint32_t>(mir_power_mode_on));
    disp0->set_orientation(mir_orientation_left);

    auto disp1 = configuration.add_display_output();
    disp1->set_output_id(id1.as_value());
    disp1->set_used(used1);
    disp1->set_position_x(pt1.x.as_uint32_t());
    disp1->set_position_y(pt1.y.as_uint32_t());
    disp1->set_current_mode(mode_index1);
    disp1->set_current_format(format1);
    disp1->set_power_mode(static_cast<uint32_t>(mir_power_mode_off));
    disp1->set_orientation(mir_orientation_inverted);

    mediator->configure_display(&configuration,
                                &configuration_response, null_callback.get());

    EXPECT_THAT(configuration_response, mt::DisplayConfigMatches(std::cref(stub_display_config)));

    mediator->disconnect(nullptr, nullptr, null_callback.get());
}

TEST_F(SessionMediator, fully_packs_buffer_for_create_screencast)
{
    using namespace testing;

    mp::ScreencastParameters screencast_parameters;
    mp::Screencast screencast;
    auto const& stub_buffer = stub_screencast->stub_buffer;

    EXPECT_CALL(mock_ipc_operations, pack_buffer(_, Ref(stub_buffer), _));

    mediator.create_screencast(&screencast_parameters,
                               &screencast, null_callback.get());
    EXPECT_EQ(stub_buffer.id().as_value(), screencast.buffer_stream().buffer().buffer_id());
}

TEST_F(SessionMediator, partially_packs_buffer_for_screencast_buffer)
{
    using namespace testing;

    mp::ScreencastId screencast_id;
    mp::Buffer protobuf_buffer;
    auto const& stub_buffer = stub_screencast->stub_buffer;

    EXPECT_CALL(mock_ipc_operations,
        pack_buffer(_, Ref(stub_buffer), mg::BufferIpcMsgType::update_msg))
        .Times(1);

    mediator.screencast_buffer(&screencast_id,
                               &protobuf_buffer, null_callback.get());
    EXPECT_EQ(stub_buffer.id().as_value(), protobuf_buffer.buffer_id());
}

TEST_F(SessionMediator, prompt_provider_fds_allocated_by_connector)
{
    using namespace ::testing;
    int const fd_count{11};
    int const dummy_fd{__LINE__};
    mp::SocketFDRequest request;
    mp::SocketFD response;
    request.set_number(fd_count);

    EXPECT_CALL(connector, client_socket_fd(_))
        .Times(fd_count)
        .WillRepeatedly(Return(dummy_fd));

    mediator.connect(&connect_parameters, &connection, null_callback.get());

    mediator.new_fds_for_prompt_providers(&request, &response, null_callback.get());
    EXPECT_THAT(response.fd_size(), Eq(fd_count));

    mediator.disconnect(nullptr, nullptr, null_callback.get());
}

TEST_F(SessionMediator, exchange_buffer)
{
    using namespace testing;
    auto const& mock_stream = stubbed_session->mock_primary_stream_at(mf::SurfaceId{0});
    mp::Buffer exchanged_buffer;
    mtd::StubBuffer stub_buffer1;
    mtd::StubBuffer stub_buffer2;

    //create
    Sequence seq;
    EXPECT_CALL(*mock_stream, swap_buffers(_, _))
        .InSequence(seq)
        .WillOnce(InvokeArgument<1>(&stub_buffer1));
    //exchange
    EXPECT_CALL(*mock_stream, swap_buffers(&stub_buffer1,_))
        .InSequence(seq)
        .WillOnce(InvokeArgument<1>(&stub_buffer2));

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
    EXPECT_THAT(surface_response.buffer_stream().buffer().buffer_id(), Eq(stub_buffer1.id().as_value()));

    buffer_request.mutable_id()->set_value(surface_response.id().value());
    buffer_request.mutable_buffer()->set_buffer_id(surface_response.buffer_stream().buffer().buffer_id());
    mediator.exchange_buffer(&buffer_request, &exchanged_buffer, null_callback.get());
    EXPECT_THAT(exchanged_buffer.buffer_id(), Eq(stub_buffer2.id().as_value()));
}

TEST_F(SessionMediator, session_exchange_buffer_sends_minimum_information)
{
    using namespace testing;
    mp::Buffer exchanged_buffer;
    mf::SurfaceId surf_id{0};
    mtd::StubBuffer buffer1;
    mtd::StubBuffer buffer2;
    auto stream = stubbed_session->mock_primary_stream_at(surf_id);
    ON_CALL(*stream, swap_buffers(nullptr,_))
        .WillByDefault(InvokeArgument<1>(&buffer2));
    ON_CALL(*stream, swap_buffers(&buffer1,_))
        .WillByDefault(InvokeArgument<1>(&buffer2));
    ON_CALL(*stream, swap_buffers(&buffer2,_))
        .WillByDefault(InvokeArgument<1>(&buffer1));

    Sequence seq;
    //create
    EXPECT_CALL(mock_ipc_operations, pack_buffer(_, Ref(buffer2), mg::BufferIpcMsgType::full_msg))
        .InSequence(seq);
    //swap1
    EXPECT_CALL(mock_ipc_operations, unpack_buffer(_, Ref(buffer2)))
        .InSequence(seq);
    EXPECT_CALL(mock_ipc_operations, pack_buffer(_, Ref(buffer1), mg::BufferIpcMsgType::full_msg))
        .InSequence(seq);
    //swap2
    EXPECT_CALL(mock_ipc_operations, unpack_buffer(_, Ref(buffer1)))
        .InSequence(seq);
    EXPECT_CALL(mock_ipc_operations, pack_buffer(_, Ref(buffer2), mg::BufferIpcMsgType::update_msg))
        .InSequence(seq);
    //swap3
    EXPECT_CALL(mock_ipc_operations, unpack_buffer(_, Ref(buffer2)))
        .InSequence(seq);
    EXPECT_CALL(mock_ipc_operations, pack_buffer(_, Ref(buffer1), mg::BufferIpcMsgType::update_msg))
        .InSequence(seq);

    mediator.connect(&connect_parameters, &connection, null_callback.get());

    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
    buffer_request.mutable_id()->set_value(surface_response.id().value());
    buffer_request.mutable_buffer()->set_buffer_id(surface_response.buffer_stream().buffer().buffer_id());

    mediator.exchange_buffer(&buffer_request, &exchanged_buffer, null_callback.get());
    buffer_request.mutable_buffer()->set_buffer_id(exchanged_buffer.buffer_id());

    mediator.exchange_buffer(&buffer_request, &exchanged_buffer, null_callback.get());
    buffer_request.mutable_buffer()->set_buffer_id(exchanged_buffer.buffer_id());

    mediator.exchange_buffer(&buffer_request, &exchanged_buffer, null_callback.get());
    mediator.disconnect(nullptr, nullptr, null_callback.get());
}

TEST_F(SessionMediator, exchange_buffer_throws_if_client_submits_bad_request)
{
    using namespace testing;
    auto const& mock_stream = stubbed_session->mock_primary_stream_at(mf::SurfaceId{0});
    mp::Buffer exchanged_buffer;
    mtd::StubBuffer stub_buffer1;
    mtd::StubBuffer stub_buffer2;

    EXPECT_CALL(*mock_stream, swap_buffers(_, _))
        .WillOnce(InvokeArgument<1>(&stub_buffer1));

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
    EXPECT_THAT(surface_response.buffer_stream().buffer().buffer_id(), Eq(stub_buffer1.id().as_value()));

    buffer_request.mutable_id()->set_value(surface_response.id().value());
    //client doesnt own stub_buffer2
    buffer_request.mutable_buffer()->set_buffer_id(stub_buffer2.id().as_value());
    EXPECT_THROW({
        mediator.exchange_buffer(&buffer_request, &exchanged_buffer, null_callback.get());
    }, std::logic_error);

    //client made up its own surface id.
    buffer_request.mutable_id()->set_value(surface_response.id().value() + 2); 
    buffer_request.mutable_buffer()->set_buffer_id(stub_buffer1.id().as_value());
    EXPECT_THROW({
        mediator.exchange_buffer(&buffer_request, &exchanged_buffer, null_callback.get());
    }, std::logic_error);
}

TEST_F(SessionMediator, exchange_buffer_different_for_different_surfaces)
{
    using namespace testing;
    mp::SurfaceParameters surface_request;
    mp::BufferRequest req1;
    mp::BufferRequest req2;
    auto const& mock_stream1 = stubbed_session->mock_primary_stream_at(mf::SurfaceId{0});
    auto const& mock_stream2 = stubbed_session->mock_primary_stream_at(mf::SurfaceId{1});
    Sequence seq;
    EXPECT_CALL(*mock_stream1, swap_buffers(_,_))
        .InSequence(seq);
    EXPECT_CALL(*mock_stream2, swap_buffers(_,_))
        .InSequence(seq);
    EXPECT_CALL(*mock_stream2, swap_buffers(_,_))
        .InSequence(seq);
    EXPECT_CALL(*mock_stream1, swap_buffers(_,_))
        .InSequence(seq);

    mediator.connect(&connect_parameters, &connection, null_callback.get());

    mediator.create_surface(&surface_request, &surface_response, null_callback.get());
    req1.mutable_id()->set_value(surface_response.id().value());
    *req1.mutable_buffer() = surface_response.buffer_stream().buffer();
    mediator.create_surface(&surface_request, &surface_response, null_callback.get());
    req2.mutable_id()->set_value(surface_response.id().value());
    *req2.mutable_buffer() = surface_response.buffer_stream().buffer();
    mediator.exchange_buffer(&req2, &buffer_response, null_callback.get());
    mediator.exchange_buffer(&req1, &buffer_response, null_callback.get());
    mediator.disconnect(nullptr, nullptr, null_callback.get());
}

TEST_F(SessionMediator, buffer_fd_resources_are_put_in_resource_cache)
{
    using namespace testing;
    NiceMock<MockResourceCache> mock_cache;
    mp::Buffer exchanged_buffer;

    mir::Fd fake_fd0(mir::IntOwnedFd{99});
    mir::Fd fake_fd1(mir::IntOwnedFd{100});
    mir::Fd fake_fd2(mir::IntOwnedFd{101});

    EXPECT_CALL(mock_ipc_operations, pack_buffer(_,_,_))
        .WillOnce(Invoke([&](mg::BufferIpcMessage& msg, mg::Buffer const&, mg::BufferIpcMsgType)
        { msg.pack_fd(fake_fd0); }))
        .WillOnce(Invoke([&](mg::BufferIpcMessage& msg, mg::Buffer const&, mg::BufferIpcMsgType)
        { msg.pack_fd(fake_fd1); }))
        .WillOnce(Invoke([&](mg::BufferIpcMessage& msg, mg::Buffer const&, mg::BufferIpcMsgType)
        { msg.pack_fd(fake_fd2); }));

    EXPECT_CALL(mock_cache, save_fd(_,fake_fd0));
    EXPECT_CALL(mock_cache, save_fd(_,fake_fd1));
    EXPECT_CALL(mock_cache, save_fd(_,fake_fd2));

    mf::SessionMediator mediator{
        shell, mt::fake_shared(mock_ipc_operations), graphics_changer,
        surface_pixel_formats, report,
        std::make_shared<mtd::NullEventSinkFactory>(),
        std::make_shared<mtd::NullMessageSender>(),
        mt::fake_shared(mock_cache), stub_screencast, &connector, nullptr, nullptr,
        std::make_shared<mtd::NullANRDetector>(),
        mir::cookie::Authority::create(),
        mt::fake_shared(mock_hub)};

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
    buffer_request.mutable_id()->set_value(surface_response.id().value());
    buffer_request.mutable_buffer()->set_buffer_id(surface_response.buffer_stream().buffer().buffer_id());

    mediator.exchange_buffer(&buffer_request, &exchanged_buffer, null_callback.get());
    buffer_request.mutable_buffer()->set_buffer_id(exchanged_buffer.buffer_id());
    exchanged_buffer.clear_fd();

    mediator.exchange_buffer(&buffer_request, &exchanged_buffer, null_callback.get());
    buffer_request.mutable_buffer()->set_buffer_id(exchanged_buffer.buffer_id());
    buffer_request.mutable_buffer()->clear_fd();
}

// Regression test for LP: #1441759
TEST_F(SessionMediator, completes_exchange_buffer_when_completion_is_invoked_asynchronously_from_thread_that_initiated_exchange)
{
    using namespace testing;
    auto const& mock_stream = stubbed_session->mock_primary_stream_at(mf::SurfaceId{0});
    mtd::StubBuffer stub_buffer1;
    mtd::StubBuffer stub_buffer2;
    std::function<void(mg::Buffer*)> completion_func;

    // create
    InSequence seq;
    EXPECT_CALL(*mock_stream, swap_buffers(_, _))
        .WillOnce(InvokeArgument<1>(&stub_buffer1));
    // exchange, steal completion function
    EXPECT_CALL(*mock_stream, swap_buffers(_,_))
        .WillOnce(SaveArg<1>(&completion_func));

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());

    buffer_request.mutable_id()->set_value(surface_response.id().value());
    *buffer_request.mutable_buffer() = surface_response.buffer_stream().buffer();

    mediator.exchange_buffer(&buffer_request, &buffer_response, null_callback.get());

    // Execute completion function asynchronously (i.e. not as part of the exchange_buffer
    // call), but from the same thread that initiated the exchange_buffer operation
    completion_func(&stub_buffer2);
}

MATCHER(ConfigEq, "stream configurations are equivalent")
{
    return (std::get<0>(arg).stream_id == std::get<1>(arg).stream_id) &&
           (std::get<0>(arg).displacement == std::get<1>(arg).displacement);
}

namespace mir
{
namespace shell
{
void PrintTo(msh::StreamSpecification const& s, std::ostream* os)
{
    *os << "streams with id: " << s.stream_id.as_value(); 
}
}
}

MATCHER_P(StreamsAre, value, "configuration streams match")
{
    if(!arg.streams.is_set())
        return false;
    EXPECT_THAT(arg.streams.value(), testing::Pointwise(ConfigEq(), value));
    return !(::testing::Test::HasFailure());
}

TEST_F(SessionMediator, arranges_bufferstreams_via_shell)
{
    using namespace testing;
    mp::Void null;
    mp::SurfaceModifications mods;
    mp::BufferStreamParameters stream_request;
    std::array<mp::BufferStream,2> streams;
    std::array<geom::Displacement,2> displacement = { {
        geom::Displacement{-12,11}, geom::Displacement{4,-3} } };

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
    for (auto &stream : streams)
        mediator.create_buffer_stream(&stream_request, &stream, null_callback.get());
    mods.mutable_surface_id()->set_value(surface_response.id().value());
    for (auto i = 0u; i < streams.size(); i++)
    {
        auto stream = mods.mutable_surface_specification()->add_stream();
        stream->mutable_id()->set_value(streams[i].id().value());
        stream->set_displacement_x(displacement[i].dx.as_int());
        stream->set_displacement_y(displacement[i].dy.as_int());
    }

    EXPECT_CALL(*shell, modify_surface(_,
        mf::SurfaceId{surface_response.id().value()},
        StreamsAre(std::vector<msh::StreamSpecification>{
            {mf::BufferStreamId(streams[0].id().value()), displacement[0], {}},
            {mf::BufferStreamId(streams[1].id().value()), displacement[1], {}},
        })));

    mediator.modify_surface(&mods, &null, null_callback.get());
}

TEST_F(SessionMediator, sends_a_buffer_when_submit_buffer_is_called)
{
    using namespace testing;
    auto sink_factory = std::make_shared<mtd::MockEventSinkFactory>();
    auto mock_sink = sink_factory->the_mock_sink();

    auto buffer1 = std::make_shared<mtd::StubBuffer>();
    mf::SessionMediator mediator{
        shell,
        mt::fake_shared(mock_ipc_operations),
        graphics_changer,
        surface_pixel_formats,
        report,
        sink_factory,
        std::make_shared<mtd::NullMessageSender>(),
        resource_cache,
        stub_screencast,
        nullptr,
        nullptr,
        nullptr,
        std::make_shared<mtd::NullANRDetector>(),
        mir::cookie::Authority::create(),
        mt::fake_shared(mock_hub)};

    mp::Void null;
    mp::BufferRequest request;

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());

    auto mock_stream = stubbed_session->mock_primary_stream_at(mf::SurfaceId{0});
    request.mutable_id()->set_value(surface_response.id().value());
    request.mutable_buffer()->set_buffer_id(buffer1->id().as_value());

    InSequence seq;
    EXPECT_CALL(mock_ipc_operations, unpack_buffer(_,_));
    EXPECT_CALL(*mock_stream, swap_buffers(_,_))
        .WillOnce(InvokeArgument<1>(buffer1.get()));
    EXPECT_CALL(*mock_sink, send_buffer(_, Ref(*buffer1), mg::BufferIpcMsgType::full_msg));

    mediator.submit_buffer(&request, &null, null_callback.get());
}

TEST_F(SessionMediator, allocates_from_the_session)
{
    using namespace testing;
    auto num_requests = 3u;
    mp::Void null;
    mp::BufferStreamId id;
    id.set_value(0);
    mp::BufferAllocation request;
    *request.mutable_id() = id;
    mg::BufferProperties properties(geom::Size{34, 84}, mir_pixel_format_abgr_8888, mg::BufferUsage::hardware);
    for(auto i = 0u; i < num_requests; i++)
    {
        auto buffer_request = request.add_buffer_requests();
        buffer_request->set_width(properties.size.width.as_int());
        buffer_request->set_height(properties.size.height.as_int());
        buffer_request->set_pixel_format(properties.format);
        buffer_request->set_buffer_usage((int)properties.usage);
    }

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());

    mediator.allocate_buffers(&request, &null, null_callback.get());
    EXPECT_THAT(stubbed_session->num_alloc_requests(), Eq(num_requests));
}

TEST_F(SessionMediator, removes_buffer_from_the_session)
{
    using namespace testing;
    auto num_requests = 3u;
    mp::Void null;
    mp::BufferStreamId id;
    id.set_value(0);
    mp::BufferRelease request;
    *request.mutable_id() = id;
    auto buffer_id = 442u;
    for(auto i = 0u; i < num_requests; i++)
    {
        auto buffer_request = request.add_buffers();
        buffer_request->set_buffer_id(buffer_id);
    }

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
    mediator.release_buffers(&request, &null, null_callback.get());
    EXPECT_THAT(stubbed_session->num_destroy_requests(), Eq(num_requests));
}

TEST_F(SessionMediator, doesnt_mind_swap_buffers_returning_nullptr_in_submit)
{
    using namespace testing;
    auto mock_stream = stubbed_session->mock_primary_stream_at(mf::SurfaceId{0});
    ON_CALL(*mock_stream, swap_buffers(_,_))
        .WillByDefault(InvokeArgument<1>(nullptr));
    auto buffer1 = std::make_shared<mtd::StubBuffer>();
    mf::SessionMediator mediator{
        shell, mt::fake_shared(mock_ipc_operations), graphics_changer,
        surface_pixel_formats, report,
        std::make_shared<mtd::NullEventSinkFactory>(),
        std::make_shared<mtd::NullMessageSender>(),
        resource_cache, stub_screencast, nullptr, nullptr, nullptr,
        std::make_shared<mtd::NullANRDetector>(),
        mir::cookie::Authority::create(),
        mt::fake_shared(mock_hub)};

    mp::Void null;
    mp::BufferRequest request;

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());

    request.mutable_id()->set_value(surface_response.id().value());
    request.mutable_buffer()->set_buffer_id(buffer1->id().as_value());

    InSequence seq;
    EXPECT_CALL(mock_ipc_operations, unpack_buffer(_,_));
    EXPECT_CALL(*mock_stream, swap_buffers(_,_))
        .WillOnce(InvokeArgument<1>(nullptr));

    mediator.submit_buffer(&request, &null, null_callback.get());
}

TEST_F(SessionMediator, doesnt_mind_swap_buffers_returning_nullptr_in_create)
{
    using namespace testing;
    mf::SurfaceId surf_id{0};
    mtd::StubBuffer buffer;

    auto stream = stubbed_session->mock_primary_stream_at(surf_id);
    ON_CALL(*stream, swap_buffers(_,_))
        .WillByDefault(InvokeArgument<1>(nullptr));

    Sequence seq;
    EXPECT_CALL(*stream, swap_buffers(_,_))
        .InSequence(seq)
        .WillOnce(InvokeArgument<1>(nullptr));

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
}

TEST_F(SessionMediator, doesnt_mind_swap_buffers_returning_nullptr_in_bstream_create)
{
    using namespace testing;
    mf::SurfaceId surf_id{0};
    mtd::StubBuffer buffer;

    auto stream = stubbed_session->mock_primary_stream_at(surf_id);
    ON_CALL(*stream, swap_buffers(_,_))
        .WillByDefault(InvokeArgument<1>(nullptr));

    Sequence seq;
    EXPECT_CALL(*stream, swap_buffers(_,_))
        .InSequence(seq)
        .WillOnce(InvokeArgument<1>(nullptr));

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());
}

TEST_F(SessionMediator, configures_swap_intervals_on_streams)
{
    using namespace testing;
    mf::SurfaceId surf_id{0};
    mp::StreamConfiguration request;
    mp::Void response;

    auto interval = 0u;
    mtd::StubBuffer buffer;

    auto stream = stubbed_session->mock_primary_stream_at(surf_id);
    EXPECT_CALL(*stream, allow_framedropping(true));

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_surface(&surface_parameters, &surface_response, null_callback.get());

    request.mutable_id()->set_value(surf_id.as_value());
    request.set_swapinterval(interval);
    mediator.configure_buffer_stream(&request, &response, null_callback.get());
}

namespace
{
MATCHER(IsReplyWithEvents, "")
{
    auto buffer = std::get<0>(arg);
    auto buffer_len = std::get<1>(arg);

    mir::protobuf::wire::Result result;
    if (!result.ParseFromArray(buffer, buffer_len))
    {
        *result_listener << "is not a protobuf Result";
        return false;
    }

    auto num_events = result.events_size();
    if (num_events > 0)
    {
        *result_listener << "has " << num_events << " events";
    }
    else
    {
        *result_listener << "has no events";
    }
    return num_events > 0;
}

void send_non_event(std::shared_ptr<mtd::MockMessageSender> sender)
{
    sender->send("hello", 5, {});
}
}

TEST_F(SessionMediator, events_sent_before_surface_creation_reply_are_buffered)
{
    using namespace testing;

    auto mock_sender = std::make_shared<mtd::MockMessageSender>();

    class EventSinkFactory : public mf::EventSinkFactory
    {
    public:
        EventSinkFactory(std::shared_ptr<mg::PlatformIpcOperations> const& ops)
            : ops{ops}
        {
        }

        std::unique_ptr<mf::EventSink> create_sink(
            std::shared_ptr<mf::MessageSender> const& sender)
        {
            return std::make_unique<mf::detail::EventSender>(sender, ops);
        }

    private:
        std::shared_ptr<mg::PlatformIpcOperations> const ops;
    };

    auto sink_factory = std::make_shared<EventSinkFactory>(mt::fake_shared(mock_ipc_operations));

    mf::SessionMediator mediator{
        shell, mt::fake_shared(mock_ipc_operations), graphics_changer,
        surface_pixel_formats, report, sink_factory,
        mock_sender,
        resource_cache, stub_screencast, nullptr, nullptr, nullptr,
        std::make_shared<mtd::NullANRDetector>(),
        mir::cookie::Authority::create(),
        mt::fake_shared(mock_hub)};

    ON_CALL(*shell, create_surface( _, _, _))
        .WillByDefault(
            Invoke([session = stubbed_session.get()](auto, auto params, auto sink)
                   {
                       sink->send_ping(0xdeadbeef);
                       return session->create_surface(params, sink);
                   }));

    InSequence seq;
    EXPECT_CALL(*mock_sender, send(_,_,_))
        .With(Args<0, 1>(Not(IsReplyWithEvents())));
    EXPECT_CALL(*mock_sender, send(_,_,_))
        .With(Args<0, 1>(IsReplyWithEvents()));

    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_surface(
        &surface_parameters,
        &surface_response,
        google::protobuf::NewCallback(&send_non_event, mock_sender));
}

TEST_F(SessionMediator, doesnt_inadventently_set_buffer_field_when_theres_no_buffer)
{
    mp::Void null;
    mf::SurfaceId surf_id{0};
    mp::BufferStreamParameters stream_request;
    mp::BufferStream stream_response;
    auto stream = stubbed_session->mock_primary_stream_at(surf_id);
    ON_CALL(*stream, swap_buffers(nullptr,testing::_))
        .WillByDefault(testing::InvokeArgument<1>(nullptr));
    mediator.connect(&connect_parameters, &connection, null_callback.get());
    mediator.create_buffer_stream(&stream_request, &stream_response, null_callback.get());
    EXPECT_FALSE(stream_response.has_buffer());
}

TEST_F(SessionMediator, sets_base_display_configuration)
{
    using namespace testing;

    mtd::StubDisplayConfig display_config{2};
    mp::DisplayConfiguration request;
    mp::Void response;

    mf::detail::pack_protobuf_display_configuration(request, display_config);

    auto mock_display_changer = std::make_shared<NiceMock<mtd::MockDisplayChanger>>();

    ON_CALL(*mock_display_changer, base_configuration())
        .WillByDefault(Return(mt::fake_shared(display_config)));
    EXPECT_CALL(*mock_display_changer,
                mock_set_base_configuration(
                    mt::DisplayConfigMatches(std::cref(display_config))));

    auto const mediator = create_session_mediator_with_display_changer(mock_display_changer);

    mediator->connect(&connect_parameters, &connection, null_callback.get());
    mediator->set_base_display_configuration(&request, &response, null_callback.get());
}

TEST_F(SessionMediator, sanitizes_base_display_configuration_before_setting)
{
    using namespace testing;

    auto const set_second_mode =
        [] (mg::UserDisplayConfigurationOutput& output)
        {
            output.current_mode_index = 1;
        };

    mtd::StubDisplayConfig single_output_base_config{1};
    mtd::StubDisplayConfig dual_output_requested_config{2};
    mtd::StubDisplayConfig single_output_sanitized_config{1};

    dual_output_requested_config.for_each_output(set_second_mode);
    single_output_sanitized_config.for_each_output(set_second_mode);

    mp::DisplayConfiguration request;
    mp::Void response;

    mf::detail::pack_protobuf_display_configuration(request, dual_output_requested_config);

    auto mock_display_changer = std::make_shared<NiceMock<mtd::MockDisplayChanger>>();

    ON_CALL(*mock_display_changer, base_configuration())
        .WillByDefault(Return(mt::fake_shared(single_output_base_config)));

    EXPECT_CALL(*mock_display_changer,
                mock_set_base_configuration(
                    mt::DisplayConfigMatches(std::cref(single_output_sanitized_config))));

    auto const mediator = create_session_mediator_with_display_changer(mock_display_changer);

    mediator->connect(&connect_parameters, &connection, null_callback.get());
    mediator->set_base_display_configuration(&request, &response, null_callback.get());
}

TEST_F(SessionMediator, raise_with_invalid_cookie_throws)
{
    mp::RaiseRequest raise_request;
    mediator.connect(&connect_parameters, &connection, null_callback.get());

    EXPECT_THROW({
        mediator.raise_surface(&raise_request, &void_response, null_callback.get());
    }, mir::cookie::SecurityCheckError);
}

TEST_F(SessionMediator, connect_sends_input_devices_at_seat)
{
    using namespace testing;
    mtd::StubDevice dev1{MirInputDeviceId{3}, mi::DeviceCapability::keyboard, "kbd", "kbd-aaf474"};
    mtd::StubDevice dev2{MirInputDeviceId{7}, mi::DeviceCapability::touchscreen, "ts", "ts-ewrkw2"};
    std::vector<std::shared_ptr<mir::input::Device>> devices{mt::fake_shared(dev1), mt::fake_shared(dev2)};
    ON_CALL(mock_hub, for_each_input_device(_))
        .WillByDefault(Invoke(
            [&](std::function<void(mir::input::Device const&)> const& callback)
            {
                for(auto const& dev : devices)
                    callback(*dev);
            }));

    mediator.connect(&connect_parameters, &connection, null_callback.get());

    EXPECT_THAT(connection.input_devices(), mt::InputDevicesMatch(devices));
}
