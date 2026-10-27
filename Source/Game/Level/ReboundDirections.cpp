#include "Game/Level/ReboundDirections.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

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

        NS::Vector3 ShiftReboundDirection(std::span<const NS::Vector3> table,
                                          const NS::Vector3& assigned,
                                          const NS::Vector3& normal,
                                          const NS::Vector3& shift,
                                          float stepRadians) noexcept
        {
            NS::Vector3 perpendicular{shift - assigned * NS::Dot(shift, assigned)};
            if (NS::IsNearlyZero(perpendicular))
            {
                return assigned;
            }
            perpendicular.Normalize();

            const NS::Vector3* bestNeighbor{nullptr};
            float bestDot{0.0f};
            for (const NS::Vector3& direction : table)
            {
                const float angle{NS::AngleBetween(direction, assigned)};
                if (NS::IsNearlyZero(angle) || angle > stepRadians + NS::k_Epsilon)
                {
                    continue;
                }
                const float dot{NS::Dot(direction, perpendicular)};
                if (dot > bestDot)
                {
                    bestDot = dot;
                    bestNeighbor = &direction;
                }
            }
            if (bestNeighbor == nullptr || !NS::HasPositiveMagnitude(NS::Dot(*bestNeighbor, normal)))
            {
                return assigned;
            }
            return *bestNeighbor;
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
            if (NS::AngleBetween(from, to) <= maxRadians)
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
