#include "Game/Level/HitTimeline.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Windows/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <span>
#include <type_traits>
#include <utility>

namespace NS::Game::Level
{
    namespace
    {
        template <class Kind>
        concept HasReflectedFields = requires { Kind::StaticReflection(); };

        template <HasReflectedFields Kind>
        [[nodiscard]] const NS::Obj::ReflectionInfo* ReflectionOf(const Kind&) noexcept
        {
            return Kind::StaticReflection();
        }

        template <class Kind>
            requires(!HasReflectedFields<Kind>)
        [[nodiscard]] const NS::Obj::ReflectionInfo* ReflectionOf(const Kind&) noexcept
        {
            return nullptr;
        }

        template <class Kind> void TryMakeEvent(std::string_view name, std::optional<HitEventValue>& result)
        {
            if (!result.has_value() && name == Kind{}.typeName)
            {
                result.emplace(std::in_place_type<Kind>);
            }
        }

        template <class... Kinds>
        [[nodiscard]] std::optional<HitEventValue> MakeByName(std::string_view name,
                                                              std::type_identity<std::variant<Kinds...>>)
        {
            std::optional<HitEventValue> result;
            (TryMakeEvent<Kinds>(name, result), ...);
            return result;
        }

        // 整数でない数 (1.5 など) と整数の外の値を断る
        [[nodiscard]] bool ReadInteger(const nlohmann::json& value, int& out) noexcept
        {
            if (!value.is_number_integer())
            {
                return false;
            }
            out = value.get<int>();
            return true;
        }

        // 事象 1 行を読む。読めなければ error に理由を書いて std::nullopt
        [[nodiscard]] std::optional<HitEvent> ParseEvent(const nlohmann::json& row,
                                                         std::size_t index,
                                                         std::string& error)
        {
            if (!row.is_object())
            {
                error = std::format("{} 行目が object でない", index);
                return std::nullopt;
            }
            const nlohmann::json::const_iterator type = row.find("type");
            if (type == row.end() || !type->is_string())
            {
                error = std::format("{} 行目に種類 type が無い", index);
                return std::nullopt;
            }
            std::optional<HitEventValue> value = MakeHitEventValue(type->get<std::string>());
            if (!value.has_value())
            {
                error = std::format("{} 行目の種類 {} を知らない", index, type->get<std::string>());
                return std::nullopt;
            }

            HitEvent event{.value = std::move(*value)};
            const nlohmann::json::const_iterator start = row.find("start");
            if (start == row.end() || !ReadInteger(*start, event.start))
            {
                error = std::format("{} 行目の始まり start が整数でない", index);
                return std::nullopt;
            }
            if (event.start < 0 && !CanStartBeforeContact(event.value))
            {
                error = std::format(
                    "{} 行目の {} は触れる前 (マイナスのフレーム) に置けない", index, type->get<std::string>());
                return std::nullopt;
            }
            const nlohmann::json::const_iterator length = row.find("length");
            if (length == row.end() || !ReadInteger(*length, event.length) || event.length < 0)
            {
                error = std::format("{} 行目の長さ length が 0 以上の整数でない", index);
                return std::nullopt;
            }
            const nlohmann::json::const_iterator direction = row.find("direction");
            if (direction != row.end())
            {
                std::optional<HitDirection> parsed;
                if (direction->is_string())
                {
                    parsed = ParseHitDirection(direction->get<std::string>());
                }
                if (!parsed.has_value())
                {
                    error = std::format("{} 行目の向き direction を知らない", index);
                    return std::nullopt;
                }
                event.direction = *parsed;
            }
            const nlohmann::json::const_iterator fields = row.find("fields");
            if (fields != row.end())
            {
                if (!fields->is_object())
                {
                    error = std::format("{} 行目の欄 fields が object でない", index);
                    return std::nullopt;
                }
                const NS::Obj::ReflectionInfo* info = HitEventReflection(event.value);
                if (info != nullptr)
                {
                    (void)NS::Obj::ApplyReflectedFields(HitEventFields(event.value), *info, *fields);
                }
                else if (!fields->empty())
                {
                    NS_LOG_WARN(Game,
                                "当たりのタイムラインの {} 行目の種類 {} は欄を持たないので、欄を読まない",
                                index,
                                HitEventTypeName(event.value));
                }
            }
            return event;
        }
    } // namespace

    std::string_view HitEventTypeName(const HitEventValue& value) noexcept
    {
        return std::visit([]<class Kind>(const Kind& event) -> std::string_view { return event.typeName; }, value);
    }

    std::string_view HitEventLabel(const HitEventValue& value) noexcept
    {
        return std::visit([]<class Kind>(const Kind& event) -> std::string_view { return event.label; }, value);
    }

    bool CanStartBeforeContact(const HitEventValue& value) noexcept
    {
        return std::visit([]<class Kind>(const Kind& event) -> bool { return event.beforeContact; }, value);
    }

    std::optional<HitEventValue> MakeHitEventValue(std::string_view typeName)
    {
        return MakeByName(typeName, std::type_identity<HitEventValue>{});
    }

    const NS::Obj::ReflectionInfo* HitEventReflection(const HitEventValue& value) noexcept
    {
        return std::visit(
            []<class Kind>(const Kind& event) -> const NS::Obj::ReflectionInfo* { return ReflectionOf(event); }, value);
    }

    void* HitEventFields(HitEventValue& value) noexcept
    {
        return std::visit([]<class Kind>(Kind& event) -> void* { return &event; }, value);
    }

    const void* HitEventFields(const HitEventValue& value) noexcept
    {
        return std::visit([]<class Kind>(const Kind& event) -> const void* { return &event; }, value);
    }

    HitDirection HitDirectionOf(float u, float v) noexcept
    {
        // 斜めの境目は左右へ倒す。どちらへ倒しても当たり方は変わらず、帯の行を引く向きが決まればよい
        if (std::abs(u) >= std::abs(v))
        {
            if (u >= 0.0f)
            {
                return HitDirection::Right;
            }
            return HitDirection::Left;
        }
        if (v >= 0.0f)
        {
            return HitDirection::Up;
        }
        return HitDirection::Down;
    }

    float HitDirectionWeight(HitDirection direction, float u, float v) noexcept
    {
        if (direction == HitDirection::Any)
        {
            return 1.0f;
        }
        // 真ん中は角度が決まらない。帯の行を引く向きと同じ 1 つに全部を渡す
        if (u == 0.0f && v == 0.0f)
        {
            if (direction == HitDirectionOf(u, v))
            {
                return 1.0f;
            }
            return 0.0f;
        }
        float center = 0.0f;
        switch (direction)
        {
        case HitDirection::Up:
            center = 90.0f;
            break;
        case HitDirection::Left:
            center = 180.0f;
            break;
        case HitDirection::Down:
            center = 270.0f;
            break;
        default:
            break;
        }
        const float angle = NS::ToDegrees(NS::Radians{std::atan2(v, u)}).value;
        // 向きの角度からのずれを -180〜180 へ畳む。90 度離れると 0
        float difference = std::fmod(angle - center, 360.0f);
        if (difference > 180.0f)
        {
            difference -= 360.0f;
        }
        if (difference < -180.0f)
        {
            difference += 360.0f;
        }
        return std::max(1.0f - std::abs(difference) / 90.0f, 0.0f);
    }

    std::string_view HitDirectionName(HitDirection direction) noexcept
    {
        switch (direction)
        {
        case HitDirection::Right:
            return "right";
        case HitDirection::Left:
            return "left";
        case HitDirection::Up:
            return "up";
        case HitDirection::Down:
            return "down";
        default:
            return "any";
        }
    }

    std::optional<HitDirection> ParseHitDirection(std::string_view name) noexcept
    {
        for (int index = static_cast<int>(HitDirection::Any); index <= static_cast<int>(HitDirection::Down); ++index)
        {
            const HitDirection direction = static_cast<HitDirection>(index);
            if (HitDirectionName(direction) == name)
            {
                return direction;
            }
        }
        return std::nullopt;
    }

    std::optional<HitTimeline> ParseHitTimeline(const nlohmann::json& doc, std::string& error)
    {
        if (!doc.is_object())
        {
            error = "全体が object でない";
            return std::nullopt;
        }
        HitTimeline timeline;
        const nlohmann::json::const_iterator version = doc.find("version");
        int versionNumber = 0;
        if (version == doc.end() || !ReadInteger(*version, versionNumber) || versionNumber != timeline.version)
        {
            error = std::format("版 version が {} でない", timeline.version);
            return std::nullopt;
        }
        const nlohmann::json::const_iterator events = doc.find("events");
        if (events == doc.end() || !events->is_array())
        {
            error = "事象の並び events が配列でない";
            return std::nullopt;
        }
        timeline.events.reserve(events->size());
        for (std::size_t i = 0; i < events->size(); ++i)
        {
            std::optional<HitEvent> event = ParseEvent((*events)[i], i, error);
            // 1 行でも読めなければファイルごと読まない。一部だけの返りは、置いた並びと違う当たりになる
            if (!event.has_value())
            {
                return std::nullopt;
            }
            timeline.events.push_back(std::move(*event));
        }
        return timeline;
    }

    nlohmann::json HitTimelineToJson(const HitTimeline& timeline)
    {
        nlohmann::json events = nlohmann::json::array();
        for (const HitEvent& event : timeline.events)
        {
            nlohmann::json row;
            row["type"] = HitEventTypeName(event.value);
            row["start"] = event.start;
            row["length"] = event.length;
            row["direction"] = HitDirectionName(event.direction);
            const NS::Obj::ReflectionInfo* info = HitEventReflection(event.value);
            if (info != nullptr)
            {
                row["fields"] = NS::Obj::SerializeReflectedFields(HitEventFields(event.value), *info);
            }
            else
            {
                row["fields"] = nlohmann::json::object();
            }
            events.push_back(std::move(row));
        }
        nlohmann::json doc;
        doc["version"] = timeline.version;
        doc["events"] = std::move(events);
        return doc;
    }

    std::string_view HitTimelineNameOf(HitTier tier) noexcept
    {
        // 段ごとに 1 行。段を足したら行を足す
        switch (tier)
        {
        case HitTier::Center:
            return "center";
        case HitTier::Wide:
            return "miss";
        }
        return {};
    }

    HitTimelineLibrary& HitTimelineLibrary::Get()
    {
        static HitTimelineLibrary instance;
        return instance;
    }

    const std::string& HitTimelineLibrary::Directory()
    {
        if (m_directory.empty())
        {
            m_directory = NS::OS::FileSystem::Combine(
                NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "HitTimelines");
        }
        return m_directory;
    }

    void HitTimelineLibrary::SetDirectory(std::string directory)
    {
        m_directory = std::move(directory);
        Reload();
    }

    void HitTimelineLibrary::EnsureLoaded()
    {
        if (!m_loaded)
        {
            Reload();
        }
    }

    void HitTimelineLibrary::Reload()
    {
        m_timelines.clear();
        m_reportedTiers.clear();
        m_loaded = true;

        const std::string& directory = Directory();
        if (!NS::OS::FileSystem::IsDirectory(directory))
        {
            NS_LOG_ERROR(Game, "当たりのタイムラインの置き場 {} が無い", directory);
            return;
        }
        for (const std::string& path : NS::OS::FileSystem::ListFiles(directory, ".json"))
        {
            const std::optional<std::string> text = NS::OS::FileSystem::ReadAllText(path);
            if (!text.has_value())
            {
                NS_LOG_ERROR(Game, "当たりのタイムライン {} を読めない", path);
                continue;
            }
            if (text->size() > m_maxFileBytes)
            {
                NS_LOG_ERROR(
                    Game, "当たりのタイムライン {} が上限 ({} byte) を超えるので読まない", path, m_maxFileBytes);
                continue;
            }
            const nlohmann::json root = nlohmann::json::parse(*text, nullptr, false);
            if (root.is_discarded())
            {
                NS_LOG_ERROR(Game, "当たりのタイムライン {} の JSON を読めない", path);
                continue;
            }
            std::string error;
            std::optional<HitTimeline> timeline = ParseHitTimeline(root, error);
            if (!timeline.has_value())
            {
                NS_LOG_ERROR(Game, "当たりのタイムライン {} が壊れている: {}", path, error);
                continue;
            }
            m_timelines[NS::OS::FileSystem::Stem(path)] = std::move(*timeline);
        }
    }

    const HitTimeline* HitTimelineLibrary::Find(std::string_view name)
    {
        EnsureLoaded();
        const std::map<std::string, HitTimeline, std::less<>>::const_iterator it = m_timelines.find(name);
        if (it == m_timelines.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    const HitTimeline* HitTimelineLibrary::FindForTier(HitTier tier)
    {
        const std::string_view name = HitTimelineNameOf(tier);
        const HitTimeline* timeline = nullptr;
        if (!name.empty())
        {
            timeline = Find(name);
        }
        if (timeline == nullptr && m_reportedTiers.insert(static_cast<int>(tier)).second)
        {
            NS_LOG_ERROR(Game,
                         "段 {} の当たりのタイムライン {} が無いか壊れていて、返りを出さない",
                         static_cast<int>(tier),
                         name);
        }
        return timeline;
    }

    void HitTimelineLibrary::Set(std::string_view name, HitTimeline timeline)
    {
        EnsureLoaded();
        if (name.empty())
        {
            return;
        }
        m_timelines[std::string{name}] = std::move(timeline);
    }

    bool HitTimelineLibrary::Save(std::string_view name)
    {
        const HitTimeline* timeline = Find(name);
        if (timeline == nullptr)
        {
            return false;
        }
        // 種類の既定値と同じく辞書順のキーで書く。同じ中身の 2 回の保存は byte 一致になる
        const std::string text =
            HitTimelineToJson(*timeline).dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        const std::string path = NS::OS::FileSystem::Combine(Directory(), std::string{name} + ".json");
        const std::byte* raw = reinterpret_cast<const std::byte*>(text.data());
        if (!NS::OS::FileSystem::WriteAllBytes(path, std::span<const std::byte>(raw, text.size())))
        {
            NS_LOG_ERROR(Game, "当たりのタイムライン {} を書けない", path);
            return false;
        }
        return true;
    }
} // namespace NS::Game::Level
