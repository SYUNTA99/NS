#include "Game/Level/SensorKinds.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/SubObjects/HitSensor.h"

namespace GL
{
    namespace Level
    {
        void SetSensorKind(NS::HitSensor& sensor, SensorKind kind) noexcept
        {
            sensor.SetKind(static_cast<NS::uint8>(kind));
        }

        bool IsSensorKind(const NS::HitSensor& sensor, SensorKind kind) noexcept
        {
            // 未設定はどの受け手にも応じられないので、未設定の相手を問われても真にしない
            if (kind == SensorKind::Unset)
            {
                return false;
            }
            return sensor.Kind() == static_cast<NS::uint8>(kind);
        }
    } // namespace Level
} // namespace GL
