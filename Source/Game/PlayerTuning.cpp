#include "Game/PlayerTuning.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Scene/Component.h"
#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Reflection.h"
#include "Framework/Scene/ReflectionJson.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/LevelJson.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <string>
#include <utility>

std::filesystem::path PlayerTuningPath()
{
    // Assets 配下に置けば出荷パッケージにも同梱され、 開発時はリポジトリ直下、 出荷時は実行ファイル隣を
    // 同じ相対パスで解決できる
    return ::NS::Core::FileSystem::ContentRoot() / "Assets" / "PlayerTuning.json";
}

void MergePlayerTuningText(NS::Game::Level::ObjectInstance& playerObject, std::string_view jsonText) noexcept
{
    const nlohmann::json json = nlohmann::json::parse(jsonText, nullptr, false);
    if (json.is_discarded())
    {
        NS_LOG_WARN(::NS::Core::LogCat::Game, "PlayerTuning の解析に失敗、 既定の構成と値で続行");
        return;
    }

    const auto components = json.find("components");
    if (components == json.end() || !components->is_array())
        return;

    for (const nlohmann::json& entry : *components)
    {
        const auto typeIt = entry.find("type");
        const auto fieldsIt = entry.find("fields");
        if (typeIt == entry.end() || !typeIt->is_string() || fieldsIt == entry.end() || !fieldsIt->is_object())
            continue;
        const std::string typeName = typeIt->get<std::string>();

        // 型名が一致する既存データへ写す。 同型は最初の 1 件だけが対象で、 無い型は構成ごと追加する
        NS::Game::Level::ComponentData* target = nullptr;
        for (NS::Game::Level::ComponentData& component : playerObject.components)
        {
            if (component.typeName == typeName)
            {
                target = &component;
                break;
            }
        }
        if (target == nullptr)
        {
            playerObject.components.push_back(NS::Game::Level::ComponentData{typeName, {}});
            target = &playerObject.components.back();
        }

        for (const auto& [name, value] : fieldsIt->items())
        {
            NS::Game::Level::FieldValue parsed;
            if (!NS::Game::Level::JsonToFieldValue(name, value, parsed))
                continue;
            bool replaced = false;
            for (NS::Game::Level::FieldValue& field : target->fields)
            {
                if (field.name == parsed.name)
                {
                    field.value = std::move(parsed.value);
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
                target->fields.push_back(std::move(parsed));
        }
    }
}

void MergeSavedPlayerTuning(NS::Game::Level::ObjectInstance& playerObject) noexcept
{
    const auto path = PlayerTuningPath();
    if (!::NS::Core::FileSystem::Exists(path))
        return; // 無ければコード既定の構成と値をそのまま使う

    const auto bytes = ::NS::Core::FileSystem::ReadAllBytes(path);
    if (!bytes.has_value())
        return;

    MergePlayerTuningText(playerObject, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
}

void ApplyPlayerObjectComponents(NS::Scene::GameObject& player,
                                 const NS::Game::Level::ObjectInstance& playerObject,
                                 bool startCreated) noexcept
{
    for (const NS::Game::Level::ComponentData& component : playerObject.components)
    {
        // 型名が一致する既存コンポーネントへ適用する。 同型は最初の 1 件だけが対象
        NS::Scene::Component* target = nullptr;
        for (NS::Scene::Component* comp : player.Components())
        {
            if (comp == nullptr)
                continue;
            const NS::Scene::ReflectionInfo* info = comp->GetReflection();
            if (info != nullptr && component.typeName == info->typeName)
            {
                target = comp;
                break;
            }
        }
        // 無い型は登録 factory で生成して構成へ加える。 editor の Add Component がデータへ足した
        // 追加分はこの経路で live へ現れる。 未登録型はここで読み飛ばされる
        bool created = false;
        if (target == nullptr)
        {
            target = NS::Scene::CreateComponent(component.typeName, player);
            created = target != nullptr;
        }
        if (target == nullptr)
            continue;
        NS::Scene::ApplyJsonFields(*target, NS::Game::Level::ComponentFieldsToJson(component));
        // player 稼働後にデータへ足された component は自分で開始する。 OnStart 前の適用は player 側がまとめて開始する
        if (created && startCreated)
            target->OnStart();
    }
}
