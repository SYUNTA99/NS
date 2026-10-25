#include "NSlib/Audio/AudioDevice.h"

#include "NSlib/Core/Logger.h"

#include <Audio.h>
#include <audioclient.h>
#include <wrl/client.h>

namespace NS
{
    namespace Audio
    {
        namespace
        {
            // 音無しの間に、機器が差されたかを確かめる間隔
            constexpr int k_SilentRetryFrames = 120;

            // DirectXTK の AudioEngine は、成功・機器が無い・使用中の時のほかは throw し、例外を切ったビルドでは終了する。
            // 同じ作り方で XAudio2 と既定の機器の声を先に作ってみて、throw しない結果かを確かめる。
            // TODO: 声を作った後の失敗 (GetChannelMask・X3DAudioInitialize) と、確かめてから作るまでの機器の変化は見ていない。
            //       そこで終わる報告が来たら、AudioEngine を使わず XAudio2 を直に作り、HRESULT で受ける
            [[nodiscard]] bool EngineCanBeCreated() noexcept
            {
                Microsoft::WRL::ComPtr<IXAudio2> xaudio2;
                HRESULT hr = XAudio2Create(xaudio2.GetAddressOf(), 0u);
                if (SUCCEEDED(hr))
                {
                    IXAudio2MasteringVoice* master = nullptr;
                    hr = xaudio2->CreateMasteringVoice(&master, 0u, 0u, 0u, nullptr, nullptr, AudioCategory_GameEffects);
                    if (master != nullptr)
                    {
                        master->DestroyVoice();
                    }
                }
                if (SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) || hr == AUDCLNT_E_DEVICE_IN_USE)
                {
                    return true;
                }
                NS_LOG_ERROR(Audio, "AudioDevice: XAudio2 を作れない ({:#010x})。音無しで動く", static_cast<unsigned int>(hr));
                return false;
            }
        } // namespace

        AudioDevice::AudioDevice(bool enable)
        {
            if (!enable || !EngineCanBeCreated())
            {
                return;
            }

            // 機器が無い・使用中の時は、AudioEngine_ThrowOnNoAudioHW を付けないので音無しで動く
            m_engine = std::make_unique<DirectX::AudioEngine>(DirectX::AudioEngine_Default);
            if (!m_engine->IsAudioDevicePresent())
            {
                NS_LOG_WARN(Audio, "AudioDevice: 音の機器が無いか使用中のため、音無しで動く");
            }
        }

        AudioDevice::~AudioDevice() = default;

        void AudioDevice::Update()
        {
            if (!m_engine || m_engine->Update())
            {
                return;
            }

            // Update が false を返すのは、音無しの時か機器を失った時。
            // 失った時は最初の 1 回をすぐ、それからは音無しの時と同じく間をあけて、既定の機器へ繋ぎ直す。
            // 繋ぎ直す前に XAudio2 を作れなかった時は、機器を失ったままなので、毎フレームでなく間をあけて試す
            if (m_engine->IsCriticalError() && !m_lost)
            {
                NS_LOG_WARN(Audio, "AudioDevice: 音の機器を失ったので、既定の機器へ繋ぎ直す");
                m_lost = true;
                m_silentFrames = k_SilentRetryFrames;
            }
            if (++m_silentFrames < k_SilentRetryFrames)
            {
                return;
            }
            m_silentFrames = 0;
            if (!EngineCanBeCreated())
            {
                return;
            }
            const bool wasLost = m_lost;
            m_lost = false;
            if (m_engine->Reset())
            {
                NS_LOG_INFO(Audio, "AudioDevice: 既定の機器へ繋いだ");
            }
            else if (wasLost)
            {
                NS_LOG_WARN(Audio, "AudioDevice: 既定の機器が無いため、音無しで動く");
            }
        }

        void AudioDevice::Suspend() noexcept
        {
            if (m_engine)
            {
                m_engine->Suspend();
            }
        }
    } // namespace Audio
} // namespace NS
