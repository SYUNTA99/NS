#include "NSlib/Audio/SoundScene.h"

#include "NSlib/Audio/XA2Sound.h"
#include "NSlib/Audio/detail/SoundVolume.h"

#include <Audio.h>

#include <cmath>
#include <utility>

namespace NS
{
    namespace Audio
    {
        SoundScene::SoundScene() noexcept = default;

        SoundScene::~SoundScene() = default;

        SoundHandle SoundScene::Play(const XA2Sound* sound, float volume, bool loop)
        {
            return Start(sound, volume, loop, nullptr);
        }

        SoundHandle SoundScene::PlayAt(const XA2Sound* sound, const Vector3& position, float volume, bool loop)
        {
            return Start(sound, volume, loop, &position);
        }

        void SoundScene::SetListener(const Vector3& position, const Vector3& forward, const Vector3& up) noexcept
        {
            if (IsFinite(position))
            {
                m_listener.position = position;
            }
            // X3DAudio は長さ 1 で直交した前と上を求める。作れない入力では、前の向きのままにする
            const float forwardLength = forward.Length();
            if (!HasPositiveMagnitude(forwardLength))
            {
                return;
            }
            const Vector3 unitForward = forward / forwardLength;
            const Vector3 upAcross = RejectUnit(up, unitForward);
            const float upLength = upAcross.Length();
            if (!HasPositiveMagnitude(upLength))
            {
                return;
            }
            m_listener.forward = unitForward;
            m_listener.up = upAcross / upLength;
        }

        void SoundScene::SetFullVolumeDistance(float meters) noexcept
        {
            if (std::isfinite(meters) && meters > 0.0f)
            {
                m_fullVolumeDistance = meters;
            }
        }

        SoundHandle SoundScene::Start(const XA2Sound* sound, float volume, bool loop, const Vector3* position)
        {
            if (sound == nullptr)
            {
                return {};
            }
            Voice voice;
            voice.loop = loop;
            // 位置が非数の音は、位置なしで鳴らす
            if (position != nullptr && IsFinite(*position))
            {
                voice.positional = true;
                voice.position = *position;
            }
            if (sound->IsValid())
            {
                DirectX::SOUND_EFFECT_INSTANCE_FLAGS flags = DirectX::SoundEffectInstance_Default;
                if (voice.positional)
                {
                    flags = DirectX::SoundEffectInstance_Use3D;
                }
                voice.instance = sound->Effect()->CreateInstance(flags);
                voice.instance->SetVolume(detail::ToXAudioVolume(volume));
                voice.instance->Play(loop);
                // ソースボイスは Play が作るので、定位は Play の後に渡す
                if (voice.positional)
                {
                    Apply3D(voice);
                }
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
                if (voice.positional && voice.instance)
                {
                    Apply3D(voice);
                }
            }
        }

        void SoundScene::Apply3D(const Voice& voice) const
        {
            DirectX::AudioListener listener;
            listener.SetPosition(m_listener.position);
            listener.SetOrientation(m_listener.forward, m_listener.up);
            DirectX::AudioEmitter emitter;
            emitter.SetPosition(voice.position);
            // 音源のチャンネル数は音のデータと合わせる。半径 0 で、ステレオの音も 1 点から鳴らす
            emitter.EnableDefaultMultiChannel(voice.instance->GetChannelCount(), 0.0f);
            emitter.CurveDistanceScaler = m_fullVolumeDistance;
            // NS の座標は左手系
            voice.instance->Apply3D(listener, emitter, false);
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
