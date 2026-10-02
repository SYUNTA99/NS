#pragma once

#include "Game/Level/LaunchArc.h"
#include "Game/Player/ReboundArc.h"
#include "Runtime/Core/Math.h"

namespace NS::Game::Level
{
    //! @brief 衝突の配分の計算が読む調整値
    //! @details MakeImpactTuning が PlayerParams の欄から全部入れる。値の正は PlayerParams 1 つで、
    //! ここには既定を持たない (既定の写しを持つと、PlayerParams の既定を変えた時に静かに食い違う)
    struct ImpactTuning
    {
        float centerHitStopScale{};            //!< 中心近くの当たりの止めの倍率
        float shakeAmplitude{};                //!< 相手の往復の振れ幅の基準
        bool breakEnabled{};                   //!< 破壊を許すか
        float breakSpeedScale{};               //!< 貫通で自機が持つ速度の倍率
        float breakStopSeconds{};              //!< 貫通の止めの秒
        float reboundDistance{};               //!< 質量 1・威力 1 の自機の反動の距離 (m)
        float reboundApexHeight{};             //!< 質量 1・威力 1 の自機の反動の高さ (m)
        float centerHitReboundDistanceScale{}; //!< 中心近くの当たりの反動の距離の倍率
        float launchDistance{};                //!< 質量 1・威力 1 の相手の飛ぶ距離 (m)
        float launchMassExponent{};            //!< 質量で割る指数。範囲は 0〜1
        float launchApexHeight{};              //!< 質量 1・威力 1 の相手の飛ぶ高さ (m)
        float launchRiseGravity{};             //!< 相手の上りの重力 (m/s²)
        float launchFallGravityScale{};        //!< 相手の下りの重力の倍率
        float launchApexBandSpeed{};           //!< 頂点の帯の縦速度 (m/s)
        float launchApexBandGravityScale{};    //!< 頂点の帯の重力倍率
        float hitStopBaseSeconds{};            //!< 質量 1・威力 1 の止めの秒
        float hitStopMaxSeconds{};             //!< 止めの上限秒
        float fixedDelta{};                    //!< 固定ステップの秒
    };

    //! @brief 衝突の配分の計算へ渡す、検知で決まった値
    struct ImpactInput
    {
        float power = 0.0f;     //!< 最終威力。CollisionInput が溜めと当たり位置から出した値
        bool centerHit = false; //!< 中心近くの当たりの場合 true。段の演出を掛けない当たりは false
        float mass = 1.0f;      //!< 相手の質量
        float toughness = 0.0f; //!< 相手の耐久
        bool breakable = false; //!< 相手が壊れる動きを持つ場合 true
        NS::Core::Vector3 awayDirection{1.0f, 0.0f, 0.0f};   //!< 自機が弾かれる水平の向き。正規化済み
        NS::Core::Vector3 launchDirection{1.0f, 0.0f, 0.0f}; //!< 相手を飛ばす水平の向き。正規化済み
        NS::Core::Vector3 slamVelocity{0.0f, 0.0f, 0.0f};    //!< 突進の狙いの速度
    };

    //! @brief 衝突の配分の計算の結果
    //! @details 貫通の当たりは反動も飛ばしもしないので、曲線と比は 0 のまま
    struct ImpactOutcome
    {
        bool broke = false;                                    //!< 貫通する場合 true
        float massFactor = 0.0f;                               //!< 質量 ÷ (質量 + 1)
        float reboundScale = 0.0f;                             //!< 自機の反動の高さと距離に掛けた比
        float launchScale = 0.0f;                              //!< 相手の曲線の距離と高さに掛けた比
        NS::Game::Player::ReboundArc reboundArc{};             //!< 自機の反動の向きと高さと距離
        LaunchArc launchArc{};                                 //!< 相手の飛ぶ曲線
        NS::Core::Vector3 breakSelfVelocity{0.0f, 0.0f, 0.0f}; //!< 貫通で明けに自機が持つ速度。貫通しない当たりは 0
        int stopSteps = 0;                                     //!< 止めるフレーム数。0 は止めない
        float shakeAmplitude = 0.0f;                           //!< 相手の往復の振れ幅
    };

    //! @brief 威力と質量と調整値から、衝突の配分を決める
    //! @details 状態を持たない。重い相手ほど自機の反動が大きく、相手の飛ぶ距離と高さは小さい。
    //! 反動の初速は自機の重力から出すので、反動しない貫通以外は Player::ReboundVelocityFor へ reboundArc を渡して得る
    //! @pre input.mass は有限で 0 より大きい (MapObjParams::Mass が保つ)。
    //! awayDirection と launchDirection は水平で正規化済み (呼び手の ImpactResolver が組む)。
    //! tuning は MakeImpactTuning が PlayerParams から作った値で、fixedDelta は 0 より大きい
    //! @param[in] input 検知で決まった値
    //! @param[in] tuning 調整値
    //! @return 配分の結果
    [[nodiscard]] ImpactOutcome ComputeImpactOutcome(const ImpactInput& input, const ImpactTuning& tuning) noexcept;
} // namespace NS::Game::Level
