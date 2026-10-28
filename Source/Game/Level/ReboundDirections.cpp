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

            // 画面の上で、direction が入力から離れた角度。画面へ映した長さが無い向き (奥か手前だけへ向く物) は角度が決まらず空
            // 長さが非数の時も空にする。比べた結果が全部偽になり、合ったかの判定を黙って通すため
            std::optional<float> ScreenAngle(const NS::Vector3& direction,
                                             const ScreenAxes& screen,
                                             const ScreenInput& input) noexcept
            {
                const float onRight{NS::Dot(direction, screen.right)};
                const float onUp{NS::Dot(direction, screen.up)};
                const float onLength{std::sqrt(onRight * onRight + onUp * onUp)};
                if (!NS::HasPositiveMagnitude(onLength))
                {
                    return std::nullopt;
                }
                const float cosine{std::clamp((onRight * input.right + onUp * input.up) / onLength, -1.0f, 1.0f)};
                return std::acos(cosine);
            }

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
                    std::abs(NS::Dot(direction, screen.flatForward)) > depthLimit)
                {
                    return std::nullopt;
                }
                return ScreenAngle(direction, screen, input);
            }

            // 割り当てた向きから 1 段以内の候補なら、画面の上で入力から離れた角度を返す。候補でなければ空
            std::optional<float> StepScreenAngle(const NS::Vector3& direction,
                                                 const ReboundHit& hit,
                                                 const ScreenAxes& screen,
                                                 const ScreenInput& input,
                                                 const ReboundChoiceLimits& limits) noexcept
            {
                if (NS::AngleBetweenUnit(direction, hit.assigned) > limits.stepRadians + NS::k_Epsilon)
                {
                    return std::nullopt;
                }
                return CandidateScreenAngle(direction, hit, screen, input, limits);
            }

            // 入力の側へ 1 段で曲げる向き。割り当てた向きより、画面で同じに見える差を超えて入力に近い物だけを曲げる先にする。
            // 曲げる先が無いか、左右に同じだけ近い 2 つが並んで 1 つに決まらなければ nullptr
            const NS::Vector3* OneStepTowardInput(std::span<const NS::Vector3> table,
                                                  const ReboundHit& hit,
                                                  const ScreenAxes& screen,
                                                  const ScreenInput& input,
                                                  const ReboundChoiceLimits& limits) noexcept
            {
                // 画面に映らない割り当ては、映る候補ならどれでも入力に近い
                const std::optional<float> assignedAngle{ScreenAngle(hit.assigned, screen, input)};
                float ceiling{NS::k_Pi + NS::k_Epsilon};
                if (assignedAngle.has_value())
                {
                    ceiling = *assignedAngle - limits.screenTieRadians;
                }
                std::optional<float> bestAngle{};
                for (const NS::Vector3& direction : table)
                {
                    const std::optional<float> angle{StepScreenAngle(direction, hit, screen, input, limits)};
                    if (angle.has_value() && *angle < ceiling && (!bestAngle.has_value() || *angle < *bestAngle))
                    {
                        bestAngle = angle;
                    }
                }
                if (!bestAngle.has_value())
                {
                    return nullptr;
                }

                // 画面で同じに見える候補の中から、カメラの見る向きへ一番動かない物を選ぶ。同じなら画面で入力に近い物
                const NS::Vector3* chosen{nullptr};
                float chosenDepth{0.0f};
                float chosenAngle{0.0f};
                bool tied{false};
                for (const NS::Vector3& direction : table)
                {
                    const std::optional<float> angle{StepScreenAngle(direction, hit, screen, input, limits)};
                    if (!angle.has_value() || *angle >= ceiling || *angle > *bestAngle + limits.screenTieRadians)
                    {
                        continue;
                    }
                    const float depth{std::abs(NS::Dot(direction, screen.forward))};
                    const bool sameDepth{std::abs(depth - chosenDepth) <= NS::k_Epsilon};
                    const bool sameAngle{std::abs(*angle - chosenAngle) <= NS::k_Epsilon};
                    if (chosen != nullptr && sameDepth && sameAngle)
                    {
                        tied = true;
                        continue;
                    }
                    if (chosen == nullptr || depth < chosenDepth - NS::k_Epsilon || (sameDepth && *angle < chosenAngle))
                    {
                        chosen = &direction;
                        chosenDepth = depth;
                        chosenAngle = *angle;
                        tied = false;
                    }
                }
                // 浮動小数の最後の桁で左右の片方を選ぶと、45° の坂で前の入力が左へ曲がった
                if (tied)
                {
                    return nullptr;
                }
                return chosen;
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

            // 当たり方で決まった向きは、候補を絞る前に別に比べる。奥と手前の上限と、同じに見える向きの選び方に
            // かけると、合った入力でも別の向きへ飛び、強くもならない (D-08)
            const std::optional<float> assignedAngle{ScreenAngle(hit.assigned, screen, input)};
            // 合ったかは 1 段に絞る前の全部の候補で見る。絞った後で比べると、入力の側に候補が無いだけで合った事になる
            const bool matched{assignedAngle.has_value() && *assignedAngle <= *bestAngle + limits.screenTieRadians};
            if (matched)
            {
                return ReboundChoice{.direction = hit.assigned, .result = ReboundInputResult::Matched};
            }

            const NS::Vector3* shifted{OneStepTowardInput(table, hit, screen, input, limits)};
            if (shifted == nullptr)
            {
                return kept;
            }
            return ReboundChoice{.direction = *shifted, .result = ReboundInputResult::Shifted};
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
