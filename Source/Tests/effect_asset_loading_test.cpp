#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Effect.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/Gfx/D3DEffect.h"
#include "Tests/TestWarpGpu.h"

#include <gtest/gtest.h>

namespace
{
    class EffectSettingsProbe : public NS::Object
    {
    public:
        float amount = 1.0f;
        NS::Effect* effect = nullptr;
        NS_REFLECT_BEGIN(EffectSettingsProbe, NS::Object)
        NS_FIELD(amount, "量", NS::FieldRange::NonNegative())
        NS_FIELD(effect, "素材")
        NS_REFLECT_END()
    };
} // namespace

TEST(EffectAssets, LoadEffectReturnsTheSameAssetForNormalizedPaths)
{
    const ScopedWarpGpu gpu;
    ASSERT_TRUE(gpu.Valid());
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    NS::Effect* first = assets.LoadEffect("Assets/Effects/hit_flash_min.efkefc");
    NS::Effect* second = assets.LoadEffect("Assets\\Effects\\hit_flash_min.efkefc");
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_TRUE(first->IsValid());
    EXPECT_EQ(first->Path(), "Assets/Effects/hit_flash_min.efkefc");
    ASSERT_NE(first->D3D(), nullptr);
    EXPECT_EQ(first->D3D()->Seed(), NS::Gfx::D3DEffect::DefaultSeed("Assets/Effects/hit_flash_min.efkefc"));
}

TEST(EffectAssets, EmptyAndUnreadablePathsReturnInvalidAssetsThatPreserveTheirPath)
{
    const ScopedWarpGpu gpu;
    ASSERT_TRUE(gpu.Valid());
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    NS::Effect* empty = assets.LoadEffect("");
    ASSERT_NE(empty, nullptr);
    EXPECT_FALSE(empty->IsValid());
    EXPECT_EQ(empty->Path(), "");

    NS::Effect* missing = assets.LoadEffect("Assets/Effects/missing.efkefc");
    ASSERT_NE(missing, nullptr);
    EXPECT_FALSE(missing->IsValid());
    EXPECT_EQ(missing->Path(), "Assets/Effects/missing.efkefc");

    NS::Effect* outside = assets.LoadEffect("../outside.efkefc");
    ASSERT_NE(outside, nullptr);
    EXPECT_FALSE(outside->IsValid());
    EXPECT_EQ(outside->Path(), "../outside.efkefc");
}

TEST(EffectAssets, MissingReferenceSurvivesSerialization)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::AssetManagerScope scope{assets};
    EffectSettingsProbe settings;
    NS::ApplyJsonFields(&settings, {{"素材", "Assets/Effects/missing.efkefc"}, {"量", 9.0f}});
    NS::Effect* missing = settings.effect;
    ASSERT_NE(missing, nullptr);
    EXPECT_FALSE(missing->IsValid());
    EXPECT_EQ(NS::SerializeClassObjFields(&settings)["素材"], "Assets/Effects/missing.efkefc");
    EXPECT_EQ(settings.amount, 9.0f);
}
