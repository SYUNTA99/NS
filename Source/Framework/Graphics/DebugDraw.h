#pragma once

/// @file DebugDraw.h
/// @brief NS::Graphics::DebugDraw — line / AABB / Capsule wireframe 描画。
///
/// 蓄積 → `Flush(renderer, vp)` で 1 描画呼出。 で実装。

#include "Framework/Core/Math.h"

#include <cstddef>

namespace NS::Graphics
{
    class Renderer;
}

namespace NS::Graphics::DebugDraw
{
    /// Line 1 本を蓄積。 で実装。
    void Line(const NS::Core::Vector3& a, const NS::Core::Vector3& b, const NS::Core::Color& color) noexcept;
    /// AABB wireframe (12 line) を蓄積。
    void AABB(const NS::Core::AABB& box, const NS::Core::Color& color) noexcept;
    /// Capsule wireframe を蓄積。
    void Capsule(const NS::Core::Vector3& base,
                 const NS::Core::Vector3& axis,
                 float radius,
                 const NS::Core::Color& color) noexcept;
    /// 蓄積を 1 描画呼出 (line list) で出力し、内部バッファを clear する。
    void Flush(Renderer& renderer, const NS::Core::Matrix& viewProjection) noexcept;
    /// バッファを破棄。Flush を呼ばないフレーム末尾用。
    void Clear() noexcept;
    /// テスト用。内部 vertex 蓄積数を返す。
    [[nodiscard]] std::size_t VertexCount() noexcept;
} // namespace NS::Graphics::DebugDraw
