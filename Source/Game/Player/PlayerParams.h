#pragma once

#include "Game/Player/LaunchPitch.h"
#include "Game/Player/PlayerGravity.h"
#include "NSlib/Object/Component.h"
#include "NSlib/Object/Reflection/Curve.h"

#include <algorithm>
#include <string>

class Player;

namespace NS::Game::Player
{
    class PlayerParams;
} // namespace NS::Game::Player

namespace NS::Game::Level
{
    class ImpactResolver;
    class SlamArrow;
    struct ImpactTuning;
    // ImpactResolver.h が宣言して ImpactResolver.cpp が定義する。friend に書くために先に宣言しておく
    ImpactTuning MakeImpactTuning(const NS::Game::Player::PlayerParams& params) noexcept;
} // namespace NS::Game::Level

namespace NS::Game::Player
{
    //! @brief 自機の遊びの調整値の欄を持つ部品
    //! @details 移動・溜め・衝突はここの欄を読む。見た目と演出の欄は描く部品 (PlayerAppearance・ChargeEffects・
    //! ImpactEffects・TargetMarker・SlamArrow) が自分で持つ。ここに残る演出の欄は、Player が自分の計算に読む
    //! 突進の回転数・構えの縮み・アニメーションの選び方だけ
    class PlayerParams : public NS::Obj::Component
    {
    public:
        PlayerParams() noexcept;

        //! 欄「体力」の値。1 未満は 1 として返す
        [[nodiscard]] int MaxHealth() const noexcept { return std::max(m_maxHealth, 1); }
        //! 欄「スティック遊び」の値。スティックの倒しがこれ以下なら入力なしとして扱う
        [[nodiscard]] float StickDeadzone() const noexcept { return m_stickDeadzone; }
        //! 欄「ブレーキのしきい値」の値。進行方向と入力方向の内積がこれ以下ならブレーキに入る
        [[nodiscard]] float BrakeThreshold() const noexcept { return m_brakeThreshold; }
        //! 欄「上昇重力」「下降重力」「頂点滞空 Vy」「頂点滞空倍率」の写し。重力の強さを選ぶ ChooseGravity へ渡す
        [[nodiscard]] PlayerGravity Gravity() const noexcept
        {
            return PlayerGravity{
                .rise = m_gravityUp, .fall = m_gravityDown, .apexSpeed = m_apexHangVy, .apexScale = m_apexHangScale};
        }
        //! @brief 反動の間の重力の組。上りだけ欄「反動の上りの重力倍率」を掛け、下りと頂点の帯は Gravity() と同じ
        //! @details Player::ReboundGravity が ChooseGravity へ渡す組と、Player::ReboundVelocityFor が初速の曲線を組む
        //! 元の組は同じ。どちらかだけ欄の選び方を変える道を残さない
        [[nodiscard]] PlayerGravity ReboundGravity() const noexcept
        {
            return PlayerGravity{.rise = m_gravityUp * m_reboundRiseGravityScale,
                                 .fall = m_gravityDown,
                                 .apexSpeed = m_apexHangVy,
                                 .apexScale = m_apexHangScale};
        }
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
        //! @brief 溜め中に最高速へ掛ける倍率を返す
        //! @details 1 − 欄「チャージ減速率」を 0..1 に丸める。
        //! 溜めている間に Player::AdvanceCharge が自機の最高速度へ掛ける
        //! @return 最高速へ掛ける倍率
        [[nodiscard]] float ChargingSpeedScale() const noexcept;

        NS_REFLECT_BEGIN(PlayerParams, NS::Obj::Component)
        NS_REFLECT_GROUP("体力")
        NS_REFLECT_FIELD(m_maxHealth, "体力")
        NS_REFLECT_GROUP("ジャンプ")
        NS_REFLECT_FIELD(m_jumpImpulse, "ジャンプ初速")
        NS_REFLECT_FIELD(m_gravityUp, "上昇重力")
        NS_REFLECT_FIELD(m_gravityDown, "下降重力")
        NS_REFLECT_FIELD(m_apexHangVy, "頂点滞空 Vy")
        NS_REFLECT_FIELD(m_apexHangScale, "頂点滞空倍率")
        NS_REFLECT_FIELD(m_jumpReleaseScale, "ジャンプ離し倍率")
        NS_REFLECT_FIELD(m_coyoteTime, "コヨーテ時間")
        NS_REFLECT_FIELD(m_jumpBufferTime, "先行入力時間")
        NS_REFLECT_GROUP("移動")
        NS_REFLECT_FIELD(m_walkSpeed, "歩き速度")
        NS_REFLECT_FIELD(m_runSpeed, "走行速度")
        NS_REFLECT_FIELD(m_acceleration, "加速度")
        NS_REFLECT_FIELD(m_airAcceleration, "空中の加速度")
        NS_REFLECT_FIELD(m_turningDrag, "曲がる時の抵抗")
        NS_REFLECT_FIELD(m_friction, "手を放した時の減速度")
        NS_REFLECT_FIELD(m_deceleration, "ブレーキの減速度")
        NS_REFLECT_FIELD(m_brakeThreshold, "ブレーキのしきい値")
        NS_REFLECT_FIELD(m_stickDeadzone, "スティック遊び")
        NS_REFLECT_FIELD(m_turnSpeed, "振り向きの速さ")
        NS_REFLECT_GROUP("段と縁")
        NS_REFLECT_FIELD(m_maxStepHeight, "登れる段の高さ")
        NS_REFLECT_FIELD(m_ledgeGrabBelowHand, "掴める縁の下向き距離")
        NS_REFLECT_FIELD(m_ledgeReach, "縁へ手を伸ばす距離")
        NS_REFLECT_FIELD(m_ledgeClimbDuration, "よじ登りの所要時間")
        NS_REFLECT_FIELD(m_ledgeShimmySpeed, "縁の横移動速度")
        NS_REFLECT_GROUP("突進")
        NS_REFLECT_FIELD(m_bodySlamSpeed, "突進速度")
        NS_REFLECT_FIELD(m_bodySlamDistance, "突進距離")
        NS_REFLECT_FIELD(m_launchPitchLimitDegrees, "放つ角度の上限")
        NS_REFLECT_FIELD(m_tapSlamSpeed, "通常突進の初速")
        NS_REFLECT_FIELD(m_tapSlamUpSpeed, "通常突進の上向き初速")
        NS_REFLECT_FIELD(m_tapSlamDistance, "通常突進の距離")
        NS_REFLECT_FIELD(m_slamAimHoldTime, "狙いの巻き戻し秒")
        NS_REFLECT_FIELD(m_slamAimFadeTime, "狙いの巻き戻しが消える秒")
        NS_REFLECT_GROUP("溜め")
        NS_REFLECT_FIELD(m_chargeThresholdSeconds, "チャージしきい値秒")
        NS_REFLECT_FIELD(m_chargeFullSeconds, "チャージ満タン秒")
        NS_REFLECT_FIELD(m_chargeSlowRate, "チャージ減速率")
        NS_REFLECT_FIELD(m_chargeFactorCurve, "チャージ倍率カーブ")
        NS_REFLECT_GROUP("溜めすぎ")
        NS_REFLECT_FIELD(m_overchargeSeconds, "溜めすぎの秒数")
        NS_REFLECT_FIELD(m_overchargeSwayMaxOffset, "紫の揺れの最大のずれ")
        NS_REFLECT_FIELD(m_overchargeSwayStartRate, "紫の揺れの始めの速さ")
        NS_REFLECT_FIELD(m_overchargeSwayEndRate, "紫の揺れの終わりの速さ")
        NS_REFLECT_FIELD(m_overchargeSwayFallbackDistance, "相手がいない時に直す距離")
        NS_REFLECT_FIELD(m_overchargePowerMax, "紫の威力の上限")
        NS_REFLECT_GROUP("反動")
        NS_REFLECT_FIELD(m_reboundRiseGravityScale, "反動の上りの重力倍率")
        NS_REFLECT_FIELD(m_reboundAirAcceleration, "反動中の空中の加速度")
        NS_REFLECT_FIELD(m_reboundApexHeight, "反動の高さ")
        NS_REFLECT_FIELD(m_reboundDistance, "反動の距離")
        NS_REFLECT_FIELD(m_centerHitReboundDistanceScale, "中心近くの当たりの反動の距離の倍率")
        NS_REFLECT_FIELD(m_centerHitReboundHeightScale, "中心近くの当たりの反動の高さの倍率")
        NS_REFLECT_GROUP("外れ")
        NS_REFLECT_FIELD(m_missReboundDistanceScale, "外れの反動の距離の倍率")
        NS_REFLECT_FIELD(m_missReboundHeightRatio, "外れの反動の高さの割合")
        NS_REFLECT_FIELD(m_missSlamBounce, "叩きつけた時の跳ね")
        NS_REFLECT_FIELD(m_missBoxEdgeSharpness, "四角の角の鋭さ")
        NS_REFLECT_FIELD(m_missLaunchHeightRatio, "外れで飛ばす相手の弧の高さの割合")
        NS_REFLECT_FIELD(m_missLaunchDistanceRatio, "外れで飛ばす相手の距離の割合")
        NS_REFLECT_FIELD(m_missSkidSteps, "外れのこすって止まるまでのフレーム数")
        NS_REFLECT_FIELD(m_missSkidExponent, "外れのこすって止まる減り方")
        NS_REFLECT_GROUP("押し飛ばし")
        NS_REFLECT_FIELD(m_launchDistance, "押し飛ばしの距離")
        NS_REFLECT_FIELD(m_launchMassExponent, "押し飛ばしの質量指数")
        NS_REFLECT_FIELD(m_launchApexHeight, "押し飛ばしの高さ")
        NS_REFLECT_FIELD(m_launchRiseGravity, "押し飛ばしの上昇重力")
        NS_REFLECT_FIELD(m_launchFallGravityScale, "下りの速さの倍率")
        NS_REFLECT_FIELD(m_launchApexBandSpeed, "頂点の帯の縦速度")
        NS_REFLECT_FIELD(m_launchApexBandGravityScale, "頂点の帯の重力倍率")
        NS_REFLECT_GROUP("ヒットストップ")
        NS_REFLECT_FIELD(m_centerHitStopScale, "中心近くの当たりのヒットストップ倍率")
        NS_REFLECT_FIELD(m_hitStopMaxSeconds, "ヒットストップの上限秒")
        NS_REFLECT_GROUP("破壊")
        NS_REFLECT_FIELD(m_breakEnabled, "破壊を許可")
        NS_REFLECT_FIELD(m_breakSpeedScale, "貫通時の減速倍率")
        NS_REFLECT_FIELD(m_breakStopSeconds, "貫通の止め秒")
        NS_REFLECT_GROUP("演出: 突進の回転数")
        NS_REFLECT_FIELD(m_chargedSlamTurns, "溜めた突進の届くまでの回転数")
        NS_REFLECT_FIELD(m_tapSlamTurns, "通常突進の届くまでの回転数")
        NS_REFLECT_FIELD(m_chargedReboundTurns, "溜めて当てた反動の回転数")
        NS_REFLECT_FIELD(m_tapReboundTurns, "通常突進で当てた反動の回転数")
        NS_REFLECT_GROUP("演出: 構えの縮み")
        NS_REFLECT_FIELD(m_chargeSquashScale, "構えの縮み")
        NS_REFLECT_FIELD(m_pressSquashScale, "押しの構えの縮み")
        NS_REFLECT_GROUP("演出: アニメーション")
        NS_REFLECT_FIELD(m_idleClip, "立ちのクリップ")
        NS_REFLECT_FIELD(m_walkClip, "歩きのクリップ")
        NS_REFLECT_FIELD(m_runClip, "走りのクリップ")
        NS_REFLECT_FIELD(m_jumpClip, "跳ぶクリップ")
        NS_REFLECT_FIELD(m_fallClip, "落ちるクリップ")
        NS_REFLECT_FIELD(m_ledgeHangClip, "ぶら下がりのクリップ")
        NS_REFLECT_FIELD(m_runBlendRatio, "走りへ移る速さの比")
        NS_REFLECT_FIELD(m_minPlaybackSpeed, "再生速度の下限")
        NS_REFLECT_FIELD(m_spawnFloorTop, "補う床の上面")
        NS_REFLECT_FIELD(m_spawnClearance, "補う足元の余白")
        NS_REFLECT_FIELD(m_contactTolerance, "突進の接触探索の誤差")
        NS_REFLECT_FIELD(m_launchHeightTolerance, "突進の高さ探索の誤差")
        NS_REFLECT_FIELD(m_launchAngleGuardDegrees, "突進の角度計算の上限")
        NS_REFLECT_FIELD(m_launchMaxFrames, "突進の予測フレーム上限")
        NS_REFLECT_END()

    private:
        float m_spawnFloorTop = 0.5f;
        float m_spawnClearance = 0.01f;
        float m_contactTolerance = 0.001f;
        float m_launchHeightTolerance = LaunchPitchDesc{}.heightTolerance;
        float m_launchAngleGuardDegrees = LaunchPitchDesc{}.angleGuardDegrees;
        int m_launchMaxFrames = LaunchPath{}.maxFrames;
        friend class NS::Game::Level::ImpactResolver;
        friend class NS::Game::Level::SlamArrow;
        friend NS::Game::Level::ImpactTuning NS::Game::Level::MakeImpactTuning(const PlayerParams& params) noexcept;
        float m_reboundApexHeight = 1.15f;
        float m_reboundDistance = 0.575f;
        // 真ん中の当たりは、後ろのカメラの方へ低く速く弾き返す。手前へ来る動きは真後ろから大きくなって見え、
        // 弾き返されたと読める。距離 4.5 倍 (威力 2・質量 1 で 5.2 m) と高さ 0.6 倍 (頂点 1.4 m) で、
        // 真ん中だけの印の浮きは残す。前は 2 倍と 1 倍で、頂点 2.3 m・横の速さ秒速 2 m のほぼ真上の跳ねだった
        float m_centerHitReboundDistanceScale = 4.5f;
        float m_centerHitReboundHeightScale = 0.6f;
        // 外れは手応えが来ない「すかし」。勢いが相手に止められず、外した側へ滑って抜けていく。距離を 2.5 倍に
        // 伸ばして高さを下げ、浮かずに速く横へ抜けて、後ろからのカメラでどちらへ外したかが見える量にする
        float m_missReboundDistanceScale = 2.5f;
        // 外れは浮かせず、地面すれすれを滑って抜け、すぐ次を狙わせる。真ん中の 5% で、0.15 では反動の上りの
        // 弱い重力のせいで 0.2 m ほどの弧に 24 フレームかかり、ふわっと浮いて見えた
        float m_missReboundHeightRatio = 0.05f;
        // 下の縁の外れは地面へ叩きつけ、少し跳ねてこする。真下を向いた面で外れの高さの 4 割
        float m_missSlamBounce = 0.4f;
        // 箱の相手の面の読み方。見本の出発点 6 で、縁に沿った所は縁の向きへ真っすぐ、角の近くだけ斜めに逸れる
        float m_missBoxEdgeSharpness = 6.0f;
        // 外れの相手は低く短く飛んで地面を跳ねる。真ん中と同じ角度の弧だと「弾き飛ばした」に見えるので、
        // 距離を押し込む成分の 2 乗で縮めた上から、高さだけさらに 0.35 倍にして地面すれすれに出す
        float m_missLaunchHeightRatio = 0.35f;
        // 外れは手応えが来ない「すかし」。相手は触れた所から少しずれるだけにする。押し込む成分の 2 乗の上から
        // 0.1 倍で、端の外れは 1 m ほど、赤のすぐ外でも 3 m ほどしか動かず、画面の中に残る
        float m_missLaunchDistanceRatio = 0.1f;
        // 外れの着地からこすって止まり、操作が戻るまで。着いた速さに依らず同じフレーム数で戻り、身体で覚えられる
        // 0 はこすらずに、着いたフレームに立ちへ戻る。外れはすぐ次を狙えるよう 10
        int m_missSkidSteps = 10;
        // 速さ = 着いた速さ × (1 − 経過 ÷ フレーム数)^減り方。2 で、すぐ落ちて最後に少し擦れが残る
        float m_missSkidExponent = 2.0f;
        float m_launchDistance = 29.0f;
        float m_launchMassExponent = 0.35f;
        float m_launchApexHeight = 2.0f;
        float m_launchRiseGravity = 25.0f;
        float m_launchFallGravityScale = 1.4f;
        float m_launchApexBandSpeed = 1.0f;
        float m_launchApexBandGravityScale = 0.5f;
        float m_centerHitStopScale = 2.0f;
        float m_hitStopMaxSeconds = 12.0f / 60.0f;
        bool m_breakEnabled = false;
        float m_breakSpeedScale = 0.75f;
        float m_breakStopSeconds = 4.0f / 60.0f;
        // 回転は届くまでの回転数で持ち、回る速さは届くまでの秒から毎回出す。速さや距離を触っても回る数が変わらない
        // 通常突進 2 は本人の「到達まで一回転か 2, 3 回転」から、溜めた突進 5 は通常突進とはっきり違う数から始める
        float m_chargedSlamTurns = 5.0f; // 溜めた突進が届くまでに回る回転数
        float m_tapSlamTurns = 2.0f;     // 通常突進が届くまでに回る回転数
        // 反動は溜めて当てた時と通常突進で当てた時で変える。1 と 3 は 1〜3 の両端で、差が一番分かる組
        float m_chargedReboundTurns = 3.0f; // 溜めて当てた反動が着地までに回る回転数
        float m_tapReboundTurns = 1.0f;     // 通常突進で当てた反動が着地までに回る回転数
        friend class ::Player;
        float m_chargeThresholdSeconds = 0.2f;
        float m_chargeFullSeconds = 1.0f;
        // 溜めきりから押したままで勝手に出るまで。3 は本人の「3 秒ほど赤からゆっくりと紫に」から
        float m_overchargeSeconds = 3.0f;
        // 紫の揺れは相手の所の横のずれ (m) で持つ。角度で持つと、同じ揺れでも遠い相手ほど外れる
        // 2.2 は見本 overcharge-sway.html の値。真ん中の半幅 0.4 m に対し、振れきると外れまで届く
        float m_overchargeSwayMaxOffset = 2.2f;
        // 1 秒に振れる回数。紫の始めは遅く、紫の深さの 2 乗で終わりの速さへ上がる。見本の 0.8〜2.6 回
        float m_overchargeSwayStartRate = 0.8f;
        float m_overchargeSwayEndRate = 2.6f;
        // 狙う相手がいない時にずれを角度へ直す距離 (m)。10 は欄「突進距離」の既定で、矢印の先が最大のずれだけ振れる
        float m_overchargeSwayFallbackDistance = 10.0f;
        // 紫になりきった時に溜めきりの威力へ掛ける倍率。見本の 1.5
        float m_overchargePowerMax = 1.5f;
        float m_chargeSlowRate = 0.7f;
        NS::Obj::Curve m_chargeFactorCurve{};
        float m_chargeSquashScale = 0.95f;
        float m_pressSquashScale = 0.97f;
        std::string m_idleClip = "idle";
        std::string m_walkClip = "walk";
        std::string m_runClip = "run";
        std::string m_jumpClip{};
        std::string m_fallClip{};
        std::string m_ledgeHangClip{};
        float m_runBlendRatio = 0.4f;
        float m_minPlaybackSpeed = 0.5f;
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
        // 溜めて放つ突進を相手の赤の高さへ向ける角度の上限 (度)。上向きも下向きもこの角度で切る
        // 突進が高い所へ登る手段にならない所で止める。水平 20 m/s・上りの重力 -25 で上がれる高さは 45 度で約 8 m、
        // 40 度で約 5.6 m、30 度で約 2.6 m。2026-10-03 本人の指定で 40
        float m_launchPitchLimitDegrees = 40.0f;
        // 軽く当てても速く届かせる。溜めきりの 20 m/s の 4 分の 3 から始める。距離は変えないので届くまで約 0.42 秒
        float m_tapSlamSpeed = 15.0f;
        float m_tapSlamUpSpeed = 3.0f;
        float m_tapSlamDistance = 6.25f;
        float m_slamAimHoldTime = 0.11f;
        float m_slamAimFadeTime = 0.19f;
        float m_reboundRiseGravityScale = 0.5f;
        float m_reboundAirAcceleration = 2.0f;
    };
} // namespace NS::Game::Player
