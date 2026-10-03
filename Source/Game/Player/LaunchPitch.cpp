#include "Game/Player/LaunchPitch.h"

#include "Runtime/Core/Math.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Player
{
    namespace
    {
        // 着きたい高さとの差をここまで詰める (m)。1 mm は面の上の位置で 0.001 未満で、段の境目を動かさない
        constexpr float k_HeightTolerance = 0.001f;
        // 二分探索の回数の上限。上限 40 度・20 m/s の幅 (約 34 m/s) は 32 回で 1e-8 m/s まで縮む。浮動小数の桁が
        // 尽きて差が縮まない時に止める
        constexpr int k_SearchSteps = 32;
        // 道筋を進めるフレーム数の上限。突進距離 10 m を突進速度 20 m/s で進むのは 30 フレーム。極端に遅い水平の速さで
        // 回り続けるのを止める
        constexpr int k_MaxFrames = 600;
        // 放つ角度の上限の頭打ち (度)。90 度では水平の速さに対する縦の速さが発散する
        constexpr float k_MaxAngleDegrees = 89.0f;

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
        for (int frame = 0; frame < k_MaxFrames; ++frame)
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
            !IsFinitePositive(desc.dt))
        {
            return level;
        }
        const float angle = std::clamp(desc.maxAngleDegrees, 0.0f, k_MaxAngleDegrees);
        const float limit = desc.horizontalSpeed * std::tan(NS::Core::ToRadians(NS::Core::Degrees{angle}).value);
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
                        .grounded = desc.grounded};
        const float rise = desc.targetHeight - desc.ballHeight;
        const auto heightFor = [&](float vertical) {
            path.verticalSpeed = vertical;
            return LaunchHeightAt(path, desc.contactDistance);
        };

        // 水平で着くなら水平のまま。触れる所が今の位置 (距離 0) の時もここで決まり、探索の真ん中の角度で出ない
        if (std::abs(heightFor(0.0f) - rise) <= k_HeightTolerance)
        {
            return LaunchPitchResult{.verticalSpeed = 0.0f, .reachable = true};
        }
        // 上限の角度の中で着かない
        if (rise < heightFor(low) - k_HeightTolerance || rise > heightFor(high) + k_HeightTolerance)
        {
            return level;
        }

        // 式で解かないのは、重力が上りと下りで違い、頂点付近で弱まるため。縦の速さが大きいほど触れる所で高い
        for (int i = 0; i < k_SearchSteps; ++i)
        {
            const float middle = (low + high) * 0.5f;
            const float height = heightFor(middle);
            if (std::abs(height - rise) <= k_HeightTolerance)
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
