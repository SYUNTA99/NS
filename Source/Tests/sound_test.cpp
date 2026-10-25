#include "NSlib/Audio/AudioDevice.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/Sound.h"
#include "NSlib/CoreObject/IUse/IUseAudio.h"
#include "NSlib/CoreObject/Reflection/ActorBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/SoundName.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>
#include <vector>

namespace
{
    struct SoundSettings
    {
        NS::SoundName sound{"player/jump"};
        NS_REFLECT_BEGIN(SoundSettings, void)
        NS_FIELD(sound, "音")
        NS_REFLECT_END_VALUE()
    };

    class TestAudioActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(TestAudioActor, NS::SceneActor)

    protected:
        void OnInit() override { CreateClassObj(m_sound); }
    };
    NS_CLASS(TestAudioActor)
} // namespace

// SoundName の欄は音の種類の欄で、保存と読み込みで名前の文字列を往復する
TEST(SoundNameTest, FieldIsASoundFieldSavedAsItsName)
{
    const NS::ReflectionInfo& info = *SoundSettings::StaticReflection();
    const NS::FieldDesc* field = NS::FindField(&info, "音");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->assetKind, NS::AssetKind::Sound);

    SoundSettings settings;
    EXPECT_EQ(NS::SerializeReflectedFields(&settings, info)["音"], nlohmann::json("player/jump"));
    EXPECT_EQ(NS::ApplyReflectedFields(&settings, info, nlohmann::json{{"音", "player/land"}}), 0u);
    EXPECT_EQ(settings.sound.name, "player/land");
}

// 部品 Sound を作っていないアクターでは、読むのも鳴らすのも何もせず、ログも出さない
TEST(SoundTest, ActorWithoutTheSlotDoesNothing)
{
    NS::SceneActor actor;
    ASSERT_EQ(actor.GetSound(), nullptr);
    const TestLogCapture log;

    NS::PreloadSounds(&actor, {NS::SoundName{"missing"}});
    NS::PlaySound(&actor, NS::SoundName{"missing"}, 1.0f);

    EXPECT_EQ(log.ErrorCount(), 0);
    EXPECT_EQ(log.WarningCount(), 0);
}

// 装置を持たないシーンでは鳴らさない。装置を受け取った後は、読んでいない名前を WARN で知らせる
TEST(SoundTest, SceneWithoutDeviceDoesNotPlay)
{
    NS::Scene scene;
    TestAudioActor* actor = scene.SpawnActor<TestAudioActor>("TestAudioActor");
    ASSERT_NE(actor, nullptr);
    ASSERT_NE(actor->GetSound(), nullptr);
    const TestLogCapture log;

    NS::PlaySound(actor, NS::SoundName{"unread"}, 1.0f);
    EXPECT_EQ(log.WarningCount(), 0);

    NS::Audio::AudioDevice device(false);
    scene.SetAudioDevice(&device);
    ASSERT_EQ(actor->GetAudioDevice(), &device);
    NS::PlaySound(actor, NS::SoundName{"unread"}, 1.0f);
    NS::PlaySound(actor, NS::SoundName{"unread"}, 1.0f);
    EXPECT_EQ(log.WarningCount(), 1);
}

// 先に読むのは AssetManager の装置で、シーンの装置を見ない。読めない音は ERROR を出し、鳴らす時は黙って鳴らさない
TEST(SoundTest, PreloadUsesTheAssetManagerDevice)
{
    NS::Audio::AudioDevice device(false);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    assets.SetAudioDevice(&device);
    const NS::AssetManagerScope scope{assets};
    NS::Scene scene;
    TestAudioActor* actor = scene.SpawnActor<TestAudioActor>("TestAudioActor");
    ASSERT_NE(actor, nullptr);
    ASSERT_EQ(actor->GetAudioDevice(), nullptr);
    const TestLogCapture log;

    NS::PreloadSounds(actor, {NS::SoundName{"missing"}, NS::SoundName{"missing"}});
    EXPECT_EQ(log.ErrorCount(), 1);

    scene.SetAudioDevice(&device);
    NS::PlaySound(actor, NS::SoundName{"missing"}, 1.0f);
    EXPECT_EQ(log.ErrorCount(), 1);
    EXPECT_EQ(log.WarningCount(), 0);
}

// 空の名前は音を使わない欄なので、読みも鳴らしもせず、ERROR も WARN も出さない
TEST(SoundTest, EmptyNameIsNotUsed)
{
    NS::Audio::AudioDevice device(false);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    assets.SetAudioDevice(&device);
    const NS::AssetManagerScope scope{assets};
    NS::Scene scene;
    scene.SetAudioDevice(&device);
    TestAudioActor* actor = scene.SpawnActor<TestAudioActor>("TestAudioActor");
    ASSERT_NE(actor, nullptr);
    const TestLogCapture log;

    NS::PreloadSounds(actor, {NS::SoundName{}});
    NS::PlaySound(actor, NS::SoundName{}, 1.0f);

    EXPECT_EQ(log.ErrorCount(), 0);
    EXPECT_EQ(log.WarningCount(), 0);
}

// 部品 Sound は保存のキー Sound で並び、保存と読み直しの後も持ち主に残る
TEST(SoundTest, SlotIsSavedUnderTheSoundKeyAndSurvivesReload)
{
    NS::Scene scene;
    TestAudioActor* actor = scene.SpawnActor<TestAudioActor>("TestAudioActor");
    ASSERT_NE(actor, nullptr);
    std::vector<std::string_view> keys;
    NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& field, NS::Object* part) {
        if (part == actor->GetSound())
        {
            keys.push_back(field.key);
        }
    });
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0], "Sound");

    const nlohmann::json saved = NS::ActorToJson(actor);
    EXPECT_TRUE(NS::ActorJsonClassObjs(saved).contains("Sound"));
    const std::unique_ptr<NS::SceneActor> loaded = NS::ActorFromJson(saved, scene);
    ASSERT_NE(loaded, nullptr);
    EXPECT_NE(loaded->GetSound(), nullptr);
}
