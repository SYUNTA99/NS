#include "NSlib/CoreObject/IGet/IGetAudio.h"

#include "NSlib/Audio/XA2Sound.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/Asset/Sound.h"
#include "NSlib/CoreObject/ClassObjs/SoundObj.h"

namespace NS
{
    void PlaySound(const IGetAudio* user, const Sound* sound, float volume)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (user->GetAudioDevice() == nullptr || sound == nullptr || !sound->IsValid())
        {
            return;
        }
        sound->XA2()->Play(volume);
    }

    uint32 PlayLoopSound(const IGetAudio* user, const Sound* sound, float volume)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        SoundObj* part = user->GetSound();
        if (part == nullptr)
        {
            return 0;
        }
        return part->PlayLoop(user->GetAudioDevice(), sound, volume);
    }

    void StopSound(const IGetAudio* user, uint32 id) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (SoundObj* part = user->GetSound())
        {
            part->Stop(id);
        }
    }

    void SetSoundVolume(const IGetAudio* user, uint32 id, float volume) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (SoundObj* part = user->GetSound())
        {
            part->SetVolume(id, volume);
        }
    }
} // namespace NS
