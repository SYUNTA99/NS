#pragma once

#include "Game/Level/SensorKinds.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace GL
{
    namespace Level
    {
        /// 重なったプレイヤーへ即死を通知する範囲
        /// 上面に着地させないため、地形の当たりは持たない
        class DeathZoneActor : public NS::SceneActor
        {
        public:
            NS_REFLECT_NONE(DeathZoneActor, NS::SceneActor)

            void OnSensorEnter(NS::HitSensor* self, NS::HitSensor* other) override;

        private:
            // 大きさは .actor の欄
            NS_SENSORS_BEGIN()
            NS_SENSOR(m_bodySensor, SensorKind::Area, NS::HitSensorShape::Box)
            NS_SENSORS_END()
        };
    } // namespace Level
} // namespace GL
