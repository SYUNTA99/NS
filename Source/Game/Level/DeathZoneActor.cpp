#include "Game/Level/DeathZoneActor.h"

#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/SubObjects/HitSensor.h"

namespace GL::Level
{
    void DeathZoneActor::OnInit()
    {
        NS::Obj::ShapeHitSensor* area = CreateSubObj<NS::Obj::HitSensor, NS::Obj::ShapeHitSensor>(m_bodySensor);
        SetSensorKind(*area, SensorKind::Area);
        area->SetBox(NS::Vector3{1000.0f, 5.0f, 1000.0f});
    }

    void DeathZoneActor::AttackSensor(NS::Obj::HitSensor& self, NS::Obj::HitSensor& other)
    {
        if (IsSensorKind(other, SensorKind::PlayerBody))
        {
            (void)SendMsgInstantDeath(other, self);
        }
    }

    NS_PLACEABLE(DeathZoneActor, "落下死の範囲")
} // namespace GL::Level
