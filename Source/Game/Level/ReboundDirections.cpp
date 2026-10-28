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

            // direction を画面の右と上へ映した長さ
            float ScreenLength(const NS::Vector3& direction, const ScreenAxes& screen) noexcept
            {
                const float onRight{NS::Dot(direction, screen.right)};
                const float onUp{NS::Dot(direction, screen.up)};
                return std::sqrt(onRight * onRight + onUp * onUp);
            }

            // 画面の上で、direction が入力から離れた角度。画面へ映した長さが無い向き (奥か手前だけへ向く物) は角度が決まらず空
            // 長さが非数の時も空にする。比べた結果が全部偽になり、合ったかの判定を黙って通すため
            std::optional<float> ScreenAngle(const NS::Vector3& direction,
                                             const ScreenAxes& screen,
                                             const ScreenInput& input) noexcept
            {
                const float onRight{NS::Dot(direction, screen.right)};
                const float onUp{NS::Dot(direction, screen.up)};
                const float onLength{ScreenLength(direction, screen)};
                if (!NS::HasPositiveMagnitude(onLength))
                {
                    return std::nullopt;
                }
                const float cosine{std::clamp((onRight * input.right + onUp * input.up) / onLength, -1.0f, 1.0f)};
                return std::acos(cosine);
            }

            // 奥と手前へ飛ぶ量は、当たる前に進んでいた量まで。壁や床の傾きが足した奥と手前の動きは使わない。
            // カメラも当たる前の向きも軸にそろう事はほぼ無いので、わずかなずれの分は余裕で通す
            bool IsCandidate(const NS::Vector3& direction,
                             const ReboundHit& hit,
                             const ScreenAxes& screen,
                             const ReboundChoiceLimits& limits) noexcept
            {
                const float depthLimit{std::abs(NS::Dot(hit.incoming, screen.flatForward)) +
                                       std::sin(limits.depthSlackRadians) + NS::k_Epsilon};
                return NS::HasPositiveMagnitude(NS::Dot(direction, hit.normal)) &&
                       std::abs(NS::Dot(direction, screen.flatForward)) <= depthLimit;
            }

            // direction が入力補正の候補なら、画面の上で入力から離れた角度を返す。候補でなければ空
            std::optional<float> CandidateScreenAngle(const NS::Vector3& direction,
                                                      const ReboundHit& hit,
                                                      const ScreenAxes& screen,
                                                      const ScreenInput& input,
                                                      const ReboundChoiceLimits& limits) noexcept
            {
                if (!IsCandidate(direction, hit, screen, limits))
                {
                    return std::nullopt;
                }
                return ScreenAngle(direction, screen, input);
            }

            // 表の向きの、傾きと水平の目盛りの番号。真上と真下は水平の番号を持たない
            struct TableCell
            {
                int pitch;
                std::optional<int> yaw;
            };

            TableCell CellOf(const NS::Vector3& direction, float stepRadians) noexcept
            {
                const int turn{static_cast<int>(std::lround(2.0f * NS::k_Pi / stepRadians))};
                const int pole{turn / 4};
                const int pitch{static_cast<int>(std::lround(std::asin(std::clamp(direction.y, -1.0f, 1.0f)) / stepRadians))};
                if (std::abs(pitch) >= pole)
                {
                    return TableCell{.pitch = pitch, .yaw = std::nullopt};
                }
                const int yaw{static_cast<int>(std::lround(std::atan2(direction.x, direction.z) / stepRadians))};
                return TableCell{.pitch = pitch, .yaw = ((yaw % turn) + turn) % turn};
            }

            // 2 つの表の向きが、傾きと水平の番号をそれぞれ 1 つまで動かした隣か。真上と真下の隣は、1 つ下か上の傾きの全部の向き。
            // 角度で決めると、傾いた向きでは水平の目盛りが狭まり、2 目盛り先まで入る
            bool IsNeighbor(const NS::Vector3& from, const NS::Vector3& to, float stepRadians) noexcept
            {
                const TableCell a{CellOf(from, stepRadians)};
                const TableCell b{CellOf(to, stepRadians)};
                if (std::abs(a.pitch - b.pitch) > 1)
                {
                    return false;
                }
                if (!a.yaw.has_value() || !b.yaw.has_value())
                {
                    return a.pitch != b.pitch;
                }
                const int turn{static_cast<int>(std::lround(2.0f * NS::k_Pi / stepRadians))};
                const int apart{std::abs(*a.yaw - *b.yaw)};
                const int yawSteps{std::min(apart, turn - apart)};
                return yawSteps <= 1 && (yawSteps != 0 || a.pitch != b.pitch);
            }

            // スティックの入力を、カメラの右と上で世界の向きにする。面へ向かう分は、面に沿って上へ向かう向きに置き換え、
            // 壁へ押し込むと上へ抜ける。床と天井は面に沿う上が無いので、面へ向かう分を捨てる
            NS::Vector3 InputInWorld(const ReboundHit& hit, const ScreenAxes& screen, const ScreenInput& input) noexcept
            {
                const NS::Vector3 onScreen{screen.right * input.right + screen.up * input.up};
                const float into{NS::Dot(onScreen, hit.normal)};
                if (into >= 0.0f)
                {
                    return onScreen;
                }
                const NS::Vector3 alongSurface{onScreen - hit.normal * into};
                NS::Vector3 surfaceUp{NS::RejectUnit(NS::Vector3{0.0f, 1.0f, 0.0f}, hit.normal)};
                if (NS::IsNearlyZero(surfaceUp))
                {
                    return alongSurface;
                }
                surfaceUp.Normalize();
                return alongSurface - surfaceUp * into;
            }

            // 割り当てた向きの隣の候補で、入力を世界の向きにした物との角度が、割り当てた向きより小さい物なら、その角度を返す。
            // 曲げる先でなければ空
            std::optional<float> StepAngle(const NS::Vector3& direction,
                                           const NS::Vector3& wanted,
                                           float assignedAngle,
                                           const ReboundHit& hit,
                                           const ScreenAxes& screen,
                                           const ReboundChoiceLimits& limits) noexcept
            {
                if (!IsNeighbor(hit.assigned, direction, limits.stepRadians) ||
                    !IsCandidate(direction, hit, screen, limits))
                {
                    return std::nullopt;
                }
                const float angle{NS::AngleBetweenUnit(direction, wanted)};
                if (angle >= assignedAngle - NS::k_Epsilon)
                {
                    return std::nullopt;
                }
                return angle;
            }

            // 割り当てた向きの隣の候補から、入力を世界の向きにした物に一番近い物を選ぶ。一番近い物から同じに見なす差の中の
            // 物は同じだけ近いとし、その中で割り当てた向きから離れる角度が一番小さい物にする。差が少しでも近い方を選ぶと、
            // 真後ろから 3° 回ったカメラで前を入力した時、真上の隣と斜め上の隣のわずかな差で左右へ曲がった。
            // 曲げる先が無いか、離れる角度も同じ 2 つが残って 1 つに決まらなければ nullptr
            const NS::Vector3* OneStepTowardInput(std::span<const NS::Vector3> table,
                                                  const ReboundHit& hit,
                                                  const ScreenAxes& screen,
                                                  const ScreenInput& input,
                                                  const ReboundChoiceLimits& limits) noexcept
            {
                NS::Vector3 wanted{InputInWorld(hit, screen, input)};
                if (NS::IsNearlyZero(wanted))
                {
                    return nullptr;
                }
                wanted.Normalize();
                const float assignedAngle{NS::AngleBetweenUnit(hit.assigned, wanted)};

                std::optional<float> bestAngle{};
                for (const NS::Vector3& direction : table)
                {
                    const std::optional<float> angle{StepAngle(direction, wanted, assignedAngle, hit, screen, limits)};
                    if (angle.has_value() && (!bestAngle.has_value() || *angle < *bestAngle))
                    {
                        bestAngle = angle;
                    }
                }
                if (!bestAngle.has_value())
                {
                    return nullptr;
                }

                const NS::Vector3* chosen{nullptr};
                float chosenTurn{0.0f};
                bool tied{false};
                for (const NS::Vector3& direction : table)
                {
                    const std::optional<float> angle{StepAngle(direction, wanted, assignedAngle, hit, screen, limits)};
                    if (!angle.has_value() || *angle > *bestAngle + limits.screenTieRadians)
                    {
                        continue;
                    }
                    const float turn{NS::AngleBetweenUnit(direction, hit.assigned)};
                    if (chosen != nullptr && std::abs(turn - chosenTurn) <= NS::k_Epsilon)
                    {
                        tied = true;
                        continue;
                    }
                    if (chosen == nullptr || turn < chosenTurn)
                    {
                        chosen = &direction;
                        chosenTurn = turn;
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
            // 合ったかは 1 段に絞る前の全部の候補で見る。絞った後で比べると、入力の側に候補が無いだけで合った事になる。
            // 画面にほとんど映らない向き (後ろから見て真後ろへ返る時など) は、画面へ映した向きが入力と合っても合ったとしない。
            // 合ったとすると、入力しても画面では動きが変わらない
            const bool matched{assignedAngle.has_value() &&
                               ScreenLength(hit.assigned, screen) >= limits.matchVisibleLength &&
                               *assignedAngle <= *bestAngle + limits.screenTieRadians};
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
