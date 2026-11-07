#pragma once

// .actor の名前付き項目の並び (動作・名前の束) を読み書きする関数のテンプレート。
// 使うのは ActorActionInfo.cpp と HitReaction.cpp だけなので、公開ヘッダ ActorFile.h に置かず、ログと書式の include
// を広げない

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ItemVariant.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <functional>
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS
{
    namespace detail
    {
        //! .actor の名前付き項目の並びの要素 1 つを読んだ結果。読まない要素は entry が空で、reason に理由を持つ
        template <typename T> struct ListedActorFileItem
        {
            std::optional<T> entry;
            std::string reason;
        };

        //! 名前付き項目の並びを要素ごとに読む。読むか読まないかの決まりは、読む時と書き戻す時でこの 1 か所を通す
        template <typename T>
        [[nodiscard]] std::vector<ListedActorFileItem<T>> ListNamedActorFileItems(
            const nlohmann::json& list,
            std::optional<T> (*parse)(const nlohmann::json&, std::string&),
            std::string_view itemLabel)
        {
            std::vector<ListedActorFileItem<T>> listed(list.size());
            std::set<std::string, std::less<>> names;
            for (std::size_t i = 0; i < list.size(); ++i)
            {
                std::optional<T> entry = parse(list[i], listed[i].reason);
                if (!entry.has_value())
                {
                    continue;
                }
                // 同じ名前を 2 つ持つと、始める物が並びの順で黙って決まる
                if (!names.insert(entry->name).second)
                {
                    listed[i].reason = std::format("名前 {} が前の{}と重なる", entry->name, itemLabel);
                    continue;
                }
                listed[i].entry = std::move(entry);
            }
            return listed;
        }

        //! .actor の文書の名前付き項目の並びを読む。壊れた要素や重複名は ERROR に出して飛ばす
        template <typename T>
        [[nodiscard]] std::vector<T> ReadNamedActorFileItems(std::string_view actorFileName,
                                                             const nlohmann::json* list,
                                                             std::optional<T> (*parse)(const nlohmann::json&,
                                                                                       std::string&),
                                                             std::string_view keyName,
                                                             std::string_view itemLabel)
        {
            std::vector<T> items;
            if (list == nullptr)
            {
                return items;
            }
            if (!list->is_array())
            {
                NS_LOG_ERROR(Scene,
                             ".actor {} の{}の並び {} が配列でないので、{}を読まない",
                             actorFileName,
                             itemLabel,
                             keyName,
                             itemLabel);
                return items;
            }
            std::vector<ListedActorFileItem<T>> listed = ListNamedActorFileItems(*list, parse, itemLabel);
            for (std::size_t i = 0; i < listed.size(); ++i)
            {
                if (!listed[i].entry.has_value())
                {
                    NS_LOG_ERROR(
                        Scene, ".actor {} の {} 番目の{}を読まない。{}", actorFileName, i, itemLabel, listed[i].reason);
                    continue;
                }
                items.push_back(std::move(*listed[i].entry));
            }
            return items;
        }

        //! .actor の名前付き項目の並びを書き戻す。読まなかった要素は字面のまま同じ位置に残す
        template <typename T>
        [[nodiscard]] nlohmann::json BuildNamedActorFileItemsJson(const nlohmann::json* current,
                                                                  const std::vector<T>& items,
                                                                  std::optional<T> (*parse)(const nlohmann::json&,
                                                                                            std::string&),
                                                                  nlohmann::json (*toJson)(const T&),
                                                                  std::string_view itemLabel)
        {
            nlohmann::json list = nlohmann::json::array();
            std::size_t next = 0;
            if (current != nullptr && current->is_array())
            {
                const std::vector<ListedActorFileItem<T>> listed = ListNamedActorFileItems(*current, parse, itemLabel);
                for (std::size_t i = 0; i < listed.size(); ++i)
                {
                    if (!listed[i].entry.has_value())
                    {
                        list.push_back((*current)[i]);
                        continue;
                    }
                    if (next < items.size())
                    {
                        list.push_back(toJson(items[next]));
                        ++next;
                    }
                }
            }
            for (; next < items.size(); ++next)
            {
                list.push_back(toJson(items[next]));
            }
            return list;
        }

        //! 種類が欄の表の外に持つ値を、欄 fields から item へ読む関数。読んだ鍵は fields から外す。
        //! 壊れていれば error に理由を書いて false
        template <class Variant>
        using ReadExtraItemFields = bool (*)(Variant& item, nlohmann::json& fields, std::string& error);

        //! 1 行の JSON から項目の種類 type と欄 fields を読む。種類は Variant の種類の typeName で探す
        //! 欠けた欄は既定のまま、知らない欄と、type・fields・rowKeys の外の行の鍵は WARN を出して飛ばす
        //! @param[in] rowKeys 呼び手が同じ行から読む鍵 (長さ・始まりなど)
        //! @return 種類を知らない・欄の形が壊れている場合は std::nullopt で、error に理由を書く
        template <class Variant>
        [[nodiscard]] std::optional<Variant> ParseItemRow(const nlohmann::json& row,
                                                          std::initializer_list<std::string_view> rowKeys,
                                                          std::string& error,
                                                          ReadExtraItemFields<Variant> readExtra = nullptr)
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
            const std::string& typeName = type->get_ref<const std::string&>();
            std::optional<Variant> item = MakeItem<Variant>(typeName);
            if (!item.has_value())
            {
                error = std::format("種類 {} を知らない", typeName);
                return std::nullopt;
            }
            for (nlohmann::json::const_iterator entry = row.begin(); entry != row.end(); ++entry)
            {
                const bool known = entry.key() == "type" || entry.key() == "fields" ||
                                   std::find(rowKeys.begin(), rowKeys.end(), entry.key()) != rowKeys.end();
                if (!known)
                {
                    NS_LOG_WARN(Scene, "種類 {} の行に読み手のいない欄 {} があり、読まない", typeName, entry.key());
                }
            }
            const nlohmann::json::const_iterator fields = row.find("fields");
            if (fields == row.end())
            {
                return item;
            }
            if (!fields->is_object())
            {
                error = "欄 fields が object でない";
                return std::nullopt;
            }
            nlohmann::json reflected = *fields;
            if (readExtra != nullptr && !readExtra(*item, reflected, error))
            {
                return std::nullopt;
            }
            const ReflectionInfo* info = ItemReflection(*item);
            if (info != nullptr)
            {
                (void)ApplyReflectedFields(ItemFields(*item), *info, reflected);
            }
            else if (!reflected.empty())
            {
                NS_LOG_WARN(Scene, "種類 {} は欄を持たないので、欄を読まない", typeName);
            }
            return item;
        }

        //! item の種類 type と欄 fields を row へ書く。欄の無い種類は空の fields を書く
        template <class Variant> void WriteItemRow(const Variant& item, nlohmann::json& row)
        {
            row["type"] = ItemTypeName(item);
            nlohmann::json fields = nlohmann::json::object();
            const ReflectionInfo* info = ItemReflection(item);
            if (info != nullptr)
            {
                fields = SerializeReflectedFields(ItemFields(item), *info);
            }
            row["fields"] = std::move(fields);
        }
    } // namespace detail
} // namespace NS
