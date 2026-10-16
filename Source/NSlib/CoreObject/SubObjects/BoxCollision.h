#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/SubObjects/Collision.h"

namespace NS
{
    //! 描画と独立した箱の衝突形状。WorldOBB は回転と非一様スケールを保持する
    class BoxCollision : public Collision
    {
    public:
        //! 既定 halfExtents {0.5,0.5,0.5} で構築する
        explicit BoxCollision(SceneActor* owner) noexcept : Collision(owner) {}
        //! halfExtents を指定して構築する。負の成分は 0 にクランプ
        BoxCollision(SceneActor* owner, const Vector3& halfExtents) noexcept;

        //! 当たり箱の各軸の半径を設定する。負の成分は 0 にクランプ
        void SetHalfExtents(const Vector3& halfExtents) noexcept;

        //! 当たり箱の各軸の半径を返す。owner の scale を掛ける前の値
        [[nodiscard]] Vector3 HalfExtents() const noexcept;

        //! 持ち主の局所座標系での中心オフセットを設定 / 取得する。当たり箱を視覚と独立にずらすのに使う
        void SetCenterOffset(const Vector3& offset) noexcept;
        [[nodiscard]] Vector3 CenterOffset() const noexcept;

        //! 持ち主の局所座標系での回転をクォータニオンで設定 / 取得する。owner 回転にこれを重ねて当たり箱を回す
        void SetLocalRotation(const Quaternion& rotation) noexcept;
        [[nodiscard]] Quaternion LocalRotation() const noexcept;

        //! 局所回転を仰角・方位角・傾きのオイラー角 (度) で読み書きする Inspector 用アクセサ。内部はクォータニオン保持
        void SetRotationEulerDegrees(const Vector3& eulerDegrees) noexcept;
        [[nodiscard]] Vector3 RotationEulerDegrees() const noexcept;

        //! Owner の世界変換に当たり箱の局所のずれと回転を重ねた AABB を返す。回転時は内包する軸並行にする
        //! Owner が未登録の場合は局所のずれと回転だけを反映した AABB を返す。例外は投げない
        [[nodiscard]] AABB WorldAABB() const noexcept;

        //! Owner の世界変換に当たり箱の局所のずれと回転を重ねた有向境界ボックスを返す
        //! 回転・非一様 scale を厳密に保持する。Owner 未登録時は局所のずれと回転だけを反映する
        [[nodiscard]] OBB WorldOBB() const noexcept;

        // 当たり箱の形状の半径と Transform からの独立オフセット / 回転を Inspector へ公開する
        // 半径は負を 0 にクランプし、回転は Euler 度で受けるため setter 経由で書く
        NS_REFLECT_BEGIN(BoxCollision, Collision)
        NS_REFLECT_ACCESSOR(Vector3, "半径", HalfExtents(), SetHalfExtents)
        NS_REFLECT_ACCESSOR(Vector3, "中心オフセット", CenterOffset(), SetCenterOffset)
        NS_REFLECT_ACCESSOR(Vector3, "回転 (度)", RotationEulerDegrees(), SetRotationEulerDegrees)
        NS_REFLECT_END()

    private:
        // 固形の body として OBB のまま入れる
        [[nodiscard]] JPH::BodyID SyncBody(Phys::PhysicsScene& physics, JPH::BodyID current) override;

        Vector3 m_halfExtents{0.5f, 0.5f, 0.5f};               // 当たり箱の各軸半径
        Vector3 m_centerOffset{0.0f, 0.0f, 0.0f};              // 持ち主の局所座標系での中心オフセット
        Quaternion m_localRotation = Quaternion::Identity; // owner 回転に重ねる局所回転
    };
} // namespace NS
