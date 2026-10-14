#pragma once

#include "NSlib/Object/Actor.h"

namespace GL::Level
{
    //! @brief 重なったプレイヤーへ即死を通知する範囲
    //! @details 上面に着地させないため、地形の当たりは持たない
    class DeathZone : public NS::Obj::Actor
    {
    public:
        NS_REFLECT_NONE(DeathZone, NS::Obj::Actor)

        void AttackSensor(NS::Obj::HitSensor& self, NS::Obj::HitSensor& other) override;

    protected:
        void OnInit() override;
    };
} // namespace GL::Level
