#include "Game/Level/WaterActor.h"

#include "Game/Level/SensorKinds.h"
#include "Game/Level/WaterSwell.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/HitSensorsObj.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/SoundObj.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/System/HitSensorDirector.h"
#include "NSlib/CoreObject/System/SceneRenderer.h"

namespace GL
{
    namespace Level
    {
        void WaterActor::OnInit()
        {
            CreateClassObj(m_model);
            m_model->SetDrawsAfterSky();
            m_model->SetNeverCulled();
            // 水面を上下に包む箱。大きさは .actor の hitSensors の値
            CreateClassObj(m_hitSensors);
            m_bodyHitSensor = m_hitSensors->AddShape(this, "Body", SensorKind::Area, NS::HitSensorShape::Box);
            CreateClassObj(m_params);
            // エフェクトと音の部品は、置き場を問う先 (自分の IGet〜) を作る時に受ける
            CreateClassObj(m_effect, this, &TransformObj());
            CreateClassObj(m_sound, this);
        }

        void WaterActor::OnSensorEnter(NS::HitSensor* self, NS::HitSensor* other)
        {
            SinkBodyBelowSurface(self, other);
        }

        void WaterActor::OnSensorStay(NS::HitSensor* self, NS::HitSensor* other)
        {
            SinkBodyBelowSurface(self, other);
        }

        void WaterActor::SinkBodyBelowSurface(NS::HitSensor* self, NS::HitSensor* other)
        {
            NS_ASSERT(Game, self != nullptr && other != nullptr, "重なったセンサーが渡されていない");
            if (!IsSensorKind(other, SensorKind::PlayerBody))
            {
                return;
            }
            const NS::SensorVolume body = NS::HitSensorDirector::VolumeOf(other);
            const NS::AABB bounds = body.Bounds();
            const NS::Vector3 center = body.Center();
            // 描画の無い世界では時間を 0 として読む
            float timeSeconds = 0.0f;
            if (const NS::SceneRenderer* renderer = GetSceneRenderer())
            {
                timeSeconds = renderer->SceneTimeSeconds();
            }
            const WaterSwell swell = ReadWaterSwell(m_model->MaterialField());
            const float surface = TransformObj().Position().y + WaterSwellHeight(swell, center.x, center.z, timeSeconds);
            if (bounds.Center.y - bounds.Extents.y > surface)
            {
                return;
            }
            (void)m_params->KillAndPlayEntry(this, self, other, NS::Vector3{center.x, surface, center.z});
        }

        NS_PLACEABLE(WaterActor, "水面")
    } // namespace Level
} // namespace GL
