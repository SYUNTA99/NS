#pragma once

#include "Game/Level/DeathZoneParams.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace GL
{
    namespace Level
    {
        //! 海や湖の水面。空の後に描くので、画面の深度と空のキューブマップを読める
        //! 上に立たせないため、地形の当たりは持たない。
        //! 自機の体の下の端が、うねりで上下する水面より下へ入ったフレームに、即死を知らせて、その水面の高さに
        //! 音とエフェクトを出す。水面の高さは、描くシェーダーと同じ式 (WaterSwell) で、その場所とその時の値を計算する。
        //! 体のセンサーは水面を上下に包む箱で、大きさは .actor の hitSensors の値。調べるのは箱に入っている間だけ
        class WaterActor : public NS::SceneActor
        {
        public:
            NS_REFLECT_BEGIN(WaterActor, NS::SceneActor)
            NS_CLASSOBJ(m_params, "Params")
            NS_REFLECT_END()

            void OnSensorEnter(NS::HitSensor* self, NS::HitSensor* other) override;
            void OnSensorStay(NS::HitSensor* self, NS::HitSensor* other) override;
            [[nodiscard]] DeathZoneParams& Params() noexcept { return *m_params; }
            [[nodiscard]] const DeathZoneParams& Params() const noexcept { return *m_params; }

        protected:
            void OnInit() override;

        private:
            //! other が自機の体で、その下の端が今の水面より下なら、即死を知らせて音とエフェクトを出す
            void SinkBodyBelowSurface(NS::HitSensor* self, NS::HitSensor* other);

            NS::HitSensor* m_bodyHitSensor = nullptr; // 体のセンサー (非所有)。持つのは HitSensorsObj

            std::unique_ptr<DeathZoneParams> m_params;
        };
    } // namespace Level
} // namespace GL
