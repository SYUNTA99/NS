#pragma once

#include "Game/Entity/EntityStateManagerComponent.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/StateMachine.h"

#include <string>
#include <string_view>

namespace NS::Game::Player
{
    class PlayerComponent;

    //! @brief 自機の状態機械を持つ Component
    //! @details 状態の一覧はセミコロン区切りのデータから組み、先頭が初期状態になる
    //! 未登録名は飛ばし、1 つも組めなければ既定の並びへ退避する
    //! 依存: NS::Game::Entity::EntityStateManagerComponent, NS::Object::StateMachine, PlayerComponent
    class PlayerStateManagerComponent : public NS::Game::Entity::EntityStateManagerComponent
    {
    public:
        [[nodiscard]] const char* CurrentName() const noexcept override;
        [[nodiscard]] bool IsBuilt() const noexcept override;
        bool ChangeByName(std::string_view name) override;
        void ResetToFirst() noexcept override;

        //! まだ組んでいなければ一覧から組む。遷移に渡す所有者もここで控える
        //! @details 1 歩の頭で組む必要があるのは、現在状態を見て決める判断が Step より前にあるため
        //! @param[in] player 状態へ渡す所有者
        void EnsureBuilt(PlayerComponent& player);

        //! 現在状態で 1 歩進める。初回はここで組む
        //! @param[in] player 状態へ渡す所有者
        //! @param[in] dt 固定ステップの秒数
        void Step(PlayerComponent& player, float dt);

        //! 同居する PlayerComponent を控える。ChangeByName が遷移に所有者を要るため
        void OnStart() override;

        // 欄は登録される具象型に置く。リフレクションの直列化は自分の型の欄だけを回り、基底の鎖はたどらない
        NS_REFLECT_BEGIN(PlayerStateManagerComponent, NS::Game::Entity::EntityStateManagerComponent)
        NS_REFLECT_FIELD(m_stateNames, "状態一覧")
        NS_REFLECT_END()

    private:
        //! m_stateNames のセミコロン区切りから状態機械を組む。全滅時は既定の並びへ退避する
        void BuildStates(PlayerComponent& player);

        std::string m_stateNames = "Idle;Walk;Fall;LedgeHanging;LedgeClimbing;BodySlam"; // 先頭が初期状態
        NS::Object::StateMachine<PlayerComponent> m_machine;
        PlayerComponent* m_player = nullptr; // 遷移に渡す所有者 (非所有)
    };
} // namespace NS::Game::Player
