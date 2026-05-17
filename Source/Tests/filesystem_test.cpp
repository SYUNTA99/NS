#include <gtest/gtest.h>

#include <ns/core/filesystem.h>
#include <ns/core/logger.h>

#include <chrono>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    /// テスト用にユニークな一時パスを生成する (実時刻ナノ秒 + テスト名)
    std::filesystem::path MakeTempPath(const std::string& suffix)
    {
        const auto ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        return std::filesystem::temp_directory_path() / ("ns_fstest_" + std::to_string(ns) + "_" + suffix);
    }
} // namespace

class FileSystemLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { ns::core::Logger::Init(); }
    void TearDown() override { ns::core::Logger::Shutdown(); }
};

TEST(NsCoreFileSystem, ExistsReturnsFalseForMissingFile)
{
    const auto p = MakeTempPath("missing.txt");
    EXPECT_FALSE(ns::core::FileSystem::Exists(p));
}

TEST(NsCoreFileSystem, CreateDirectoryThenExistsReturnsTrue)
{
    const auto dir = MakeTempPath("dir") / "nested" / "deep";
    ASSERT_TRUE(ns::core::FileSystem::CreateDirectories(dir));
    EXPECT_TRUE(ns::core::FileSystem::Exists(dir));
    std::filesystem::remove_all(MakeTempPath("dir"));
}

TEST(NsCoreFileSystem, WriteAndReadAllBytesRoundTrip)
{
    const auto path = MakeTempPath("bytes.bin");
    const std::vector<std::byte> original = {
        std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}, std::byte{0x00}, std::byte{0xFF}};

    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(original.data()), static_cast<std::streamsize>(original.size()));
    }

    const auto read = ns::core::FileSystem::ReadAllBytes(path);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, original);

    std::filesystem::remove(path);
}

TEST(NsCoreFileSystem, WriteAndReadAllTextRoundTrip)
{
    const auto path = MakeTempPath("text.txt");
    const std::string original = "Hello, ファイル!\nLine 2";

    {
        std::ofstream out(path);
        out << original;
    }

    const auto read = ns::core::FileSystem::ReadAllText(path);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, original);

    std::filesystem::remove(path);
}

TEST_F(FileSystemLoggerTest, ReadAllBytesReturnsNulloptForMissingFile)
{
    const auto path = MakeTempPath("nonexistent.bin");
    const auto result = ns::core::FileSystem::ReadAllBytes(path);
    EXPECT_FALSE(result.has_value());
}

TEST(NsCoreFileSystem, GetExeDirectoryReturnsExistingPath)
{
    const auto dir = ns::core::FileSystem::GetExeDirectory();
    EXPECT_FALSE(dir.empty());
    EXPECT_TRUE(ns::core::FileSystem::Exists(dir));
    EXPECT_TRUE(dir.is_absolute());
}
