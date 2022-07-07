/*
 * Copyright © 2022 Canonical Ltd.
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

#ifndef MIR_COMPOSITOR_BASIC_SCREEN_SHOOTER_H_
#define MIR_COMPOSITOR_BASIC_SCREEN_SHOOTER_H_

#include "mir/compositor/screen_shooter.h"
#include "mir/time/clock.h"

#include <mutex>

namespace mir
{
class Executor;
namespace renderer
{
class Renderer;
namespace gl
{
class BufferOutputSurface;
}
}
namespace compositor
{
class Scene;

class BasicScreenShooter: public ScreenShooter
{
public:
    BasicScreenShooter(
        std::shared_ptr<Scene> const& scene,
        std::shared_ptr<time::Clock> const& clock,
        Executor& executor,
        std::unique_ptr<renderer::gl::BufferOutputSurface>&& render_target,
        std::unique_ptr<renderer::Renderer>&& renderer);

    void capture(
        std::shared_ptr<renderer::software::WriteMappableBuffer> const& buffer,
        geometry::Rectangle const& area,
        std::function<void(std::optional<time::Timestamp>)>&& callback) override;

private:
    struct Self
    {
        Self(
            std::shared_ptr<Scene> const& scene,
            std::shared_ptr<time::Clock> const& clock,
            std::unique_ptr<renderer::gl::BufferOutputSurface>&& render_target,
            std::unique_ptr<renderer::Renderer>&& renderer);

        auto render(
            std::shared_ptr<renderer::software::WriteMappableBuffer> const& buffer,
            geometry::Rectangle const& area) -> time::Timestamp;

        std::mutex mutex;
        std::shared_ptr<Scene> const scene;
        std::unique_ptr<renderer::gl::BufferOutputSurface> const render_target;
        std::unique_ptr<renderer::Renderer> const renderer;
        std::shared_ptr<time::Clock> const clock;
    };
    std::shared_ptr<Self> const self;
    Executor& executor;
};
}
}

#endif // MIR_COMPOSITOR_BASIC_SCREEN_SHOOTER_H_
