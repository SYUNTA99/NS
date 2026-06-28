#pragma once

/// @file SlopeColliderComponent.h
/// @brief 楔形スロープの三角形 collider Component
///
/// @details Owner の root world transform を基準に、 wedge の 5 面、 斜面・底面・裏壁の四角形と
/// 左右側面の三角形を 8 三角形に分割した世界座標版 Triangle 配列を返す
/// 斜面のみだと側面 / 裏 / 底から capsule がめり込むため全面を登録する
/// LevelPlayScene 側でこれを集約して `CharacterControllerInput::worldTriangles`
/// 経由で physics に渡す
/// 角度・半サイズは反射 set で編集でき、 WorldTriangles が member を都度読むため形状の再生成は要らない

#include "Framework/Math/Math.h"
#include "Framework/Physics/SweptTriangle.h"
#include "Framework/Scene/Component.h"

#include <array>

namespace NS::Scene
{
    /// wedge slope の world 座標 Triangle を返す Component。BoxColliderComponent とは独立
    class SlopeColliderComponent : public Component
    {
    public:
        /// @param angleDegrees     斜面の傾斜角。 45 / 30 / 22.5 / 15 度のいずれかを想定する
        /// @param halfExtents      wedge の半サイズ。 デフォルトは 1m cell に合わせた 0.5・0.5・0.5
        SlopeColliderComponent(float angleDegrees, const NS::Math::Vector3& halfExtents) noexcept;

        /// 角度を度数法で返す
        [[nodiscard]] float AngleDegrees() const noexcept { return m_angleDegrees; }
        /// 半サイズ
        [[nodiscard]] NS::Math::Vector3 HalfExtents() const noexcept { return m_halfExtents; }

        /// world 座標の wedge 三角形 8 個、 内訳は斜面2・底2・裏壁2・側面各1。 Owner 未登録なら local 座標版
        [[nodiscard]] std::array<NS::Physics::Triangle, 8> WorldTriangles() const noexcept;

        // 角度・半サイズを Inspector / 直列化へ公開する。 WorldTriangles は member を都度読むため set で即反映する
        NS_REFLECT_BEGIN(SlopeColliderComponent)
        NS_REFLECT_FIELD(m_angleDegrees, "Angle (deg)")
        NS_REFLECT_FIELD(m_halfExtents, "Half Extents")
        NS_REFLECT_END()

    private:
        float m_angleDegrees = 45.0f;
        NS::Math::Vector3 m_halfExtents{0.5f, 0.5f, 0.5f};
    };
} // namespace NS::Scene
