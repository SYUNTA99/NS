#include "NSlib/CoreObject/IUse/IUseHitSensor.h"
#include "NSlib/Core/Assert.h"

#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/System/HitSensorDirector.h"

namespace NS
{
    void InitHitSensor(const IUseHitSensor* user, HitSensor* sensor)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, sensor != nullptr, "InitHitSensor: センサーが null");
        if (HitSensorDirector* director = user->GetHitSensorDirector())
        {
            director->Register(sensor);
        }
    }

    void EndHitSensor(const IUseHitSensor* user, HitSensor* sensor)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, sensor != nullptr, "EndHitSensor: センサーが null");
        if (HitSensorDirector* director = user->GetHitSensorDirector())
        {
            director->Unregister(sensor);
        }
    }

    void EnableHitSensor(HitSensor* sensor) noexcept
    {
        NS_ASSERT(Scene, sensor != nullptr, "EnableHitSensor: センサーが null");
        sensor->ValidateBySystem();
    }

    void DisableHitSensor(HitSensor* sensor) noexcept
    {
        NS_ASSERT(Scene, sensor != nullptr, "DisableHitSensor: センサーが null");
        sensor->InvalidateBySystem();
    }
} // namespace NS
