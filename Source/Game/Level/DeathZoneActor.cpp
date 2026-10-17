#include "Game/Level/DeathZoneActor.h"

#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"

namespace GL
{
    namespace Level
    {
        void DeathZoneActor::OnInit()
        {
            NS::ShapeHitSensor* area = CreateClassObj<NS::HitSensor, NS::ShapeHitSensor>(m_bodySensor, this);
            SetSensorKind(*area, SensorKind::Area);
            area->SetBox(NS::Vector3{1000.0f, 5.0f, 1000.0f});
        }

        void DeathZoneActor::AttackSensor(NS::HitSensor& self, NS::HitSensor& other)
        {
            if (IsSensorKind(other, SensorKind::PlayerBody))
            {
                (void)SendMsgInstantDeath(other, self);
            }
        }

        NS_PLACEABLE(DeathZoneActor, "落下死の範囲")
    } // namespace Level
} // namespace GL
