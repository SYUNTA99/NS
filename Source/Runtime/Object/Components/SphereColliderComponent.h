#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/ColliderComponent.h"

namespace NS::Object
{
    //! @brief 球 collider を Scene に登録する Component
    //! @details owner の world 変換から中心と scale 込み半径を備えた WorldSphere を返す
    //! 半径は owner scale の最大成分で拡縮する。非一様 scale でも球を保つため最大軸を採用する
    //! 回転は球に無関係なので持たない。当たり箱を視覚と独立にずらす offset のみ持つ
    class SphereColliderComponent : public ColliderComponent
    {
    public:
        //! 既定半径 0.5 で構築する
        SphereColliderComponent() noexcept;
        //! 半径を指定して構築する。 負は 0 にクランプ
        explicit SphereColliderComponent(float radius) noexcept;

        //! 負は 0 にクランプ
        void SetRadius(float radius) noexcept;
        [[nodiscard]] float Radius() const noexcept;

        //! owner local 空間での中心オフセットを設定 / 取得する
        void SetCenterOffset(const NS::Core::Vector3& offset) noexcept;
        [[nodiscard]] NS::Core::Vector3 CenterOffset() const noexcept;

        //! owner の world 変換に offset を重ねた球を返す。 半径は owner scale 最大成分で拡縮する
        //! Owner 未登録時は local offset / radius だけを反映する。 例外は投げない
        [[nodiscard]] NS::Core::Sphere WorldSphere() const noexcept;

        //! owner の world 変換を反映した世界軸並行 AABB を返す。 Owner 未登録時は local だけを反映する
        [[nodiscard]] NS::Core::AABB WorldAABB() const noexcept;

        //! world 座標の球を physics へ入れる
        void AddToPhysics(NS::Physics::PhysicsWorld& physics) const override;

        NS_REFLECT_BEGIN(SphereColliderComponent, ColliderComponent)
        NS_REFLECT_ACCESSOR(float, "半径", Radius(), SetRadius)
        NS_REFLECT_ACCESSOR(NS::Core::Vector3, "中心オフセット", CenterOffset(), SetCenterOffset)
        NS_REFLECT_END()

    private:
        float m_radius = 0.5f;                              // 球の半径 (owner scale 前)
        NS::Core::Vector3 m_centerOffset{0.0f, 0.0f, 0.0f}; // owner local 空間での中心オフセット
    };
} // namespace NS::Object
