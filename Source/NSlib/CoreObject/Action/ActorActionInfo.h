#pragma once

#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/SoundName.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace NS
{
    // 動作の事象の項目の型。始まった項目は持ち主のアクターが読み、自分の部品へ頼む。動作の部品はほかの部品を知らない

    //! 持ち主の Animation でクリップを流す
    struct ActionAnimInfo
    {
        std::string_view typeName = "Anim";
        std::string_view label = "アニメーション";

        AnimClip* clip = nullptr;  // 流すクリップ
        float blendSeconds = 0.0f; // 前のクリップから移る秒。0 以上
        bool looping = true;       // 終わりまで流したら頭へ戻る場合 true

        NS_REFLECT_BEGIN(ActionAnimInfo, void)
        NS_FIELD(clip, "クリップ")
        NS_FIELD(blendSeconds, "切り替えの秒")
        NS_FIELD(looping, "繰り返す")
        NS_REFLECT_END_VALUE()
    };

    //! 持ち主の Sound で効果音を鳴らす
    struct ActionSoundInfo
    {
        std::string_view typeName = "Sound";
        std::string_view label = "効果音";

        SoundName sound{};   // 鳴らす音
        float volume = 1.0f; // 音量 0〜1

        NS_REFLECT_BEGIN(ActionSoundInfo, void)
        NS_FIELD(sound, "音")
        NS_FIELD(volume, "音量")
        NS_REFLECT_END_VALUE()
    };

    //! 持ち主の EffectObj で、持ち主の位置にエフェクトを出す
    struct ActionEffectInfo
    {
        std::string_view typeName = "Effect";
        std::string_view label = "エフェクト";

        Effect* effect = nullptr; // 出すエフェクト

        NS_REFLECT_BEGIN(ActionEffectInfo, void)
        NS_FIELD(effect, "エフェクト")
        NS_REFLECT_END_VALUE()
    };

    //! パッドの振動。左右のモーターの速さの曲線で、長さの間だけ書く
    struct ActionPadInfo
    {
        std::string_view typeName = "Pad";
        std::string_view label = "パッドの振動";

        bool layered = false; // 走っている振動を始め直さずに重ねる場合 true
        Curve left{};         // 左のモーターの速さ 0〜1
        Curve right{};        // 右のモーターの速さ 0〜1

        NS_REFLECT_BEGIN(ActionPadInfo, void)
        NS_FIELD(layered, "前の振動に重ねる")
        NS_FIELD(left, "左の速さ")
        NS_FIELD(right, "右の速さ")
        NS_REFLECT_END_VALUE()
    };

    //! 向きを持たないカメラの揺れ。足したトラウマが時間で減る
    struct ActionCameraInfo
    {
        std::string_view typeName = "Camera";
        std::string_view label = "カメラの揺れ";

        float trauma = 0.5f; // 足すトラウマ 0〜1

        NS_REFLECT_BEGIN(ActionCameraInfo, void)
        NS_FIELD(trauma, "トラウマ")
        NS_REFLECT_END_VALUE()
    };

    //! 画面の白い光。長さの間に始めの濃さから直線で薄れる
    struct ActionScreenInfo
    {
        std::string_view typeName = "Screen";
        std::string_view label = "白い光";

        float alpha = 0.5f; // 始めの濃さ 0〜1

        NS_REFLECT_BEGIN(ActionScreenInfo, void)
        NS_FIELD(alpha, "濃さ")
        NS_REFLECT_END_VALUE()
    };

    //! 動作の事象の項目の種類ごとの値。種類を足す時はここへ型を足す
    using ActionEventInfo = std::
        variant<ActionAnimInfo, ActionSoundInfo, ActionEffectInfo, ActionPadInfo, ActionCameraInfo, ActionScreenInfo>;

    //! 動作の事象 1 つ。状態に入ったフレームを 0 にしたフレーム数で始まりと長さを持つ
    struct ActorActionEvent
    {
        ActionEventInfo item{};
        int start = 0;  // 始まりのフレーム。0 以上
        int length = 1; // 長さ (フレーム数)。0 以上
    };

    //! 動作の中でセンサーを入り切りする事象
    //! 始まりのフレームに valid の側へ切り替え、始まり + 長さのフレームに逆へ戻す。
    //! 長さの途中で動作が止まった時も戻す
    struct ActionSensorEvent
    {
        std::string sensor; // 切り替えるセンサーの名前 (HitSensor::Name)。空でない
        bool valid = true;  // 長さの間、調べる対象にする場合 true。偽なら長さの間外す
        int start = 0;      // 始まりのフレーム。0 以上
        int length = 1;     // 長さ (フレーム数)。0 以上
    };

    //! 状態ごとの動作。同じフレームに始まる事象は並びの順に起きる
    struct ActorActionInfo
    {
        std::string name;
        //! 動作の長さ。結んだ状態の中で進める Step の回数で、この回数だけ OnStep が進んだ次の OnStep で
        //! IsStateEnd が真になる。0 は長さを決めていない。
        //! 事象の終わり (ActionLength) と別に持つので、見た目の事象を直しても状態の長さは変わらない
        int length = 0;
        std::vector<ActorActionEvent> events;
        std::vector<ActionSensorEvent> sensors;
    };

    //! 動作 1 つを JSON の形 {"name", "length", "events": [{"type", "start", "length", "fields"}],
    //! "sensors": [{"sensor", "valid", "start", "length"}]} から読む
    //! 欠けた欄は既定のまま、知らない欄は WARN を出して飛ばす。sensors は無ければ空、length は無ければ 0
    //! @return 名前が空・動作の長さが負か整数でない・始まりか長さが負・知らない種類・
    //! 入り切りの sensor が空でない文字列でない・入り切りの valid が真偽でない場合は
    //! std::nullopt で、error に理由を書く
    [[nodiscard]] std::optional<ActorActionInfo> ParseActorActionInfo(const nlohmann::json& doc, std::string& error);

    [[nodiscard]] nlohmann::json ActorActionInfoToJson(const ActorActionInfo& action);

    //! .actor の文書の動作の並びを読む
    //! 壊れた動作と、前の動作と名前の重なる動作は、理由を ERROR に出して読まない
    //! @param[in] actorFileName ERROR に出す .actor の名前
    [[nodiscard]] std::vector<ActorActionInfo> ReadActorActions(std::string_view actorFileName,
                                                                const nlohmann::json& actorFile);

    //! .actor の文書へ動作の並びを書く。書く物が無ければ鍵ごと消す
    //! 今の並びのうち ReadActorActions が読まない動作は、字面のまま同じ位置に残す。読む動作の位置へ
    //! actions を並びの順に書き、余った actions は末尾へ足し、足りない位置は消す。今の並びが配列でなければ
    //! actions だけを書く
    void WriteActorActions(nlohmann::json& actorFile, const std::vector<ActorActionInfo>& actions);

    //! 事象の並びの長さ (フレーム数)。一番遅く終わる事象と入り切りの、始まり + 長さ。どちらも無ければ 0。
    //! int を越える長さは int の最大。状態の長さは ActorActionInfo::length が持ち、これはエディタが並びを見せる長さ
    [[nodiscard]] int ActionLength(const ActorActionInfo& action) noexcept;
} // namespace NS
