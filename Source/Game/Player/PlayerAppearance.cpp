#include "Game/Player/PlayerAppearance.h"

#include "Game/Level/ImpactInputJudge.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/SlamAim.h"
#include "Game/Player.h"
#include "Game/Player/PlayerJudges.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Graphics/StaticMesh.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/AssetManager.h"
#include "NSlib/Object/Components/Body.h"
#include "NSlib/Object/Components/Collider.h"
#include "NSlib/Object/Components/Model.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Windows/Clock.h"
#include <algorithm>
#include <cmath>

namespace
{
    // 参照が書いてあればそのファイルの mesh、空か引き当てられなければ仮の形を返す
    [[nodiscard]] NS::Gfx::Mesh* ResolveLook(NS::Obj::AssetManager& assets,
                                             const std::string& ref,
                                             NS::Gfx::Mesh* placeholder)
    {
        if (ref.empty())
        {
            return placeholder;
        }
        if (NS::Gfx::Mesh* mesh = NS::Obj::ResolveMeshFromRef(assets, ref))
        {
            return mesh;
        }
        NS_LOG_WARN(Game, "PlayerAppearance: 見た目の参照を引き当てられない。仮の形で描く: {}", ref);
        return placeholder;
    }

    // 上と向きの外積を水平の回転軸として axis へ書く。正の角度で上面がその向きへ倒れる前転になる
    // 長さの無い向きと、水平成分に非数・無限大を含む向きは採らず、axis を書き換えない
    // TryNormalizeHorizontal は長さの 2 乗が非数だと下限との比較が偽になって通すので、先に有限かを見る
    void SetRollAxisToward(const NS::Vector3& direction, NS::Vector3& axis) noexcept
    {
        const float lengthSq = direction.x * direction.x + direction.z * direction.z;
        if (!std::isfinite(lengthSq))
        {
            return;
        }
        NS::Vector3 forward{};
        if (!NS::TryNormalizeHorizontal(direction, forward))
        {
            return;
        }
        axis = NS::Vector3{forward.z, 0.0f, -forward.x};
    }
} // namespace

namespace NS::Game::Player
{
    // 配置物を組む経路では参照の引き当てが Player::ForEachPart の並びに回る。Model が自分の参照から mesh
    // を差した後に差し直す
    PlayerAppearance::PlayerAppearance() noexcept : NS::Obj::Component() {}

    void PlayerAppearance::Curl() noexcept
    {
        if (m_curled)
        {
            return;
        }
        m_curled = true;
        ShowCurrentLook();
    }

    void PlayerAppearance::Uncurl() noexcept
    {
        if (!m_curled)
        {
            return;
        }
        m_curled = false;
        ShowCurrentLook();
    }

    void PlayerAppearance::OnStart()
    {
        if (::Player* ownerPlayer = NS::Obj::Cast<::Player>(Owner()))
        {
            m_body = &ownerPlayer->Body();
            m_actor = ownerPlayer;
            m_resolver = &ownerPlayer->Resolver();
        }
    }

    void PlayerAppearance::OnEndPlay()
    {
        ResetDrawScale();
    }

    void PlayerAppearance::ResetDrawScale() noexcept
    {
        m_landingSquashRemaining = 0;
        m_landingSquashVertical = 1.0f;
        if (Owner() == nullptr)
        {
            return;
        }
        if (NS::Obj::Model* renderer = Owner()->ModelPart())
        {
            (void)renderer->SnapDrawScale(NS::Vector3{1.0f, 1.0f, 1.0f});
        }
    }

    void PlayerAppearance::AdvanceSpin() noexcept
    {
        m_spinDegreesThisFrame = 0.0f;
        NS::Obj::Model* renderer = nullptr;
        if (Owner() != nullptr)
        {
            renderer = Owner()->ModelPart();
        }

        if (!m_curled)
        {
            // 立ち姿は回さず、玉の回転と軸と速さも捨てる。丸まり直した玉へ前の玉の値を持ち越さない
            m_spin = NS::Quaternion::Identity;
            m_spinAxis = k_FirstSpinAxis;
            m_spinSpeed = 0.0f;
            // 前のフレームの値も揃える。今の値だけを戻すと、持ち替えたフレームの立ち姿が、玉の姿勢から戻る途中の
            // 傾きで描かれる
            if (renderer != nullptr)
            {
                renderer->SnapLocalRotation(m_spin);
            }
            return;
        }
        if (!m_actor->CanMoveBody())
        {
            // 当たりの止めで身体を動かせない間は、絵も止める
            return;
        }

        if (m_actor->IsBodySlamming())
        {
            SetRollAxisToward(m_actor->BodySlamVelocity(), m_spinAxis);
            m_spinSpeed = m_actor->BodySlamSpinSpeed();
        }
        else if (m_actor->ChargeJudge().IsHoldingCharge())
        {
            // 放せば出る向きへ回す。溜めて放した突進は狙いの線の向きへ、通常突進と線の無い時は
            // AimDirection の向きへ出る。狙いが決まらないフレームは前の軸で回し続ける
            NS::Vector3 aim = m_actor->AimDirection();
            NS::Game::Level::AimLine line{};
            if (m_actor->ChargeJudge().IsCharging() && m_actor->TryGetAimLine(line))
            {
                aim = line.direction;
            }
            SetRollAxisToward(aim, m_spinAxis);
            m_spinSpeed =
                m_emptyChargeSpinSpeed +
                (m_fullChargeSpinSpeed - m_emptyChargeSpinSpeed) * m_actor->ChargeJudge().Charge01();
        }
        else if ((m_actor->IsRebounding() || m_actor->IsSkidding()) && m_actor->ReboundMissTumble().has_value())
        {
            AdvanceMissTumble(*m_actor->ReboundMissTumble());
        }
        else if (m_actor->IsRebounding())
        {
            // 弾かれた向きへ前転する。真正面の当たりでは突進と逆向きになる
            // 反動の間は空中の操作で速度の向きが変わっても、弾かれた向きから取った軸のまま回す
            SetRollAxisToward(m_actor->ReboundDirection(), m_spinAxis);
            m_spinSpeed = m_actor->ReboundSpinSpeed();
        }
        // 放した後の空中と、反動に入らずに突進が終わった後は、直前のフレームの軸と速さのまま回る

        const float degrees = m_spinSpeed * NS::OS::FrameTimer::FixedDelta();
        const float radians = NS::ToRadians(NS::Degrees{degrees}).value;
        const NS::Quaternion turn = NS::Quaternion::CreateFromAxisAngle(m_spinAxis, radians);
        // 既に回った姿勢の後に今の軸の回転を足す。SimpleMath の q1 * q2 は q1 の後に q2 で、軸は根の空間で固定
        m_spin = m_spin * turn;
        m_spin.Normalize();
        m_spinDegreesThisFrame = degrees;

        if (renderer != nullptr)
        {
            renderer->SetLocalRotation(m_spin);
        }
    }

    void PlayerAppearance::AdvanceMissTumble(const MissTumble& tumble) noexcept
    {
        // 新しい反動の最初のフレームに、その時の回転を寄せ始めの回転として控える
        if (m_tumbleReboundCount != m_actor->ReboundCount())
        {
            m_tumbleReboundCount = m_actor->ReboundCount();
            m_tumbleStartSpin = m_spinAxis * m_spinSpeed;
            m_tumbleSteps = 0;
            m_tumbleWobblePhase = 0.0f;
            if (m_resolver != nullptr)
            {
                // 種は何回目の当たりか。黄金角ずつずらし、続けて外しても始まりの向きが重ならない。Replay では同じ
                constexpr float k_GoldenAngle = 2.39996323f;
                m_tumbleWobblePhase = std::fmod(static_cast<float>(m_resolver->LastImpact().sequence) * k_GoldenAngle,
                                                2.0f * NS::k_Pi);
            }
        }
        ++m_tumbleSteps;

        // 当たる前の回転を割合だけ残してねじれに足す。溜めて外したほど大きく振り回される
        const NS::Vector3 target = tumble.twist * (m_missTwistTurnsPerSecond * 360.0f * tumble.power) +
                                         m_tumbleStartSpin * m_missSpinCarryRatio;
        float blend = 1.0f;
        if (m_missSpinBlendSteps > 0)
        {
            blend = std::min(static_cast<float>(m_tumbleSteps) / static_cast<float>(m_missSpinBlendSteps), 1.0f);
        }
        // こすって止まる間は、身体の速さと同じ割合で回転も落とす
        const NS::Vector3 spin =
            (m_tumbleStartSpin + (target - m_tumbleStartSpin) * blend) * m_actor->SkidSpeedScale();
        const float speed = spin.Length();
        if (!std::isfinite(speed) || speed <= NS::k_Epsilon)
        {
            m_spinSpeed = 0.0f;
            return;
        }

        // 止まりかけのコマのように、軸自体をぶれの角度だけ傾け、傾けた向きをぶれの速さで回す
        const NS::Vector3 axis = spin / speed;
        NS::Vector3 helper{0.0f, 1.0f, 0.0f};
        if (std::abs(axis.y) > 0.9f)
        {
            helper = NS::Vector3{1.0f, 0.0f, 0.0f};
        }
        NS::Vector3 side = axis.Cross(helper);
        side.Normalize();
        const NS::Vector3 other = axis.Cross(side);
        const float phase = m_tumbleWobblePhase + 2.0f * NS::k_Pi * m_missWobbleTurnsPerSecond *
                                                      static_cast<float>(m_tumbleSteps) *
                                                      NS::OS::FrameTimer::FixedDelta();
        const float tilt = NS::ToRadians(NS::Degrees{m_missWobbleDegrees}).value;
        const NS::Vector3 lean = side * std::cos(phase) + other * std::sin(phase);
        NS::Vector3 tilted = axis * std::cos(tilt) + lean * std::sin(tilt);
        tilted.Normalize();
        m_spinAxis = tilted;
        m_spinSpeed = speed;
    }

    void PlayerAppearance::OnUpdate()
    {
        if (m_body == nullptr)
        {
            return;
        }
        if (m_actor->IsCurled())
        {
            Curl();
        }
        else
        {
            Uncurl();
        }
        AdvanceSpin();
        AdvanceLandingSquash();
        WriteDrawScale();
    }

    void PlayerAppearance::AdvanceLandingSquash() noexcept
    {
        if (!m_actor->CanMoveBody())
        {
            // 当たりの止めで身体を動かせない間は、潰れの戻しも止める
            return;
        }

        // 反動の出口の条件が成り立ったフレームが着地。移動の後に回るので、このフレームの接地を見る
        // 次のフレームに立ちへ移るので、1 回の反動で 1 フレームだけ成り立つ
        // 戻すフレーム数が 0 以下では戻す手段が無く、潰れたまま残るので潰さない
        if (m_actor->IsRebounding() && PlayerJudgeLand::Judge(m_body->IsGrounded(), m_body->VerticalVelocity()) &&
            m_landingSquashRecoverSteps > 0)
        {
            m_landingSquashRemaining = m_landingSquashRecoverSteps;
            m_landingSquashVertical = m_landingSquash;
        }
        else if (m_landingSquashRemaining > 0)
        {
            --m_landingSquashRemaining;
            if (m_landingSquashRemaining == 0)
            {
                // 補間の残差を残さない。元の形をそのまま使う
                m_landingSquashVertical = 1.0f;
                return;
            }
            const float total = static_cast<float>(m_landingSquashRecoverSteps);
            const float elapsed = total - static_cast<float>(m_landingSquashRemaining);
            m_landingSquashVertical = m_landingSquash + (1.0f - m_landingSquash) * (elapsed / total);
        }
        else
        {
            return;
        }

        if (!std::isfinite(m_landingSquashVertical) || !(m_landingSquashVertical > 0.0f))
        {
            NS_LOG_WARN(
                Game, "PlayerAppearance: 着地の潰れが有限の正でなく、潰さなかった: {}", m_landingSquash);
            m_landingSquashRemaining = 0;
            m_landingSquashVertical = 1.0f;
        }
    }

    void PlayerAppearance::WriteDrawScale() noexcept
    {
        if (Owner() == nullptr)
        {
            return;
        }
        NS::Obj::Model* renderer = Owner()->ModelPart();
        if (renderer == nullptr)
        {
            return;
        }
        // 当たりの潰れと伸びの間は構えを混ぜない。構えの最中に来た止めも元の形から潰す
        NS::Vector3 shape{1.0f, 1.0f, 1.0f};
        if (m_resolver != nullptr && m_resolver->IsShapeAnimating())
        {
            shape = m_resolver->ShapeFactors();
        }
        else if (m_actor != nullptr)
        {
            shape.y = m_actor->StanceHeight();
        }
        // 着地の潰れの水平は体積を保つ 1 ÷ √縦
        const float vertical = m_landingSquashVertical;
        const float horizontal = 1.0f / std::sqrt(vertical);
        const NS::Vector3 scale{shape.x * horizontal, shape.y * vertical, shape.z * horizontal};
        if (renderer->SetDrawScale(scale))
        {
            m_drawScaleRejected = false;
            return;
        }
        // 欄が有限の正でない間は毎フレーム断られるので、知らせるのは断られ始めたフレームだけ
        if (!m_drawScaleRejected)
        {
            NS_LOG_WARN(Game,
                        "PlayerAppearance: 描く形の倍率が有限の正でなく、書かなかった: ({}, {}, {})",
                        scale.x,
                        scale.y,
                        scale.z);
        }
        m_drawScaleRejected = true;
    }

    void PlayerAppearance::ResolveAssets(NS::Obj::AssetManager& assets)
    {
        NS::Gfx::Mesh* standingPlaceholder = nullptr;
        NS::Gfx::Mesh* ballPlaceholder = nullptr;
        const NS::Obj::Collider* collider = nullptr;
        if (Owner() != nullptr)
        {
            if (::Player* ownerPlayer = NS::Obj::Cast<::Player>(Owner()))
            {
                collider = &ownerPlayer->Collider();
            }
        }
        if (collider != nullptr)
        {
            // 寸法の正は移動と当たりの裁定が読むカプセルの欄。玉は円柱の長さ 0 のカプセルで、直径が当たりと揃う
            // 立ち姿は丸まりに依らない立ち姿の半長で作る。今の当たりの半長は玉の間 0 で、丸まっている間に引き直すと
            // 立ち姿まで玉になる
            standingPlaceholder =
                assets.GetOrMakeCapsuleMesh(collider->CapsuleRadius(), collider->StandingHalfHeight());
            ballPlaceholder = assets.GetOrMakeCapsuleMesh(collider->CapsuleRadius(), 0.0f);
        }
        else
        {
            NS_LOG_WARN(Game, "PlayerAppearance: 同居する当たりの部品 Collider が無く、仮の形の寸法を決められない");
        }

        m_standingMesh = ResolveLook(assets, m_standingMeshRef, standingPlaceholder);
        m_ballMesh = ResolveLook(assets, m_ballMeshRef, ballPlaceholder);
        ShowCurrentLook();
    }

    void PlayerAppearance::ShowCurrentLook() noexcept
    {
        if (Owner() == nullptr)
        {
            return;
        }
        NS::Obj::Model* renderer = Owner()->ModelPart();
        if (renderer == nullptr)
        {
            return;
        }

        NS::Gfx::Mesh* mesh = m_standingMesh;
        if (m_curled)
        {
            mesh = m_ballMesh;
        }
        // 引き当て前は Model が自分の参照から差した mesh を残す
        if (mesh == nullptr)
        {
            return;
        }
        renderer->SetMesh(mesh);
    }

    NS_CLASS(PlayerAppearance)
} // namespace NS::Game::Player
