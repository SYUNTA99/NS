#include "Game/Level/KillZone.h"

#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Level
{
    KillZone::KillZone() noexcept
    {
        // 厚み 10m と 2km 四方は、固定ステップの移動量では突き抜けられない
        NS::Obj::ShapeHitSensor* area = NS::Obj::ComponentCast<NS::Obj::ShapeHitSensor>(CreatePart("BodySensor"));
        SetSensorKind(*area, SensorKind::Area);
        area->SetBox(NS::Core::Vector3{1000.0f, 5.0f, 1000.0f});
    }

    void KillZone::AttackSensor(NS::Obj::HitSensor& self, NS::Obj::HitSensor& other)
    {
        if (IsSensorKind(other, SensorKind::PlayerBody))
        {
            (void)SendMsgKill(other, self);
        }
    }

    bool IsKillZoneObject(const nlohmann::json& object) noexcept
    {
        return NS::Obj::ObjectJsonClass(object) == "KillZone";
    }

    nlohmann::json MakeKillZoneObject()
    {
        nlohmann::json object = NS::Obj::MakePrototypeJson<KillZone>();
        // 上面 y=-50 は従来の落下死の高さ
        NS::Obj::SetObjectPosition(object, NS::Core::Vector3{0.0f, -55.0f, 0.0f});
        return object;
    }

    bool EnsureKillZoneObject(nlohmann::json& scene)
    {
        for (const nlohmann::json& object : NS::Obj::SceneJsonObjects(scene))
        {
            if (IsKillZoneObject(object))
            {
                return false;
            }
        }
        NS::Obj::SceneJsonObjects(scene).push_back(MakeKillZoneObject());
        NS::Obj::EnsureUniqueObjectIds(scene);
        return true;
    }

    NS_PLACEABLE(KillZone, "落下死の範囲")
} // namespace NS::Game::Level
