#include "Game/Player/PlayerParams.h"

#include "NSlib/Core/Math.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

#include <cmath>

namespace GL::Player
{
    PlayerParams::PlayerParams() noexcept
    {
        m_chargeFactorCurve.count = 2;
        m_chargeFactorCurve.keys[0] = NS::Obj::Curve::Key{0.0f, 1.0f};
        m_chargeFactorCurve.keys[1] = NS::Obj::Curve::Key{1.0f, 2.0f};
    }

    float PlayerParams::ChargeFactorFor(float charge01) const noexcept
    {
        if (!std::isfinite(charge01))
        {
            return 1.0f;
        }
        const float clamped = NS::Clamp(charge01, 0.0f, 1.0f);
        const float factor = m_chargeFactorCurve.Evaluate(clamped);
        // Inspector で点を全部消すと Evaluate が 0 を返して威力が消えるため、0 以下は 1 とみなす
        if (!(factor > 0.0f))
        {
            return 1.0f;
        }
        return factor;
    }

    float PlayerParams::ChargeFactorFor(float charge01, float overcharge01) const noexcept
    {
        float depth = 0.0f;
        if (std::isfinite(overcharge01))
        {
            depth = NS::Clamp(overcharge01, 0.0f, 1.0f);
        }
        return ChargeFactorFor(charge01) * (1.0f + (m_overchargePowerMax - 1.0f) * depth);
    }

    float PlayerParams::ChargingSpeedScale() const noexcept
    {
        // 減速率の欄は非有限の書き込みを捨てるので、ここへ来る値は有限。Clamp だけで 0..1 に収まる
        return NS::Clamp(1.0f - m_chargeSlowRate, 0.0f, 1.0f);
    }

    NS_CLASS(PlayerParams)
} // namespace GL::Player
