#include "GameCore/PlayerTuning.h"

#include "GameCore/Level/LevelData.h"
#include "GameCore/Level/LevelJson.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

std::filesystem::path PlayerTuningPath()
{
    // Assets 配下に置けば出荷パッケージにも同梱され、 開発時はリポジトリ直下、 出荷時は実行ファイル隣を
    // 同じ相対パスで解決できる
    return ::NS::Core::FileSystem::ContentRoot() / "Assets" / "PlayerTuning.json";
}

void MergePlayerTuningText(NS::GameCore::Level::ObjectInstance& playerObject, std::string_view jsonText) noexcept
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
        NS::GameCore::Level::ComponentData* target = nullptr;
        for (NS::GameCore::Level::ComponentData& component : playerObject.components)
        {
            if (component.typeName == typeName)
            {
                target = &component;
                break;
            }
        }
        if (target == nullptr)
        {
            playerObject.components.push_back(NS::GameCore::Level::ComponentData{typeName, {}});
            target = &playerObject.components.back();
        }

        for (const auto& [name, value] : fieldsIt->items())
        {
            NS::GameCore::Level::FieldValue parsed;
            if (!NS::GameCore::Level::JsonToFieldValue(name, value, parsed))
                continue;
            bool replaced = false;
            for (NS::GameCore::Level::FieldValue& field : target->fields)
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

void MergeSavedPlayerTuning(NS::GameCore::Level::ObjectInstance& playerObject) noexcept
{
    const auto path = PlayerTuningPath();
    if (!::NS::Core::FileSystem::Exists(path))
        return; // 無ければコード既定の構成と値をそのまま使う

    const auto bytes = ::NS::Core::FileSystem::ReadAllBytes(path);
    if (!bytes.has_value())
        return;

    MergePlayerTuningText(playerObject, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
}
