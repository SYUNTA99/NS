#pragma once

#include "Runtime/Object/Actor.h"

namespace NS::Game::Level
{
    //! @brief 追う相手の後ろから映す追従カメラ。ThirdPersonFollow と、相手の接地と速さを渡す FollowCameraFeed を持つ
    class FollowCamera : public NS::Obj::Actor
    {
    public:
        FollowCamera() noexcept;

        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        [[nodiscard]] const char* ClassName() const noexcept override { return "FollowCamera"; }
    };
} // namespace NS::Game::Level
