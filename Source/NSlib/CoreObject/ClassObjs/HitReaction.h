#pragma once

#include "NSlib/CoreObject/ClassObjs/CameraModifier.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/System/HitScreenDirector.h"
#include "NSlib/CoreObject/System/PadRumbleDirector.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace NS
{
    // 名前の束と動作のタイムラインが使う項目の型。曲線の横軸は項目が始まったフレームを 0 にしたフレーム数。
    // 向きを使う項目は、束を始める時に渡す 2 つのセンサーの位置から向きを出す

    //! 持ち主の描く形の倍率。縮み・潰れ・伸びを 1 本の流れで持つ
    //! 向きの倍率は 2 つのセンサーを結ぶ水平の向きで x と z へ分ける
    struct HitShapeItem
    {
        std::string_view typeName = "Shape"; // ファイルに書く種類の名前
        std::string_view label = "描く形";   // 窓とインスペクタに出す名前

        Curve along{};  // 相手へ向かう向きの倍率。元の形が 1
        Curve height{}; // 高さの倍率。元の形が 1
        Curve side{};   // 相手へ向かう向きに直角な水平の横の倍率。点が無ければ 1

        NS_REFLECT_BEGIN(HitShapeItem, void)
        NS_FIELD(along, "突進の向きの倍率")
        NS_FIELD(height, "高さの倍率")
        NS_FIELD(side, "横の倍率")
        NS_REFLECT_END_VALUE()
    };

    //! 持ち主の描く形を、床に沿う画面の横方向へ揺らす。最初の振れは相手と逆の側
    struct HitBodyShakeItem
    {
        std::string_view typeName = "BodyShake";
        std::string_view label = "横揺れ";

        //! 最初の振れ幅。高さ 720 ピクセルの画面の上のピクセル数で持ち、始めたフレームにカメラとの距離からゲームの中の距離を出す
        float amplitudePixels = 10.0f;
        int flipFrames = 1; // 左右を入れ替えるフレーム数。1 未満は 1

        NS_REFLECT_BEGIN(HitBodyShakeItem, void)
        NS_FIELD(amplitudePixels, "振れ幅のピクセル")
        NS_FIELD(flipFrames, "左右を入れ替えるフレーム数")
        NS_REFLECT_END_VALUE()
    };

    //! 渡した位置から持ち主の体の裏へ震えを伝える。描く形だけを揺らし、当たりと根の位置は動かさない
    struct HitBodyTremorItem
    {
        std::string_view typeName = "BodyTremor";
        std::string_view label = "衝撃の震え";

        //! 振れ幅。基準の高さの画面の上のピクセル数で持ち、毎フレームカメラとの距離からゲームの中の距離を出す
        float amplitudePixels = 3.0f;
        //! 渡した位置から体の一番遠い所へ届くまでのフレーム数。長さの半分を超える分は使わない
        int reachFrames = 6;
        //! 振れ幅の、体の半径に対する割合。正ならピクセルの欄の代わりに使う
        float radiusRatio = 0.0f;
        float referenceHeight = 720.0f; // ピクセルの欄を数える画面の高さ

        NS_REFLECT_BEGIN(HitBodyTremorItem, void)
        NS_FIELD(amplitudePixels, "振れ幅のピクセル")
        NS_FIELD(reachFrames, "裏まで届くフレーム数")
        NS_FIELD(radiusRatio, "体の半径に対する振れ幅の割合")
        NS_FIELD(referenceHeight, "ピクセルを数える画面の高さ")
        NS_REFLECT_END_VALUE()
    };

    //! 画面を沈めて震わせる。同じ持ち主の「沈みを戻す」で跳ね返り、戻されなければ長さで終わる
    //! ピクセルは高さ 1080 の画面で数える。カメラの位置と向きは変えない
    struct HitCameraSinkItem
    {
        std::string_view typeName = "CameraSink";
        std::string_view label = "沈む揺れ";

        float bottomPixels = 9.0f;   // 底の深さ (ピクセル)
        int sinkFrames = 2;          // 底へ届くまでのフレーム数
        float tremblePixels = 5.0f;  // 底での震えの最初の大きさ (ピクセル)
        int trembleFrames = 6;       // 震えるフレーム数
        int tremblePeriodFrames = 2; // 震えの 1 往復のフレーム数
        float overshootRatio = 0.5f; // 跳ね返りで 0 を越えて上へ出る量の、底の深さに対する割合
        int bouncePeriodFrames = 8;  // 跳ね返りの 1 往復のフレーム数

        NS_REFLECT_BEGIN(HitCameraSinkItem, void)
        NS_FIELD(bottomPixels, "底の深さのピクセル")
        NS_FIELD(sinkFrames, "沈むフレーム数")
        NS_FIELD(tremblePixels, "震えの大きさ")
        NS_FIELD(trembleFrames, "震えのフレーム数")
        NS_FIELD(tremblePeriodFrames, "震えの 1 往復")
        NS_FIELD(overshootRatio, "行き過ぎの割合")
        NS_FIELD(bouncePeriodFrames, "跳ね返りの 1 往復")
        NS_REFLECT_END_VALUE()
    };

    //! 同じ持ち主が先に始めた沈む揺れを跳ね返す。新しい揺れは始めず、沈みが無ければ何もしない
    struct HitCameraSinkReleaseItem
    {
        std::string_view typeName = "CameraSinkRelease";
        std::string_view label = "沈みを戻す";
    };

    //! カメラの寄りと傾き。項目の長さは保つフレーム数と戻すフレーム数の和
    struct HitZoomRollItem
    {
        std::string_view typeName = "ZoomRoll";
        std::string_view label = "寄りと傾き";

        float zoom = 1.15f;       // 寄りの倍率。1 は寄らない
        float rollDegrees = 3.0f; // 傾き (度)。正は画面の上端を、相手がカメラの右なら右へ倒す
        int returnFrames = 6;     // 長さのうち、元へ戻すのに使う末尾のフレーム数
        //! 束を始めてから寄せ始めるまでのフレーム数。止めの間も数える
        int delayFrames = 0;

        NS_REFLECT_BEGIN(HitZoomRollItem, void)
        NS_FIELD(zoom, "寄りの倍率")
        NS_FIELD(rollDegrees, "傾き")
        NS_FIELD(returnFrames, "戻すフレーム数")
        NS_FIELD(delayFrames, "始めるまでのフレーム数")
        NS_REFLECT_END_VALUE()
    };

    //! カメラのトラウマを足し、相手の側へ一撃を振る。揺れの途中なら前のトラウマに足す
    //! 揺れの長さは項目の長さでなく減る速さで決まる
    struct HitCameraTraumaItem
    {
        std::string_view typeName = "CameraTrauma";
        std::string_view label = "トラウマの揺れ";

        float trauma = 0.9f;         // 足すトラウマ 0〜1
        float yawDegrees = 3.0f;     // トラウマ 1 の横の首振りの最大 (度)
        float pitchDegrees = 2.0f;   // トラウマ 1 の縦の首振りの最大 (度)
        float rollDegrees = 1.5f;    // トラウマ 1 の傾きの最大 (度)
        float frequency = 9.0f;      // ノイズの格子を 1 秒に進める数
        float decayPerSecond = 1.8f; // トラウマが 1 秒に減る量
        float exponent = 2.0f;       // トラウマを振れ幅にする指数
        float kickDegrees = 2.5f;    // 一撃の山の大きさ (度)
        int kickPeakFrames = 1;      // 一撃の山のフレーム。始めたフレームを 1 と数える

        NS_REFLECT_BEGIN(HitCameraTraumaItem, void)
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

    //! カメラの位置と注視点を、相手へ向かう水平の向きへ曲線の距離だけずらす。向きと地平線は変えない
    struct HitCameraLurchItem
    {
        std::string_view typeName = "CameraLurch";
        std::string_view label = "つんのめり";

        Curve distance{}; // ずらす距離 (m)。負は手前へ

        NS_REFLECT_BEGIN(HitCameraLurchItem, void)
        NS_FIELD(distance, "ずらす距離")
        NS_REFLECT_END_VALUE()
    };

    //! カメラの位置と注視点を、相手から離れる向きの画面方向へ曲線の距離だけずらす
    //! 奥行きの成分は捨て、画面方向が無ければ揺らさない。向きと地平線は変えない
    struct HitCameraReboundSwayItem
    {
        std::string_view typeName = "CameraReboundSway";
        std::string_view label = "反動の揺れ";

        Curve distance{}; // ずらす距離 (m)。負は相手へ向かう向き

        NS_REFLECT_BEGIN(HitCameraReboundSwayItem, void)
        NS_FIELD(distance, "ずらす距離")
        NS_REFLECT_END_VALUE()
    };

    //! 画面の白い光。長さの間に始めの濃さから直線で薄れる
    struct HitFlashItem
    {
        std::string_view typeName = "Flash";
        std::string_view label = "白い光";

        float alpha = 0.5f; // 始めの濃さ 0〜1

        NS_REFLECT_BEGIN(HitFlashItem, void)
        NS_FIELD(alpha, "濃さ")
        NS_REFLECT_END_VALUE()
    };

    //! 渡した位置から画面に輪を広げ、輪の部分の絵を外へ押し出す。長さが出すフレーム数
    struct HitDistortionRingItem
    {
        std::string_view typeName = "DistortionRing";
        std::string_view label = "歪みの輪";

        Curve radius{};          // 輪の半径。画面の高さに対する割合。点が無ければ 0
        Curve push{};            // 輪の真ん中で絵を押し出す長さ。画面の高さに対する割合。点が無ければ歪めない
        float halfWidth = 0.05f; // 輪の半分の幅。画面の高さに対する割合

        NS_REFLECT_BEGIN(HitDistortionRingItem, void)
        NS_FIELD(radius, "半径")
        NS_FIELD(push, "押し")
        NS_FIELD(halfWidth, "半分の幅")
        NS_REFLECT_END_VALUE()
    };

    //! 渡した位置の真下から、上を向いた面へ光の輪を広げる。長さが出すフレーム数
    struct HitGroundWaveItem
    {
        std::string_view typeName = "GroundWave";
        std::string_view label = "床の波";

        Curve radius{};   // 輪の半径 (m)。点が無ければ 0
        Curve strength{}; // 輪の強さ。面の倒れと頂の光の量。点が無ければ出さない

        NS_REFLECT_BEGIN(HitGroundWaveItem, void)
        NS_FIELD(radius, "半径")
        NS_FIELD(strength, "強さ")
        NS_REFLECT_END_VALUE()
    };

    //! パッドの振動。左右のモーターの速さの曲線で、長さの間だけ書く
    //! 向き別の時は、持ち主から見た相手の向きの角度で隣り合う 2 つの向きの曲線を混ぜる。
    //! 向きは右・左・上・下で、向き無しの曲線は使わない
    struct HitPadVibrationItem
    {
        std::string_view typeName = "PadVibration";
        std::string_view label = "パッドの振動";

        bool byDirection = false; // 向き別の 4 組を使う場合 true。偽なら向き無しの 1 組
        Curve left{};             // 向き無しの左のモーターの速さ 0〜1
        Curve right{};            // 向き無しの右のモーターの速さ 0〜1
        Curve rightLeft{};        // 相手が右の時の左のモーター
        Curve rightRight{};
        Curve leftLeft{}; // 相手が左の時の左のモーター
        Curve leftRight{};
        Curve upLeft{}; // 相手が上の時の左のモーター
        Curve upRight{};
        Curve downLeft{}; // 相手が下の時の左のモーター
        Curve downRight{};

        NS_REFLECT_BEGIN(HitPadVibrationItem, void)
        NS_FIELD(byDirection, "向き別")
        NS_FIELD(left, "左の速さ")
        NS_FIELD(right, "右の速さ")
        NS_FIELD(rightLeft, "右の時の左の速さ")
        NS_FIELD(rightRight, "右の時の右の速さ")
        NS_FIELD(leftLeft, "左の時の左の速さ")
        NS_FIELD(leftRight, "左の時の右の速さ")
        NS_FIELD(upLeft, "上の時の左の速さ")
        NS_FIELD(upRight, "上の時の右の速さ")
        NS_FIELD(downLeft, "下の時の左の速さ")
        NS_FIELD(downRight, "下の時の右の速さ")
        NS_REFLECT_END_VALUE()
    };

    //! 世界を遅くし、実時間で普段の速さへ戻す
    struct HitWorldSpeedItem
    {
        std::string_view typeName = "WorldSpeed";
        std::string_view label = "世界の速さ";

        float startSpeed = 0.2f;    // 始まりの世界の速さ 0〜1
        float returnSeconds = 0.3f; // 普段の速さへ戻るまでの実時間 (秒)
        Curve shape{};              // 戻り方。横軸は 0〜1 の経過の割合、縦軸は 0〜1 の戻りの割合。点が無ければ直線

        NS_REFLECT_BEGIN(HitWorldSpeedItem, void)
        NS_FIELD(startSpeed, "始まりの速さ")
        NS_FIELD(returnSeconds, "戻る秒")
        NS_FIELD(shape, "戻り方")
        NS_REFLECT_END_VALUE()
    };

    //! 長さの間、持ち主以外の世界を止める
    struct HitOthersStopItem
    {
        std::string_view typeName = "OthersStop";
        std::string_view label = "自機以外の止め";
    };

    //! 持ち主のアクターへ渡す項目。名前と値を持ち主が読んで、持ち主の演出 (残像・エフェクトなど) を始める
    //! NSlib は名前と値の意味を知らない。値の名前は持ち主が決める
    struct HitOwnerItem
    {
        std::string_view typeName = "Owner";
        std::string_view label = "持ち主へ渡す項目";

        std::string name; // 持ち主が始める演出の名前
        //! 値の名前から値。ファイルでは欄の「値」に書き、インスペクタの欄の表には載らない
        std::map<std::string, float, std::less<>> values;

        NS_REFLECT_BEGIN(HitOwnerItem, void)
        NS_FIELD(name, "名前")
        NS_REFLECT_END_VALUE()
    };

    //! 項目の種類ごとの値。種類を足す時はここへ型を足す
    using HitReactionItem = std::variant<HitShapeItem,
                                         HitBodyShakeItem,
                                         HitBodyTremorItem,
                                         HitCameraSinkItem,
                                         HitCameraSinkReleaseItem,
                                         HitZoomRollItem,
                                         HitCameraTraumaItem,
                                         HitCameraLurchItem,
                                         HitCameraReboundSwayItem,
                                         HitFlashItem,
                                         HitDistortionRingItem,
                                         HitGroundWaveItem,
                                         HitPadVibrationItem,
                                         HitWorldSpeedItem,
                                         HitOthersStopItem,
                                         HitOwnerItem>;

    //! 束の 1 項目。束を始めたフレームに始まり、長さのフレーム数だけ続く
    struct HitReactionEntry
    {
        HitReactionItem item{};
        int length = 1; // 長さ (フレーム数)。0 以上
    };

    //! 名前の束。始めると全部の項目が同じフレームに始まり、止めの長さの間は持ち主の状態と体が止まる
    //! 項目の値は書いた値のまま使い、始める時に書き換えない
    struct HitReactionSet
    {
        std::string name;
        int stopFrames = 0; // 持ち主の止めのフレーム数。0 以上
        std::vector<HitReactionEntry> entries;
    };

    [[nodiscard]] std::string_view HitReactionItemTypeName(const HitReactionItem& item) noexcept;

    [[nodiscard]] std::string_view HitReactionItemLabel(const HitReactionItem& item) noexcept;

    //! 種類の名前から、欄が全部既定値のその種類の値を作る
    //! @return 知らない名前の場合は std::nullopt
    [[nodiscard]] std::optional<HitReactionItem> MakeHitReactionItem(std::string_view typeName);

    //! item の種類のリフレクション。欄の無い種類は nullptr
    [[nodiscard]] const ReflectionInfo* HitReactionItemReflection(const HitReactionItem& item) noexcept;

    //! item の今の種類の値を指す番地。HitReactionItemReflection の欄を読み書きする時に渡す
    [[nodiscard]] void* HitReactionItemFields(HitReactionItem& item) noexcept;
    [[nodiscard]] const void* HitReactionItemFields(const HitReactionItem& item) noexcept;

    //! row の鍵 key の値を 0 以上の整数のフレーム数として out へ読む。無い・整数でない・負・int に入らない場合は
    //! false で、out は変えない
    [[nodiscard]] bool ReadHitReactionFrames(const nlohmann::json& row, std::string_view key, int& out);

    //! 1 行の JSON から項目の種類 type と欄 fields を読む
    //! 欠けた欄は既定のまま、知らない欄と、type・fields・rowKeys の外の行の鍵は WARN を出して飛ばす
    //! @param[in] rowKeys 呼び手が同じ行から読む鍵 (長さ・始まりなど)
    //! @return 種類を知らない・欄の形が壊れている場合は std::nullopt で、error に理由を書く
    [[nodiscard]] std::optional<HitReactionItem> ParseHitReactionItem(const nlohmann::json& row,
                                                                      std::initializer_list<std::string_view> rowKeys,
                                                                      std::string& error);

    //! item の種類 type と欄 fields を row へ書く
    void WriteHitReactionItem(const HitReactionItem& item, nlohmann::json& row);

    //! 束 1 つを JSON の形 {"name", "stopFrames", "items": [{"type", "length", "fields"}]} から読む
    //! 欠けた欄は既定のまま、知らない欄は WARN を出して飛ばす
    //! @return 名前が空・止めの長さか項目の長さが負・知らない種類の場合は std::nullopt で、error に理由を書く
    [[nodiscard]] std::optional<HitReactionSet> ParseHitReactionSet(const nlohmann::json& doc, std::string& error);

    [[nodiscard]] nlohmann::json HitReactionSetToJson(const HitReactionSet& set);

    //! .actor の文書の束の並びを読む
    //! 壊れた束と、前の束と名前の重なる束は、理由を ERROR に出して読まない
    //! @param[in] actorFileName ERROR に出す .actor の名前
    [[nodiscard]] std::vector<HitReactionSet> ReadHitReactionSets(std::string_view actorFileName,
                                                                  const nlohmann::json& actorFile);

    //! .actor の文書へ束の並びを書く。書く物が無ければ鍵ごと消す
    //! 今の並びのうち ReadHitReactionSets が読まない束は、字面のまま同じ位置に残す。読む束の位置へ
    //! sets を並びの順に書き、余った sets は末尾へ足し、足りない位置は消す。今の並びが配列でなければ sets だけを書く
    void WriteHitReactionSets(nlohmann::json& actorFile, const std::vector<HitReactionSet>& sets);

    //! 向き別の振動の 4 つの向きの重み。足すと 1
    struct HitDirectionWeights
    {
        float right = 0.0f;
        float left = 0.0f;
        float up = 0.0f;
        float down = 0.0f;
    };

    //! 右を side、上を up とした向きの角度で、隣り合う 2 つの向きを直線に混ぜた重み
    //! 向きの角度から 90 度離れると 0。side と up が両方 0 なら角度が決まらないので右を 1 にする
    [[nodiscard]] HitDirectionWeights BlendHitDirectionWeights(float side, float up) noexcept;

    //! 束から始めた、持ち主へ渡す項目 1 つの進み
    //! 持ち主は名前で演出を選び、frame と値で描く。frame は始めたフレームが 0
    struct HitOwnerRun
    {
        HitOwnerItem item{};
        int length = 0;     // 長さ (フレーム数)。frame がこれに届いたフレームの頭で外れる
        int frame = 0;      // 始めてから進めたフレーム数
        Vector3 position{}; // 束を始めた時に渡した位置
    };

    class HitSensor;
    class Model;

    //! 束か項目を始める 1 回の呼び出しに渡す、持ち主の読む物と書く先。呼び出しの中だけ使い、覚えない
    //! 組む所は SceneActor・PropActor・ActionTimeline の 3 つ。ActionTimeline は ActionTimelineSelf の
    //! const SceneActor* から写すので、SceneActor の関数 1 つには寄らない。項目を足す時は 3 か所を揃える
    struct HitReactionSelf
    {
        //! .actor の名前・カメラ・シーンを読み、管理クラスへの依頼の主にする。事前条件: nullptr でない
        const SceneActor* actor = nullptr;
        Model* model = nullptr; // 形・揺れ・震えを書く先。持ち主に Model が無ければ nullptr
    };

    //! 当たりの演出を頼む部品。.actor の名前の束を持ち、名前で始める
    //! 揺れと寄りは CameraManager、白・震えの線・歪みの輪・床の波は HitScreenDirector、振動は PadRumbleDirector、
    //! 描く揺れ・形・震えは持ち主の Model が進める。止めの長さは Start が返し、持ち主の SceneActor が数える。
    //! 持ち主は覚えず、呼ぶたびに受ける。持ち主へ渡す項目の進みはここに持ち、持ち主の SceneActor::Update が進める
    class HitReaction : public Object
    {
    public:
        //! シーンに 1 つの HitScreenDirector と PadRumbleDirector
        //! を作る。更新の最中に作ると、そのフレームの段に間に合わない
        void CreateDirectors(const SceneActor* owner);

        //! 持ち主の .actor (SceneActor::ActorFileName) から束を読み直す
        //! .actor が無ければ束は空。壊れた束は ReadHitReactionSets が ERROR を出して読まない
        void ReloadSets(const SceneActor* owner);

        //! 名前の束を始める。全部の項目が呼んだフレームに始まり、項目の値は束に書いた値のまま使う
        //! 向きを使う項目は、self の中心から other の中心への向きで決め、位置を使う項目は position を使う
        //! @param[in] name 束の名前
        //! @param[in] position 触れた位置などの世界の点
        //! @param[in] self 持ち主のセンサー。事前条件: nullptr でない
        //! @param[in] other 相手のセンサー。事前条件: nullptr でない
        //! @return 始めた場合は束の止めの長さ (フレーム数)。持ち主が SceneActor::RequestHitStop で止めに頼む。
        //! その名前の束が無い場合は WARN を出して std::nullopt
        [[nodiscard]] std::optional<int> Start(const HitReactionSelf& owner,
                                               std::string_view name,
                                               const Vector3& position,
                                               const HitSensor* self,
                                               const HitSensor* other);

        //! 項目を 1 つ、長さ length で始める。動作のタイムラインが事象の始まりのフレームに呼ぶ
        //! 向きと位置は Start と同じく self・other・position から出す。持ち主の止めには触らない
        //! @param[in] self 持ち主のセンサー。事前条件: nullptr でない
        //! @param[in] other 相手のセンサー。事前条件: nullptr でない
        //! @param[in] shapeFrame 形の項目を何フレーム目の姿から描くか。途中のフレームから入った動作の形に使い、
        //! ほかの項目は読まない。事前条件: 0 以上
        void StartItem(const HitReactionSelf& owner,
                       const HitReactionItem& item,
                       int length,
                       const Vector3& position,
                       const HitSensor* self,
                       const HitSensor* other,
                       int shapeFrame = 0);

        //! 持ち主の .actor の名前の束。無ければ nullptr
        [[nodiscard]] const HitReactionSet* FindSet(std::string_view name) const noexcept;

        //! 始めて長さの終わっていない、持ち主へ渡す項目。始めた順
        [[nodiscard]] const std::vector<HitOwnerRun>& OwnerRuns() const noexcept { return m_ownerRuns; }

        //! index 以降に足された、持ち主へ渡す項目の写し。
        //! 写しで返すので、回している間に次の当たりの演出を始めて並びが伸びても、指す先が消えない
        [[nodiscard]] std::vector<HitOwnerRun> OwnerRunsSince(std::size_t index) const;

        //! 持ち主へ渡す項目を 1 フレーム進め、長さの終わった物を外す
        //! 呼ぶのは SceneActor::Update の頭だけ。持ち主の止めの間も進める
        void AdvanceOwnerRuns() noexcept;

        // 下の関数の owner は管理クラスとカメラを引くアクターで、依頼の主にもなる。事前条件: nullptr でない

        //! 白の光を頼む。前の白が残っていても始め直す
        //! @param[in] frames 白の光のフレーム数。0 以下なら光らない
        //! @param[in] alpha 始めの濃さ。残りのフレーム数に比例して薄くなる
        void StartFlash(const SceneActor* owner, int frames, float alpha) noexcept;

        //! カメラの揺れを始める
        //! @param[in] desc 揺れの設定。フレーム数 0 以下なら揺らさない
        //! @return 揺れを積んだか、フレーム数 0 以下で揺らさなかった場合 true。
        //! 設定が壊れている・上限を超える・カメラの管理役が無い場合 false
        bool StartShake(const SceneActor* owner, const CameraShakeDesc& desc);

        //! カメラの沈む揺れを始める
        //! @param[in] desc 4 拍の形。フレーム数 0 以下なら揺らさない
        //! @return 揺れを積んだか、フレーム数 0 以下で揺らさなかった場合 true。
        //! 設定が壊れている・上限を超える・カメラの管理役が無い場合 false
        bool StartSink(const SceneActor* owner, const CameraSinkDesc& desc);

        //! カメラのずれを始める。位置と注視点を同じだけずらし、向きは変えない
        //! @param[in] desc ずらす向きと距離の曲線。フレーム数 0 以下ならずらさない
        //! @return ずれを積んだか、フレーム数 0 以下でずらさなかった場合 true。
        //! 設定が壊れている・上限を超える・カメラの管理役が無い場合 false
        bool StartNudge(const SceneActor* owner, const CameraNudgeDesc& desc);

        //! カメラのトラウマを足す。揺れの途中なら前のトラウマに足す
        //! @param[in] desc 足す量と揺れの形
        //! @return 足した場合 true。量が壊れている・カメラの管理役が無い場合 false
        bool AddTrauma(const SceneActor* owner, const CameraTraumaDesc& desc);

        //! カメラの寄りと傾きを始める
        //! @param[in] desc 寄りと傾きの設定
        //! @return 積んだ場合 true。設定が壊れている・カメラの管理役が無い場合 false
        bool StartZoomRoll(const SceneActor* owner, const CameraZoomRollDesc& desc);

        //! 震えの線を頼む。前の線が残っていても始め直す
        //! @param[in] desc 震えの線の設定。半径かフレーム数が 0 以下なら出さない
        void StartShakeLines(const SceneActor* owner, const HitShakeLinesDesc& desc) noexcept;

        //! 歪みの輪を頼む。前の輪が残っていても始め直す
        //! @param[in] desc 歪みの輪の設定。フレーム数が 0 以下なら出さない
        void StartDistortionRing(const SceneActor* owner, const HitDistortionRingDesc& desc) noexcept;

        //! この部品が頼んだ歪みの輪を消す
        //! Stop は輪を消さない
        void StopDistortionRing(const SceneActor* owner) noexcept;

        //! この部品が頼んだ床の波を消す
        //! Stop は波を消さない
        void StopGroundWave(const SceneActor* owner) noexcept;

        //! パッドの振動を頼む。前の振動が残っていても始め直す
        //! @param[in] pad 振動の設定
        void StartPadVibration(const SceneActor* owner, const HitPadVibration& pad);
        //! 書いている振動に pad を重ねる
        //! 重ねたフレームを 0 として pad を進め、値を足す。pad の長さを過ぎたら 0 を足す。
        //! 何も書いていなければ StartPadVibration と同じ
        //! @param[in] pad 重ねる振動
        void BlendPadVibration(const SceneActor* owner, const HitPadVibration& pad);

        //! この部品が頼んだ演出を止める。白と震えの線を消し、振動を 0 にし、揺れ・ずれ・寄りを外し、
        //! 持ち主へ渡す項目を外す
        //! トラウマは残す。次の当たりが止めてから始め直しても、続けて当てたトラウマは足される。
        //! ほかの物が頼んだカメラの効果は残す
        void Stop(const SceneActor* owner);

        //! Stop に加えて、この部品が頼んだカメラの効果をトラウマも含めて全部外す。途中で外れても効果を残さない
        void StopAll(const SceneActor* owner);

        // 状態は保存しない。型検索で引けるよう型名だけ登録する
        NS_REFLECT_NONE(HitReaction, Object)

    private:
        struct ItemStarter;

        std::vector<HitReactionSet> m_sets;     // 持ち主の .actor の束
        std::vector<HitOwnerRun> m_ownerRuns;   // 進めている持ち主へ渡す項目
        uint32 m_startCount = 0;                // 束か項目を始めた回数。揺れの種に混ぜる
    };
} // namespace NS
