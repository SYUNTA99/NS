#include "Framework/Core/Filesystem.h"
#include "Game/Editor/BlockRegistry.h"
#include "Game/Editor/LevelFilePaths.h"
#include "Game/Level/ChunkIO.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/detail/crc32.h"

#include <cstring>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;
namespace EditorNs = NS::Game::Editor;

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
    src.blocks.push_back({0, 0, 0, EditorNs::kBlockIdSolid, 0, 0});
    src.blocks.push_back({1, 0, 1, EditorNs::kBlockIdSolid, 1, 0});
    src.blocks.push_back({2, 0, 0, EditorNs::kBlockIdCoin, 0, 0});
    const auto crc0 = src.ComputeCrc32();

    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));

    LevelNs::LevelData dst;
    ASSERT_TRUE(LevelNs::LoadLevelFromFile(dst, *path));
    EXPECT_EQ(dst.ComputeCrc32(), crc0);
}

TEST(SaveLoadRoundTrip, TwoSavesAreByteIdentical)
{
    EditorNs::EnsureLevelsDirectoryExists();
    auto path1 = EditorNs::BuildLevelPath("test_byteid_a");
    auto path2 = EditorNs::BuildLevelPath("test_byteid_b");
    ASSERT_TRUE(path1);
    ASSERT_TRUE(path2);

    LevelNs::LevelData src;
    src.blocks.push_back({5, 5, 5, EditorNs::kBlockIdSolid, 0, 0});

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
    src.blocks.push_back({0, 0, 0, EditorNs::kBlockIdSolid, 0, 0});
    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, *path));

    auto bytes = NS::Core::FileSystem::ReadAllBytes(*path);
    ASSERT_TRUE(bytes.has_value());
    ASSERT_GE(bytes->size(), 1u);
    (*bytes)[0] = std::byte{'X'};
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(*path, std::span<const std::byte>(*bytes)));

    LevelNs::LevelData dst;
    EXPECT_FALSE(LevelNs::LoadLevelFromFile(dst, *path));
    EXPECT_TRUE(dst.blocks.empty());
}

TEST(SaveLoadRoundTrip, BuildLevelPathRejectsTraversal)
{
    // path traversal が path 構築層で構造的に止まることを test (T-03-10)
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
    src.blocks.push_back({0, 0, 0, EditorNs::kBlockIdSolid, 0, 0});
    src.blocks.push_back({2, 1, 4, EditorNs::kBlockIdSolid, 2, 0});
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
    ASSERT_EQ(dst.blocks.size(), 2u);
    EXPECT_EQ(dst.spawnX, 7);
    EXPECT_EQ(dst.spawnY, 3);
    EXPECT_EQ(dst.spawnZ, -1);
}
