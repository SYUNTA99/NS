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

            // XAudio2 の MS-ADPCM が受け付ける 1 ブロックの標本の数。DirectX::IsValid はこの範囲を見ない
            constexpr uint16 k_MinAdpcmSamplesPerBlock = 32;
            constexpr uint16 k_MaxAdpcmSamplesPerBlock = 512;

            //! MS-ADPCM の時だけ、1 ブロックの標本の数が XAudio2 の範囲にあるかを見る。ほかの形式は true
            [[nodiscard]] bool IsXAudio2AdpcmBlock(const WAVEFORMATEX* wfx) noexcept
            {
                if (DirectX::GetFormatTag(wfx) != WAVE_FORMAT_ADPCM)
                {
                    return true;
                }
                const uint16 samples = reinterpret_cast<const ADPCMWAVEFORMAT*>(wfx)->wSamplesPerBlock;
                return samples >= k_MinAdpcmSamplesPerBlock && samples <= k_MaxAdpcmSamplesPerBlock;
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
            // 範囲の外は SoundEffect を作れても、鳴らす時に XAudio2 が声を作れず DirectXTK が throw する
            if (!IsXAudio2AdpcmBlock(wav.wfx))
            {
                NS_LOG_ERROR(Audio,
                             "SoundClip: MS-ADPCM の 1 ブロックの標本の数 {} が XAudio2 の範囲 {}〜{} の外: {}",
                             reinterpret_cast<const ADPCMWAVEFORMAT*>(wav.wfx)->wSamplesPerBlock,
                             k_MinAdpcmSamplesPerBlock,
                             k_MaxAdpcmSamplesPerBlock,
                             path);
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
