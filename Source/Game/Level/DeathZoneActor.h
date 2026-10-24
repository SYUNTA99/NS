#pragma once

#include "NSlib/CoreObject/SceneActor.h"

namespace GL
{
    namespace Level
    {
        //! @brief 重なったプレイヤーへ即死を通知する範囲
        //! @details 上面に着地させないため、地形の当たりは持たない
        class DeathZoneActor : public NS::SceneActor
        {
        public:
            NS_REFLECT_NONE(DeathZoneActor, NS::SceneActor)

            void AttackSensor(NS::HitSensor* self, NS::HitSensor* other) override;

        protected:
            void OnInit() override;
        };
    } // namespace Level
} // namespace GL
