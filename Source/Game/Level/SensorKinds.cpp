#include "Game/Level/SensorKinds.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"

namespace GL::Level
{
        bool IsSensorKind(const NS::HitSensor* sensor, SensorKind kind) noexcept
        {
            NS_ASSERT(Game, sensor != nullptr, "種類を問うセンサーが無い");
            // 未設定はどの受け手にも応じられないので、未設定の相手を問われても真にしない
            if (kind == SensorKind::Unset)
            {
                return false;
            }
            return sensor->Kind() == static_cast<NS::uint8>(kind);
        }
} // namespace GL::Level
