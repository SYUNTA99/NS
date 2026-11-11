#include "NSlib/Audio/SoundScene.h"

#include "NSlib/Audio/XA2Sound.h"
#include "NSlib/Audio/detail/SoundVolume.h"

#include <Audio.h>

#include <utility>

namespace NS
{
    namespace Audio
    {
        SoundScene::SoundScene() noexcept = default;

        SoundScene::~SoundScene() = default;

        SoundHandle SoundScene::Play(const XA2Sound* sound, float volume, bool loop)
        {
            if (sound == nullptr)
            {
                return {};
            }
            Voice voice;
            voice.loop = loop;
            if (sound->IsValid())
            {
                voice.instance = sound->Effect()->CreateInstance();
                voice.instance->SetVolume(detail::ToXAudioVolume(volume));
                voice.instance->Play(loop);
            }
            voice.id = m_nextId;
            ++m_nextId;
            m_voices.push_back(std::move(voice));
            return SoundHandle{m_voices.back().id};
        }

        void SoundScene::Stop(SoundHandle handle) noexcept
        {
            const std::size_t index = IndexOf(handle);
            if (index == m_voices.size())
            {
                return;
            }
            // ソースボイスは SoundEffectInstance の破棄で止まる
            m_voices.erase(m_voices.begin() + static_cast<std::ptrdiff_t>(index));
        }

        void SoundScene::StopAll() noexcept
        {
            m_voices.clear();
        }

        void SoundScene::SetVolume(SoundHandle handle, float volume)
        {
            const std::size_t index = IndexOf(handle);
            if (index == m_voices.size() || !m_voices[index].instance)
            {
                return;
            }
            m_voices[index].instance->SetVolume(detail::ToXAudioVolume(volume));
        }

        bool SoundScene::Exists(SoundHandle handle) const noexcept
        {
            return IndexOf(handle) != m_voices.size();
        }

        void SoundScene::Update()
        {
            std::erase_if(m_voices, [](const Voice& voice) {
                return !voice.loop && (!voice.instance || voice.instance->GetState() == DirectX::SoundState::STOPPED);
            });
            for (const Voice& voice : m_voices)
            {
                // 機器を失うと DirectXTK がソースボイスを捨てて止めた事にするので、鳴っているかは毎回問う
                if (voice.loop && voice.instance && voice.instance->GetState() != DirectX::SoundState::PLAYING)
                {
                    voice.instance->Play(true);
                }
            }
        }

        std::size_t SoundScene::IndexOf(SoundHandle handle) const noexcept
        {
            if (!handle.IsValid())
            {
                return m_voices.size();
            }
            for (std::size_t index = 0; index < m_voices.size(); ++index)
            {
                if (m_voices[index].id == handle.value)
                {
                    return index;
                }
            }
            return m_voices.size();
        }
    } // namespace Audio
} // namespace NS
