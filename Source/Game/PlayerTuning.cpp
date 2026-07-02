#include "Game/PlayerTuning.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Scene/Component.h"
#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Reflection.h"
#include "Framework/Scene/ReflectionJson.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <string>

std::filesystem::path PlayerTuningPath()
{
    // Assets 配下に置けば出荷パッケージにも同梱され、 開発時はリポジトリ直下、 出荷時は実行ファイル隣を
    // 同じ相対パスで解決できる
    return ::NS::Core::FileSystem::ContentRoot() / "Assets" / "PlayerTuning.json";
}

void ApplyPlayerTuningText(NS::Scene::GameObject& player, std::string_view jsonText) noexcept
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
        if (typeIt == entry.end() || !typeIt->is_string() || fieldsIt == entry.end())
            continue;
        const std::string typeName = typeIt->get<std::string>();

        // 型名が一致する既存コンポーネントへ適用する。 同型は最初の 1 件だけが対象
        NS::Scene::Component* target = nullptr;
        for (NS::Scene::Component* comp : player.Components())
        {
            if (comp == nullptr)
                continue;
            const NS::Scene::ReflectionInfo* info = comp->GetReflection();
            if (info != nullptr && typeName == info->typeName)
            {
                target = comp;
                break;
            }
        }
        // 無い型は登録 factory で生成して構成へ加える。 editor の Add Component が保存した
        // 追加分はこの経路で次回起動時に復元される。 未登録型はここで読み飛ばされる
        if (target == nullptr)
            target = NS::Scene::CreateComponent(typeName, player);
        if (target != nullptr)
            NS::Scene::ApplyJsonFields(*target, *fieldsIt);
    }
}

void LoadPlayerTuning(NS::Scene::GameObject& player) noexcept
{
    const auto path = PlayerTuningPath();
    if (!::NS::Core::FileSystem::Exists(path))
        return; // 無ければコード既定の構成と値をそのまま使う

    const auto bytes = ::NS::Core::FileSystem::ReadAllBytes(path);
    if (!bytes.has_value())
        return;

    ApplyPlayerTuningText(player, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
}
