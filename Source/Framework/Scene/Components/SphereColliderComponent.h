#pragma once

/// @file SphereColliderComponent.h
/// @brief 球 collider Component。 owner の world 変換から中心と scale 込み半径を備えた WorldSphere を返す

#include "Framework/Math/Math.h"
#include "Framework/Physics/Sphere.h"
#include "Framework/Scene/Component.h"

namespace NS::Scene
{
    /// 球 collider を SceneBase に登録する Component
    /// 半径は owner scale の最大成分で拡縮する。 非一様 scale でも球を保つため最大軸を採用する
    /// 回転は球に無関係なので持たない。 当たり箱を視覚と独立にずらす offset のみ持つ
    class SphereColliderComponent : public Component
    {
    public:
        /// 既定半径 0.5 で構築する
        SphereColliderComponent() noexcept;
        /// 半径を指定して構築する。 負は 0 にクランプ
        explicit SphereColliderComponent(float radius) noexcept;

        /// 半径を設定 / 取得する。 負は 0 にクランプ
        void SetRadius(float radius) noexcept;
        [[nodiscard]] float Radius() const noexcept;

        /// owner local 空間での中心オフセットを設定 / 取得する
        void SetCenterOffset(const NS::Math::Vector3& offset) noexcept;
        [[nodiscard]] NS::Math::Vector3 CenterOffset() const noexcept;

        /// owner の world 変換に offset を重ねた球を返す。 半径は owner scale 最大成分で拡縮する
        /// Owner 未登録時は local offset / radius だけを反映する。 例外は投げない
        [[nodiscard]] NS::Physics::Sphere WorldSphere() const noexcept;

        /// owner の world 変換を反映した世界軸並行 AABB を返す。 Owner 未登録時は local だけを反映する
        [[nodiscard]] NS::Math::AABB WorldAABB() const noexcept;

        NS_REFLECT_BEGIN(SphereColliderComponent)
        NS_REFLECT_ACCESSOR(float, "Radius", Radius(), SetRadius)
        NS_REFLECT_ACCESSOR(NS::Math::Vector3, "Center Offset", CenterOffset(), SetCenterOffset)
        NS_REFLECT_END()

    private:
        float m_radius = 0.5f;
        NS::Math::Vector3 m_centerOffset{0.0f, 0.0f, 0.0f};
    };
} // namespace NS::Scene
