#pragma once

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

namespace NS
{
    class AssetManager;
    class Scene;

    //! @brief SceneActor::Init が受ける、アクターを作る時の情報
    //! @details 作る道 (シーンの組み直し・JSON から湧かす・作り直す・型から湧かす) で同じ形を渡す
    struct ActorInitInfo
    {
        //! 型から湧かすアクターは配置の JSON が無いので nullptr。そのアクターには種類の既定だけが書かれる
        const nlohmann::json* placement = nullptr;
        Scene& scene;
        AssetManager& assets;
    };

} // namespace NS
