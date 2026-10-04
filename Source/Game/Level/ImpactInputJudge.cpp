#include "Game/Level/ImpactInputJudge.h"

namespace NS::Game::Level
{
    const char* SlamKindLabel(SlamKind kind) noexcept
    {
        switch (kind)
        {
        case SlamKind::Tap:
            return "通常突進";
        case SlamKind::Charged:
            return "チャージ突進";
        default:
            return "なし";
        }
    }

    void ImpactInputJudge::Step(bool held) noexcept
    {
        if (held)
        {
            // 押している間は溜めすぎきったフレームの他は何も控えない。押したフレームで通常突進を出すとチャージ狙いにも
            // 必ず 1 回混ざる
            ++m_heldSteps;
            if (m_phase == HoldPhase::Charging && IsChargeFull() && m_heldSteps >= chargeMaxSteps + overchargeSteps)
            {
                m_phase = HoldPhase::AwaitingLaunch;
                m_fired = SlamKind::Charged;
            }
            return;
        }

        if (m_heldSteps > 0)
        {
            // 発動点は離したフレームだけ。控えた後の放しまで控えると、1 押しから技が 2 つ出る
            if (m_phase != HoldPhase::Charging)
            {
                m_releasedSteps = 0;
            }
            else if (IsCharging())
            {
                m_releasedSteps = m_heldSteps;
                m_fired = SlamKind::Charged;
            }
            else
            {
                m_releasedSteps = 0;
                m_fired = SlamKind::Tap;
            }
            m_heldSteps = 0;
            m_phase = HoldPhase::Charging;
        }
    }

    SlamKind ImpactInputJudge::TakeFired() noexcept
    {
        const SlamKind kind = m_fired;
        m_fired = SlamKind::None;
        return kind;
    }

    bool ImpactInputJudge::JustPressed() const noexcept
    {
        return m_heldSteps == 1;
    }

    bool ImpactInputJudge::IsHeld() const noexcept
    {
        return m_heldSteps > 0;
    }

    bool ImpactInputJudge::IsHoldingCharge() const noexcept
    {
        return IsHeld() && m_phase != HoldPhase::Spent;
    }

    bool ImpactInputJudge::ReachedThreshold() const noexcept
    {
        // しきい値に 0 以下を入れられると無入力でもチャージ扱いになるので m_heldSteps > 0 も見る
        return m_heldSteps > 0 && m_heldSteps >= chargeThresholdSteps;
    }

    bool ImpactInputJudge::IsCharging() const noexcept
    {
        return ReachedThreshold() && m_phase != HoldPhase::Spent;
    }

    bool ImpactInputJudge::JustStartedCharging() const noexcept
    {
        if (!IsCharging())
        {
            return false;
        }
        // 入り口は 1 フレーム前の保持で判定を引き直して決める。控えた結果だと、しきい値を変えたフレームで境目がずれる
        // 溜めすぎで出た後は IsCharging が偽なので、ここへは段階 Charging か AwaitingLaunch でしか来ない
        const int previous = m_heldSteps - 1;
        return !(previous > 0 && previous >= chargeThresholdSteps);
    }

    bool ImpactInputJudge::IsChargeFull() const noexcept
    {
        return IsCharging() && m_heldSteps >= chargeMaxSteps;
    }

    int ImpactInputJudge::ChargeSteps() const noexcept
    {
        if (m_phase == HoldPhase::Spent)
        {
            return 0;
        }
        if (m_heldSteps > 0)
        {
            return m_heldSteps;
        }
        return m_releasedSteps;
    }

    float ImpactInputJudge::Charge01() const noexcept
    {
        const int steps = ChargeSteps();
        if (steps <= 0 || steps < chargeThresholdSteps)
        {
            return 0.0f;
        }
        const int span = chargeMaxSteps - chargeThresholdSteps;
        // 満タン秒をしきい値以下にされると幅が 0 以下になり 0 除算になるため、割らずに満タンへ倒す
        if (span <= 0)
        {
            return 1.0f;
        }
        const float raw = static_cast<float>(steps - chargeThresholdSteps) / static_cast<float>(span);
        if (raw >= 1.0f)
        {
            return 1.0f;
        }
        return raw;
    }

    float ImpactInputJudge::Overcharge01() const noexcept
    {
        const int steps = ChargeSteps();
        if (steps <= 0 || steps < chargeThresholdSteps || steps < chargeMaxSteps)
        {
            return 0.0f;
        }
        // 溜めすぎのフレーム数を 0 以下にされると満タンのフレームに出るので、割らずに溜めすぎきりへ倒す
        if (overchargeSteps <= 0)
        {
            return 1.0f;
        }
        const float raw = static_cast<float>(steps - chargeMaxSteps) / static_cast<float>(overchargeSteps);
        if (raw >= 1.0f)
        {
            return 1.0f;
        }
        return raw;
    }

    int ImpactInputJudge::OverchargedSteps() const noexcept
    {
        const int steps = ChargeSteps();
        if (steps <= 0 || steps < chargeThresholdSteps || steps < chargeMaxSteps)
        {
            return 0;
        }
        return steps - chargeMaxSteps;
    }

    bool ImpactInputJudge::IsAwaitingLaunch() const noexcept
    {
        return m_phase == HoldPhase::AwaitingLaunch;
    }

    void ImpactInputJudge::MarkLaunched() noexcept
    {
        if (m_phase == HoldPhase::AwaitingLaunch)
        {
            m_phase = HoldPhase::Spent;
        }
    }
} // namespace NS::Game::Level
