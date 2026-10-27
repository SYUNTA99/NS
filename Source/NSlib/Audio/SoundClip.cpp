#include "NSlib/Audio/SoundClip.h"

#include "NSlib/Audio/AudioDevice.h"
#include "NSlib/Audio/detail/SoundVolume.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Core/Windows/StringUtils.h"

#include <Audio.h>
#include <SoundCommon.h>
#include <WAVFileReader.h>

#include <cstdint>
#include <string>

namespace NS
{
    namespace Audio
    {
        namespace
        {
            [[nodiscard]] bool IsSupportedFormat(const WAVEFORMATEX* wfx) noexcept
            {
                switch (DirectX::GetFormatTag(wfx))
                {
                case WAVE_FORMAT_PCM:
                case WAVE_FORMAT_IEEE_FLOAT:
                case WAVE_FORMAT_ADPCM:
                    return true;
                default:
                    return false;
                }
            }
        } // namespace

        SoundClip::SoundClip(AudioDevice* device, std::string_view path)
        {
            // 読み込みと形式の確かめを装置より先に行い、装置の無いテストでも壊れた素材を見つける
            const std::wstring widePath = OS::StringUtils::WideFromUtf8(path);
            std::unique_ptr<uint8_t[]> bytes;
            DirectX::WAVData wav{};
            const HRESULT hr = DirectX::LoadWAVAudioFromFileEx(widePath.c_str(), bytes, wav);
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Audio, "SoundClip: WAV を読めない: {} (hr=0x{:08X})", path, static_cast<uint>(hr));
                return;
            }
            // SoundEffect の構築子は形式が違うと throw するので、渡す前に同じ検査をする
            if (!IsSupportedFormat(wav.wfx) || !DirectX::IsValid(wav.wfx) || wav.startAudio == nullptr ||
                wav.audioBytes == 0)
            {
                NS_LOG_ERROR(Audio, "SoundClip: WAV の形式が PCM・IEEE float・MS-ADPCM でないか壊れている: {}", path);
                return;
            }

            if (device == nullptr || !device->IsValid())
            {
                return;
            }
            // TODO: XAudio2 の中の失敗は DirectXTK が throw し、例外を切ったビルドでは終了する。
            //       素材を読んだ時に落ちる報告が来たら、SoundEffect を使わず XAudio2 の声を直に作る
            m_effect = std::make_unique<DirectX::SoundEffect>(
                device->Engine(), bytes, wav.wfx, wav.startAudio, static_cast<size_t>(wav.audioBytes));
        }

        SoundClip::~SoundClip() = default;

        void SoundClip::Play(float volume)
        {
            if (!m_effect)
            {
                return;
            }
            m_effect->Play(detail::ToXAudioVolume(volume), 0.0f, 0.0f);
        }
    } // namespace Audio
} // namespace NS
