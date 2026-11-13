#pragma once

#include "Game/Level/DeathZoneParams.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace GL
{
    namespace Level
    {
        //! 重なったプレイヤーへ即死を通知する範囲
        //! 上面に着地させないため、地形の当たりは持たない。
        //! 死ぬ時の音とエフェクトは、消える自機でなくこの範囲が出す。消えるアクターが出したエフェクトは、粒が出る前に親が止まる
        class DeathZoneActor : public NS::SceneActor
        {
        public:
            NS_REFLECT_BEGIN(DeathZoneActor, NS::SceneActor)
            NS_CLASSOBJ(m_params, "Params")
            NS_REFLECT_END()

            void OnSensorEnter(NS::HitSensor* self, NS::HitSensor* other) override;
            [[nodiscard]] DeathZoneParams& Params() noexcept { return *m_params; }
            [[nodiscard]] const DeathZoneParams& Params() const noexcept { return *m_params; }

        protected:
            void OnInit() override;

        private:
            NS::HitSensor* m_bodyHitSensor = nullptr; // 体のセンサー (非所有)。持つのは HitSensorsObj

            std::unique_ptr<DeathZoneParams> m_params;
        };
    } // namespace Level
} // namespace GL
