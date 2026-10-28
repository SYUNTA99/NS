#include "NSlib/CoreObject/Action/ActorAction.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/detail/ActorFileItems.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <limits>
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

        // row の鍵 key の真偽を out へ読む。鍵が無ければ out は既定のまま true。真偽でなければ false
        [[nodiscard]] bool ReadOptionalBool(const nlohmann::json& row, std::string_view key, bool& out)
        {
            const nlohmann::json::const_iterator value = row.find(key);
            if (value == row.end())
            {
                return true;
            }
            if (!value->is_boolean())
            {
                return false;
            }
            out = value->get<bool>();
            return true;
        }

        // 入り切りの行 1 つを読む
        [[nodiscard]] std::optional<ActionSensorEvent> ParseSensorSwitch(const nlohmann::json& row,
                                                                         std::size_t index,
                                                                         std::string& error)
        {
            if (!row.is_object())
            {
                error = std::format("{} 番目の入り切りが object でない", index);
                return std::nullopt;
            }
            ActionSensorEvent event;
            if (!ReadHitReactionFrames(row, "start", event.start))
            {
                error = std::format("{} 番目の入り切りの始まり start が 0 以上の整数でない", index);
                return std::nullopt;
            }
            if (!ReadHitReactionFrames(row, "length", event.length))
            {
                error = std::format("{} 番目の入り切りの長さ length が 0 以上の整数でない", index);
                return std::nullopt;
            }
            // 既定の名前は持たない。名前はアクターのクラスの宣言が決める物で、NSlib は知らない
            const nlohmann::json::const_iterator sensor = row.find("sensor");
            if (sensor == row.end() || !sensor->is_string() || sensor->get_ref<const std::string&>().empty())
            {
                error = std::format("{} 番目の入り切りのセンサーの名前 sensor が空でない文字列でない", index);
                return std::nullopt;
            }
            event.sensor = sensor->get<std::string>();
            if (!ReadOptionalBool(row, "valid", event.valid))
            {
                error = std::format("{} 番目の入り切りの valid が真偽でない", index);
                return std::nullopt;
            }
            for (nlohmann::json::const_iterator entry = row.begin(); entry != row.end(); ++entry)
            {
                const std::string& key = entry.key();
                if (key != "sensor" && key != "valid" && key != "start" && key != "length")
                {
                    NS_LOG_WARN(Scene, "{} 番目の入り切りに読み手のいない欄 {} があり、読まない", index, key);
                }
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
        const nlohmann::json::const_iterator sensors = doc.find("sensors");
        if (sensors != doc.end() && !sensors->is_array())
        {
            error = std::format("動作 {} の入り切りの並び sensors が配列でない", action.name);
            return std::nullopt;
        }
        for (nlohmann::json::const_iterator entry = doc.begin(); entry != doc.end(); ++entry)
        {
            if (entry.key() != "name" && entry.key() != "length" && entry.key() != "events" &&
                entry.key() != "sensors")
            {
                NS_LOG_WARN(Scene, "動作 {} に読み手のいない欄 {} があり、読まない", action.name, entry.key());
            }
        }
        if (doc.contains("length") && !ReadHitReactionFrames(doc, "length", action.length))
        {
            error = std::format("動作 {} の長さ length が 0 以上の整数でない", action.name);
            return std::nullopt;
        }
        if (sensors != doc.end())
        {
            action.sensors.reserve(sensors->size());
            for (std::size_t i = 0; i < sensors->size(); ++i)
            {
                std::string reason;
                std::optional<ActionSensorEvent> sensor = ParseSensorSwitch((*sensors)[i], i, reason);
                if (!sensor.has_value())
                {
                    error = std::format("動作 {} の {}", action.name, reason);
                    return std::nullopt;
                }
                action.sensors.push_back(*sensor);
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
        // 長さを決めていない動作は鍵を書かない。状態の長さを持たない動作の形を変えない
        if (action.length > 0)
        {
            doc["length"] = action.length;
        }
        doc["events"] = std::move(events);
        // 入り切りの無い動作は鍵を書かない。入り切りを使わない .actor の形を変えない
        if (!action.sensors.empty())
        {
            nlohmann::json sensors = nlohmann::json::array();
            for (const ActionSensorEvent& sensor : action.sensors)
            {
                nlohmann::json row;
                row["sensor"] = sensor.sensor;
                row["valid"] = sensor.valid;
                row["start"] = sensor.start;
                row["length"] = sensor.length;
                sensors.push_back(std::move(row));
            }
            doc["sensors"] = std::move(sensors);
        }
        return doc;
    }

    namespace
    {
        // 始まりと長さを足した終わりのフレーム。int を越える終わりは int の最大にする
        int EndFrame(int start, int length) noexcept
        {
            const int64 end = static_cast<int64>(start) + static_cast<int64>(length);
            return static_cast<int>(std::min<int64>(end, std::numeric_limits<int>::max()));
        }
    } // namespace

    std::vector<ActorAction> ReadActorActions(std::string_view actorFileName, const nlohmann::json& actorFile)
    {
        return detail::ReadNamedActorFileItems(
            actorFileName, ActorFileActions(actorFile), &ParseActorAction, "actions", "動作");
    }

    void WriteActorActions(nlohmann::json& actorFile, const std::vector<ActorAction>& actions)
    {
        // 読まなかった動作は字面のまま同じ位置に残す。捨てると、直していない本人の値が .actor から消える
        SetActorFileActions(
            actorFile,
            detail::BuildNamedActorFileItemsJson(
                ActorFileActions(actorFile), actions, &ParseActorAction, &ActorActionToJson, "動作"));
    }

    int ActionLength(const ActorAction& action) noexcept
    {
        int length = 0;
        for (const ActorActionEvent& event : action.events)
        {
            length = std::max(length, EndFrame(event.start, event.length));
        }
        for (const ActionSensorEvent& event : action.sensors)
        {
            length = std::max(length, EndFrame(event.start, event.length));
        }
        return length;
    }
} // namespace NS
