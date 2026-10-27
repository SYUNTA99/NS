#include "Game/Level/ImpactOutcome.h"
#include "NSlib/Core/Types.h"

#include <algorithm>
#include <cmath>

namespace GL::Level
{
        namespace
        {
            // 貫通の止めの上限秒をフレーム数へ換算する。非有限と 0 以下は 0 で、止めない
            [[nodiscard]] int MaxHitStopSteps(const ImpactTuning& tuning) noexcept
            {
                const float raw = tuning.hitStopMaxSeconds / tuning.fixedDelta;
                if (!std::isfinite(raw) || raw <= 0.0f)
                {
                    return 0;
                }
                return static_cast<int>(std::lround(raw));
            }

            // 秒をフレーム数へ換算して 0 から MaxHitStopSteps までに丸める
            [[nodiscard]] int SecondsToSteps(const ImpactTuning& tuning, float seconds) noexcept
            {
                const float raw = seconds / tuning.fixedDelta;
                if (!std::isfinite(raw))
                {
                    return 0;
                }
                return NS::Clamp(static_cast<int>(std::lround(raw)), 0, MaxHitStopSteps(tuning));
            }

            struct TierScales
            {
                float hitStop = 1.0f;
                float reboundDistance = 1.0f; // 反動の距離の倍率。外れの高さは外れの節で下げる
                float reboundHeight = 1.0f;
            };

            // 段ごとに 1 行の表。段を足したら行を足す。真偽で分けると段が 2 つと決め打ちになる
            [[nodiscard]] TierScales TierScalesFor(HitTier tier, const ImpactTuning& tuning) noexcept
            {
                switch (tier)
                {
                case HitTier::Center:
                    return TierScales{.hitStop = tuning.centerHitStopScale,
                                      .reboundDistance = tuning.centerHitReboundDistanceScale,
                                      .reboundHeight = tuning.centerHitReboundHeightScale};
                case HitTier::Wide:
                    return TierScales{.reboundDistance = tuning.missReboundDistanceScale};
                }
                // 番号から作った段の外の値は倍率を掛けない
                return TierScales{};
            }
        } // namespace

        NS::Vector3 MissSurfaceNormal(float u, float v, float sharpness, const NS::Vector3& slamDirection) noexcept
        {
            NS::Vector3 forward{};
            if (!NS::TryNormalizeHorizontal(slamDirection, forward))
            {
                return NS::Vector3{};
            }
            float p = sharpness;
            if (!std::isfinite(p) || p < 2.0f)
            {
                p = 2.0f;
            }
            // 面の右は JudgeHitFace と同じ。左手系で上から見て、進む向きの右
            const NS::Vector3 right{forward.z, 0.0f, -forward.x};
            const NS::Vector3 up{0.0f, 1.0f, 0.0f};
            const float absU = std::abs(u);
            const float absV = std::abs(v);
            const float edge = std::min(std::pow(std::pow(absU, p) + std::pow(absV, p), 1.0f / p), 1.0f);
            // 超楕円の面の勾配。p = 2 で位置そのもの、p が大きいと大きい方の軸へ寄る
            const float slideU = std::copysign(std::pow(absU, p - 1.0f), u);
            const float slideV = std::copysign(std::pow(absV, p - 1.0f), v);
            const float slideLength = std::sqrt(slideU * slideU + slideV * slideV);
            NS::Vector3 normal = -forward;
            if (std::isfinite(edge) && NS::HasPositiveMagnitude(slideLength))
            {
                const NS::Vector3 slide = (right * slideU + up * slideV) / slideLength;
                normal = slide * edge - forward * std::sqrt(std::max(1.0f - edge * edge, 0.0f));
            }
            normal.Normalize();
            return normal;
        }

        float MissSkidSpeedScale(int elapsedSteps, int totalSteps, float exponent) noexcept
        {
            if (totalSteps <= 0 || elapsedSteps >= totalSteps)
            {
                return 0.0f;
            }
            float c = exponent;
            if (!std::isfinite(c) || !NS::IsPositive(c))
            {
                c = 1.0f;
            }
            const float left = 1.0f - static_cast<float>(std::max(elapsedSteps, 0)) / static_cast<float>(totalSteps);
            return std::pow(left, c);
        }

        ImpactOutcome ComputeImpactOutcome(const ImpactInput& input, const ImpactTuning& tuning) noexcept
        {
            ImpactOutcome outcome{};
            const float power = input.power;
            const float mass = input.mass;
            outcome.massFactor = mass / (mass + 1.0f);

            const TierScales tierScales = TierScalesFor(input.tier, tuning);
            const float hitStopScale = tierScales.hitStop;

            // 面の向きは貫通の当たりにも出す。外れの火花は貫通でも面に沿って流れる
            float sharpness = 2.0f;
            if (input.bodyShape == NS::HitSensorShape::Box)
            {
                sharpness = tuning.missBoxEdgeSharpness;
            }
            outcome.faceNormal = MissSurfaceNormal(input.faceU, input.faceV, sharpness, input.slamVelocity);

            // 壊れない相手は押し飛ばしへ回す。貫通させると残った当たりへ自機がめり込む
            if (tuning.breakEnabled && input.breakable && input.toughness <= power)
            {
                outcome.broke = true;
                // 向きを保ったまま減速する。倍率は相手の質量に依らない
                outcome.breakSelfVelocity = input.slamVelocity * tuning.breakSpeedScale;
                outcome.stopSteps = SecondsToSteps(tuning, tuning.breakStopSeconds * hitStopScale);
                return outcome;
            }

            // 質量 1・威力 1 を基準にし、高さと距離を同率で伸ばして初角度を保つ
            // TODO: 質量 0.5 未満の相手を置く時は、反動の高さと距離に下限を足す
            outcome.reboundScale = power * 2.0f * outcome.massFactor;
            // 中心近くの当たりは距離を伸ばして高さを下げる。真ん中に当てた時は低く速く後ろへ、カメラの方へ戻る
            const float reboundDistance = tuning.reboundDistance * outcome.reboundScale * tierScales.reboundDistance;
            outcome.reboundArc = GL::Player::ReboundArc{
                .direction = NS::Vector3{input.awayDirection.x, 0.0f, input.awayDirection.z},
                .apexHeight = tuning.reboundApexHeight * outcome.reboundScale * tierScales.reboundHeight,
                .distance = reboundDistance};
            NS::Vector3 launchDirection = input.launchDirection;
            // 外れで相手へ押し込む成分。突進の向きと触れた面の向きの内積で、面の真ん中は 1、端ほど 0 へ寄る
            float missPush = 1.0f;

            // 外れは勢いを法線への反発と面に沿う滑りへ分け、配分量は維持する
            // 水平だけ反射する。縦も映すと上の縁を越え、低い反動の弧で相手に再衝突する
            if (input.tier == HitTier::Wide)
            {
                const NS::Vector3 normal = outcome.faceNormal;
                const float slamSpeed = input.slamVelocity.Length();
                if (NS::HasPositiveMagnitude(slamSpeed))
                {
                    missPush = NS::Clamp(-(input.slamVelocity / slamSpeed).Dot(normal), 0.0f, 1.0f);
                }
                NS::Vector3 forward{};
                NS::Vector3 flatNormal{};
                if (NS::TryNormalizeHorizontal(input.slamVelocity, forward))
                {
                    // 真上か真下の縁で水平の成分が無い時は、来た向きへ戻す
                    flatNormal = -forward;
                    NS::Vector3 horizontal{};
                    if (NS::HasPositiveMagnitudeHorizontal(normal) &&
                        NS::TryNormalizeHorizontal(normal, horizontal))
                    {
                        flatNormal = horizontal;
                    }
                    const NS::Vector3 mirrored = forward - flatNormal * (2.0f * forward.Dot(flatNormal));
                    NS::Vector3 deflect{};
                    if (NS::TryNormalizeHorizontal(mirrored, deflect))
                    {
                        outcome.reboundArc.direction = deflect;
                    }
                    launchDirection = -flatNormal;
                }
                // ねじれる軸はかすった所の摩擦から出す。滑る成分 = 来た向き − 押し込む成分、軸 = n × 滑る成分
                // 右の縁では相手の側が引きずられ、相手の方へ巻き込まれる向きに回る
                NS::Vector3 slide{};
                if (NS::TryNormalizeHorizontal(input.slamVelocity, slide))
                {
                    slide = slide - normal * slide.Dot(normal);
                }
                outcome.reboundArc.missTumble =
                    GL::Player::MissTumble{.twist = normal.Cross(slide), .power = input.power};
                // 浮く感じは真ん中だけの物にする。下を向いた面は地面へ叩きつけられ、さらに低く跳ねる
                const float downward = NS::Clamp(-normal.y, 0.0f, 1.0f);
                const float slam = 1.0f - (1.0f - tuning.missSlamBounce) * downward;
                outcome.reboundArc.apexHeight *= tuning.missReboundHeightRatio * slam;
            }

            // 指数の範囲は 0〜1。負にすると重い物ほど飛ぶ逆転になる
            float massExponent = tuning.launchMassExponent;
            if (!std::isfinite(massExponent))
            {
                massExponent = 1.0f;
            }
            massExponent = NS::Clamp(massExponent, 0.0f, 1.0f);

            // 威力は距離に線形に効き、質量で割ると重い物ほど飛ばない。高さは距離と同じ比で伸ばし、打ち上げの角度を揃える
            outcome.launchScale = power / std::pow(mass, massExponent);
            outcome.launchArc = LaunchArc{.direction = launchDirection,
                                          .distance = tuning.launchDistance * outcome.launchScale,
                                          .apexHeight = input.launch.apexHeight * outcome.launchScale,
                                          .riseGravity = input.launch.riseGravity,
                                          .fallGravityScale = input.launch.fallGravityScale,
                                          .apexBandSpeed = input.launch.apexBandSpeed,
                                          .apexBandGravityScale = input.launch.apexBandGravityScale};
            // 外れは押し込む成分に応じて飛ぶ量を減らし、弾き飛ばしに見えないよう距離と高さも抑える
            if (input.tier == HitTier::Wide)
            {
                const float pushSquared = missPush * missPush;
                outcome.launchArc.distance *= pushSquared * input.launch.missDistanceRatio;
                outcome.launchArc.apexHeight *= pushSquared * input.launch.missHeightRatio;
            }
            return outcome;
        }
} // namespace GL::Level
