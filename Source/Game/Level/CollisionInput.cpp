#include "Game/Level/CollisionInput.h"

#include "Game/Level/ImpactResolver.h"
#include "Game/Player.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Platform/Clock.h"
#include "Runtime/Platform/Gamepad.h"
#include "Runtime/Platform/Input.h"
#include "Runtime/Platform/Mouse.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        // ImpactInputJudge は固定ステップの整数で数えるため、秒の欄をフレーム数へ丸めて渡す
        [[nodiscard]] int SecondsToSteps(float seconds, float dt) noexcept
        {
            if (!(dt > 0.0f))
            {
                return 0;
            }
            const float raw = seconds / dt;
            if (!std::isfinite(raw))
            {
                return 0;
            }
            return std::max(0, static_cast<int>(std::lround(raw)));
        }
    } // namespace

    // Player の観測の段と決定の段は溜めを身体の段 (BodyStep) より前に進めるので、チャージ減速は同じフレームの移動に効く
    CollisionInput::CollisionInput() noexcept : NS::Obj::Component() {}

    const NS::Game::Player::PlayerParams& CollisionInput::Tuning() const noexcept
    {
        if (m_params != nullptr)
        {
            return *m_params;
        }
        static const NS::Game::Player::PlayerParams defaults;
        return defaults;
    }

    void CollisionInput::OnStart()
    {
        if (m_player != nullptr)
        {
            m_body = &m_player->Body();
            m_resolver = &m_player->Resolver();
        }
    }

    void CollisionInput::OnEndPlay()
    {
        m_stateReady = false;
        m_controlReady = false;
        if (m_player != nullptr)
        {
            m_player->m_charge.Finish();
        }
        // 構えを掛けたまま外れると縮んだ形が残るため、必ず元の形へ戻す
        if (m_stanceApplied)
        {
            RootTransform().SetScale(m_homeScale);
            m_stanceApplied = false;
        }
        // 押しの印が真の間、自機は着地しても丸まりを解かない。外れた後は印を書く物が無いので、真のまま残すと
        // 着地で解けなくなる。印を偽へ戻し、丸まりもここで解く。ResetState は速度と状態機械まで戻すので呼ばない
        if (m_player != nullptr && m_body != nullptr)
        {
            m_player->SetBodySlamHeld(false);
            m_player->SetCurled(false);
        }
    }

    bool CollisionInput::ReadHeld() const
    {
        NS::Platform::Input& input = NS::Platform::Input::Get();
        const bool leftFree = input.GameReceivesMouseButton(NS::Platform::MouseButton::Left);
        const NS::Platform::Mouse& mouse = input.Mouse();
        const NS::Platform::Gamepad& pad = input.Gamepad(0);
        return (leftFree && mouse.IsHeld(NS::Platform::MouseButton::Left)) ||
               pad.IsHeld(NS::Platform::GamepadButton::X);
    }

    void CollisionInput::OnUpdate()
    {
        Step(ReadHeld(), NS::Platform::FrameTimer::FixedDelta());
    }

    void CollisionInput::Observe(bool held)
    {
        m_observedHeld = held;
        m_observedRushing = m_body != nullptr && m_player->IsBodySlamming();
        m_stateReady = true;
        m_controlReady = true;
        UpdateAimTarget(held);
    }

    NS::Core::Vector3 CollisionInput::PredictedSlamVelocity() const noexcept
    {
        if (m_body == nullptr)
        {
            return NS::Core::Vector3{};
        }
        return m_player->BodySlamVelocity();
    }

    void CollisionInput::AdvanceState(float dt)
    {
        if (!m_stateReady)
        {
            return;
        }
        m_stateReady = false;
        if (m_player != nullptr)
        {
            m_player->StepCharge(m_observedHeld, dt);
            return;
        }
        AdvanceCharge(m_observedHeld, dt);
    }

    void CollisionInput::ApplyControl(bool refreshVelocity)
    {
        if (m_stateReady || !m_controlReady)
        {
            return;
        }
        m_controlReady = false;
        if (refreshVelocity && m_observedRushing && m_body != nullptr && m_body->IsActive() &&
            m_player->IsBodySlamming())
        {
            m_player->ApplyBodySlamHeading();
        }
#if !defined(NS_SHIPPING)
        DrawChargeRing();
#endif
    }

    void CollisionInput::Step(bool held, float dt)
    {
        Observe(held);
        AdvanceState(dt);
        ApplyControl(false);
    }

    void CollisionInput::AdvanceCharge(bool held, float dt)
    {
        m_judge.chargeThresholdSteps = SecondsToSteps(Tuning().m_chargeThresholdSeconds, dt);
        m_judge.chargeMaxSteps = SecondsToSteps(Tuning().m_chargeFullSeconds, dt);
        m_judge.Step(held);

        if (m_judge.JustPressed() && m_body != nullptr)
        {
            m_player->MarkBodySlamAim();
        }
        if (m_body != nullptr)
        {
            // 溜めに入るのを待たずに、押したフレームから丸まる。押したフレームだけ入れると、押したまま出直した後に
            // 丸まりが戻らない
            if (m_judge.IsHeld())
            {
                m_player->SetCurled(true);
            }
            m_player->SetBodySlamHeld(m_judge.IsHeld());
        }

        const SlamKind fired = m_judge.TakeFired();
        if (fired != SlamKind::None && m_body != nullptr)
        {
            float charge01 = 0.0f;
            if (fired == SlamKind::Charged)
            {
                charge01 = m_judge.Charge01();
            }
            // 放したフレームは線をまだ引き直していないので、控えた線は放す前のフレームに矢印を貼った線。
            // 溜めて放した突進はスティックを見ずにその向きへ出す。タップは矢印が出ないので入力の向きへ出す
            if (fired == SlamKind::Charged && m_hasAimLine)
            {
                m_player->RequestBodySlam(charge01, m_aimLine.direction, m_aimLine.launchVerticalSpeed);
            }
            else
            {
                m_player->RequestBodySlam(charge01);
            }
            NS_LOG_INFO(Game, "体当たり発動: {} 溜め {:.2f}", SlamKindLabel(fired), charge01);
        }

        if (m_body != nullptr)
        {
            float scale = 1.0f;
            if (m_judge.IsCharging())
            {
                scale = ChargingSpeedScale();
            }
            m_player->SetMaxSpeedScale(scale);
        }

        if (m_judge.JustStartedCharging() && m_body != nullptr)
        {
            // 縦は残す。空中で溜めたフレームに 0 を書くと落下が一瞬止まって引っかかる
            const NS::Core::Vector3 velocity = m_body->Velocity();
            m_body->SetVelocity(NS::Core::Vector3{0.0f, velocity.y, 0.0f});
        }

        m_hasAimLine = m_observedHasAimLine;
        m_hasAimTarget = m_observedHasAimTarget;
        m_aimLine = m_observedAimLine;
        m_aimTarget = m_observedAimTarget;
        if (m_hasAimLine)
        {
            m_aimLine.origin = RootTransform().Position();
        }
        if (m_hasAimTarget)
        {
            m_aimTarget.origin = RootTransform().Position();
        }
        UpdateChargeStance();
    }

    void CollisionInput::UpdateAimTarget(bool held)
    {
        m_observedHasAimTarget = false;
        m_observedHasAimLine = false;
        if (!held || m_body == nullptr)
        {
            return;
        }
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        const NS::Obj::CameraComponent* camera = scene->MainCamera();
        if (camera == nullptr)
        {
            return;
        }
        NS::Core::Vector3 direction{};
        if (!NS::Core::TryNormalizeHorizontal(camera->ForwardHorizontal(), direction))
        {
            return;
        }
        if (!std::isfinite(direction.x) || !std::isfinite(direction.z))
        {
            return;
        }
        m_observedAimLine = AimLine{.origin = RootTransform().Position(),
                                    .direction = direction,
                                    .length = m_player->BodySlamDistance(),
                                    .launchVerticalSpeed = 0.0f,
                                    .grounded = m_body->IsGrounded()};
        m_observedHasAimLine = true;
        if (m_resolver == nullptr)
        {
            return;
        }
        m_observedHasAimTarget =
            m_resolver->FindSlamLineTarget(m_observedAimLine.direction, m_observedAimLine.length, m_observedAimTarget);
        // 放す時に添える縦の速さ。届かない相手と相手が無い時は 0 で水平に放つ
        if (m_observedHasAimTarget)
        {
            m_observedAimLine.launchVerticalSpeed = m_observedAimTarget.launchVerticalSpeed;
        }
    }

    bool CollisionInput::TryGetAimTarget(SlamLineTarget& outTarget) const noexcept
    {
        if (!m_hasAimTarget)
        {
            return false;
        }
        outTarget = m_aimTarget;
        return true;
    }

    bool CollisionInput::TryGetAimLine(AimLine& outLine) const noexcept
    {
        if (!m_hasAimLine)
        {
            return false;
        }
        outLine = m_aimLine;
        return true;
    }

    void CollisionInput::UpdateChargeStance()
    {
        // 凍結と潰れ・伸びの最中は触らない。書くと控えた元の形と衝突演出が壊れる
        const bool resolverAnimating = m_resolver != nullptr && m_resolver->IsScaleAnimating();
        const bool movementFrozen = m_body != nullptr && !m_body->IsActiveSelf();
        if (m_judge.IsHeld() && !resolverAnimating && !movementFrozen)
        {
            if (!m_stanceApplied)
            {
                m_homeScale = RootTransform().Scale();
                m_stanceApplied = true;
            }
            float scale = Tuning().m_pressSquashScale;
            if (m_judge.IsCharging())
            {
                scale = Tuning().m_chargeSquashScale;
            }
            RootTransform().SetScale(NS::Core::Vector3{m_homeScale.x, m_homeScale.y * scale, m_homeScale.z});
        }
        else if (m_stanceApplied && !resolverAnimating)
        {
            RootTransform().SetScale(m_homeScale);
            m_stanceApplied = false;
        }
    }

#if !defined(NS_SHIPPING)
    void CollisionInput::DrawChargeRing()
    {
        if (!m_judge.IsCharging() || m_body == nullptr)
        {
            return;
        }

        const float charge01 = m_judge.Charge01();
        const NS::Core::Vector3 center = Owner()->Root().Position();
        const float footY = center.y - m_body->CapsuleHalfHeight() - m_body->CapsuleRadius() + 0.05f;
        // 溜め量が輪の広がりに出ないと、満タンまでの途中が読めない
        const float radius = m_body->CapsuleRadius() + 0.25f + charge01 * 0.75f;
        NS::Core::Color color{1.0f, 0.85f, 0.2f, 1.0f};
        if (m_judge.IsChargeFull())
        {
            color = NS::Core::Color{1.0f, 1.0f, 1.0f, 1.0f};
        }
        NS::Gfx::DebugDraw::Circle(NS::Core::Vector3{center.x, footY, center.z},
                                   NS::Core::Vector3{radius, 0.0f, 0.0f},
                                   NS::Core::Vector3{0.0f, 0.0f, radius},
                                   color);
    }
#endif

    float CollisionInput::ChargeFactorFor(float charge01) const noexcept
    {
        if (!std::isfinite(charge01))
        {
            return 1.0f;
        }
        const float clamped = NS::Core::Clamp(charge01, 0.0f, 1.0f);
        const float factor = Tuning().m_chargeFactorCurve.Evaluate(clamped);
        // Inspector で点を全部消すと Evaluate が 0 を返して威力が消えるため、0 以下は 1 とみなす
        if (!(factor > 0.0f))
        {
            return 1.0f;
        }
        return factor;
    }

    float CollisionInput::ChargingSpeedScale() const noexcept
    {
        // 減速率の欄は非有限の書き込みを捨てるので、ここへ来る値は有限。Clamp だけで 0..1 に収まる
        return NS::Core::Clamp(1.0f - Tuning().m_chargeSlowRate, 0.0f, 1.0f);
    }

    NS_CLASS(CollisionInput)
} // namespace NS::Game::Level
