#include "Game/Player/ChargeEffects.h"
#include "NSlib/Core/Types.h"

#include "Game/Level/ImpactInputJudge.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerStatus.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/EffectKeeper.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IUse/IUseEffect.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <cmath>

namespace GL
{
    namespace Player
    {
        NS::Gfx::EffectPlayDesc ChargeEffects::PlayDesc(const NS::Vector3& position,
                                                        const NS::Quaternion& rotation,
                                                        float charge01) const noexcept
        {
            NS::Gfx::EffectPlayDesc desc{};
            desc.position = position;
            desc.rotation = rotation;
            desc.dynamicInputs[m_chargeInput] = charge01;
            return desc;
        }

        ChargeEffects::ChargeEffects(NS::SceneActor* owner) noexcept : m_owner(owner) {}

        void ChargeEffects::Preload()
        {
            NS::PreloadEffects(m_owner,
                               {m_curlAsset,
                                m_spinAsset,
                                m_grindAsset,
                                m_gatherAsset,
                                m_fullAsset,
                                m_burstAsset,
                                m_trailAsset,
                                m_swaySparksAsset});
        }

        void ChargeEffects::Update(const ChargeStatus& charge,
                                   const BodySlamStatus& slam,
                                   const PlayerMotion& motion,
                                   const NS::Vector3& aimDirection,
                                   const PlayerAppearance* appearance,
                                   const GL::Level::ImpactAnswer& impact)
        {
            NS_ASSERT(Game, appearance != nullptr, "見た目の部品が渡されていない");
            ForgetEndedEffects();

            const GL::Level::ImpactInputJudge& judge = charge.Judge();
            const NS::Vector3 center = m_owner->Root().Position();
            // 溜めすぎで出た後は押したままでも溜めの層を消す
            const bool held = judge.IsHoldingCharge();
            if (judge.JustPressed())
            {
                StartPress(center, aimDirection);
            }
            if (judge.JustStartedCharging())
            {
                StartCharging(center, aimDirection, judge.Charge01());
            }
            if (held && judge.IsChargeFull() && !m_fullShown)
            {
                StartFullFlash(center);
            }
            m_framesSinceFull = 0;
            if (held && m_fullShown)
            {
                m_framesSinceFull = NS::GetEffectStep(m_owner) - m_fullStep + 1;
            }
            if (held)
            {
                FollowHeldEffects(center, aimDirection, judge.Charge01(), appearance->SpinDegreesThisFrame());
                if (charge.SwayReachedEdge())
                {
                    PlaySwaySparks(center, charge);
                }
            }
            else if (m_wasHeld)
            {
                ClearHeldEffects();
            }

            const bool slamming = motion.bodySlamming;
            if (slamming && !m_wasSlamming)
            {
                StartRelease(center, slam);
            }
            UpdateTrail(center, slam, slamming, impact);
            // 接触前の縮みや止めを光で隠さないよう、当たる直前に入るか接触が近づいたら放しの弾けを消す
            if (m_burst != 0 && (motion.beforeContact || IsContactNear(impact)))
            {
                StopAndClear(m_burst);
            }

            m_wasHeld = held;
            m_wasSlamming = slamming;
        }

        void ChargeEffects::Clear()
        {
            ClearHeldEffects();
            StopAndClear(m_trail);
            StopAndClear(m_burst);
            m_wasHeld = false;
            m_wasSlamming = false;
            m_framesSinceFull = 0;
        }

        float ChargeEffects::ReleaseBurstScale(float charge01) const noexcept
        {
            float charge = 0.0f;
            if (std::isfinite(charge01))
            {
                charge = std::clamp(charge01, 0.0f, 1.0f);
            }
            return m_tapBurstScale + m_fullBurstScaleGain * charge;
        }

        NS::Quaternion ChargeEffects::YawToward(const NS::Vector3& direction) noexcept
        {
            const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
            if (!std::isfinite(length) || length <= 0.0f)
            {
                return NS::Quaternion::Identity;
            }
            const float yaw = std::atan2(direction.x, direction.z);
            return NS::Quaternion::CreateFromAxisAngle(NS::Vector3{0.0f, 1.0f, 0.0f}, yaw);
        }

        void ChargeEffects::ForgetEndedEffects() noexcept
        {
            const auto forgetIfEnded = [this](NS::uint32& id) {
                if (id == 0)
                {
                    return;
                }
                const NS::EffectRecord* record = NS::FindEffect(m_owner, id);
                if (record != nullptr && record->endStep.has_value())
                {
                    id = 0;
                }
            };
            forgetIfEnded(m_curl);
            forgetIfEnded(m_burst);
            forgetIfEnded(m_trail);
            forgetIfEnded(m_full);
        }

        void ChargeEffects::StartPress(const NS::Vector3& center, const NS::Vector3& aimDirection)
        {
            // 前の押しの層が残っていれば、新しい押しの層と重ねない
            StopAndClear(m_curl);
            StopAndClear(m_spin);
            m_fullShown = false;
            m_spinDegrees = 0.0f;

            m_curl = NS::PlayEffect(m_owner, m_curlAsset, PlayDesc(center, NS::Quaternion::Identity, 0.0f));
            NS::StopEffectAfter(m_owner, m_curl, std::max(m_curlSteps, 1));
            m_spin = NS::PlayEffect(m_owner, m_spinAsset, PlayDesc(center, NS::Quaternion::Identity, 0.0f));
            // 玉を包む光は押したフレームから出し、丸まりの殻が消えた後も放すまで途切れさせない
            StopAndClear(m_gather);
            m_gather = NS::PlayEffect(m_owner, m_gatherAsset, PlayDesc(center, YawToward(aimDirection), 0.0f));
        }

        void ChargeEffects::StartCharging(const NS::Vector3& center, const NS::Vector3& aimDirection, float charge01)
        {
            StopAndClear(m_grind);
            m_grind = NS::PlayEffect(m_owner, m_grindAsset, PlayDesc(center, YawToward(aimDirection), charge01));
        }

        void ChargeEffects::StartFullFlash(const NS::Vector3& center)
        {
            m_fullShown = true;
            m_fullStep = NS::GetEffectStep(m_owner);
            StopAndClear(m_full);
            m_full = NS::PlayEffect(m_owner, m_fullAsset, PlayDesc(center, NS::Quaternion::Identity, 1.0f));
            NS::StopEffectAfter(m_owner, m_full, std::max(m_fullFlashSteps, 1));
        }

        void ChargeEffects::PlaySwaySparks(const NS::Vector3& center, const ChargeStatus& charge)
        {
            GL::Level::AimLine line{};
            if (!charge.TryGetAimLine(line))
            {
                return;
            }
            float side = 1.0f;
            if (charge.SwayOffset() < 0.0f)
            {
                side = -1.0f;
            }
            // 擦れの節は +Y の 45 度の円錐へ飛ぶ。+Y を横から 45 度起こすと円錐の下の縁が水平になり、床へ潜る粒が出ない
            const NS::Vector3 right{line.direction.z, 0.0f, -line.direction.x};
            NS::Vector3 heading = right * side + NS::Vector3{0.0f, m_sideLift, 0.0f};
            heading.Normalize();

            const float depth = NS::Clamp(charge.Judge().Overcharge01(), 0.0f, 1.0f);
            const float count = static_cast<float>(m_overchargeSparkCountMin) +
                                static_cast<float>(m_overchargeSparkCountMax - m_overchargeSparkCountMin) * depth;
            const float speed =
                m_overchargeSparkSpeedMin + (m_overchargeSparkSpeedMax - m_overchargeSparkSpeedMin) * depth;
            NS::Gfx::EffectPlayDesc sparks{};
            sparks.position = center;
            sparks.rotation = NS::Quaternion::FromToRotation(NS::Vector3{0.0f, 1.0f, 0.0f}, heading);
            sparks.dynamicInputs[0] = 0.0f;
            sparks.dynamicInputs[1] = std::round(count);
            // 秒の速さを 1 フレームの距離にして絵へ渡す
            sparks.dynamicInputs[2] = speed / 60.0f;
            sparks.dynamicInputs[3] = 0.0f;
            static_cast<void>(NS::PlayEffect(m_owner, m_swaySparksAsset, sparks));
        }

        void ChargeEffects::FollowHeldEffects(const NS::Vector3& center,
                                              const NS::Vector3& aimDirection,
                                              float charge01,
                                              float spinDegreesThisFrame) noexcept
        {
            m_spinDegrees = std::fmod(m_spinDegrees + spinDegreesThisFrame, 360.0f);
            const NS::Quaternion spinBoard = NS::Quaternion::CreateFromAxisAngle(
                NS::Vector3::UnitZ, NS::ToRadians(NS::Degrees{m_spinDegrees}).value);
            Place(m_curl, center, NS::Quaternion::Identity);
            Place(m_spin, center, spinBoard);
            Place(m_grind, center, YawToward(aimDirection));
            // 溜まる光の根は今放せば出る向きへ回す。溜めている間は狙いの線の向きになる。
            // 定義は根の手前に低く、奥に高く光の点を生む
            Place(m_gather, center, YawToward(aimDirection));
            Place(m_full, center, NS::Quaternion::Identity);
            SetCharge(m_spin, charge01);
            SetCharge(m_grind, charge01);
            SetCharge(m_gather, charge01);
            // 溜まる光は溜めきりで玉を包む光を広がりきった大きさの光に替え、この数で F + 4 までの替わり方を選び、
            // F + 5 から放すまで落ち着いた光を出す
            SetInput(m_gather, m_fullFramesInput, static_cast<float>(m_framesSinceFull));
        }

        void ChargeEffects::ClearHeldEffects() noexcept
        {
            // すぐ放した時も殻や閃きが残らないよう、保持中の層を同時に消す
            StopAndClear(m_curl);
            StopAndClear(m_spin);
            StopAndClear(m_grind);
            StopAndClear(m_gather);
            StopAndClear(m_full);
        }

        void ChargeEffects::StartRelease(const NS::Vector3& center, const BodySlamStatus& slam)
        {
            const float charge01 = slam.Charge01();
            m_slamDirection = slam.Direction();
            const NS::Quaternion facing = YawToward(m_slamDirection);

            // 効果の全体を縮めると、通常突進の散って残る筋は玉の輪郭の内側で生まれ、はじけの光も画面を明るくしない。
            // 大きさを変えるのは輪・丸屋根・筋だけにする
            NS::Gfx::EffectPlayDesc burst = PlayDesc(center, facing, charge01);
            burst.dynamicInputs[m_burstScaleInput] = ReleaseBurstScale(charge01);
            const NS::uint32 burstId = NS::PlayEffect(m_owner, m_burstAsset, burst);
            NS::StopEffectAfter(m_owner, burstId, std::max(m_burstSteps, 1));
            m_burst = burstId;

            // 前の突進の尾がまだ伸びていれば止め、新しい尾と繋げない
            if (m_trail != 0)
            {
                StopTrailRoot();
            }
            m_trail = NS::PlayEffect(m_owner, m_trailAsset, PlayDesc(center, facing, charge01));
        }

        void ChargeEffects::UpdateTrail(const NS::Vector3& center,
                                        const BodySlamStatus& slam,
                                        bool slamming,
                                        const GL::Level::ImpactAnswer& impact) noexcept
        {
            if (m_trail == 0)
            {
                return;
            }
            const NS::EffectRecord* record = NS::FindEffect(m_owner, m_trail);
            if (record == nullptr || record->rootStopStep.has_value())
            {
                return;
            }

            // 尾を止めるのは当たりの状態に入ったフレーム。当たりで突進が打ち切られた検知のフレームは残す
            bool stop = false;
            if (impact.beginHit)
            {
                stop = true;
            }
            else if (!slamming && !impact.rebounded && !impact.broke)
            {
                stop = true;
            }
            if (stop)
            {
                StopTrailRoot();
                return;
            }
            // 尾は水平の向きだけを見るので、突進の向きを使う。突進の速度の水平は向き × 欄の速さで、速さが正なら同じ向き
            if (slamming)
            {
                m_slamDirection = slam.Direction();
            }
            Place(m_trail, center, YawToward(m_slamDirection));
        }

        void ChargeEffects::StopTrailRoot() noexcept
        {
            NS::StopEffectRoot(m_owner, m_trail);
            NS::StopEffectAfter(m_owner, m_trail, std::max(m_trailFadeSteps, 1));
        }

        void ChargeEffects::Place(NS::uint32 id, const NS::Vector3& position, const NS::Quaternion& rotation) noexcept
        {
            if (id == 0)
            {
                return;
            }
            const NS::EffectRecord* record = NS::FindEffect(m_owner, id);
            if (record == nullptr || record->endStep.has_value())
            {
                return;
            }
            // 描画の無い世界でも向きは記録に残し、試しが読む
            NS::SetEffectRotation(m_owner, id, rotation);
            // TODO: 固定ステップの位置へ置いている。60 を超える画面で段々に見えたら、補間の位置で渡す
            NS::SetEffectTransform(m_owner, id, position, rotation, NS::Vector3::One);
        }

        void ChargeEffects::SetCharge(NS::uint32 id, float charge01) const noexcept
        {
            SetInput(id, m_chargeInput, charge01);
        }

        void ChargeEffects::SetInput(NS::uint32 id, int index, float value) const noexcept
        {
            if (id == 0)
            {
                return;
            }
            const NS::EffectRecord* record = NS::FindEffect(m_owner, id);
            if (record == nullptr || record->endStep.has_value())
            {
                return;
            }
            NS::SetEffectDynamicInput(m_owner, id, index, value);
        }

        bool ChargeEffects::IsContactNear(const GL::Level::ImpactAnswer& impact) const noexcept
        {
            if (impact.rebounded || impact.broke)
            {
                return true;
            }
            const int frames = impact.framesToPredictedContact;
            return frames >= 0 && frames <= std::max(m_burstClearFrames, 0);
        }

        void ChargeEffects::StopAndClear(NS::uint32& id) noexcept
        {
            if (id == 0)
            {
                return;
            }
            NS::StopEffect(m_owner, id);
            id = 0;
        }
    } // namespace Player
} // namespace GL
