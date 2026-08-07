#include "Editor/LevelFilePaths.h"
#include "Game/Level/BlockObject.h"
#include "Game/Level/FollowCameraObject.h"
#include "Game/Level/KillZoneComponent.h"
#include "Game/Player.h"
#include "Runtime/Core/Filesystem.h"
#include "Runtime/Object/Components/ThirdPersonFollowComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <cstring>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <vector>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Object;
namespace EditorNs = NS::Editor;

namespace
{
    // テストの吐くシーンは TestOutput/ 配下に隔離する。.gitignore はこの置き場だけを除外する
    std::optional<std::filesystem::path> TestScenePath(const std::string& name)
    {
        return EditorNs::BuildLevelPath("TestOutput/" + name);
    }
} // namespace

TEST(SaveLoadRoundTrip, SaveAndReloadSemanticEqual)
{
    auto path = TestScenePath("test_roundtrip");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{1.0f, 2.0f, 3.0f}, NS::Core::Quaternion{}));
    src.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    SceneNs::ObjectData rotated = LevelNs::MakeCellObject(1, 0, 1);
    SceneNs::SetObjectRotation(rotated,
                               NS::Core::Quaternion::CreateFromYawPitchRoll(NS::Core::k_Pi * 0.5f, 0.0f, 0.0f));
    src.objects.push_back(rotated);
    src.objects.push_back(LevelNs::MakeCellObject(2, 0, 0));
    // 編集中のレベルは読込採番か Command 採番で常に id を持つため、 基準 CRC も採番後から取る
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects.push_back(NS::Game::Level::MakeFollowCameraObject(src.objects[0].objectId));
    SceneNs::EnsureUniqueObjectIds(src);
    const auto crc0 = src.ComputeCrc32();

    ASSERT_TRUE(SceneNs::SaveSceneToJsonFile(src, *path));

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::LoadSceneFromJsonFile(dst, *path));
    EXPECT_EQ(dst.ComputeCrc32(), crc0);
}

TEST(SaveLoadRoundTrip, ObjectNameSurvivesJsonRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects[0].name = "足場A";
    const auto namedCrc = src.ComputeCrc32();

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, SceneNs::SerializeSceneToJson(src)));

    ASSERT_EQ(dst.objects.size(), 1u);
    EXPECT_EQ(dst.objects[0].name, "足場A");
    EXPECT_EQ(dst.ComputeCrc32(), namedCrc);

    // 名前は未保存検知に出す必要があるので、外したら CRC が動く
    src.objects[0].name.clear();
    EXPECT_NE(src.ComputeCrc32(), namedCrc);
}

TEST(SaveLoadRoundTrip, ObjectActiveSurvivesJsonRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    SceneNs::EnsureUniqueObjectIds(src);
    const auto enabledCrc = src.ComputeCrc32();

    src.objects[0].active = false;
    const auto disabledCrc = src.ComputeCrc32();
    // 有効かどうかは保存対象なので、切ったら未保存検知に出る
    EXPECT_NE(disabledCrc, enabledCrc);

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, SceneNs::SerializeSceneToJson(src)));

    ASSERT_EQ(dst.objects.size(), 1u);
    EXPECT_FALSE(dst.objects[0].active);
    EXPECT_EQ(dst.ComputeCrc32(), disabledCrc);
}

TEST(SaveLoadRoundTrip, ObjectOrderSurvivesJsonRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    src.objects.push_back(LevelNs::MakeCellObject(1, 0, 0));
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects[0].order = 1;
    src.objects[1].order = 0;
    const auto orderedCrc = src.ComputeCrc32();

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, SceneNs::SerializeSceneToJson(src)));

    ASSERT_EQ(dst.objects.size(), 2u);
    EXPECT_EQ(dst.objects[0].order, 1u);
    EXPECT_EQ(dst.objects[1].order, 0u);
    EXPECT_EQ(dst.ComputeCrc32(), orderedCrc);
}

TEST(SaveLoadRoundTrip, MissingActiveAndOrderReadAsDefaults)
{
    // 欄を持たない古いファイルが従来どおり読めること
    SceneNs::SceneData src;
    src.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    SceneNs::EnsureUniqueObjectIds(src);

    const std::string text = SceneNs::SerializeSceneToJson(src);
    const nlohmann::json json = nlohmann::json::parse(text);
    const nlohmann::json& first = json.at("objects").at(0);
    // 既定値は書かない。 欄が増えても古いファイルと byte 互換が保てる
    EXPECT_TRUE(first.find("active") == first.end());
    EXPECT_TRUE(first.find("order") == first.end());

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, text));
    ASSERT_EQ(dst.objects.size(), 1u);
    EXPECT_TRUE(dst.objects[0].active);
    EXPECT_EQ(dst.objects[0].order, 0u);
}

TEST(SaveLoadRoundTrip, ObjectParentSurvivesJsonRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    src.objects.push_back(LevelNs::MakeCellObject(1, 0, 0));
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects[1].parentId = src.objects[0].objectId;
    const auto parentedCrc = src.ComputeCrc32();

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, SceneNs::SerializeSceneToJson(src)));

    ASSERT_EQ(dst.objects.size(), 2u);
    EXPECT_EQ(dst.objects[0].parentId, SceneNs::k_NoObjectId);
    EXPECT_EQ(dst.objects[1].parentId, dst.objects[0].objectId);
    EXPECT_EQ(dst.ComputeCrc32(), parentedCrc);

    // 親子も未保存検知に出す
    src.objects[1].parentId = SceneNs::k_NoObjectId;
    EXPECT_NE(src.ComputeCrc32(), parentedCrc);
}

// 正準 JSON は object キーが辞書順・float が最短往復表現なので、 同一データの 2 回保存は
// バイト一致する。 これがレベル差分の決定性 (git diff の安定) を担保する
TEST(SaveLoadRoundTrip, TwoSavesAreByteIdentical)
{
    auto path1 = TestScenePath("test_byteid_a");
    auto path2 = TestScenePath("test_byteid_b");
    ASSERT_TRUE(path1);
    ASSERT_TRUE(path2);

    SceneNs::SceneData src;
    src.objects.push_back(LevelNs::MakeCellObject(5, 5, 5));

    ASSERT_TRUE(SceneNs::SaveSceneToJsonFile(src, *path1));
    ASSERT_TRUE(SceneNs::SaveSceneToJsonFile(src, *path2));

    auto b1 = NS::Core::FileSystem::ReadAllBytes(*path1);
    auto b2 = NS::Core::FileSystem::ReadAllBytes(*path2);
    ASSERT_TRUE(b1.has_value());
    ASSERT_TRUE(b2.has_value());
    ASSERT_EQ(b1->size(), b2->size());
    EXPECT_EQ(std::memcmp(b1->data(), b2->data(), b1->size()), 0);
}

TEST(SaveLoadRoundTrip, LoadCorruptedFileFallsBackToEmpty)
{
    auto path = TestScenePath("test_corrupted");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;
    src.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    ASSERT_TRUE(SceneNs::SaveSceneToJsonFile(src, *path));

    auto bytes = NS::Core::FileSystem::ReadAllBytes(*path);
    ASSERT_TRUE(bytes.has_value());
    ASSERT_GE(bytes->size(), 1u);
    // 先頭の '{' を壊すと JSON parse が失敗し、 load は false + 空 SceneData を返す
    (*bytes)[0] = std::byte{'X'};
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(*path, std::span<const std::byte>(*bytes)));

    SceneNs::SceneData dst;
    EXPECT_FALSE(SceneNs::LoadSceneFromJsonFile(dst, *path));
    EXPECT_TRUE(dst.objects.empty());
}

// object 数が上限を超えるレベルは保存段でクラッシュせず false を返す (メモリ枯渇による DoS の防御)
TEST(SaveLoadRoundTrip, RejectsOversizedObjectCount)
{
    auto path = TestScenePath("test_oversized");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData huge;
    huge.objects.resize(100'001); // 上限 100'000 を 1 件超過させる

    EXPECT_FALSE(SceneNs::SaveSceneToJsonFile(huge, *path));
}

// 型名 + リフレクションフィールド値 (全 5 種の値) を持つコンポ一覧が save→load で復元される
// (全コンポ一覧を持つ形式の往復) 並びは正準化 (名前昇順) されるため等価判定は CRC ではなく正準 JSON の一致で行う
TEST(SaveLoadRoundTrip, ComponentsRoundTrip)
{
    auto path = TestScenePath("test_components");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;
    SceneNs::ObjectData freeObject{};
    SceneNs::SetObjectPosition(freeObject, NS::Core::Vector3{1.5f, 0.0f, 0.0f});

    nlohmann::json comp = SceneNs::MakeComponentEntry("BoxColliderComponent");
    SceneNs::SetField(comp, "vHalf", NS::Core::Vector3{1.0f, 2.0f, 3.0f});
    SceneNs::SetField(comp, "iCount", 7);
    SceneNs::SetField(comp, "bOn", true);
    SceneNs::SetField(comp, "fSpeed", 1.5f);
    SceneNs::SetField(comp, "fWhole", 4.0f); // 整数値の float が int に化けないことを確かめる
    freeObject.components.push_back(std::move(comp));
    src.objects.push_back(std::move(freeObject));
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    // 正準 JSON 同士の比較なので、 読込側と同じく採番 + 追従カメラ済の状態に揃えてから保存する
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects.push_back(NS::Game::Level::MakeFollowCameraObject(src.objects[1].objectId));
    SceneNs::EnsureUniqueObjectIds(src);

    ASSERT_TRUE(SceneNs::SaveSceneToJsonFile(src, *path));

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::LoadSceneFromJsonFile(dst, *path));

    EXPECT_EQ(SceneNs::SerializeSceneToJson(dst), SceneNs::SerializeSceneToJson(src));

    ASSERT_EQ(dst.objects.size(), 3u);
    // freeObject は BoxCollider に加え transform を表す TransformComponent を持つ
    ASSERT_EQ(dst.objects[0].components.size(), 2u);
    const nlohmann::json* box = SceneNs::FindComponentEntry(dst.objects[0], "BoxColliderComponent");
    ASSERT_NE(box, nullptr);
    const nlohmann::json& fields = box->at("fields");
    ASSERT_EQ(fields.size(), 5u);

    EXPECT_FLOAT_EQ(SceneNs::FieldVector3(*box, "vHalf", {}).y, 2.0f);

    ASSERT_TRUE(fields.at("iCount").is_number_integer()); // int
    EXPECT_EQ(SceneNs::FieldInt(*box, "iCount", -1), 7);

    ASSERT_TRUE(fields.at("bOn").is_boolean());
    EXPECT_TRUE(fields.at("bOn").get<bool>());

    ASSERT_TRUE(fields.at("fSpeed").is_number_float()); // float
    EXPECT_FLOAT_EQ(SceneNs::FieldFloat(*box, "fSpeed", -1.0f), 1.5f);

    ASSERT_TRUE(fields.at("fWhole").is_number_float()); // 整数値でも float のまま
    EXPECT_FLOAT_EQ(SceneNs::FieldFloat(*box, "fWhole", -1.0f), 4.0f);
}

TEST(SaveLoadRoundTrip, BuildLevelPathRejectsTraversal)
{
    // path traversal が path 構築層で構造的に止まることを検証する
    EXPECT_FALSE(EditorNs::BuildLevelPath("../etc/passwd").has_value());
    EXPECT_FALSE(EditorNs::BuildLevelPath("..").has_value());
    EXPECT_FALSE(EditorNs::BuildLevelPath("a/../b").has_value());
    // サブフォルダ区切りは有効
    EXPECT_TRUE(EditorNs::BuildLevelPath("a/b").has_value());
}

// 統一配置物 (ObjectData) の transform と className が 保存・再読込の往復で完全復元できることを検証する
TEST(SaveLoadRoundTrip, ObjectsRoundTrip)
{
    auto path = TestScenePath("test_objects_roundtrip");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;

    SceneNs::ObjectData freeObject{};
    SceneNs::SetObjectPosition(freeObject, NS::Core::Vector3{1.5f, 2.25f, -3.75f});
    SceneNs::SetObjectRotation(freeObject, NS::Core::Quaternion{0.0f, 0.70710677f, 0.0f, 0.70710677f});
    SceneNs::SetObjectScale(freeObject, NS::Core::Vector3{2.0f, 0.5f, 1.0f});
    src.objects.push_back(freeObject);

    SceneNs::ObjectData gridObject{};
    // 読込は全 object に transform を保証するため、 基準 CRC を合わせるよう src 側にも 1 つ持たせる
    SceneNs::EnsureTransformComponent(gridObject);
    src.objects.push_back(gridObject);
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));

    // 編集中のレベルは常に採番済なので、 基準 CRC も採番 + 追従カメラ済から取る
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects.push_back(NS::Game::Level::MakeFollowCameraObject(src.objects[2].objectId));
    SceneNs::EnsureUniqueObjectIds(src);
    const auto crc0 = src.ComputeCrc32();
    ASSERT_TRUE(SceneNs::SaveSceneToJsonFile(src, *path));

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::LoadSceneFromJsonFile(dst, *path));
    EXPECT_EQ(dst.ComputeCrc32(), crc0);

    ASSERT_EQ(dst.objects.size(), 4u);

    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(dst.objects[0]).x, 1.5f);
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(dst.objects[0]).z, -3.75f);
    EXPECT_NEAR(SceneNs::ObjectRotation(dst.objects[0]).w, 0.70710677f, 1e-5f);
    EXPECT_FLOAT_EQ(SceneNs::ObjectScale(dst.objects[0]).x, 2.0f);

    // クラス名は書いた object だけ載って戻り、 素の object は空のまま
    EXPECT_EQ(dst.objects[0].className, "");
    EXPECT_EQ(dst.objects[2].className, "Player");
}

// 配置物の "基本色" リフレクション値が save→reload を往復で保持される
// 種別固定の色上書きが消え、 色は component 経由で永続することの担保
TEST(SaveLoadRoundTrip, BaseColorSurvivesRoundTrip)
{
    auto path = TestScenePath("test_basecolor");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;
    SceneNs::ObjectData solid = LevelNs::MakeCellObject(0, 0, 0);
    const NS::Core::Vector3 baseColor{0.2f, 0.6f, 0.9f};
    for (nlohmann::json& component : solid.components)
        if (SceneNs::HasField(component, "基本色"))
            SceneNs::SetField(component, "基本色", baseColor);
    src.objects.push_back(solid);
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));

    ASSERT_TRUE(SceneNs::SaveSceneToJsonFile(src, *path));
    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::LoadSceneFromJsonFile(dst, *path));

    // solid + player
    ASSERT_EQ(dst.objects.size(), 2u);
    bool found = false;
    for (const nlohmann::json& component : dst.objects[0].components)
    {
        if (!SceneNs::HasField(component, "基本色"))
            continue;
        const NS::Core::Vector3 v = SceneNs::FieldVector3(component, "基本色", {});
        EXPECT_FLOAT_EQ(v.x, 0.2f);
        EXPECT_FLOAT_EQ(v.y, 0.6f);
        EXPECT_FLOAT_EQ(v.z, 0.9f);
        found = true;
    }
    EXPECT_TRUE(found) << "Base Color が往復で消えた";
}

// プレイヤー実体の pose が save→load を往復で保持される。 読込は書いてある物だけを返す
TEST(SaveLoadRoundTrip, PlayerObjectRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{1.25f, 3.5f, -2.75f},
                                           NS::Core::Quaternion{0.0f, 0.70710677f, 0.0f, 0.70710677f}));
    SceneNs::EnsureUniqueObjectIds(src);

    const std::string json = SceneNs::SerializeSceneToJson(src);
    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, json));

    ASSERT_EQ(dst.objects.size(), 1u);
    const std::size_t playerIndex = FindPlayerObjectIndex(dst);
    ASSERT_NE(playerIndex, SceneNs::k_NoObjectIndex);
    const SceneNs::ObjectData& loaded = dst.objects[playerIndex];
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(loaded).x, 1.25f);
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(loaded).y, 3.5f);
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(loaded).z, -2.75f);
    EXPECT_NEAR(SceneNs::ObjectRotation(loaded).y, 0.70710677f, 1e-5f);
    EXPECT_NEAR(SceneNs::ObjectRotation(loaded).w, 0.70710677f, 1e-5f);
}

// 空のシーンには読込時の補完がプレイヤーと追従カメラを既定構成で合成し、 プレイ可能な最小構成を保証する
TEST(EnsurePlayableObjects, SynthesizesPlayerAndFollowCamera)
{
    SceneNs::SceneData level;
    EXPECT_TRUE(EnsurePlayerObject(level));
    EXPECT_TRUE(NS::Game::Level::EnsureFollowCameraObject(level, PlayerObjectId(level)));
    EXPECT_TRUE(LevelNs::EnsureKillZoneObject(level));

    const std::size_t playerIndex = FindPlayerObjectIndex(level);
    ASSERT_NE(playerIndex, SceneNs::k_NoObjectIndex);
    const SceneNs::ObjectData& player = level.objects[playerIndex];
    EXPECT_NE(player.objectId, 0u); // 合成後の一意化で永続 id も振られる
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(player).y, Player::k_DefaultSpawnY);
    // 既定構成 5 点。 mesh 描画 + 移動 + 入力 + 命 + 接地影
    EXPECT_NE(SceneNs::FindComponentEntry(player, "MeshRendererComponent"), nullptr);
    EXPECT_NE(SceneNs::FindComponentEntry(player, "CharacterMovementComponent"), nullptr);
    EXPECT_NE(SceneNs::FindComponentEntry(player, "PlayerInputComponent"), nullptr);
    EXPECT_NE(SceneNs::FindComponentEntry(player, "HealthComponent"), nullptr);
    EXPECT_NE(SceneNs::FindComponentEntry(player, "ShadowComponent"), nullptr);

    // 追従カメラも 1 台合成され、 追従対象は合成したプレイヤーを指す
    const std::size_t followIndex = NS::Game::Level::FindFollowCameraObjectIndex(level);
    ASSERT_NE(followIndex, SceneNs::k_NoObjectIndex);
    const nlohmann::json* comp = SceneNs::FindComponentEntry(level.objects[followIndex], "ThirdPersonFollowComponent");
    ASSERT_NE(comp, nullptr);
    ASSERT_TRUE(SceneNs::HasField(*comp, "追従対象"));
    EXPECT_EQ(SceneNs::FieldObjectRef(*comp, "追従対象").id, player.objectId);

    // 落下死体積も 1 つ敷かれる
    bool hasKillZone = false;
    for (const SceneNs::ObjectData& object : level.objects)
    {
        if (LevelNs::IsKillZoneObject(object))
            hasKillZone = true;
    }
    EXPECT_TRUE(hasKillZone);
}

// プレイヤーが複数居ても先頭を正とする。 手編集の重複でも読込と補完は成立する
TEST(SaveLoadRoundTrip, MultiplePlayersFirstWins)
{
    SceneNs::SceneData src;
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{1.0f, 0.0f, 0.0f}, NS::Core::Quaternion{}));
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{9.0f, 0.0f, 0.0f}, NS::Core::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(src);

    const std::string json = SceneNs::SerializeSceneToJson(src);
    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, json));

    ASSERT_EQ(dst.objects.size(), 2u);
    EXPECT_FALSE(EnsurePlayerObject(dst)); // プレイヤーが居るので合成しない
    const std::size_t playerIndex = FindPlayerObjectIndex(dst);
    ASSERT_EQ(playerIndex, 0u);
    EXPECT_FLOAT_EQ(SceneNs::ObjectPosition(dst.objects[playerIndex]).x, 1.0f);
}

// プレイヤーだけのシーンには読込時の補完がプレイヤーを追う 1 台を合成し、「必ず 1 台」を保証する
TEST(EnsurePlayableObjects, SynthesizesFollowCameraTargetingExistingPlayer)
{
    SceneNs::SceneData dst;
    dst.objects.push_back(MakePlayerObject(NS::Core::Vector3{2.0f, 1.41f, 0.0f}, NS::Core::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(dst);
    const std::uint32_t playerId = dst.objects[0].objectId;

    EXPECT_FALSE(EnsurePlayerObject(dst)); // プレイヤー自体は合成していない
    EXPECT_TRUE(NS::Game::Level::EnsureFollowCameraObject(dst, PlayerObjectId(dst)));

    const std::size_t followIndex = NS::Game::Level::FindFollowCameraObjectIndex(dst);
    ASSERT_NE(followIndex, SceneNs::k_NoObjectIndex);
    const SceneNs::ObjectData& follow = dst.objects[followIndex];
    EXPECT_NE(follow.objectId, 0u); // 合成後の一意化で永続 id も振られる

    const nlohmann::json* comp = SceneNs::FindComponentEntry(follow, "ThirdPersonFollowComponent");
    ASSERT_NE(comp, nullptr);
    // 追従先はプレイヤー実体への通常の ObjectRef
    ASSERT_TRUE(SceneNs::HasField(*comp, "追従対象"));
    EXPECT_EQ(SceneNs::FieldObjectRef(*comp, "追従対象").id, playerId);
    // データが持つのは誰を追うかだけ。 遠景を抑える投影値は component のコード既定を使う
    EXPECT_FALSE(SceneNs::HasField(*comp, "ファークリップ"));
    NS::Object::ThirdPersonFollowComponent live;
    EXPECT_FLOAT_EQ(live.FarPlane(), 100.0f);
}

// environment 欄が save→load で往復する。 照明は Component へ移り、 ここに残るのは skybox
TEST(SaveLoadRoundTrip, EnvironmentRoundTrip)
{
    SceneNs::SceneData src;
    src.environment.skyboxCubemapPath = "Assets/Skybox/kurt/";
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects.push_back(NS::Game::Level::MakeFollowCameraObject(src.objects[0].objectId));
    SceneNs::EnsureUniqueObjectIds(src);
    const auto crc0 = src.ComputeCrc32();

    const std::string json = SceneNs::SerializeSceneToJson(src);
    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, json));

    EXPECT_EQ(dst.environment.skyboxCubemapPath, "Assets/Skybox/kurt/");
    EXPECT_EQ(dst.ComputeCrc32(), crc0);
}

// 旧版のファイルは黙って既定値で読まず、 読込自体を拒否する。 欄名が違う旧データの静かな破壊を防ぐ
TEST(SaveLoadRoundTrip, RejectsOldFormatVersion)
{
    const std::string json = R"({ "version": 2, "objects": [] })";
    SceneNs::SceneData dst;
    EXPECT_FALSE(SceneNs::DeserializeSceneFromJson(dst, json));
}

// 旧形式の照明キーは照明が Component へ移った今、 読み飛ばされる。 skybox だけが environment から読める
TEST(SaveLoadRoundTrip, LegacyLightingKeysAreIgnored)
{
    const std::string json = R"({
        "version": 3,
        "objects": [],
        "environment": { "skybox": "Assets/Skybox/kurt/", "lightColor": [0.5, 0.6, 0.7] }
    })";

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, json));

    // skybox は読める。 旧照明キー lightColor は無視され、 読込は壊れない
    EXPECT_EQ(dst.environment.skyboxCubemapPath, "Assets/Skybox/kurt/");
}

// 追従カメラ実体が既に居れば合成は走らず、 Target 参照ごと往復で保持される
TEST(SaveLoadRoundTrip, FollowCameraObjectRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects.push_back(NS::Game::Level::MakeFollowCameraObject(src.objects[0].objectId));
    SceneNs::EnsureUniqueObjectIds(src);
    const auto crc0 = src.ComputeCrc32();

    const std::string json = SceneNs::SerializeSceneToJson(src);
    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, json));

    ASSERT_EQ(dst.objects.size(), 2u);
    EXPECT_EQ(dst.ComputeCrc32(), crc0);
    const std::size_t followIndex = NS::Game::Level::FindFollowCameraObjectIndex(dst);
    ASSERT_EQ(followIndex, 1u);
    const nlohmann::json* comp = SceneNs::FindComponentEntry(dst.objects[1], "ThirdPersonFollowComponent");
    ASSERT_NE(comp, nullptr);
    ASSERT_TRUE(SceneNs::HasField(*comp, "追従対象"));
    EXPECT_EQ(SceneNs::FieldObjectRef(*comp, "追従対象").id, dst.objects[0].objectId);
}
