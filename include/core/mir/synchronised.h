/*
 * Copyright © 2017 Canonical Ltd.
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

#ifndef MIR_SYNCHRONISED_H_
#define MIR_SYNCHRONISED_H_

#include <mutex>
#include <type_traits>
#include <optional>

namespace mir
{

/**
 * An object that enforces unique access to the data it contains
 *
 * This behaves like a mutex which owns the data it guards, and
 * can give out a smart-pointer-esque handle to lock and access it.
 *
 * \tparam T  The type of data contained
 */
template<typename T, typename Mutex = std::mutex>
class Synchronised
{
public:
    Synchronised() = default;
    Synchronised(T&& initial_value)
        : value{std::move(initial_value)}
    {
    }

    Synchronised(Synchronised const&) = delete;
    Synchronised& operator=(Synchronised const&) = delete;

    /**
     * Smart-pointer-esque accessor for the protected data.
     *
     * Ensures exclusive access to the referenced data.
     *
     * \note Instances of Locked must not outlive the Synchronised
     *       they are derived from.
     */
    template<typename U>
    class LockedImpl
    {
    public:
        LockedImpl(LockedImpl&& from) noexcept
            : value{from.value},
              lock{std::move(from.lock)}
        {
            from.value = nullptr;
        }

        ~LockedImpl() = default;

        auto operator*() const -> U&
        {
            return *value;
        }

        auto operator->() const -> U*
        {
            return value;
        }

        /**
         * Relinquish access to the data
         *
         * This prevents further access to the contained data through
         * this handle, and allows other code to acquire access.
         */
        void drop()
        {
            value = nullptr;
            lock.unlock();
        }

    private:
        friend class Synchronised;
        LockedImpl(std::unique_lock<std::mutex>&& lock, U& value)
            : value{&value},
              lock{std::move(lock)}
        {
        }

        U* value;
        std::unique_lock<std::mutex> lock;
    };

    /**
     * Smart-pointer-esque accessor for the protected data.
     *
     * Ensures exclusive access to the referenced data.
     *
     * \note Instances of Locked must not outlive the Synchronised
     *       they are derived from.
     */
    using Locked = LockedImpl<T>;
    /**
     * Lock the data and return an accessor.
     *
     * \return A smart-pointer-esque accessor for the contained data.
     *         While code has access to a live Locked instance it is
     *         guaranteed to have unique access to the contained data.
     */
    auto lock() -> Locked
    {
        return LockedImpl<T>{std::unique_lock{mutex}, value};
    }

    /**
     * Smart-pointer-esque accessor for the protected data.
     *
     * Provides const access to the protected data, with the guarantee
     * that no changes to the data can be made while this handle
     * is live.
     *
     * \note Instances of Locked must not outlive the Synchronised
     *       they are derived from.
     */
    using LockedView = LockedImpl<T const>;
     /**
     * Lock the data and return an accessor.
     *
     * \return A smart-pointer-esque accessor for the contained data.
     *         While code has access to a live Locked instance it is
     *         guaranteed to have unique access to the contained data.
     */
    auto lock() const -> LockedView
    {
        return LockedImpl<T const>{std::unique_lock{mutex}, value};
    }

    template<typename Rep, typename Period, typename = std::enable_if_t<std::is_invocable_v<decltype(&Mutex::try_lock_for), Mutex&, std::chrono::duration<Rep, Period> const&>>>
    auto try_lock_for(std::chrono::duration<Rep, Period> const& timeout) const -> std::optional<Locked>
    {
        std::unique_lock<Mutex> lock{mutex, timeout};
        if (lock.owns_lock())
        {
            return LockedImpl<T>{std::move(lock), value};
        }
        return {};        
    }
private:
    Mutex mutable mutex;
    T value;
};

}

#endif //MIR_SYNCHRONISED_H_
