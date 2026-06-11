#pragma once

/// @file CharacterController.h
/// @brief NS::Physics::CharacterController — Capsule + sub-step swept 物理
///
/// 入出力は POD struct。gameplay 値 (gravity / jump 等) は保持しない (責任分担)

#include "Framework/Math/Math.h"
#include "Framework/Physics/SweptTriangle.h"

#include <span>

namespace NS::Physics
{
    /// 1 frame の Update 入力。dt は fixed step
    /// AABB と Triangle の両 world を同 substep 内で sweep し、最小 TOI 側を採用する
    struct CharacterControllerInput
    {
        NS::Math::Vector3 position{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 velocity{0.0f, 0.0f, 0.0f};
        float dt = 0.0f;
        float capsuleRadius = 0.4f;
        float capsuleHalfHeight = 0.5f;
        std::span<const NS::Math::AABB> world{};
        std::span<const NS::Physics::Triangle> worldTriangles{};
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
        /// 使わない (Determinism 制約)
        [[nodiscard]] CharacterControllerResult Update(const CharacterControllerInput& input) noexcept;
    };
} // namespace NS::Physics
