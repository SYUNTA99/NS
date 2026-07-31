#include "Runtime/Object/Reflection/TypeRegistry.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/SceneData.h"

#include <algorithm>
#include <cassert>
#include <memory>

namespace NS::Object
{
    TypeRegistry& TypeRegistry::Get() noexcept
    {
        // 関数内 static で初期化順を確定させ、他 TU の静的登録より先に器を用意する
        static TypeRegistry instance;
        return instance;
    }

    void TypeRegistry::Register(const char* className, GameObjectCreateFn create, ComponentAttachFn attach)
    {
        if (className == nullptr)
            return;
        if ((create == nullptr) == (attach == nullptr))
            return;
        // 静的初期化中に呼ばれ Logger がまだ無いので、二重登録は assert で即座に落とす
        const bool duplicate = (Find(className) != nullptr);
        assert(!duplicate && "class registered twice");
        if (duplicate)
            return;
        m_entries.push_back(Entry{className, create, attach});
    }

    const std::vector<TypeRegistry::Entry>& TypeRegistry::Entries() const noexcept
    {
        return m_entries;
    }

    const TypeRegistry::Entry* TypeRegistry::Find(std::string_view className) const noexcept
    {
        for (const Entry& entry : m_entries)
        {
            if (className == entry.className)
                return &entry;
        }
        return nullptr;
    }

    std::unique_ptr<GameObject> CreateRegisteredObject(const ObjectData& object)
    {
        // className が正。 一致登録があればその器で作る
        if (!object.className.empty())
        {
            const TypeRegistry::Entry* entry = TypeRegistry::Get().Find(object.className);
            if (entry != nullptr && entry->create != nullptr)
                return entry->create();
            NS_LOG_WARN(Scene, "CreateRegisteredObject: 未登録クラス {} を素の GameObject で組む", object.className);
        }
        return std::make_unique<GameObject>();
    }

    Component* CreateComponent(std::string_view typeName, GameObject& obj)
    {
        const TypeRegistry::Entry* entry = TypeRegistry::Get().Find(typeName);
        if (entry != nullptr && entry->attach != nullptr)
            return entry->attach(obj);
        // 未登録の type 名は生成せず読み飛ばす。 不正な型注入を構造的に防ぐ第一の門
        NS_LOG_WARN(Scene, "未登録のコンポーネント型 {} を読み飛ばす", typeName);
        return nullptr;
    }

    bool IsRegistered(std::string_view typeName) noexcept
    {
        const TypeRegistry::Entry* entry = TypeRegistry::Get().Find(typeName);
        return entry != nullptr && entry->attach != nullptr;
    }

    const std::vector<std::string>& RegisteredNames()
    {
        // 登録は全て main 前の静的初期化で済むので、初回呼び出し時に確定した一覧を組める
        // パレット表示が実行ごとに揺れないよう名前順へ揃える
        static const std::vector<std::string> names = [] {
            std::vector<std::string> result;
            for (const TypeRegistry::Entry& entry : TypeRegistry::Get().Entries())
            {
                if (entry.attach != nullptr)
                    result.emplace_back(entry.className);
            }
            std::sort(result.begin(), result.end());
            return result;
        }();
        return names;
    }
} // namespace NS::Object
