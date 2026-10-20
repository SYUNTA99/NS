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
    //! @brief 動作の事象 1 つ。項目の型は名前の束と同じで、状態に入ったフレームを 0 にしたフレーム数で始まりと長さを持つ
    struct ActorActionEvent
    {
        HitReactionItem item{};
        int start = 0;  //!< 始まりのフレーム。0 以上
        int length = 1; //!< 長さ (フレーム数)。0 以上
    };

    //! @brief 状態ごとの動作。同じフレームに始まる事象は並びの順に起きる
    struct ActorAction
    {
        std::string name;
        std::vector<ActorActionEvent> events;
    };

    //! @brief 動作 1 つを JSON の形 {"name", "events": [{"type", "start", "length", "fields"}]} から読む
    //! @details 欠けた欄は既定のまま、知らない欄は WARN を出して飛ばす
    //! @return 名前が空・始まりか長さが負・知らない種類の場合は std::nullopt で、error に理由を書く
    [[nodiscard]] std::optional<ActorAction> ParseActorAction(const nlohmann::json& doc, std::string& error);

    [[nodiscard]] nlohmann::json ActorActionToJson(const ActorAction& action);

    //! @brief .actor の文書の動作の並びを読む
    //! @details 壊れた動作と、前の動作と名前の重なる動作は、理由を ERROR に出して読まない
    //! @param[in] actorFileName ERROR に出す .actor の名前
    [[nodiscard]] std::vector<ActorAction> ReadActorActions(std::string_view actorFileName,
                                                            const nlohmann::json& actorFile);

    //! .actor の文書へ動作の並びを書く。並びが空なら鍵ごと消す
    void WriteActorActions(nlohmann::json& actorFile, const std::vector<ActorAction>& actions);
} // namespace NS
