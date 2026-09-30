#pragma once

#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Scene/SceneJson.h"

namespace NS::Game::Level
{
    //! @brief 触れたプレイヤーを即死させる体積。奈落の下へ大きく置き、落下死をレベルのデータとして表す
    //! @details 当たり箱はトリガーにする。固形だと落ちてきたプレイヤーが上面に着地してしまう
    class KillZone : public NS::Obj::Actor
    {
    public:
        KillZone() noexcept;

        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        [[nodiscard]] const char* ClassName() const noexcept override { return "KillZone"; }
    };

    //! 即死体積の配置物の JSON か
    [[nodiscard]] bool IsKillZoneObject(const nlohmann::json& object) noexcept;

    //! 奈落用の即死体積のひな形の JSON を作る
    [[nodiscard]] nlohmann::json MakeKillZoneObject();

    //! 即死体積が 1 つも無ければ既定の落下死体積を敷き、永続 id まで振る
    //! 無いレベルは奈落で死ねず落ち続けてしまうので、新しいレベルを作る時に通す
    [[nodiscard]] bool EnsureKillZoneObject(nlohmann::json& scene);
} // namespace NS::Game::Level
