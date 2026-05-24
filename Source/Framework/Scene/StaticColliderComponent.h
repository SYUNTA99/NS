#pragma once

/// @file StaticColliderComponent.h
/// @brief 静的 AABB collider Component。Owner の Root::WorldPosition を center とし、
///        halfExtents から world AABB を返す。MainScene の collision world 構築に使う。

#include "Framework/Core/Math.h"
#include "Framework/Scene/Component.h"

namespace NS::Scene
{
    /// 軸並行 BoundingBox を RootScene に登録する Component。
    /// 回転非対応 (AABB 厳守)。Mesh と分離し、視覚と衝突を独立して調整可能にする。
    class StaticColliderComponent : public Component
    {
    public:
        StaticColliderComponent() noexcept = default;
        explicit StaticColliderComponent(const NS::Core::Vector3& halfExtents) noexcept;
        /// GameObject owner を受け取って auto-register する ctor。halfExtents は default {0.5,0.5,0.5}。
        explicit StaticColliderComponent(NS::Scene::GameObject* owner) noexcept;
        /// owner と halfExtents を同時に渡す ctor。halfExtents は ClampNonNegative で負を 0 にクランプ。
        StaticColliderComponent(NS::Scene::GameObject* owner, const NS::Core::Vector3& halfExtents) noexcept;

        /// 半サイズを設定。
        void SetHalfExtents(const NS::Core::Vector3& halfExtents) noexcept;

        /// 現在の半サイズ。
        [[nodiscard]] NS::Core::Vector3 HalfExtents() const noexcept;

        /// Owner の root world position を center とした AABB を返す。
        /// Owner が未登録の場合は origin 中心の AABB を返す (no-throw)。
        [[nodiscard]] NS::Core::AABB WorldAABB() const noexcept;

    private:
        NS::Core::Vector3 m_halfExtents{0.5f, 0.5f, 0.5f};
    };
} // namespace NS::Scene
