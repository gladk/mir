/*
 * Copyright © 2013 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mir/shared_library.h"

#ifdef MIR_DONT_USE_DLVSYM
#include <mir/log.h>
#endif

#include <boost/throw_exception.hpp>

#include <dlfcn.h>

#include <stdexcept>

mir::SharedLibrary::SharedLibrary(char const* library_name) :
    so(dlopen(library_name, RTLD_NOW | RTLD_LOCAL))
{
    if (!so)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error(dlerror()));
    }
}

mir::SharedLibrary::SharedLibrary(std::string const& library_name) :
    SharedLibrary(library_name.c_str()) {}

mir::SharedLibrary::~SharedLibrary() noexcept
{
    dlclose(so);
}

void* mir::SharedLibrary::load_symbol(char const* function_name) const
{
    if (void* result = dlsym(so, function_name))
    {
        return result;
    }
    else
    {
        BOOST_THROW_EXCEPTION(std::runtime_error(dlerror()));
    }
}

void* mir::SharedLibrary::load_symbol(char const* function_name, char const* version) const
{
    // Some libc implementations (such as musl) do not support dlvsym

#ifdef MIR_DONT_USE_DLVSYM
    // Load the function without checking the version
    log_debug("Cannot check \"%s\" symbol version is \"%s\": dlvsym() is unavailable", function_name, version);
    return load_symbol(function_name);
#else
    if (void* result = dlvsym(so, function_name, version))
    {
        return result;
    }
    else
    {
        BOOST_THROW_EXCEPTION(std::runtime_error(dlerror()));
    }
#endif
}
