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
            // ゴールの球にプレイヤーの体が重なれば触れたとみなす。半径は .actor の欄
            NS::ShapeHitSensor* area = CreateClassObj<NS::HitSensor, NS::ShapeHitSensor>(m_bodySensor, this);
            NS::InitHitSensor(this, area);
            SetSensorKind(area, SensorKind::Area);
            area->SetShape(NS::HitSensorShape::Sphere);
        }

        void GoalActor::AttackSensor(NS::HitSensor* self, NS::HitSensor* other)
        {
            NS_ASSERT(Game, self != nullptr && other != nullptr, "重なったセンサーが渡されていない");
            // 触れている間は毎フレーム送る。流れの最中の知らせは進行役が捨てる
            if (IsSensorKind(other, SensorKind::PlayerBody))
            {
                (void)SendMsgGoal(other, self, m_params->FadeOutSeconds(), m_params->FadeInSeconds());
            }
        }

        NS_PLACEABLE(GoalActor, "ゴール")
    } // namespace Level
} // namespace GL
