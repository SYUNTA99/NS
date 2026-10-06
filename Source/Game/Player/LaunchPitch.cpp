#include "Game/Player/LaunchPitch.h"

#include "NSlib/Core/Math.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Player
{
    namespace
    {
        [[nodiscard]] bool IsFinitePositive(float value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }
    } // namespace

    float LaunchHeightAt(const LaunchPath& path, float distance) noexcept
    {
        if (!IsFinitePositive(path.horizontalSpeed) || !IsFinitePositive(path.dt) || !std::isfinite(distance) ||
            distance < 0.0f || !std::isfinite(path.verticalSpeed))
        {
            return 0.0f;
        }
        const float step = path.horizontalSpeed * path.dt;
        float travelled = 0.0f;
        float height = 0.0f;
        float vertical = path.verticalSpeed;
        for (int frame = 0; frame < path.maxFrames; ++frame)
        {
            // 速さへ重力を足してから位置を進める。Player::UpdateBodySlam の Gravity の後に
            // 身体の段の MoveBody が動かす順。足し方は NS::Obj::AddGravity の速度 += 重力の向き × 強さ × dt を、
            // 向き -Y で縦だけにした物
            vertical += ChooseGravity(path.gravity, vertical) * path.dt;
            float next = height + vertical * path.dt;
            // 接地して放った玉は床で止まり、床の上を水平に進む
            if (path.grounded && next < 0.0f)
            {
                next = 0.0f;
                vertical = 0.0f;
            }
            if (travelled + step >= distance)
            {
                const float t = (distance - travelled) / step;
                const float result = height + (next - height) * t;
                if (!std::isfinite(result))
                {
                    return 0.0f;
                }
                return result;
            }
            travelled += step;
            height = next;
        }
        if (!std::isfinite(height))
        {
            return 0.0f;
        }
        return height;
    }

    LaunchPitchResult LaunchPitch(const LaunchPitchDesc& desc) noexcept
    {
        const LaunchPitchResult level{};
        if (!std::isfinite(desc.ballHeight) || !std::isfinite(desc.targetHeight) ||
            !std::isfinite(desc.contactDistance) || desc.contactDistance < 0.0f ||
            !std::isfinite(desc.maxAngleDegrees) || !IsFinitePositive(desc.horizontalSpeed) ||
            !IsFinitePositive(desc.dt) || !IsFinitePositive(desc.heightTolerance) ||
            !IsFinitePositive(desc.angleGuardDegrees) || desc.angleGuardDegrees >= 90.0f || desc.maxFrames <= 0)
        {
            return level;
        }
        const float angle = std::clamp(desc.maxAngleDegrees, 0.0f, desc.angleGuardDegrees);
        const float limit = desc.horizontalSpeed * std::tan(NS::ToRadians(NS::Degrees{angle}).value);
        // 地上は床があるので下へ向けない
        float low = -limit;
        if (desc.grounded)
        {
            low = 0.0f;
        }
        float high = limit;

        LaunchPath path{.horizontalSpeed = desc.horizontalSpeed,
                        .verticalSpeed = 0.0f,
                        .gravity = desc.gravity,
                        .dt = desc.dt,
                        .grounded = desc.grounded, .maxFrames = desc.maxFrames};
        const float rise = desc.targetHeight - desc.ballHeight;
        const auto heightFor = [&](float vertical) {
            path.verticalSpeed = vertical;
            return LaunchHeightAt(path, desc.contactDistance);
        };

        // 水平で着くなら水平のまま。触れる所が今の位置 (距離 0) の時もここで決まり、探索の真ん中の角度で出ない
        if (std::abs(heightFor(0.0f) - rise) <= desc.heightTolerance)
        {
            return LaunchPitchResult{.verticalSpeed = 0.0f, .reachable = true};
        }
        // 上限の角度の中で着かない
        if (rise < heightFor(low) - desc.heightTolerance || rise > heightFor(high) + desc.heightTolerance)
        {
            return level;
        }

        // 式で解かないのは、重力が上りと下りで違い、頂点付近で弱まるため。縦の速さが大きいほど触れる所で高い
        while (low < high)
        {
            const float middle = (low + high) * 0.5f;
            if (middle == low || middle == high)
            {
                break;
            }
            const float height = heightFor(middle);
            if (std::abs(height - rise) <= desc.heightTolerance)
            {
                return LaunchPitchResult{.verticalSpeed = middle, .reachable = true};
            }
            if (height < rise)
            {
                low = middle;
            }
            else
            {
                high = middle;
            }
        }
        return LaunchPitchResult{.verticalSpeed = (low + high) * 0.5f, .reachable = true};
    }
} // namespace NS::Game::Player
