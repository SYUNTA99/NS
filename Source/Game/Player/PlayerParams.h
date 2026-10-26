#pragma once

#include "Game/Level/AirGravity.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/Object.h"

#include <algorithm>

class PlayerActor;

namespace GL
{
    namespace Player
    {
        class PlayerParams;
    } // namespace Player
} // namespace GL

namespace GL
{
    namespace Level
    {
        class ImpactResolver;
        class SlamArrow;
        struct ImpactTuning;
        struct LaunchArc;
        ImpactTuning MakeImpactTuning(const GL::Player::PlayerParams* params) noexcept;
    } // namespace Level
} // namespace GL

namespace GL
{
    namespace Player
    {
        struct ReboundArc;
    } // namespace Player
} // namespace GL

namespace GL
{
    namespace Player
    {
        //! @brief 自機の移動・溜め・衝突・反動の調整値を持つ部品
        class PlayerParams : public NS::Object
        {
        public:
            PlayerParams() noexcept;

            //! 欄「体力」の値。1 未満は 1 として返す
            [[nodiscard]] int MaxHealth() const noexcept { return std::max(m_maxHealth, 1); }
            //! スティックの倒しがこの値未満なら入力なしとして扱う
            [[nodiscard]] float StickDeadzone() const noexcept { return m_stickDeadzone; }
            //! 入力方向への水平速度成分がこの値未満ならブレーキに入る
            [[nodiscard]] float BrakeThreshold() const noexcept { return m_brakeThreshold; }
            //! 欄「エアダッシュの距離」を「エアダッシュの秒」で進む速さ (m/s)。返る速さの元にもなる
            //! 秒が 0 以下か非数なら 0 を返し、ダッシュは動かない
            [[nodiscard]] float AirDashSpeed() const noexcept;
            //! 欄「上昇重力」「下降重力」「頂点滞空 Vy」「頂点滞空倍率」の写し。重力の強さを選ぶ ChooseGravity へ渡す
            [[nodiscard]] GL::Level::AirGravity Gravity() const noexcept
            {
                return GL::Level::AirGravity{.rise = m_gravityUp,
                                             .fall = m_gravityDown,
                                             .apexSpeed = m_apexHangVy,
                                             .apexScale = m_apexHangScale};
            }
            //! 上りだけ反動の倍率を掛けた重力を返す。移動と初速の計算に同じ値を使う
            [[nodiscard]] GL::Level::AirGravity ReboundGravity() const noexcept
            {
                return GL::Level::AirGravity{.rise = m_gravityUp * m_reboundRiseGravityScale,
                                             .fall = m_gravityDown,
                                             .apexSpeed = m_apexHangVy,
                                             .apexScale = m_apexHangScale};
            }
            //! 当たりが決めた反動の向き・高さ・距離を、ReboundGravity の重力で飛ぶ曲線にする
            //! 反動の初速と滞空の秒は、自機が弾く時も裁定が記録に書く時もこの曲線から引く
            [[nodiscard]] GL::Level::LaunchArc ReboundLaunchArc(const ReboundArc& arc) const noexcept;
            //! @brief 溜め量 0..1 を欄「チャージ倍率カーブ」で威力の倍率にする
            //! @details 非有限の入力とカーブの 0 以下の値は 1 とみなす。ImpactResolver が当たりの威力を作る時に読む
            //! @param[in] charge01 溜め量。0..1 の外は丸める
            //! @return 威力の倍率
            [[nodiscard]] float ChargeFactorFor(float charge01) const noexcept;
            //! @brief 溜め量と溜めすぎの深さを威力の倍率にする
            //! @details 溜め量の倍率 (1 つ引数の ChargeFactorFor) に、溜めすぎの深さで 1 から欄「紫の威力の上限」まで
            //! 線形に上がる倍率を掛ける。非有限の深さは 0、0..1 の外は丸める
            //! @param[in] charge01 溜め量。0..1 の外は丸める
            //! @param[in] overcharge01 溜めすぎの深さ
            //! @return 威力の倍率
            [[nodiscard]] float ChargeFactorFor(float charge01, float overcharge01) const noexcept;
            //! 溜め中の最高速度の倍率を返す。1 − チャージ減速率を 0〜1 に丸める
            [[nodiscard]] float ChargingSpeedScale() const noexcept;

            NS_REFLECT_BEGIN(PlayerParams, NS::Object)
            NS_GROUP("体力")
            NS_FIELD(m_maxHealth, "体力")
            NS_GROUP("ジャンプ")
            NS_FIELD(m_jumpImpulse, "ジャンプ初速")
            NS_FIELD(m_gravityUp, "上昇重力")
            NS_FIELD(m_gravityDown, "下降重力")
            NS_FIELD(m_apexHangVy, "頂点滞空 Vy")
            NS_FIELD(m_apexHangScale, "頂点滞空倍率")
            NS_FIELD(m_jumpReleaseScale, "ジャンプ離し倍率")
            NS_FIELD(m_coyoteTime, "コヨーテ時間")
            NS_FIELD(m_jumpBufferTime, "先行入力時間")
            NS_GROUP("移動")
            NS_FIELD(m_walkSpeed, "歩き速度")
            NS_FIELD(m_runSpeed, "走行速度")
            NS_FIELD(m_acceleration, "加速度")
            NS_FIELD(m_airAcceleration, "空中の加速度")
            NS_FIELD(m_turningDrag, "曲がる時の抵抗")
            NS_FIELD(m_airControlReturnSeconds, "ダッシュと反動の後に空中の操作が戻る秒")
            NS_FIELD(m_friction, "手を放した時の減速度")
            NS_FIELD(m_deceleration, "ブレーキの減速度")
            NS_FIELD(m_brakeThreshold, "ブレーキのしきい値")
            NS_FIELD(m_stickDeadzone, "スティック遊び")
            NS_FIELD(m_turnSpeed, "振り向きの速さ")
            NS_GROUP("段と縁")
            NS_FIELD(m_maxStepHeight, "登れる段の高さ")
            NS_FIELD(m_ledgeGrabBelowHand, "掴める縁の下向き距離")
            NS_FIELD(m_ledgeReach, "縁へ手を伸ばす距離")
            NS_FIELD(m_ledgeClimbDuration, "よじ登りの所要時間")
            NS_FIELD(m_ledgeShimmySpeed, "縁の横移動速度")
            NS_GROUP("突進")
            NS_FIELD(m_bodySlamSpeed, "チャージ突進の速度")
            NS_FIELD(m_bodySlamDistance, "チャージ突進の距離")
            NS_FIELD(m_launchPitchLimitDegrees, "放つ角度の上限")
            NS_FIELD(m_tapSlamSpeed, "通常突進の初速")
            NS_FIELD(m_tapSlamUpSpeed, "通常突進の上向き初速")
            NS_FIELD(m_tapSlamDistance, "通常突進の距離")
            NS_FIELD(m_slamAimHoldTime, "狙いの巻き戻し秒")
            NS_FIELD(m_slamAimFadeTime, "狙いの巻き戻しが消える秒")
            NS_GROUP("溜め")
            NS_FIELD(m_chargeThresholdSeconds, "チャージしきい値秒")
            NS_FIELD(m_chargeFullSeconds, "チャージ満タン秒")
            NS_FIELD(m_chargeSlowRate, "チャージ減速率")
            NS_FIELD(m_chargeFactorCurve, "チャージ倍率カーブ")
            NS_GROUP("溜めすぎ")
            NS_FIELD(m_overchargeSeconds, "溜めすぎの秒数")
            NS_FIELD(m_overchargeSwayMaxOffset, "紫の揺れの最大のずれ")
            NS_FIELD(m_overchargeSwayStartRate, "紫の揺れの始めの速さ")
            NS_FIELD(m_overchargeSwayEndRate, "紫の揺れの終わりの速さ")
            NS_FIELD(m_overchargeSwayFallbackDistance, "相手がいない時に直す距離")
            NS_FIELD(m_overchargePowerMax, "紫の威力の上限")
            NS_GROUP("エアダッシュ")
            NS_FIELD(m_airDashDistance, "エアダッシュの距離")
            NS_FIELD(m_airDashSeconds, "エアダッシュの秒")
            NS_FIELD(m_airDashExitSpeed, "エアダッシュ後に残る速さ")
            NS_FIELD(m_intendedHoldSeconds, "行きたい向きとみなす秒")
            NS_FIELD(m_retiltDegrees, "倒し直しとみなす角度")
            NS_GROUP("地形の反動")
            NS_FIELD(m_reboundTableDivisions, "向きの表の分割数")
            NS_FIELD(m_reboundTableWithPitch, "表に傾きの段を入れる")
            NS_FIELD(m_terrainReboundSpeedScale, "返る速さのダッシュの速さに対する倍率")
            NS_FIELD(m_reboundKeepRatio, "返る力の逃げない割合")
            NS_FIELD(m_reboundMinFacing, "反動に数える当て方の下限")
            NS_FIELD(m_reboundFullFacing, "全部返ったに数える当て方の下限")
            NS_FIELD(m_reboundLockSeconds, "反動の操作なしの秒")
            NS_FIELD(m_reboundSteerEndSeconds, "反動の補正が終わる秒")
            NS_FIELD(m_dashReboundGravity, "地形の反動の重力")
            NS_FIELD(m_dashReboundDeceleration, "地形の反動の減速度")
            NS_FIELD(m_reboundSteerLimitRatio, "曲げの上限の段の割合")
            NS_FIELD(m_reboundSteerMaxDegreesPerSecond, "曲げの最大の角速度")
            NS_FIELD(m_drawReboundLines, "反動の線を描く")
            NS_FIELD(m_reboundLineLength, "反動の線の長さ")
            NS_GROUP("反動")
            NS_FIELD(m_reboundRiseGravityScale, "反動の上りの重力倍率")
            NS_FIELD(m_reboundAirAcceleration, "反動中の空中の加速度")
            NS_FIELD(m_reboundApexHeight, "反動の高さ")
            NS_FIELD(m_reboundDistance, "反動の距離")
            NS_FIELD(m_centerHitReboundDistanceScale, "中心近くの当たりの反動の距離の倍率")
            NS_FIELD(m_centerHitReboundHeightScale, "中心近くの当たりの反動の高さの倍率")
            NS_GROUP("外れ")
            NS_FIELD(m_missReboundDistanceScale, "外れの反動の距離の倍率")
            NS_FIELD(m_missReboundHeightRatio, "外れの反動の高さの割合")
            NS_FIELD(m_missSlamBounce, "叩きつけた時の跳ね")
            NS_FIELD(m_missBoxEdgeSharpness, "四角の角の鋭さ")
            NS_FIELD(m_missSkidSteps, "外れのこすって止まるまでのフレーム数")
            NS_FIELD(m_missSkidExponent, "外れのこすって止まる減り方")
            NS_GROUP("押し飛ばし")
            NS_FIELD(m_launchDistance, "押し飛ばしの距離")
            NS_FIELD(m_launchMassExponent, "押し飛ばしの質量指数")
            NS_GROUP("ヒットストップ")
            NS_FIELD(m_centerHitStopScale, "中心近くの当たりのヒットストップ倍率")
            NS_FIELD(m_hitStopMaxSeconds, "ヒットストップの上限秒")
            NS_GROUP("破壊")
            NS_FIELD(m_breakEnabled, "破壊を許可")
            NS_FIELD(m_breakSpeedScale, "貫通時の減速倍率")
            NS_FIELD(m_breakStopSeconds, "貫通の止め秒")
            NS_GROUP("演出: 突進の回転数")
            NS_FIELD(m_chargedSlamTurns, "溜めた突進の届くまでの回転数")
            NS_FIELD(m_tapSlamTurns, "通常突進の届くまでの回転数")
            NS_FIELD(m_chargedReboundTurns, "溜めて当てた反動の回転数")
            NS_FIELD(m_tapReboundTurns, "通常突進で当てた反動の回転数")
            NS_GROUP("計算の内部")
            NS_FIELD(m_contactTolerance, "突進の接触探索の誤差")
            NS_FIELD(m_launchHeightTolerance, "突進の高さ探索の誤差")
            NS_FIELD(m_launchAngleGuardDegrees, "突進の角度計算の上限")
            NS_FIELD(m_launchMaxFrames, "突進の予測フレーム上限")
            NS_REFLECT_END()

        private:
            float m_contactTolerance = 0.001f;
            float m_launchHeightTolerance = 0.001f;
            float m_launchAngleGuardDegrees = 89.0f;
            int m_launchMaxFrames = 600;
            friend class GL::Level::ImpactResolver;
            friend class GL::Level::SlamArrow;
            friend GL::Level::ImpactTuning GL::Level::MakeImpactTuning(const PlayerParams* params) noexcept;
            float m_reboundApexHeight = 1.15f;
            float m_reboundDistance = 0.575f;
            // 真ん中はカメラ側へ低く速く弾き返し、浮きを抑える
            float m_centerHitReboundDistanceScale = 4.5f;
            float m_centerHitReboundHeightScale = 0.6f;
            // 外れは外した側へ低く滑って抜ける
            float m_missReboundDistanceScale = 2.5f;
            // 外れの浮きを抑え、次の狙いへ早く戻す
            float m_missReboundHeightRatio = 0.05f;
            // 下の縁の外れは地面へ叩きつけ、少し跳ねてこする。真下を向いた面で外れの高さの 4 割
            float m_missSlamBounce = 0.4f;
            // 箱の相手の面の読み方。見本の出発点 6 で、縁に沿った所は縁の向きへ真っすぐ、角の近くだけ斜めに逸れる
            float m_missBoxEdgeSharpness = 6.0f;
            // 着地時の速さに依らず、この固定フレーム数で操作を戻す。0 なら着地直後に戻す
            int m_missSkidSteps = 10;
            // 速さ = 着いた速さ × (1 − 経過 ÷ フレーム数)^減り方。2 で、すぐ落ちて最後に少し擦れが残る
            float m_missSkidExponent = 2.0f;
            float m_launchDistance = 29.0f;
            float m_launchMassExponent = 0.35f;
            float m_centerHitStopScale = 2.0f;
            float m_hitStopMaxSeconds = 12.0f / 60.0f;
            bool m_breakEnabled = false;
            float m_breakSpeedScale = 0.75f;
            float m_breakStopSeconds = 4.0f / 60.0f;
            // 回転数で指定し、到達時間から速さを求める。速度や距離を変えても回る数を保つ
            float m_chargedSlamTurns = 5.0f;    // 溜めた突進が届くまでに回る回転数
            float m_tapSlamTurns = 2.0f;        // 通常突進が届くまでに回る回転数
            float m_chargedReboundTurns = 3.0f; // 溜めて当てた反動が着地までに回る回転数
            float m_tapReboundTurns = 1.0f;     // 通常突進で当てた反動が着地までに回る回転数
            friend class ::PlayerActor;
            float m_chargeThresholdSeconds = 0.2f;
            float m_chargeFullSeconds = 1.0f;
            // 溜めきりから自動発動までの秒
            float m_overchargeSeconds = 3.0f;
            // 相手の位置での横ずれ (m)。角度指定だと遠い相手ほど外れやすくなる
            float m_overchargeSwayMaxOffset = 2.2f;
            // 揺れの回数 (回/秒)。溜めすぎの深さの 2 乗で終わりの速さへ寄せる
            float m_overchargeSwayStartRate = 0.8f;
            float m_overchargeSwayEndRate = 2.6f;
            // 相手がいない時に、横ずれを角度へ変換する距離 (m)
            float m_overchargeSwayFallbackDistance = 10.0f;
            // 溜めすぎきった時に、溜めきりの威力へ掛ける倍率
            float m_overchargePowerMax = 1.5f;
            float m_chargeSlowRate = 0.7f;
            NS::Curve m_chargeFactorCurve{};
            int m_maxHealth = 8;
            float m_jumpImpulse = 12.0f;
            float m_gravityUp = -25.0f;
            float m_gravityDown = -35.0f;
            float m_apexHangVy = 1.0f;
            float m_apexHangScale = 0.5f;
            float m_jumpReleaseScale = 0.6f;
            float m_coyoteTime = 0.025f;
            float m_jumpBufferTime = 0.25f;
            float m_walkSpeed = 4.0f;
            float m_runSpeed = 8.0f;
            float m_acceleration = 40.0f;
            float m_airAcceleration = 40.0f;
            float m_turningDrag = 40.0f;
            // ダッシュと反動の後の落下で、空中の加速度と曲がる時の抵抗が 0 から全部へ戻るまでの秒
            float m_airControlReturnSeconds = 0.5f;
            float m_friction = 40.0f;
            float m_deceleration = 40.0f;
            float m_brakeThreshold = -0.8f;
            float m_stickDeadzone = 0.3f;
            float m_maxStepHeight = 0.25f;
            float m_ledgeGrabBelowHand = 0.5f;
            float m_ledgeReach = 0.3f;
            float m_ledgeClimbDuration = 0.25f;
            float m_ledgeShimmySpeed = 2.0f;
            float m_turnSpeed = 970.0f;
            float m_bodySlamSpeed = 20.0f;
            float m_bodySlamDistance = 10.0f;
            // 赤の高さへ向ける角度の上限 (度)。突進で高所へ登れる範囲を抑える
            float m_launchPitchLimitDegrees = 40.0f;
            // 通常突進の速度 (m/s)。距離は溜めた突進と同じ
            float m_tapSlamSpeed = 15.0f;
            float m_tapSlamUpSpeed = 3.0f;
            float m_tapSlamDistance = 6.25f;
            float m_slamAimHoldTime = 0.11f;
            float m_slamAimFadeTime = 0.19f;
            float m_reboundRiseGravityScale = 0.5f;
            float m_reboundAirAcceleration = 2.0f;
            float m_airDashDistance = 4.4f;   // m。当たらなければこの距離を秒で進む
            float m_airDashSeconds = 0.22f;   // 当たらなかった時に終わる秒
            float m_airDashExitSpeed = 8.0f;  // 当たらずに終わったフレームに落とす速さ (m/s)
            float m_intendedHoldSeconds = 0.1f;    // 同じ向きをこの秒以上倒し続けたら、ダッシュの向きにする
            float m_retiltDegrees = 30.0f;         // 向きがこの角度より変わったら、倒し続けた秒を数え直す
            int m_reboundTableDivisions = 8;  // 8・12・16 のうち一番近い物へ丸める
            bool m_reboundTableWithPitch = true; // 偽なら縦・横・奥の 3 枚の円の表
            float m_terrainReboundSpeedScale = 1.0f; // ダッシュで当てた時に返る速さ。返った後に続けて当たる時は掛け直さない
            float m_reboundKeepRatio = 0.6f;  // かすめた時に残る速さの割合
            // 当て方は進む向きと面の法線の内積の大きさ。下限より浅い当たりは掃引の滑りに任せる
            float m_reboundMinFacing = 0.2f;
            float m_reboundFullFacing = 0.9f; // これ以上なら強い演出の束を始める
            float m_reboundLockSeconds = 0.15f;    // 反動を始めてから操作が戻るまで
            float m_reboundSteerEndSeconds = 0.6f; // 反動を始めてから補正の段が終わるまで
            float m_dashReboundGravity = 30.0f;     // 補正の段の重力 (m/s²)
            float m_dashReboundDeceleration = 6.0f; // 補正の段の水平の減速度 (m/s²)
            float m_reboundSteerLimitRatio = 0.45f; // 曲げの累計の上限を 1 段の角度に対する割合で。0.5 未満へ丸める
            float m_reboundSteerMaxDegreesPerSecond = 120.0f; // 補正の段の終わりに届く曲げの角速度
            bool m_drawReboundLines = true;
            float m_reboundLineLength = 2.0f; // 開発用の向きの線の長さ (m)
        };
    } // namespace Player
} // namespace GL
