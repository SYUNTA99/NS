#include "Game/Level/DeathZoneActor.h"

#include "Game/Level/SensorKinds.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/HitSensorsObj.h"
#include "NSlib/CoreObject/ClassObjs/SoundObj.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/System/HitSensorDirector.h"

namespace GL
{
    namespace Level
    {
        void DeathZoneActor::OnInit()
        {
            // 大きさは .actor の hitSensors の値
            CreateClassObj(m_hitSensors);
            m_bodyHitSensor = m_hitSensors->AddShape(this, "Body", SensorKind::Area, NS::HitSensorShape::Box);
            CreateClassObj(m_params);
            // エフェクトと音の部品は、置き場を問う先 (自分の IGet〜) を作る時に受ける
            CreateClassObj(m_effect, this, &TransformObj());
            CreateClassObj(m_sound, this);
        }

        void DeathZoneActor::OnSensorEnter(NS::HitSensor* self, NS::HitSensor* other)
        {
            NS_ASSERT(Game, self != nullptr && other != nullptr, "重なったセンサーが渡されていない");
            if (!IsSensorKind(other, SensorKind::PlayerBody))
            {
                return;
            }
            // エフェクトは、入った体の真下 (真上) の、範囲の上面に出す。
            // 形は消える前に読む。知らせを受けた自機は、その場で消えてセンサーが無効になる
            const NS::AABB bounds = NS::HitSensorDirector::VolumeOf(self).Bounds();
            const NS::Vector3 body = NS::HitSensorDirector::VolumeOf(other).Center();
            const NS::Vector3 surface{body.x, bounds.Center.y + bounds.Extents.y, body.z};
            (void)m_params->KillAndPlayEntry(this, self, other, surface);
        }

        NS_PLACEABLE(DeathZoneActor, "落下死の範囲")
    } // namespace Level
} // namespace GL
