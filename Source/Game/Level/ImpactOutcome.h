#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Player/ReboundArc.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/HitSensor.h"

namespace NS::Game::Level
{
    //! @brief 衝突の配分の計算が読む調整値
    //! @details MakeImpactTuning が PlayerParams の欄から全部入れる。値の正は PlayerParams 1 つで、
    //! ここには既定を持たない (既定の写しを持つと、PlayerParams の既定を変えた時に静かに食い違う)
    struct ImpactTuning
    {
        float centerHitStopScale{};            //!< 中心近くの当たりの貫通の止めの倍率
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
        float hitStopMaxSeconds{};             //!< 貫通の止めの上限秒
        float fixedDelta{};                    //!< 固定ステップの秒
        float missReboundHeightRatio{};        //!< 外れの反動の高さの、同じ威力と質量の真ん中に対する割合
        float missSlamBounce{};                //!< 真下を向いた面で外した時の、外れの反動の高さに掛ける割合
        float missBoxEdgeSharpness{};          //!< 箱の相手の面を、角を丸めた箱の表面として読む時の鋭さ。2 で球
    };

    //! @brief 衝突の配分の計算へ渡す、検知で決まった値
    struct ImpactInput
    {
        float power = 0.0f;           //!< 最終威力。溜めの倍率と、相手の正面の面で当てはまった決まりの威力の倍率の積
        HitTier tier = HitTier::Wide; //!< 当たりの段。反動の距離と貫通の止めの倍率を段で引く
        float mass = 1.0f;            //!< 相手の質量
        float toughness = 0.0f;       //!< 相手の耐久
        bool breakable = false;       //!< 相手が壊れる動きを持つ場合 true
        NS::Core::Vector3 awayDirection{1.0f, 0.0f, 0.0f};   //!< 自機が弾かれる水平の向き。正規化済み
        NS::Core::Vector3 launchDirection{1.0f, 0.0f, 0.0f}; //!< 相手を飛ばす水平の向き。正規化済み
        NS::Core::Vector3 slamVelocity{0.0f, 0.0f, 0.0f};    //!< 突進の狙いの速度
        float faceU = 0.0f; //!< 段を決めた面の上の左右の位置。自機から見て右が正。外れの逸れる向きを決める
        float faceV = 0.0f; //!< 段を決めた面の上の上下の位置。上が正
        NS::Obj::HitSensorShape bodyShape = NS::Obj::HitSensorShape::Sphere; //!< 相手の体の形の種類
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
        //! 貫通の当たりの止めるフレーム数。押し飛ばしの当たりの止めはタイムラインが持つので 0
        int stopSteps = 0;
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

    //! @brief 外れで自機が触れた相手の表面の向きを、面の上の位置から出す
    //! @details 面の上の位置を、角の鋭さ sharpness の超楕円の面の上の点として読む。端の近さ
    //! e = (|u|^p + |v|^p)^(1/p) (1 で頭打ち)、面の上の逸れる向き = (符号 u × |u|^(p−1), 符号 v × |v|^(p−1)) を
    //! 正規化した物、向き = e × (面の右と上への逸れる向き) + √(1 − e²) × 自機へ向かう向き。p = 2 は球の表面と同じ。
    //! p を大きくすると、縁に沿った所はどこでも縁の向きへ真っすぐ、角の近くだけ斜めに逸れる。
    //! 面の右は JudgeHitFace と同じく、上から見て進む向きの右
    //! @param[in] u 面の上の左右の位置。自機から見て右が正
    //! @param[in] v 面の上の上下の位置。上が正
    //! @param[in] sharpness 角の鋭さ p。2 より小さい値と有限でない値は 2
    //! @param[in] slamDirection 突進の向き。縦の成分は捨てる
    //! @return 表面の向き。正規化済みで自機の側を向く。突進の水平の向きが決まらない時は (0, 0, 0)
    [[nodiscard]] NS::Core::Vector3 MissSurfaceNormal(float u,
                                                      float v,
                                                      float sharpness,
                                                      const NS::Core::Vector3& slamDirection) noexcept;

    //! @brief 外れの着地からこすって止まる間の、着いた水平の速さに掛ける倍率を返す
    //! @details (1 − elapsedSteps ÷ totalSteps)^exponent。着いた速さに依らず totalSteps フレームで 0 になる
    //! @param[in] elapsedSteps 着いてからのフレーム数。着いたフレームが 0
    //! @param[in] totalSteps 止まるまでのフレーム数。0 以下は着いたフレームから 0
    //! @param[in] exponent 減り方。大きいほど早く落ちて長く残る。有限の正でない値は 1 (直線)
    //! @return 0〜1 の倍率
    [[nodiscard]] float MissSkidSpeedScale(int elapsedSteps, int totalSteps, float exponent) noexcept;

    //! @brief 止めの間の横揺れの、frame フレーム目の符号付きのずれを返す
    //! @details 振れ幅 × (1 − frame ÷ length)² × ばらつき × 向き。向きは 1 フレーム目が firstSign で、1 フレームごとに
    //! 入れ替わる。ばらつきは seed と frame から出す 0.7〜1 で、同じ種と frame なら同じ値
    //! @param[in] frame 揺れの何フレーム目か。始まりのフレームが 1
    //! @param[in] length 揺れのフレーム数。0 以下は揺らさない
    //! @param[in] amplitude 最初の振れ幅
    //! @param[in] seed ばらつきの種
    //! @param[in] firstSign 1 フレーム目の向き。負なら −、それ以外は +
    //! @return ずれ。frame が 1 より前か length 以降は 0
    [[nodiscard]] float BodyShakeOffset(
        int frame, int length, float amplitude, std::uint32_t seed, float firstSign) noexcept;
} // namespace NS::Game::Level
