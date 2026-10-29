#pragma once

#include "Game/Level/SensorKinds.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace GL
{
    namespace Level
    {
        //! 重なったプレイヤーへ即死を通知する範囲
        //! 上面に着地させないため、地形の当たりは持たない
        class DeathZoneActor : public NS::SceneActor
        {
        public:
            NS_REFLECT_NONE(DeathZoneActor, NS::SceneActor)

            void OnSensorEnter(NS::HitSensor* self, NS::HitSensor* other) override;

        protected:
            void OnInit() override;

        private:
            NS::HitSensor* m_bodyHitSensor = nullptr; // 体のセンサー (非所有)。持つのは HitSensorsObj
        };
    } // namespace Level
} // namespace GL
