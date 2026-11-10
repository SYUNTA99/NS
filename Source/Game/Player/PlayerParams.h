#pragma once

#include "Game/Level/AirGravity.h"
#include "NSlib/CoreObject/Object.h"

#include <algorithm>

class PlayerActor;

namespace GL
{
    namespace Player
    {
        //! 自機の移動・エアダッシュ・反動の調整値を持つ部品
        class PlayerParams : public NS::Object
        {
        public:
            PlayerParams() noexcept;

            //! 欄「体力」の値。1 未満は 1 として返す
            [[nodiscard]] int MaxHealth() const noexcept { return std::max(m_maxHealth, 1); }
            //! スティックの倒しがこの値未満なら入力なしとして扱う
            [[nodiscard]] float StickDeadzone() const noexcept { return m_stickDeadzone; }
            //! 欄「登れる坂の上限」をラジアンにした値。物理の Body::Move と、坂かを判定する IsWalkableNormal へ渡す
            [[nodiscard]] float MaxSlopeRadians() const noexcept
            {
                return NS::ToRadians(NS::Degrees{m_maxSlopeDegrees}).value;
            }
            //! 欄「反動で面から離れる間の登れる坂の上限」をラジアンにした値。物理の Body::Move へ渡す
            [[nodiscard]] float ReboundLeaveMaxSlopeRadians() const noexcept
            {
                return NS::ToRadians(NS::Degrees{m_reboundLeaveMaxSlopeDegrees}).value;
            }
            //! 入力方向への水平速度成分がこの値未満ならブレーキに入る
            [[nodiscard]] float BrakeThreshold() const noexcept { return m_brakeThreshold; }
            //! 欄「上昇重力」「下降重力」「頂点滞空 Vy」「頂点滞空倍率」の写し。重力の強さを選ぶ ChooseGravity へ渡す
            [[nodiscard]] GL::Level::AirGravity Gravity() const noexcept
            {
                return GL::Level::AirGravity{.rise = m_gravityUp,
                                             .fall = m_gravityDown,
                                             .apexSpeed = m_apexHangVy,
                                             .apexScale = m_apexHangScale};
            }

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
            NS_FIELD(m_coyoteFrames, "コヨーテのフレーム数")
            NS_FIELD(m_jumpBufferFrames, "先行入力のフレーム数")
            NS_GROUP("移動")
            NS_FIELD(m_walkSpeed, "歩き速度")
            NS_FIELD(m_runSpeed, "走行速度")
            NS_FIELD(m_acceleration, "加速度")
            NS_FIELD(m_airAcceleration, "空中の加速度")
            NS_FIELD(m_turningDrag, "曲がる時の抵抗")
            NS_FIELD(m_airControlReturnFrames, "ダッシュと反動の後に空中の操作が戻るフレーム数")
            NS_FIELD(m_friction, "手を放した時の減速度")
            NS_FIELD(m_deceleration, "ブレーキの減速度")
            NS_FIELD(m_brakeThreshold, "ブレーキのしきい値")
            NS_FIELD(m_stickDeadzone, "スティック遊び")
            NS_FIELD(m_turnSpeed, "振り向きの速さ")
            NS_GROUP("段と縁")
            NS_FIELD(m_maxStepHeight, "登れる段の高さ")
            NS_FIELD(m_maxSlopeDegrees, "登れる坂の上限", NS::FieldRange{0.0f, 90.0f})
            NS_FIELD(m_reboundLeaveMaxSlopeDegrees, "反動で面から離れる間の登れる坂の上限", NS::FieldRange{0.0f, 90.0f})
            NS_FIELD(m_ledgeGrabBelowHand, "掴める縁の下向き距離")
            NS_FIELD(m_ledgeReach, "縁へ手を伸ばす距離")
            NS_FIELD(m_ledgeHoldAboveHand, "掴まっている縁の上向きの許し")
            NS_FIELD(m_ledgeClimbFrames, "よじ登りのフレーム数")
            NS_FIELD(m_ledgeShimmySpeed, "縁の横移動速度")
            NS_GROUP("エアダッシュ")
            NS_FIELD(m_airDashDistance, "エアダッシュの距離", NS::FieldRange::Positive())
            NS_FIELD(m_airDashFrames, "エアダッシュのフレーム数")
            NS_FIELD(m_airDashExitSpeed, "エアダッシュ後に残る速さ")
            NS_GROUP("地形の反動")
            NS_FIELD(m_reboundTableDivisions, "向きの表の分割数")
            NS_FIELD(m_reboundTableWithPitch, "表に傾きの段を入れる")
            NS_FIELD(m_terrainReboundSpeedScale, "返る速さのダッシュの速さに対する倍率")
            NS_FIELD(m_reboundScreenTieDegrees, "画面で同じに見える角度の差", NS::FieldRange::NonNegative())
            NS_FIELD(m_reboundDepthSlackDegrees, "奥と手前の上限の余裕の角度")
            NS_FIELD(m_reboundMatchVisibleLength, "合ったとみなす、画面に映る長さの下限", NS::FieldRange::NonNegative())
            NS_FIELD(m_reboundMatchedSpeedScale, "向きが合った時の返る速さの倍率", NS::FieldRange::Positive())
            NS_FIELD(m_reboundShiftedEffectScale, "ずらした時の演出の強さの倍率", NS::FieldRange::NonNegative())
            NS_FIELD(m_reboundMinFacing, "反動に数える当て方の下限")
            NS_FIELD(m_reboundFullFacing, "全部返ったに数える当て方の下限")
            NS_FIELD(m_reboundStopMinFrames, "反動の止めの最短のフレーム数")
            NS_FIELD(m_reboundStopMaxFrames, "反動の止めの上限のフレーム数")
            NS_FIELD(m_reboundStopStickChange, "スティックが動いているとみなす変化", NS::FieldRange::NonNegative())
            NS_FIELD(m_reboundLockFrames, "反動の操作なしのフレーム数")
            NS_FIELD(m_reboundSteerEndFrames, "反動の補正が終わるフレーム数")
            NS_FIELD(m_dashReboundGravity, "地形の反動の重力")
            NS_FIELD(m_dashReboundDeceleration, "地形の反動の減速度")
            NS_FIELD(m_reboundSteerLimitRatio, "曲げの上限の段の割合")
            NS_FIELD(m_reboundSteerMaxDegreesPerSecond, "曲げの最大の角速度")
            NS_PROTECTED_FIELD(m_drawReboundLines, "反動の線を描く")
            NS_FIELD(m_reboundLineLength, "反動の線の長さ")
            NS_REFLECT_END()

        private:
            friend class ::PlayerActor;
            int m_maxHealth = 8;
            float m_jumpImpulse = 12.0f;
            float m_gravityUp = -25.0f;
            float m_gravityDown = -35.0f;
            float m_apexHangVy = 1.0f;
            float m_apexHangScale = 0.5f;
            float m_jumpReleaseScale = 0.6f;
            int m_coyoteFrames = 1;      // 足場を離れても跳べる、宙のフレーム数
            int m_jumpBufferFrames = 15; // 着地の前に押した跳ぶ入力を覚えておくフレーム数
            float m_walkSpeed = 4.0f;
            float m_runSpeed = 8.0f;
            float m_acceleration = 40.0f;
            float m_airAcceleration = 40.0f;
            float m_turningDrag = 40.0f;
            // ダッシュと反動の後の落下で、空中の加速度と曲がる時の抵抗が 0 から全部へ戻るまでのフレーム数
            int m_airControlReturnFrames = 30;
            float m_friction = 40.0f;
            float m_deceleration = 40.0f;
            float m_brakeThreshold = -0.8f;
            float m_stickDeadzone = 0.3f;
            float m_maxStepHeight = 0.25f;
            // これより急な面は、歩いても壁と同じく遮り、反動の続きで当たっても跳ね返る (度)
            float m_maxSlopeDegrees = 40.0f;
            float m_reboundLeaveMaxSlopeDegrees = 85.0f; // 垂直の壁は登れる面に数えない
            float m_ledgeGrabBelowHand = 0.5f;
            float m_ledgeReach = 0.3f;
            // 掴まっている間に測り直す上面が、手より上に出てよい高さ (m)。撃った線の誤差で手を放さないための余白
            float m_ledgeHoldAboveHand = 0.01f;
            int m_ledgeClimbFrames = 15;
            float m_ledgeShimmySpeed = 2.0f;
            float m_turnSpeed = 970.0f;
            float m_airDashDistance = 4.4f;   // m。当たらなければ、m_airDashFrames の歩でこの距離を進む
            int m_airDashFrames = 29;         // 当たらなかったダッシュが終わるまでの状態の歩の数
            float m_airDashExitSpeed = 8.0f;  // 当たらずに終わったフレームに落とす速さ (m/s)
            int m_reboundTableDivisions = 8;  // 8・12・16 のうち一番近い物へ丸める
            bool m_reboundTableWithPitch = true; // 偽なら縦・横・奥の 3 枚の円の表
            float m_terrainReboundSpeedScale = 1.0f; // ダッシュで当てた時に返る速さ。返った後に続けて当たる時は掛け直さない
            float m_reboundScreenTieDegrees = 10.0f;   // 画面の角度の差がこれ以内なら、入力が返る向きと合ったとみなす
            float m_reboundDepthSlackDegrees = 10.0f;  // 奥と手前へ飛ぶ量の上限に足す、カメラのずれの余裕
            // 返る向きを画面の右と上へ映した長さがこれより短いと、入力と合ったとみなさずに入力の側へ曲げる
            float m_reboundMatchVisibleLength = 0.5f;
            float m_reboundMatchedSpeedScale = 1.2f;   // 入力が返る向きと合った時だけ、当てた速さに掛ける
            float m_reboundShiftedEffectScale = 0.7f;  // 入力補正で向きを変えた時に、走っている演出に掛ける
            // 当て方は進む向きと面の法線の内積の大きさ。下限より浅い当たりは掃引の滑りに任せる
            float m_reboundMinFacing = 0.2f;
            float m_reboundFullFacing = 0.9f; // これ以上なら強い演出の束を始める
            // 地形に当たったフレームを入れて数える止め。止めが明けたフレームのスティックで反動の向きを選ぶ
            int m_reboundStopMinFrames = 3;
            int m_reboundStopMaxFrames = 12;      // スティックが動いている間は、ここまで止めを延ばす
            // 1 フレームの左スティックの位置の差がこれより大きいと、動いているとみなす。止めの延長と、押したフレームのエアダッシュの向きに使う
            float m_reboundStopStickChange = 0.05f;
            int m_reboundLockFrames = 9;      // 反動を始めてから操作が戻るまで
            int m_reboundSteerEndFrames = 36; // 反動を始めてから補正の段が終わるまで
            float m_dashReboundGravity = 30.0f;     // 補正の段の重力 (m/s²)
            float m_dashReboundDeceleration = 6.0f; // 補正の段の水平の減速度 (m/s²)
            float m_reboundSteerLimitRatio = 0.45f; // 曲げの累計の上限を 1 段の角度に対する割合で。0.5 未満へ丸める
            float m_reboundSteerMaxDegreesPerSecond = 120.0f; // 補正の段の終わりに届く曲げの角速度
            bool m_drawReboundLines = true;
            float m_reboundLineLength = 2.0f; // 開発用の向きの線の長さ (m)
        };
    } // namespace Player
} // namespace GL
