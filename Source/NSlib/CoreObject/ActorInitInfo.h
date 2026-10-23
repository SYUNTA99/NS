#pragma once

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <string_view>

namespace NS
{
    class Scene;

    //! @brief SceneActor::Init が受ける、アクターを作る時の情報
    //! @details 作る道 (シーンの組み直し・JSON から湧かす・作り直す・型から湧かす) で同じ形を渡す
    struct ActorInitInfo
    {
        //! 型から湧かすアクターは配置の JSON が無いので nullptr。.actor の値だけが書かれる
        const nlohmann::json* placement = nullptr;
        //! 入るシーン。シーンへ入らない物は nullptr
        Scene* scene = nullptr;
        //! 敷く .actor の名前。空なら作るクラスと同じ名前の .actor を敷く
        std::string_view actorFile;
    };

} // namespace NS
