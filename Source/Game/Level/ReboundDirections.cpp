#include "Game/Level/ReboundDirections.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace GL
{
    namespace Level
    {
        namespace
        {
            // 資料 v0.2 の 6.1 が決める分割の数
            int SnapDivisions(int divisions) noexcept
            {
                const std::array<int, 3> supported{8, 12, 16};
                int best{supported[0]};
                for (const int candidate : supported)
                {
                    if (std::abs(candidate - divisions) < std::abs(best - divisions))
                    {
                        best = candidate;
                    }
                }
                return best;
            }

            NS::Vector3 FromYawPitch(float yaw, float pitch) noexcept
            {
                const float horizontal{std::cos(pitch)};
                return NS::Vector3{horizontal * std::sin(yaw), std::sin(pitch), horizontal * std::cos(yaw)};
            }

            // 3 枚の円は軸の向きを共有するので、同じ向きは 1 つだけ入れる
            void AddIfNew(std::vector<NS::Vector3>& table, const NS::Vector3& direction)
            {
                for (const NS::Vector3& existing : table)
                {
                    if (NS::IsNearlyParallel(existing, direction))
                    {
                        return;
                    }
                }
                table.push_back(direction);
            }

            // スティックの入力を長さ 1 にした、画面の右と上の成分
            struct ScreenInput
            {
                float right;
                float up;
            };

            // direction が入力補正の候補なら、画面の上で入力から離れた角度を返す。候補でなければ空
            std::optional<float> CandidateScreenAngle(const NS::Vector3& direction,
                                                      const ReboundHit& hit,
                                                      const ScreenAxes& screen,
                                                      const ScreenInput& input,
                                                      const ReboundChoiceLimits& limits) noexcept
            {
                // 奥と手前へ飛ぶ量は、当たる前に進んでいた量まで。壁や床の傾きが足した奥と手前の動きは使わない。
                // カメラも当たる前の向きも軸にそろう事はほぼ無いので、わずかなずれの分は余裕で通す
                const float depthLimit{std::abs(NS::Dot(hit.incoming, screen.flatForward)) +
                                       std::sin(limits.depthSlackRadians) + NS::k_Epsilon};
                if (!NS::HasPositiveMagnitude(NS::Dot(direction, hit.normal)) ||
                    NS::AngleBetweenUnit(direction, hit.assigned) > limits.maxRadians + NS::k_Epsilon ||
                    std::abs(NS::Dot(direction, screen.flatForward)) > depthLimit)
                {
                    return std::nullopt;
                }
                // 画面へ映した長さが無い向き (奥か手前だけへ向く物) は、画面の上の角度が決まらない
                const float onRight{NS::Dot(direction, screen.right)};
                const float onUp{NS::Dot(direction, screen.up)};
                const float onLength{std::sqrt(onRight * onRight + onUp * onUp)};
                if (NS::IsNearlyZero(onLength))
                {
                    return std::nullopt;
                }
                const float cosine{std::clamp((onRight * input.right + onUp * input.up) / onLength, -1.0f, 1.0f)};
                return std::acos(cosine);
            }

            // 後ろから見るカメラは少し見下ろすので、奥・斜め上・真上が全部画面の上に見え、奥を入力する向きが無い。
            // 後ろから見ていて真上へ返る時は、候補のうち 1 段以内で、カメラの水平の前へ一番進む向きを返す。無ければ nullptr
            const NS::Vector3* AheadOfStraightUp(std::span<const NS::Vector3> table,
                                                 const ReboundHit& hit,
                                                 const ScreenAxes& screen,
                                                 const ScreenInput& input,
                                                 const ReboundChoiceLimits& limits) noexcept
            {
                if (!NS::IsNearlyParallel(hit.assigned, NS::Vector3{0.0f, 1.0f, 0.0f}) ||
                    NS::IsNearlyZeroHorizontal(hit.incoming))
                {
                    return nullptr;
                }
                const NS::Vector3 flatIncoming{
                    NS::NormalizedOr(NS::Vector3{hit.incoming.x, 0.0f, hit.incoming.z}, screen.flatForward)};
                if (NS::AngleBetweenUnit(flatIncoming, screen.flatForward) > limits.behindViewRadians)
                {
                    return nullptr;
                }
                const NS::Vector3* ahead{nullptr};
                float aheadDot{0.0f};
                for (const NS::Vector3& direction : table)
                {
                    const float dot{NS::Dot(direction, screen.flatForward)};
                    if (dot > aheadDot &&
                        NS::AngleBetweenUnit(direction, hit.assigned) <= limits.stepRadians + NS::k_Epsilon &&
                        CandidateScreenAngle(direction, hit, screen, input, limits).has_value())
                    {
                        aheadDot = dot;
                        ahead = &direction;
                    }
                }
                return ahead;
            }
        } // namespace

        float DirectionStepRadians(int divisions) noexcept
        {
            return 2.0f * NS::k_Pi / static_cast<float>(SnapDivisions(divisions));
        }

        std::vector<NS::Vector3> MakeReboundDirections(int divisions, ReboundTableShape shape)
        {
            const int count{SnapDivisions(divisions)};
            const float step{DirectionStepRadians(count)};
            std::vector<NS::Vector3> table{};
            switch (shape)
            {
            case ReboundTableShape::ThreePlanes:
                for (int i = 0; i < count; ++i)
                {
                    const float angle{step * static_cast<float>(i)};
                    const float c{std::cos(angle)};
                    const float s{std::sin(angle)};
                    AddIfNew(table, NS::Vector3{s, 0.0f, c});
                    AddIfNew(table, NS::Vector3{s, c, 0.0f});
                    AddIfNew(table, NS::Vector3{0.0f, s, c});
                }
                break;
            case ReboundTableShape::YawPitch:
            {
                // 傾きは真上と真下を除いた ±step の倍数。真上と真下は水平の向きによらず 1 つずつ
                const int pitchSteps{count / 4 - 1};
                for (int p = -pitchSteps; p <= pitchSteps; ++p)
                {
                    const float pitch{step * static_cast<float>(p)};
                    for (int i = 0; i < count; ++i)
                    {
                        table.push_back(FromYawPitch(step * static_cast<float>(i), pitch));
                    }
                }
                table.push_back(NS::Vector3{0.0f, 1.0f, 0.0f});
                table.push_back(NS::Vector3{0.0f, -1.0f, 0.0f});
                break;
            }
            }
            return table;
        }

        NS::Vector3 ReflectAcross(const NS::Vector3& velocity, const NS::Vector3& normal) noexcept
        {
            return velocity - normal * (2.0f * NS::Dot(velocity, normal));
        }

        bool AssignReboundDirection(std::span<const NS::Vector3> table,
                                    const NS::Vector3& incoming,
                                    const NS::Vector3& normal,
                                    NS::Vector3& outDirection) noexcept
        {
            NS::Vector3 reflected{ReflectAcross(incoming, normal)};
            reflected.Normalize();
            bool found{false};
            float bestDot{-2.0f};
            for (const NS::Vector3& direction : table)
            {
                // 面に沿う向きと面へ向かう向きは返る向きにならない
                if (!NS::HasPositiveMagnitude(NS::Dot(direction, normal)))
                {
                    continue;
                }
                const float dot{NS::Dot(direction, reflected)};
                if (dot > bestDot)
                {
                    bestDot = dot;
                    outDirection = direction;
                    found = true;
                }
            }
            return found;
        }

        ReboundChoice ChooseReboundDirection(std::span<const NS::Vector3> table,
                                             const ReboundHit& hit,
                                             const ScreenAxes& screen,
                                             float stickRight,
                                             float stickUp,
                                             const ReboundChoiceLimits& limits) noexcept
        {
            const ReboundChoice kept{.direction = hit.assigned, .result = ReboundInputResult::Kept};
            const float stickLength{std::sqrt(stickRight * stickRight + stickUp * stickUp)};
            if (NS::IsNearlyZero(stickLength))
            {
                return kept;
            }
            const ScreenInput input{.right = stickRight / stickLength, .up = stickUp / stickLength};

            std::optional<float> bestAngle{};
            for (const NS::Vector3& direction : table)
            {
                const std::optional<float> angle{CandidateScreenAngle(direction, hit, screen, input, limits)};
                if (angle.has_value() && (!bestAngle.has_value() || *angle < *bestAngle))
                {
                    bestAngle = angle;
                }
            }
            if (!bestAngle.has_value())
            {
                return kept;
            }

            // 画面で同じに見える候補の中から、カメラの見る向きへ一番動かない物を選ぶ。同じなら画面で入力に近い物
            const NS::Vector3* chosen{nullptr};
            float chosenDepth{0.0f};
            float chosenAngle{0.0f};
            for (const NS::Vector3& direction : table)
            {
                const std::optional<float> angle{CandidateScreenAngle(direction, hit, screen, input, limits)};
                if (!angle.has_value() || *angle > *bestAngle + limits.screenTieRadians)
                {
                    continue;
                }
                const float depth{std::abs(NS::Dot(direction, screen.forward))};
                const bool lessDepth{depth < chosenDepth - NS::k_Epsilon};
                const bool sameDepthCloser{std::abs(depth - chosenDepth) <= NS::k_Epsilon && *angle < chosenAngle};
                if (chosen == nullptr || lessDepth || sameDepthCloser)
                {
                    chosen = &direction;
                    chosenDepth = depth;
                    chosenAngle = *angle;
                }
            }

            if (!NS::IsNearlyParallel(*chosen, hit.assigned))
            {
                return ReboundChoice{.direction = *chosen, .result = ReboundInputResult::Shifted};
            }
            if (const NS::Vector3* ahead = AheadOfStraightUp(table, hit, screen, input, limits))
            {
                return ReboundChoice{.direction = *ahead, .result = ReboundInputResult::Shifted};
            }
            return ReboundChoice{.direction = hit.assigned, .result = ReboundInputResult::Matched};
        }

        NS::Vector3 ShiftInputOnScreen(const NS::Vector3& assigned,
                                       const NS::Vector3& cameraRight,
                                       const NS::Vector3& cameraUp,
                                       const NS::Vector3& cameraForward,
                                       float stickRight,
                                       float stickUp) noexcept
        {
            const NS::Vector3 right{NS::RejectUnit(cameraRight, assigned)};
            const NS::Vector3 up{NS::RejectUnit(cameraUp, assigned)};
            const NS::Vector3 depth{NS::RejectUnit(cameraForward, assigned)};
            const float upLength{std::min(up.Length(), 1.0f)};
            return right * stickRight + (up + depth * (1.0f - upLength)) * stickUp;
        }

        NS::Vector3 TurnToward(const NS::Vector3& from, const NS::Vector3& to, float maxRadians) noexcept
        {
            if (NS::AngleBetweenUnit(from, to) <= maxRadians)
            {
                return to;
            }
            NS::Vector3 side{NS::RejectUnit(to, from)};
            if (NS::IsNearlyZero(side))
            {
                return from;
            }
            side.Normalize();
            return from * std::cos(maxRadians) + side * std::sin(maxRadians);
        }
    } // namespace Level
} // namespace GL
