#pragma once

#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"

#include <string_view>

// Recoilot のために作った当たりの反応の項目。NSlib の束 (NS::HitReaction) が名前で引き、束を始めたフレームに Start を呼ぶ
// 曲線の横軸は項目が始まったフレームを 0 にしたフレーム数

namespace GL
{
    namespace Level
    {
        //! 画面を沈めて震わせる。同じ持ち主の「沈みを戻す」で跳ね返り、戻されなければ長さで終わる
        //! カメラの位置と向きは変えない
        struct HitCameraSinkInfo
        {
            std::string_view typeName = "CameraSink";
            std::string_view label = "沈む揺れ";

            void Start(const NS::HitReactionStart& start) const;

            float bottomPixels = 9.0f;   // 底の深さ (ピクセル)
            int sinkFrames = 2;          // 底へ届くまでのフレーム数
            float tremblePixels = 5.0f;  // 底での震えの最初の大きさ (ピクセル)
            int trembleFrames = 6;       // 震えるフレーム数
            int tremblePeriodFrames = 2; // 震えの 1 往復のフレーム数
            float overshootRatio = 0.5f; // 跳ね返りで 0 を越えて上へ出る量の、底の深さに対する割合
            int bouncePeriodFrames = 8;  // 跳ね返りの 1 往復のフレーム数
            float referenceHeightPixels = 1080.0f; // ピクセルを数える画面の高さ

            NS_REFLECT_BEGIN(HitCameraSinkInfo, void)
            NS_FIELD(bottomPixels, "底の深さのピクセル")
            NS_FIELD(sinkFrames, "沈むフレーム数")
            NS_FIELD(tremblePixels, "震えの大きさ")
            NS_FIELD(trembleFrames, "震えのフレーム数")
            NS_FIELD(tremblePeriodFrames, "震えの 1 往復")
            NS_FIELD(overshootRatio, "行き過ぎの割合")
            NS_FIELD(bouncePeriodFrames, "跳ね返りの 1 往復")
            NS_FIELD(referenceHeightPixels, "ピクセルを数える画面の高さ")
            NS_REFLECT_END_VALUE()
        };

        //! 同じ持ち主が先に始めた沈む揺れを跳ね返す。新しい揺れは始めず、沈みが無ければ何もしない
        struct HitCameraSinkReleaseInfo
        {
            std::string_view typeName = "CameraSinkRelease";
            std::string_view label = "沈みを戻す";

            void Start(const NS::HitReactionStart& start) const;
        };

        //! カメラの寄りと傾き。項目の長さは保つフレーム数と戻すフレーム数の和
        struct HitZoomRollInfo
        {
            std::string_view typeName = "ZoomRoll";
            std::string_view label = "寄りと傾き";

            void Start(const NS::HitReactionStart& start) const;

            float zoom = 1.15f;       // 寄りの倍率。1 は寄らない
            float rollDegrees = 3.0f; // 傾き (度)。正は画面の上端を、相手がカメラの右なら右へ倒す
            int returnFrames = 6;     // 長さのうち、元へ戻すのに使う末尾のフレーム数
            //! 束を始めてから寄せ始めるまでのフレーム数。止めの間も数える
            int delayFrames = 0;

            NS_REFLECT_BEGIN(HitZoomRollInfo, void)
            NS_FIELD(zoom, "寄りの倍率")
            NS_FIELD(rollDegrees, "傾き")
            NS_FIELD(returnFrames, "戻すフレーム数")
            NS_FIELD(delayFrames, "始めるまでのフレーム数")
            NS_REFLECT_END_VALUE()
        };

        //! カメラの位置と注視点を、相手へ向かう水平の向きへ曲線の距離だけずらす。向きと地平線は変えない
        struct HitCameraLurchInfo
        {
            std::string_view typeName = "CameraLurch";
            std::string_view label = "つんのめり";

            void Start(const NS::HitReactionStart& start) const;

            NS::Curve distance{}; // ずらす距離 (m)。負は手前へ

            NS_REFLECT_BEGIN(HitCameraLurchInfo, void)
            NS_FIELD(distance, "ずらす距離")
            NS_REFLECT_END_VALUE()
        };

        //! カメラの位置と注視点を、相手から離れる向きの画面方向へ曲線の距離だけずらす
        //! 奥行きの成分は捨て、画面方向が無ければ揺らさない。向きと地平線は変えない
        struct HitCameraAwaySwayInfo
        {
            std::string_view typeName = "CameraAwaySway";
            std::string_view label = "離れる向きの揺れ";

            void Start(const NS::HitReactionStart& start) const;

            NS::Curve distance{}; // ずらす距離 (m)。負は相手へ向かう向き
            //! 画面へ写した向きの長さがこれ以下なら揺らさない。向きが画面の奥をほぼ真っすぐ指すと、画面の上の向きが決まらない
            float minScreenShare = 0.2f;

            NS_REFLECT_BEGIN(HitCameraAwaySwayInfo, void)
            NS_FIELD(distance, "ずらす距離")
            NS_FIELD(minScreenShare, "画面の向きの長さの下限")
            NS_REFLECT_END_VALUE()
        };

        //! 長さの間、持ち主以外の世界を止める (NS::Scene::RequestStopScene)。始めた歩の次の歩から止まる
        struct HitOthersStopInfo
        {
            std::string_view typeName = "OthersStop";
            std::string_view label = "持ち主以外の止め";

            void Start(const NS::HitReactionStart& start) const;
        };
    } // namespace Level
} // namespace GL
