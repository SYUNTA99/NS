#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Player/ReboundArc.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/SubObjects/HitSensor.h"

namespace GL
{
    namespace Level
    {
        //! @brief 衝突の配分に使う調整値
        //! @details 既定値の重複を避け、MakeImpactTuning が PlayerParams から全項目を入れる
        struct ImpactTuning
        {
            float centerHitStopScale{}; //!< 中心近くの当たりの貫通の止めの倍率
            bool breakEnabled{};
            float breakSpeedScale{}; //!< 貫通で自機が持つ速度の倍率
            float breakStopSeconds{};
            float reboundDistance{};               //!< 質量 1・威力 1 の自機の反動の距離 (m)
            float reboundApexHeight{};             //!< 質量 1・威力 1 の自機の反動の高さ (m)
            float centerHitReboundDistanceScale{}; //!< 中心近くの当たりの反動の距離の倍率
            float centerHitReboundHeightScale{};   //!< 中心近くの当たりの反動の高さの倍率
            float missReboundDistanceScale{};
            float launchDistance{};         //!< 質量 1・威力 1 の相手の飛ぶ距離 (m)
            float launchMassExponent{};     //!< 質量で割る指数。範囲は 0〜1
            float hitStopMaxSeconds{};      //!< 貫通の止めの上限秒
            float fixedDelta{};             //!< 固定ステップの秒
            float missReboundHeightRatio{}; //!< 外れの反動の高さの、同じ威力と質量の真ん中に対する割合
            float missSlamBounce{};         //!< 真下を向いた面で外した時の、外れの反動の高さに掛ける割合
            float missBoxEdgeSharpness{};   //!< 箱の相手の面を、角を丸めた箱の表面として読む時の鋭さ。2 で球
        };

        //! @brief 衝突の配分の計算へ渡す、検知で決まった値
        struct ImpactInput
        {
            float power = 0.0f; //!< 最終威力。溜めの倍率と、相手の正面の面で当てはまった決まりの威力の倍率の積
            HitTier tier = HitTier::Wide; //!< 当たりの段。反動の距離と貫通の止めの倍率を段で引く
            float mass = 1.0f;
            float toughness = 0.0f;
            bool breakable = false;                        //!< 相手が壊れる動きを持つ場合 true
            NS::Vector3 awayDirection{1.0f, 0.0f, 0.0f};   //!< 自機が弾かれる水平の向き。正規化済み
            NS::Vector3 launchDirection{1.0f, 0.0f, 0.0f}; //!< 相手を飛ばす水平の向き。正規化済み
            NS::Vector3 slamVelocity{0.0f, 0.0f, 0.0f};    //!< 突進の狙いの速度
            float faceU = 0.0f; //!< 段を決めた面の上の左右の位置。自機から見て右が正。外れの逸れる向きを決める
            float faceV = 0.0f; //!< 段を決めた面の上の上下の位置。上が正
            NS::HitSensorShape bodyShape = NS::HitSensorShape::Sphere;
            LaunchShape launch{}; //!< 相手が当たりの答えで返した飛び方
        };

        //! @brief 衝突の配分の計算の結果
        //! @details 貫通の当たりは反動も飛ばしもしないので、曲線と比は 0 のまま
        struct ImpactOutcome
        {
            bool broke = false;        //!< 貫通する場合 true
            float massFactor = 0.0f;   //!< 質量 ÷ (質量 + 1)
            float reboundScale = 0.0f; //!< 自機の反動の高さと距離に掛けた比
            float launchScale = 0.0f;  //!< 相手の曲線の距離と高さに掛けた比
            GL::Player::ReboundArc reboundArc{};
            LaunchArc launchArc{};
            NS::Vector3 breakSelfVelocity{0.0f, 0.0f, 0.0f}; //!< 貫通で明けに自機が持つ速度。貫通しない当たりは 0
            //! 貫通の当たりの止めるフレーム数。押し飛ばしの当たりの止めはタイムラインが持つので 0
            int stopSteps = 0;
        };

        //! 威力・質量・調整値から反動と飛ばしを決める。反動の初速は PlayerParams::ReboundLaunchArc の曲線から求める
        //! 前提: mass は有限の正、両方向は水平で正規化済み、fixedDelta は正
        //! tuning は MakeImpactTuning、input.launch は MapObjParams::Launch の値を渡す
        [[nodiscard]] ImpactOutcome ComputeImpactOutcome(const ImpactInput& input, const ImpactTuning& tuning) noexcept;

        //! 面の位置を角の鋭さ sharpness の超楕円として読み、自機側を向く単位法線を返す
        //! u は右、v は上が正。sharpness は 2 で球、2 未満・非有限は 2 に補正する
        //! slamDirection は水平成分を使う。水平の向きが決まらない時は (0, 0, 0)
        [[nodiscard]] NS::Vector3 MissSurfaceNormal(float u,
                                                    float v,
                                                    float sharpness,
                                                    const NS::Vector3& slamDirection) noexcept;

        //! @brief 外れの着地からこすって止まる間の、着いた水平の速さに掛ける倍率を返す
        //! @details (1 − elapsedSteps ÷ totalSteps)^exponent。着いた速さに依らず totalSteps フレームで 0 になる
        //! @param[in] elapsedSteps 着いてからのフレーム数。着いたフレームが 0
        //! @param[in] totalSteps 止まるまでのフレーム数。0 以下は着いたフレームから 0
        //! @param[in] exponent 減り方。大きいほど早く落ちて長く残る。有限の正でない値は 1 (直線)
        //! @return 0〜1 の倍率
        [[nodiscard]] float MissSkidSpeedScale(int elapsedSteps, int totalSteps, float exponent) noexcept;

        //! @brief 止めの間の横揺れの、frame フレーム目の振れ幅の包みを返す
        //! @details 振れ幅 × (1 − frame ÷ length)。BodyShakeOffset の大きさはこれを超えない
        //! @param[in] frame 揺れの何フレーム目か。始まりのフレームが 1
        //! @param[in] length 揺れのフレーム数。0 以下は揺らさない
        //! @param[in] amplitude 最初の振れ幅
        //! @return 包み。frame が 1 より前か length 以降は 0
        [[nodiscard]] float BodyShakeReach(int frame, int length, float amplitude) noexcept;

        //! 横揺れの符号付きのずれを返す。BodyShakeReach の範囲で弱め、同じ seed・frame では同じ値
        //! frame は開始を 1 とする。1 未満か length 以降、length が非正なら 0
        //! firstSign は負なら −、他は +。flipFrames ごとに左右を反転し、1 未満は 1 に補正する
        [[nodiscard]] float BodyShakeOffset(
            int frame, int length, float amplitude, NS::uint32 seed, float firstSign, int flipFrames) noexcept;

        //! 押して戻す揺れのずれを返す。同じ seed と押し番号では同じばらつき
        //! frame は開始を 1 とする。1 未満か length より後、length が非正なら 0
        //! pushFrames は 1 以上に補正する。returnRatio は押した後の毎フレームの減衰倍率
        [[nodiscard]] float DepthShakeOffset(
            int frame, int length, float depth, int pushFrames, float returnRatio, NS::uint32 seed) noexcept;
    } // namespace Level
} // namespace GL
