#include "Game/Level/CollisionInput.h"

#include "Game/Level/ImpactResolver.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/GameObject.h"
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

    // -140 はチャージ減速を書いてから PlayerComponent (200) が動く並びにするため
    CollisionInput::CollisionInput() noexcept : NS::Obj::Component(NS::Obj::TickPriority::Update - 140)
    {
        m_chargeFactorCurve.count = 2;
        m_chargeFactorCurve.keys[0] = NS::Obj::Curve::Key{0.0f, 1.0f};
        m_chargeFactorCurve.keys[1] = NS::Obj::Curve::Key{1.0f, 2.0f};

        m_positionFactorCurve.count = 2;
        m_positionFactorCurve.keys[0] = NS::Obj::Curve::Key{0.0f, 1.0f};
        m_positionFactorCurve.keys[1] = NS::Obj::Curve::Key{1.0f, 0.7f};
    }

    void CollisionInput::OnStart()
    {
        m_movement = Owner()->FindComponent<NS::Game::Player::PlayerComponent>();
        m_resolver = Owner()->FindComponent<ImpactResolver>();
    }

    void CollisionInput::OnEndPlay()
    {
        // 構えを掛けたまま外れると縮んだ形が残るため、必ず元の形へ戻す
        if (m_stanceApplied)
        {
            RootTransform().SetScale(m_homeScale);
            m_stanceApplied = false;
        }
        // 押しの印が真の間、自機は着地しても丸まりを解かない。外れた後は印を書く物が無いので、真のまま残すと
        // 着地で解けなくなる。印を偽へ戻し、丸まりもここで解く。ResetState は速度と状態機械まで戻すので呼ばない
        if (m_movement != nullptr)
        {
            m_movement->SetBodySlamHeld(false);
            m_movement->SetCurled(false);
        }
    }

    void CollisionInput::OnUpdate()
    {
        const float dt = NS::Platform::FrameTimer::FixedDelta();
        m_judge.chargeThresholdSteps = SecondsToSteps(m_chargeThresholdSeconds, dt);
        m_judge.chargeMaxSteps = SecondsToSteps(m_chargeFullSeconds, dt);

        NS::Platform::Input& input = NS::Platform::Input::Get();
        // エディタの操作クリックが衝突入力へ漏れるため、UI がマウスを取っている間は読まない
        // エディタがプレイ中の Scene のタブから左ボタンだけ渡している間は読む
        const bool leftFree = input.GameReceivesMouseButton(NS::Platform::MouseButton::Left);
        const NS::Platform::Mouse& mouse = input.Mouse();
        const NS::Platform::Gamepad& pad = input.Gamepad(0);
        const bool held =
            (leftFree && mouse.IsHeld(NS::Platform::MouseButton::Left)) || pad.IsHeld(NS::Platform::GamepadButton::X);
        m_judge.Step(held);

        if (m_judge.JustPressed() && m_movement != nullptr)
        {
            m_movement->MarkBodySlamAim();
        }
        if (m_movement != nullptr)
        {
            // 溜めに入るのを待たずに、押したフレームから丸まる。押したフレームだけ入れると、押したまま出直した後に
            // 丸まりが戻らない
            if (m_judge.IsHeld())
            {
                m_movement->SetCurled(true);
            }
            m_movement->SetBodySlamHeld(m_judge.IsHeld());
        }

        const SlamKind fired = m_judge.TakeFired();
        if (fired != SlamKind::None && m_movement != nullptr)
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
                m_movement->RequestBodySlam(charge01, m_aimLine.direction);
            }
            else
            {
                m_movement->RequestBodySlam(charge01);
            }
            NS_LOG_INFO(Game, "体当たり発動: {} 溜め {:.2f}", SlamKindLabel(fired), charge01);
        }

        if (m_movement != nullptr)
        {
            float scale = 1.0f;
            if (m_judge.IsCharging())
            {
                scale = ChargingSpeedScale();
            }
            m_movement->SetMaxSpeedScale(scale);
        }

        if (m_judge.JustStartedCharging() && m_movement != nullptr)
        {
            // 縦は残す。空中で溜めたフレームに 0 を書くと落下が一瞬止まって引っかかる
            const NS::Core::Vector3 velocity = m_movement->Velocity();
            m_movement->SetVelocity(NS::Core::Vector3{0.0f, velocity.y, 0.0f});
        }

        UpdateAimTarget();
        SteerTowardTarget();
        UpdateChargeStance();

#if !defined(NS_SHIPPING)
        DrawChargeRing();
#endif
    }

    void CollisionInput::UpdateAimTarget()
    {
        m_hasAimTarget = false;
        m_hasAimLine = false;
        if (!m_judge.IsHeld() || m_movement == nullptr)
        {
            return;
        }
        // 線はカメラの正面だけで引き、押したキーとスティックの向きは入れない。カメラが無ければ見ている正面が無い
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
        // 非数と無限の向きは正規化を通り抜ける
        if (!std::isfinite(direction.x) || !std::isfinite(direction.z))
        {
            return;
        }
        m_aimLine = AimLine{
            .origin = RootTransform().Position(), .direction = direction, .length = m_movement->BodySlamDistance()};
        m_hasAimLine = true;
        if (m_resolver == nullptr)
        {
            return;
        }
        m_hasAimTarget = m_resolver->FindSlamLineTarget(m_aimLine.direction, m_aimLine.length, m_aimTarget);
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

    void CollisionInput::SteerTowardTarget()
    {
        if (m_movement == nullptr || m_resolver == nullptr)
        {
            return;
        }

        // 突進中は今飛んでいる向き、押している間は狙いの線の向き (カメラの正面) の前方を探す。
        // 押している間は同じ向きを、寄せた角度を測る基準として自機へ渡す。線が無ければ AimDirection で代える
        // 溜めて放した突進は線の向きへ出るが、放すまでに歩いた分だけ相手への角度が変わるので、
        // 自機は放す時に控えた相手を放す向きから測り直す
        // 線の上の相手を先に選ぶ。一番近い相手だけを見ると、狙う相手と違う近くの相手の側へ回る
        // 押している間の線の上の相手は、カメラの正面の線で探した狙う相手
        NS::Core::Vector3 forward{};
        SlamLineTarget onLine{};
        bool hasOnLine = false;
        if (m_movement->IsBodySlamming())
        {
            const NS::Core::Vector3 velocity = m_movement->BodySlamVelocity();
            forward = NS::Core::Vector3{velocity.x, 0.0f, velocity.z};
            hasOnLine = m_resolver->FindSlamLineTarget(forward, m_movement->BodySlamDistance(), onLine);
        }
        else if (m_judge.IsHeld())
        {
            forward = m_movement->AimDirection();
            if (m_hasAimLine)
            {
                forward = m_aimLine.direction;
            }
            hasOnLine = TryGetAimTarget(onLine);
        }
        else
        {
            return;
        }

        NS::Obj::ObjectRef preferred{};
        if (hasOnLine)
        {
            preferred = onLine.target;
        }
        NS::Core::Vector3 center{};
        if (m_resolver->FindHomingTarget(forward, m_homingSearchDegrees, m_homingSearchDistance, center, preferred))
        {
            m_movement->SteerToward(center, m_homingSearchDegrees, forward);
        }
    }

    void CollisionInput::UpdateChargeStance()
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
            float scale = m_pressSquashScale;
            if (m_judge.IsCharging())
            {
                scale = m_chargeSquashScale;
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
        if (!m_judge.IsCharging() || m_movement == nullptr)
        {
            return;
        }

        const float charge01 = m_judge.Charge01();
        const NS::Core::Vector3 center = Owner()->Root().Position();
        const float footY = center.y - m_movement->CapsuleHalfHeight() - m_movement->CapsuleRadius() + 0.05f;
        // 溜め量が輪の広がりに出ないと、満タンまでの途中が読めない
        const float radius = m_movement->CapsuleRadius() + 0.25f + charge01 * 0.75f;
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
        const float factor = m_chargeFactorCurve.Evaluate(clamped);
        // Inspector で点を全部消すと Evaluate が 0 を返して威力が消えるため、0 以下は 1 とみなす
        if (!(factor > 0.0f))
        {
            return 1.0f;
        }
        return factor;
    }

    float CollisionInput::PositionFactorFor(float offset01) const noexcept
    {
        if (!std::isfinite(offset01))
        {
            return 1.0f;
        }

        const float clamped = NS::Core::Clamp(offset01, 0.0f, 1.0f);
        const float factor = m_positionFactorCurve.Evaluate(clamped);
        // 点を全部消すと威力が 0 になるため、チャージ倍率カーブと同じく 0 以下は 1 とみなす
        if (!(factor > 0.0f))
        {
            return 1.0f;
        }

        return factor;
    }

    HitTier CollisionInput::HitTierFor(float offset01) const noexcept
    {
        // 横ずれが測れない当たりに中心近くの白の光と長い止めを出さない
        if (!std::isfinite(offset01))
        {
            return HitTier::Wide;
        }
        if (offset01 < m_centerTierEdge)
        {
            return HitTier::Center;
        }
        if (offset01 < m_nearTierEdge)
        {
            return HitTier::Near;
        }
        return HitTier::Wide;
    }

    float CollisionInput::ChargingSpeedScale() const noexcept
    {
        // 減速率の欄は非有限の書き込みを捨てるので、ここへ来る値は有限。Clamp だけで 0..1 に収まる
        return NS::Core::Clamp(1.0f - m_chargeSlowRate, 0.0f, 1.0f);
    }

    NS_CLASS(CollisionInput)
} // namespace NS::Game::Level
