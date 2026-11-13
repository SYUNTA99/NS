#include "Game/Level/DeathZoneParams.h"

#include "Game/Level/LevelMessages.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/SoundObj.h"
#include "NSlib/CoreObject/IGet/IGetAudio.h"
#include "NSlib/CoreObject/IGet/IGetEffect.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace GL
{
    namespace Level
    {
        bool DeathZoneParams::KillAndPlayEntry(const NS::SceneActor* owner,
                                               NS::HitSensor* self,
                                               NS::HitSensor* other,
                                               const NS::Vector3& position) const
        {
            NS_ASSERT(Game, owner != nullptr, "音とエフェクトを出すアクターが渡されていない");
            NS_ASSERT(Game, self != nullptr && other != nullptr, "重なったセンサーが渡されていない");
            // 受け手が応じなかった時は、死んでいないので何も出さない
            if (!SendMsgInstantDeath(other, self, RestartDelayFrames()))
            {
                return false;
            }
            // 空の欄は鳴らさず、出さない
            (void)NS::PlaySound(owner, m_enterSound, m_enterVolume);
            (void)NS::PlayEffect(owner, m_enterEffect, position);
            return true;
        }
    } // namespace Level
} // namespace GL
