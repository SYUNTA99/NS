#include "Game/Level/CollisionInputComponent.h"

#include "Game/Level/ImpactResolverComponent.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Core/Clock.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Platform/Gamepad.h"
#include "Runtime/Platform/Input.h"
#include "Runtime/Platform/Mouse.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        // ImpactInputJudge は固定ステップの整数で数えるため、秒の欄を歩数へ丸めて渡す
        [[nodiscard]] int SecondsToSteps(float seconds, float dt) noexcept
        {
            if (!(dt > 0.0f))
                return 0;
            const float raw = seconds / dt;
            if (!std::isfinite(raw))
                return 0;
            return std::max(0, static_cast<int>(std::lround(raw)));
        }
    } // namespace

    // -140 はチャージ減速を書いてから PlayerComponent (200) が動く並びにするため
    CollisionInputComponent::CollisionInputComponent() noexcept
        : NS::Object::Component(NS::Object::TickPriority::Update - 140)
    {
        m_chargeFactorCurve.count = 2;
        m_chargeFactorCurve.keys[0] = NS::Object::Curve::Key{0.0f, 1.0f};
        m_chargeFactorCurve.keys[1] = NS::Object::Curve::Key{1.0f, 2.0f};

        m_positionFactorCurve.count = 2;
        m_positionFactorCurve.keys[0] = NS::Object::Curve::Key{0.0f, 1.0f};
        m_positionFactorCurve.keys[1] = NS::Object::Curve::Key{1.0f, 0.7f};
    }

    void CollisionInputComponent::OnStart()
    {
        m_movement = Owner()->FindComponent<NS::Game::Player::PlayerComponent>();
        m_resolver = Owner()->FindComponent<ImpactResolverComponent>();
    }

    void CollisionInputComponent::OnEndPlay()
    {
        // 構えを掛けたまま外れると縮んだ形が残るため、必ず元の形へ戻す
        if (m_stanceApplied)
        {
            RootTransform().SetScale(m_homeScale);
            m_stanceApplied = false;
        }
    }

    void CollisionInputComponent::OnUpdate()
    {
        const float dt = NS::Core::FrameTimer::FixedDelta();
        m_judge.chargeThresholdSteps = SecondsToSteps(m_chargeThresholdSeconds, dt);
        m_judge.chargeMaxSteps = SecondsToSteps(m_chargeFullSeconds, dt);

        auto& input = NS::Platform::Input::Get();
        // エディタの操作クリックが衝突入力へ漏れるため、UI がマウスを取っている間は読まない
        const bool mouseFree = !input.UiWantsMouse();
        const auto& mouse = input.Mouse();
        const auto& pad = input.Gamepad(0);
        const bool held =
            (mouseFree && mouse.IsHeld(NS::Platform::MouseButton::Left)) || pad.IsHeld(NS::Platform::GamepadButton::X);
        m_judge.Step(held);

        if (m_judge.JustPressed() && m_movement != nullptr)
            m_movement->MarkBodySlamAim();

        const SlamKind fired = m_judge.TakeFired();
        if (fired != SlamKind::None && m_movement != nullptr)
        {
            float charge01 = 0.0f;
            if (fired == SlamKind::Charged)
                charge01 = m_judge.Charge01();
            m_movement->RequestBodySlam(charge01);
            NS_LOG_INFO(Game, "体当たり発動: {} 溜め {:.2f}", SlamKindLabel(fired), charge01);
        }

        // 走行速度から絶対値で書く。前の歩の値へ掛けると毎歩積み重なり、最高速度が指数的に 0 へ落ちる
        if (m_movement != nullptr)
        {
            const float scale = m_judge.IsCharging() ? 1.0f - m_chargeSlowRate : 1.0f;
            m_movement->SetMaxSpeed(m_movement->RunSpeed() * scale);
        }

        UpdateChargeStance();

#if !defined(NS_SHIPPING)
        DrawChargeRing();
#endif
    }

    void CollisionInputComponent::UpdateChargeStance()
    {
        // 凍結と潰れ・伸びの最中は触らない。書くと控えた元の形と衝突演出が壊れる
        const bool resolverAnimating = m_resolver != nullptr && m_resolver->IsScaleAnimating();
        const bool movementFrozen = m_movement != nullptr && !m_movement->IsActiveSelf();
        if (m_judge.IsHeld() && !resolverAnimating && !movementFrozen)
        {
            if (!m_stanceApplied)
            {
                m_homeScale = RootTransform().Scale();
                m_stanceApplied = true;
            }
            const float scale = m_judge.IsCharging() ? m_chargeSquashScale : m_pressSquashScale;
            RootTransform().SetScale(NS::Core::Vector3{m_homeScale.x, m_homeScale.y * scale, m_homeScale.z});
        }
        else if (m_stanceApplied && !resolverAnimating)
        {
            RootTransform().SetScale(m_homeScale);
            m_stanceApplied = false;
        }
    }

#if !defined(NS_SHIPPING)
    void CollisionInputComponent::DrawChargeRing()
    {
        if (!m_judge.IsCharging() || m_movement == nullptr)
            return;

        const float charge01 = m_judge.Charge01();
        const NS::Core::Vector3 center = Owner()->Root().Position();
        const float footY = center.y - m_movement->CapsuleHalfHeight() - m_movement->CapsuleRadius() + 0.05f;
        // 溜め量が輪の広がりに出ないと、満タンまでの途中が読めない
        const float radius = m_movement->CapsuleRadius() + 0.25f + charge01 * 0.75f;
        NS::Core::Color color{1.0f, 0.85f, 0.2f, 1.0f};
        if (m_judge.IsChargeFull())
            color = NS::Core::Color{1.0f, 1.0f, 1.0f, 1.0f};
        constexpr int k_Segments = 24;
        for (int i = 0; i < k_Segments; ++i)
        {
            const float a0 = 2.0f * NS::Core::k_Pi * static_cast<float>(i) / static_cast<float>(k_Segments);
            const float a1 = 2.0f * NS::Core::k_Pi * static_cast<float>(i + 1) / static_cast<float>(k_Segments);
            NS::Graphics::DebugDraw::Line(
                NS::Core::Vector3{center.x + std::cos(a0) * radius, footY, center.z + std::sin(a0) * radius},
                NS::Core::Vector3{center.x + std::cos(a1) * radius, footY, center.z + std::sin(a1) * radius},
                color);
        }
    }
#endif

    float CollisionInputComponent::ChargeFactorFor(float charge01) const noexcept
    {
        if (!std::isfinite(charge01))
            return 1.0f;
        const float clamped = NS::Core::Clamp(charge01, 0.0f, 1.0f);
        const float factor = m_chargeFactorCurve.Evaluate(clamped);
        // Inspector で点を全部消すと Evaluate が 0 を返して威力が消えるため、0 以下は 1 とみなす
        if (!(factor > 0.0f))
            return 1.0f;
        return factor;
    }

    float CollisionInputComponent::PositionFactorFor(float offset01) const noexcept
    {
        if (!std::isfinite(offset01))
            return 1.0f;
        const float clamped = NS::Core::Clamp(offset01, 0.0f, 1.0f);
        const float factor = m_positionFactorCurve.Evaluate(clamped);
        // 点を全部消すと威力が 0 になるため、チャージ倍率カーブと同じく 0 以下は 1 とみなす
        if (!(factor > 0.0f))
            return 1.0f;
        return factor;
    }

    bool CollisionInputComponent::IsPeak(float positionFactor) const noexcept
    {
        return positionFactor >= m_peakThreshold;
    }

    NS_CLASS(CollisionInputComponent)
} // namespace NS::Game::Level
