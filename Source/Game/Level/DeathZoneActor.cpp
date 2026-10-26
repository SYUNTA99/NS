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
        void DeathZoneActor::OnSensorEnter(NS::HitSensor* self, NS::HitSensor* other)
        {
            OnSensorStay(self, other);
        }

        // 入っている間は毎フレーム送る。受け手が捨てた時も次のフレームに届き直す
        void DeathZoneActor::OnSensorStay(NS::HitSensor* self, NS::HitSensor* other)
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
