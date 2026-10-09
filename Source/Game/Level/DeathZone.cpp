#include "Game/Level/DeathZone.h"

#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Level
{
    DeathZone::DeathZone() noexcept
    {
        // 厚み 10m と 2km 四方は、固定ステップの移動量では突き抜けられない
        NS::Obj::ShapeHitSensor* area = NS::Obj::Cast<NS::Obj::ShapeHitSensor>(CreatePart("BodySensor"));
        SetSensorKind(*area, SensorKind::Area);
        area->SetBox(NS::Vector3{1000.0f, 5.0f, 1000.0f});
    }

    void DeathZone::AttackSensor(NS::Obj::HitSensor& self, NS::Obj::HitSensor& other)
    {
        if (IsSensorKind(other, SensorKind::PlayerBody))
        {
            (void)SendMsgInstantDeath(other, self);
        }
    }

    NS_PLACEABLE(DeathZone, "落下死の範囲")
} // namespace NS::Game::Level
