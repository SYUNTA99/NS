#include "Game/Level/ImpactOutcome.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Level
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
            // 整数のフレームへ丸めるので、同じ秒の指定は毎回同じ長さ止まる
            const float raw = seconds / tuning.fixedDelta;
            if (!std::isfinite(raw))
            {
                return 0;
            }
            return NS::Core::Clamp(static_cast<int>(std::lround(raw)), 0, MaxHitStopSteps(tuning));
        }

        // 段が配分に掛ける倍率
        struct TierScales
        {
            float hitStop = 1.0f;         // 貫通の止めの倍率
            float reboundDistance = 1.0f; // 反動の距離の倍率。外れの高さは外れの節で下げる
        };

        // 段ごとに 1 行の表。段を足したら行を足す。真偽で分けると段が 2 つと決め打ちになる
        [[nodiscard]] TierScales TierScalesFor(HitTier tier, const ImpactTuning& tuning) noexcept
        {
            switch (tier)
            {
            case HitTier::Center:
                return TierScales{.hitStop = tuning.centerHitStopScale,
                                  .reboundDistance = tuning.centerHitReboundDistanceScale};
            case HitTier::Wide:
                return TierScales{.reboundDistance = tuning.missReboundDistanceScale};
            }
            // 番号から作った段の外の値は倍率を掛けない
            return TierScales{};
        }
    } // namespace

    NS::Core::Vector3 MissSurfaceNormal(float u,
                                        float v,
                                        float sharpness,
                                        const NS::Core::Vector3& slamDirection) noexcept
    {
        NS::Core::Vector3 forward{};
        if (!NS::Core::TryNormalizeHorizontal(slamDirection, forward))
        {
            return NS::Core::Vector3{};
        }
        float p = sharpness;
        if (!std::isfinite(p) || p < 2.0f)
        {
            p = 2.0f;
        }
        // 面の右は JudgeHitFace と同じ。左手系で上から見て、進む向きの右
        const NS::Core::Vector3 right{forward.z, 0.0f, -forward.x};
        const NS::Core::Vector3 up{0.0f, 1.0f, 0.0f};
        const float absU = std::abs(u);
        const float absV = std::abs(v);
        const float edge = std::min(std::pow(std::pow(absU, p) + std::pow(absV, p), 1.0f / p), 1.0f);
        // 超楕円の面の勾配。p = 2 で位置そのもの、p が大きいと大きい方の軸へ寄る
        const float slideU = std::copysign(std::pow(absU, p - 1.0f), u);
        const float slideV = std::copysign(std::pow(absV, p - 1.0f), v);
        const float slideLength = std::sqrt(slideU * slideU + slideV * slideV);
        NS::Core::Vector3 normal = -forward;
        if (std::isfinite(edge) && slideLength > NS::Core::k_Epsilon)
        {
            const NS::Core::Vector3 slide = (right * slideU + up * slideV) / slideLength;
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
        if (!std::isfinite(c) || !(c > 0.0f))
        {
            c = 1.0f;
        }
        const float left = 1.0f - static_cast<float>(std::max(elapsedSteps, 0)) / static_cast<float>(totalSteps);
        return std::pow(left, c);
    }

    float BodyShakeOffset(int frame, int length, float amplitude, std::uint32_t seed, float firstSign) noexcept
    {
        if (length <= 0 || frame < 1 || frame >= length)
        {
            return 0.0f;
        }
        const float left = 1.0f - static_cast<float>(frame) / static_cast<float>(length);
        // 振れ幅だけをばらつかせ、左右の入れ替わりは乱さない。乱すと止まって見えるフレームができる
        constexpr float k_HashToUnit = 1.0f / 4294967295.0f;
        const float spread = 0.7f + 0.3f * static_cast<float>(NS::Core::NoiseHash(frame, seed)) * k_HashToUnit;
        float sign = 1.0f;
        if (firstSign < 0.0f)
        {
            sign = -1.0f;
        }
        if (frame % 2 == 0)
        {
            sign = -sign;
        }
        return amplitude * left * left * spread * sign;
    }

    ImpactOutcome ComputeImpactOutcome(const ImpactInput& input, const ImpactTuning& tuning) noexcept
    {
        ImpactOutcome outcome{};
        const float power = input.power;
        const float mass = input.mass;
        outcome.massFactor = mass / (mass + 1.0f);

        const TierScales tierScales = TierScalesFor(input.tier, tuning);
        const float hitStopScale = tierScales.hitStop;

        // 破壊を許可していない間は耐久を見ない。壊れる相手も押し飛ばしと反発へ回る
        // 壊れる動きを持たない相手も同じ。貫通させると、残った相手の当たりへ自機がめり込んで止まる
        // 反動と飛ばしの比は押し飛ばしの当たりだけが埋める。貫通は反動も飛ばしもしないので 0
        if (tuning.breakEnabled && input.breakable && input.toughness <= power)
        {
            outcome.broke = true;
            // 向きを保ったまま減速する。倍率は相手の質量に依らない
            outcome.breakSelfVelocity = input.slamVelocity * tuning.breakSpeedScale;
            outcome.stopSteps = SecondsToSteps(tuning, tuning.breakStopSeconds * hitStopScale);
            return outcome;
        }

        // 質量因子 mass/(mass+1) は質量が大きいほど 1 へ寄る。軽い物は勢いを持っていくのでほとんど返らない
        // 2 倍して質量 1 で 1 にし、欄を質量 1・威力 1 の高さと距離で持つ
        // 高さと距離に同じ倍率を掛け、威力と質量が変わっても弾かれ始めの角度を揃える
        // TODO: 質量 0.5 より軽い物では自機の返りが 0 に近づく。軽い物を置く時は、先に高さと距離の下限を足す
        outcome.reboundScale = power * 2.0f * outcome.massFactor;
        // 中心近くの当たりだけ距離を伸ばし、高さは変えない。真ん中に当てた時は後ろへ飛ぶ
        const float reboundDistance = tuning.reboundDistance * outcome.reboundScale * tierScales.reboundDistance;
        outcome.reboundArc = NS::Game::Player::ReboundArc{
            .direction = NS::Core::Vector3{input.awayDirection.x, 0.0f, input.awayDirection.z},
            .apexHeight = tuning.reboundApexHeight * outcome.reboundScale,
            .distance = reboundDistance};
        NS::Core::Vector3 launchDirection = input.launchDirection;
        // 外れで相手へ押し込む成分。突進の向きと触れた面の向きの内積で、面の真ん中は 1、端ほど 0 へ寄る
        float missPush = 1.0f;

        // 外れは触れた表面の向き n で来た勢いを分ける。n へ押し込む成分は跳ね返り、面に沿って滑る成分はそのまま
        // 残るので、真ん中は来た向きへ戻り、端は勢いの多くが横へ逃げて相手の脇を逸れる。量は今の配分のまま、
        // 向きと符号だけをこの分け方から借りる。相手は押し込む成分だけを受け取り、−n の向きへ押される
        // 水平の向きは n の水平の成分で映す。縦の成分まで映すと、上の縁の外れが前へ抜けて相手を越え、
        // 低い反動の弧のまま相手へもう 1 度ぶつかる。上下の縁は高さだけで分ける
        if (input.tier == HitTier::Wide)
        {
            float sharpness = 2.0f;
            if (input.bodyShape == NS::Obj::HitSensorShape::Box)
            {
                sharpness = tuning.missBoxEdgeSharpness;
            }
            const NS::Core::Vector3 normal = MissSurfaceNormal(input.faceU, input.faceV, sharpness, input.slamVelocity);
            const float slamSpeed = input.slamVelocity.Length();
            if (slamSpeed > NS::Core::k_Epsilon)
            {
                missPush = NS::Core::Clamp(-(input.slamVelocity / slamSpeed).Dot(normal), 0.0f, 1.0f);
            }
            NS::Core::Vector3 forward{};
            NS::Core::Vector3 flatNormal{};
            if (NS::Core::TryNormalizeHorizontal(input.slamVelocity, forward))
            {
                // 真上か真下の縁で水平の成分が無い時は、来た向きへ戻す
                flatNormal = -forward;
                NS::Core::Vector3 horizontal{};
                if (NS::Core::Vector2{normal.x, normal.z}.Length() > NS::Core::k_Epsilon &&
                    NS::Core::TryNormalizeHorizontal(normal, horizontal))
                {
                    flatNormal = horizontal;
                }
                const NS::Core::Vector3 mirrored = forward - flatNormal * (2.0f * forward.Dot(flatNormal));
                NS::Core::Vector3 deflect{};
                if (NS::Core::TryNormalizeHorizontal(mirrored, deflect))
                {
                    outcome.reboundArc.direction = deflect;
                }
                launchDirection = -flatNormal;
            }
            // ねじれる軸はかすった所の摩擦から出す。滑る成分 = 来た向き − 押し込む成分、軸 = n × 滑る成分
            // 右の縁では相手の側が引きずられ、相手の方へ巻き込まれる向きに回る
            NS::Core::Vector3 slide{};
            if (NS::Core::TryNormalizeHorizontal(input.slamVelocity, slide))
            {
                slide = slide - normal * slide.Dot(normal);
            }
            outcome.reboundArc.missTumble =
                NS::Game::Player::MissTumble{.twist = normal.Cross(slide), .power = input.power};
            // 浮く感じは真ん中だけの物にする。下を向いた面は地面へ叩きつけられ、さらに低く跳ねる
            const float downward = NS::Core::Clamp(-normal.y, 0.0f, 1.0f);
            const float slam = 1.0f - (1.0f - tuning.missSlamBounce) * downward;
            outcome.reboundArc.apexHeight *= tuning.missReboundHeightRatio * slam;
        }

        // 指数の範囲は 0〜1。負にすると重い物ほど飛ぶ逆転になる
        float massExponent = tuning.launchMassExponent;
        if (!std::isfinite(massExponent))
        {
            massExponent = 1.0f;
        }
        massExponent = NS::Core::Clamp(massExponent, 0.0f, 1.0f);

        // 威力は距離に線形に効き、質量で割ると重い物ほど飛ばない。高さは距離と同じ比で伸ばし、打ち上げの角度を揃える
        outcome.launchScale = power / std::pow(mass, massExponent);
        outcome.launchArc = LaunchArc{.direction = launchDirection,
                                      .distance = tuning.launchDistance * outcome.launchScale,
                                      .apexHeight = tuning.launchApexHeight * outcome.launchScale,
                                      .riseGravity = tuning.launchRiseGravity,
                                      .fallGravityScale = tuning.launchFallGravityScale,
                                      .apexBandSpeed = tuning.launchApexBandSpeed,
                                      .apexBandGravityScale = tuning.launchApexBandGravityScale};
        // 外れは力が相手へ真っすぐ入らない。飛ぶ量を押し込む成分の 2 乗で減らした上から距離と弧をさらに縮め、
        // 相手は少しずれるだけにして、真ん中の「弾き飛ばした」に見せない
        if (input.tier == HitTier::Wide)
        {
            const float pushSquared = missPush * missPush;
            outcome.launchArc.distance *= pushSquared * tuning.missLaunchDistanceRatio;
            outcome.launchArc.apexHeight *= pushSquared * tuning.missLaunchHeightRatio;
        }
        return outcome;
    }
} // namespace NS::Game::Level
