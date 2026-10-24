#pragma once

#include "Game/Level/GoalParams.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace GL
{
    namespace Level
    {
        //! @brief プレイヤーとの重なりで MsgGoal を送り、受け手にクリア処理を委ねるゴール
        class GoalActor : public NS::SceneActor
        {
        public:
            NS_REFLECT_BEGIN(GoalActor, NS::SceneActor)
            NS_CLASSOBJ(m_params, "Params")
            NS_REFLECT_END()

            void AttackSensor(NS::HitSensor* self, NS::HitSensor* other) override;
            [[nodiscard]] GoalParams& Params() noexcept { return *m_params; }
            [[nodiscard]] const GoalParams& Params() const noexcept { return *m_params; }

        protected:
            void OnInit() override;

        private:
            std::unique_ptr<GoalParams> m_params;
        };
    } // namespace Level
} // namespace GL
