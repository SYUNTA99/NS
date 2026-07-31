#pragma once

#include "Runtime/Math/Math.h"

namespace NS::Physics
{
    class PhysicsWorld;

    /// 1 フレーム分の Update 入力。dt は固定ステップ
    /// 衝突は physicsWorld への capsule sweep で解決する。nullptr なら衝突なしで motion を進める
    struct CapsuleMoverInput
    {
        NS::Math::Vector3 position{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 velocity{0.0f, 0.0f, 0.0f};
        float dt = 0.0f;
        float capsuleRadius = 0.4f;
        float capsuleHalfHeight = 0.5f;
        const PhysicsWorld* physicsWorld = nullptr;
    };

    /// Update の戻り値。新 position / velocity と接触情報
    struct CapsuleMoverResult
    {
        NS::Math::Vector3 position{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 velocity{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 contactNormal{0.0f, 0.0f, 0.0f};
        bool grounded = false;
    };

    /// @brief Capsule をサブステップで swept する物理
    /// @details 入出力は POD struct。重力やジャンプ等のゲームプレイ値は持たない
    class CapsuleMover
    {
    public:
        CapsuleMover() noexcept = default;

        /// 1 フレーム分の物理を進めて新しい状態を返す
        /// 結果を毎回同じにするため `NS::Core::FrameTimer::DeltaSeconds()` 等の可変 delta は使わない
        [[nodiscard]] CapsuleMoverResult Update(const CapsuleMoverInput& input) noexcept;
    };
} // namespace NS::Physics
