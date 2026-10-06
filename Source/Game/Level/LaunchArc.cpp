#include "Game/Level/LaunchArc.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        // 曲線を式で辿るための値。LaunchArc の欄から毎回組み直す
        struct ArcShape
        {
            NS::Vector3 forward{};        // 水平の向き (長さ 1)
            float horizontalSpeed = 0.0f; // 水平の速さ (m/s)
            float riseSpeed = 0.0f;       // 発射の瞬間の上向きの速さ (m/s)
            float riseGravity = 0.0f;     // 上りの重力の大きさ (m/s^2)
            float fallGravity = 0.0f;     // 下りの重力の大きさ (m/s^2)
            float bandSpeed = 0.0f;       // 頂点の帯の縦速度 (m/s)
            float bandScale = 1.0f;       // 頂点の帯の間に重力へ掛ける倍率
        };

        // 上りの帯の外・上りの帯・下りの帯の 3 区間の秒。下りの帯の外は発射の高さを過ぎても落ち続ける
        struct ArcSegments
        {
            float riseOutside = 0.0f;
            float riseBand = 0.0f;
            float fallBand = 0.0f;
        };

        [[nodiscard]] ArcSegments SegmentsOf(const ArcShape& shape) noexcept
        {
            ArcSegments segments;
            float bandEntrySpeed = shape.riseSpeed;
            if (shape.riseSpeed > shape.bandSpeed)
            {
                segments.riseOutside = (shape.riseSpeed - shape.bandSpeed) / shape.riseGravity;
                bandEntrySpeed = shape.bandSpeed;
            }
            segments.riseBand = bandEntrySpeed / (shape.riseGravity * shape.bandScale);
            segments.fallBand = shape.bandSpeed / (shape.fallGravity * shape.bandScale);
            return segments;
        }

        // 発射の高さへ戻るまでの下りの秒
        [[nodiscard]] float FallSecondsOf(const ArcShape& shape, float apexHeight) noexcept
        {
            const float bandGravity = shape.fallGravity * shape.bandScale;
            const float bandDrop = shape.bandSpeed * shape.bandSpeed / (2.0f * bandGravity);
            if (apexHeight <= bandDrop)
            {
                return std::sqrt(2.0f * apexHeight / bandGravity);
            }
            const float landingSpeed =
                std::sqrt(shape.bandSpeed * shape.bandSpeed + 2.0f * shape.fallGravity * (apexHeight - bandDrop));
            return shape.bandSpeed / bandGravity + (landingSpeed - shape.bandSpeed) / shape.fallGravity;
        }

        // arc が曲線にならない値なら false
        [[nodiscard]] bool TryShapeOf(const LaunchArc& arc, ArcShape& outShape) noexcept
        {
            if (!NS::IsPositiveFinite(arc.distance) || !NS::IsPositiveFinite(arc.apexHeight) ||
                !NS::IsPositiveFinite(arc.riseGravity) || !NS::IsPositiveFinite(arc.fallGravityScale) ||
                !NS::IsPositiveFinite(arc.apexBandGravityScale) || !std::isfinite(arc.apexBandSpeed) ||
                arc.apexBandSpeed < 0.0f || !NS::IsFinite(arc.direction))
            {
                return false;
            }
            ArcShape shape;
            if (!NS::TryNormalizeHorizontal(arc.direction, shape.forward))
            {
                return false;
            }
            shape.riseGravity = arc.riseGravity;
            shape.fallGravity = shape.riseGravity * arc.fallGravityScale;
            shape.bandSpeed = arc.apexBandSpeed;
            shape.bandScale = arc.apexBandGravityScale;

            // 帯の中は重力に倍率が掛かるぶん、同じ高さに要る初速が変わる。帯より遅く飛び出すなら上りは全部帯の中
            const float bandSquared = shape.bandSpeed * shape.bandSpeed;
            const float bandRiseHeight = bandSquared / (2.0f * shape.riseGravity * shape.bandScale);
            if (arc.apexHeight <= bandRiseHeight)
            {
                shape.riseSpeed = std::sqrt(2.0f * shape.riseGravity * shape.bandScale * arc.apexHeight);
            }
            else
            {
                shape.riseSpeed = std::sqrt(2.0f * shape.riseGravity * arc.apexHeight -
                                            bandSquared * (1.0f / shape.bandScale - 1.0f));
            }

            const ArcSegments segments = SegmentsOf(shape);
            const float flightSeconds = segments.riseOutside + segments.riseBand + FallSecondsOf(shape, arc.apexHeight);
            shape.horizontalSpeed = arc.distance / flightSeconds;
            if (!std::isfinite(shape.horizontalSpeed) || !std::isfinite(shape.riseSpeed))
            {
                return false;
            }
            outShape = shape;
            return true;
        }

        // 発射から seconds 秒後の、起点から見た位置
        [[nodiscard]] NS::Vector3 ArcOffsetAt(const ArcShape& shape, float seconds) noexcept
        {
            const ArcSegments segments = SegmentsOf(shape);
            const NS::Vector3 horizontal = shape.forward * (shape.horizontalSpeed * seconds);

            float t = seconds;
            if (t <= segments.riseOutside)
            {
                const float height = shape.riseSpeed * t - 0.5f * shape.riseGravity * t * t;
                return NS::Vector3{horizontal.x, height, horizontal.z};
            }
            float height = shape.riseSpeed * segments.riseOutside -
                           0.5f * shape.riseGravity * segments.riseOutside * segments.riseOutside;
            const float bandEntrySpeed = std::min(shape.riseSpeed, shape.bandSpeed);
            t -= segments.riseOutside;

            const float riseBandGravity = shape.riseGravity * shape.bandScale;
            if (t <= segments.riseBand)
            {
                height += bandEntrySpeed * t - 0.5f * riseBandGravity * t * t;
                return NS::Vector3{horizontal.x, height, horizontal.z};
            }
            height +=
                bandEntrySpeed * segments.riseBand - 0.5f * riseBandGravity * segments.riseBand * segments.riseBand;
            t -= segments.riseBand;

            const float fallBandGravity = shape.fallGravity * shape.bandScale;
            if (t <= segments.fallBand)
            {
                height -= 0.5f * fallBandGravity * t * t;
                return NS::Vector3{horizontal.x, height, horizontal.z};
            }
            height -= 0.5f * fallBandGravity * segments.fallBand * segments.fallBand;
            t -= segments.fallBand;

            height -= shape.bandSpeed * t + 0.5f * shape.fallGravity * t * t;
            return NS::Vector3{horizontal.x, height, horizontal.z};
        }
    } // namespace

    NS::Vector3 LaunchArcInitialVelocity(const LaunchArc& arc) noexcept
    {
        ArcShape shape;
        if (!TryShapeOf(arc, shape))
        {
            return NS::Vector3{0.0f, 0.0f, 0.0f};
        }
        const NS::Vector3 horizontal = shape.forward * shape.horizontalSpeed;
        return NS::Vector3{horizontal.x, shape.riseSpeed, horizontal.z};
    }

    NS::Vector3 LaunchArcOffsetAt(const LaunchArc& arc, float seconds) noexcept
    {
        ArcShape shape;
        if (!std::isfinite(seconds) || seconds < 0.0f || !TryShapeOf(arc, shape))
        {
            return NS::Vector3{};
        }
        return ArcOffsetAt(shape, seconds);
    }

    float LaunchArcFlightSeconds(const LaunchArc& arc) noexcept
    {
        ArcShape shape;
        if (!TryShapeOf(arc, shape))
        {
            return 0.0f;
        }
        return arc.distance / shape.horizontalSpeed;
    }

} // namespace NS::Game::Level
