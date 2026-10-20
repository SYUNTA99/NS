#pragma once

#include "Game/Level/HitTier.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace GL
{
    namespace Level
    {
        //! @brief 事象が起きる外れの向き。面の上の位置 (u, v) の絶対値の大きい方の軸と符号で決まる
        //! @details 番号はファイルに書かず、名前 (HitDirectionName) で書く。
        //! パッドの振動だけは、隣り合う 2 つの向きの行を位置の角度で混ぜて鳴らす (HitDirectionWeight)
        enum class HitDirection
        {
            Any,   //!< どの向きの当たりでも起きる
            Right, //!< 自機から見て右の外れ
            Left,  //!< 自機から見て左の外れ
            Up,    //!< 上の外れ
            Down,  //!< 下の外れ
        };

        //! @brief 自機の止め。始まりから長さの間、自機の状態と身体を止める
        struct HitStopEvent
        {
            std::string_view typeName = "HitStop"; //!< ファイルに書く種類の名前
            std::string_view label = "自機の止め"; //!< パネルに出す名前
            //! 触れる前 (マイナスのフレーム) に置けるか。当たりの結果 (相手・反動・威力) を読む種類は置けない
            bool beforeContact = false;
        };

        //! @brief 自機の描く形の倍率。始まりからのフレーム数を横軸にした曲線 2 本で、縮み・潰れ・伸びを 1
        //! 本の流れで持つ
        //! @details 突進の向きの倍率は、当たりの水平の向きの軸成分の 2 乗で x と z へ混ぜる。貫通の当たりでは起こさない
        struct ShapeEvent
        {
            std::string_view typeName = "Shape";
            std::string_view label = "自機の形";
            bool beforeContact = true; //!< 触れる前の縮みを置く

            NS::Curve along{};  //!< 突進の向きの倍率。元の形が 1
            NS::Curve height{}; //!< 高さの倍率。元の形が 1
            //! 突進の向きに直角な水平の横の倍率。元の形が 1。点が無ければ 1。真後ろのカメラから見える幅
            NS::Curve side{};

            NS_REFLECT_BEGIN(ShapeEvent, void)
            NS_FIELD(along, "突進の向きの倍率")
            NS_FIELD(height, "高さの倍率")
            NS_FIELD(side, "横の倍率")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 相手の止め。置かれていた相手を食い込ませ、長さの間だけ止めて形を縮める
        struct TargetFreezeEvent
        {
            std::string_view typeName = "TargetFreeze";
            std::string_view label = "相手の止め";
            bool beforeContact = false;

            float pushInDistance = 0.06f; //!< 止めの頭で飛ぶ向きへ食い込ませる距離 (m)
            float squashThickness = 0.7f; //!< 相手の飛ぶ向きの厚みの倍率
            float squashHeight = 1.1f;    //!< 相手の高さの倍率

            NS_REFLECT_BEGIN(TargetFreezeEvent, void)
            NS_FIELD(pushInDistance, "食い込み距離")
            NS_FIELD(squashThickness, "潰れの厚み")
            NS_FIELD(squashHeight, "潰れの伸び上がり")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 相手を飛ばす (貫通の当たりでは壊させる)。始まりのフレームに 1 回だけ起きる
        struct TargetLaunchEvent
        {
            std::string_view typeName = "TargetLaunch";
            std::string_view label = "相手を飛ばす";
            bool beforeContact = false;
        };

        //! @brief 自機の反動を始める (貫通の当たりでは突き抜ける速度を書く)。始まりのフレームに 1 回だけ起きる
        //! @details 自機は止めの始まりからこの事象まで止まったまま。この事象が無いタイムラインは止めの終わりで動き出す
        struct ReboundEvent
        {
            std::string_view typeName = "Rebound";
            std::string_view label = "反動を始める";
            bool beforeContact = false;
        };

        //! @brief カメラの揺れ。長さが揺れのフレーム数
        //! @details 最初の振れの大きさは 強さ × 威力 × 質量の効き で、横と縦の重みの向きへ分ける
        struct CameraShakeEvent
        {
            std::string_view typeName = "CameraShake";
            std::string_view label = "カメラの揺れ";
            bool beforeContact = false; //!< 振れは威力と質量で決まる

            float strength = 0.06f;    //!< 威力 1・質量の効き 1 の最初の振れの大きさ (m)
            float sideWeight = 0.0f;   //!< 振れの向きの横の重み
            float upWeight = 1.0f;     //!< 振れの向きの縦の重み
            int longestFlipFrames = 1; //!< 振れの向きが入れ替わるまでの最長フレーム数

            NS_REFLECT_BEGIN(CameraShakeEvent, void)
            NS_FIELD(strength, "強さ")
            NS_FIELD(sideWeight, "横の重み")
            NS_FIELD(upWeight, "縦の重み")
            NS_FIELD(longestFlipFrames, "入れ替わりの最長フレーム数")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 開始時に威力に応じたトラウマを加え、外した側へ一撃を振る
        //! @details 連続命中は加算する。揺れの長さは行の長さでなく decayPerSecond で決まる
        struct CameraTraumaEvent
        {
            std::string_view typeName = "CameraTrauma";
            std::string_view label = "トラウマの揺れ";
            bool beforeContact = false; //!< 量は威力、一撃の向きは面の上の位置で決まる

            float trauma = 0.9f;         //!< 威力 1 で足すトラウマ 0〜1
            float yawDegrees = 3.0f;     //!< トラウマ 1 の横の首振りの最大 (度)
            float pitchDegrees = 2.0f;   //!< トラウマ 1 の縦の首振りの最大 (度)
            float rollDegrees = 1.5f;    //!< トラウマ 1 の傾きの最大 (度)
            float frequency = 9.0f;      //!< ノイズの格子を 1 秒に進める数
            float decayPerSecond = 1.8f; //!< トラウマが 1 秒に減る量
            float exponent = 2.0f;       //!< トラウマを振れ幅にする指数
            float kickDegrees = 2.5f;    //!< 一撃の山の大きさ (度)
            int kickPeakFrames = 1;      //!< 一撃の山のフレーム。始まりのフレームを 1 と数える

            NS_REFLECT_BEGIN(CameraTraumaEvent, void)
            NS_FIELD(trauma, "トラウマ")
            NS_FIELD(yawDegrees, "横の首振り")
            NS_FIELD(pitchDegrees, "縦の首振り")
            NS_FIELD(rollDegrees, "傾き")
            NS_FIELD(frequency, "細かさ")
            NS_FIELD(decayPerSecond, "減る速さ")
            NS_FIELD(exponent, "指数")
            NS_FIELD(kickDegrees, "一撃の大きさ")
            NS_FIELD(kickPeakFrames, "一撃の山のフレーム")
            NS_REFLECT_END_VALUE()
        };

        //! @brief カメラの寄りと傾き。長さは保つフレーム数と戻すフレーム数の和
        struct ZoomRollEvent
        {
            std::string_view typeName = "ZoomRoll";
            std::string_view label = "寄りと傾き";
            bool beforeContact = false; //!< 傾きの向きは相手の飛ぶ向きで決まる

            float zoom = 1.15f;       //!< 寄りの倍率。1 は寄らない
            float rollDegrees = 3.0f; //!< 傾き (度)。正は画面の上端を、相手の飛ぶ向きがカメラの右なら右へ倒す
            int returnFrames = 6;     //!< 長さのうち、元へ戻すのに使う末尾のフレーム数

            NS_REFLECT_BEGIN(ZoomRollEvent, void)
            NS_FIELD(zoom, "寄りの倍率")
            NS_FIELD(rollDegrees, "傾き")
            NS_FIELD(returnFrames, "戻すフレーム数")
            NS_REFLECT_END_VALUE()
        };

        //! @brief パッドの振動。始まりからのフレーム数を横軸にした、左右のモーターの速さの曲線 2 本。長さの間だけ書く
        struct PadVibrationEvent
        {
            std::string_view typeName = "PadVibration";
            std::string_view label = "パッドの振動";
            bool beforeContact = false;

            NS::Curve left{};  //!< 左のモーターの速さ 0〜1
            NS::Curve right{}; //!< 右のモーターの速さ 0〜1

            NS_REFLECT_BEGIN(PadVibrationEvent, void)
            NS_FIELD(left, "左の速さ")
            NS_FIELD(right, "右の速さ")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 画面の白い光。長さの間に始めの濃さから直線で薄れる
        struct FlashEvent
        {
            std::string_view typeName = "Flash";
            std::string_view label = "白い光";
            bool beforeContact = false;

            float alpha = 0.5f; //!< 始めの濃さ 0〜1

            NS_REFLECT_BEGIN(FlashEvent, void)
            NS_FIELD(alpha, "濃さ")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 当たりのエフェクトを出す。始まりのフレームに 1 回だけ起きる
        struct HitEffectEvent
        {
            std::string_view typeName = "HitEffect";
            std::string_view label = "当たりのエフェクト";
            bool beforeContact = false; //!< 絵は当たりの記録から置く
        };

        //! @brief 相手の飛び出しのエフェクトを出す。始まりのフレームに 1 回だけ起きる
        struct FlightEffectEvent
        {
            std::string_view typeName = "FlightEffect";
            std::string_view label = "飛び出しのエフェクト";
            bool beforeContact = false;
        };

        //! @brief 段階的な明け。始まりのフレームに世界を遅くし、実時間で普段の速さへ戻す。始まりのフレームに 1
        //! 回だけ起きる
        struct GradualReleaseEvent
        {
            std::string_view typeName = "GradualRelease";
            std::string_view label = "段階的な明け";
            bool beforeContact = false;

            float startSpeed = 0.2f;    //!< 始まりの世界の速さ 0〜1
            float returnSeconds = 0.3f; //!< 普段の速さへ戻るまでの実時間 (秒)
            NS::Curve shape{}; //!< 戻り方。横軸は 0〜1 の経過の割合、縦軸は 0〜1 の戻りの割合。点が無ければ直線
            //! 溜めすぎて (紫で) 出した突進の当たりだけで遅くする場合 true。赤の溜めきりと通常突進では何もしない
            bool overchargedOnly = false;

            NS_REFLECT_BEGIN(GradualReleaseEvent, void)
            NS_FIELD(startSpeed, "始まりの速さ")
            NS_FIELD(returnSeconds, "戻る秒")
            NS_FIELD(shape, "戻り方")
            NS_FIELD(overchargedOnly, "紫の時だけ")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 画面を沈めて震わせ、同じタイムラインの反動開始時に跳ね返す
        //! @details 深さと震えは威力に応じて頭打ちになる。画素は高さ 1080 基準で、カメラの位置と向きは変えない
        struct CameraSinkEvent
        {
            std::string_view typeName = "CameraSink";
            std::string_view label = "沈む揺れ";
            bool beforeContact = false; //!< 深さは威力で決まる

            float maxPixels = 9.0f;      //!< 底の深さの頭打ち (画素)
            float powerBase = 1.0f;      //!< 頭打ちへ近づく速さを決める威力の基準。威力がこの値で頭打ちの約 63%
            int sinkFrames = 2;          //!< 底へ届くまでのフレーム数
            float tremblePixels = 5.0f;  //!< 底での震えの最初の大きさの頭打ち (画素)
            int trembleFrames = 6;       //!< 震えるフレーム数
            int tremblePeriodFrames = 2; //!< 震えの 1 往復のフレーム数
            float overshootRatio = 0.5f; //!< 跳ね返りで 0 を越えて上へ出る量の、底の深さに対する割合
            int bouncePeriodFrames = 8;  //!< 跳ね返りの 1 往復のフレーム数

            NS_REFLECT_BEGIN(CameraSinkEvent, void)
            NS_FIELD(maxPixels, "沈みの最大")
            NS_FIELD(powerBase, "威力の基準")
            NS_FIELD(sinkFrames, "沈むフレーム数")
            NS_FIELD(tremblePixels, "震えの大きさ")
            NS_FIELD(trembleFrames, "震えのフレーム数")
            NS_FIELD(tremblePeriodFrames, "震えの 1 往復")
            NS_FIELD(overshootRatio, "行き過ぎの割合")
            NS_FIELD(bouncePeriodFrames, "跳ね返りの 1 往復")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 長さの間、自機以外の世界を止める
        //! @details 対象は RunsWhileOthersHeld で決まる。自機・入力・カメラ・画面は更新を続ける
        struct OthersStopEvent
        {
            std::string_view typeName = "OthersStop";
            std::string_view label = "自機以外の止め";
            bool beforeContact = true; //!< 予測した当たりの前に置く
        };

        //! @brief 自機と相手の描く形を、床に沿う画面の横方向へ逆向きに揺らす
        //! @details 行の長さは使わず、当たりの止めに合わせて弱める。当たりの位置は動かさない
        struct BodyShakeEvent
        {
            std::string_view typeName = "BodyShake";
            std::string_view label = "横揺れ";
            bool beforeContact = false;

            //! 相手の最初の振れ幅。高さ 720
            //! 画素の画面の上の画素数で持ち、始まりのフレームにカメラとの距離から世界の長さへ直す
            float amplitudePixels = 10.0f;
            //! 自機の最初の振れ幅。画素の数え方は amplitudePixels と同じ
            float selfAmplitudePixels = 10.0f;
            //! 左右を入れ替えるフレーム数。1 未満は 1
            int flipFrames = 1;

            NS_REFLECT_BEGIN(BodyShakeEvent, void)
            NS_FIELD(amplitudePixels, "振れ幅の画素")
            NS_FIELD(selfAmplitudePixels, "自機の振れ幅の画素")
            NS_FIELD(flipFrames, "左右を入れ替えるフレーム数")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 相手の描く形を受けた力の向きへ、自機を逆向きへ押して戻す
        //! @details 行の長さは使わず、止めの最後まで揺らして明けに戻す。当たりの位置は動かさない
        struct DepthShakeEvent
        {
            std::string_view typeName = "DepthShake";
            std::string_view label = "奥揺れ";
            bool beforeContact = false; //!< 押す向きは当たりで決まる

            float depth = 1.2f;        //!< 相手を奥へ押す最初の幅 (m)
            float selfDepth = 0.8f;    //!< 自機を手前へ押す最初の幅 (m)
            int pushFrames = 4;        //!< 押し直すフレーム数。1 未満は 1
            float returnRatio = 0.45f; //!< 押してから 1 フレームごとにずれに掛ける倍率

            NS_REFLECT_BEGIN(DepthShakeEvent, void)
            NS_FIELD(depth, "相手の奥の幅")
            NS_FIELD(selfDepth, "自機の手前の幅")
            NS_FIELD(pushFrames, "押す間のフレーム数")
            NS_FIELD(returnRatio, "戻りの残り")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 止めの間は左右の残像を出し、明けには止めの最後の姿を薄めて残す
        //! @details 行の長さは使わず、当たりの止めに合わせる。明けの残像は releaseFrames だけ残る
        struct GhostEvent
        {
            std::string_view typeName = "Ghost";
            std::string_view label = "残像";
            bool beforeContact = false; //!< 写す二人は当たりで決まる

            //! 相手の左右の写しの最初の離れ。高さ 720 画素の画面の上の画素数で持ち、始まりのフレームに世界の長さへ直す
            float spreadPixels = 112.5f;
            float selfSpreadPixels = 35.0f; //!< 自機の左右の写しの最初の離れ。画素の数え方は spreadPixels と同じ
            float sideOpacity = 0.45f;      //!< 止めの間の左右の写しの不透明度。自機と相手で同じ値を使う
            int releaseFrames = 6;          //!< 明けの写しを残すフレーム数。0 以下は残さない
            float releaseOpacity = 0.45f;   //!< 明けの写しの最初の不透明度

            NS_REFLECT_BEGIN(GhostEvent, void)
            NS_FIELD(spreadPixels, "離れの画素")
            NS_FIELD(selfSpreadPixels, "自機の離れの画素")
            NS_FIELD(sideOpacity, "左右の残像の不透明度")
            NS_FIELD(releaseFrames, "明けの残像のフレーム数")
            NS_FIELD(releaseOpacity, "明けの残像の不透明度")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 衝突点から体の裏へ震えを伝える
        //! @details 行の長さは使わず、当たりの止めに合わせる。描く形だけを揺らし、当たりと根の位置は動かさない
        struct ImpactTremorEvent
        {
            std::string_view typeName = "ImpactTremor";
            std::string_view label = "衝撃の震え";
            bool beforeContact = false;

            //! 振れ幅。高さ 720 画素の画面の上の画素数で持ち、毎フレームその物とカメラの距離から世界の長さへ直す
            float amplitudePixels = 3.0f;
            //! 衝突点から体の一番遠い所へ届くまでのフレーム数。震えのフレーム数の半分を超える分は使わない
            int reachFrames = 6;
            //! 相手の振れ幅の、体の半径に対する割合。正なら相手だけ画素の欄の代わりに使う。0 は画素の欄のまま
            float targetAmplitudeRatio = 0.0f;

            float referenceHeight = 720.0f;
            NS_REFLECT_BEGIN(ImpactTremorEvent, void)
            NS_FIELD(amplitudePixels, "振れ幅の画素")
            NS_FIELD(reachFrames, "裏まで届くフレーム数")
            NS_FIELD(targetAmplitudeRatio, "相手の振れ幅の割合")
            NS_FIELD(referenceHeight, "画素寸法の基準の高さ")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 触れた点の真下から、上を向いた面へ光の輪を広げる
        //! @details 長さが表示フレーム数。曲線の横軸は開始からのフレーム数
        struct GroundWaveEvent
        {
            std::string_view typeName = "GroundWave";
            std::string_view label = "床の波";
            bool beforeContact = false; //!< 中心は触れた点で決まる

            NS::Curve radius{};   //!< 輪の半径 (m)。点が無ければ 0
            NS::Curve strength{}; //!< 輪の強さ。面の倒れと頂の光の量。点が無ければ 0 で出さない

            NS_REFLECT_BEGIN(GroundWaveEvent, void)
            NS_FIELD(radius, "半径")
            NS_FIELD(strength, "強さ")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 触れた点から画面に輪を広げ、輪の部分の絵を外へ押し出す
        //! @details 長さが表示フレーム数。曲線の横軸は開始からのフレーム数
        struct DistortionRingEvent
        {
            std::string_view typeName = "DistortionRing";
            std::string_view label = "歪みの輪";
            bool beforeContact = false; //!< 中心は触れた点で決まる

            NS::Curve radius{}; //!< 輪の半径。画面の高さに対する割合。点が無ければ 0
            NS::Curve push{};   //!< 輪の真ん中で絵を押し出す長さ。画面の高さに対する割合。点が無ければ 0 で歪めない
            float halfWidth = 0.05f; //!< 輪の半分の幅。画面の高さに対する割合

            NS_REFLECT_BEGIN(DistortionRingEvent, void)
            NS_FIELD(radius, "半径")
            NS_FIELD(push, "押し")
            NS_FIELD(halfWidth, "半分の幅")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 二人を囲む線を画面へ描き、横揺れと同じ拍で動かす
        //! @details 行の長さは使わず、当たりの止めに合わせる。範囲は開始時の二人の形で決まる
        struct ShakeLinesEvent
        {
            std::string_view typeName = "ShakeLines";
            std::string_view label = "震えの線";
            bool beforeContact = false; //!< 挟む相手は当たりで決まる

            float lengthPixels = 80.0f; //!< 内側の線の長さ。高さ 720 画素の画面の上の画素数
            float widthPixels = 7.0f;   //!< 線の太さ。画素の数え方は lengthPixels と同じ
            float gapPixels = 16.0f;    //!< 輪郭から内側の線までの間。画素の数え方は lengthPixels と同じ
            int flipFrames = 2;         //!< 外と内へずらし直すフレーム数。1 未満は 1

            NS_REFLECT_BEGIN(ShakeLinesEvent, void)
            NS_FIELD(lengthPixels, "線の長さの画素")
            NS_FIELD(widthPixels, "線の太さの画素")
            NS_FIELD(gapPixels, "輪郭から離す画素")
            NS_FIELD(flipFrames, "ずらし直すフレーム数")
            NS_REFLECT_END_VALUE()
        };

        //! @brief カメラの位置と注視点を、突進の水平の向きへ曲線の距離だけずらす
        //! @details 長さが表示フレーム数。向きと地平線は変えない
        struct CameraLurchEvent
        {
            std::string_view typeName = "CameraLurch";
            std::string_view label = "つんのめり";
            bool beforeContact = false; //!< 突進の向きは当たりの記録から読む

            NS::Curve distance{}; //!< 始まりからのフレーム数を横軸にした、ずらす距離 (m)。負は手前へ

            NS_REFLECT_BEGIN(CameraLurchEvent, void)
            NS_FIELD(distance, "ずらす距離")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 反動の画面方向へ、カメラの位置と注視点を曲線の距離だけずらす
        //! @details 長さが表示フレーム数。奥行き成分は捨て、画面方向が無ければ揺らさない。向きと地平線は保つ
        struct CameraReboundSwayEvent
        {
            std::string_view typeName = "CameraReboundSway";
            std::string_view label = "反動の揺れ";
            bool beforeContact = false; //!< 反動の向きは当たりで決まる

            NS::Curve distance{}; //!< 始まりからのフレーム数を横軸にした、ずらす距離 (m)。負は反動と逆の向き

            NS_REFLECT_BEGIN(CameraReboundSwayEvent, void)
            NS_FIELD(distance, "ずらす距離")
            NS_REFLECT_END_VALUE()
        };

        //! @brief 事象の種類ごとの値。種類を足す時はここへ型を足す
        using HitEventValue = std::variant<HitStopEvent,
                                           ShapeEvent,
                                           TargetFreezeEvent,
                                           TargetLaunchEvent,
                                           ReboundEvent,
                                           CameraShakeEvent,
                                           ZoomRollEvent,
                                           PadVibrationEvent,
                                           FlashEvent,
                                           HitEffectEvent,
                                           FlightEffectEvent,
                                           GradualReleaseEvent,
                                           CameraTraumaEvent,
                                           OthersStopEvent,
                                           CameraSinkEvent,
                                           BodyShakeEvent,
                                           DepthShakeEvent,
                                           GhostEvent,
                                           ImpactTremorEvent,
                                           CameraLurchEvent,
                                           CameraReboundSwayEvent,
                                           ShakeLinesEvent,
                                           GroundWaveEvent,
                                           DistortionRingEvent>;

        //! @brief タイムラインの 1 行。触れたフレームを 0 にしたフレーム数で、始まりと長さを持つ
        struct HitEvent
        {
            HitEventValue value{};                      //!< 種類と、その種類の値
            int start = 0;                              //!< 始まりのフレーム。触れる前はマイナス
            int length = 1;                             //!< 長さ (フレーム数)。0 以上
            HitDirection direction = HitDirection::Any; //!< 起きる外れの向き
        };

        //! @brief 当たり方 1 つの事象の並び。同じフレームに始まる事象は並びの順に起きる
        struct HitTimeline
        {
            int version = 1;
            std::vector<HitEvent> events;
        };

        [[nodiscard]] std::string_view HitEventTypeName(const HitEventValue& value) noexcept;

        [[nodiscard]] std::string_view HitEventLabel(const HitEventValue& value) noexcept;

        [[nodiscard]] bool CanStartBeforeContact(const HitEventValue& value) noexcept;

        //! @brief 種類の名前から、欄が全部既定値のその種類の値を作る
        //! @return 知らない名前の場合は std::nullopt
        [[nodiscard]] std::optional<HitEventValue> MakeHitEventValue(std::string_view typeName);

        //! value の種類のリフレクション。欄の無い種類は nullptr
        [[nodiscard]] const NS::ReflectionInfo* HitEventReflection(const HitEventValue& value) noexcept;

        //! value の今の種類の値を指す番地。HitEventReflection の欄を読み書きする時に渡す
        [[nodiscard]] void* HitEventFields(HitEventValue& value) noexcept;
        [[nodiscard]] const void* HitEventFields(const HitEventValue& value) noexcept;

        //! 絶対値の大きい軸の符号で外れの向きを決める。同値なら左右。u は右、v は上が正
        [[nodiscard]] HitDirection HitDirectionOf(float u, float v) noexcept;

        //! 位置の角度で隣り合う向きの重みを直線補間する。u は右、v は上が正
        //! 4 方向の重みの和は 1。原点では HitDirectionOf の向きだけ 1、Any は常に 1
        [[nodiscard]] float HitDirectionWeight(HitDirection direction, float u, float v) noexcept;

        [[nodiscard]] std::string_view HitDirectionName(HitDirection direction) noexcept;

        //! @brief ファイルに書く名前から向きを引く
        //! @return 知らない名前の場合は std::nullopt
        [[nodiscard]] std::optional<HitDirection> ParseHitDirection(std::string_view name) noexcept;

        //! JSON を読む。未知の欄は警告して読み飛ばす
        //! 版・形式・種類が不正なら std::nullopt を返し、error に理由を書く
        [[nodiscard]] std::optional<HitTimeline> ParseHitTimeline(const nlohmann::json& doc, std::string& error);

        //! timeline をファイルの JSON の形 {"version": 1, "events": [...]} へ書き出す
        [[nodiscard]] nlohmann::json HitTimelineToJson(const HitTimeline& timeline);

        //! @brief 段のタイムラインのファイル名 (拡張子を除く)
        //! @return 真ん中は "center"、外れは "miss"。番号から作った段の外の値は空
        [[nodiscard]] std::string_view HitTimelineNameOf(HitTier tier) noexcept;

        //! ファイル名に使える場合 true、それ以外の場合は false
        //! 空・ドット・禁止文字・前後の空白・予約名を拒否する
        [[nodiscard]] bool IsValidHitTimelineName(std::string_view name) noexcept;

        //! @brief 相手の名前と段のタイムラインのファイル名を拡張子なしで返す
        //! @return 名前が空なら段の名前、それ以外は <名前>.<段の名前>
        [[nodiscard]] std::string HitTimelineFileName(std::string_view name, HitTier tier);

        //! @brief 名前ごとに Directory()/<名前>.json を管理するタイムライン集
        //! @details 初回検索時に全ファイルを読む。不正なファイルはエラーを一度出し、保持しない
        class HitTimelineLibrary
        {
        public:
            [[nodiscard]] static HitTimelineLibrary& Get();

            [[nodiscard]] const std::string& Directory();

            //! ディレクトリを差し替えて読み直す。試しは一時ディレクトリへ向ける
            void SetDirectory(std::string directory);

            //! Directory() の *.json を全て読み直す
            void Reload();

            //! name のタイムライン。無いか壊れていれば nullptr
            [[nodiscard]] const HitTimeline* Find(std::string_view name);

            //! @brief tier の段のタイムライン
            //! @details 無いか壊れている段、段の外の値は nullptr で、段ごとに 1 回だけエラーを出す
            [[nodiscard]] const HitTimeline* FindForTier(HitTier tier);

            //! <名前>.<段名> があれば優先し、無ければ段名を使う。空の名前も段名を使う
            //! 名前に対応するファイルが一つも無ければ、名前ごとに一度エラーを出す
            [[nodiscard]] std::string NameFor(std::string_view name, HitTier tier);

            //! @brief NameFor の名前のタイムライン
            //! @details 段の名前に戻った時は FindForTier と同じ
            [[nodiscard]] const HitTimeline* FindFor(std::string_view name, HitTier tier);

            //! 読んだ全部のタイムラインの事象の一番早い始まり。0 より早い物が無ければ 0
            [[nodiscard]] int EarliestStart();

            [[nodiscard]] std::vector<std::string> TargetNames();

            //! 読んだ全部のタイムラインのファイル名の並び。拡張子は含まない
            [[nodiscard]] std::vector<std::string> FileNames();

            //! name のタイムラインを差し替える。ファイルへは書かない
            void Set(std::string_view name, HitTimeline timeline);

            //! name のタイムラインを Directory() へ書く。無いか書けなければ false
            [[nodiscard]] bool Save(std::string_view name);

        private:
            HitTimelineLibrary() = default;
            void EnsureLoaded();

            std::size_t m_maxFileBytes = 1024 * 1024;
            std::string m_directory; // 空なら既定のディレクトリ
            std::map<std::string, HitTimeline, std::less<>> m_timelines;
            std::set<int> m_reportedTiers;                      // 引けないとエラーを出した段の番号
            std::set<std::string, std::less<>> m_reportedNames; // 1 つも無いとエラーを出した相手の名前
            bool m_loaded = false;
        };
    } // namespace Level
} // namespace GL
