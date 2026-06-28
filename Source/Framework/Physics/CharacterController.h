#pragma once

/// @file CharacterController.h
/// @brief NS::Physics::CharacterController — Capsule + sub-step swept 物理
///
/// 入出力は POD struct。gravity / jump 等の gameplay 値は責任分担として保持しない

#include "Framework/Math/Math.h"

namespace NS::Physics
{
    class PhysicsWorld;

    /// 1 frame の Update 入力。dt は fixed step
    /// 衝突は physicsWorld への capsule sweep query で解決する。 null なら衝突なしで motion を進める
    struct CharacterControllerInput
    {
        NS::Math::Vector3 position{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 velocity{0.0f, 0.0f, 0.0f};
        float dt = 0.0f;
        float capsuleRadius = 0.4f;
        float capsuleHalfHeight = 0.5f;
        const PhysicsWorld* physicsWorld = nullptr;
    };

    /// Update の戻り値。新 position / velocity と接触情報
    struct CharacterControllerResult
    {
        NS::Math::Vector3 position{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 velocity{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 contactNormal{0.0f, 0.0f, 0.0f};
        bool grounded = false;
    };

    class CharacterController
    {
    public:
        CharacterController() noexcept = default;

        /// 1 frame ぶん物理を進めて新状態を返す。`NS::Core::FrameTimer::DeltaSeconds()` 等の variable delta は
        /// 使わない。 これは Determinism 制約による
        [[nodiscard]] CharacterControllerResult Update(const CharacterControllerInput& input) noexcept;
    };
} // namespace NS::Physics
