#pragma once

/// @file BoxColliderComponent.h
/// @brief 箱型 collider Component。 owner の world 変換から WorldAABB (内包軸並行) と
///        WorldOBB (回転・非一様 scale 厳密) を返す。 LevelPlayScene の collision world 構築に使う

#include "Framework/Math/Math.h"
#include "Framework/Physics/SweptOBB.h"
#include "Framework/Scene/Component.h"

namespace NS::Scene
{
    /// 箱型 collider を SceneBase に登録する Component
    /// WorldAABB は回転時に内包軸並行ボックスへ畳むが、 WorldOBB は回転・非一様 scale を厳密に保持する
    /// Mesh と分離し、 視覚と衝突を独立して調整可能にする
    class BoxColliderComponent : public Component
    {
    public:
        /// 既定 halfExtents {0.5,0.5,0.5} で構築する
        BoxColliderComponent() noexcept;
        /// halfExtents を指定して構築する。 ClampNonNegative で負を 0 にクランプ
        explicit BoxColliderComponent(const NS::Math::Vector3& halfExtents) noexcept;

        /// 半サイズを設定
        void SetHalfExtents(const NS::Math::Vector3& halfExtents) noexcept;

        /// 現在の半サイズ
        [[nodiscard]] NS::Math::Vector3 HalfExtents() const noexcept;

        /// owner local 空間での中心オフセットを設定 / 取得する。 当たり箱を視覚と独立にずらすのに使う
        void SetCenterOffset(const NS::Math::Vector3& offset) noexcept;
        [[nodiscard]] NS::Math::Vector3 CenterOffset() const noexcept;

        /// owner local 空間での回転を quaternion で設定 / 取得する (owner 回転にこれを重ねて当たり箱を回す)
        void SetLocalRotation(const NS::Math::Quaternion& rotation) noexcept;
        [[nodiscard]] NS::Math::Quaternion LocalRotation() const noexcept;

        /// local 回転を Euler (度・ pitch/yaw/roll) で読み書きする Inspector 向け窓口。 内部は quaternion 保持
        void SetRotationEulerDegrees(const NS::Math::Vector3& eulerDegrees) noexcept;
        [[nodiscard]] NS::Math::Vector3 RotationEulerDegrees() const noexcept;

        /// Owner の world 変換に当たり箱の local offset / 回転を重ねた AABB を返す (回転時は内包軸並行へ畳む)
        /// Owner が未登録の場合は local offset / 回転だけを反映した AABB を返す (例外を投げない)
        [[nodiscard]] NS::Math::AABB WorldAABB() const noexcept;

        /// Owner の world 変換に当たり箱の local offset / 回転を重ねた有向境界ボックスを返す
        /// 回転・非一様 scale を厳密に保持する。 Owner 未登録時は local offset / 回転だけを反映する
        [[nodiscard]] NS::Physics::OBB WorldOBB() const noexcept;

        // 当たり箱の形状 (半径) と Transform からの独立オフセット / 回転を Inspector へ公開する
        // 半径は負クランプ、 回転は Euler 度で受けるため全て setter 経由で書く
        NS_REFLECT_BEGIN(BoxColliderComponent)
        NS_REFLECT_ACCESSOR(NS::Math::Vector3, "Half Extents", HalfExtents(), SetHalfExtents)
        NS_REFLECT_ACCESSOR(NS::Math::Vector3, "Center Offset", CenterOffset(), SetCenterOffset)
        NS_REFLECT_ACCESSOR(NS::Math::Vector3, "Rotation (deg)", RotationEulerDegrees(), SetRotationEulerDegrees)
        NS_REFLECT_END()

    private:
        // owner world 変換に重ねる当たり箱の local 変換 (offset + 回転) を行列化する
        [[nodiscard]] NS::Math::Matrix LocalMatrix() const noexcept;

        NS::Math::Vector3 m_halfExtents{0.5f, 0.5f, 0.5f};
        NS::Math::Vector3 m_centerOffset{0.0f, 0.0f, 0.0f};
        NS::Math::Quaternion m_localRotation = NS::Math::Quaternion::Identity;
    };
} // namespace NS::Scene
