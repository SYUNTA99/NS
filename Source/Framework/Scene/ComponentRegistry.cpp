#include "Framework/Scene/ComponentRegistry.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"
#include "Framework/Scene/Components/BoxColliderComponent.h"
#include "Framework/Scene/Components/CapsuleColliderComponent.h"
#include "Framework/Scene/Components/HazardComponent.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/Components/PoleComponent.h"
#include "Framework/Scene/Components/SlopeColliderComponent.h"
#include "Framework/Scene/Components/SphereColliderComponent.h"
#include "Framework/Scene/GameObject.h"

namespace NS::Scene
{
    namespace
    {
        // 型名 → 生成 lambda の手書きエントリ。 名前はコンポの反射 typeName と一致させる
        struct Entry
        {
            const char* name;
            Component* (*attach)(GameObject&);
        };

        // curated subset。 既定 ctor を持つ型はその自然既定で構築する。 持たない型 (Slope/Pole/MeshRenderer) は
        // 読み込み時に反射 / BuildPlacedObject が上書きする placeholder 既定値を渡す
        // 除外型 (player / editor / camera 専用) はここに一切書かない
        const Entry kEntries[] = {
            {"BoxColliderComponent",
             [](GameObject& o) -> Component* { return o.AddComponent<BoxColliderComponent>(); }},
            {"SphereColliderComponent",
             [](GameObject& o) -> Component* { return o.AddComponent<SphereColliderComponent>(); }},
            {"CapsuleColliderComponent",
             [](GameObject& o) -> Component* { return o.AddComponent<CapsuleColliderComponent>(); }},
            {"SlopeColliderComponent",
             [](GameObject& o) -> Component* {
                 return o.AddComponent<SlopeColliderComponent>(45.0f, NS::Math::Vector3{0.5f, 0.5f, 0.5f});
             }},
            {"PoleComponent", [](GameObject& o) -> Component* { return o.AddComponent<PoleComponent>(0.15f, 2.0f); }},
            {"HazardComponent", [](GameObject& o) -> Component* { return o.AddComponent<HazardComponent>(); }},
            {"MeshRendererComponent",
             [](GameObject& o) -> Component* { return o.AddComponent<MeshRendererComponent>(nullptr, nullptr); }},
        };
    } // namespace

    Component* CreateComponent(std::string_view typeName, GameObject& obj)
    {
        for (const Entry& entry : kEntries)
        {
            if (typeName == entry.name)
                return entry.attach(obj);
        }
        // allowlist 外の type 名は生成せず読み飛ばす。 不正な型注入を構造的に防ぐ第一の門
        NS_LOG_WARN(::NS::Core::LogCat::Game, "未登録のコンポーネント型 {} を読み飛ばす", typeName);
        return nullptr;
    }

    bool IsRegistered(std::string_view typeName) noexcept
    {
        for (const Entry& entry : kEntries)
        {
            if (typeName == entry.name)
                return true;
        }
        return false;
    }

    const std::vector<std::string>& RegisteredNames()
    {
        static const std::vector<std::string> names = [] {
            std::vector<std::string> result;
            result.reserve(sizeof(kEntries) / sizeof(kEntries[0]));
            for (const Entry& entry : kEntries)
                result.emplace_back(entry.name);
            return result;
        }();
        return names;
    }
} // namespace NS::Scene
