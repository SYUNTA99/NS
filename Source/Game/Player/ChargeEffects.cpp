#include "Game/Player/ChargeEffects.h"
#include "Game/Player.h"

#include "Game/Level/ImpactInputJudge.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Player/PlayerAppearance.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/IUse/IUseEffect.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/SubObjects/EffectKeeper.h"
#include "NSlib/Object/Transform.h"

#include <algorithm>
#include <cmath>

namespace GL::Player
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

    ChargeEffects::ChargeEffects() noexcept : NS::Obj::SubObject() {}

    void ChargeEffects::OnStart()
    {
        if (::Player* ownerPlayer = NS::Obj::Cast<::Player>(Owner()))
        {
            m_actor = ownerPlayer;
            m_appearance = &ownerPlayer->Appearance();
            m_resolver = &ownerPlayer->Resolver();
        }

        if (const NS::Obj::Actor* owner = Owner())
        {
            NS::Obj::PreloadEffects(owner,
                                    {m_curlAsset,
                                     m_spinAsset,
                                     m_grindAsset,
                                     m_gatherAsset,
                                     m_fullAsset,
                                     m_burstAsset,
                                     m_trailAsset,
                                     m_swaySparksAsset});
        }
    }

    void ChargeEffects::OnUpdate()
    {
        if (m_actor == nullptr)
        {
            return;
        }
        ForgetEndedEffects();

        const GL::Level::ImpactInputJudge& judge = m_actor->ChargeJudge();
        const NS::Vector3 center = RootTransform().Position();
        // 溜めすぎで出た後は押したままでも溜めの層を消す
        const bool held = judge.IsHoldingCharge();
        if (judge.JustPressed())
        {
            StartPress(center);
        }
        if (judge.JustStartedCharging())
        {
            StartCharging(center);
        }
        if (held && judge.IsChargeFull() && !m_fullShown)
        {
            StartFullFlash(center);
        }
        m_framesSinceFull = 0;
        if (held && m_fullShown)
        {
            m_framesSinceFull = NS::Obj::EffectStep(m_actor) - m_fullStep + 1;
        }
        if (held)
        {
            FollowHeldEffects(center, judge.Charge01());
            if (m_actor->ChargeSwayReachedEdge())
            {
                PlaySwaySparks(center);
            }
        }
        else if (m_wasHeld)
        {
            ClearHeldEffects();
        }

        const bool slamming = m_actor->IsBodySlamming();
        if (slamming && !m_wasSlamming)
        {
            StartRelease(center);
        }
        UpdateTrail(center, slamming);
        // 接触前の縮みや止めを光で隠さないよう、接触が近づいたら放しの弾けを消す
        if (m_burst != 0 && m_resolver != nullptr && (m_resolver->IsBeforeContact() || IsContactNear()))
        {
            StopAndClear(m_burst);
        }

        m_wasHeld = held;
        m_wasSlamming = slamming;
    }

    void ChargeEffects::OnEndPlay()
    {
        ClearHeldEffects();
        StopAndClear(m_trail);
        StopAndClear(m_burst);
        m_wasHeld = false;
        m_wasSlamming = false;
        m_trailAwaitsFreeze = false;
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
        const auto forgetIfEnded = [this](std::uint32_t& id) {
            if (id == 0)
            {
                return;
            }
            const NS::Obj::EffectRecord* record = NS::Obj::FindEffect(m_actor, id);
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

    void ChargeEffects::StartPress(const NS::Vector3& center)
    {
        // 前の押しの層が残っていれば、新しい押しの層と重ねない
        StopAndClear(m_curl);
        StopAndClear(m_spin);
        m_fullShown = false;
        m_spinDegrees = 0.0f;

        m_curl = NS::Obj::PlayEffect(m_actor, m_curlAsset, PlayDesc(center, NS::Quaternion::Identity, 0.0f));
        NS::Obj::StopEffectAfter(m_actor, m_curl, std::max(m_curlSteps, 1));
        m_spin = NS::Obj::PlayEffect(m_actor, m_spinAsset, PlayDesc(center, NS::Quaternion::Identity, 0.0f));
        // 玉を包む光は押したフレームから出し、丸まりの殻が消えた後も放すまで途切れさせない
        StopAndClear(m_gather);
        m_gather = NS::Obj::PlayEffect(m_actor, m_gatherAsset, PlayDesc(center, YawToward(HeldAimDirection()), 0.0f));
    }

    void ChargeEffects::StartCharging(const NS::Vector3& center)
    {
        StopAndClear(m_grind);
        const float charge01 = m_actor->ChargeJudge().Charge01();
        m_grind = NS::Obj::PlayEffect(m_actor, m_grindAsset, PlayDesc(center, YawToward(HeldAimDirection()), charge01));
    }

    void ChargeEffects::StartFullFlash(const NS::Vector3& center)
    {
        m_fullShown = true;
        m_fullStep = NS::Obj::EffectStep(m_actor);
        StopAndClear(m_full);
        m_full = NS::Obj::PlayEffect(m_actor, m_fullAsset, PlayDesc(center, NS::Quaternion::Identity, 1.0f));
        NS::Obj::StopEffectAfter(m_actor, m_full, std::max(m_fullFlashSteps, 1));
    }

    void ChargeEffects::PlaySwaySparks(const NS::Vector3& center)
    {
        GL::Level::AimLine line{};
        if (!m_actor->TryGetAimLine(line))
        {
            return;
        }
        float side = 1.0f;
        if (m_actor->ChargeSwayOffset() < 0.0f)
        {
            side = -1.0f;
        }
        // 擦れの節は +Y の 45 度の円錐へ飛ぶ。+Y を横から 45 度起こすと円錐の下の縁が水平になり、床へ潜る粒が出ない
        const NS::Vector3 right{line.direction.z, 0.0f, -line.direction.x};
        NS::Vector3 heading = right * side + NS::Vector3{0.0f, m_sideLift, 0.0f};
        heading.Normalize();

        const float depth = NS::Clamp(m_actor->ChargeJudge().Overcharge01(), 0.0f, 1.0f);
        const float count = static_cast<float>(m_overchargeSparkCountMin) +
                            static_cast<float>(m_overchargeSparkCountMax - m_overchargeSparkCountMin) * depth;
        const float speed = m_overchargeSparkSpeedMin + (m_overchargeSparkSpeedMax - m_overchargeSparkSpeedMin) * depth;
        NS::Gfx::EffectPlayDesc sparks{};
        sparks.position = center;
        sparks.rotation = NS::Quaternion::FromToRotation(NS::Vector3{0.0f, 1.0f, 0.0f}, heading);
        sparks.dynamicInputs[0] = 0.0f;
        sparks.dynamicInputs[1] = std::round(count);
        // 秒の速さを 1 フレームの距離にして絵へ渡す
        sparks.dynamicInputs[2] = speed / 60.0f;
        sparks.dynamicInputs[3] = 0.0f;
        static_cast<void>(NS::Obj::PlayEffect(m_actor, m_swaySparksAsset, sparks));
    }

    void ChargeEffects::FollowHeldEffects(const NS::Vector3& center, float charge01) noexcept
    {
        if (m_appearance != nullptr)
        {
            m_spinDegrees = std::fmod(m_spinDegrees + m_appearance->SpinDegreesThisFrame(), 360.0f);
        }
        const NS::Quaternion spinBoard =
            NS::Quaternion::CreateFromAxisAngle(NS::Vector3::UnitZ, NS::ToRadians(NS::Degrees{m_spinDegrees}).value);
        Place(m_curl, center, NS::Quaternion::Identity);
        Place(m_spin, center, spinBoard);
        Place(m_grind, center, YawToward(HeldAimDirection()));
        // 溜まる光の根は狙いの線 (カメラの正面の水平の向き) へ回す。定義は根の手前に低く、奥に高く光の点を生む
        Place(m_gather, center, YawToward(HeldAimDirection()));
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

    void ChargeEffects::StartRelease(const NS::Vector3& center)
    {
        const float charge01 = m_actor->BodySlamCharge01();
        m_slamDirection = m_actor->BodySlamDirection();
        const NS::Quaternion facing = YawToward(m_slamDirection);

        // 効果の全体を縮めると、通常突進の散って残る筋は玉の輪郭の内側で生まれ、はじけの光も画面を明るくしない。
        // 大きさを変えるのは輪・丸屋根・筋だけにする
        NS::Gfx::EffectPlayDesc burst = PlayDesc(center, facing, charge01);
        burst.dynamicInputs[m_burstScaleInput] = ReleaseBurstScale(charge01);
        const std::uint32_t burstId = NS::Obj::PlayEffect(m_actor, m_burstAsset, burst);
        NS::Obj::StopEffectAfter(m_actor, burstId, std::max(m_burstSteps, 1));
        m_burst = burstId;

        // 前の突進の尾がまだ伸びていれば止め、新しい尾と繋げない
        if (m_trail != 0)
        {
            StopTrailRoot();
        }
        m_trailAwaitsFreeze = false;
        m_trail = NS::Obj::PlayEffect(m_actor, m_trailAsset, PlayDesc(center, facing, charge01));
    }

    void ChargeEffects::UpdateTrail(const NS::Vector3& center, bool slamming) noexcept
    {
        if (m_trail == 0)
        {
            return;
        }
        const NS::Obj::EffectRecord* record = NS::Obj::FindEffect(m_actor, m_trail);
        if (record == nullptr || record->rootStopStep.has_value())
        {
            return;
        }

        bool stop = false;
        if (m_resolver != nullptr && m_resolver->FreezeBeganThisStep())
        {
            stop = true;
        }
        else if (m_trailAwaitsFreeze)
        {
            // 当たりを検知した次のフレームに止めが始まらなかった (止めが 0 の当たり)
            stop = true;
        }
        else if (!slamming)
        {
            // 当たりで突進が打ち切られたフレームは、次のフレームの止めの頭まで尾を残す
            if (m_resolver != nullptr && (m_resolver->DidRebound() || m_resolver->DidBreak()))
            {
                m_trailAwaitsFreeze = true;
            }
            else
            {
                stop = true;
            }
        }
        if (stop)
        {
            StopTrailRoot();
            return;
        }
        if (slamming)
        {
            m_slamDirection = m_actor->BodySlamVelocity();
        }
        Place(m_trail, center, YawToward(m_slamDirection));
    }

    void ChargeEffects::StopTrailRoot() noexcept
    {
        NS::Obj::StopEffectRoot(m_actor, m_trail);
        NS::Obj::StopEffectAfter(m_actor, m_trail, std::max(m_trailFadeSteps, 1));
        m_trailAwaitsFreeze = false;
    }

    void ChargeEffects::Place(std::uint32_t id, const NS::Vector3& position, const NS::Quaternion& rotation) noexcept
    {
        if (id == 0)
        {
            return;
        }
        const NS::Obj::EffectRecord* record = NS::Obj::FindEffect(m_actor, id);
        if (record == nullptr || record->endStep.has_value())
        {
            return;
        }
        // 描画の無い世界でも向きは記録に残し、試しが読む
        NS::Obj::SetEffectRotation(m_actor, id, rotation);
        // TODO: 固定ステップの位置へ置いている。60 を超える画面で段々に見えたら、補間の位置で渡す
        NS::Obj::SetEffectTransform(m_actor, id, position, rotation, NS::Vector3::One);
    }

    void ChargeEffects::SetCharge(std::uint32_t id, float charge01) const noexcept
    {
        SetInput(id, m_chargeInput, charge01);
    }

    void ChargeEffects::SetInput(std::uint32_t id, int index, float value) const noexcept
    {
        if (id == 0)
        {
            return;
        }
        const NS::Obj::EffectRecord* record = NS::Obj::FindEffect(m_actor, id);
        if (record == nullptr || record->endStep.has_value())
        {
            return;
        }
        NS::Obj::SetEffectDynamicInput(m_actor, id, index, value);
    }

    NS::Vector3 ChargeEffects::HeldAimDirection() const noexcept
    {
        GL::Level::AimLine line{};
        if (m_actor != nullptr)
        {
            if (m_actor->TryGetAimLine(line))
            {
                return line.direction;
            }
            return m_actor->AimDirection();
        }
        return NS::Vector3{0.0f, 0.0f, 1.0f};
    }

    bool ChargeEffects::IsContactNear() const noexcept
    {
        if (m_resolver->DidRebound() || m_resolver->DidBreak())
        {
            return true;
        }
        const int frames = m_resolver->FramesToPredictedContact();
        return frames >= 0 && frames <= std::max(m_burstClearFrames, 0);
    }

    void ChargeEffects::StopAndClear(std::uint32_t& id) noexcept
    {
        if (id == 0)
        {
            return;
        }
        NS::Obj::StopEffect(m_actor, id);
        id = 0;
    }

    NS_CLASS(ChargeEffects)
} // namespace GL::Player
