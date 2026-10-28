#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/EffectAsset.h"
#include "NSlib/CoreObject/Asset/EffectCatalog.h"
#include "NSlib/CoreObject/Asset/EffectSet.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <span>

namespace
{
    constexpr const char* k_AssetId = "11111111111111111111111111111111";
    constexpr const char* k_SetId = "22222222222222222222222222222222";

    class EffectSettingsProbe : public NS::Object
    {
    public:
        float amount = 1.0f;
        NS::EffectAsset* effect = nullptr;
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
            Write("Assets/Effects/sample.effectset",
                  nlohmann::json{{"id", k_SetId},
                                 {"label", "演出"},
                                 {"settings", {{"EffectSettingsProbe", {{"量", 2.0f}, {"素材", k_AssetId}}}}}}
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
    NS::EffectAsset* original = assets.LoadEffectAsset(k_AssetId);
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
    NS::EffectAsset* missing = settings.effect;
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

TEST_F(EffectAssets, DefinitionReloadKeepsSharedSettingsAtTheSameAddress)
{
    NS::AssetManager assets{root.generic_string()};
    NS::AssetManagerScope scope{assets};
    NS::EffectSet* set = assets.LoadEffectSet(k_SetId);
    ASSERT_TRUE(set->IsValid());
    const EffectSettingsProbe& settings = set->Settings<EffectSettingsProbe>();
    EXPECT_EQ(settings.amount, 2.0f);
    ASSERT_NE(settings.effect, nullptr);
    EXPECT_EQ(settings.effect, assets.LoadEffectAsset(k_AssetId));
    nlohmann::json updated = set->Document();
    updated["settings"]["EffectSettingsProbe"]["量"] = 4.0f;
    updated["settings"]["EffectSettingsProbe"]["未知の値"] = 8;
    Write("Assets/Effects/sample.effectset", updated.dump());
    assets.ReloadEffectAssets();
    EXPECT_EQ(&settings, &set->Settings<EffectSettingsProbe>());
    EXPECT_EQ(settings.amount, 4.0f);
    EXPECT_EQ(set->Document()["settings"]["EffectSettingsProbe"]["未知の値"], 8);
}

TEST_F(EffectAssets, DuplicateGetsANewIdentityAndKeepsTheOriginal)
{
    NS::AssetManager assets{root.generic_string()};
    const std::optional<std::string> copied = assets.DuplicateEffectFile("Assets/Effects/sample.effect", "copy");
    ASSERT_TRUE(copied.has_value());
    NS::Object* object = assets.LoadEffectFile(*copied);
    const NS::EffectAsset* copy = NS::Cast<NS::EffectAsset>(object);
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

TEST_F(EffectAssets, FailedSaveStaysPendingAndCanBeRetried)
{
    NS::AssetManager assets{root.generic_string()};
    NS::EffectSet* set = assets.LoadEffectSet(k_SetId);
    const nlohmann::json document = set->Document();
    Write("Assets/Effects/blocked", "file");
    ASSERT_TRUE(set->Replace("Assets/Effects/blocked/sample.effectset", document));
    EXPECT_FALSE(assets.SaveEffectSet(*set));
    EXPECT_TRUE(assets.HasUnsavedEffectSets());
    ASSERT_TRUE(set->Replace("Assets/Effects/sample.effectset", document));
    EXPECT_TRUE(assets.SaveAllEffectSets());
    EXPECT_FALSE(assets.HasUnsavedEffectSets());
}

TEST_F(EffectAssets, NewlyImportedFilesLoadWithoutReplacingEditedSettings)
{
    NS::AssetManager assets{root.generic_string()};
    const NS::AssetManagerScope useAssets{assets};
    NS::EffectSet* original = assets.LoadEffectSet(k_SetId);
    const EffectSettingsProbe& settings = original->Settings<EffectSettingsProbe>();
    ASSERT_EQ(NS::ApplyJsonFields(original->Sections().at("EffectSettingsProbe").get(), {{"量", 9.0f}}), 0u);
    const char* newId = "44444444444444444444444444444444";
    Write("Assets/Effects/imported.effect",
          nlohmann::json{{"id", newId}, {"label", "追加"}, {"source", "Assets/Effects/sample.efkefc"}, {"seed", 7}}
              .dump());
    EXPECT_TRUE(assets.LoadEffectAsset(newId)->IsValid());
    EXPECT_NE(assets.LoadEffectFile("Assets/Effects/imported.effect"), nullptr);
    nlohmann::json document = original->Document();
    document["id"] = "55555555555555555555555555555555";
    Write("Assets/Effects/imported.effectset", document.dump());
    EXPECT_NE(assets.LoadEffectFile("Assets/Effects/imported.effectset"), nullptr);
    EXPECT_TRUE(assets.LoadEffectSet("55555555555555555555555555555555")->IsValid());
    EXPECT_EQ(settings.amount, 9.0f);
}

TEST_F(EffectAssets, ReloadRemovesOldValuesWhenAFieldOrDefinitionDisappears)
{
    NS::AssetManager assets{root.generic_string()};
    const NS::AssetManagerScope useAssets{assets};
    NS::EffectSet* set = assets.LoadEffectSet(k_SetId);
    const EffectSettingsProbe& settings = set->Settings<EffectSettingsProbe>();
    ASSERT_EQ(settings.amount, 2.0f);
    nlohmann::json document = set->Document();
    document["settings"]["EffectSettingsProbe"] = nlohmann::json::object();
    Write("Assets/Effects/sample.effectset", document.dump());
    assets.ReloadEffectAssets();
    EXPECT_EQ(settings.amount, 1.0f);
    EXPECT_EQ(NS::SerializeClassObjFields(&settings)["素材"], "");
    ASSERT_EQ(NS::ApplyJsonFields(set->Sections().at("EffectSettingsProbe").get(), {{"量", 5.0f}}), 0u);
    ASSERT_TRUE(NS::OS::FileSystem::RemoveFile((root / "Assets/Effects/sample.effectset").generic_string()));
    assets.ReloadEffectAssets();
    EXPECT_FALSE(set->IsValid());
    EXPECT_EQ(settings.amount, 1.0f);
    EXPECT_EQ(NS::SerializeClassObjFields(&settings)["素材"], "");
}
