#include "Game/Level/SlamArrow.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/Actors/MapPartsActor.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Reflection/AssetRef.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/SubObjects/Animation.h"
#include "NSlib/CoreObject/SubObjects/MeshCollision.h"
#include "NSlib/CoreObject/SubObjects/Model.h"
#include "NSlib/CoreObject/SubObjects/Shadow.h"
#include "NSlib/Graphics/GraphicObject.h"
#include "NSlib/Graphics/D3DMaterial.h"
#include "NSlib/Windows/Filesystem.h"
#include "TestAnimatedPlayerActor.h"
#include "TestLogCapture.h"

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

namespace
{
    // 偽の素材の読み方。名前ごとの置き場を指す。欄の読み方は欄の型から多重定義で選ばれる
    void LoadAssetRef(NS::AssetRef<FakeAsset>& ref)
    {
        ref.SetLoaded(&FakeAssets()[ref.Name()]);
    }
} // namespace

namespace
{
    class AssetRefProbePart final : public NS::Object
    {
    public:
        NS::AssetRef<FakeAsset> asset{"sphere"};
        NS_REFLECT_BEGIN(AssetRefProbePart, NS::Object)
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

// Model と Shadow の素材は参照の欄で持つ。コードが決めていた素材も、欄の既定の名前になる
TEST(AssetRef, ModelAndShadowHoldTheirAssetsInRefFields)
{
    for (const char* key : {"メッシュ", "マテリアル", "残像のマテリアル"})
    {
        const NS::FieldDesc* field = NS::FindField(NS::Model::StaticReflection(), key);
        ASSERT_NE(field, nullptr) << key;
        EXPECT_EQ(field->type, NS::FieldType::AssetRef) << key;
    }
    for (const char* key : {"メッシュ", "マテリアル"})
    {
        const NS::FieldDesc* field = NS::FindField(NS::Shadow::StaticReflection(), key);
        ASSERT_NE(field, nullptr) << key;
        EXPECT_EQ(field->type, NS::FieldType::AssetRef) << key;
    }

    const NS::Model model{nullptr};
    EXPECT_EQ(NS::SerializeSubObjectFields(model)["残像のマテリアル"], nlohmann::json("water"));
    const NS::Shadow shadow{nullptr};
    const nlohmann::json shadowFields = NS::SerializeSubObjectFields(shadow);
    EXPECT_EQ(shadowFields["メッシュ"], nlohmann::json("shadowQuad"));
    EXPECT_EQ(shadowFields["マテリアル"], nlohmann::json("shadow"));
}

// 読めない名前の欄も、その名前を持つ中身の空の素材を指すので、保存し直しても名前が残る
TEST(AssetField, UnreadableNamesStayWhenSavedAgain)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::AssetManagerScope useAssets{assets};
    const nlohmann::json written{{"メッシュ", "Assets/Models/__ns_missing_mesh__.glb"},
                                 {"マテリアル", "Assets/Materials/__ns_missing__.mat"},
                                 {"残像のマテリアル", "../__ns_outside__.mat"}};
    NS::Model model{nullptr};
    EXPECT_EQ(NS::ApplyJsonFields(model, written), 0u); // 受ける欄の無い鍵の数

    const nlohmann::json saved = NS::SerializeSubObjectFields(model);
    EXPECT_EQ(saved["メッシュ"], written["メッシュ"]);
    EXPECT_EQ(saved["マテリアル"], written["マテリアル"]);
    EXPECT_EQ(saved["残像のマテリアル"], written["残像のマテリアル"]);
    EXPECT_TRUE(assets.LoadMaterial("Assets/Materials/__ns_missing__.mat")->IsEmpty());
}

// 欄の名前から素材を引く決まりは AssetManager の口の中にある
// 当たりの口は装置が無くても形を作れるので、名前の決まりをここで確かめる
TEST(AssetManagerLoad, MeshShapeFollowsTheNameRules)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::Phys::MeshShape* cube = assets.LoadMeshShape("cube");
    ASSERT_NE(cube, nullptr);
    // 組み込みの形は、登録の前でもファイルとして読まずに形を作る
    const NS::Phys::MeshShape* quad = assets.LoadMeshShape("shadowQuad");
    ASSERT_NE(quad, nullptr);
    EXPECT_NE(quad, cube);
    // 選んでいない・読めないファイル・基準のフォルダの外は、描く物と同じく cube で代える
    EXPECT_EQ(assets.LoadMeshShape(""), cube);
    EXPECT_EQ(assets.LoadMeshShape("Assets/Models/__ns_missing_shape__.glb"), cube);
    EXPECT_EQ(assets.LoadMeshShape("../__ns_outside__.glb"), cube);
}

// Renderer の無い場で読んだマテリアルは、シェーダとテクスチャも含めて空で、ERROR を出さない
// ゲームでは Renderer が先にあるので、この道を通らない
TEST(AssetManagerLoad, MaterialWithoutARendererIsEmptyAndLogsNoError)
{
    ASSERT_EQ(NS::Gfx::Gpu().device, nullptr);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const TestLogCapture log;
    const NS::Material* material = assets.LoadMaterial("Assets/Materials/stone.mat");
    ASSERT_FALSE(material->IsEmpty());
    EXPECT_FALSE(material->D3D()->IsValid());
    EXPECT_EQ(log.ErrorCount(), 0);
}

// 当たりは Model のメッシュの名前から作るので、メッシュを替えて読み直すと当たりも替わる
TEST(AssetRef, ReloadingAnotherMeshChangesTheCollisionShape)
{
    NS::Scene scene;
    NS::MapPartsActor* parts = scene.SpawnTransient<NS::MapPartsActor>();
    ASSERT_NE(parts, nullptr);
    const NS::MeshCollision* collision = NS::Cast<NS::MeshCollision>(parts->CollisionSubObj());
    ASSERT_NE(collision, nullptr);
    NS::AssetManager& assets = NS::AssetManager::Get();
    EXPECT_EQ(collision->Shape(), assets.LoadMeshShape(parts->ModelSubObj()->MeshRef()));

    parts->ModelSubObj()->SetMeshRef("sphere");
    parts->ReloadAssets();
    EXPECT_EQ(collision->Shape(), assets.LoadMeshShape("sphere"));
    EXPECT_NE(collision->Shape(), assets.LoadMeshShape("cube"));
}

// モデルの欄を空にして読み直すと、前に使っていた骨格を残さない
TEST(AssetRef, EmptyModelLeavesNoSkeleton)
{
    const NS::FieldDesc* field = NS::FindField(NS::Animation::StaticReflection(), "モデル");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type, NS::FieldType::AssetRef);

    const std::unique_ptr<PlayerActor> player = NS::CreateActor<TestAnimatedPlayerActor>();
    NS::Animation* animation = player->AnimationSubObj();
    const NS::Anim::Skeleton skeleton;
    animation->SetSkeleton(&skeleton);
    player->ReloadAssets();
    EXPECT_EQ(animation->Skeleton(), nullptr);
}

// 叩きつけの矢印と自機の見た目も、素材を参照の欄で持つ。自機の姿は組み込みの形を読む
TEST(AssetRef, SlamArrowAndPlayerLooksHoldTheirAssetsInRefFields)
{
    for (const char* key : {"板の資産", "帯の材質の資産", "矢じりの材質の資産"})
    {
        const NS::FieldDesc* field = NS::FindField(GL::Level::SlamArrow::StaticReflection(), key);
        ASSERT_NE(field, nullptr) << key;
        EXPECT_EQ(field->type, NS::FieldType::AssetRef) << key;
    }
    for (const char* key : {"立ち姿のメッシュ", "玉のメッシュ"})
    {
        const NS::FieldDesc* field = NS::FindField(GL::Player::PlayerAppearance::StaticReflection(), key);
        ASSERT_NE(field, nullptr) << key;
        EXPECT_EQ(field->type, NS::FieldType::AssetRef) << key;
    }

    NS::AssetManager& assets = NS::AssetManager::Get();
    EXPECT_NE(assets.LoadMeshShape("capsule"), assets.LoadMeshShape("cube"));
}

// 自機の姿は、姿ごとの倍率を Model の局所の倍率へ書いて大きさを合わせる
TEST(AssetRef, PlayerLookWritesItsScaleToTheModel)
{
    NS::Scene scene;
    PlayerActor* player = scene.SpawnTransient<PlayerActor>();
    ASSERT_NE(player, nullptr);
    GL::Player::PlayerAppearance* appearance = &player->Appearance();
    const NS::Model* model = player->ModelSubObj();
    const nlohmann::json fields = NS::SerializeSubObjectFields(*appearance);
    const nlohmann::json standing = fields["立ち姿の倍率"];
    const nlohmann::json ball = fields["玉の倍率"];
    ASSERT_TRUE(standing.is_array());
    ASSERT_TRUE(ball.is_array());

    EXPECT_FLOAT_EQ(model->LocalScale().y, standing[1].get<float>());
    appearance->Curl();
    EXPECT_FLOAT_EQ(model->LocalScale().y, ball[1].get<float>());
    appearance->Uncurl();
    EXPECT_FLOAT_EQ(model->LocalScale().y, standing[1].get<float>());
}
