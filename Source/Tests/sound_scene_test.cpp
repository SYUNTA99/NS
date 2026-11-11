#include "NSlib/Audio/AudioDevice.h"
#include "NSlib/Audio/SoundScene.h"
#include "NSlib/Audio/XA2Sound.h"
#include "NSlib/Core/Windows/Filesystem.h"

#include "TestLogCapture.h"

#include <Audio.h>
#include <gtest/gtest.h>

#include <string>

namespace
{
    [[nodiscard]] std::string ShippedSoundPath()
    {
        using NS::OS::FileSystem;
        return FileSystem::Combine(FileSystem::ContentRoot(), "Assets/Sounds/jump.wav");
    }
} // namespace

TEST(SoundSceneTest, NullSoundGivesAnInvalidHandle)
{
    NS::Audio::SoundScene sounds;

    const NS::Audio::SoundHandle handle = sounds.Play(nullptr, 1.0f);

    EXPECT_FALSE(handle.IsValid());
    EXPECT_FALSE(sounds.Exists(handle));
}

// 音の装置が無い世界で読んだ音も一覧に残り、始めた・止めたを番号で読める
TEST(SoundSceneTest, LoopOfASoundWithoutVoiceStaysUntilStop)
{
    const NS::Audio::XA2Sound clip(nullptr, ShippedSoundPath());
    ASSERT_FALSE(clip.IsValid());
    NS::Audio::SoundScene sounds;

    const NS::Audio::SoundHandle handle = sounds.Play(&clip, 0.5f, true);

    ASSERT_TRUE(handle.IsValid());
    EXPECT_TRUE(sounds.Exists(handle));
    sounds.Update();
    EXPECT_TRUE(sounds.Exists(handle));

    sounds.Stop(handle);
    EXPECT_FALSE(sounds.Exists(handle));
}

// ソースボイスの無い 1 回の音は、鳴り終わったかを問えないので、次の Update で外れる
TEST(SoundSceneTest, OneShotWithoutVoiceIsDroppedByTheNextUpdate)
{
    const NS::Audio::XA2Sound clip(nullptr, ShippedSoundPath());
    NS::Audio::SoundScene sounds;

    const NS::Audio::SoundHandle handle = sounds.Play(&clip, 0.5f);

    ASSERT_TRUE(handle.IsValid());
    EXPECT_TRUE(sounds.Exists(handle));
    sounds.Update();
    EXPECT_FALSE(sounds.Exists(handle));
}

TEST(SoundSceneTest, HandlesAreNotReused)
{
    const NS::Audio::XA2Sound clip(nullptr, ShippedSoundPath());
    NS::Audio::SoundScene sounds;

    const NS::Audio::SoundHandle first = sounds.Play(&clip, 1.0f, true);
    sounds.Stop(first);
    const NS::Audio::SoundHandle second = sounds.Play(&clip, 1.0f, true);

    EXPECT_NE(first.value, second.value);
    EXPECT_FALSE(sounds.Exists(first));
    EXPECT_TRUE(sounds.Exists(second));
}

TEST(SoundSceneTest, StopAllEmptiesTheScene)
{
    const NS::Audio::XA2Sound clip(nullptr, ShippedSoundPath());
    NS::Audio::SoundScene sounds;
    const NS::Audio::SoundHandle loop = sounds.Play(&clip, 1.0f, true);
    const NS::Audio::SoundHandle oneShot = sounds.Play(&clip, 1.0f);

    sounds.StopAll();

    EXPECT_FALSE(sounds.Exists(loop));
    EXPECT_FALSE(sounds.Exists(oneShot));
}

TEST(SoundSceneTest, UnknownHandlesAndAnEmptySceneDoNothing)
{
    const TestLogCapture log;
    NS::Audio::SoundScene sounds;

    sounds.Stop(NS::Audio::SoundHandle{42});
    sounds.SetVolume(NS::Audio::SoundHandle{42}, 0.5f);
    sounds.Update();
    sounds.StopAll();

    EXPECT_FALSE(sounds.Exists(NS::Audio::SoundHandle{42}));
    EXPECT_EQ(log.ErrorCount(), 0);
}

// 音の機器がある PC でだけ動く。ソースボイスを作って鳴らし、番号で止められる
TEST(SoundSceneTest, WithADeviceSoundsPlayAndStop)
{
    NS::Audio::AudioDevice device(true);
    if (!device.IsValid() || !device.Engine()->IsAudioDevicePresent())
    {
        GTEST_SKIP() << "音の機器が無い";
    }
    const NS::Audio::XA2Sound clip(&device, ShippedSoundPath());
    ASSERT_TRUE(clip.IsValid());
    {
        NS::Audio::SoundScene sounds;
        // 聞こえない音量で鳴らす
        const NS::Audio::SoundHandle oneShot = sounds.Play(&clip, 0.0f);
        const NS::Audio::SoundHandle loop = sounds.Play(&clip, 0.0f, true);
        sounds.Update();

        EXPECT_TRUE(sounds.Exists(oneShot));
        EXPECT_TRUE(sounds.Exists(loop));
        sounds.SetVolume(loop, 0.0f);
        sounds.Stop(oneShot);
        sounds.Stop(loop);
        EXPECT_FALSE(sounds.Exists(oneShot));
        EXPECT_FALSE(sounds.Exists(loop));
    }
    device.Suspend();
}
