#include "Game/PlayerActor.h"
#include "Editor/EditorActors.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "Tests/TestWarpGpu.h"

#include <gtest/gtest.h>

TEST(EffectAssetMigration, PlayerDoesNotOwnTheObsoleteChargeRuntime)
{
    const nlohmann::json actor = NS::Editor::MakePrototypeJson<PlayerActor>();
    const nlohmann::json& parts = NS::ActorJsonClassObjs(actor);
    EXPECT_FALSE(parts.contains("ChargeEffects"));
}

TEST(EffectNaming, ResourceAndClassObjKeepDifferentReflectionIdentities)
{
    NS::Effect asset("Assets/Effects/hit_flash_min.efkefc");
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
    EXPECT_EQ(NS::FindField(NS::EffectObj::StaticReflection(), "演出セット"), nullptr);
}

TEST(EffectAssetMigration, ShippedEffectsLoadThroughAssetManager)
{
    const ScopedWarpGpu gpu;
    ASSERT_TRUE(gpu.Valid());
    const std::string contentRoot = NS::OS::FileSystem::ContentRoot();
    const std::string effectsDir = NS::OS::FileSystem::Combine(contentRoot, "Assets/Effects");
    const std::vector<std::string> effects = NS::OS::FileSystem::ListFilesRecursive(effectsDir, ".efkefc");
    ASSERT_FALSE(effects.empty());
    NS::AssetManager assets{contentRoot};
    for (const std::string& fullPath : effects)
    {
        const std::string relative = NS::OS::FileSystem::RelativeUnder(contentRoot, fullPath).value_or("");
        const NS::Effect* loaded = assets.LoadEffect(relative);
        ASSERT_NE(loaded, nullptr) << relative;
        EXPECT_TRUE(loaded->IsValid()) << relative;
    }
}
