#include "NSlib/CoreObject/ClassObjs/HitReaction.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/IUse/IUseCamera.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <set>
#include <type_traits>
#include <utility>

namespace NS
{
    namespace
    {
        template <class Kind>
        concept HasReflectedFields = requires { Kind::StaticReflection(); };

        template <class Kind> void TryMakeItem(std::string_view name, std::optional<HitReactionItem>& result)
        {
            if (!result.has_value() && name == Kind{}.typeName)
            {
                result.emplace(std::in_place_type<Kind>);
            }
        }

        template <class... Kinds>
        [[nodiscard]] std::optional<HitReactionItem> MakeByName(std::string_view name,
                                                                std::type_identity<std::variant<Kinds...>>)
        {
            std::optional<HitReactionItem> result;
            (TryMakeItem<Kinds>(name, result), ...);
            return result;
        }

        // 持ち主へ渡す項目の値を書く欄の鍵。リフレクションの欄の表に無いので、欄を当てる前に外す
        constexpr const char* k_OwnerValuesKey = "値";

        // 欄の値の名前から値。数でない値があれば読まずに false
        [[nodiscard]] bool ReadOwnerValues(const nlohmann::json& values, std::map<std::string, float, std::less<>>& out)
        {
            if (!values.is_object())
            {
                return false;
            }
            for (nlohmann::json::const_iterator entry = values.begin(); entry != values.end(); ++entry)
            {
                if (!entry.value().is_number())
                {
                    return false;
                }
                out[entry.key()] = entry.value().get<float>();
            }
            return true;
        }

        // 欄 fields を item へ当てる。壊れていれば error に理由を書いて false
        [[nodiscard]] bool ApplyItemFields(HitReactionItem& item, const nlohmann::json& fields, std::string& error)
        {
            if (!fields.is_object())
            {
                error = "欄 fields が object でない";
                return false;
            }
            nlohmann::json reflected = fields;
            if (HitOwnerItem* owner = std::get_if<HitOwnerItem>(&item))
            {
                const nlohmann::json::const_iterator values = fields.find(k_OwnerValuesKey);
                if (values != fields.end())
                {
                    if (!ReadOwnerValues(*values, owner->values))
                    {
                        error = std::format("欄 {} が名前から数への object でない", k_OwnerValuesKey);
                        return false;
                    }
                    reflected.erase(k_OwnerValuesKey);
                }
            }
            const ReflectionInfo* info = HitReactionItemReflection(item);
            if (info != nullptr)
            {
                (void)ApplyReflectedFields(HitReactionItemFields(item), *info, reflected);
            }
            else if (!reflected.empty())
            {
                NS_LOG_WARN(Scene, "種類 {} は欄を持たないので、欄を読まない", HitReactionItemTypeName(item));
            }
            return true;
        }

        // doc の鍵のうち known に無い物を WARN で知らせる。読まずに飛ばす
        void WarnUnknownKeys(const nlohmann::json& doc,
                             std::initializer_list<std::string_view> known,
                             std::string_view where)
        {
            for (nlohmann::json::const_iterator entry = doc.begin(); entry != doc.end(); ++entry)
            {
                if (std::find(known.begin(), known.end(), entry.key()) == known.end())
                {
                    NS_LOG_WARN(Scene, "{} に読み手のいない欄 {} があり、読まない", where, entry.key());
                }
            }
        }

        // 項目の行 1 つを読む。束の項目は長さ length を持つ
        [[nodiscard]] std::optional<HitReactionEntry> ParseEntry(const nlohmann::json& row,
                                                                 std::size_t index,
                                                                 std::string& error)
        {
            std::string reason;
            std::optional<HitReactionItem> item = ParseHitReactionItem(row, {"length"}, reason);
            if (!item.has_value())
            {
                error = std::format("{} 番目の項目: {}", index, reason);
                return std::nullopt;
            }
            HitReactionEntry entry{.item = std::move(*item)};
            if (!ReadHitReactionFrames(row, "length", entry.length))
            {
                error = std::format("{} 番目の項目の長さ length が 0 以上の整数でない", index);
                return std::nullopt;
            }
            return entry;
        }

        // 揺れのフレーム数の上限。揺れの並びは始める時に全フレームぶんの領域を取るので、欄の打ち間違いの大きな値を止める
        // 1 秒を超える揺れは当たりの返りではなく、画面が揺れ続けている状態
        constexpr int k_MaxShakeFrames = 60;

        // 上限を超えた長さは警告して断る。カメラの無い場面は黙って断る
        bool CanStartCameraMotion(const SceneActor* owner, int frames)
        {
            if (frames > k_MaxShakeFrames)
            {
                NS_LOG_WARN(
                    Scene, "揺れのフレーム数 {} が上限 {} を超えていて、揺らさなかった", frames, k_MaxShakeFrames);
                return false;
            }
            return owner != nullptr && owner->GetCameraManager() != nullptr;
        }

        // 持ち主が nullptr の間は引かない。FindSceneSystem に nullptr を渡すと NS_ASSERT
        // で止まり、出荷版では検査されない
        template <class T> T* FindDirector(SceneActor* owner)
        {
            if (owner == nullptr)
            {
                return nullptr;
            }
            return FindSceneSystem<T>(owner);
        }
    } // namespace

    void HitReaction::CreateDirectors()
    {
        if (m_owner == nullptr)
        {
            return;
        }
        (void)GetOrCreateSceneSystem<HitScreenDirector>(m_owner);
        (void)GetOrCreateSceneSystem<PadRumbleDirector>(m_owner);
    }

    void HitReaction::StartFlash(int frames, float alpha) noexcept
    {
        if (HitScreenDirector* screen = FindDirector<HitScreenDirector>(m_owner))
        {
            screen->StartFlash(*m_owner, frames, alpha);
        }
    }

    bool HitReaction::StartShake(const CameraShakeDesc& desc)
    {
        if (desc.frames <= 0)
        {
            return true;
        }
        if (!CanStartCameraMotion(m_owner, desc.frames))
        {
            return false;
        }
        if (!StartCameraShake(m_owner, desc))
        {
            NS_LOG_WARN(Scene,
                        "揺れの設定が壊れていて、揺らさなかった: 横 {} 縦 {} フレーム数 {} 最長 {}",
                        desc.sideAmplitude,
                        desc.upAmplitude,
                        desc.frames,
                        desc.longestFlipFrames);
            return false;
        }
        return true;
    }

    bool HitReaction::StartSink(const CameraSinkDesc& desc)
    {
        if (desc.frames <= 0)
        {
            return true;
        }
        if (!CanStartCameraMotion(m_owner, desc.frames))
        {
            return false;
        }
        if (!AddCameraModifier(m_owner, CameraSinkModifier::Create(desc)))
        {
            NS_LOG_WARN(Scene,
                        "沈む揺れの設定が壊れていて、揺らさなかった: 深さ {} 震え {} 行き過ぎ {} フレーム数 {}",
                        desc.bottomPixels,
                        desc.tremblePixels,
                        desc.overshootRatio,
                        desc.frames);
            return false;
        }
        return true;
    }

    bool HitReaction::StartNudge(const CameraNudgeDesc& desc)
    {
        if (desc.frames <= 0)
        {
            return true;
        }
        if (!CanStartCameraMotion(m_owner, desc.frames))
        {
            return false;
        }
        if (!AddCameraModifier(m_owner, CameraNudgeModifier::Create(desc)))
        {
            NS_LOG_WARN(Scene,
                        "ずれの設定が壊れていて、ずらさなかった: 向き ({}, {}, {}) フレーム数 {}",
                        desc.direction.x,
                        desc.direction.y,
                        desc.direction.z,
                        desc.frames);
            return false;
        }
        return true;
    }

    bool HitReaction::StartZoomRoll(const CameraZoomRollDesc& desc)
    {
        if (m_owner == nullptr || m_owner->GetCameraManager() == nullptr)
        {
            return false;
        }
        if (!StartCameraZoomRoll(m_owner, desc))
        {
            NS_LOG_WARN(Scene,
                        "寄りと傾きの設定が壊れていて、寄せなかった: 倍率 {} 傾き {} 保つ {} 戻す {}",
                        desc.zoom,
                        desc.rollDegrees,
                        desc.holdFrames,
                        desc.returnFrames);
            return false;
        }
        return true;
    }

    void HitReaction::StartShakeLines(const HitShakeLinesDesc& desc) noexcept
    {
        if (HitScreenDirector* screen = FindDirector<HitScreenDirector>(m_owner))
        {
            screen->StartShakeLines(*m_owner, desc);
        }
    }

    void HitReaction::StartDistortionRing(const HitDistortionRingDesc& desc) noexcept
    {
        if (HitScreenDirector* screen = FindDirector<HitScreenDirector>(m_owner))
        {
            screen->StartDistortionRing(*m_owner, desc);
        }
    }

    void HitReaction::StopDistortionRing() noexcept
    {
        if (HitScreenDirector* screen = FindDirector<HitScreenDirector>(m_owner))
        {
            screen->StopDistortionRing(*m_owner);
        }
    }

    void HitReaction::StartPadVibration(const HitPadVibration& pad)
    {
        if (PadRumbleDirector* rumble = FindDirector<PadRumbleDirector>(m_owner))
        {
            rumble->Start(*m_owner, pad);
        }
    }

    void HitReaction::BlendPadVibration(const HitPadVibration& pad)
    {
        if (PadRumbleDirector* rumble = FindDirector<PadRumbleDirector>(m_owner))
        {
            rumble->Blend(*m_owner, pad);
        }
    }

    void HitReaction::Stop()
    {
        if (m_owner == nullptr)
        {
            return;
        }
        if (HitScreenDirector* screen = FindSceneSystem<HitScreenDirector>(m_owner))
        {
            screen->Stop(*m_owner);
        }
        if (PadRumbleDirector* rumble = FindSceneSystem<PadRumbleDirector>(m_owner))
        {
            rumble->Stop(*m_owner);
        }
        StopCameraHitEffects(m_owner);
    }

    bool HitReaction::AddTrauma(const CameraTraumaDesc& desc)
    {
        // カメラの無い場面 (試しの台) では揺らす先が無い。設定の誤りではないので黙って返す
        if (m_owner == nullptr || m_owner->GetCameraManager() == nullptr)
        {
            return false;
        }
        if (!AddCameraTrauma(m_owner, desc))
        {
            NS_LOG_WARN(Scene,
                        "トラウマの量か形が壊れていて、揺らさなかった: 量 {} 減る速さ {}",
                        desc.trauma,
                        desc.shape.decayPerSecond);
            return false;
        }
        return true;
    }

    void HitReaction::StopAll()
    {
        Stop();
        // プレイを終えた後の視点にトラウマを残さない
        if (m_owner != nullptr)
        {
            StopCameraEffects(m_owner);
        }
    }

    std::string_view HitReactionItemTypeName(const HitReactionItem& item) noexcept
    {
        return std::visit([]<class Kind>(const Kind& value) -> std::string_view { return value.typeName; }, item);
    }

    std::string_view HitReactionItemLabel(const HitReactionItem& item) noexcept
    {
        return std::visit([]<class Kind>(const Kind& value) -> std::string_view { return value.label; }, item);
    }

    std::optional<HitReactionItem> MakeHitReactionItem(std::string_view typeName)
    {
        return MakeByName(typeName, std::type_identity<HitReactionItem>{});
    }

    const ReflectionInfo* HitReactionItemReflection(const HitReactionItem& item) noexcept
    {
        return std::visit(
            []<class Kind>(const Kind&) -> const ReflectionInfo* {
                if constexpr (HasReflectedFields<Kind>)
                {
                    return Kind::StaticReflection();
                }
                else
                {
                    return nullptr;
                }
            },
            item);
    }

    void* HitReactionItemFields(HitReactionItem& item) noexcept
    {
        return std::visit([]<class Kind>(Kind& value) -> void* { return &value; }, item);
    }

    const void* HitReactionItemFields(const HitReactionItem& item) noexcept
    {
        return std::visit([]<class Kind>(const Kind& value) -> const void* { return &value; }, item);
    }

    bool ReadHitReactionFrames(const nlohmann::json& row, std::string_view key, int& out)
    {
        const nlohmann::json::const_iterator value = row.find(key);
        // 1.5 のような整数でない数と、整数の外の値も断る
        if (value == row.end() || !value->is_number_integer() || value->get<int>() < 0)
        {
            return false;
        }
        out = value->get<int>();
        return true;
    }

    std::optional<HitReactionItem> ParseHitReactionItem(const nlohmann::json& row,
                                                        std::initializer_list<std::string_view> rowKeys,
                                                        std::string& error)
    {
        if (!row.is_object())
        {
            error = "行が object でない";
            return std::nullopt;
        }
        const nlohmann::json::const_iterator type = row.find("type");
        if (type == row.end() || !type->is_string())
        {
            error = "種類 type が無い";
            return std::nullopt;
        }
        std::optional<HitReactionItem> item = MakeHitReactionItem(type->get<std::string>());
        if (!item.has_value())
        {
            error = std::format("種類 {} を知らない", type->get<std::string>());
            return std::nullopt;
        }
        for (nlohmann::json::const_iterator entry = row.begin(); entry != row.end(); ++entry)
        {
            const bool known = entry.key() == "type" || entry.key() == "fields" ||
                               std::find(rowKeys.begin(), rowKeys.end(), entry.key()) != rowKeys.end();
            if (!known)
            {
                NS_LOG_WARN(
                    Scene, "種類 {} の行に読み手のいない欄 {} があり、読まない", type->get<std::string>(), entry.key());
            }
        }
        const nlohmann::json::const_iterator fields = row.find("fields");
        if (fields != row.end() && !ApplyItemFields(*item, *fields, error))
        {
            return std::nullopt;
        }
        return item;
    }

    void WriteHitReactionItem(const HitReactionItem& item, nlohmann::json& row)
    {
        row["type"] = HitReactionItemTypeName(item);
        nlohmann::json fields = nlohmann::json::object();
        const ReflectionInfo* info = HitReactionItemReflection(item);
        if (info != nullptr)
        {
            fields = SerializeReflectedFields(HitReactionItemFields(item), *info);
        }
        if (const HitOwnerItem* owner = std::get_if<HitOwnerItem>(&item))
        {
            nlohmann::json values = nlohmann::json::object();
            for (const std::pair<const std::string, float>& value : owner->values)
            {
                values[value.first] = value.second;
            }
            fields[k_OwnerValuesKey] = std::move(values);
        }
        row["fields"] = std::move(fields);
    }

    std::optional<HitReactionSet> ParseHitReactionSet(const nlohmann::json& doc, std::string& error)
    {
        if (!doc.is_object())
        {
            error = "束が object でない";
            return std::nullopt;
        }
        HitReactionSet set;
        const nlohmann::json::const_iterator name = doc.find("name");
        if (name == doc.end() || !name->is_string() || name->get_ref<const std::string&>().empty())
        {
            error = "名前 name が空でない文字列でない";
            return std::nullopt;
        }
        set.name = name->get<std::string>();
        if (doc.contains("stopFrames") && !ReadHitReactionFrames(doc, "stopFrames", set.stopFrames))
        {
            error = std::format("束 {} の止めの長さ stopFrames が 0 以上の整数でない", set.name);
            return std::nullopt;
        }
        const nlohmann::json::const_iterator items = doc.find("items");
        if (items == doc.end() || !items->is_array())
        {
            error = std::format("束 {} の項目の並び items が配列でない", set.name);
            return std::nullopt;
        }
        WarnUnknownKeys(doc, {"name", "stopFrames", "items"}, std::format("束 {}", set.name));
        set.entries.reserve(items->size());
        for (std::size_t i = 0; i < items->size(); ++i)
        {
            std::string reason;
            std::optional<HitReactionEntry> entry = ParseEntry((*items)[i], i, reason);
            // 1 項目でも読めなければ束ごと読まない。一部だけの束は、書いた物と違う演出になる
            if (!entry.has_value())
            {
                error = std::format("束 {} の {}", set.name, reason);
                return std::nullopt;
            }
            set.entries.push_back(std::move(*entry));
        }
        return set;
    }

    nlohmann::json HitReactionSetToJson(const HitReactionSet& set)
    {
        nlohmann::json items = nlohmann::json::array();
        for (const HitReactionEntry& entry : set.entries)
        {
            nlohmann::json row;
            WriteHitReactionItem(entry.item, row);
            row["length"] = entry.length;
            items.push_back(std::move(row));
        }
        nlohmann::json doc;
        doc["name"] = set.name;
        doc["stopFrames"] = set.stopFrames;
        doc["items"] = std::move(items);
        return doc;
    }

    std::vector<HitReactionSet> ReadHitReactionSets(std::string_view actorFileName, const nlohmann::json& actorFile)
    {
        std::vector<HitReactionSet> sets;
        const nlohmann::json* list = ActorFileHitReactions(actorFile);
        if (list == nullptr)
        {
            return sets;
        }
        if (!list->is_array())
        {
            NS_LOG_ERROR(Scene, ".actor {} の束の並び hitReactions が配列でないので、束を読まない", actorFileName);
            return sets;
        }
        std::set<std::string, std::less<>> names;
        for (std::size_t i = 0; i < list->size(); ++i)
        {
            std::string error;
            std::optional<HitReactionSet> set = ParseHitReactionSet((*list)[i], error);
            if (!set.has_value())
            {
                NS_LOG_ERROR(Scene, ".actor {} の {} 番目の束を読まない。{}", actorFileName, i, error);
                continue;
            }
            // 同じ名前を 2 つ持つと、始める束が並びの順で黙って決まる
            if (!names.insert(set->name).second)
            {
                NS_LOG_ERROR(
                    Scene, ".actor {} の {} 番目の束を読まない。名前 {} が前の束と重なる", actorFileName, i, set->name);
                continue;
            }
            sets.push_back(std::move(*set));
        }
        return sets;
    }

    void WriteHitReactionSets(nlohmann::json& actorFile, const std::vector<HitReactionSet>& sets)
    {
        nlohmann::json list = nlohmann::json::array();
        for (const HitReactionSet& set : sets)
        {
            list.push_back(HitReactionSetToJson(set));
        }
        SetActorFileHitReactions(actorFile, std::move(list));
    }
} // namespace NS
