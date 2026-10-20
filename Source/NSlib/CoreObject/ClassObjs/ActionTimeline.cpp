#include "NSlib/CoreObject/ClassObjs/ActionTimeline.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"

#include <cstddef>
#include <format>
#include <set>
#include <utility>

namespace NS
{
    namespace
    {
        // 事象の行 1 つを読む。時計は状態に入ったフレームを 0 と数えるので、負の始まりは読まない
        [[nodiscard]] std::optional<ActorActionEvent> ParseEvent(const nlohmann::json& row,
                                                                 std::size_t index,
                                                                 std::string& error)
        {
            std::string reason;
            std::optional<HitReactionItem> item = ParseHitReactionItem(row, {"start", "length"}, reason);
            if (!item.has_value())
            {
                error = std::format("{} 番目の事象: {}", index, reason);
                return std::nullopt;
            }
            ActorActionEvent event{.item = std::move(*item)};
            if (!ReadHitReactionFrames(row, "start", event.start))
            {
                error = std::format("{} 番目の事象の始まり start が 0 以上の整数でない", index);
                return std::nullopt;
            }
            if (!ReadHitReactionFrames(row, "length", event.length))
            {
                error = std::format("{} 番目の事象の長さ length が 0 以上の整数でない", index);
                return std::nullopt;
            }
            return event;
        }
    } // namespace

    std::optional<ActorAction> ParseActorAction(const nlohmann::json& doc, std::string& error)
    {
        if (!doc.is_object())
        {
            error = "動作が object でない";
            return std::nullopt;
        }
        ActorAction action;
        const nlohmann::json::const_iterator name = doc.find("name");
        if (name == doc.end() || !name->is_string() || name->get_ref<const std::string&>().empty())
        {
            error = "名前 name が空でない文字列でない";
            return std::nullopt;
        }
        action.name = name->get<std::string>();
        const nlohmann::json::const_iterator events = doc.find("events");
        if (events == doc.end() || !events->is_array())
        {
            error = std::format("動作 {} の事象の並び events が配列でない", action.name);
            return std::nullopt;
        }
        for (nlohmann::json::const_iterator entry = doc.begin(); entry != doc.end(); ++entry)
        {
            if (entry.key() != "name" && entry.key() != "events")
            {
                NS_LOG_WARN(Scene, "動作 {} に読み手のいない欄 {} があり、読まない", action.name, entry.key());
            }
        }
        action.events.reserve(events->size());
        for (std::size_t i = 0; i < events->size(); ++i)
        {
            std::string reason;
            std::optional<ActorActionEvent> event = ParseEvent((*events)[i], i, reason);
            // 1 つでも読めなければ動作ごと読まない。一部だけの動作は、置いた並びと違う演出になる
            if (!event.has_value())
            {
                error = std::format("動作 {} の {}", action.name, reason);
                return std::nullopt;
            }
            action.events.push_back(std::move(*event));
        }
        return action;
    }

    nlohmann::json ActorActionToJson(const ActorAction& action)
    {
        nlohmann::json events = nlohmann::json::array();
        for (const ActorActionEvent& event : action.events)
        {
            nlohmann::json row;
            WriteHitReactionItem(event.item, row);
            row["start"] = event.start;
            row["length"] = event.length;
            events.push_back(std::move(row));
        }
        nlohmann::json doc;
        doc["name"] = action.name;
        doc["events"] = std::move(events);
        return doc;
    }

    std::vector<ActorAction> ReadActorActions(std::string_view actorFileName, const nlohmann::json& actorFile)
    {
        std::vector<ActorAction> actions;
        const nlohmann::json* list = ActorFileActions(actorFile);
        if (list == nullptr)
        {
            return actions;
        }
        if (!list->is_array())
        {
            NS_LOG_ERROR(Scene, ".actor {} の動作の並び actions が配列でないので、動作を読まない", actorFileName);
            return actions;
        }
        std::set<std::string, std::less<>> names;
        for (std::size_t i = 0; i < list->size(); ++i)
        {
            std::string error;
            std::optional<ActorAction> action = ParseActorAction((*list)[i], error);
            if (!action.has_value())
            {
                NS_LOG_ERROR(Scene, ".actor {} の {} 番目の動作を読まない。{}", actorFileName, i, error);
                continue;
            }
            // 同じ名前を 2 つ持つと、状態が始める動作が並びの順で黙って決まる
            if (!names.insert(action->name).second)
            {
                NS_LOG_ERROR(Scene,
                             ".actor {} の {} 番目の動作を読まない。名前 {} が前の動作と重なる",
                             actorFileName,
                             i,
                             action->name);
                continue;
            }
            actions.push_back(std::move(*action));
        }
        return actions;
    }

    void WriteActorActions(nlohmann::json& actorFile, const std::vector<ActorAction>& actions)
    {
        nlohmann::json list = nlohmann::json::array();
        for (const ActorAction& action : actions)
        {
            list.push_back(ActorActionToJson(action));
        }
        SetActorFileActions(actorFile, std::move(list));
    }
} // namespace NS
