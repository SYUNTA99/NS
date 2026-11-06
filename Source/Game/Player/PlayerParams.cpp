#include "Game/Player/PlayerParams.h"

#include "Game/Level/LaunchArc.h"
#include "Game/Player/ReboundArc.h"

namespace GL
{
    namespace Player
    {
        PlayerParams::PlayerParams() noexcept = default;

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
