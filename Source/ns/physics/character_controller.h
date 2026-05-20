#pragma once

/// @file character_controller.h
/// @brief ns::physics::CharacterController — Capsule + sub-step swept 物理。
///
/// 入出力は POD struct。gameplay 値 (gravity / jump 等) は保持しない ( 責任分担)。
///  で実装。

#include "ns/core/math.h"

#include <span>

namespace ns::physics
{
    /// 1 frame の Update 入力。dt は fixed step ( Determinism)。
    struct CharacterControllerInput
    {
        ns::core::Vector3 position{0.0f, 0.0f, 0.0f};
        ns::core::Vector3 velocity{0.0f, 0.0f, 0.0f};
        float dt = 0.0f;
        float capsuleRadius = 0.4f;
        float capsuleHalfHeight = 0.5f;
        std::span<const ns::core::AABB> world{};
    };

    /// Update の戻り値。新 position / velocity と接触情報。
    struct CharacterControllerResult
    {
        ns::core::Vector3 position{0.0f, 0.0f, 0.0f};
        ns::core::Vector3 velocity{0.0f, 0.0f, 0.0f};
        ns::core::Vector3 contactNormal{0.0f, 0.0f, 0.0f};
        bool grounded = false;
    };

    class CharacterController
    {
    public:
        CharacterController() noexcept = default;

        /// 1 frame ぶん物理を進めて新状態を返す。`Application::DeltaTime()` 等の variable delta は
        /// 使わない ( Determinism 制約、SC5 担保)。
        [[nodiscard]] CharacterControllerResult Update(const CharacterControllerInput& input) noexcept;
    };
} // namespace ns::physics
