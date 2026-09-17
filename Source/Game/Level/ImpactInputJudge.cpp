#include "Game/Level/ImpactInputJudge.h"

namespace NS::Game::Level
{
    namespace
    {
        // 毎秒 60 回数えると int は約 414 日であふれる。溜めは高々数十フレームなので頭打ちにしても判定に影響しない
        constexpr int k_MaxCountedSteps = 1 << 20;
    } // namespace

    const char* SlamKindLabel(SlamKind kind) noexcept
    {
        switch (kind)
        {
        case SlamKind::Tap:
            return "タップ";
        case SlamKind::Charged:
            return "チャージ";
        default:
            return "なし";
        }
    }

    void ImpactInputJudge::Step(bool held) noexcept
    {
        if (held)
        {
            // 押している間は何も控えない。押したフレームでタップを出すとチャージ狙いにも必ず 1 回混ざる
            if (m_heldSteps < k_MaxCountedSteps)
            {
				++m_heldSteps;
            }
            return;
        }

        if (m_heldSteps > 0)
        {
            // 発動点は離したフレームだけ
            if (IsCharging())
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

    bool ImpactInputJudge::IsCharging() const noexcept
    {
        // しきい値に 0 以下を入れられると無入力でもチャージ扱いになるので m_heldSteps > 0 も見る
        return m_heldSteps > 0 && m_heldSteps >= chargeThresholdSteps;
    }

    bool ImpactInputJudge::JustStartedCharging() const noexcept
    {
        if (!IsCharging())
        {
            return false;
        }
        // 入り口は 1 フレーム前の保持で判定を引き直して決める。控えた結果だと、しきい値を変えたフレームで境目がずれる
        const int previous = m_heldSteps - 1;
        return !(previous > 0 && previous >= chargeThresholdSteps);
    }

    bool ImpactInputJudge::IsChargeFull() const noexcept
    {
        return IsCharging() && m_heldSteps >= chargeMaxSteps;
    }

    int ImpactInputJudge::ChargeSteps() const noexcept
    {
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
} // namespace NS::Game::Level
