#include "Game/Level/GoalActor.h"

#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SubObjects/HitSensor.h"
#include "NSlib/CoreObject/SubObjects/Model.h"

namespace GL
{
    namespace Level
    {
        void GoalActor::OnInit()
        {
            CreateSubObj(m_params);
            NS::Model* mesh = CreateSubObj(m_model, this);
            mesh->SetMeshRef("cube");
            mesh->SetBaseColor(NS::Color{1.0f, 0.84f, 0.0f});
            // ゴールの半径 0.9 m にプレイヤーの体が重なれば触れたとみなす
            NS::ShapeHitSensor* area = CreateSubObj<NS::HitSensor, NS::ShapeHitSensor>(m_bodySensor, this);
            SetSensorKind(*area, SensorKind::Area);
            area->SetSphere(0.9f);
        }

        void GoalActor::AttackSensor(NS::HitSensor& self, NS::HitSensor& other)
        {
            // 触れている間は毎フレーム送る。流れの最中の知らせは進行役が捨てる
            if (IsSensorKind(other, SensorKind::PlayerBody))
            {
                (void)SendMsgGoal(other, self, m_params->FadeOutSeconds(), m_params->FadeInSeconds());
            }
        }

        NS_PLACEABLE(GoalActor, "ゴール")
    } // namespace Level
} // namespace GL
