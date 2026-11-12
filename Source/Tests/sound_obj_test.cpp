#include "NSlib/Audio/AudioDevice.h"
#include "NSlib/Audio/SoundScene.h"
#include "NSlib/Audio/XA2Sound.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/Asset/Sound.h"
#include "NSlib/CoreObject/ClassObjs/SoundObj.h"
#include "NSlib/CoreObject/IGet/IGetAudio.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "TestSpawn.h"

#include <Audio.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{
    //! 音の部品を持つだけのアクター
    class TestSoundActor : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(TestSoundActor, NS::SceneActor)

    protected:
        void OnInit() override { CreateClassObj(m_sound, this); }
    };

    //! 部品を持たず、受けた鳴らす先だけを答える IGetAudio。SoundObj を単体で試す時に渡す
    class TestAudioUser final : public NS::IGetAudio
    {
    public:
        //! @param[in] sounds 答える鳴らす先。nullptr は音を出さない世界
        explicit TestAudioUser(NS::Audio::SoundScene* sounds = nullptr) noexcept : m_sounds(sounds) {}

        [[nodiscard]] NS::SoundObj* GetSound() const noexcept override { return nullptr; }
        [[nodiscard]] NS::Audio::SoundScene* GetSoundScene() const noexcept override { return m_sounds; }

    private:
        NS::Audio::SoundScene* m_sounds = nullptr;
    };

    //! 音の機器がある PC でだけ動くテストの土台。機器が無ければ SetUp で飛ばす
    class SoundObjDeviceTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            if (!m_device.IsValid() || !m_device.Engine()->IsAudioDevicePresent())
            {
                GTEST_SKIP() << "音の機器が無い";
            }
            using NS::OS::FileSystem;
            const std::string path = FileSystem::Combine(FileSystem::ContentRoot(), "Assets/Sounds/jump.wav");
            m_clip = std::make_unique<NS::Sound>("Assets/Sounds/jump.wav",
                                                 std::make_unique<NS::Audio::XA2Sound>(&m_device, path));
            ASSERT_TRUE(m_clip->IsValid());
        }

        // 鳴っている音のデータを捨てる前に、音の処理を止める
        void TearDown() override { m_device.Suspend(); }

        NS::Audio::AudioDevice m_device{true};
        std::unique_ptr<NS::Sound> m_clip;
    };
} // namespace

NS_CLASS(TestSoundActor)

// 鳴らした音は、終わるまで番号つきで一覧に残る。音を出さない世界では鳴らさずに残す
TEST(SoundObjTest, PlayKeepsARecordUntilStop)
{
    const NS::Sound clip{"Assets/Sounds/jump.wav", nullptr};
    const TestAudioUser user;
    NS::SoundObj sound{&user};

    const NS::uint32 first = sound.Play(&clip, 0.5f, true);
    const NS::uint32 second = sound.Play(&clip, 0.25f, true);

    EXPECT_NE(first, 0u);
    EXPECT_NE(second, first);
    ASSERT_EQ(sound.Records().size(), 2u);
    EXPECT_EQ(sound.Records()[0].asset, &clip);
    EXPECT_FALSE(sound.Records()[0].handle.IsValid());
    EXPECT_TRUE(sound.Records()[0].loop);
    EXPECT_FLOAT_EQ(sound.Records()[1].volume, 0.25f);

    sound.SetVolume(second, 0.75f);
    EXPECT_FLOAT_EQ(sound.Records()[1].volume, 0.75f);

    sound.Stop(first);
    ASSERT_EQ(sound.Records().size(), 1u);
    EXPECT_EQ(sound.Records()[0].id, second);
    // 無い番号を止めても、残りは変わらない
    sound.Stop(first);
    EXPECT_EQ(sound.Records().size(), 1u);
}

// 位置から鳴らした音も、位置なしの音と同じに一覧に残り、同じ番号の列から番号を振る
TEST(SoundObjTest, PlayAtKeepsARecordLikePlay)
{
    const NS::Sound clip{"Assets/Sounds/jump.wav", nullptr};
    NS::Audio::SoundScene sounds;
    const TestAudioUser user{&sounds};
    NS::SoundObj sound{&user};

    const NS::uint32 first = sound.Play(&clip, 0.5f, true);
    const NS::uint32 second = sound.PlayAt(&clip, NS::Vector3{1.0f, 2.0f, 3.0f}, 0.25f, true);

    EXPECT_NE(second, 0u);
    EXPECT_NE(second, first);
    ASSERT_EQ(sound.Records().size(), 2u);
    EXPECT_FLOAT_EQ(sound.Records()[1].volume, 0.25f);
    EXPECT_TRUE(sound.Records()[1].loop);

    // 音を出さない世界でも、位置から鳴らした音は番号を受けて一覧に残る
    const TestAudioUser silentUser;
    NS::SoundObj silentSound{&silentUser};
    EXPECT_NE(silentSound.PlayAt(&clip, NS::Vector3{1.0f, 2.0f, 3.0f}, 0.25f, true), 0u);
    EXPECT_EQ(silentSound.Records().size(), 1u);
}

TEST(SoundObjTest, NullAndPathlessSoundsAreNotKept)
{
    const NS::Sound pathless{"", nullptr};
    const TestAudioUser user;
    NS::SoundObj sound{&user};

    EXPECT_EQ(sound.Play(nullptr, 1.0f, true), 0u);
    EXPECT_EQ(sound.Play(&pathless, 1.0f, false), 0u);
    EXPECT_EQ(sound.PlayAt(nullptr, NS::Vector3{}, 1.0f, true), 0u);
    EXPECT_EQ(sound.PlayAt(&pathless, NS::Vector3{}, 1.0f, false), 0u);
    EXPECT_TRUE(sound.Records().empty());
}

// 音を出さない世界では鳴り終わったかを問えないので、1 回の音は次の Update で外れ、ループは残る
TEST(SoundObjTest, WithoutASoundSceneOneShotsAreDroppedAndLoopsStay)
{
    const NS::Sound clip{"Assets/Sounds/jump.wav", nullptr};
    const TestAudioUser user;
    NS::SoundObj sound{&user};
    const NS::uint32 loop = sound.Play(&clip, 0.5f, true);
    ASSERT_NE(sound.Play(&clip, 0.5f, false), 0u);
    ASSERT_EQ(sound.Records().size(), 2u);

    sound.Update();

    ASSERT_EQ(sound.Records().size(), 1u);
    EXPECT_EQ(sound.Records()[0].id, loop);
}

// 音の無い素材は SoundScene に残らないので、1 回の音は次の Update で外れ、ループは Stop まで残る
TEST(SoundObjTest, WithASoundSceneSilentOneShotsAreDroppedAndLoopsStay)
{
    const NS::Sound clip{"Assets/Sounds/jump.wav", nullptr};
    NS::Audio::SoundScene sounds;
    const TestAudioUser user{&sounds};
    NS::SoundObj sound{&user};
    const NS::uint32 loop = sound.Play(&clip, 0.5f, true);
    ASSERT_NE(sound.Play(&clip, 0.5f, false), 0u);

    sound.Update();

    ASSERT_EQ(sound.Records().size(), 1u);
    EXPECT_EQ(sound.Records()[0].id, loop);
}

// 鳴っている 1 回の音は Update で外れず、Stop で SoundScene からも外れる。音は聞こえない音量で鳴らす
TEST_F(SoundObjDeviceTest, PlayingOneShotStaysUntilItEndsOrIsStopped)
{
    NS::Audio::SoundScene sounds;
    const TestAudioUser user{&sounds};
    NS::SoundObj sound{&user};
    const NS::uint32 oneShot = sound.Play(m_clip.get(), 0.0f, false);
    ASSERT_EQ(sound.Records().size(), 1u);
    const NS::Audio::SoundHandle handle = sound.Records()[0].handle;
    ASSERT_TRUE(handle.IsValid());

    sound.Update();
    EXPECT_EQ(sound.Records().size(), 1u);

    sound.Stop(oneShot);
    EXPECT_TRUE(sound.Records().empty());
    EXPECT_FALSE(sounds.Exists(handle));
}

// 世界から外れるアクターは、鳴らし続けていた音を基底が止め、1 回の音は SoundScene に残して最後まで鳴らす
TEST_F(SoundObjDeviceTest, DisappearingActorStopsItsLoopsAndLeavesOneShots)
{
    NS::Scene scene;
    scene.SetAudioDevice(&m_device);
    TestSoundActor* spawned = SpawnFromActorFile<TestSoundActor>(scene, "TestSoundActor");
    ASSERT_NE(spawned, nullptr);
    TestSoundActor& actor = *spawned;
    ASSERT_NE(actor.GetSoundScene(), nullptr);
    ASSERT_NE(NS::PlaySound(&actor, m_clip.get(), 0.0f, true), 0u);
    ASSERT_NE(NS::PlaySound(&actor, m_clip.get(), 0.0f), 0u);
    ASSERT_EQ(actor.GetSound()->Records().size(), 2u);
    const NS::Audio::SoundHandle loop = actor.GetSound()->Records()[0].handle;
    const NS::Audio::SoundHandle oneShot = actor.GetSound()->Records()[1].handle;

    actor.Disappear();

    EXPECT_TRUE(actor.GetSound()->Records().empty());
    EXPECT_FALSE(scene.GetSoundScene()->Exists(loop));
    EXPECT_TRUE(scene.GetSoundScene()->Exists(oneShot));
    scene.GetSoundScene()->StopAll();
}

// 音の装置を受け取っていないシーンのアクターも、鳴らした音を一覧に残し、外れる時に空にする
TEST(SoundObjTest, ActorInASceneWithoutADeviceKeepsRecords)
{
    const NS::Sound clip{"Assets/Sounds/jump.wav", nullptr};
    NS::Scene scene;
    TestSoundActor* spawned = SpawnFromActorFile<TestSoundActor>(scene, "TestSoundActor");
    ASSERT_NE(spawned, nullptr);
    TestSoundActor& actor = *spawned;
    ASSERT_EQ(actor.GetSoundScene(), nullptr);
    ASSERT_NE(NS::PlaySound(&actor, &clip, 0.5f, true), 0u);
    ASSERT_NE(NS::PlaySound(&actor, &clip, 0.5f, true), 0u);
    actor.Update();
    ASSERT_EQ(actor.GetSound()->Records().size(), 2u);

    actor.Disappear();

    EXPECT_TRUE(actor.GetSound()->Records().empty());
}

// 音の装置が替わると GetSoundScene が nullptr を返し、番号を持つ側が止められなくなるので、シーンが全部止める
TEST(SoundObjTest, ChangingTheDeviceOfASceneStopsEverySound)
{
    NS::Audio::AudioDevice device(false);
    const NS::Audio::XA2Sound clip(nullptr, "");
    NS::Scene scene;
    scene.SetAudioDevice(&device);
    NS::Audio::SoundScene* sounds = scene.GetSoundScene();
    ASSERT_NE(sounds, nullptr);
    const NS::Audio::SoundHandle loop = sounds->Play(&clip, 0.5f, true);
    ASSERT_TRUE(sounds->Exists(loop));

    scene.SetAudioDevice(&device);
    EXPECT_TRUE(sounds->Exists(loop));

    scene.SetAudioDevice(nullptr);
    EXPECT_FALSE(sounds->Exists(loop));
}

// 部品を作っていないアクターでは、関数は何もせず 0 を返す
TEST(SoundObjTest, ActorWithoutThePartPlaysNothing)
{
    const NS::Sound clip{"Assets/Sounds/jump.wav", nullptr};
    NS::Scene scene;
    TestEmptyActor* actor = SpawnFromActorFile<TestEmptyActor>(scene, "TestEmptyActor");
    ASSERT_NE(actor, nullptr);
    ASSERT_EQ(actor->GetSound(), nullptr);

    EXPECT_EQ(NS::PlaySound(actor, &clip, 0.5f, true), 0u);
    EXPECT_EQ(NS::PlaySound(actor, &clip, 0.5f), 0u);
    EXPECT_EQ(NS::PlaySoundAt(actor, &clip, NS::Vector3{}, 0.5f), 0u);
    NS::StopSound(actor, 1u);
    NS::SetSoundVolume(actor, 1u, 0.5f);
}
