#include "NSlib/CoreObject/Reflection/AssetRef.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObject.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

namespace
{
    struct FakeAsset
    {
    };

    // 偽の素材の置き場。名前ごとに 1 つ
    std::map<std::string, FakeAsset>& FakeAssets()
    {
        static std::map<std::string, FakeAsset> assets;
        return assets;
    }
} // namespace

// 偽の素材の読み方。名前ごとの置き場を指す
template <> void NS::LoadAssetRef<FakeAsset>(AssetRef<FakeAsset>& ref)
{
    ref.SetLoaded(&FakeAssets()[ref.Name()]);
}

namespace
{
    class AssetRefProbePart final : public NS::SubObject
    {
    public:
        NS::AssetRef<FakeAsset> asset{"sphere"};
        NS_REFLECT_BEGIN(AssetRefProbePart, NS::SubObject)
        NS_REFLECT_FIELD(asset, "素材")
        NS_REFLECT_END()
    };

    class AssetRefProbeActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(AssetRefProbeActor, NS::SceneActor)
        NS_REFLECT_SUBOBJ(part, "Part")
        NS_REFLECT_END()

        std::unique_ptr<AssetRefProbePart> part;
        int loadedCalls = 0;
        const FakeAsset* loadedAtCall = nullptr;

    protected:
        void OnInit() override { CreateSubObj(part); }

        void OnAssetsLoaded() override
        {
            NS::SceneActor::OnAssetsLoaded();
            ++loadedCalls;
            loadedAtCall = part->asset.Get();
        }
    };
} // namespace

// 作る時は、欄を書いた後にアクターの参照を読み、続けて OnAssetsLoaded を呼ぶ
TEST(AssetRef, ActorLoadsItsRefsWhenCreated)
{
    NS::Scene scene;
    const AssetRefProbeActor* actor = scene.SpawnTransient<AssetRefProbeActor>();
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->part->asset.Get(), &FakeAssets()["sphere"]);
    EXPECT_EQ(actor->loadedCalls, 1);
    EXPECT_EQ(actor->loadedAtCall, &FakeAssets()["sphere"]);
}

// 欄を書き換えた後の読み直しは、作る時と同じ組を通る
TEST(AssetRef, ReloadAssetsReadsTheNewName)
{
    NS::Scene scene;
    AssetRefProbeActor* actor = scene.SpawnTransient<AssetRefProbeActor>();
    ASSERT_NE(actor, nullptr);
    (void)NS::ApplyJsonFields(*actor->part, nlohmann::json{{"素材", "cube"}});
    ASSERT_EQ(actor->part->asset.Get(), nullptr);

    actor->ReloadAssets();
    EXPECT_EQ(actor->part->asset.Get(), &FakeAssets()["cube"]);
    EXPECT_EQ(actor->loadedCalls, 2);
    EXPECT_EQ(actor->loadedAtCall, &FakeAssets()["cube"]);
}

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
