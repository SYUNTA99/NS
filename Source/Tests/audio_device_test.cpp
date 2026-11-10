#include "NSlib/Audio/AudioDevice.h"
#include "NSlib/Audio/XA2Sound.h"
#include "NSlib/Audio/SoundLoop.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Sound.h"

#include "TestLogCapture.h"

#include <Audio.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

// 作らない設定の装置は無効で、更新しても何もせず、失敗として ERROR を出さない
TEST(AudioDeviceTest, DisabledDeviceIsInvalidAndSilent)
{
    const TestLogCapture log;

    NS::Audio::AudioDevice device(false);
    device.Update();

    EXPECT_FALSE(device.IsValid());
    EXPECT_EQ(log.ErrorCount(), 0);
}

namespace
{
    void AppendU16(std::vector<char>& bytes, std::uint16_t value)
    {
        bytes.push_back(static_cast<char>(value & 0xFF));
        bytes.push_back(static_cast<char>((value >> 8) & 0xFF));
    }

    void AppendU32(std::vector<char>& bytes, std::uint32_t value)
    {
        AppendU16(bytes, static_cast<std::uint16_t>(value & 0xFFFF));
        AppendU16(bytes, static_cast<std::uint16_t>(value >> 16));
    }

    void AppendTag(std::vector<char>& bytes, const char* tag)
    {
        bytes.insert(bytes.end(), tag, tag + 4);
    }

    //! 16 ビットの PCM の WAV を作る。blockAlign を変えると形式の値が壊れた WAV になる
    [[nodiscard]] std::vector<char> MakePcmWav(std::uint16_t blockAlign)
    {
        constexpr std::uint16_t k_Channels = 1;
        constexpr std::uint32_t k_SampleRate = 44100;
        constexpr std::uint16_t k_Bits = 16;
        constexpr std::uint32_t k_DataBytes = 64;

        std::vector<char> bytes;
        AppendTag(bytes, "RIFF");
        AppendU32(bytes, 4 + (8 + 16) + (8 + k_DataBytes));
        AppendTag(bytes, "WAVE");
        AppendTag(bytes, "fmt ");
        AppendU32(bytes, 16);
        AppendU16(bytes, 1); // WAVE_FORMAT_PCM
        AppendU16(bytes, k_Channels);
        AppendU32(bytes, k_SampleRate);
        AppendU32(bytes, k_SampleRate * blockAlign);
        AppendU16(bytes, blockAlign);
        AppendU16(bytes, k_Bits);
        AppendTag(bytes, "data");
        AppendU32(bytes, k_DataBytes);
        bytes.insert(bytes.end(), k_DataBytes, '\0');
        return bytes;
    }

    //! 2 チャンネル・48 kHz の MS-ADPCM の WAV を 1 ブロックだけ作る。ブロックのバイト数は標本の数から出す
    [[nodiscard]] std::vector<char> MakeAdpcmWav(std::uint16_t samplesPerBlock)
    {
        constexpr std::uint16_t k_Channels = 2;
        constexpr std::uint32_t k_SampleRate = 48000;
        constexpr std::uint32_t k_FormatBytes = 18 + 32;
        constexpr std::int16_t k_Coefficients[7][2] = {
            {256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232}};
        const std::uint16_t blockAlign =
            static_cast<std::uint16_t>((samplesPerBlock - 2) * k_Channels / 2 + 7 * k_Channels);

        std::vector<char> bytes;
        AppendTag(bytes, "RIFF");
        AppendU32(bytes, 4 + (8 + k_FormatBytes) + (8 + blockAlign));
        AppendTag(bytes, "WAVE");
        AppendTag(bytes, "fmt ");
        AppendU32(bytes, k_FormatBytes);
        AppendU16(bytes, 2); // WAVE_FORMAT_ADPCM
        AppendU16(bytes, k_Channels);
        AppendU32(bytes, k_SampleRate);
        AppendU32(bytes, k_SampleRate / samplesPerBlock * blockAlign);
        AppendU16(bytes, blockAlign);
        AppendU16(bytes, 4);  // 1 標本のビット数
        AppendU16(bytes, 32); // cbSize
        AppendU16(bytes, samplesPerBlock);
        AppendU16(bytes, 7); // 係数の数
        for (const std::int16_t(&pair)[2] : k_Coefficients)
        {
            AppendU16(bytes, static_cast<std::uint16_t>(pair[0]));
            AppendU16(bytes, static_cast<std::uint16_t>(pair[1]));
        }
        AppendTag(bytes, "data");
        AppendU32(bytes, blockAlign);
        bytes.insert(bytes.end(), blockAlign, '\0');
        return bytes;
    }

    class XA2SoundTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_dir = std::filesystem::temp_directory_path() / "ns_sound_clip_test" /
                    ::testing::UnitTest::GetInstance()->current_test_info()->name();
            std::error_code error;
            std::filesystem::remove_all(m_dir, error);
            std::filesystem::create_directories(m_dir);
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(m_dir, error);
        }

        [[nodiscard]] std::string PathOf(const char* name) const
        {
            const std::u8string u8 = (m_dir / name).generic_u8string();
            return std::string(u8.begin(), u8.end());
        }

        [[nodiscard]] std::string WriteFile(const char* name, const std::vector<char>& bytes) const
        {
            std::ofstream file(m_dir / name, std::ios::binary);
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            return PathOf(name);
        }

        std::filesystem::path m_dir;
        NS::Audio::AudioDevice m_device{false};
    };
} // namespace

// 読める WAV でも、無効な装置では音を作らない。音を作らない設定は失敗ではないので ERROR を出さない
TEST_F(XA2SoundTest, ValidWavWithInvalidDeviceIsInvalidWithoutError)
{
    const std::string path = WriteFile("ok.wav", MakePcmWav(2));
    const TestLogCapture log;

    NS::Audio::XA2Sound clip(&m_device, path);
    clip.Play(1.0f);

    EXPECT_FALSE(clip.IsValid());
    EXPECT_EQ(log.ErrorCount(), 0);
}

// 無いファイルは読めず、ERROR を出す
TEST_F(XA2SoundTest, MissingFileIsInvalidWithError)
{
    const TestLogCapture log;

    const NS::Audio::XA2Sound clip(&m_device, PathOf("missing.wav"));

    EXPECT_FALSE(clip.IsValid());
    EXPECT_EQ(log.ErrorCount(), 1);
}

// WAV でないファイルは読めず、ERROR を出す
TEST_F(XA2SoundTest, NotWavFileIsInvalidWithError)
{
    const std::vector<char> text = {'n', 'o', 't', ' ', 'a', ' ', 'w', 'a', 'v', 'e'};
    const std::string path = WriteFile("text.wav", text);
    const TestLogCapture log;

    const NS::Audio::XA2Sound clip(&m_device, path);

    EXPECT_FALSE(clip.IsValid());
    EXPECT_EQ(log.ErrorCount(), 1);
}

// 形式の値が壊れた WAV (1 サンプルのバイト数が合わない) は読めず、ERROR を出す
TEST_F(XA2SoundTest, BrokenFormatIsInvalidWithError)
{
    const std::string path = WriteFile("broken.wav", MakePcmWav(3));
    const TestLogCapture log;

    const NS::Audio::XA2Sound clip(&m_device, path);

    EXPECT_FALSE(clip.IsValid());
    EXPECT_EQ(log.ErrorCount(), 1);
}

// 1 ブロックが 512 標本を超える MS-ADPCM は、XAudio2 が声を作れずに DirectXTK が throw するので、読む時に断る
TEST_F(XA2SoundTest, AdpcmBlockOverXAudio2LimitIsInvalidWithError)
{
    const std::string path = WriteFile("long_block.wav", MakeAdpcmWav(1012));
    const TestLogCapture log;

    const NS::Audio::XA2Sound clip(&m_device, path);

    EXPECT_FALSE(clip.IsValid());
    EXPECT_EQ(log.ErrorCount(), 1);
}

// 32〜512 標本の中でも、XAudio2 が受け付けない標本の数は読む時に断る
TEST_F(XA2SoundTest, AdpcmBlockThatXAudio2RejectsIsInvalidWithError)
{
    const std::string path = WriteFile("rejected_block.wav", MakeAdpcmWav(500));
    const TestLogCapture log;

    const NS::Audio::XA2Sound clip(&m_device, path);

    EXPECT_FALSE(clip.IsValid());
    EXPECT_EQ(log.ErrorCount(), 1);
}

// 1 ブロックがちょうど 512 標本の MS-ADPCM は XAudio2 が受け付けるので、断らない
TEST_F(XA2SoundTest, AdpcmBlockAtXAudio2LimitIsReadWithoutError)
{
    const std::string path = WriteFile("max_block.wav", MakeAdpcmWav(512));
    const TestLogCapture log;

    const NS::Audio::XA2Sound clip(&m_device, path);

    EXPECT_EQ(log.ErrorCount(), 0);
}

// 出荷の音は全部、XAudio2 が鳴らせる形で読める
TEST(SoundAssetTest, ShippedSoundsAreReadWithoutError)
{
    using NS::OS::FileSystem;
    const std::vector<std::string> paths =
        FileSystem::ListFiles(FileSystem::Combine(FileSystem::ContentRoot(), "Assets/Sounds"), ".wav");
    ASSERT_FALSE(paths.empty());
    const TestLogCapture log;

    for (const std::string& path : paths)
    {
        const NS::Audio::XA2Sound clip(nullptr, path);
    }

    EXPECT_EQ(log.ErrorCount(), 0);
}

namespace
{
    //! WAV の fmt の塊を返す。見つからなければ空
    [[nodiscard]] std::vector<char> ReadWavFormat(const std::string& path)
    {
        std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::size_t offset = 12;
        while (offset + 8 <= bytes.size())
        {
            std::uint32_t size = 0;
            std::memcpy(&size, bytes.data() + offset + 4, sizeof(size));
            if (std::memcmp(bytes.data() + offset, "fmt ", 4) == 0 && offset + 8 + size <= bytes.size())
            {
                return std::vector<char>(bytes.begin() + static_cast<std::ptrdiff_t>(offset + 8),
                                         bytes.begin() + static_cast<std::ptrdiff_t>(offset + 8 + size));
            }
            offset += 8 + size + (size & 1);
        }
        return {};
    }
} // namespace

// 出荷の音は全部、この PC の XAudio2 がソースボイスを作れる形式で置く。作れないと DirectXTK が鳴らす時に throw して止まる
TEST(SoundAssetTest, ShippedSoundsCreateXAudio2Voice)
{
    using NS::OS::FileSystem;
    NS::Audio::AudioDevice device(true);
    // 機器が無い時も IsValid は true で、GetInterface が nullptr を返す
    if (!device.IsValid() || !device.Engine()->IsAudioDevicePresent())
    {
        GTEST_SKIP() << "音の機器が無い";
    }
    IXAudio2* xaudio = device.Engine()->GetInterface();
    const std::vector<std::string> paths =
        FileSystem::ListFiles(FileSystem::Combine(FileSystem::ContentRoot(), "Assets/Sounds"), ".wav");
    ASSERT_FALSE(paths.empty());

    for (const std::string& path : paths)
    {
        const std::vector<char> format = ReadWavFormat(path);
        ASSERT_GE(format.size(), sizeof(PCMWAVEFORMAT)) << path;
        // PCM の fmt は cbSize を持たないので、WAVEFORMATEX の大きさまで 0 で埋める
        std::vector<char> full(std::max(format.size(), sizeof(WAVEFORMATEX)), '\0');
        std::memcpy(full.data(), format.data(), format.size());

        IXAudio2SourceVoice* voice = nullptr;
        const HRESULT hr = xaudio->CreateSourceVoice(&voice, reinterpret_cast<const WAVEFORMATEX*>(full.data()));
        EXPECT_TRUE(SUCCEEDED(hr)) << path << " hr=0x" << std::hex << static_cast<std::uint32_t>(hr);
        if (voice != nullptr)
        {
            voice->DestroyVoice();
        }
    }
}

// 装置を受け取っていない AssetManager は、同じ道筋に同じ無効な音を返し、ファイルを読まないので ERROR を出さない
TEST(SoundAssetTest, WithoutDeviceReturnsSameInvalidSoundWithoutError)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const TestLogCapture log;

    const NS::Sound* first = assets.LoadSound("Assets/Sounds/missing.wav");
    const NS::Sound* second = assets.LoadSound("Assets/Sounds/./missing.wav");

    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_FALSE(first->IsValid());
    EXPECT_EQ(log.ErrorCount(), 0);
}

// 装置を受け取った AssetManager は、読めない音で ERROR を出す (無効な装置でも読み込みは確かめる)
TEST(SoundAssetTest, WithDeviceReportsUnreadableSound)
{
    NS::Audio::AudioDevice device(false);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    assets.SetAudioDevice(&device);
    const TestLogCapture log;

    const NS::Sound* sound = assets.LoadSound("Assets/Sounds/missing.wav");

    EXPECT_FALSE(sound->IsValid());
    EXPECT_EQ(log.ErrorCount(), 1);
}

// 無効な音から作ったループは無効で、鳴っているかは Play から Stop までの間だけ true になる
TEST_F(XA2SoundTest, LoopFromInvalidClipTracksPlayingState)
{
    const NS::Audio::XA2Sound clip(&m_device, WriteFile("ok.wav", MakePcmWav(2)));
    NS::Audio::SoundLoop loop(clip);

    EXPECT_FALSE(loop.IsValid());
    EXPECT_FALSE(loop.IsPlaying());
    loop.Play();
    EXPECT_TRUE(loop.IsPlaying());
    loop.Stop();
    EXPECT_FALSE(loop.IsPlaying());
}
