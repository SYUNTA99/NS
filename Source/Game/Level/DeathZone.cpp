#include "Game/Level/DeathZone.h"

#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Level
{
    DeathZone::DeathZone() noexcept
    {
        // 厚み 10m と 2km 四方は、固定ステップの移動量では突き抜けられない
        NS::Obj::ShapeHitSensor* area = NS::Obj::ComponentCast<NS::Obj::ShapeHitSensor>(CreatePart("BodySensor"));
        SetSensorKind(*area, SensorKind::Area);
        area->SetBox(NS::Core::Vector3{1000.0f, 5.0f, 1000.0f});
    }

    void DeathZone::AttackSensor(NS::Obj::HitSensor& self, NS::Obj::HitSensor& other)
    {
        if (IsSensorKind(other, SensorKind::PlayerBody))
        {
            (void)SendMsgInstantDeath(other, self);
        }
    }

    bool IsDeathZoneObject(const nlohmann::json& object) noexcept
    {
        return NS::Obj::ObjectJsonClass(object) == "DeathZone";
    }

    nlohmann::json MakeDeathZoneObject()
    {
        nlohmann::json object = NS::Obj::MakePrototypeJson<DeathZone>();
        // 上面 y=-50 は従来の落下死の高さ
        NS::Obj::SetObjectPosition(object, NS::Core::Vector3{0.0f, -55.0f, 0.0f});
        return object;
    }

    bool EnsureDeathZoneObject(nlohmann::json& scene)
    {
        for (const nlohmann::json& object : NS::Obj::SceneJsonObjects(scene))
        {
            if (IsDeathZoneObject(object))
            {
                return false;
            }
        }
        NS::Obj::SceneJsonObjects(scene).push_back(MakeDeathZoneObject());
        NS::Obj::EnsureUniqueObjectIds(scene);
        return true;
    }

    NS_PLACEABLE(DeathZone, "落下死の範囲")
} // namespace NS::Game::Level
