#include "NSlib/Audio/AudioDevice.h"
#include "NSlib/Core/Windows/FileSystem.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Sound.h"
#include "NSlib/CoreObject/System/MusicDirector.h"

#include <gtest/gtest.h>

// 始めた曲は Stop まで始め直さない。止めた後は、次の Start でまた始める
TEST(MusicDirector, StartIsAcceptedOnceUntilStop)
{
    NS::MusicDirector music;
    EXPECT_FALSE(music.IsStarted());

    EXPECT_TRUE(music.Start(nullptr, nullptr, 0.5f));
    EXPECT_TRUE(music.IsStarted());
    EXPECT_FALSE(music.Start(nullptr, nullptr, 0.5f));

    music.Stop();
    EXPECT_FALSE(music.IsStarted());
    EXPECT_TRUE(music.Start(nullptr, nullptr, 0.5f));
}

// 装置の無いシーン (当たりの下見) と音を出せない装置でも、始める・進める・止めるを同じに呼べる
TEST(MusicDirector, StartsAndUpdatesWithOrWithoutADevice)
{
    NS::Audio::AudioDevice device(false);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    assets.SetAudioDevice(&device);
    const NS::Sound* clip = assets.LoadSound("Assets/Sounds/bgm_course.wav");
    ASSERT_NE(clip, nullptr);
    NS::MusicDirector music;

    EXPECT_TRUE(music.Start(nullptr, clip, 0.5f));
    music.Update();
    music.Stop();

    EXPECT_TRUE(music.Start(&device, clip, 0.5f));
    music.Update();
    music.Stop();
    EXPECT_FALSE(music.IsStarted());
}
