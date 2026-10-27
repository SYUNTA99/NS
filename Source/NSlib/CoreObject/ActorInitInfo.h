#pragma once

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <string_view>

namespace NS
{
    class Scene;

    //! SceneActor の初期化情報
    struct ActorInitInfo
    {
        const nlohmann::json* placement = nullptr; // 配置 JSON が無い場合は nullptr (.actor の値のみ適用)
        Scene* scene = nullptr;                    // シーンに所属しない場合は nullptr
        std::string_view actorFile;                // 空なら生成クラスと同名の .actor を適用
    };

} // namespace NS
