#include "Editor/LevelFilePaths.h"
#include "Framework/Core/Filesystem.h"
#include "Game/Blocks/BlockRegistry.h"
#include "Game/Level/ChunkIO.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/LevelJson.h"

#include <cstring>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;
namespace EditorNs = NS::Editor;

TEST(SaveLoadRoundTrip, SaveAndReloadSemanticEqual)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path = EditorNs::BuildLevelPath("test_roundtrip");
    ASSERT_TRUE(path.has_value());

    LevelNs::LevelData src;
    src.spawnX = 1;
    src.spawnY = 2;
    src.spawnZ = 3;
    src.themeId = 4;
    src.coinThreshold = 10;
    src.timeLimitSeconds = 180;
    src.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 0));
    src.objects.push_back(LevelNs::MakeGridObject(1, 0, 1, 1));
    src.objects.push_back(LevelNs::MakeGridObject(2, 0, 0, 0));
    const auto crc0 = src.ComputeCrc32();

    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));

    LevelNs::LevelData dst;
    ASSERT_TRUE(LevelNs::LoadLevelFromFile(dst, *path));
    EXPECT_EQ(dst.ComputeCrc32(), crc0);
}

TEST(SaveLoadRoundTrip, DefaultObjectShapeColliderIsBox)
{
    LevelNs::ObjectInstance obj{};
    EXPECT_EQ(LevelNs::ObjectShapeCollider(obj), LevelNs::ShapeCollider::Box);

    LevelNs::SetObjectShapeCollider(obj, LevelNs::ShapeCollider::Sphere);
    EXPECT_EQ(LevelNs::ObjectShapeCollider(obj), LevelNs::ShapeCollider::Sphere);
    EXPECT_EQ(obj.shapeCollider, static_cast<std::uint8_t>(1));
}

TEST(SaveLoadRoundTrip, ShapeColliderAndDimensionsSurviveRoundTrip)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path = EditorNs::BuildLevelPath("test_collider_shape");
    ASSERT_TRUE(path.has_value());

    LevelNs::LevelData src;
    LevelNs::ObjectInstance obj{}; // 自由配置物 (gridAligned は立てない)
    obj.positionX = 2.0f;
    LevelNs::SetObjectShapeCollider(obj, LevelNs::ShapeCollider::Capsule);
    obj.colliderHalfExtentsX = 0.3f; // capsule では半径
    obj.colliderHalfExtentsY = 0.7f; // capsule では半高
    src.objects.push_back(obj);

    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));

    LevelNs::LevelData dst;
    ASSERT_TRUE(LevelNs::LoadLevelFromFile(dst, *path));
    ASSERT_EQ(dst.objects.size(), 1u);
    EXPECT_EQ(LevelNs::ObjectShapeCollider(dst.objects[0]), LevelNs::ShapeCollider::Capsule);
    EXPECT_FLOAT_EQ(dst.objects[0].colliderHalfExtentsX, 0.3f);
    EXPECT_FLOAT_EQ(dst.objects[0].colliderHalfExtentsY, 0.7f);
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

    LevelNs::LevelData src;
    src.objects.push_back(LevelNs::MakeGridObject(5, 5, 5, 0));

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

    LevelNs::LevelData src;
    src.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 0));
    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));

    auto bytes = NS::Core::FileSystem::ReadAllBytes(*path);
    ASSERT_TRUE(bytes.has_value());
    ASSERT_GE(bytes->size(), 1u);
    // 先頭の '{' を壊すと JSON parse が失敗し、 load は false + 空 LevelData を返す
    (*bytes)[0] = std::byte{'X'};
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(*path, std::span<const std::byte>(*bytes)));

    LevelNs::LevelData dst;
    EXPECT_FALSE(LevelNs::LoadLevelFromFile(dst, *path));
    EXPECT_TRUE(dst.objects.empty());
}

// object 数が上限を超えるレベルは保存段でクラッシュせず false を返す (memory exhaustion の DoS 防御)
TEST(SaveLoadRoundTrip, RejectsOversizedObjectCount)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path = EditorNs::BuildLevelPath("test_oversized");
    ASSERT_TRUE(path.has_value());

    LevelNs::LevelData huge;
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

    LevelNs::LevelData level;
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

    LevelNs::LevelData src;
    LevelNs::ObjectInstance freeObject{};
    freeObject.positionX = 1.5f;

    LevelNs::ComponentData comp;
    comp.typeName = "BoxColliderComponent";
    comp.fields.push_back(LevelNs::FieldValue{"vHalf", NS::Math::Vector3{1.0f, 2.0f, 3.0f}});
    comp.fields.push_back(LevelNs::FieldValue{"iCount", 7});
    comp.fields.push_back(LevelNs::FieldValue{"bOn", true});
    comp.fields.push_back(LevelNs::FieldValue{"fSpeed", 1.5f});
    comp.fields.push_back(LevelNs::FieldValue{"fWhole", 4.0f}); // 整数値の float が int に化けないことを確かめる
    freeObject.components.push_back(std::move(comp));
    src.objects.push_back(std::move(freeObject));

    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));

    LevelNs::LevelData dst;
    ASSERT_TRUE(LevelNs::LoadLevelFromFile(dst, *path));

    EXPECT_EQ(LevelNs::SerializeLevelToJson(dst), LevelNs::SerializeLevelToJson(src));

    ASSERT_EQ(dst.objects.size(), 1u);
    ASSERT_EQ(dst.objects[0].components.size(), 1u);
    const auto& fields = dst.objects[0].components[0].fields;
    EXPECT_EQ(dst.objects[0].components[0].typeName, "BoxColliderComponent");
    ASSERT_EQ(fields.size(), 5u);

    const auto find = [&fields](const char* name) -> const LevelNs::FieldValue* {
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

// 統一配置物 (ObjectInstance) と material 文字列表が round-trip で完全復元できることを検証する
TEST(SaveLoadRoundTrip, ObjectsAndMaterialsRoundTrip)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path = EditorNs::BuildLevelPath("test_objects_roundtrip");
    ASSERT_TRUE(path.has_value());

    LevelNs::LevelData src;
    src.materialPaths.push_back("Assets/Materials/stone.mat");
    src.materialPaths.push_back("Assets/Materials/grid.mat");

    LevelNs::ObjectInstance freeObject{};
    freeObject.positionX = 1.5f;
    freeObject.positionY = 2.25f;
    freeObject.positionZ = -3.75f;
    freeObject.rotationY = 0.70710677f;
    freeObject.rotationW = 0.70710677f;
    freeObject.scaleX = 2.0f;
    freeObject.scaleY = 0.5f;
    freeObject.scaleZ = 1.0f;
    freeObject.materialIndex = 1;
    freeObject.flags = 0;
    freeObject.colliderHalfExtentsX = 0.3f;
    freeObject.colliderHalfExtentsY = 1.25f;
    freeObject.colliderHalfExtentsZ = 0.8f;
    freeObject.colliderOffsetX = 0.1f;
    freeObject.colliderOffsetY = -0.4f;
    freeObject.colliderOffsetZ = 0.6f;
    freeObject.colliderRotationY = 0.70710677f;
    freeObject.colliderRotationW = 0.70710677f;
    src.objects.push_back(freeObject);

    LevelNs::ObjectInstance gridObject{};
    gridObject.materialIndex = -1;
    gridObject.flags = LevelNs::kObjectFlagGridAligned;
    src.objects.push_back(gridObject);

    const auto crc0 = src.ComputeCrc32();
    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));

    LevelNs::LevelData dst;
    ASSERT_TRUE(LevelNs::LoadLevelFromFile(dst, *path));
    EXPECT_EQ(dst.ComputeCrc32(), crc0);

    ASSERT_EQ(dst.objects.size(), 2u);
    ASSERT_EQ(dst.materialPaths.size(), 2u);
    EXPECT_EQ(dst.materialPaths[0], "Assets/Materials/stone.mat");
    EXPECT_EQ(dst.materialPaths[1], "Assets/Materials/grid.mat");

    EXPECT_FLOAT_EQ(dst.objects[0].positionX, 1.5f);
    EXPECT_FLOAT_EQ(dst.objects[0].positionZ, -3.75f);
    EXPECT_FLOAT_EQ(dst.objects[0].rotationW, 0.70710677f);
    EXPECT_FLOAT_EQ(dst.objects[0].scaleX, 2.0f);
    EXPECT_EQ(dst.objects[0].materialIndex, 1);
    EXPECT_EQ(dst.objects[0].flags, 0u);
    EXPECT_FLOAT_EQ(dst.objects[0].colliderHalfExtentsX, 0.3f);
    EXPECT_FLOAT_EQ(dst.objects[0].colliderHalfExtentsY, 1.25f);
    EXPECT_FLOAT_EQ(dst.objects[0].colliderHalfExtentsZ, 0.8f);
    EXPECT_FLOAT_EQ(dst.objects[0].colliderOffsetX, 0.1f);
    EXPECT_FLOAT_EQ(dst.objects[0].colliderOffsetY, -0.4f);
    EXPECT_FLOAT_EQ(dst.objects[0].colliderOffsetZ, 0.6f);
    EXPECT_FLOAT_EQ(dst.objects[0].colliderRotationY, 0.70710677f);
    EXPECT_FLOAT_EQ(dst.objects[0].colliderRotationW, 0.70710677f);

    EXPECT_EQ(dst.objects[1].materialIndex, -1);
    EXPECT_EQ(dst.objects[1].flags, LevelNs::kObjectFlagGridAligned);
}

// 旧 blocks → 統一 objects の移行写像 (セル整数 → world float、 rotation → yaw quaternion、 gridAligned 付与)
// blockId が決めていた種別は移行で実 component へ起き、 solid は Box・ slope は SlopeCollider になる
TEST(SaveLoadRoundTrip, MigrateBlocksToObjectsMapsCells)
{
    LevelNs::LevelData level;
    std::vector<LevelNs::BlockEntry> blocks;
    blocks.push_back({1, 2, 3, NS::Game::Blocks::kBlockIdSolid, 0, 0});
    blocks.push_back({-4, 0, 5, NS::Game::Blocks::kBlockIdSlope45, 1, 0});
    LevelNs::MigrateBlocksToObjects(level, blocks);

    ASSERT_EQ(level.objects.size(), 2u);

    const auto hasComponent = [](const LevelNs::ObjectInstance& object, const char* typeName) {
        for (const auto& component : object.components)
            if (component.typeName == typeName)
                return true;
        return false;
    };

    EXPECT_FLOAT_EQ(level.objects[0].positionX, 1.0f);
    EXPECT_FLOAT_EQ(level.objects[0].positionY, 2.0f);
    EXPECT_FLOAT_EQ(level.objects[0].positionZ, 3.0f);
    EXPECT_FLOAT_EQ(level.objects[0].scaleX, 1.0f);
    EXPECT_TRUE(hasComponent(level.objects[0], "BoxColliderComponent"));
    EXPECT_EQ(level.objects[0].materialIndex, -1);
    EXPECT_NE(level.objects[0].flags & LevelNs::kObjectFlagGridAligned, 0);

    EXPECT_FLOAT_EQ(level.objects[1].positionX, -4.0f);
    EXPECT_TRUE(hasComponent(level.objects[1], "SlopeColliderComponent"));
    // rotation=1 は yaw 90°、 単位 quaternion ではない (w != 1)
    EXPECT_NE(level.objects[1].rotationW, 1.0f);
}

// 旧 "kind" だけで components 配列を持たない古いレベルは、 LoadLevelFromFile が読込時に実 component へ
// 移行する。 editor / play どちらの load 経路でも LegacyKind placeholder が残らず描画・当たりが起きる
TEST(SaveLoadRoundTrip, LegacyKindFileMigratesOnLoad)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path = EditorNs::BuildLevelPath("test_legacy_kind");
    ASSERT_TRUE(path.has_value());

    // 旧フォーマット: gridAligned solid を kind=1 だけで表し、 components 配列を持たない
    const std::string legacy = R"({"formatVersion":1,"objects":[)"
                               R"({"transform":{"pos":[0,0,0],"rot":[0,0,0,1],"scale":[1,1,1]},"flags":1,"kind":1}]})";
    const auto* raw = reinterpret_cast<const std::byte*>(legacy.data());
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(*path, std::span<const std::byte>(raw, legacy.size())));

    LevelNs::LevelData dst;
    ASSERT_TRUE(LevelNs::LoadLevelFromFile(dst, *path));
    ASSERT_EQ(dst.objects.size(), 1u);

    const auto hasComponent = [](const LevelNs::ObjectInstance& object, const char* typeName) {
        for (const auto& component : object.components)
            if (component.typeName == typeName)
                return true;
        return false;
    };
    EXPECT_FALSE(hasComponent(dst.objects[0], "LegacyKind")); // 移行で placeholder は消える
    EXPECT_TRUE(hasComponent(dst.objects[0], "MeshRendererComponent"));
    EXPECT_TRUE(hasComponent(dst.objects[0], "BoxColliderComponent"));
}

// 配置物の "Base Color" 反射値が save→reload を往復で保持される
// 種別固定の色上書きが消え、 色は component 経由で永続することの担保
TEST(SaveLoadRoundTrip, BaseColorSurvivesRoundTrip)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path = EditorNs::BuildLevelPath("test_basecolor");
    ASSERT_TRUE(path.has_value());

    LevelNs::LevelData src;
    LevelNs::ObjectInstance solid = LevelNs::MakeGridObject(0, 0, 0, 0);
    const NS::Math::Vector3 baseColor{0.2f, 0.6f, 0.9f};
    for (auto& component : solid.components)
        for (auto& field : component.fields)
            if (field.name == "Base Color")
                field.value = baseColor;
    src.objects.push_back(solid);

    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));
    LevelNs::LevelData dst;
    ASSERT_TRUE(LevelNs::LoadLevelFromFile(dst, *path));

    ASSERT_EQ(dst.objects.size(), 1u);
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
