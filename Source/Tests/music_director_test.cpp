#include "NSlib/Audio/AudioDevice.h"
#include "NSlib/Audio/SoundScene.h"
#include "NSlib/Core/Windows/FileSystem.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Sound.h"
#include "NSlib/CoreObject/System/MusicDirector.h"

#include <Audio.h>
#include <gtest/gtest.h>

// 始めた曲は Stop まで始め直さない。止めた後は、次の Start でまた始める
TEST(MusicDirector, StartIsAcceptedOnceUntilStop)
{
    NS::MusicDirector music;
    EXPECT_FALSE(music.IsStarted());

    EXPECT_TRUE(music.Start(nullptr, nullptr, 0.5f));
    EXPECT_TRUE(music.IsStarted());
    EXPECT_FALSE(music.Start(nullptr, nullptr, 0.5f));

    music.Stop(nullptr);
    EXPECT_FALSE(music.IsStarted());
    EXPECT_TRUE(music.Start(nullptr, nullptr, 0.5f));
}

// 鳴らす先の無いシーン (当たりの下見) でも、音を出せない装置の音でも、始める・止めるを同じに呼べる
TEST(MusicDirector, StartsAndStopsWithOrWithoutASoundScene)
{
    NS::Audio::AudioDevice device(false);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    assets.SetAudioDevice(&device);
    const NS::Sound* clip = assets.LoadSound("Assets/Sounds/bgm_course.wav");
    ASSERT_NE(clip, nullptr);
    NS::MusicDirector music;

    EXPECT_TRUE(music.Start(nullptr, clip, 0.5f));
    music.Stop(nullptr);

    NS::Audio::SoundScene sounds;
    EXPECT_TRUE(music.Start(&sounds, clip, 0.5f));
    sounds.Update();
    EXPECT_TRUE(music.IsStarted());
    music.Stop(&sounds);
    EXPECT_FALSE(music.IsStarted());
}

// 曲は SoundScene の中でループの音として鳴り、Update を越えて残り、Stop で外れる。音の機器がある PC でだけ動く
TEST(MusicDirector, MusicStaysInTheSoundSceneUntilStop)
{
    NS::Audio::AudioDevice device(true);
    if (!device.IsValid() || !device.Engine()->IsAudioDevicePresent())
    {
        GTEST_SKIP() << "音の機器が無い";
    }
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    assets.SetAudioDevice(&device);
    const NS::Sound* clip = assets.LoadSound("Assets/Sounds/bgm_course.wav");
    ASSERT_NE(clip, nullptr);
    ASSERT_TRUE(clip->IsValid());
    {
        NS::Audio::SoundScene sounds;
        NS::MusicDirector music;
        // 番号は 1 から振られる。曲だけを鳴らすので、最初の番号が曲
        const NS::Audio::SoundHandle first{1};

        // 聞こえない音量で鳴らす
        ASSERT_TRUE(music.Start(&sounds, clip, 0.0f));
        sounds.Update();
        EXPECT_TRUE(sounds.Exists(first));

        music.Stop(&sounds);
        EXPECT_FALSE(sounds.Exists(first));
    }
    device.Suspend();
    assets.Clear();
}
