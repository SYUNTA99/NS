#include "Editor/LevelFilePaths.h"
#include "Framework/Core/Filesystem.h"
#include "Game/Blocks/BlockRegistry.h"
#include "Game/Level/ChunkIO.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/detail/crc32.h"

#include <cstring>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;
namespace EditorNs = NS::Editor;

TEST(SaveLoadRoundTrip, SaveAndReloadProducesIdenticalCrc)
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
    src.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, NS::Game::Blocks::kBlockIdSolid, 0));
    src.objects.push_back(LevelNs::MakeGridObject(1, 0, 1, NS::Game::Blocks::kBlockIdSolid, 1));
    src.objects.push_back(LevelNs::MakeGridObject(2, 0, 0, NS::Game::Blocks::kBlockIdCoin, 0));
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

TEST(SaveLoadRoundTrip, TwoSavesAreByteIdentical)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path1 = EditorNs::BuildLevelPath("test_byteid_a");
    auto path2 = EditorNs::BuildLevelPath("test_byteid_b");
    ASSERT_TRUE(path1);
    ASSERT_TRUE(path2);

    LevelNs::LevelData src;
    src.objects.push_back(LevelNs::MakeGridObject(5, 5, 5, NS::Game::Blocks::kBlockIdSolid, 0));

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
    src.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, NS::Game::Blocks::kBlockIdSolid, 0));
    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));

    auto bytes = NS::Core::FileSystem::ReadAllBytes(*path);
    ASSERT_TRUE(bytes.has_value());
    ASSERT_GE(bytes->size(), 1u);
    (*bytes)[0] = std::byte{'X'};
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(*path, std::span<const std::byte>(*bytes)));

    LevelNs::LevelData dst;
    EXPECT_FALSE(LevelNs::LoadLevelFromFile(dst, *path));
    EXPECT_TRUE(dst.objects.empty());
}

TEST(SaveLoadRoundTrip, BuildLevelPathRejectsTraversal)
{
    // path traversal が path 構築層で構造的に止まることを検証する
    EXPECT_FALSE(EditorNs::BuildLevelPath("../etc/passwd").has_value());
    EXPECT_FALSE(EditorNs::BuildLevelPath("..").has_value());
    EXPECT_FALSE(EditorNs::BuildLevelPath("a/b").has_value());
}

// 既知 chunk (META/BLKS/SPWN) と CRC3 の間に未知 chunk 'XXXX' を挿入しても、
// v1 reader が unknown chunk を size 分 skip し、 既知 chunk の値を完全復元できることを検証
// forward-compat の end-to-end 自動検証 (将来 DECO / ENMY 等を導入した
// file を旧 reader に読ませた時の挙動を保証する)
TEST(SaveLoadRoundTrip, ForwardCompatibleUnknownChunkSkip)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path = EditorNs::BuildLevelPath("test_forward");
    ASSERT_TRUE(path.has_value());

    LevelNs::LevelData src;
    src.spawnX = 7;
    src.spawnY = 3;
    src.spawnZ = -1;
    src.themeId = 5;
    src.coinThreshold = 12;
    src.timeLimitSeconds = 240;
    src.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, NS::Game::Blocks::kBlockIdSolid, 0));
    src.objects.push_back(LevelNs::MakeGridObject(2, 1, 4, NS::Game::Blocks::kBlockIdSolid, 2));
    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));

    auto bytes = NS::Core::FileSystem::ReadAllBytes(*path);
    ASSERT_TRUE(bytes.has_value());

    constexpr std::size_t kCrc3ChunkBytes = 4 + 4 + 4;
    ASSERT_GT(bytes->size(), kCrc3ChunkBytes);
    const std::size_t insertOffset = bytes->size() - kCrc3ChunkBytes;

    std::vector<std::byte> injected;
    injected.reserve(bytes->size() + 16);
    injected.insert(injected.end(), bytes->begin(), bytes->begin() + insertOffset);

    constexpr std::byte unknownChunk[] = {
        std::byte{'X'},
        std::byte{'X'},
        std::byte{'X'},
        std::byte{'X'},
        std::byte{8},
        std::byte{0},
        std::byte{0},
        std::byte{0},
        std::byte{0xDE},
        std::byte{0xAD},
        std::byte{0xBE},
        std::byte{0xEF},
        std::byte{0xCA},
        std::byte{0xFE},
        std::byte{0xBA},
        std::byte{0xBE},
    };
    injected.insert(injected.end(), std::begin(unknownChunk), std::end(unknownChunk));

    const std::uint32_t newCrc =
        NS::Game::Level::detail::Crc32(std::span<const std::byte>(injected.data(), injected.size()));

    constexpr std::byte crc3Header[] = {
        std::byte{'C'},
        std::byte{'R'},
        std::byte{'C'},
        std::byte{'3'},
        std::byte{4},
        std::byte{0},
        std::byte{0},
        std::byte{0},
    };
    injected.insert(injected.end(), std::begin(crc3Header), std::end(crc3Header));
    for (int i = 0; i < 4; ++i)
        injected.push_back(static_cast<std::byte>((newCrc >> (i * 8)) & 0xFF));

    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(*path, std::span<const std::byte>(injected)));

    LevelNs::LevelData dst;
    ASSERT_TRUE(LevelNs::LoadLevelFromFile(dst, *path));
    EXPECT_EQ(dst.ComputeCrc32(), src.ComputeCrc32());
    ASSERT_EQ(dst.objects.size(), 2u);
    EXPECT_EQ(dst.spawnX, 7);
    EXPECT_EQ(dst.spawnY, 3);
    EXPECT_EQ(dst.spawnZ, -1);
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
    freeObject.kind = NS::Game::Blocks::kBlockIdSolid;
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
    gridObject.kind = NS::Game::Blocks::kBlockIdSlope45;
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
    EXPECT_EQ(dst.objects[0].kind, NS::Game::Blocks::kBlockIdSolid);
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

    EXPECT_EQ(dst.objects[1].kind, NS::Game::Blocks::kBlockIdSlope45);
    EXPECT_EQ(dst.objects[1].materialIndex, -1);
    EXPECT_EQ(dst.objects[1].flags, LevelNs::kObjectFlagGridAligned);
}

// 旧 blocks → 統一 objects の移行写像 (セル整数 → world float、 rotation → yaw quaternion、 gridAligned 付与)
TEST(SaveLoadRoundTrip, MigrateBlocksToObjectsMapsCells)
{
    LevelNs::LevelData level;
    std::vector<LevelNs::BlockEntry> blocks;
    blocks.push_back({1, 2, 3, NS::Game::Blocks::kBlockIdSolid, 0, 0});
    blocks.push_back({-4, 0, 5, NS::Game::Blocks::kBlockIdSlope45, 1, 0});
    LevelNs::MigrateBlocksToObjects(level, blocks);

    ASSERT_EQ(level.objects.size(), 2u);

    EXPECT_FLOAT_EQ(level.objects[0].positionX, 1.0f);
    EXPECT_FLOAT_EQ(level.objects[0].positionY, 2.0f);
    EXPECT_FLOAT_EQ(level.objects[0].positionZ, 3.0f);
    EXPECT_FLOAT_EQ(level.objects[0].scaleX, 1.0f);
    EXPECT_EQ(level.objects[0].kind, NS::Game::Blocks::kBlockIdSolid);
    EXPECT_EQ(level.objects[0].materialIndex, -1);
    EXPECT_NE(level.objects[0].flags & LevelNs::kObjectFlagGridAligned, 0);

    EXPECT_FLOAT_EQ(level.objects[1].positionX, -4.0f);
    EXPECT_EQ(level.objects[1].kind, NS::Game::Blocks::kBlockIdSlope45);
    // rotation=1 は yaw 90°、 単位 quaternion ではない (w != 1)
    EXPECT_NE(level.objects[1].rotationW, 1.0f);
}
