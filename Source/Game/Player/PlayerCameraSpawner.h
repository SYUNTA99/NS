#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Game::Player
{
    //! @brief 自機を追うカメラがシーンに無ければ、Player Camera を 1 台足す Component
    //! @details Player Camera は ThirdPersonFollow と FollowCameraFeed を積んだ普通の配置物
    //! 足すのは世界が回っている間だけ。編集中に足すと保存と Undo に紛れ込む
    //! 依存: NS::Obj::ThirdPersonFollow, NS::Game::Level::FollowCameraFeed
    class PlayerCameraSpawner : public NS::Obj::Component
    {
    public:
        PlayerCameraSpawner() noexcept;

        //! 持ち主を追う追従カメラが 1 台も無ければ Player Camera を足す
        void OnUpdate() override;

        // 保存する調整値は無い。リフレクションの鎖と型名だけ通す
        NS_REFLECT_NONE(PlayerCameraSpawner, NS::Obj::Component)
    };
} // namespace NS::Game::Player
