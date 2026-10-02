#include "Game/Level/ImpactOutcome.h"

#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        // 上限秒をフレーム数へ換算する。非有限と 0 以下は 0 で、止めない
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

        // 最終威力と質量から止めるフレーム数を出す。0 なら止めない
        [[nodiscard]] int ComputeHitStopSteps(const ImpactTuning& tuning,
                                              float power,
                                              float mass,
                                              float hitStopScale) noexcept
        {
            // 質量差をそのままフレーム数に出すと停止が伸びすぎるので平方根で圧縮する
            const float raw = tuning.hitStopBaseSeconds * power * std::sqrt(mass) / tuning.fixedDelta * hitStopScale;
            if (!std::isfinite(raw))
            {
                return 0;
            }
            const int steps = static_cast<int>(std::lround(raw));
            return NS::Core::Clamp(steps, 0, MaxHitStopSteps(tuning));
        }
    } // namespace

    ImpactOutcome ComputeImpactOutcome(const ImpactInput& input, const ImpactTuning& tuning) noexcept
    {
        ImpactOutcome outcome{};
        const float power = input.power;
        const float mass = input.mass;
        outcome.massFactor = mass / (mass + 1.0f);

        float hitStopScale = 1.0f;
        if (input.centerHit)
        {
            hitStopScale = tuning.centerHitStopScale;
        }
        // 反発の質量因子の残り。動きは軽い側が受け取るので、重い物ほど揺れない
        outcome.shakeAmplitude = tuning.shakeAmplitude / (1.0f + mass);

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
        float reboundDistance = tuning.reboundDistance * outcome.reboundScale;
        if (input.centerHit)
        {
            reboundDistance *= tuning.centerHitReboundDistanceScale;
        }
        outcome.reboundArc = NS::Game::Player::ReboundArc{
            .direction = NS::Core::Vector3{input.awayDirection.x, 0.0f, input.awayDirection.z},
            .apexHeight = tuning.reboundApexHeight * outcome.reboundScale,
            .distance = reboundDistance};

        // 指数の範囲は 0〜1。負にすると重い物ほど飛ぶ逆転になる
        float massExponent = tuning.launchMassExponent;
        if (!std::isfinite(massExponent))
        {
            massExponent = 1.0f;
        }
        massExponent = NS::Core::Clamp(massExponent, 0.0f, 1.0f);

        // 威力は距離に線形に効き、質量で割ると重い物ほど飛ばない。高さは距離と同じ比で伸ばし、打ち上げの角度を揃える
        outcome.launchScale = power / std::pow(mass, massExponent);
        outcome.launchArc = LaunchArc{.direction = input.launchDirection,
                                      .distance = tuning.launchDistance * outcome.launchScale,
                                      .apexHeight = tuning.launchApexHeight * outcome.launchScale,
                                      .riseGravity = tuning.launchRiseGravity,
                                      .fallGravityScale = tuning.launchFallGravityScale,
                                      .apexBandSpeed = tuning.launchApexBandSpeed,
                                      .apexBandGravityScale = tuning.launchApexBandGravityScale};

        outcome.stopSteps = ComputeHitStopSteps(tuning, power, mass, hitStopScale);
        return outcome;
    }
} // namespace NS::Game::Level
