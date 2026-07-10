#include "Editor/PlayerTuningIO.h"

#include "Editor/PlayerTuning.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

namespace NS::Editor
{
    bool SavePlayerTuning(const NS::Scene::GameObject& player) noexcept
    {
        nlohmann::json components = nlohmann::json::array();
        for (const NS::Scene::Component* comp : player.Components())
        {
            // 反射を持たないコンポーネントは保存できる値が無いので飛ばす
            if (comp == nullptr || comp->GetReflection() == nullptr)
                continue;
            components.push_back(NS::Scene::SerializeComponent(*comp));
        }

        nlohmann::json json;
        json["components"] = std::move(components);
        const std::string text = json.dump(2);
        const auto* data = reinterpret_cast<const std::byte*>(text.data());
        const auto path = ::PlayerTuningPath();
        const bool ok = ::NS::Core::FileSystem::WriteAllBytes(path, std::span<const std::byte>(data, text.size()));
        if (!ok)
            NS_LOG_ERROR(::NS::Core::LogCat::Game, "PlayerTuning.json の書き込みに失敗: {}", path.string());
        return ok;
    }
} // namespace NS::Editor
