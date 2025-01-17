/*
 * Copyright © 2014 Canonical Ltd.
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

#ifndef MIR_TEST_BARRIER_H_
#define MIR_TEST_BARRIER_H_

#include <mutex>
#include <condition_variable>

namespace mir
{
namespace test
{
class Barrier
{
public:
    explicit Barrier(unsigned wait_threads) : wait_threads{wait_threads} {}

    void reset(unsigned threads)
    {
        std::unique_lock lock(mutex);
        wait_threads  = threads;
    }

    void ready()
    {
        std::unique_lock lock(mutex);
        if (--wait_threads)
        {
            if (!cv.wait_for(lock, std::chrono::minutes(1), [&]{ return wait_threads == 0; }))
                throw std::runtime_error("Timeout");
        }
        else
        {
            lock.unlock();
            cv.notify_all();
        }
    }

    Barrier(Barrier const&) = delete;
    Barrier& operator=(Barrier const&) = delete;
private:
    unsigned wait_threads;
    std::mutex mutex;
    std::condition_variable cv;
};
}
}



#endif /* MIR_TEST_BARRIER_H_ */
