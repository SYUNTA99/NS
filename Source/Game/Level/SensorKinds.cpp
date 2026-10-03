#include "Game/Level/SensorKinds.h"

#include "Runtime/Object/Components/HitSensor.h"

namespace NS::Game::Level
{
    void SetSensorKind(NS::Obj::HitSensor& sensor, SensorKind kind) noexcept
    {
        sensor.SetKind(static_cast<std::uint8_t>(kind));
    }

    bool IsSensorKind(const NS::Obj::HitSensor& sensor, SensorKind kind) noexcept
    {
        // 未設定はどの受け手にも応じられないので、未設定の相手を問われても真にしない
        if (kind == SensorKind::Unset)
        {
            return false;
        }
        return sensor.Kind() == static_cast<std::uint8_t>(kind);
    }
} // namespace NS::Game::Level
