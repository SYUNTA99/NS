#pragma once

// .actor の名前付き項目の並び (動作・名前の束) を読み書きする関数のテンプレート。
// 使うのは ActorAction.cpp と HitReaction.cpp だけなので、公開ヘッダ ActorFile.h に置かず、ログと書式の include を広げない

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ActorJson.h"

#include <cstddef>
#include <format>
#include <functional>
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
        template <typename T>
        struct ListedActorFileItem
        {
            std::optional<T> entry;
            std::string reason;
        };

        //! 名前付き項目の並びを要素ごとに読む。読むか読まないかの決まりは、読む時と書き戻す時でこの 1 か所を通す
        template <typename T>
        [[nodiscard]] std::vector<ListedActorFileItem<T>>
        ListNamedActorFileItems(const nlohmann::json& list,
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
        [[nodiscard]] std::vector<T>
        ReadNamedActorFileItems(std::string_view actorFileName,
                                const nlohmann::json* list,
                                std::optional<T> (*parse)(const nlohmann::json&, std::string&),
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
                NS_LOG_ERROR(
                    Scene, ".actor {} の{}の並び {} が配列でないので、{}を読まない", actorFileName, itemLabel, keyName, itemLabel);
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
        [[nodiscard]] nlohmann::json
        BuildNamedActorFileItemsJson(const nlohmann::json* current,
                                     const std::vector<T>& items,
                                     std::optional<T> (*parse)(const nlohmann::json&, std::string&),
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
    } // namespace detail
} // namespace NS
