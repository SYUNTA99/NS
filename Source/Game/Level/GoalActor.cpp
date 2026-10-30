#include "Game/Level/GoalActor.h"

#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/HitSensorsObj.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"

namespace GL
{
    namespace Level
    {
        void GoalActor::OnInit()
        {
            // ゴールの球にプレイヤーの体が重なれば触れたとみなす。半径は .actor の hitSensors の値
            CreateClassObj(m_hitSensors);
            m_bodyHitSensor = m_hitSensors->AddShape(this, "Body", SensorKind::Area, NS::HitSensorShape::Sphere);
            CreateClassObj(m_params);
            CreateClassObj(m_model);
            NS::InitDraw(this, m_model.get(), &Root());
        }

        void GoalActor::OnSensorEnter(NS::HitSensor* self, NS::HitSensor* other)
        {
            NS_ASSERT(Game, self != nullptr && other != nullptr, "重なったセンサーが渡されていない");
            // 触れ始めた時に 1 回送る。クリアの流れはやり直しで自機を出現位置へ戻すので、触れたままにはならない
            if (IsSensorKind(other, SensorKind::PlayerBody))
            {
                (void)SendMsgGoal(other, self, m_params->FadeOutSeconds(), m_params->FadeInSeconds());
            }
        }

        NS_PLACEABLE(GoalActor, "ゴール")
    } // namespace Level
} // namespace GL
