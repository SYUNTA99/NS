#pragma once

#include "Runtime/Math/Math.h"
#include "Runtime/Object/Components/ColliderComponent.h"
#include "Runtime/Physics/SweptTriangle.h"

#include <array>

namespace NS::Object
{
    /// @brief 楔形スロープの三角形 collider Component。BoxColliderComponent とは独立に登録する
    /// @details Owner の root world transform を基準に、wedge の 5 面、斜面・底面・裏壁の四角形と
    /// 左右側面の三角形を 8 三角形に分割した世界座標版 Triangle 配列を返す
    /// 斜面のみだと側面 / 裏 / 底から capsule がめり込むため全面を登録する
    /// world の組み直し時に physics へ登録され、CapsuleMover が読む
    /// 角度・半サイズはリフレクション set で編集でき、WorldTriangles が member を都度読むため形状の再生成は要らない
    class SlopeColliderComponent : public ColliderComponent
    {
    public:
        /// 1m cell に合わせた 45 度・半サイズ 0.5 の既定で組む。角度・半サイズはリフレクション set で入る
        SlopeColliderComponent() noexcept = default;

        [[nodiscard]] float AngleDegrees() const noexcept { return m_angleDegrees; }
        [[nodiscard]] NS::Math::Vector3 HalfExtents() const noexcept { return m_halfExtents; }

        /// world 座標の wedge 三角形 8 個、 内訳は斜面2・底2・裏壁2・側面各1。 Owner 未登録なら local 座標版
        [[nodiscard]] std::array<NS::Physics::Triangle, 8> WorldTriangles() const noexcept;

        /// wedge の 8 三角形を physics へ入れる
        void AddToPhysics(NS::Physics::PhysicsWorld& physics) const override;

        // 角度・半サイズを Inspector / 直列化へ公開する。 WorldTriangles は member を都度読むため set で即反映する
        NS_REFLECT_BEGIN(SlopeColliderComponent, ColliderComponent)
        NS_REFLECT_FIELD(m_angleDegrees, "角度 (度)")
        NS_REFLECT_FIELD(m_halfExtents, "半径")
        NS_REFLECT_END()

    private:
        float m_angleDegrees = 45.0f;                      // 斜面の傾斜角
        NS::Math::Vector3 m_halfExtents{0.5f, 0.5f, 0.5f}; // wedge の半サイズ
    };
} // namespace NS::Object
