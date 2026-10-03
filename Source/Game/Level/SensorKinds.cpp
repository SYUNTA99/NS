#include "Game/Level/SensorKinds.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/HitSensor.h"

namespace NS::Game::Level
{
    namespace
    {
        // 名前を今の HitSensorType へ写す。Runtime が種類を不透明な値にする区切りでこの写しごと消える
        // 写せない種類は Unset で、false を返す
        bool ToSensorType(SensorKind kind, NS::Obj::HitSensorType& out) noexcept
        {
            switch (kind)
            {
            case SensorKind::PlayerBody:
                out = NS::Obj::HitSensorType::PlayerBody;
                return true;
            case SensorKind::MapObjBody:
                out = NS::Obj::HitSensorType::MapObjBody;
                return true;
            case SensorKind::Area:
                out = NS::Obj::HitSensorType::Area;
                return true;
            case SensorKind::Unset:
                break;
            }
            return false;
        }
    } // namespace

    void SetSensorKind(NS::Obj::HitSensor& sensor, SensorKind kind) noexcept
    {
        NS::Obj::HitSensorType type = NS::Obj::HitSensorType::Count;
        if (!ToSensorType(kind, type))
        {
            NS_LOG_ERROR(Game, "SetSensorKind: 未設定は書けない。種類は変えない");
            return;
        }
        sensor.SetType(type);
    }

    bool IsSensorKind(const NS::Obj::HitSensor& sensor, SensorKind kind) noexcept
    {
        NS::Obj::HitSensorType type = NS::Obj::HitSensorType::Count;
        return ToSensorType(kind, type) && sensor.Type() == type;
    }
} // namespace NS::Game::Level
