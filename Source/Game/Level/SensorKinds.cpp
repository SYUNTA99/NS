#include "Game/Level/SensorKinds.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Object/SubObjects/HitSensor.h"

namespace GL::Level
{
    void SetSensorKind(NS::Obj::HitSensor& sensor, SensorKind kind) noexcept
    {
        sensor.SetKind(static_cast<NS::uint8>(kind));
    }

    bool IsSensorKind(const NS::Obj::HitSensor& sensor, SensorKind kind) noexcept
    {
        // 未設定はどの受け手にも応じられないので、未設定の相手を問われても真にしない
        if (kind == SensorKind::Unset)
        {
            return false;
        }
        return sensor.Kind() == static_cast<NS::uint8>(kind);
    }
} // namespace GL::Level
