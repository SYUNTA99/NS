#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Game::Entity
{
    class EntityComponent;
}

namespace NS::Game::Level
{
    //! @brief 持ち主の接地と速度を追従カメラへ渡す Component
    //! @details ThirdPersonFollowComponent は NS::Object にあり、NS::Game の型を名指しできない
    //! 値だけを運ぶことで include の向きを保つ
    //! 帯は LateUpdate + 40 で、やり直しの後・カメラの追従の前
    //! 依存: NS::Game::Entity::EntityComponent, NS::Object::ThirdPersonFollowComponent
    class FollowCameraFeedComponent : public NS::Object::Component
    {
    public:
        FollowCameraFeedComponent() noexcept;

        //! 同居する移動を控える。無ければ OnUpdate は何もしない
        void OnStart() override;
        //! 持ち主を追っている追従カメラすべてへ接地と速度を渡す
        void OnUpdate() override;

        // 保存する調整値は無い。リフレクションの鎖と型名だけ通す
        NS_REFLECT_NONE(FollowCameraFeedComponent, NS::Object::Component)

    private:
        NS::Game::Entity::EntityComponent* m_entity = nullptr; // 持ち主の移動 (非所有)
    };
} // namespace NS::Game::Level
