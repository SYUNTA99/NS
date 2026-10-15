#pragma once

#include "NSlib/Object/SceneActor.h"

namespace GL::Level
{
    //! @brief 重なったプレイヤーへ即死を通知する範囲
    //! @details 上面に着地させないため、地形の当たりは持たない
    class DeathZoneActor : public NS::Obj::SceneActor
    {
    public:
        NS_REFLECT_NONE(DeathZoneActor, NS::Obj::SceneActor)

        void AttackSensor(NS::Obj::HitSensor& self, NS::Obj::HitSensor& other) override;

    protected:
        void OnInit() override;
    };
} // namespace GL::Level
