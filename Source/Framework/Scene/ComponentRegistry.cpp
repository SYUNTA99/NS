#include "Framework/Scene/ComponentRegistry.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <algorithm>
#include <cassert>

namespace NS::Scene
{
    ComponentRegistry& ComponentRegistry::Get() noexcept
    {
        // 関数内 static で初期化順を確定させ、他 TU の静的登録より先に器を用意する
        static ComponentRegistry instance;
        return instance;
    }

    void ComponentRegistry::Register(const char* name, ComponentAttachFn attach)
    {
        // 静的初期化中に呼ばれ Logger がまだ無いので、二重登録は assert で即座に落とす
        const bool inserted = m_entries.emplace(name, attach).second;
        assert(inserted && "component type registered twice");
        static_cast<void>(inserted);
    }

    const std::unordered_map<std::string_view, ComponentAttachFn>& ComponentRegistry::Entries() const noexcept
    {
        return m_entries;
    }

    Component* CreateComponent(std::string_view typeName, GameObject& obj)
    {
        const auto& entries = ComponentRegistry::Get().Entries();
        const auto it = entries.find(typeName);
        if (it != entries.end())
            return it->second(obj);
        // 未登録の type 名は生成せず読み飛ばす。 不正な型注入を構造的に防ぐ第一の門
        NS_LOG_WARN(::NS::Core::LogCat::Game, "未登録のコンポーネント型 {} を読み飛ばす", typeName);
        return nullptr;
    }

    bool IsRegistered(std::string_view typeName) noexcept
    {
        const auto& entries = ComponentRegistry::Get().Entries();
        return entries.find(typeName) != entries.end();
    }

    const std::vector<std::string>& RegisteredNames()
    {
        // 登録は全て main 前の静的初期化で済むので、初回呼び出し時に確定した一覧を組める
        // map の走査順は不定なので、パレット表示が実行ごとに揺れないよう名前順へ揃える
        static const std::vector<std::string> names = [] {
            std::vector<std::string> result;
            const auto& entries = ComponentRegistry::Get().Entries();
            result.reserve(entries.size());
            for (const auto& [name, attach] : entries)
                result.emplace_back(name);
            std::sort(result.begin(), result.end());
            return result;
        }();
        return names;
    }
} // namespace NS::Scene
