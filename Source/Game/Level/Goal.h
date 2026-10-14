#pragma once

#include "Game/Level/GoalParams.h"
#include "NSlib/Object/Actor.h"

namespace GL::Level
{
    //! @brief プレイヤーとの重なりで MsgGoal を送り、受け手にクリア処理を委ねるゴール
    class Goal : public NS::Obj::Actor
    {
    public:
        NS_REFLECT_BEGIN(Goal, NS::Obj::Actor)
        NS_REFLECT_SUBOBJ(m_params, "Params")
        NS_REFLECT_END()

        void AttackSensor(NS::Obj::HitSensor& self, NS::Obj::HitSensor& other) override;

    protected:
        void OnInit() override;

    private:
        std::unique_ptr<GoalParams> m_params;
    };
} // namespace GL::Level
