#include "NSlib/Core/Types.h"
#include "TestEffectFiles.h"

#include <gtest/gtest.h>

TEST(EffekseerPlayback, DynamicInputIsAppliedBeforeTheFirstGeneration)
{
    const Effekseer::ManagerRef manager = Effekseer::Manager::Create(128);
    const Effekseer::EffectRef effect = LoadEffectFile("Source/Tests/data/effects/stack_by_input.efkefc");
    ASSERT_NE(effect, nullptr);
    const Effekseer::Handle handle = manager->Play(effect, 0.0f, 0.0f, 0.0f);
    manager->SetDynamicInput(handle, 0, 3.0f);

    manager->Update(0.0f);

    EXPECT_EQ(manager->GetInstanceCount(handle), 4);
    manager->SetDynamicInput(handle, 0, 1.0f);
    manager->Update(0.0f);
    EXPECT_EQ(manager->GetInstanceCount(handle), 4);
}

TEST(EffekseerPlayback, PausingOneInstanceKeepsOtherInstancesAdvancingAndAllowsStopping)
{
    const Effekseer::ManagerRef manager = Effekseer::Manager::Create(128);
    const Effekseer::EffectRef effect = LoadEffectFile("Source/Tests/data/effects/stream_life10.efkefc");
    ASSERT_NE(effect, nullptr);
    const Effekseer::Handle paused = manager->Play(effect, 0.0f, 0.0f, 0.0f);
    const Effekseer::Handle running = manager->Play(effect, 0.0f, 0.0f, 0.0f);
    manager->Update(3.0f);
    const NS::int32 before = manager->GetInstanceCount(paused);
    ASSERT_GT(before, 1);
    manager->SetPaused(paused, true);

    for (int step = 0; step < 4; ++step)
    {
        manager->Update(1.0f);
    }

    EXPECT_EQ(manager->GetInstanceCount(paused), before);
    EXPECT_GT(manager->GetInstanceCount(running), before);
    manager->SetLocation(paused, 2.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(manager->GetLocation(paused).X, 2.0f);
    manager->StopEffect(paused);
    manager->Update(0.0f);
    EXPECT_FALSE(manager->Exists(paused));
    EXPECT_TRUE(manager->Exists(running));
}

TEST(EffekseerPlayback, SpeedIsPerInstanceAndZeroCanBeResumed)
{
    const Effekseer::ManagerRef manager = Effekseer::Manager::Create(128);
    const Effekseer::EffectRef effect = LoadEffectFile("Source/Tests/data/effects/mover_x.efkefc");
    ASSERT_NE(effect, nullptr);
    const Effekseer::Handle frozen = manager->Play(effect, 0.0f, 0.0f, 0.0f);
    const Effekseer::Handle fast = manager->Play(effect, 0.0f, 0.0f, 0.0f);
    const Effekseer::Handle normal = manager->Play(effect, 0.0f, 0.0f, 0.0f);
    manager->Update(0.0f);
    manager->SetSpeed(frozen, 0.0f);
    manager->SetSpeed(fast, 2.0f);

    for (int step = 0; step < 61; ++step)
    {
        manager->Update(1.0f);
    }

    EXPECT_FALSE(manager->Exists(fast));
    EXPECT_TRUE(manager->Exists(normal));
    EXPECT_TRUE(manager->Exists(frozen));
    manager->SetSpeed(frozen, 1.0f);
    for (int step = 0; step < 121; ++step)
    {
        manager->Update(1.0f);
    }
    EXPECT_FALSE(manager->Exists(frozen));
}
