#include "Game/Level/Goal.h"

#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Level
{
    Goal::Goal() noexcept
    {
        AttachFixedSubObject(m_params);
        // 目印の金色の立方体
        (void)CreatePart("Model");
        NS::Obj::Model* mesh = ModelSubObj();
        mesh->SetMeshRef("cube");
        mesh->SetBaseColor(NS::Vector3{1.0f, 0.84f, 0.0f});
        // 「触れた」とみなすプレイヤー中心からの距離 0.9m の球。プレイヤーの体の寸法ぶん手前で触れる
        NS::Obj::ShapeHitSensor* area = NS::Obj::Cast<NS::Obj::ShapeHitSensor>(CreatePart("BodySensor"));
        SetSensorKind(*area, SensorKind::Area);
        area->SetSphere(0.9f);
    }

    void Goal::ForEachSubObj(const SubObjVisitor& visitor) const
    {
        NS::Obj::Actor::ForEachSubObj(visitor);
        visitor("Params", const_cast<GoalParams&>(m_params));
    }

    void Goal::AttackSensor(NS::Obj::HitSensor& self, NS::Obj::HitSensor& other)
    {
        // 触れている間は毎フレーム送る。流れの最中の知らせは進行役が捨てる
        if (IsSensorKind(other, SensorKind::PlayerBody))
        {
            (void)SendMsgGoal(other, self, m_params.FadeOutSeconds(), m_params.FadeInSeconds());
        }
    }

    NS_PLACEABLE(Goal, "ゴール")
} // namespace NS::Game::Level
