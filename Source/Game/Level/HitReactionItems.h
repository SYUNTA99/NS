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
        //! 持ち主の描く形の倍率。縮み・潰れ・伸びを 1 本の流れで持つ
        //! 向きの倍率は 2 つのセンサーを結ぶ水平の向きで x と z へ分ける。同じ持ち主の前の形は差し替える
        struct HitShapeInfo
        {
            std::string_view typeName = "Shape"; // ファイルに書く種類の名前
            std::string_view label = "描く形";   // 窓とインスペクタに出す名前

            void Start(const NS::HitReactionStart& start) const;

            NS::Curve along{};  // 相手へ向かう向きの倍率。元の形が 1
            NS::Curve height{}; // 高さの倍率。元の形が 1
            NS::Curve side{};   // 相手へ向かう向きに直角な水平の横の倍率。点が無ければ 1

            NS_REFLECT_BEGIN(HitShapeInfo, void)
            NS_FIELD(along, "突進の向きの倍率")
            NS_FIELD(height, "高さの倍率")
            NS_FIELD(side, "横の倍率")
            NS_REFLECT_END_VALUE()
        };

        //! 持ち主の描く形を、床に沿う画面の横方向へ揺らす。最初の振れは相手と逆の側。同じ持ち主の前の揺れは差し替える
        struct HitBodyShakeInfo
        {
            std::string_view typeName = "BodyShake";
            std::string_view label = "横揺れ";

            void Start(const NS::HitReactionStart& start) const;

            //! 最初の振れ幅。高さ 720 ピクセルの画面の上のピクセル数で持つ
            //! 始めたフレームに、カメラとの距離からゲームの中の距離を出す
            float amplitudePixels = 10.0f;
            int flipFrames = 1;           // 左右を入れ替えるフレーム数。1 未満は 1
            float amplitudeSpread = 0.3f; // 振れ幅のばらつきの割合。フレームごとに振れ幅へ (1 − これ) 〜 1 を掛ける

            NS_REFLECT_BEGIN(HitBodyShakeInfo, void)
            NS_FIELD(amplitudePixels, "振れ幅のピクセル")
            NS_FIELD(flipFrames, "左右を入れ替えるフレーム数")
            NS_FIELD(amplitudeSpread, "振れ幅のばらつき")
            NS_REFLECT_END_VALUE()
        };

        //! 渡した位置から持ち主の体の裏へ震えを伝える。描く形だけを揺らし、当たりと根の位置は動かさない
        //! 同じ持ち主の前の震えは差し替える
        struct HitBodyTremorInfo
        {
            std::string_view typeName = "BodyTremor";
            std::string_view label = "衝撃の震え";

            void Start(const NS::HitReactionStart& start) const;

            //! 振れ幅。基準の高さの画面の上のピクセル数で持ち、毎フレームカメラとの距離からゲームの中の距離を出す
            float amplitudePixels = 3.0f;
            //! 渡した位置から体の一番遠い所へ届くまでのフレーム数。長さの半分を超える分は使わない
            int reachFrames = 6;
            //! 振れ幅の、体の半径に対する割合。正ならピクセルの欄の代わりに使う
            float radiusRatio = 0.0f;
            float referenceHeight = 720.0f; // ピクセルの欄を数える画面の高さ

            NS_REFLECT_BEGIN(HitBodyTremorInfo, void)
            NS_FIELD(amplitudePixels, "振れ幅のピクセル")
            NS_FIELD(reachFrames, "裏まで届くフレーム数")
            NS_FIELD(radiusRatio, "体の半径に対する振れ幅の割合")
            NS_FIELD(referenceHeight, "ピクセルを数える画面の高さ")
            NS_REFLECT_END_VALUE()
        };

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

        //! 画面の白い光 (NS::Scene::ScreenFlash)。長さの間に始めの濃さから直線で薄れる
        struct HitFlashInfo
        {
            std::string_view typeName = "Flash";
            std::string_view label = "白い光";

            void Start(const NS::HitReactionStart& start) const;

            float alpha = 0.5f; // 始めの濃さ 0〜1

            NS_REFLECT_BEGIN(HitFlashInfo, void)
            NS_FIELD(alpha, "濃さ")
            NS_REFLECT_END_VALUE()
        };

        //! 渡した位置から画面に輪を広げ、輪の部分の絵を外へ押し出す。長さが出すフレーム数
        //! GameScene の HitScreenDirector が出す。HitReaction::Stop と ScaleRunning は輪に触らない
        struct HitDistortionRingInfo
        {
            std::string_view typeName = "DistortionRing";
            std::string_view label = "歪みの輪";

            void Start(const NS::HitReactionStart& start) const;

            NS::Curve radius{};      // 輪の半径。画面の高さに対する割合。点が無ければ 0
            NS::Curve push{};        // 輪の真ん中で絵を押し出す長さ。画面の高さに対する割合。点が無ければ歪めない
            float halfWidth = 0.05f; // 輪の半分の幅。画面の高さに対する割合

            NS_REFLECT_BEGIN(HitDistortionRingInfo, void)
            NS_FIELD(radius, "半径")
            NS_FIELD(push, "押し")
            NS_FIELD(halfWidth, "半分の幅")
            NS_REFLECT_END_VALUE()
        };

        //! 渡した位置の真下から、上を向いた面へ光の輪を広げる。長さが出すフレーム数
        //! GameScene の HitScreenDirector が出す。HitReaction::Stop と ScaleRunning は波に触らない
        struct HitGroundWaveInfo
        {
            std::string_view typeName = "GroundWave";
            std::string_view label = "床の波";

            void Start(const NS::HitReactionStart& start) const;

            NS::Curve radius{};   // 輪の半径 (m)。点が無ければ 0
            NS::Curve strength{}; // 輪の強さ。面の倒れと頂の光の量。点が無ければ出さない

            NS_REFLECT_BEGIN(HitGroundWaveInfo, void)
            NS_FIELD(radius, "半径")
            NS_FIELD(strength, "強さ")
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
