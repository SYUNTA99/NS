#pragma once

/// @file debug_draw.h
/// @brief ns::graphics::DebugDraw — line / AABB / Capsule wireframe 描画。
///
/// 蓄積 → `Flush(renderer, vp)` で 1 描画呼出。 で実装。

#include "ns/core/math.h"

#include <cstddef>

namespace ns::graphics
{
    class Renderer;
}

namespace ns::graphics::DebugDraw
{
    /// Line 1 本を蓄積。 で実装。
    void Line(const ns::core::Vector3& a, const ns::core::Vector3& b, const ns::core::Color& color) noexcept;
    /// AABB wireframe (12 line) を蓄積。
    void AABB(const ns::core::AABB& box, const ns::core::Color& color) noexcept;
    /// Capsule wireframe を蓄積。
    void Capsule(const ns::core::Vector3& base,
                 const ns::core::Vector3& axis,
                 float radius,
                 const ns::core::Color& color) noexcept;
    /// 蓄積を 1 描画呼出 (line list) で出力し、内部バッファを clear する。
    void Flush(Renderer& renderer, const ns::core::Matrix& viewProjection) noexcept;
    /// バッファを破棄。Flush を呼ばないフレーム末尾用。
    void Clear() noexcept;
    /// テスト用。内部 vertex 蓄積数を返す。
    [[nodiscard]] std::size_t VertexCount() noexcept;
} // namespace ns::graphics::DebugDraw
