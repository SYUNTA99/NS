#pragma once

/// @file static_collider_component.h
/// @brief 静的 AABB collider Component。Owner の Root::WorldPosition を center とし、
///        halfExtents から world AABB を返す。MainScene の collision world 構築に使う。

#include "ns/core/math.h"
#include "ns/scene/component.h"

namespace ns::scene
{
    /// 軸並行 BoundingBox を World に登録する Component。
    /// 回転非対応 (AABB 厳守)。Mesh と分離し、視覚と衝突を独立して調整可能にする。
    class StaticColliderComponent : public Component
    {
    public:
        StaticColliderComponent() noexcept = default;
        explicit StaticColliderComponent(const ns::core::Vector3& halfExtents) noexcept;

        /// 半サイズを設定。
        void SetHalfExtents(const ns::core::Vector3& halfExtents) noexcept;

        /// 現在の半サイズ。
        [[nodiscard]] ns::core::Vector3 HalfExtents() const noexcept;

        /// Owner の root world position を center とした AABB を返す。
        /// Owner が未登録の場合は origin 中心の AABB を返す (no-throw)。
        [[nodiscard]] ns::core::AABB WorldAABB() const noexcept;

    private:
        ns::core::Vector3 m_halfExtents{0.5f, 0.5f, 0.5f};
    };
} // namespace ns::scene
