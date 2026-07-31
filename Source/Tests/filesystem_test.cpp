#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <Runtime/Core/Filesystem.h>
#include <Runtime/Core/Logger.h>
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
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST(NsCoreFileSystem, ExistsReturnsFalseForMissingFile)
{
    const auto p = MakeTempPath("missing.txt");
    EXPECT_FALSE(NS::Core::FileSystem::Exists(p));
}

TEST(NsCoreFileSystem, CreateDirectoryThenExistsReturnsTrue)
{
    const auto root = MakeTempPath("dir");
    const auto dir = root / "nested" / "deep";
    ASSERT_TRUE(NS::Core::FileSystem::CreateDirectories(dir));
    EXPECT_TRUE(NS::Core::FileSystem::Exists(dir));
    std::filesystem::remove_all(root);
}

TEST(NsCoreFileSystem, ResolveUnderReturnsAbsoluteInsideBase)
{
    const std::filesystem::path base = "C:/content";
    const auto resolved = NS::Core::FileSystem::ResolveUnder(base, "Assets/Skybox/kurt/");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, std::filesystem::path("C:/content/Assets/Skybox/kurt/").lexically_normal());
}

TEST(NsCoreFileSystem, ResolveUnderRejectsAbsolutePath)
{
    const std::filesystem::path base = "C:/content";
    EXPECT_FALSE(NS::Core::FileSystem::ResolveUnder(base, "D:/evil/path").has_value());
    EXPECT_FALSE(NS::Core::FileSystem::ResolveUnder(base, "C:/content/Assets").has_value());
}

TEST(NsCoreFileSystem, ResolveUnderRejectsEscapeAboveBase)
{
    const std::filesystem::path base = "C:/content";
    EXPECT_FALSE(NS::Core::FileSystem::ResolveUnder(base, "../outside").has_value());
    EXPECT_FALSE(NS::Core::FileSystem::ResolveUnder(base, "a/../../outside").has_value());
    // ドライブ相対も base 配下を保証できないので拒否
    EXPECT_FALSE(NS::Core::FileSystem::ResolveUnder(base, "C:evil").has_value());
}

TEST(NsCoreFileSystem, ResolveUnderAllowsInternalDotDot)
{
    const std::filesystem::path base = "C:/content";
    const auto resolved = NS::Core::FileSystem::ResolveUnder(base, "a/../b");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, std::filesystem::path("C:/content/b"));
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

    const auto read = NS::Core::FileSystem::ReadAllBytes(path);
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

    const auto read = NS::Core::FileSystem::ReadAllText(path);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, original);

    std::filesystem::remove(path);
}

TEST_F(FileSystemLoggerTest, ReadAllBytesReturnsNulloptForMissingFile)
{
    const auto path = MakeTempPath("nonexistent.bin");
    const auto result = NS::Core::FileSystem::ReadAllBytes(path);
    EXPECT_FALSE(result.has_value());
}

TEST(NsCoreFileSystem, GetExeDirectoryReturnsExistingPath)
{
    const auto dir = NS::Core::FileSystem::GetExeDirectory();
    EXPECT_FALSE(dir.empty());
    EXPECT_TRUE(NS::Core::FileSystem::Exists(dir));
    EXPECT_TRUE(dir.is_absolute());
}

TEST(NsCoreFileSystem, ListFilesFiltersByExtension)
{
    const auto dir = MakeTempPath("listdir");
    ASSERT_TRUE(NS::Core::FileSystem::CreateDirectories(dir));

    const std::vector<std::byte> data = {std::byte{0x01}};
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(dir / "a.scene", data));
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(dir / "b.scene", data));
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(dir / "c.txt", data));

    const auto levels = NS::Core::FileSystem::ListFiles(dir, ".scene");
    EXPECT_EQ(levels.size(), 2u);
    for (const auto& p : levels)
        EXPECT_EQ(p.extension(), ".scene");

    const auto all = NS::Core::FileSystem::ListFiles(dir);
    EXPECT_EQ(all.size(), 3u);

    std::filesystem::remove_all(dir);
}

TEST_F(FileSystemLoggerTest, ListFilesReturnsEmptyForMissingDirectory)
{
    const auto dir = MakeTempPath("listdir_missing");
    const auto files = NS::Core::FileSystem::ListFiles(dir, ".scene");
    EXPECT_TRUE(files.empty());
}

TEST(NsCoreFileSystem, ListDirectoriesReturnsOnlySubdirectories)
{
    const auto root = MakeTempPath("listdirs");
    ASSERT_TRUE(NS::Core::FileSystem::CreateDirectories(root / "sub1"));
    ASSERT_TRUE(NS::Core::FileSystem::CreateDirectories(root / "sub2"));
    const std::vector<std::byte> data = {std::byte{0x01}};
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(root / "file.txt", data));

    const auto dirs = NS::Core::FileSystem::ListDirectories(root);
    EXPECT_EQ(dirs.size(), 2u);
    for (const auto& p : dirs)
        EXPECT_TRUE(std::filesystem::is_directory(p));

    std::filesystem::remove_all(root);
}

TEST_F(FileSystemLoggerTest, ListDirectoriesReturnsEmptyForMissingDirectory)
{
    const auto dir = MakeTempPath("listdirs_missing");
    EXPECT_TRUE(NS::Core::FileSystem::ListDirectories(dir).empty());
}

TEST(NsCoreFileSystem, IsDirectoryDistinguishesDirectoryFromFileAndMissing)
{
    const auto root = MakeTempPath("isdir");
    ASSERT_TRUE(NS::Core::FileSystem::CreateDirectories(root));
    const std::vector<std::byte> data = {std::byte{0x01}};
    ASSERT_TRUE(NS::Core::FileSystem::WriteAllBytes(root / "file.txt", data));

    EXPECT_TRUE(NS::Core::FileSystem::IsDirectory(root));
    // 通常ファイルと不存在はともに false。後者は Skybox が退避先へ落ちる経路を保証する
    EXPECT_FALSE(NS::Core::FileSystem::IsDirectory(root / "file.txt"));
    EXPECT_FALSE(NS::Core::FileSystem::IsDirectory(root / "missing"));

    std::filesystem::remove_all(root);
}
