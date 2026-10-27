#include "NSlib/CoreObject/IGet/IGetAudio.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/Sound.h"

namespace NS
{
    void PreloadSounds(const IGetAudio* user, std::initializer_list<SoundName> names)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        Sound* sound = user->GetSound();
        if (sound == nullptr)
        {
            return;
        }
        for (const SoundName& name : names)
        {
            sound->Preload(&AssetManager::Get(), name.name);
        }
    }

    void PlaySound(const IGetAudio* user, const SoundName& name, float volume)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        Sound* sound = user->GetSound();
        if (sound == nullptr || user->GetAudioDevice() == nullptr)
        {
            return;
        }
        sound->Play(name.name, volume);
    }
} // namespace NS
