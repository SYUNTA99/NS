#include "Game/Player/PlayerParams.h"
#include "Game/Player.h"
#include "Game/Player/PlayerAppearance.h"

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <cmath>

namespace NS::Game::Player
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
        const float clamped = NS::Core::Clamp(charge01, 0.0f, 1.0f);
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
            depth = NS::Core::Clamp(overcharge01, 0.0f, 1.0f);
        }
        return ChargeFactorFor(charge01) * (1.0f + (m_overchargePowerMax - 1.0f) * depth);
    }

    float PlayerParams::ChargingSpeedScale() const noexcept
    {
        // 減速率の欄は非有限の書き込みを捨てるので、ここへ来る値は有限。Clamp だけで 0..1 に収まる
        return NS::Core::Clamp(1.0f - m_chargeSlowRate, 0.0f, 1.0f);
    }

    void PlayerParams::ResolveAssets(NS::Obj::AssetManager& assets)
    {
        if (::Player* ownerPlayer = NS::Obj::Cast<::Player>(Owner()))
        {
            ownerPlayer->Appearance().ResolveAssets(assets);
        }
    }

    NS_CLASS(PlayerParams)
} // namespace NS::Game::Player
