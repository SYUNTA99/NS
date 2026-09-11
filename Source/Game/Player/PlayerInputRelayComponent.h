#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Object
{
    class PlayerInputComponent;
}

namespace NS::Game::Player
{
    class PlayerComponent;

    //! @brief 入力の読み取り値を自機へ渡す Component
    //! @details PlayerInputComponent は NS::Object にあり、NS::Game::Player の型を名指しできない
    //! 値だけを運ぶことで include の向きを保つ
    //! 帯は EarlyUpdate + 10 で、入力を読む PlayerInputComponent の直後
    //! 依存: NS::Object::PlayerInputComponent, PlayerComponent
    class PlayerInputRelayComponent : public NS::Object::Component
    {
    public:
        PlayerInputRelayComponent() noexcept;

        //! 同居する入力と自機を控える。どちらか欠けたら OnUpdate は何もしない
        void OnStart() override;
        //! 直近の歩の入力の値を自機へ渡す
        void OnUpdate() override;

        // 保存する調整値は無い。リフレクションの鎖と型名だけ通す
        NS_REFLECT_NONE(PlayerInputRelayComponent, NS::Object::Component)

    private:
        NS::Object::PlayerInputComponent* m_input = nullptr; // 読み取り元の入力 (非所有)
        PlayerComponent* m_player = nullptr;                 // 渡し先の自機 (非所有)
    };
} // namespace NS::Game::Player
