#include "NSlib/Object/Reflection/TypeRegistry.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/ObjectJson.h"

#include <algorithm>
#include <cassert>
#include <memory>

namespace NS::Obj
{
    TypeRegistry& TypeRegistry::Get() noexcept
    {
        // 関数内 static で初期化順を確定させ、他の翻訳単位の静的登録より先に一覧の実体を用意する
        static TypeRegistry instance;
        return instance;
    }

    void TypeRegistry::Register(const char* className,
                                ActorCreateFn create,
                                SubObjectDefaultFn attach,
                                const char* label)
    {
        if (className == nullptr)
        {
            NS_LOG_ERROR(Scene, "TypeRegistry::Register: className が nullptr");
            return;
        }
        if ((create == nullptr) == (attach == nullptr))
        {
            return;
        }

        // 静的初期化中に呼ばれ Logger がまだ無いので、二重登録は assert で即座に落とす
        const bool duplicate = (Find(className) != nullptr);
        assert(!duplicate && "class registered twice");
        if (duplicate)
        {
            return;
        }
        // 置けるのは Actor だけ。SubObject に表示名を付けても一覧には出さない
        const char* placeableLabel = nullptr;
        if (create != nullptr)
        {
            placeableLabel = label;
        }
        m_entries.push_back(Entry{className, create, attach, placeableLabel});
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
            {
                return &entry;
            }
        }
        return nullptr;
    }

    std::unique_ptr<Actor> CreateRegisteredObject(const nlohmann::json& object)
    {
        // class が正。一致登録があればその型で作る
        const std::string_view className = ObjectJsonClass(object);
        if (!className.empty())
        {
            const TypeRegistry::Entry* entry = TypeRegistry::Get().Find(className);
            if (entry == nullptr || entry->create == nullptr)
            {
                NS_LOG_WARN(Scene, "CreateRegisteredObject: 未登録クラス {} を素の Actor で組む", className);
            }
        }
        return CreateActorOfClass(className);
    }

    std::unique_ptr<Actor> CreateActorOfClass(std::string_view className)
    {
        std::unique_ptr<Actor> actor;
        const TypeRegistry::Entry* entry = nullptr;
        if (!className.empty())
        {
            entry = TypeRegistry::Get().Find(className);
        }
        if (entry != nullptr && entry->create != nullptr)
        {
            actor = entry->create();
        }
        else
        {
            actor = std::make_unique<Actor>();
        }
        // 部品を作ってから返す。データの読み込みは返った部品へ欄を流し込む
        actor->EnsureInit();
        return actor;
    }

    std::unique_ptr<SubObject> CreateSubObjOfType(std::string_view typeName)
    {
        const TypeRegistry::Entry* entry = TypeRegistry::Get().Find(typeName);
        if (entry != nullptr && entry->createDefault != nullptr)
        {
            return entry->createDefault();
        }
        return nullptr;
    }

    const std::vector<const TypeRegistry::Entry*>& PlaceableEntries()
    {
        // 登録は全て main 前の静的初期化で済むので、初回呼び出し時に確定した一覧を組める
        // 静的初期化の順は翻訳単位で揺れるので、メニューが実行ごとに並び替わらないよう表示名の順へ揃える
        static const std::vector<const TypeRegistry::Entry*> entries = [] {
            std::vector<const TypeRegistry::Entry*> result;
            for (const TypeRegistry::Entry& entry : TypeRegistry::Get().Entries())
            {
                if (entry.label != nullptr)
                {
                    result.push_back(&entry);
                }
            }
            std::sort(result.begin(), result.end(), [](const TypeRegistry::Entry* a, const TypeRegistry::Entry* b) {
                return std::string_view{a->label} < std::string_view{b->label};
            });
            return result;
        }();
        return entries;
    }
} // namespace NS::Obj
