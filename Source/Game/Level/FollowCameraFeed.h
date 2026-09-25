#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Obj
{
    class ThirdPersonFollow;
}

namespace NS::Game::Level
{
    //! @brief 同じ配置物の追従カメラへ、追う相手の接地と速度と、根から立ち姿の中心までの高さを渡す Component
    //! @details ThirdPersonFollow は NS::Obj にあり、NS::Game の型を名指しできない
    //! 追う相手は ThirdPersonFollow の追従対象から引き、値だけを運ぶことで include の向きを保つ
    //! 帯は LateUpdate + 40 で、やり直しの後・カメラの追従の前
    //! 依存: NS::Game::Entity::EntityComponent, NS::Obj::ThirdPersonFollow
    class FollowCameraFeed : public NS::Obj::Component
    {
    public:
        FollowCameraFeed() noexcept;

        //! 同じ配置物の追従カメラを控える。無ければ OnUpdate は何もしない
        void OnStart() override;
        //! 追う相手の接地と速度と、根から立ち姿の中心までの高さを追従カメラへ渡す。追う相手が移動を持たなければ何もしない
        void OnUpdate() override;
        //! 追従カメラへ渡した高さを 0 に戻す。プレイを終えると OnUpdate が走らず、編集中の視点が高さを残したままになる
        void OnEndPlay() override;

        // 保存する調整値は無い。データから普通の配置物へ載せられるよう型名だけ登録する
        NS_REFLECT_NONE(FollowCameraFeed, NS::Obj::Component)

    private:
        NS::Obj::ThirdPersonFollow* m_follow = nullptr; // 同じ配置物の追従カメラ (非所有)
    };
} // namespace NS::Game::Level
