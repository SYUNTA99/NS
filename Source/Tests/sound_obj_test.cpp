#include "NSlib/Audio/XA2Sound.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/Sound.h"
#include "NSlib/CoreObject/ClassObjs/SoundObj.h"
#include "NSlib/CoreObject/IGet/IGetAudio.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "TestSpawn.h"

#include <gtest/gtest.h>

namespace
{
    //! 音の部品を持つだけのアクター
    class TestSoundActor : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(TestSoundActor, NS::SceneActor)

    protected:
        void OnInit() override { CreateClassObj(m_sound); }
    };
} // namespace

NS_CLASS(TestSoundActor)

// 鳴らし続ける音は、止めるまで番号つきで一覧に残る。装置の無い世界ではソースボイスを作らずに残す
TEST(SoundObjTest, PlayLoopKeepsARecordUntilStop)
{
    const NS::Sound clip{"Assets/Sounds/jump.wav", nullptr};
    NS::SoundObj sound;

    const NS::uint32 first = sound.PlayLoop(nullptr, &clip, 0.5f);
    const NS::uint32 second = sound.PlayLoop(nullptr, &clip, 0.25f);

    EXPECT_NE(first, 0u);
    EXPECT_NE(second, first);
    ASSERT_EQ(sound.Records().size(), 2u);
    EXPECT_EQ(sound.Records()[0].asset, &clip);
    EXPECT_EQ(sound.Records()[0].loop, nullptr);
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

TEST(SoundObjTest, NullAndPathlessSoundsAreNotKept)
{
    const NS::Sound pathless{"", nullptr};
    NS::SoundObj sound;

    EXPECT_EQ(sound.PlayLoop(nullptr, nullptr, 1.0f), 0u);
    EXPECT_EQ(sound.PlayLoop(nullptr, &pathless, 1.0f), 0u);
    EXPECT_TRUE(sound.Records().empty());
}

// 世界から外れるアクターは、鳴らし続けていた音を基底が全部止める
TEST(SoundObjTest, DisappearingActorStopsEveryLoopItPlayed)
{
    const NS::Sound clip{"Assets/Sounds/jump.wav", nullptr};
    NS::Scene scene;
    TestSoundActor* spawned = SpawnFromActorFile<TestSoundActor>(scene, "TestSoundActor");
    ASSERT_NE(spawned, nullptr);
    TestSoundActor& actor = *spawned;
    ASSERT_NE(NS::PlayLoopSound(&actor, &clip, 0.5f), 0u);
    ASSERT_NE(NS::PlayLoopSound(&actor, &clip, 0.5f), 0u);
    actor.Update();
    ASSERT_EQ(actor.GetSound()->Records().size(), 2u);

    actor.Disappear();

    EXPECT_TRUE(actor.GetSound()->Records().empty());
}

// 部品を作っていないアクターでは、関数は何もせず 0 を返す
TEST(SoundObjTest, ActorWithoutThePartPlaysNothing)
{
    const NS::Sound clip{"Assets/Sounds/jump.wav", nullptr};
    NS::Scene scene;
    TestEmptyActor* actor = SpawnFromActorFile<TestEmptyActor>(scene, "TestEmptyActor");
    ASSERT_NE(actor, nullptr);
    ASSERT_EQ(actor->GetSound(), nullptr);

    EXPECT_EQ(NS::PlayLoopSound(actor, &clip, 0.5f), 0u);
    NS::StopSound(actor, 1u);
    NS::SetSoundVolume(actor, 1u, 0.5f);
}
