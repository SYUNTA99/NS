#include "Editor/LevelFilePaths.h"
#include "Framework/Core/Filesystem.h"
#include "Game/Level/LevelIO.h"
#include "Game/Level/LevelJson.h"
#include "Game/Level/LevelObjects.h"

#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Scene;
namespace EditorNs = NS::Editor;

TEST(SaveLoadRoundTrip, SaveAndReloadSemanticEqual)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path = EditorNs::BuildLevelPath("test_roundtrip");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;
    src.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{1.0f, 2.0f, 3.0f}, NS::Math::Quaternion{}));
    src.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    src.objects.push_back(LevelNs::MakeCellObject(1, 0, 1, 1));
    src.objects.push_back(LevelNs::MakeCellObject(2, 0, 0, 0));
    // 編集中のレベルは読込採番か Command 採番で常に id を持つため、 基準 CRC も採番後から取る
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects.push_back(LevelNs::MakeFollowCameraObject(src.objects[0].objectId));
    SceneNs::EnsureUniqueObjectIds(src);
    const auto crc0 = src.ComputeCrc32();

    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));

    SceneNs::SceneData dst;
    ASSERT_TRUE(LevelNs::LoadLevelFromFile(dst, *path));
    EXPECT_EQ(dst.ComputeCrc32(), crc0);
}

// 正準 JSON は object キーが辞書順・float が shortest round-trip なので、 同一データの 2 回保存は
// バイト一致する。 これがレベル差分の決定性 (git diff の安定) を担保する
TEST(SaveLoadRoundTrip, TwoSavesAreByteIdentical)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path1 = EditorNs::BuildLevelPath("test_byteid_a");
    auto path2 = EditorNs::BuildLevelPath("test_byteid_b");
    ASSERT_TRUE(path1);
    ASSERT_TRUE(path2);

    SceneNs::SceneData src;
    src.objects.push_back(LevelNs::MakeCellObject(5, 5, 5, 0));

    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path1));
    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path2));

    auto b1 = NS::Core::FileSystem::ReadAllBytes(*path1);
    auto b2 = NS::Core::FileSystem::ReadAllBytes(*path2);
    ASSERT_TRUE(b1.has_value());
    ASSERT_TRUE(b2.has_value());
    ASSERT_EQ(b1->size(), b2->size());
    EXPECT_EQ(std::memcmp(b1->data(), b2->data(), b1->size()), 0);
}

TEST(SaveLoadRoundTrip, LoadCorruptedFileFallsBackToEmpty)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path = EditorNs::BuildLevelPath("test_corrupted");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;
    src.objects.push_back(LevelNs::MakeCellObject(0, 0, 0, 0));
    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));

    auto bytes = NS::Core::FileSystem::ReadAllBytes(*path);
    ASSERT_TRUE(bytes.has_value());
    ASSERT_GE(bytes->size(), 1u);
    // 先頭の '{' を壊すと JSON parse が失敗し、 load は false + 空 SceneData を返す
    (*bytes)[0] = std::byte{'X'};
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(*path, std::span<const std::byte>(*bytes)));

    SceneNs::SceneData dst;
    EXPECT_FALSE(LevelNs::LoadLevelFromFile(dst, *path));
    EXPECT_TRUE(dst.objects.empty());
}

// object 数が上限を超えるレベルは保存段でクラッシュせず false を返す (memory exhaustion の DoS 防御)
TEST(SaveLoadRoundTrip, RejectsOversizedObjectCount)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path = EditorNs::BuildLevelPath("test_oversized");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData huge;
    huge.objects.resize(100'001); // 上限 100'000 を 1 件超過させる

    EXPECT_FALSE(LevelNs::SaveLevelToFile(huge, *path));
}

// material path が 1 件でも上限長を超えるレベルは保存段で false を返す
// load 側は同じ閾値で拒否するので、 往復不能になる前に保存時点で止める
TEST(SaveLoadRoundTrip, RejectsOversizedMaterialPathLength)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path = EditorNs::BuildLevelPath("test_oversized_material_path");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData level;
    level.materialPaths.push_back(std::string(1'025u, 'a')); // 上限 1'024 byte を 1 byte 超過

    EXPECT_FALSE(LevelNs::SaveLevelToFile(level, *path));
}

// 型名 + 反射フィールド値 (全 5 変種) を持つコンポ一覧が save→load で復元される (full SSOT の往復)
// フィールドは辞書順でない順に積み、 往復後も意味的に同一になることを確かめる。 並びは正準化 (名前昇順)
// されるため等価判定は CRC ではなく正準 JSON の一致で行う
TEST(SaveLoadRoundTrip, ComponentsRoundTrip)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path = EditorNs::BuildLevelPath("test_components");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;
    SceneNs::ObjectData freeObject{};
    freeObject.positionX = 1.5f;

    SceneNs::ComponentData comp;
    comp.typeName = "BoxColliderComponent";
    comp.fields.push_back(SceneNs::FieldValue{"vHalf", NS::Math::Vector3{1.0f, 2.0f, 3.0f}});
    comp.fields.push_back(SceneNs::FieldValue{"iCount", 7});
    comp.fields.push_back(SceneNs::FieldValue{"bOn", true});
    comp.fields.push_back(SceneNs::FieldValue{"fSpeed", 1.5f});
    comp.fields.push_back(SceneNs::FieldValue{"fWhole", 4.0f}); // 整数値の float が int に化けないことを確かめる
    freeObject.components.push_back(std::move(comp));
    src.objects.push_back(std::move(freeObject));
    src.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    // 正準 JSON 同士の比較なので、 読込側と同じく採番 + 追従カメラ済の状態に揃えてから保存する
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects.push_back(LevelNs::MakeFollowCameraObject(src.objects[1].objectId));
    SceneNs::EnsureUniqueObjectIds(src);

    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));

    SceneNs::SceneData dst;
    ASSERT_TRUE(LevelNs::LoadLevelFromFile(dst, *path));

    EXPECT_EQ(LevelNs::SerializeLevelToJson(dst), LevelNs::SerializeLevelToJson(src));

    ASSERT_EQ(dst.objects.size(), 3u);
    ASSERT_EQ(dst.objects[0].components.size(), 1u);
    const auto& fields = dst.objects[0].components[0].fields;
    EXPECT_EQ(dst.objects[0].components[0].typeName, "BoxColliderComponent");
    ASSERT_EQ(fields.size(), 5u);

    const auto find = [&fields](const char* name) -> const SceneNs::FieldValue* {
        for (const auto& f : fields)
            if (f.name == name)
                return &f;
        return nullptr;
    };

    const auto* vHalf = find("vHalf");
    ASSERT_NE(vHalf, nullptr);
    ASSERT_EQ(vHalf->value.index(), 3u);
    EXPECT_FLOAT_EQ(std::get<NS::Math::Vector3>(vHalf->value).y, 2.0f);

    const auto* iCount = find("iCount");
    ASSERT_NE(iCount, nullptr);
    ASSERT_EQ(iCount->value.index(), 1u); // int
    EXPECT_EQ(std::get<int>(iCount->value), 7);

    const auto* bOn = find("bOn");
    ASSERT_NE(bOn, nullptr);
    ASSERT_EQ(bOn->value.index(), 2u); // bool
    EXPECT_TRUE(std::get<bool>(bOn->value));

    const auto* fSpeed = find("fSpeed");
    ASSERT_NE(fSpeed, nullptr);
    ASSERT_EQ(fSpeed->value.index(), 0u); // float
    EXPECT_FLOAT_EQ(std::get<float>(fSpeed->value), 1.5f);

    const auto* fWhole = find("fWhole");
    ASSERT_NE(fWhole, nullptr);
    ASSERT_EQ(fWhole->value.index(), 0u); // 整数値でも float のまま
    EXPECT_FLOAT_EQ(std::get<float>(fWhole->value), 4.0f);
}

TEST(SaveLoadRoundTrip, BuildLevelPathRejectsTraversal)
{
    // path traversal が path 構築層で構造的に止まることを検証する
    EXPECT_FALSE(EditorNs::BuildLevelPath("../etc/passwd").has_value());
    EXPECT_FALSE(EditorNs::BuildLevelPath("..").has_value());
    EXPECT_FALSE(EditorNs::BuildLevelPath("a/b").has_value());
}

// 統一配置物 (ObjectData) と material 文字列表が round-trip で完全復元できることを検証する
TEST(SaveLoadRoundTrip, ObjectsAndMaterialsRoundTrip)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path = EditorNs::BuildLevelPath("test_objects_roundtrip");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;
    src.materialPaths.push_back("Assets/Materials/stone.mat");
    src.materialPaths.push_back("Assets/Materials/grid.mat");

    SceneNs::ObjectData freeObject{};
    freeObject.positionX = 1.5f;
    freeObject.positionY = 2.25f;
    freeObject.positionZ = -3.75f;
    freeObject.rotationY = 0.70710677f;
    freeObject.rotationW = 0.70710677f;
    freeObject.scaleX = 2.0f;
    freeObject.scaleY = 0.5f;
    freeObject.scaleZ = 1.0f;
    freeObject.materialIndex = 1;
    src.objects.push_back(freeObject);

    SceneNs::ObjectData gridObject{};
    gridObject.materialIndex = -1;
    src.objects.push_back(gridObject);
    src.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));

    // 編集中のレベルは常に採番済なので、 基準 CRC も採番 + 追従カメラ済から取る
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects.push_back(LevelNs::MakeFollowCameraObject(src.objects[2].objectId));
    SceneNs::EnsureUniqueObjectIds(src);
    const auto crc0 = src.ComputeCrc32();
    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));

    SceneNs::SceneData dst;
    ASSERT_TRUE(LevelNs::LoadLevelFromFile(dst, *path));
    EXPECT_EQ(dst.ComputeCrc32(), crc0);

    ASSERT_EQ(dst.objects.size(), 4u);
    ASSERT_EQ(dst.materialPaths.size(), 2u);
    EXPECT_EQ(dst.materialPaths[0], "Assets/Materials/stone.mat");
    EXPECT_EQ(dst.materialPaths[1], "Assets/Materials/grid.mat");

    EXPECT_FLOAT_EQ(dst.objects[0].positionX, 1.5f);
    EXPECT_FLOAT_EQ(dst.objects[0].positionZ, -3.75f);
    EXPECT_FLOAT_EQ(dst.objects[0].rotationW, 0.70710677f);
    EXPECT_FLOAT_EQ(dst.objects[0].scaleX, 2.0f);
    EXPECT_EQ(dst.objects[0].materialIndex, 1);

    EXPECT_EQ(dst.objects[1].materialIndex, -1);
}

// 配置物の "Base Color" 反射値が save→reload を往復で保持される
// 種別固定の色上書きが消え、 色は component 経由で永続することの担保
TEST(SaveLoadRoundTrip, BaseColorSurvivesRoundTrip)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path = EditorNs::BuildLevelPath("test_basecolor");
    ASSERT_TRUE(path.has_value());

    SceneNs::SceneData src;
    SceneNs::ObjectData solid = LevelNs::MakeCellObject(0, 0, 0, 0);
    const NS::Math::Vector3 baseColor{0.2f, 0.6f, 0.9f};
    for (auto& component : solid.components)
        for (auto& field : component.fields)
            if (field.name == "Base Color")
                field.value = baseColor;
    src.objects.push_back(solid);
    src.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));

    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));
    SceneNs::SceneData dst;
    ASSERT_TRUE(LevelNs::LoadLevelFromFile(dst, *path));

    // solid + player
    ASSERT_EQ(dst.objects.size(), 2u);
    bool found = false;
    for (const auto& component : dst.objects[0].components)
        for (const auto& field : component.fields)
            if (field.name == "Base Color" && std::holds_alternative<NS::Math::Vector3>(field.value))
            {
                const auto& v = std::get<NS::Math::Vector3>(field.value);
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
    src.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{1.25f, 3.5f, -2.75f},
                                                    NS::Math::Quaternion{0.0f, 0.70710677f, 0.0f, 0.70710677f}));
    SceneNs::EnsureUniqueObjectIds(src);

    const std::string json = LevelNs::SerializeLevelToJson(src);
    SceneNs::SceneData dst;
    ASSERT_TRUE(LevelNs::DeserializeLevelFromJson(dst, json));

    ASSERT_EQ(dst.objects.size(), 1u);
    const std::size_t playerIndex = LevelNs::FindPlayerObjectIndex(dst);
    ASSERT_NE(playerIndex, SceneNs::kNoObjectIndex);
    const SceneNs::ObjectData& loaded = dst.objects[playerIndex];
    EXPECT_FLOAT_EQ(loaded.positionX, 1.25f);
    EXPECT_FLOAT_EQ(loaded.positionY, 3.5f);
    EXPECT_FLOAT_EQ(loaded.positionZ, -2.75f);
    EXPECT_FLOAT_EQ(loaded.rotationY, 0.70710677f);
    EXPECT_FLOAT_EQ(loaded.rotationW, 0.70710677f);
}

// 空のシーンには読込の門がプレイヤーと追従カメラを既定構成で合成し、 プレイ可能な最小構成を保証する
TEST(EnsureLevelSeedObjects, SynthesizesPlayerAndFollowCamera)
{
    SceneNs::SceneData level;
    EXPECT_TRUE(LevelNs::EnsureLevelSeedObjects(level));

    const std::size_t playerIndex = LevelNs::FindPlayerObjectIndex(level);
    ASSERT_NE(playerIndex, SceneNs::kNoObjectIndex);
    const SceneNs::ObjectData& player = level.objects[playerIndex];
    EXPECT_NE(player.objectId, 0u); // 合成後の一意化で永続 id も振られる
    EXPECT_FLOAT_EQ(player.positionY, LevelNs::kDefaultPlayerSpawnY);
    // 既定構成 4 点。 mesh 描画 + 移動 + 入力 + 接地影
    EXPECT_NE(SceneNs::FindComponentData(player, "MeshRendererComponent"), nullptr);
    EXPECT_NE(SceneNs::FindComponentData(player, "CharacterMovementComponent"), nullptr);
    EXPECT_NE(SceneNs::FindComponentData(player, "PlayerInputComponent"), nullptr);
    EXPECT_NE(SceneNs::FindComponentData(player, "ShadowComponent"), nullptr);

    // 追従カメラも 1 台合成され、 Target は合成したプレイヤーを指す
    const std::size_t followIndex = LevelNs::FindFollowCameraObjectIndex(level);
    ASSERT_NE(followIndex, SceneNs::kNoObjectIndex);
    const SceneNs::ComponentData* comp =
        SceneNs::FindComponentData(level.objects[followIndex], "ThirdPersonFollowComponent");
    ASSERT_NE(comp, nullptr);
    const auto* target = SceneNs::FindField(*comp, "Target");
    ASSERT_NE(target, nullptr);
    ASSERT_TRUE(std::holds_alternative<NS::Scene::ObjectRef>(target->value));
    EXPECT_EQ(std::get<NS::Scene::ObjectRef>(target->value).id, player.objectId);
}

// プレイヤーが複数居ても先頭を正とする。 手編集の重複でも読込と門は成立する
TEST(SaveLoadRoundTrip, MultiplePlayersFirstWins)
{
    SceneNs::SceneData src;
    src.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{1.0f, 0.0f, 0.0f}, NS::Math::Quaternion{}));
    src.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{9.0f, 0.0f, 0.0f}, NS::Math::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(src);

    const std::string json = LevelNs::SerializeLevelToJson(src);
    SceneNs::SceneData dst;
    ASSERT_TRUE(LevelNs::DeserializeLevelFromJson(dst, json));

    ASSERT_EQ(dst.objects.size(), 2u);
    EXPECT_FALSE(LevelNs::EnsureLevelSeedObjects(dst)); // プレイヤーが居るので合成しない
    const std::size_t playerIndex = LevelNs::FindPlayerObjectIndex(dst);
    ASSERT_EQ(playerIndex, 0u);
    EXPECT_FLOAT_EQ(dst.objects[playerIndex].positionX, 1.0f);
}

// v3 までの据え置きカメラは別リストだった。 load で PlacedVirtualCamera 持ちの配置物へ変換される
TEST(SaveLoadRoundTrip, LegacyCameraVolumesMigrateToObjects)
{
    const std::string legacyJson = R"({
        "formatVersion": 3,
        "objects": [],
        "cameraVolumes": [
            {
                "cameraPosition": [8.0, 4.0, -6.0],
                "lookTarget": [8.0, 0.0, 0.0],
                "triggerCenter": [8.0, 1.0, 0.0],
                "triggerExtent": [2.0, 1.5, 2.0],
                "priority": 20,
                "lookAtPlayer": 1
            }
        ]
    })";

    SceneNs::SceneData dst;
    ASSERT_TRUE(LevelNs::DeserializeLevelFromJson(dst, legacyJson));

    ASSERT_EQ(dst.objects.size(), 1u);
    const SceneNs::ObjectData& camera = dst.objects[0];
    EXPECT_NE(camera.objectId, 0u); // 移行後の一意化で永続 id も振られる
    EXPECT_FLOAT_EQ(camera.positionX, 8.0f);
    EXPECT_FLOAT_EQ(camera.positionY, 4.0f);
    EXPECT_FLOAT_EQ(camera.positionZ, -6.0f);

    const SceneNs::ComponentData* placed = SceneNs::FindComponentData(camera, "PlacedVirtualCamera");
    ASSERT_NE(placed, nullptr);
    const auto* lookTarget = SceneNs::FindField(*placed, "Look Target");
    ASSERT_NE(lookTarget, nullptr);
    EXPECT_FLOAT_EQ(std::get<NS::Math::Vector3>(lookTarget->value).x, 8.0f);
    const auto* priority = SceneNs::FindField(*placed, "Priority");
    ASSERT_NE(priority, nullptr);
    EXPECT_EQ(std::get<int>(priority->value), 20);
    const auto* lookAtPlayer = SceneNs::FindField(*placed, "Look At Player");
    ASSERT_NE(lookAtPlayer, nullptr);
    EXPECT_TRUE(std::get<bool>(lookAtPlayer->value));
    const auto* extent = SceneNs::FindField(*placed, "Trigger Extent");
    ASSERT_NE(extent, nullptr);
    EXPECT_FLOAT_EQ(std::get<NS::Math::Vector3>(extent->value).y, 1.5f);
}

// プレイヤーだけのシーンには読込の門がプレイヤーを追う 1 台を合成し、「必ず 1 台」を保証する
TEST(EnsureLevelSeedObjects, SynthesizesFollowCameraTargetingExistingPlayer)
{
    SceneNs::SceneData dst;
    dst.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{2.0f, 1.41f, 0.0f}, NS::Math::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(dst);
    const std::uint32_t playerId = dst.objects[0].objectId;

    EXPECT_FALSE(LevelNs::EnsureLevelSeedObjects(dst)); // プレイヤー自体は合成していない

    const std::size_t followIndex = LevelNs::FindFollowCameraObjectIndex(dst);
    ASSERT_NE(followIndex, SceneNs::kNoObjectIndex);
    const SceneNs::ObjectData& follow = dst.objects[followIndex];
    EXPECT_NE(follow.objectId, 0u); // 合成後の一意化で永続 id も振られる

    const SceneNs::ComponentData* comp = SceneNs::FindComponentData(follow, "ThirdPersonFollowComponent");
    ASSERT_NE(comp, nullptr);
    // 追従先はプレイヤー実体への通常の ObjectRef
    const auto* target = SceneNs::FindField(*comp, "Target");
    ASSERT_NE(target, nullptr);
    ASSERT_TRUE(std::holds_alternative<NS::Scene::ObjectRef>(target->value));
    EXPECT_EQ(std::get<NS::Scene::ObjectRef>(target->value).id, playerId);
    // プレイの遠景を抑える投影値も既定で焼かれる
    const auto* farPlane = SceneNs::FindField(*comp, "Far Plane");
    ASSERT_NE(farPlane, nullptr);
    EXPECT_FLOAT_EQ(std::get<float>(farPlane->value), 100.0f);
}

// 環境欄が save→load で往復する。 シーンが環境を所有する保存形式 v7 の要
TEST(SaveLoadRoundTrip, EnvironmentRoundTrip)
{
    SceneNs::SceneData src;
    src.environment.lightDirection = NS::Math::Vector3{0.5f, -0.8f, 0.1f};
    src.environment.lightColor = NS::Math::Vector3{1.0f, 0.9f, 0.8f};
    src.environment.ambientColor = NS::Math::Vector3{0.1f, 0.2f, 0.3f};
    src.environment.skyboxCubemapPath = "Assets/Skybox/kurt/";
    src.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects.push_back(LevelNs::MakeFollowCameraObject(src.objects[0].objectId));
    SceneNs::EnsureUniqueObjectIds(src);
    const auto crc0 = src.ComputeCrc32();

    const std::string json = LevelNs::SerializeLevelToJson(src);
    SceneNs::SceneData dst;
    ASSERT_TRUE(LevelNs::DeserializeLevelFromJson(dst, json));

    EXPECT_FLOAT_EQ(dst.environment.lightDirection.x, 0.5f);
    EXPECT_FLOAT_EQ(dst.environment.lightDirection.y, -0.8f);
    EXPECT_FLOAT_EQ(dst.environment.lightDirection.z, 0.1f);
    EXPECT_FLOAT_EQ(dst.environment.lightColor.x, 1.0f);
    EXPECT_FLOAT_EQ(dst.environment.lightColor.y, 0.9f);
    EXPECT_FLOAT_EQ(dst.environment.lightColor.z, 0.8f);
    EXPECT_FLOAT_EQ(dst.environment.ambientColor.x, 0.1f);
    EXPECT_FLOAT_EQ(dst.environment.ambientColor.y, 0.2f);
    EXPECT_FLOAT_EQ(dst.environment.ambientColor.z, 0.3f);
    EXPECT_EQ(dst.environment.skyboxCubemapPath, "Assets/Skybox/kurt/");
    EXPECT_EQ(dst.ComputeCrc32(), crc0);
}

// 環境欄の欠落キーは中立既定値のまま読込が継続する。 欠けたキーだけ据え置く部分適用は雛形ファイルと同じ挙動
TEST(SaveLoadRoundTrip, EnvironmentPartialKeysKeepNeutralDefaults)
{
    const std::string json = R"({
        "formatVersion": 7,
        "objects": [],
        "materialPaths": [],
        "environment": { "lightColor": [0.5, 0.6, 0.7] }
    })";

    SceneNs::SceneData dst;
    ASSERT_TRUE(LevelNs::DeserializeLevelFromJson(dst, json));

    EXPECT_FLOAT_EQ(dst.environment.lightColor.x, 0.5f);
    EXPECT_FLOAT_EQ(dst.environment.lightColor.y, 0.6f);
    EXPECT_FLOAT_EQ(dst.environment.lightColor.z, 0.7f);
    // 書かれていないキーは中立の既定値のまま残る
    const SceneNs::SceneEnvironment neutral{};
    EXPECT_FLOAT_EQ(dst.environment.lightDirection.x, neutral.lightDirection.x);
    EXPECT_FLOAT_EQ(dst.environment.lightDirection.y, neutral.lightDirection.y);
    EXPECT_FLOAT_EQ(dst.environment.ambientColor.x, neutral.ambientColor.x);
    EXPECT_EQ(dst.environment.skyboxCubemapPath, neutral.skyboxCubemapPath);
}

// 追従カメラ実体が既に居れば合成は走らず、 Target 参照ごと往復で保持される
TEST(SaveLoadRoundTrip, FollowCameraObjectRoundTrip)
{
    SceneNs::SceneData src;
    src.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    SceneNs::EnsureUniqueObjectIds(src);
    src.objects.push_back(LevelNs::MakeFollowCameraObject(src.objects[0].objectId));
    SceneNs::EnsureUniqueObjectIds(src);
    const auto crc0 = src.ComputeCrc32();

    const std::string json = LevelNs::SerializeLevelToJson(src);
    SceneNs::SceneData dst;
    ASSERT_TRUE(LevelNs::DeserializeLevelFromJson(dst, json));

    ASSERT_EQ(dst.objects.size(), 2u);
    EXPECT_EQ(dst.ComputeCrc32(), crc0);
    const std::size_t followIndex = LevelNs::FindFollowCameraObjectIndex(dst);
    ASSERT_EQ(followIndex, 1u);
    const SceneNs::ComponentData* comp = SceneNs::FindComponentData(dst.objects[1], "ThirdPersonFollowComponent");
    ASSERT_NE(comp, nullptr);
    const auto* target = SceneNs::FindField(*comp, "Target");
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(std::get<NS::Scene::ObjectRef>(target->value).id, dst.objects[0].objectId);
}
