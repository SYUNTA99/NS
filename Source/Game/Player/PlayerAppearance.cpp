#include "Game/Player/PlayerAppearance.h"

#include "Game/Level/ImpactInputJudge.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/SlamAim.h"
#include "Game/Player/PlayerStatus.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/Clock.h"
#include <algorithm>
#include <cmath>

namespace
{
    // 参照が書いてあればそのファイルの mesh、空か引き当てられなければ仮の形を返す
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

namespace GL
{
    namespace Player
    {
        PlayerAppearance::PlayerAppearance(NS::SceneActor* owner) : m_owner(owner)
        {
            NS::LoadAssetField(m_standingMesh, "capsule");
            NS::LoadAssetField(m_ballMesh, "sphere");
        }

        void PlayerAppearance::Curl() noexcept
        {
            ShowLook(m_ballMesh, m_ballScale);
        }

        void PlayerAppearance::Uncurl() noexcept
        {
            ShowLook(m_standingMesh, m_standingScale);
        }

        float PlayerAppearance::StanceHeight(const ChargeStatus& charge) const noexcept
        {
            if (!charge.Judge().IsHoldingCharge())
            {
                return 1.0f;
            }
            if (charge.Judge().IsCharging())
            {
                return m_chargeSquashScale;
            }
            return m_pressSquashScale;
        }

        void PlayerAppearance::ResetDrawScale() noexcept
        {
            m_landingSquashRemaining = 0;
            m_landingSquashVertical = 1.0f;
            if (m_owner == nullptr)
            {
                return;
            }
            if (NS::Model* renderer = m_owner->ModelClassObj())
            {
                (void)renderer->SnapDrawScale(NS::Vector3{1.0f, 1.0f, 1.0f});
            }
        }

        void PlayerAppearance::AdvanceSpin(const PlayerMotion& motion,
                                           const ChargeStatus& charge,
                                           const BodySlamStatus& slam,
                                           const ReboundStatus& rebound,
                                           const SkidStatus& skid,
                                           const NS::Vector3& aimDirection,
                                           const GL::Level::ImpactAnswer& impact) noexcept
        {
            m_spinDegreesThisFrame = 0.0f;
            NS::Model* renderer = nullptr;
            // 持ち主の無い部品は固定フレームの秒で回す
            float dt = NS::OS::FrameTimer::FixedDelta();
            if (m_owner != nullptr)
            {
                renderer = m_owner->ModelClassObj();
                dt = NS::GetFrameDelta(m_owner);
            }

            if (!motion.curled)
            {
                // 立ち姿は回さず、玉の回転と軸と速さも捨てる。丸まり直した玉へ前の玉の値を持ち越さない
                m_spin = NS::Quaternion::Identity;
                m_spinAxis = NS::Vector3::UnitX;
                m_spinSpeed = 0.0f;
                // 前のフレームの値も揃える。今の値だけを戻すと、持ち替えたフレームの立ち姿が、玉の姿勢から戻る途中の
                // 傾きで描かれる
                if (renderer != nullptr)
                {
                    renderer->SnapLocalRotation(m_spin);
                }
                return;
            }
            if (!motion.canMoveBody)
            {
                // 当たりの止めで身体を動かせない間は、絵も止める
                return;
            }

            if (motion.bodySlamming)
            {
                // 軸は水平の向きだけを見るので、突進の向きを使う。突進の速度の水平は向き ×
                // 欄の速さで、速さが正なら同じ向き
                SetRollAxisToward(slam.Direction(), m_spinAxis);
                m_spinSpeed = slam.SpinSpeed();
            }
            else if (charge.Judge().IsHoldingCharge())
            {
                // 放せば出る向きへ回す。溜めて放した突進は狙いの線の向きへ、通常突進と線の無い時は
                // aimDirection の向きへ出る。狙いが決まらないフレームは前の軸で回し続ける
                NS::Vector3 aim = aimDirection;
                GL::Level::AimLine line{};
                if (charge.Judge().IsCharging() && charge.TryGetAimLine(line))
                {
                    aim = line.direction;
                }
                SetRollAxisToward(aim, m_spinAxis);
                m_spinSpeed = m_emptyChargeSpinSpeed +
                              (m_fullChargeSpinSpeed - m_emptyChargeSpinSpeed) * charge.Judge().Charge01();
            }
            else if ((motion.rebounding || motion.skidding) && rebound.Tumble().has_value())
            {
                float skidScale = 1.0f;
                if (motion.skidding)
                {
                    skidScale = skid.SpeedScale();
                }
                AdvanceMissTumble(*rebound.Tumble(), rebound, skidScale, impact);
            }
            else if (motion.rebounding)
            {
                // 弾かれた向きへ前転する。真正面の当たりでは突進と逆向きになる
                // 反動の間は空中の操作で速度の向きが変わっても、弾かれた向きから取った軸のまま回す
                SetRollAxisToward(rebound.Direction(), m_spinAxis);
                m_spinSpeed = rebound.SpinSpeed();
            }
            // 放した後の空中と、反動に入らずに突進が終わった後は、直前のフレームの軸と速さのまま回る

            const float degrees = m_spinSpeed * dt;
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

        void PlayerAppearance::AdvanceMissTumble(const MissTumble& tumble,
                                                 const ReboundStatus& rebound,
                                                 float skidScale,
                                                 const GL::Level::ImpactAnswer& impact) noexcept
        {
            // 新しい反動の最初のフレームに、その時の回転を寄せ始めの回転として控える
            if (m_tumbleReboundCount != rebound.Count())
            {
                m_tumbleReboundCount = rebound.Count();
                m_tumbleStartSpin = m_spinAxis * m_spinSpeed;
                m_tumbleSteps = 0;
                // 種は何回目の当たりか。黄金角ずつずらし、続けて外しても始まりの向きが重ならない。同じ入力の再生では同じ
                const float goldenAngle = static_cast<float>(std::acos(-1.0) * (3.0 - std::sqrt(5.0)));
                m_tumbleWobblePhase =
                    std::fmod(static_cast<float>(impact.lastImpact.sequence) * goldenAngle, 2.0f * NS::k_Pi);
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
            const NS::Vector3 spin = (m_tumbleStartSpin + (target - m_tumbleStartSpin) * blend) * skidScale;
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
            float dt = NS::OS::FrameTimer::FixedDelta();
            if (m_owner != nullptr)
            {
                dt = NS::GetFrameDelta(m_owner);
            }
            // TODO: 歩の数に今の歩の秒を掛けるので、倍率が途中で変わると位相が跳ぶ
            // 外した反動の間に倍率を動かす時に、経過の秒を足し込む形へ変える
            const float phase = m_tumbleWobblePhase + 2.0f * NS::k_Pi * m_missWobbleTurnsPerSecond *
                                                          static_cast<float>(m_tumbleSteps) * dt;
            const float tilt = NS::ToRadians(NS::Degrees{m_missWobbleDegrees}).value;
            const NS::Vector3 lean = side * std::cos(phase) + other * std::sin(phase);
            NS::Vector3 tilted = axis * std::cos(tilt) + lean * std::sin(tilt);
            tilted.Normalize();
            m_spinAxis = tilted;
            m_spinSpeed = speed;
        }

        void PlayerAppearance::Update(const PlayerMotion& motion,
                                      const ChargeStatus& charge,
                                      const BodySlamStatus& slam,
                                      const ReboundStatus& rebound,
                                      const SkidStatus& skid,
                                      const NS::Vector3& aimDirection,
                                      const GL::Level::ImpactAnswer& impact)
        {
            AdvanceSpin(motion, charge, slam, rebound, skid, aimDirection, impact);
            AdvanceLandingSquash(motion);
            WriteDrawScale(charge, impact);
        }

        void PlayerAppearance::AdvanceLandingSquash(const PlayerMotion& motion) noexcept
        {
            if (!motion.canMoveBody)
            {
                // 当たりの止めで身体を動かせない間は、潰れの戻しも止める
                return;
            }

            // 移動の後に回るので、このフレームの着地を見る。次のフレームに立ちへ移るので、1 回の反動で 1 フレームだけ成り立つ
            // 戻すフレーム数が 0 以下では戻す手段が無く、潰れたまま残るので潰さない
            if (motion.reboundLanded && m_landingSquashRecoverSteps > 0)
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
                NS_LOG_WARN(Game, "PlayerAppearance: 着地の潰れが有限の正でなく、潰さなかった: {}", m_landingSquash);
                m_landingSquashRemaining = 0;
                m_landingSquashVertical = 1.0f;
            }
        }

        void PlayerAppearance::WriteDrawScale(const ChargeStatus& charge,
                                              const GL::Level::ImpactAnswer& impact) noexcept
        {
            // 当たりの潰れと伸びの間は構えを混ぜない。構えの最中に来た止めも元の形から潰す
            NS::Vector3 shape{1.0f, 1.0f, 1.0f};
            if (impact.shapeAnimating)
            {
                shape = impact.shapeFactors;
            }
            else
            {
                shape.y = StanceHeight(charge);
            }
            // 着地の潰れの水平は体積を保つ 1 ÷ √縦
            const float vertical = m_landingSquashVertical;
            const float horizontal = 1.0f / std::sqrt(vertical);
            const NS::Vector3 scale{shape.x * horizontal, shape.y * vertical, shape.z * horizontal};
            NS::Model* renderer = nullptr;
            if (m_owner != nullptr)
            {
                renderer = m_owner->ModelClassObj();
            }
            if (renderer == nullptr)
            {
                return;
            }
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

        void PlayerAppearance::ShowLook(NS::Mesh* mesh, const NS::Vector3& scale) noexcept
        {
            if (m_owner == nullptr)
            {
                return;
            }
            NS::Model* renderer = m_owner->ModelClassObj();
            if (renderer == nullptr)
            {
                return;
            }

            if (!renderer->SetLocalScale(scale))
            {
                NS_LOG_WARN(Game,
                            "PlayerAppearance: 姿の倍率が有限の正でなく、書かなかった: ({}, {}, {})",
                            scale.x,
                            scale.y,
                            scale.z);
            }
            renderer->SetMesh(mesh);
        }
    } // namespace Player
} // namespace GL
