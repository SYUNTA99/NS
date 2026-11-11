#include "NSlib/CoreObject/IGet/IGetAudio.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/Asset/Sound.h"
#include "NSlib/CoreObject/ClassObjs/SoundObj.h"

namespace NS
{
    uint32 PlaySound(const IGetAudio* user, const Sound* sound, float volume, bool loop)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        SoundObj* part = user->GetSound();
        if (part == nullptr)
        {
            return 0;
        }
        return part->Play(user->GetSoundScene(), sound, volume, loop);
    }

    void StopSound(const IGetAudio* user, uint32 id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (SoundObj* part = user->GetSound())
        {
            part->Stop(user->GetSoundScene(), id);
        }
    }

    void SetSoundVolume(const IGetAudio* user, uint32 id, float volume)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (SoundObj* part = user->GetSound())
        {
            part->SetVolume(user->GetSoundScene(), id, volume);
        }
    }
} // namespace NS
