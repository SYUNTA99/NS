#include "ns/graphics/debug_draw.h"

namespace ns::graphics::DebugDraw
{
    //  で実装。現状は no-op stub。

    void Line(const ns::core::Vector3& /*a*/, const ns::core::Vector3& /*b*/, const ns::core::Color& /*color*/) noexcept
    {}

    void AABB(const ns::core::AABB& /*box*/, const ns::core::Color& /*color*/) noexcept {}

    void Capsule(const ns::core::Vector3& /*base*/,
                 const ns::core::Vector3& /*axis*/,
                 float /*radius*/,
                 const ns::core::Color& /*color*/) noexcept
    {}

    void Flush(Renderer& /*renderer*/, const ns::core::Matrix& /*viewProjection*/) noexcept {}

    void Clear() noexcept {}

    std::size_t VertexCount() noexcept
    {
        return 0;
    }
} // namespace ns::graphics::DebugDraw
