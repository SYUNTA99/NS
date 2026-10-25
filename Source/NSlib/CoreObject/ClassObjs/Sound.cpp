#include "NSlib/CoreObject/ClassObjs/Sound.h"

#include "NSlib/Audio/SoundClip.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/SoundClip.h"

namespace NS
{
    void Sound::Preload(AssetManager* assets, std::string_view name)
    {
        NS_ASSERT(Audio, assets != nullptr, "音を読む AssetManager が null");
        if (name.empty())
        {
            return;
        }
        SoundClip*& sound = m_sounds[std::string{name}];
        if (sound == nullptr)
        {
            sound = assets->LoadSoundClip("Assets/Sounds/" + std::string{name} + ".wav");
        }
    }

    void Sound::Play(std::string_view name, float volume)
    {
        if (name.empty())
        {
            return;
        }
        const std::map<std::string, SoundClip*, std::less<>>::const_iterator found = m_sounds.find(name);
        if (found == m_sounds.end())
        {
            m_sounds.emplace(std::string{name}, nullptr);
            NS_LOG_WARN(Audio, "読んでいない音 {} を鳴らそうとした。PreloadSounds に名前を渡す", name);
            return;
        }
        const SoundClip* sound = found->second;
        if (sound != nullptr && sound->IsValid())
        {
            sound->Clip()->Play(volume);
        }
    }
} // namespace NS
