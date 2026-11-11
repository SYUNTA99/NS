#include "NSlib/Audio/AudioDevice.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Sound.h"
#include "NSlib/CoreObject/IGet/IGetAudio.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

#include "TestLogCapture.h"
#include "TestSpawn.h"

#include <gtest/gtest.h>

namespace
{
    struct SoundSettings
    {
        NS::Sound* sound = NS::AssetManager::Get().LoadSound("Assets/Sounds/jump.wav");
        NS_REFLECT_BEGIN(SoundSettings, void)
        NS_FIELD(sound, "音")
        NS_REFLECT_END_VALUE()
    };
} // namespace

// Sound* の欄は音の種類の欄で、保存と読み込みでパスの文字列を往復する
TEST(SoundTest, FieldIsASoundFieldSavedAsItsPath)
{
    const NS::ReflectionInfo& info = *SoundSettings::StaticReflection();
    const NS::FieldDesc* field = NS::FindField(&info, "音");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->assetKind, NS::AssetKind::Sound);

    SoundSettings settings;
    EXPECT_EQ(NS::SerializeReflectedFields(&settings, info)["音"], nlohmann::json("Assets/Sounds/jump.wav"));
    EXPECT_EQ(NS::ApplyReflectedFields(&settings, info, nlohmann::json{{"音", "Assets/Sounds/land.wav"}}), 0u);
    ASSERT_NE(settings.sound, nullptr);
    EXPECT_EQ(settings.sound->Path(), "Assets/Sounds/land.wav");
}

// 装置を持たないシーンでは鳴らさず、ログも出さない
TEST(SoundTest, SceneWithoutDeviceDoesNotPlay)
{
    NS::Audio::AudioDevice device(false);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    assets.SetAudioDevice(&device);
    const NS::AssetManagerScope scope{assets};
    NS::Sound* clip = assets.LoadSound("Assets/Sounds/jump.wav");
    ASSERT_NE(clip, nullptr);

    NS::Scene scene;
    NS::SceneActor* actor = SpawnFromActorFile<TestEmptyActor>(scene, "TestEmptyActor");
    ASSERT_NE(actor, nullptr);
    ASSERT_EQ(actor->GetSoundScene(), nullptr);
    const TestLogCapture log;

    NS::PlaySound(actor, clip, 1.0f);
    EXPECT_EQ(log.ErrorCount(), 0);
    EXPECT_EQ(log.WarningCount(), 0);
}

// 読めない音は AssetManager が読む時に 1 回だけ ERROR を出し、PlaySound は黙って鳴らさない
TEST(SoundTest, UnreadableSoundLogsErrorOnceAtLoadAndPlaySoundIgnoresIt)
{
    NS::Audio::AudioDevice device(false);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    assets.SetAudioDevice(&device);
    const NS::AssetManagerScope scope{assets};
    NS::Scene scene;
    scene.SetAudioDevice(&device);
    NS::SceneActor* actor = SpawnFromActorFile<TestEmptyActor>(scene, "TestEmptyActor");
    ASSERT_NE(actor, nullptr);
    const TestLogCapture log;

    NS::Sound* missing = assets.LoadSound("Assets/Sounds/missing.wav");
    EXPECT_EQ(assets.LoadSound("Assets/Sounds/missing.wav"), missing);
    EXPECT_EQ(log.ErrorCount(), 1);

    NS::PlaySound(actor, missing, 1.0f);
    EXPECT_EQ(log.ErrorCount(), 1);
    EXPECT_EQ(log.WarningCount(), 0);
}

// 空のパスは音を使わない欄なので、読みも鳴らしもせず、ERROR も WARN も出さない
TEST(SoundTest, EmptyPathIsNotUsed)
{
    NS::Audio::AudioDevice device(false);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    assets.SetAudioDevice(&device);
    const NS::AssetManagerScope scope{assets};
    NS::Scene scene;
    scene.SetAudioDevice(&device);
    NS::SceneActor* actor = SpawnFromActorFile<TestEmptyActor>(scene, "TestEmptyActor");
    ASSERT_NE(actor, nullptr);
    const TestLogCapture log;

    NS::Sound* empty = assets.LoadSound("");
    NS::PlaySound(actor, empty, 1.0f);

    EXPECT_EQ(log.ErrorCount(), 0);
    EXPECT_EQ(log.WarningCount(), 0);
}
