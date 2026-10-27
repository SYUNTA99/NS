#pragma once

#include "NSlib/CoreObject/ClassObjs/HitReaction.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    /// 動作の事象 1 つ。項目の型は名前の束と同じで、状態に入ったフレームを 0 にしたフレーム数で始まりと長さを持つ
    struct ActorActionEvent
    {
        HitReactionItem item{};
        int start = 0;  // 始まりのフレーム。0 以上
        int length = 1; // 長さ (フレーム数)。0 以上
    };

    /// 動作の中でセンサーを入り切りする事象
    /// 始まりのフレームに valid の側へ切り替え、始まり + 長さのフレームに逆へ戻す。
    /// 長さの途中で動作が止まった時も戻す
    struct ActionSensorEvent
    {
        bool attack = false; // 攻撃のセンサーを切り替える場合 true。偽なら体のセンサー
        bool valid = true;   // 長さの間、調べる対象にする場合 true。偽なら長さの間外す
        int start = 0;       // 始まりのフレーム。0 以上
        int length = 1;      // 長さ (フレーム数)。0 以上
    };

    /// 状態ごとの動作。同じフレームに始まる事象は並びの順に起きる
    struct ActorAction
    {
        std::string name;
        /// 動作の長さ。結んだ状態の中で進める Step の回数で、この回数だけ OnStep が進んだ次の OnStep で
        /// IsStateEnd が真になる。0 は長さを決めていない。
        /// 事象の終わり (ActionLength) と別に持つので、見た目の事象を直しても状態の長さは変わらない
        int length = 0;
        std::vector<ActorActionEvent> events;
        std::vector<ActionSensorEvent> sensors;
    };

    /// 動作 1 つを JSON の形 {"name", "length", "events": [{"type", "start", "length", "fields"}],
    /// "sensors": [{"attack", "valid", "start", "length"}]} から読む
    /// 欠けた欄は既定のまま、知らない欄は WARN を出して飛ばす。sensors は無ければ空、length は無ければ 0
    /// @return 名前が空・動作の長さが負か整数でない・始まりか長さが負・知らない種類・入り切りの欄が真偽でない場合は
    /// std::nullopt で、error に理由を書く
    [[nodiscard]] std::optional<ActorAction> ParseActorAction(const nlohmann::json& doc, std::string& error);

    [[nodiscard]] nlohmann::json ActorActionToJson(const ActorAction& action);

    /// .actor の文書の動作の並びを読む
    /// 壊れた動作と、前の動作と名前の重なる動作は、理由を ERROR に出して読まない
    /// @param[in] actorFileName ERROR に出す .actor の名前
    [[nodiscard]] std::vector<ActorAction> ReadActorActions(std::string_view actorFileName,
                                                            const nlohmann::json& actorFile);

    /// .actor の文書へ動作の並びを書く。書く物が無ければ鍵ごと消す
    /// 今の並びのうち ReadActorActions が読まない動作は、字面のまま同じ位置に残す。読む動作の位置へ
    /// actions を並びの順に書き、余った actions は末尾へ足し、足りない位置は消す。今の並びが配列でなければ
    /// actions だけを書く
    void WriteActorActions(nlohmann::json& actorFile, const std::vector<ActorAction>& actions);

    /// 事象の並びの長さ (フレーム数)。一番遅く終わる事象と入り切りの、始まり + 長さ。どちらも無ければ 0。
    /// int を越える長さは int の最大。状態の長さは ActorAction::length が持ち、これはエディタが並びを見せる長さ
    [[nodiscard]] int ActionLength(const ActorAction& action) noexcept;
} // namespace NS
