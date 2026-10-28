#include "Game/Player/ChargeEffects.h"
#include "Game/Player/ImpactEffects.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/ClassObjs/Effect.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"

#include <gtest/gtest.h>

TEST(EffectAssetMigration, PlayerEffectObjectsOnlyKeepPlaybackState)
{
    GL::Player::ChargeEffects charge;
    GL::Player::ImpactEffects impact;
    EXPECT_TRUE(NS::SerializeClassObjFields(&charge).empty());
    EXPECT_TRUE(NS::SerializeClassObjFields(&impact).empty());
}

TEST(EffectAssetMigration, EffectSelectsAnIndependentDefinition)
{
    const NS::FieldDesc* field = NS::FindField(NS::Effect::StaticReflection(), "演出セット");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type, NS::FieldType::Asset);
}

TEST(EffectAssetMigration, EveryShippedEffectHasAnAssetDefinition)
{
    const std::string root = NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets/Effects");
    const std::vector<std::string> effects = NS::OS::FileSystem::ListFilesRecursive(root, ".efkefc");
    const std::vector<std::string> assets = NS::OS::FileSystem::ListFilesRecursive(root, ".effect");
    ASSERT_FALSE(effects.empty());
    EXPECT_EQ(assets.size(), effects.size());
}
