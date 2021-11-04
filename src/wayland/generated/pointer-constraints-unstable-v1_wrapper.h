/*
 * AUTOGENERATED - DO NOT EDIT
 *
 * This file is generated from pointer-constraints-unstable-v1.xml
 * To regenerate, run the “refresh-wayland-wrapper” target.
 */

#ifndef MIR_FRONTEND_WAYLAND_POINTER_CONSTRAINTS_UNSTABLE_V1_XML_WRAPPER
#define MIR_FRONTEND_WAYLAND_POINTER_CONSTRAINTS_UNSTABLE_V1_XML_WRAPPER

#include <optional>

#include "mir/fd.h"
#include <wayland-server-core.h>

#include "mir/wayland/wayland_base.h"

namespace mir
{
namespace wayland
{

class PointerConstraintsV1;
class LockedPointerV1;
class ConfinedPointerV1;

class PointerConstraintsV1 : public Resource
{
public:
    static char const constexpr* interface_name = "zwp_pointer_constraints_v1";

    static PointerConstraintsV1* from(struct wl_resource*);

    PointerConstraintsV1(struct wl_resource* resource, Version<1>);
    virtual ~PointerConstraintsV1();

    struct wl_client* const client;
    struct wl_resource* const resource;

    struct Error
    {
        static uint32_t const already_constrained = 1;
    };

    struct Lifetime
    {
        static uint32_t const oneshot = 1;
        static uint32_t const persistent = 2;
    };

    struct Thunks;

    static bool is_instance(wl_resource* resource);

private:
    virtual void lock_pointer(struct wl_resource* id, struct wl_resource* surface, struct wl_resource* pointer, std::optional<struct wl_resource*> const& region, uint32_t lifetime) = 0;
    virtual void confine_pointer(struct wl_resource* id, struct wl_resource* surface, struct wl_resource* pointer, std::optional<struct wl_resource*> const& region, uint32_t lifetime) = 0;
};

class PointerConstraintsV1Global : public wayland::Global
{
public:
    PointerConstraintsV1Global(wl_display* display, Version<1>);

private:
    virtual void bind(wl_resource* new_zwp_pointer_constraints_v1) = 0;
    friend PointerConstraintsV1::Thunks;
};

class LockedPointerV1 : public Resource
{
public:
    static char const constexpr* interface_name = "zwp_locked_pointer_v1";

    static LockedPointerV1* from(struct wl_resource*);

    LockedPointerV1(struct wl_resource* resource, Version<1>);
    virtual ~LockedPointerV1();

    void send_locked_event() const;
    void send_unlocked_event() const;

    struct wl_client* const client;
    struct wl_resource* const resource;

    struct Opcode
    {
        static uint32_t const locked = 0;
        static uint32_t const unlocked = 1;
    };

    struct Thunks;

    static bool is_instance(wl_resource* resource);

private:
    virtual void set_cursor_position_hint(double surface_x, double surface_y) = 0;
    virtual void set_region(std::optional<struct wl_resource*> const& region) = 0;
};

class ConfinedPointerV1 : public Resource
{
public:
    static char const constexpr* interface_name = "zwp_confined_pointer_v1";

    static ConfinedPointerV1* from(struct wl_resource*);

    ConfinedPointerV1(struct wl_resource* resource, Version<1>);
    virtual ~ConfinedPointerV1();

    void send_confined_event() const;
    void send_unconfined_event() const;

    struct wl_client* const client;
    struct wl_resource* const resource;

    struct Opcode
    {
        static uint32_t const confined = 0;
        static uint32_t const unconfined = 1;
    };

    struct Thunks;

    static bool is_instance(wl_resource* resource);

private:
    virtual void set_region(std::optional<struct wl_resource*> const& region) = 0;
};

}
}

#endif // MIR_FRONTEND_WAYLAND_POINTER_CONSTRAINTS_UNSTABLE_V1_XML_WRAPPER
