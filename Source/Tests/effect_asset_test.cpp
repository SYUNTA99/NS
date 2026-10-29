#include "Game/Player/ImpactEffects.h"
#include "Game/PlayerActor.h"
#include "Editor/EditorActors.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"

#include <gtest/gtest.h>

TEST(EffectAssetMigration, PlayerDoesNotOwnTheObsoleteChargeRuntime)
{
    const nlohmann::json actor = NS::Editor::MakePrototypeJson<PlayerActor>();
    const nlohmann::json& parts = NS::ActorJsonClassObjs(actor);
    EXPECT_FALSE(parts.contains("ChargeEffects"));
    EXPECT_TRUE(parts.contains("ImpactEffects"));
}

TEST(EffectNaming, ResourceAndClassObjKeepDifferentReflectionIdentities)
{
    NS::Effect asset("test-effect");
    NS::EffectObj owner;
    EXPECT_STREQ(asset.GetReflection()->typeName, "Effect");
    EXPECT_STREQ(owner.GetReflection()->typeName, "EffectObj");
    EXPECT_EQ(NS::Cast<NS::EffectObj>(&asset), nullptr);
    EXPECT_EQ(NS::Cast<NS::Effect>(&owner), nullptr);
}

TEST(EffectAssetMigration, PlayerEffectObjectsOnlyKeepPlaybackState)
{
    NS::EffectObj effect;
    EXPECT_TRUE(NS::SerializeClassObjFields(&effect).empty());
}

TEST(EffectAssetMigration, GamePartsReferenceEffectsDirectly)
{
    const NS::FieldDesc* field = NS::FindField(GL::Player::ImpactEffects::StaticReflection(), "核の資産");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type, NS::FieldType::Asset);
    EXPECT_EQ(field->assetKind, NS::AssetKind::Effect);
    EXPECT_EQ(NS::FindField(NS::EffectObj::StaticReflection(), "演出セット"), nullptr);
    EXPECT_EQ(NS::FindField(GL::Player::ImpactEffects::StaticReflection(), "核の直径の基準"), nullptr);
}

TEST(EffectAssetMigration, EveryShippedEffectHasAnAssetDefinition)
{
    const std::string root = NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets/Effects");
    const std::vector<std::string> effects = NS::OS::FileSystem::ListFilesRecursive(root, ".efkefc");
    const std::vector<std::string> assets = NS::OS::FileSystem::ListFilesRecursive(root, ".effect");
    ASSERT_FALSE(effects.empty());
    EXPECT_EQ(assets.size(), effects.size());
}
