#include "Framework/Core/Filesystem.h"
#include "Game/Level/ChunkIO.h"
#include "Game/Level/LevelData.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <random>
#include <string>

namespace LevelNs = NS::Game::Level;

namespace
{
    std::filesystem::path UniqueTempPath(const char* name)
    {
        // 並列 test 実行や前回残骸との衝突を避けるため pid + random suffix を追加。
        static std::mt19937_64 rng{std::random_device{}()};
        const auto suffix = rng();
        return std::filesystem::temp_directory_path() /
               (std::string("ns_") + name + "_" + std::to_string(suffix) + ".nslvl");
    }

    LevelNs::LevelData MakeFixture()
    {
        LevelNs::LevelData lv;
        lv.blocks.push_back({0, 0, 0, 1, 0, 0});
        lv.blocks.push_back({1, 0, 0, 2, 1, 0});
        lv.blocks.push_back({-3, 5, 7, 100, 2, 0});
        lv.spawnX = 1;
        lv.spawnY = 2;
        lv.spawnZ = 3;
        lv.themeId = 7;
        lv.bgmId = 13;
        lv.coinThreshold = 40;
        lv.timeLimitSeconds = 300;
        return lv;
    }
} // namespace

TEST(ChunkIOTest, RoundTripPreservesAllFields)
{
    const auto path = UniqueTempPath("roundtrip");
    const auto src = MakeFixture();

    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, path));

    LevelNs::LevelData dst;
    ASSERT_TRUE(LevelNs::LoadLevelFromFile(dst, path));

    EXPECT_EQ(src.ComputeCrc32(), dst.ComputeCrc32());
    ASSERT_EQ(src.blocks.size(), dst.blocks.size());
    for (std::size_t i = 0; i < src.blocks.size(); ++i)
    {
        EXPECT_EQ(src.blocks[i].x, dst.blocks[i].x);
        EXPECT_EQ(src.blocks[i].y, dst.blocks[i].y);
        EXPECT_EQ(src.blocks[i].z, dst.blocks[i].z);
        EXPECT_EQ(src.blocks[i].blockId, dst.blocks[i].blockId);
        EXPECT_EQ(src.blocks[i].rotation, dst.blocks[i].rotation);
    }
    EXPECT_EQ(src.spawnX, dst.spawnX);
    EXPECT_EQ(src.spawnY, dst.spawnY);
    EXPECT_EQ(src.spawnZ, dst.spawnZ);
    EXPECT_EQ(src.themeId, dst.themeId);
    EXPECT_EQ(src.bgmId, dst.bgmId);
    EXPECT_EQ(src.coinThreshold, dst.coinThreshold);
    EXPECT_EQ(src.timeLimitSeconds, dst.timeLimitSeconds);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(ChunkIOTest, RoundTripIsDeterministic)
{
    const auto path1 = UniqueTempPath("det1");
    const auto path2 = UniqueTempPath("det2");
    const auto src = MakeFixture();

    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, path1));
    ASSERT_TRUE(LevelNs::SaveLevelToFile(src, path2));

    auto b1 = NS::Core::FileSystem::ReadAllBytes(path1);
    auto b2 = NS::Core::FileSystem::ReadAllBytes(path2);
    ASSERT_TRUE(b1.has_value());
    ASSERT_TRUE(b2.has_value());
    EXPECT_EQ(*b1, *b2);

    std::error_code ec;
    std::filesystem::remove(path1, ec);
    std::filesystem::remove(path2, ec);
}

TEST(ChunkIOTest, RejectMagicMismatch)
{
    const auto path = UniqueTempPath("badmagic");
    ASSERT_TRUE(LevelNs::SaveLevelToFile(MakeFixture(), path));

    auto bytes = NS::Core::FileSystem::ReadAllBytes(path);
    ASSERT_TRUE(bytes.has_value());
    (*bytes)[0] = std::byte{'X'};
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(path, *bytes));

    LevelNs::LevelData dst;
    EXPECT_FALSE(LevelNs::LoadLevelFromFile(dst, path));
    EXPECT_TRUE(dst.blocks.empty());

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(ChunkIOTest, RejectCrcMismatch)
{
    const auto path = UniqueTempPath("badcrc");
    ASSERT_TRUE(LevelNs::SaveLevelToFile(MakeFixture(), path));

    auto bytes = NS::Core::FileSystem::ReadAllBytes(path);
    ASSERT_TRUE(bytes.has_value());
    // 末尾の CRC32 値を 1 bit flip。
    const std::size_t last = bytes->size() - 1;
    (*bytes)[last] = static_cast<std::byte>(static_cast<std::uint8_t>((*bytes)[last]) ^ 0x01);
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(path, *bytes));

    LevelNs::LevelData dst;
    EXPECT_FALSE(LevelNs::LoadLevelFromFile(dst, path));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(ChunkIOTest, RejectTruncated)
{
    const auto path = UniqueTempPath("truncated");
    ASSERT_TRUE(LevelNs::SaveLevelToFile(MakeFixture(), path));

    auto bytes = NS::Core::FileSystem::ReadAllBytes(path);
    ASSERT_TRUE(bytes.has_value());
    bytes->resize(bytes->size() / 2);
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(path, *bytes));

    LevelNs::LevelData dst;
    EXPECT_FALSE(LevelNs::LoadLevelFromFile(dst, path));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(ChunkIOTest, RejectMajorVersionMismatch)
{
    const auto path = UniqueTempPath("v2");
    ASSERT_TRUE(LevelNs::SaveLevelToFile(MakeFixture(), path));

    auto bytes = NS::Core::FileSystem::ReadAllBytes(path);
    ASSERT_TRUE(bytes.has_value());
    // version major (offset +4, u16 LE) を 2 に書換 (CRC も合わせて壊れるが major reject が先に走るはず)。
    (*bytes)[4] = std::byte{2};
    (*bytes)[5] = std::byte{0};
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(path, *bytes));

    LevelNs::LevelData dst;
    EXPECT_FALSE(LevelNs::LoadLevelFromFile(dst, path));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(ChunkIOTest, EmptyLevelRoundTrip)
{
    const auto path = UniqueTempPath("empty");
    LevelNs::LevelData empty;
    ASSERT_TRUE(LevelNs::SaveLevelToFile(empty, path));

    LevelNs::LevelData dst;
    ASSERT_TRUE(LevelNs::LoadLevelFromFile(dst, path));
    EXPECT_TRUE(dst.blocks.empty());
    EXPECT_EQ(empty.ComputeCrc32(), dst.ComputeCrc32());

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
