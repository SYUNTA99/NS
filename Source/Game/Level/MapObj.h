#pragma once

#include "Runtime/Object/Actor.h"

namespace NS::Game::Level
{
    //! @brief 動く・反応する置物。岩・箱・樽
    //! @details 見た目・球の当たり・キネマティックの RigidBody・耐久・押し飛ばされた後の動き・影を持つ
    //! 体当たりを受けると飛ぶ。種類ごとの見た目と重さはデータで変える
    class MapObj : public NS::Obj::Actor
    {
    public:
        MapObj() noexcept;

        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        [[nodiscard]] const char* ClassName() const noexcept override { return "MapObj"; }
    };
} // namespace NS::Game::Level
