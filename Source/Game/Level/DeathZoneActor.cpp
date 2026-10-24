#include "Game/Level/DeathZoneActor.h"

#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"

namespace GL
{
    namespace Level
    {
        void DeathZoneActor::OnInit()
        {
            NS::ShapeHitSensor* area = CreateClassObj<NS::HitSensor, NS::ShapeHitSensor>(m_bodySensor, this);
            NS::InitHitSensor(this, area);
            SetSensorKind(area, SensorKind::Area);
            area->SetShape(NS::HitSensorShape::Box);
        }

        void DeathZoneActor::AttackSensor(NS::HitSensor* self, NS::HitSensor* other)
        {
            NS_ASSERT(Game, self != nullptr && other != nullptr, "重なったセンサーが渡されていない");
            if (IsSensorKind(other, SensorKind::PlayerBody))
            {
                (void)SendMsgInstantDeath(other, self);
            }
        }

        NS_PLACEABLE(DeathZoneActor, "落下死の範囲")
    } // namespace Level
} // namespace GL
