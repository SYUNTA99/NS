#include "Game/Player/PlayerParams.h"

#include "Game/Level/LaunchArc.h"
#include "Game/Player/LaunchPitch.h"
#include "Game/Player/ReboundArc.h"
#include "NSlib/Core/Math.h"

#include <cmath>

namespace GL
{
    namespace Player
    {
        PlayerParams::PlayerParams() noexcept
        {
            m_chargeFactorCurve.count = 2;
            m_chargeFactorCurve.keys[0] = NS::Curve::Key{0.0f, 1.0f};
            m_chargeFactorCurve.keys[1] = NS::Curve::Key{1.0f, 2.0f};
        }

        LaunchPath PlayerParams::SlamLaunchPath(float verticalSpeed, bool grounded, float dt) const noexcept
        {
            return LaunchPath{.horizontalSpeed = m_bodySlamSpeed,
                              .verticalSpeed = verticalSpeed,
                              .gravity = Gravity(),
                              .dt = dt,
                              .grounded = grounded,
                              .maxFrames = m_launchMaxFrames};
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
            if (!NS::IsPositive(factor))
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

        GL::Level::LaunchArc PlayerParams::ReboundLaunchArc(const ReboundArc& arc) const noexcept
        {
            // LaunchArc には正の上昇重力と下降倍率を渡す
            const GL::Level::AirGravity gravity = ReboundGravity();
            const float riseGravity = -gravity.rise;
            return GL::Level::LaunchArc{.direction = arc.direction,
                                        .distance = arc.distance,
                                        .apexHeight = arc.apexHeight,
                                        .riseGravity = riseGravity,
                                        .fallGravityScale = -gravity.fall / riseGravity,
                                        .apexBandSpeed = gravity.apexSpeed,
                                        .apexBandGravityScale = gravity.apexScale};
        }
    } // namespace Player
} // namespace GL
