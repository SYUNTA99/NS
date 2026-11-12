#include "Game/Player/PlayerAppearance.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/Action/ActorActionInfo.h"
#include "NSlib/CoreObject/Actors/MapPartsActor.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Texture.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/ClassObjs/MeshCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DMaterial.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/Render/BuiltinShaders.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "TestAnimatedPlayerActor.h"
#include "TestLogCapture.h"
#include "TestSpawn.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <iterator>
#include <memory>
#include <string>

namespace
{
    class AssetFieldProbePart final : public NS::Object
    {
    public:
        AssetFieldProbePart() { NS::LoadAssetField(asset, "sphere"); }

        NS::Mesh* asset = nullptr;
        NS_REFLECT_BEGIN(AssetFieldProbePart, NS::Object)
        NS_FIELD(asset, "素材")
        NS_REFLECT_END()
    };

    class AssetFieldProbeActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(AssetFieldProbeActor, NS::SceneActor)
        NS_CLASSOBJ(part, "Part")
        NS_REFLECT_END()

        std::unique_ptr<AssetFieldProbePart> part;
        int loadedCalls = 0;
        const NS::Mesh* loadedAtCall = nullptr;

    protected:
        void OnInit() override { CreateClassObj(part); }

        void OnAssetsLoaded() override
        {
            NS::SceneActor::OnAssetsLoaded();
            ++loadedCalls;
            loadedAtCall = part->asset;
        }
    };
} // namespace

NS_CLASS(AssetFieldProbeActor)

TEST(AssetField, ActorLoadsItsFieldsWhenCreated)
{
    NS::Scene scene;
    const AssetFieldProbeActor* actor = SpawnFromActorFile<AssetFieldProbeActor>(scene, "AssetFieldProbeActor");
    ASSERT_NE(actor, nullptr);
    const NS::Mesh* sphere = NS::AssetManager::Get().LoadMesh("sphere");
    EXPECT_EQ(actor->part->asset, sphere);
    EXPECT_EQ(actor->loadedCalls, 1);
    EXPECT_EQ(actor->loadedAtCall, sphere);
}

TEST(AssetField, ReloadAssetsReadsTheNewName)
{
    NS::Scene scene;
    AssetFieldProbeActor* actor = SpawnFromActorFile<AssetFieldProbeActor>(scene, "AssetFieldProbeActor");
    ASSERT_NE(actor, nullptr);
    const NS::Mesh* cube = NS::AssetManager::Get().LoadMesh("cube");
    (void)NS::ApplyJsonFields(actor->part.get(), nlohmann::json{{"素材", "cube"}});
    EXPECT_EQ(actor->part->asset, cube);

    actor->ReloadAssets();
    EXPECT_EQ(actor->loadedCalls, 2);
    EXPECT_EQ(actor->loadedAtCall, cube);
}

TEST(AssetField, JsonIsTheNameString)
{
    const AssetFieldProbePart part;
    const nlohmann::json saved = NS::SerializeClassObjFields(&part);
    EXPECT_EQ(saved["素材"], nlohmann::json("sphere"));

    AssetFieldProbePart loaded;
    NS::LoadAssetField(loaded.asset, "cube");
    EXPECT_EQ(NS::ApplyJsonFields(&loaded, saved), 0u);
    EXPECT_EQ(NS::AssetFieldName(loaded.asset), "sphere");
}

TEST(AssetField, FieldTypeIsAsset)
{
    const NS::FieldDesc* field = NS::FindField(AssetFieldProbePart::StaticReflection(), "素材");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type, NS::FieldType::Asset);
    EXPECT_EQ(field->assetKind, NS::AssetKind::Mesh);
}

TEST(AssetField, ModelAndShadowHoldTheirAssetsInAssetFields)
{
    for (const char* key : {"メッシュ", "マテリアル"})
    {
        const NS::FieldDesc* field = NS::FindField(NS::Model::StaticReflection(), key);
        ASSERT_NE(field, nullptr) << key;
        EXPECT_EQ(field->type, NS::FieldType::Asset) << key;
    }
    for (const char* key : {"メッシュ", "マテリアル"})
    {
        const NS::FieldDesc* field = NS::FindField(NS::Shadow::StaticReflection(), key);
        ASSERT_NE(field, nullptr) << key;
        EXPECT_EQ(field->type, NS::FieldType::Asset) << key;
    }

    const NS::Shadow shadow;
    const nlohmann::json shadowFields = NS::SerializeClassObjFields(&shadow);
    EXPECT_EQ(shadowFields["メッシュ"], nlohmann::json("shadowQuad"));
    EXPECT_EQ(shadowFields["マテリアル"], nlohmann::json("Assets/Materials/shadow.mat"));
}

TEST(AssetField, UnreadableNamesStayWhenSavedAgain)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::AssetManagerScope useAssets{assets};
    const TestLogCapture log;
    const nlohmann::json written{{"メッシュ", "Assets/Models/__ns_missing_mesh__.glb"},
                                 {"マテリアル", "Assets/Materials/__ns_missing__.mat"}};
    NS::Model model;
    EXPECT_EQ(NS::ApplyJsonFields(&model, written), 0u);
    const nlohmann::json outside{{"マテリアル", "../__ns_outside__.mat"}};
    NS::Shadow shadow;
    EXPECT_EQ(NS::ApplyJsonFields(&shadow, outside), 0u);

    const nlohmann::json saved = NS::SerializeClassObjFields(&model);
    EXPECT_EQ(saved["メッシュ"], written["メッシュ"]);
    EXPECT_EQ(saved["マテリアル"], written["マテリアル"]);
    EXPECT_EQ(NS::SerializeClassObjFields(&shadow)["マテリアル"], outside["マテリアル"]);
    EXPECT_TRUE(assets.LoadMaterial("Assets/Materials/__ns_missing__.mat")->IsEmpty());
    EXPECT_EQ(log.ErrorCount(), 5);
}

TEST(AssetField, UnreadableClipNamesStayWhenSavedAgain)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::AssetManagerScope useAssets{assets};
    const TestLogCapture log;

    // 動作のアニメーションの項目のクリップの欄は、読めない道を保ったまま .actor の形へ書く
    const char* const clipPaths[] = {
        "Assets/Models/__ns_missing_clip__.glb", "Assets/Models/Xbot.glb#__ns_missing__", "../__ns_outside__.glb"};
    NS::ActorActionInfo action;
    action.name = "Wait";
    for (const char* path : clipPaths)
    {
        NS::ActionAnimInfo item;
        NS::LoadAssetField(item.clip, path);
        action.events.push_back(NS::ActorActionEvent{.item = item, .start = 0, .length = 1});
    }
    const nlohmann::json saved = NS::ActorActionInfoToJson(action);
    ASSERT_EQ(saved["events"].size(), std::size(clipPaths));
    for (std::size_t i = 0; i < std::size(clipPaths); ++i)
    {
        EXPECT_EQ(saved["events"][i]["fields"]["クリップ"], nlohmann::json(clipPaths[i])) << i;
    }

    const NS::FieldDesc* clipField = NS::FindField(NS::ActionAnimInfo::StaticReflection(), "クリップ");
    ASSERT_NE(clipField, nullptr);
    EXPECT_EQ(clipField->type, NS::FieldType::Asset);
    EXPECT_EQ(clipField->assetKind, NS::AssetKind::Animation);
    EXPECT_EQ(log.ErrorCount(), 4);
}

// 欄の名前から素材を引く決まりは AssetManager の口の中にある
// 当たりの口は装置が無くても形を作れるので、名前の決まりをここで確かめる
TEST(AssetManagerLoad, MeshShapeFollowsTheNameRules)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const TestLogCapture log;
    const NS::Phys::MeshShape* cube = assets.LoadMeshShape("cube");
    ASSERT_NE(cube, nullptr);
    // 組み込みの形は、登録の前でもファイルとして読まずに形を作る
    const NS::Phys::MeshShape* quad = assets.LoadMeshShape("shadowQuad");
    ASSERT_NE(quad, nullptr);
    EXPECT_NE(quad, cube);
    EXPECT_EQ(log.ErrorCount(), 0);
    EXPECT_EQ(assets.LoadMeshShape(""), nullptr);
    EXPECT_EQ(assets.LoadMeshShape("Assets/Models/__ns_missing_shape__.glb"), nullptr);
    EXPECT_EQ(assets.LoadMeshShape("../__ns_outside__.glb"), nullptr);
    EXPECT_GE(log.ErrorCount(), 2);
}

// D3DDevice の無い場では、ファイルのメッシュも描く形を作らずに当たりだけを作り、ERROR を出さない
TEST(AssetManagerLoad, FileMeshWithoutADeviceHasOnlyCollisionAndLogsNoError)
{
    ASSERT_EQ(NS::Gfx::D3DDevice::Current(), nullptr);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const TestLogCapture log;
    const NS::Mesh* mesh = assets.LoadMesh("Assets/Models/GoalFlag.glb");
    EXPECT_EQ(mesh->D3D(), nullptr);
    EXPECT_NE(mesh->Collision(), nullptr);
    EXPECT_EQ(log.ErrorCount(), 0);
}

// D3DDevice の無い場で読んだマテリアルは、シェーダとテクスチャも含めて空で、ERROR を出さない
// ゲームでは D3DDevice が先にあるので、この道を通らない
TEST(AssetManagerLoad, MaterialWithoutADeviceIsEmptyAndLogsNoError)
{
    ASSERT_EQ(NS::Gfx::D3DDevice::Current(), nullptr);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const TestLogCapture log;
    const NS::Material* material = assets.LoadMaterial("Assets/Materials/stone.mat");
    ASSERT_FALSE(material->IsEmpty());
    EXPECT_FALSE(material->D3D()->IsValid());
    EXPECT_EQ(log.ErrorCount(), 0);
}

TEST(AssetManagerLoad, BuiltinShadersWithoutADeviceAreEmptyAndLogNoError)
{
    ASSERT_EQ(NS::Gfx::D3DDevice::Current(), nullptr);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const TestLogCapture log;
    const NS::Gfx::BuiltinShaders shaders = assets.LoadBuiltinShaders();
    ASSERT_NE(shaders.fadePs, nullptr);
    EXPECT_FALSE(shaders.fadePs->IsValid());
    EXPECT_EQ(log.ErrorCount(), 0);
}

TEST(AssetManagerLoad, TextureKeepsThePathItWasLoadedBy)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    NS::Texture* texture = assets.LoadTexture("Textures\\checker.png");
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->Path(), NS::OS::FileSystem::Normalize("Textures\\checker.png"));
    EXPECT_EQ(assets.LoadTexture(texture->Path()), texture);
}

TEST(AssetManagerLoad, UnreadableMeshDrawsNothingHasNoCollisionAndLogsOnce)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const TestLogCapture log;
    NS::Mesh* missing = assets.LoadMesh("Assets/Models/__ns_missing_mesh__.glb");
    const int errorsAfterFirst = log.ErrorCount();
    EXPECT_GE(errorsAfterFirst, 1);
    EXPECT_EQ(assets.LoadMesh("Assets\\Models\\__ns_missing_mesh__.glb"), missing);
    EXPECT_EQ(missing->D3D(), nullptr);
    EXPECT_EQ(missing->Collision(), nullptr);
    EXPECT_EQ(assets.LoadMeshShape("Assets/Models/__ns_missing_mesh__.glb"), nullptr);
    EXPECT_EQ(log.ErrorCount(), errorsAfterFirst);

    EXPECT_EQ(assets.LoadMeshShape(""), nullptr);
    EXPECT_EQ(log.ErrorCount(), errorsAfterFirst);
}

TEST(AssetManagerLoad, UnreadableMaterialDrawsTheMissingMaterialAndLogsOnce)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const TestLogCapture log;
    NS::Material* missing = assets.LoadMaterial("Assets/Materials/__ns_missing__.mat");
    const int errorsAfterFirst = log.ErrorCount();
    EXPECT_GE(errorsAfterFirst, 1);
    EXPECT_EQ(assets.LoadMaterial("Assets\\Materials\\__ns_missing__.mat"), missing);
    EXPECT_TRUE(missing->IsEmpty());
    EXPECT_FALSE(assets.MakeMaterialFileJson(*missing).has_value());

    const NS::Material* pink = assets.LoadMaterial("Assets/Materials/missing.mat");
    ASSERT_FALSE(pink->IsEmpty());
    EXPECT_EQ(missing->D3D(), pink->D3D());
    EXPECT_EQ(log.ErrorCount(), errorsAfterFirst);
}

TEST(AssetManagerLoad, PlayerAndShadowAreMaterialFilesThatCanBeWritten)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const TestLogCapture log;
    for (const char* path : {"Assets/Materials/player.mat", "Assets/Materials/shadow.mat"})
    {
        const NS::Material* material = assets.LoadMaterial(path);
        EXPECT_FALSE(material->IsEmpty()) << path;
        EXPECT_TRUE(assets.MakeMaterialFileJson(*material).has_value()) << path;
    }
    EXPECT_EQ(log.ErrorCount(), 0);
}

// マテリアルの欄が空の Model は、メッシュが glb でも組み込みの形でも既定のマテリアルで描き、ERROR を出さない
// 読めない名前の欄はピンクで描き、ERROR を出す。どちらも欄に保存する値は変えない
TEST(AssetField, ModelWithoutAMaterialDrawsGrayAndAnUnreadableMaterialDrawsPink)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::AssetManagerScope useAssets{assets};
    NS::Scene scene;
    NS::MapPartsActor* parts = SpawnFromActorFile<NS::MapPartsActor>(scene, "MapPartsActor");
    ASSERT_NE(parts, nullptr);
    NS::Model* model = parts->ModelClassObj();
    ASSERT_EQ(model->MaterialRef(), "");
    const NS::Gfx::D3DMaterial* gray = assets.LoadMaterial("Assets/Materials/default.mat")->D3D();
    const NS::Gfx::D3DMaterial* pink = assets.LoadMaterial("Assets/Materials/missing.mat")->D3D();
    ASSERT_NE(gray, nullptr);
    ASSERT_NE(pink, nullptr);
    ASSERT_NE(gray, pink);
    // 灰色はマテリアルの側で持つ。空の名前のマテリアルは中身が空のまま、既定のマテリアルを描く
    EXPECT_TRUE(assets.LoadMaterial("")->IsEmpty());
    EXPECT_EQ(assets.LoadMaterial("")->D3D(), gray);

    const TestLogCapture log;
    for (const char* mesh : {"cube", "Assets/Models/GoalFlag.glb"})
    {
        model->SetMeshRef(mesh);
        parts->ReloadAssets();
        EXPECT_EQ(model->MakeDrawItem(NS::Matrix::Identity).material, gray) << mesh;
        EXPECT_EQ(model->MaterialRef(), "") << mesh;
    }
    EXPECT_EQ(log.ErrorCount(), 0);
    // 基本の色も既定のマテリアルから読む。描く形だけを既定にすると、default.mat の色を変えても空の欄に届かない
    model->SetColorOverride(std::nullopt); // MapPartsActor.actor の基本色の上書きを外し、マテリアルの色を見る
    assets.LoadMaterial("Assets/Materials/default.mat")->SetBaseColor(NS::Color{NS::Vector3{0.25f, 0.5f, 0.75f}});
    EXPECT_FLOAT_EQ(model->MakeDrawItem(NS::Matrix::Identity).constants.baseColor.y, 0.5f);

    model->SetMaterialRef("Assets/Materials/__ns_missing__.mat");
    EXPECT_EQ(model->MakeDrawItem(NS::Matrix::Identity).material, pink);
    const int errors = log.ErrorCount();
    EXPECT_GT(errors, 0); // 数は Core のファイルの読み方にも寄るので、出た事だけを見る
    parts->ReloadAssets();
    EXPECT_EQ(model->MakeDrawItem(NS::Matrix::Identity).material, pink);
    EXPECT_EQ(log.ErrorCount(), errors); // 読めない名前の ERROR は名前ごとに 1 回で、読み直しても増えない
    EXPECT_EQ(model->MaterialRef(), "Assets/Materials/__ns_missing__.mat");
}

TEST(AssetField, ReloadingAnotherMeshChangesTheCollisionShape)
{
    NS::Scene scene;
    NS::MapPartsActor* parts = SpawnFromActorFile<NS::MapPartsActor>(scene, "MapPartsActor");
    ASSERT_NE(parts, nullptr);
    const NS::MeshCollision* collision = NS::Cast<NS::MeshCollision>(parts->CollisionClassObj());
    ASSERT_NE(collision, nullptr);
    NS::AssetManager& assets = NS::AssetManager::Get();
    EXPECT_EQ(collision->Shape(), assets.LoadMeshShape(parts->ModelClassObj()->MeshRef()));

    parts->ModelClassObj()->SetMeshRef("sphere");
    parts->ReloadAssets();
    EXPECT_EQ(collision->Shape(), assets.LoadMeshShape("sphere"));
    EXPECT_NE(collision->Shape(), assets.LoadMeshShape("cube"));
}

// Animation の骨格は Model のメッシュの欄の glb から読む。欄を空にして読み直すと骨格も空になる
TEST(AssetField, AnimationReadsTheSkeletonOfTheModelMesh)
{
    const ScopedActorFileEdit xbot("PlayerActor", [](nlohmann::json& actorFile) {
        actorFile["classObjs"]["Model"]["メッシュ"] = "Assets/Models/Xbot.glb";
    });
    NS::Scene scene;
    PlayerActor* player = SpawnTestAnimatedPlayer(scene);
    ASSERT_NE(player, nullptr);
    const NS::Mesh* xbotMesh = NS::AssetManager::Get().LoadMesh("Assets/Models/Xbot.glb");
    ASSERT_NE(xbotMesh->Skeleton(), nullptr);
    ASSERT_NE(player->AnimationClassObj()->Skeleton(), nullptr);
    EXPECT_EQ(player->AnimationClassObj()->Skeleton()->BoneCount(), xbotMesh->Skeleton()->BoneCount());

    player->ModelClassObj()->SetMeshRef("");
    player->ReloadAssets();
    EXPECT_EQ(player->AnimationClassObj()->Skeleton(), nullptr);
}

TEST(AssetField, PlayerLooksHoldTheirAssetsInAssetFields)
{
    for (const char* key : {"立ち姿のメッシュ", "玉のメッシュ"})
    {
        const NS::FieldDesc* field = NS::FindField(GL::Player::PlayerAppearance::StaticReflection(), key);
        ASSERT_NE(field, nullptr) << key;
        EXPECT_EQ(field->type, NS::FieldType::Asset) << key;
    }

    NS::AssetManager& assets = NS::AssetManager::Get();
    EXPECT_NE(assets.LoadMeshShape("capsule"), assets.LoadMeshShape("cube"));
}

TEST(AssetField, PlayerLookWritesItsScaleToTheModel)
{
    NS::Scene scene;
    PlayerActor* player = SpawnFromActorFile<PlayerActor>(scene, "PlayerActor");
    ASSERT_NE(player, nullptr);
    GL::Player::PlayerAppearance* appearance = &player->Appearance();
    const NS::Model* model = player->ModelClassObj();
    const nlohmann::json fields = NS::SerializeClassObjFields(appearance);
    const nlohmann::json standing = fields["立ち姿の倍率"];
    const nlohmann::json ball = fields["玉の倍率"];
    ASSERT_TRUE(standing.is_array());
    ASSERT_TRUE(ball.is_array());

    EXPECT_FLOAT_EQ(model->LocalScale().y, standing[1].get<float>());
    appearance->Curl(player->ModelClassObj());
    EXPECT_FLOAT_EQ(model->LocalScale().y, ball[1].get<float>());
    appearance->Uncurl(player->ModelClassObj());
    EXPECT_FLOAT_EQ(model->LocalScale().y, standing[1].get<float>());
}
