#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Effect.h"
#include "NSlib/CoreObject/Asset/EffectCatalog.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <span>

namespace
{
    constexpr const char* k_AssetId = "11111111111111111111111111111111";

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

    class EffectAssets : public testing::Test
    {
    protected:
        void SetUp() override
        {
            root = std::filesystem::absolute(std::filesystem::path{"build/effect-asset-tests"} /
                                             testing::UnitTest::GetInstance()->current_test_info()->name());
            Write("Assets/Effects/sample.efkefc", "sample");
            Write(
                "Assets/Effects/sample.effect",
                nlohmann::json{
                    {"id", k_AssetId}, {"label", "sample"}, {"source", "Assets/Effects/sample.efkefc"}, {"seed", 1234}}
                    .dump());

        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }

        void Write(const char* path, const std::string& text)
        {
            ASSERT_TRUE(NS::OS::FileSystem::WriteAllBytes(
                (root / path).generic_string(), std::as_bytes(std::span<const char>{text.data(), text.size()})));
        }

        std::filesystem::path root;
    };
} // namespace

TEST_F(EffectAssets, RenameAndLabelChangeKeepTheReferenceAndRandomBasis)
{
    NS::AssetManager assets{root.generic_string()};
    NS::Effect* original = assets.LoadEffectAsset(k_AssetId);
    ASSERT_TRUE(original->IsValid());
    const NS::uint32 seed = original->Resource().seed;
    ASSERT_TRUE(NS::OS::FileSystem::RenameFile((root / "Assets/Effects/sample.effect").generic_string(),
                                               (root / "Assets/Effects/renamed.effect").generic_string()));
    Write(
        "Assets/Effects/renamed.effect",
        nlohmann::json{{"id", k_AssetId}, {"label", "改名"}, {"source", "Assets/Effects/sample.efkefc"}, {"seed", seed}}
            .dump());
    assets.ReloadEffectAssets();
    EXPECT_EQ(assets.LoadEffectAsset(k_AssetId), original);
    EXPECT_EQ(original->Label(), "改名");
    EXPECT_EQ(original->Resource().seed, seed);
    NS::AssetManager reopened{root.generic_string()};
    EXPECT_EQ(reopened.LoadEffectAsset(k_AssetId)->Resource().seed, seed);
}

TEST_F(EffectAssets, DuplicateIdentityIsRejectedRatherThanChoosingAFile)
{
    const std::optional<std::string> text =
        NS::OS::FileSystem::ReadAllText((root / "Assets/Effects/sample.effect").generic_string());
    ASSERT_TRUE(text.has_value());
    Write("Assets/Effects/duplicate.effect", *text);
    NS::AssetManager assets{root.generic_string()};
    EXPECT_FALSE(assets.LoadEffectAsset(k_AssetId)->IsValid());
}

TEST_F(EffectAssets, MissingReferenceSurvivesSerializationAndRepair)
{
    NS::AssetManager assets{root.generic_string()};
    NS::AssetManagerScope scope{assets};
    EffectSettingsProbe settings;
    NS::ApplyJsonFields(&settings, {{"素材", "33333333333333333333333333333333"}});
    NS::Effect* missing = settings.effect;
    ASSERT_NE(missing, nullptr);
    EXPECT_FALSE(missing->IsValid());
    EXPECT_EQ(NS::SerializeClassObjFields(&settings)["素材"], "33333333333333333333333333333333");
    Write("Assets/Effects/repaired.effect",
          nlohmann::json{{"id", "33333333333333333333333333333333"},
                         {"label", "修復"},
                         {"source", "Assets/Effects/sample.efkefc"},
                         {"seed", 5}}
              .dump());
    assets.ReloadEffectAssets();
    EXPECT_EQ(settings.effect, missing);
    EXPECT_TRUE(missing->IsValid());
}

TEST_F(EffectAssets, DuplicateGetsANewIdentityAndKeepsTheOriginal)
{
    NS::AssetManager assets{root.generic_string()};
    const std::optional<std::string> copied = assets.DuplicateEffectFile("Assets/Effects/sample.effect", "copy");
    ASSERT_TRUE(copied.has_value());
    NS::Object* object = assets.LoadEffectFile(*copied);
    const NS::Effect* copy = NS::Cast<NS::Effect>(object);
    ASSERT_NE(copy, nullptr);
    EXPECT_TRUE(copy->IsValid());
    EXPECT_NE(copy->Id(), k_AssetId);
    EXPECT_EQ(copy->Resource().seed, assets.LoadEffectAsset(k_AssetId)->Resource().seed);
    ASSERT_NE(copy->Resource().path, assets.LoadEffectAsset(k_AssetId)->Resource().path);
    const std::optional<std::string> binary = NS::OS::FileSystem::ReadAllText(copy->Resource().path);
    ASSERT_TRUE(binary.has_value());
    EXPECT_EQ(*binary, "sample");
    EXPECT_FALSE(assets.DuplicateEffectFile("Assets/Effects/sample.effect", "copy").has_value());
}

TEST_F(EffectAssets, NewlyImportedFilesKeepExistingReferences)
{
    NS::AssetManager assets{root.generic_string()};
    const NS::AssetManagerScope useAssets{assets};
    EffectSettingsProbe settings;
    ASSERT_EQ(NS::ApplyJsonFields(&settings, {{"素材", k_AssetId}, {"量", 9.0f}}), 0u);
    const NS::Effect* original = settings.effect;
    const char* newId = "44444444444444444444444444444444";
    Write("Assets/Effects/imported.effect",
          nlohmann::json{{"id", newId}, {"label", "追加"}, {"source", "Assets/Effects/sample.efkefc"}, {"seed", 7}}.dump());
    EXPECT_TRUE(assets.LoadEffectAsset(newId)->IsValid());
    EXPECT_NE(assets.LoadEffectFile("Assets/Effects/imported.effect"), nullptr);
    EXPECT_EQ(settings.effect, original);
    EXPECT_EQ(settings.amount, 9.0f);
    EXPECT_TRUE(original->IsValid());
}
