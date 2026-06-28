#include "Game/PlayerTuning.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Scene/Component.h"
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

void LoadPlayerTuning(const NS::Scene::GameObject& player) noexcept
{
    const auto path = PlayerTuningPath();
    if (!::NS::Core::FileSystem::Exists(path))
        return; // 無ければコード既定値をそのまま使う

    const auto bytes = ::NS::Core::FileSystem::ReadAllBytes(path);
    if (!bytes.has_value())
        return;

    const std::string text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded())
    {
        NS_LOG_WARN(::NS::Core::LogCat::Game, "PlayerTuning.json の解析に失敗、 既定値で続行: {}", path.string());
        return;
    }

    const auto components = json.find("components");
    if (components == json.end() || !components->is_array())
        return;

    // 型名が一致する player のコンポーネントへ反射でフィールドを適用する
    for (const nlohmann::json& entry : *components)
    {
        const auto typeIt = entry.find("type");
        const auto fieldsIt = entry.find("fields");
        if (typeIt == entry.end() || !typeIt->is_string() || fieldsIt == entry.end())
            continue;
        const std::string typeName = typeIt->get<std::string>();

        for (NS::Scene::Component* comp : player.Components())
        {
            if (comp == nullptr)
                continue;
            const NS::Scene::ReflectionInfo* info = comp->GetReflection();
            if (info == nullptr || typeName != info->typeName)
                continue;
            NS::Scene::ApplyJsonFields(*comp, *fieldsIt);
            break; // 同型は最初の 1 件へ適用する
        }
    }
}
