#include <gtest/gtest.h>

#include <Framework/Scene/SceneSubsystem.h>
#include <Framework/Scene/SubsystemRegistry.h>

#include <memory>
#include <string_view>

namespace
{
    // 登録機構だけを検証するテスト専用の service。中身は空
    class DummySubsystem : public NS::Scene::SceneSubsystem
    {};

    [[nodiscard]] const NS::Scene::SubsystemEntry* FindEntry(std::string_view name)
    {
        for (const NS::Scene::SubsystemEntry& entry : NS::Scene::SubsystemRegistry::Get().Entries())
        {
            if (name == entry.name)
                return &entry;
        }
        return nullptr;
    }
} // namespace

NS_REGISTER_SUBSYSTEM(DummySubsystem, NS::Scene::SubsystemTier::Scene, [](const NS::Scene::SceneBase&) { return true; })

TEST(SubsystemRegistryTest, RegistersViaMacro)
{
    EXPECT_NE(FindEntry("DummySubsystem"), nullptr);
}

TEST(SubsystemRegistryTest, FactoryProducesInstance)
{
    const NS::Scene::SubsystemEntry* entry = FindEntry("DummySubsystem");
    ASSERT_NE(entry, nullptr);
    ASSERT_NE(entry->factory, nullptr);
    std::unique_ptr<NS::Scene::SceneSubsystem> instance = entry->factory();
    EXPECT_NE(instance, nullptr);
}

TEST(SubsystemRegistryTest, TierAndShouldCreateStored)
{
    const NS::Scene::SubsystemEntry* entry = FindEntry("DummySubsystem");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->tier, NS::Scene::SubsystemTier::Scene);
    ASSERT_NE(entry->shouldCreate, nullptr);
}
