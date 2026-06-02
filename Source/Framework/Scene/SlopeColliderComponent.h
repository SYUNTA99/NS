#pragma once

/// @file SlopeColliderComponent.h
/// @brief 楔形 (wedge) スロープの三角形 collider Component
///
/// @details Owner の root world transform を基準に、 wedge の 5 面 (斜面 quad + 底面 quad +
/// 裏壁 quad + 左右側面 triangle) を 8 三角形に分割した世界座標版 Triangle 配列を返す
/// 斜面のみだと側面 / 裏 / 底から capsule がめり込むため全面を登録する
/// LevelEditorScene 側でこれを集約して `CharacterControllerInput::worldTriangles`
/// 経由で physics に渡す
/// 角度・半サイズはコンストラクタで確定する data として保持し、 v2 で任意角度に拡張する余地を残す

#include "Framework/Core/Math.h"
#include "Framework/Physics/SweptTriangle.h"
#include "Framework/Scene/Component.h"

#include <array>

namespace NS::Scene
{
    /// 1 wedge slope の 2 三角形を世界座標で返す Component。 AABB collider ではないため
    /// `StaticColliderComponent` とは独立、 wedge 専用のスロープ block に組み込む
    class SlopeColliderComponent : public Component
    {
    public:
        /// @param owner            所有 GameObject。 base コンストラクタで auto-register
        /// @param angleDegrees     斜面の傾斜角 (45 / 30 / 22.5 / 15 度のいずれかを想定)
        /// @param halfExtents      wedge の半サイズ。 デフォルト値は 1m cell の (0.5, 0.5, 0.5)
        SlopeColliderComponent(NS::Scene::GameObject* owner,
                               float angleDegrees,
                               const NS::Core::Vector3& halfExtents) noexcept;

        /// 角度 (度数法)
        [[nodiscard]] float AngleDegrees() const noexcept { return m_angleDegrees; }
        /// 半サイズ
        [[nodiscard]] NS::Core::Vector3 HalfExtents() const noexcept { return m_halfExtents; }

        /// Owner の root world matrix を適用した世界座標版 wedge 三角形 (8 個)
        /// 内訳: 斜面 2 + 底面 2 + 裏壁 2 + 左側面 1 + 右側面 1
        /// Owner が未登録なら local 座標版を返す
        [[nodiscard]] std::array<NS::Physics::Triangle, 8> WorldTriangles() const noexcept;

    private:
        float m_angleDegrees = 45.0f;
        NS::Core::Vector3 m_halfExtents{0.5f, 0.5f, 0.5f};
    };
} // namespace NS::Scene
