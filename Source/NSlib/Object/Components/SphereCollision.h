#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/Object/Components/Collision.h"

namespace NS::Obj
{
    //! @brief 球 collision を Scene に登録する Component
    //! @details owner の world 変換から中心と scale 込み半径を備えた WorldSphere を返す
    //! 半径は owner scale の最大成分で拡縮する。非一様 scale でも球を保つため最大軸を採用する
    //! 回転は球に無関係なので持たない。当たり箱を視覚と独立にずらす offset のみ持つ
    class SphereCollision : public Collision
    {
    public:
        //! 既定半径 0.5 で構築する
        SphereCollision() noexcept;
        //! 半径を指定して構築する。負は 0 にクランプ
        explicit SphereCollision(float radius) noexcept;

        //! 負は 0 にクランプ
        void SetRadius(float radius) noexcept;
        //! 半径を返す。owner の scale を掛ける前の値
        [[nodiscard]] float Radius() const noexcept;

        //! owner local 空間での中心オフセットを設定 / 取得する
        void SetCenterOffset(const NS::Vector3& offset) noexcept;
        [[nodiscard]] NS::Vector3 CenterOffset() const noexcept;

        //! owner の world 変換に offset を重ねた球を返す。半径は owner scale 最大成分で拡縮する
        //! Owner 未登録時は local offset / radius だけを反映する。例外は投げない
        [[nodiscard]] NS::Sphere WorldSphere() const noexcept;

        //! owner の world 変換を反映した世界軸並行 AABB を返す。Owner 未登録時は local だけを反映する
        [[nodiscard]] NS::AABB WorldAABB() const noexcept;

        NS_REFLECT_BEGIN(SphereCollision, Collision)
        NS_REFLECT_ACCESSOR(float, "半径", Radius(), SetRadius)
        NS_REFLECT_ACCESSOR(NS::Vector3, "中心オフセット", CenterOffset(), SetCenterOffset)
        NS_REFLECT_END()

    private:
        // world 座標の球を body 1 個として入れる
        [[nodiscard]] JPH::BodyID SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current) override;

        float m_radius = 0.5f;                              // 球の半径 (owner scale 前)
        NS::Vector3 m_centerOffset{0.0f, 0.0f, 0.0f}; // owner local 空間での中心オフセット
    };
} // namespace NS::Obj
