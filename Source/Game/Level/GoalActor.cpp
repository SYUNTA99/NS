#include "Game/Level/GoalActor.h"

#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"

namespace GL
{
    namespace Level
    {
        void GoalActor::OnInit()
        {
            CreateClassObj(m_params);
            CreateClassObj(m_model);
            NS::InitDraw(this, m_model.get(), &Root());
        }

        void GoalActor::OnSensorEnter(NS::HitSensor* self, NS::HitSensor* other)
        {
            OnSensorStay(self, other);
        }

        void GoalActor::OnSensorStay(NS::HitSensor* self, NS::HitSensor* other)
        {
            NS_ASSERT(Game, self != nullptr && other != nullptr, "重なったセンサーが渡されていない");
            // 触れている間は毎フレーム送る。CourseDirector が流れの最中の物を捨てても、流れの後に届き直す
            if (IsSensorKind(other, SensorKind::PlayerBody))
            {
                (void)SendMsgGoal(other, self, m_params->FadeOutSeconds(), m_params->FadeInSeconds());
            }
        }

        NS_PLACEABLE(GoalActor, "ゴール")
    } // namespace Level
} // namespace GL
