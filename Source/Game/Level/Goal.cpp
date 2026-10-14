#include "Game/Level/Goal.h"

#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

namespace GL::Level
{
    void Goal::OnInit()
    {
        m_params = CreateSubObj<GoalParams>("Params");
        NS::Obj::Model* mesh = CreateSubObj<NS::Obj::Model>(ModelSlot());
        mesh->SetMeshRef("cube");
        mesh->SetBaseColor(NS::Vector3{1.0f, 0.84f, 0.0f});
        // ゴールの半径 0.9 m にプレイヤーの体が重なれば触れたとみなす
        NS::Obj::ShapeHitSensor* area = CreateSubObj<NS::Obj::ShapeHitSensor>(BodySensorSlot());
        SetSensorKind(*area, SensorKind::Area);
        area->SetSphere(0.9f);
    }

    void Goal::AttackSensor(NS::Obj::HitSensor& self, NS::Obj::HitSensor& other)
    {
        // 触れている間は毎フレーム送る。流れの最中の知らせは進行役が捨てる
        if (IsSensorKind(other, SensorKind::PlayerBody))
        {
            (void)SendMsgGoal(other, self, m_params->FadeOutSeconds(), m_params->FadeInSeconds());
        }
    }

    NS_PLACEABLE(Goal, "ゴール")
} // namespace GL::Level
