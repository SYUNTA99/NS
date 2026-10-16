#include "NSlib/CoreObject/Reflection/AssetRef.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/SubObject.h"

#include <gtest/gtest.h>

namespace
{
    struct FakeAsset
    {
    };

    class AssetRefProbePart final : public NS::SubObject
    {
    public:
        NS::AssetRef<FakeAsset> asset;
        NS_REFLECT_BEGIN(AssetRefProbePart, NS::SubObject)
        NS_REFLECT_FIELD(asset, "素材")
        NS_REFLECT_END()
    };
} // namespace

// 参照の欄の JSON の字面は名前の文字列のまま。今の保存データがそのまま読める
TEST(AssetRef, JsonIsTheNameString)
{
    AssetRefProbePart part;
    part.asset.SetName("sphere");
    const nlohmann::json saved = NS::SerializeSubObjectFields(part);
    EXPECT_EQ(saved["素材"], nlohmann::json("sphere"));

    AssetRefProbePart loaded;
    EXPECT_EQ(NS::ApplyJsonFields(loaded, saved), 0u); // 読み手のいない欄の数
    EXPECT_EQ(loaded.asset.Name(), "sphere");
}

// 欄を別の名前に書き換えると、前に読んだ物を指したままにしない
TEST(AssetRef, WritingAnotherNameDropsTheLoadedAsset)
{
    FakeAsset asset;
    AssetRefProbePart part;
    part.asset.SetName("sphere");
    part.asset.SetLoaded(&asset);

    (void)NS::ApplyJsonFields(part, nlohmann::json{{"素材", "sphere"}});
    EXPECT_EQ(part.asset.Get(), &asset);

    (void)NS::ApplyJsonFields(part, nlohmann::json{{"素材", "cube"}});
    EXPECT_EQ(part.asset.Name(), "cube");
    EXPECT_EQ(part.asset.Get(), nullptr);
}

TEST(AssetRef, FieldTypeIsAssetRef)
{
    const NS::FieldDesc* field = NS::FindField(AssetRefProbePart::StaticReflection(), "素材");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type, NS::FieldType::AssetRef);
}
