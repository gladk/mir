/*
 * AUTOGENERATED - DO NOT EDIT
 *
 * This file is generated from virtual-keyboard-unstable-v1.xml
 * To regenerate, run the “refresh-wayland-wrapper” target.
 */

#include "virtual-keyboard-unstable-v1_wrapper.h"

#include <boost/throw_exception.hpp>
#include <boost/exception/diagnostic_information.hpp>

#include <wayland-server-core.h>

#include "mir/log.h"

namespace mir
{
namespace wayland
{
extern struct wl_interface const wl_seat_interface_data;
extern struct wl_interface const zwp_virtual_keyboard_manager_v1_interface_data;
extern struct wl_interface const zwp_virtual_keyboard_v1_interface_data;
}
}

namespace mw = mir::wayland;

namespace
{
struct wl_interface const* all_null_types [] {
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr};
}

// VirtualKeyboardV1

struct mw::VirtualKeyboardV1::Thunks
{
    static int const supported_version;

    static void keymap_thunk(struct wl_client* client, struct wl_resource* resource, uint32_t format, int32_t fd, uint32_t size)
    {
        mir::Fd fd_resolved{fd};
        try
        {
            auto me = static_cast<VirtualKeyboardV1*>(wl_resource_get_user_data(resource));
            me->keymap(format, fd_resolved, size);
        }
        catch(ProtocolError const& err)
        {
            wl_resource_post_error(err.resource(), err.code(), "%s", err.message());
        }
        catch(...)
        {
            internal_error_processing_request(client, "VirtualKeyboardV1::keymap()");
        }
    }

    static void key_thunk(struct wl_client* client, struct wl_resource* resource, uint32_t time, uint32_t key, uint32_t state)
    {
        try
        {
            auto me = static_cast<VirtualKeyboardV1*>(wl_resource_get_user_data(resource));
            me->key(time, key, state);
        }
        catch(ProtocolError const& err)
        {
            wl_resource_post_error(err.resource(), err.code(), "%s", err.message());
        }
        catch(...)
        {
            internal_error_processing_request(client, "VirtualKeyboardV1::key()");
        }
    }

    static void modifiers_thunk(struct wl_client* client, struct wl_resource* resource, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
    {
        try
        {
            auto me = static_cast<VirtualKeyboardV1*>(wl_resource_get_user_data(resource));
            me->modifiers(mods_depressed, mods_latched, mods_locked, group);
        }
        catch(ProtocolError const& err)
        {
            wl_resource_post_error(err.resource(), err.code(), "%s", err.message());
        }
        catch(...)
        {
            internal_error_processing_request(client, "VirtualKeyboardV1::modifiers()");
        }
    }

    static void destroy_thunk(struct wl_client* client, struct wl_resource* resource)
    {
        try
        {
            wl_resource_destroy(resource);
        }
        catch(ProtocolError const& err)
        {
            wl_resource_post_error(err.resource(), err.code(), "%s", err.message());
        }
        catch(...)
        {
            internal_error_processing_request(client, "VirtualKeyboardV1::destroy()");
        }
    }

    static void resource_destroyed_thunk(wl_resource* resource)
    {
        delete static_cast<VirtualKeyboardV1*>(wl_resource_get_user_data(resource));
    }

    static struct wl_message const request_messages[];
    static void const* request_vtable[];
};

int const mw::VirtualKeyboardV1::Thunks::supported_version = 1;

mw::VirtualKeyboardV1::VirtualKeyboardV1(struct wl_resource* resource, Version<1>)
    : client{wl_resource_get_client(resource)},
      resource{resource}
{
    if (resource == nullptr)
    {
        BOOST_THROW_EXCEPTION((std::bad_alloc{}));
    }
    wl_resource_set_implementation(resource, Thunks::request_vtable, this, &Thunks::resource_destroyed_thunk);
}

mw::VirtualKeyboardV1::~VirtualKeyboardV1()
{
    wl_resource_set_implementation(resource, nullptr, nullptr, nullptr);
}

bool mw::VirtualKeyboardV1::is_instance(wl_resource* resource)
{
    return wl_resource_instance_of(resource, &zwp_virtual_keyboard_v1_interface_data, Thunks::request_vtable);
}

uint32_t const mw::VirtualKeyboardV1::Error::no_keymap;

struct wl_message const mw::VirtualKeyboardV1::Thunks::request_messages[] {
    {"keymap", "uhu", all_null_types},
    {"key", "uuu", all_null_types},
    {"modifiers", "uuuu", all_null_types},
    {"destroy", "1", all_null_types}};

void const* mw::VirtualKeyboardV1::Thunks::request_vtable[] {
    (void*)Thunks::keymap_thunk,
    (void*)Thunks::key_thunk,
    (void*)Thunks::modifiers_thunk,
    (void*)Thunks::destroy_thunk};

mw::VirtualKeyboardV1* mw::VirtualKeyboardV1::from(struct wl_resource* resource)
{
    if (wl_resource_instance_of(resource, &zwp_virtual_keyboard_v1_interface_data, VirtualKeyboardV1::Thunks::request_vtable))
    {
        return static_cast<VirtualKeyboardV1*>(wl_resource_get_user_data(resource));
    }
    return nullptr;
}

// VirtualKeyboardManagerV1

struct mw::VirtualKeyboardManagerV1::Thunks
{
    static int const supported_version;

    static void create_virtual_keyboard_thunk(struct wl_client* client, struct wl_resource* resource, struct wl_resource* seat, uint32_t id)
    {
        wl_resource* id_resolved{
            wl_resource_create(client, &zwp_virtual_keyboard_v1_interface_data, wl_resource_get_version(resource), id)};
        if (id_resolved == nullptr)
        {
            wl_client_post_no_memory(client);
            BOOST_THROW_EXCEPTION((std::bad_alloc{}));
        }
        try
        {
            auto me = static_cast<VirtualKeyboardManagerV1*>(wl_resource_get_user_data(resource));
            me->create_virtual_keyboard(seat, id_resolved);
        }
        catch(ProtocolError const& err)
        {
            wl_resource_post_error(err.resource(), err.code(), "%s", err.message());
        }
        catch(...)
        {
            internal_error_processing_request(client, "VirtualKeyboardManagerV1::create_virtual_keyboard()");
        }
    }

    static void resource_destroyed_thunk(wl_resource* resource)
    {
        delete static_cast<VirtualKeyboardManagerV1*>(wl_resource_get_user_data(resource));
    }

    static void bind_thunk(struct wl_client* client, void* data, uint32_t version, uint32_t id)
    {
        auto me = static_cast<VirtualKeyboardManagerV1Global*>(data);
        auto resource = wl_resource_create(
            client,
            &zwp_virtual_keyboard_manager_v1_interface_data,
            std::min((int)version, VirtualKeyboardManagerV1::Thunks::supported_version),
            id);
        if (resource == nullptr)
        {
            wl_client_post_no_memory(client);
            BOOST_THROW_EXCEPTION((std::bad_alloc{}));
        }
        try
        {
            me->bind(resource);
        }
        catch(...)
        {
            internal_error_processing_request(client, "VirtualKeyboardManagerV1 global bind");
        }
    }

    static struct wl_interface const* create_virtual_keyboard_types[];
    static struct wl_message const request_messages[];
    static void const* request_vtable[];
};

int const mw::VirtualKeyboardManagerV1::Thunks::supported_version = 1;

mw::VirtualKeyboardManagerV1::VirtualKeyboardManagerV1(struct wl_resource* resource, Version<1>)
    : client{wl_resource_get_client(resource)},
      resource{resource}
{
    if (resource == nullptr)
    {
        BOOST_THROW_EXCEPTION((std::bad_alloc{}));
    }
    wl_resource_set_implementation(resource, Thunks::request_vtable, this, &Thunks::resource_destroyed_thunk);
}

mw::VirtualKeyboardManagerV1::~VirtualKeyboardManagerV1()
{
    wl_resource_set_implementation(resource, nullptr, nullptr, nullptr);
}

bool mw::VirtualKeyboardManagerV1::is_instance(wl_resource* resource)
{
    return wl_resource_instance_of(resource, &zwp_virtual_keyboard_manager_v1_interface_data, Thunks::request_vtable);
}

void mw::VirtualKeyboardManagerV1::destroy_and_delete() const
{
    // Will result in this object being deleted
    wl_resource_destroy(resource);
}

uint32_t const mw::VirtualKeyboardManagerV1::Error::unauthorized;

mw::VirtualKeyboardManagerV1Global::VirtualKeyboardManagerV1Global(wl_display* display, Version<1>)
    : wayland::Global{
          wl_global_create(
              display,
              &zwp_virtual_keyboard_manager_v1_interface_data,
              VirtualKeyboardManagerV1::Thunks::supported_version,
              this,
              &VirtualKeyboardManagerV1::Thunks::bind_thunk)}
{
}

struct wl_interface const* mw::VirtualKeyboardManagerV1::Thunks::create_virtual_keyboard_types[] {
    &wl_seat_interface_data,
    &zwp_virtual_keyboard_v1_interface_data};

struct wl_message const mw::VirtualKeyboardManagerV1::Thunks::request_messages[] {
    {"create_virtual_keyboard", "on", create_virtual_keyboard_types}};

void const* mw::VirtualKeyboardManagerV1::Thunks::request_vtable[] {
    (void*)Thunks::create_virtual_keyboard_thunk};

mw::VirtualKeyboardManagerV1* mw::VirtualKeyboardManagerV1::from(struct wl_resource* resource)
{
    if (wl_resource_instance_of(resource, &zwp_virtual_keyboard_manager_v1_interface_data, VirtualKeyboardManagerV1::Thunks::request_vtable))
    {
        return static_cast<VirtualKeyboardManagerV1*>(wl_resource_get_user_data(resource));
    }
    return nullptr;
}

namespace mir
{
namespace wayland
{

struct wl_interface const zwp_virtual_keyboard_v1_interface_data {
    mw::VirtualKeyboardV1::interface_name,
    mw::VirtualKeyboardV1::Thunks::supported_version,
    4, mw::VirtualKeyboardV1::Thunks::request_messages,
    0, nullptr};

struct wl_interface const zwp_virtual_keyboard_manager_v1_interface_data {
    mw::VirtualKeyboardManagerV1::interface_name,
    mw::VirtualKeyboardManagerV1::Thunks::supported_version,
    1, mw::VirtualKeyboardManagerV1::Thunks::request_messages,
    0, nullptr};

}
}
