#include "NSlib/CoreObject/Reflection/TypeRegistry.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <cassert>
#include <memory>

namespace NS
{
    TypeRegistry& TypeRegistry::Get() noexcept
    {
        // 関数内 static で初期化順を確定させ、他の翻訳単位の静的登録より先に一覧の実体を用意する
        static TypeRegistry instance;
        return instance;
    }

    void TypeRegistry::Register(const char* className, ActorCreateFn create, const char* label)
    {
        if (className == nullptr)
        {
            NS_LOG_ERROR(Scene, "TypeRegistry::Register: className が nullptr");
            return;
        }

        // 静的初期化中に呼ばれ Logger がまだ無いので、二重登録は assert で即座に落とす
        const bool duplicate = (Find(className) != nullptr);
        assert(!duplicate && "class registered twice");
        if (duplicate)
        {
            return;
        }
        m_entries.push_back(Entry{className, create, label});
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

    namespace
    {
        // 一致する登録の型で作る。部品はまだ無い
        std::unique_ptr<SceneActor> ConstructOfClass(std::string_view className)
        {
            const TypeRegistry::Entry* entry = nullptr;
            if (!className.empty())
            {
                entry = TypeRegistry::Get().Find(className);
            }
            if (entry != nullptr && entry->create != nullptr)
            {
                return entry->create();
            }
            return std::make_unique<SceneActor>();
        }

        // class が正。登録の無い class は素の SceneActor で組むので、データの誤りとして知らせる
        void WarnIfUnregistered(std::string_view className)
        {
            if (className.empty())
            {
                return;
            }
            const TypeRegistry::Entry* entry = TypeRegistry::Get().Find(className);
            if (entry == nullptr || entry->create == nullptr)
            {
                NS_LOG_WARN(Scene, "CreateRegisteredObject: 未登録クラス {} を素の SceneActor で組む", className);
            }
        }
    } // namespace

    std::unique_ptr<SceneActor> CreateRegisteredObject(const nlohmann::json& object, Scene& scene)
    {
        // クラスは配置物の指す .actor の中の class だけが持つ。.actor が無ければ作るクラスが決まらない
        const std::string_view actorFile = ObjectJsonActorFile(object);
        const std::string className = ActorFileLibrary::Get().ClassNameOf(actorFile);
        if (className.empty())
        {
            NS_LOG_ERROR(Scene, "配置物 {} の .actor {} が見つからないので作らない", ObjectJsonId(object), actorFile);
            return nullptr;
        }
        WarnIfUnregistered(className);
        const ActorInitInfo info{.placement = &object, .scene = &scene, .actorFile = actorFile};
        return CreateActorOfClass(className, info);
    }

    std::unique_ptr<SceneActor> CreateActorOfClass(std::string_view className)
    {
        std::unique_ptr<SceneActor> actor = ConstructOfClass(className);
        actor->Init(nullptr);
        return actor;
    }

    std::unique_ptr<SceneActor> CreateActorOfClass(std::string_view className, const ActorInitInfo& info)
    {
        std::unique_ptr<SceneActor> actor = ConstructOfClass(className);
        actor->Init(&info);
        return actor;
    }

    std::unique_ptr<SceneActor> CreateActorWithCodeValues(std::string_view className)
    {
        const TypeRegistry::Entry* entry = TypeRegistry::Get().Find(className);
        if (entry == nullptr || entry->create == nullptr)
        {
            return nullptr;
        }
        std::unique_ptr<SceneActor> actor = entry->create();
        actor->InitWithCodeValues();
        return actor;
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
} // namespace NS
