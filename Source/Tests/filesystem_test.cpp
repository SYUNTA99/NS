#include <Runtime/Platform/Filesystem.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Platform/StringUtils.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <windows.h>

namespace
{
    //! steady_clock のナノ秒と suffix から、他のテストと重複しない一時パスを作る
    std::string MakeTempPath(const std::string& suffix)
    {
        const auto ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        std::array<wchar_t, MAX_PATH> buffer{};
        ::GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
        const std::string tempDir = NS::Platform::StringUtils::Utf8FromWide(buffer.data());
        return NS::Platform::FileSystem::Combine(tempDir, "ns_fstest_" + std::to_string(ns) + "_" + suffix);
    }

    void RemoveAllForTest(const std::string& path)
    {
        for (const std::string& file : NS::Platform::FileSystem::ListFiles(path))
            ::DeleteFileW(NS::Platform::StringUtils::WideFromUtf8(file).c_str());
        for (const std::string& dir : NS::Platform::FileSystem::ListDirectories(path))
            RemoveAllForTest(dir);
        ::RemoveDirectoryW(NS::Platform::StringUtils::WideFromUtf8(path).c_str());
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
    EXPECT_FALSE(NS::Platform::FileSystem::Exists(p));
}

TEST(NsCoreFileSystem, CreateDirectoryThenExistsReturnsTrue)
{
    const auto root = MakeTempPath("dir");
    const auto dir = NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::Combine(root, "nested"), "deep");
    ASSERT_TRUE(NS::Platform::FileSystem::CreateDirectories(dir));
    EXPECT_TRUE(NS::Platform::FileSystem::Exists(dir));
    RemoveAllForTest(root);
}

TEST(NsCoreFileSystem, ResolveUnderReturnsAbsoluteInsideBase)
{
    const std::string base = "C:/content";
    const auto resolved = NS::Platform::FileSystem::ResolveUnder(base, "Assets/Skybox/kurt/");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, "C:/content/Assets/Skybox/kurt");
}

TEST(NsCoreFileSystem, ResolveUnderRejectsAbsolutePath)
{
    const std::string base = "C:/content";
    EXPECT_FALSE(NS::Platform::FileSystem::ResolveUnder(base, "D:/evil/path").has_value());
    EXPECT_FALSE(NS::Platform::FileSystem::ResolveUnder(base, "C:/content/Assets").has_value());
}

TEST(NsCoreFileSystem, ResolveUnderRejectsEscapeAboveBase)
{
    const std::string base = "C:/content";
    EXPECT_FALSE(NS::Platform::FileSystem::ResolveUnder(base, "../outside").has_value());
    EXPECT_FALSE(NS::Platform::FileSystem::ResolveUnder(base, "a/../../outside").has_value());
    // ドライブ相対も base 配下を保証できないので拒否
    EXPECT_FALSE(NS::Platform::FileSystem::ResolveUnder(base, "C:evil").has_value());
}

TEST(NsCoreFileSystem, ResolveUnderAllowsInternalDotDot)
{
    const std::string base = "C:/content";
    const auto resolved = NS::Platform::FileSystem::ResolveUnder(base, "a/../b");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, "C:/content/b");
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

    const auto read = NS::Platform::FileSystem::ReadAllBytes(path);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, original);

    ::DeleteFileW(NS::Platform::StringUtils::WideFromUtf8(path).c_str());
}

TEST(NsCoreFileSystem, WriteAndReadAllTextRoundTrip)
{
    const auto path = MakeTempPath("text.txt");
    const std::string original = "Hello, ファイル!\nLine 2";

    {
        std::ofstream out(path);
        out << original;
    }

    const auto read = NS::Platform::FileSystem::ReadAllText(path);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, original);

    ::DeleteFileW(NS::Platform::StringUtils::WideFromUtf8(path).c_str());
}

TEST_F(FileSystemLoggerTest, ReadAllBytesReturnsNulloptForMissingFile)
{
    const auto path = MakeTempPath("nonexistent.bin");
    const auto result = NS::Platform::FileSystem::ReadAllBytes(path);
    EXPECT_FALSE(result.has_value());
}

TEST(NsCoreFileSystem, GetExeDirectoryReturnsExistingPath)
{
    const auto dir = NS::Platform::FileSystem::GetExeDirectory();
    EXPECT_FALSE(dir.empty());
    EXPECT_TRUE(NS::Platform::FileSystem::Exists(dir));
    EXPECT_TRUE(dir.size() >= 2 && dir[1] == ':');
}

TEST(NsCoreFileSystem, ListFilesFiltersByExtension)
{
    const auto dir = MakeTempPath("listdir");
    ASSERT_TRUE(NS::Platform::FileSystem::CreateDirectories(dir));

    const std::vector<std::byte> data = {std::byte{0x01}};
    ASSERT_TRUE(NS::Platform::FileSystem::WriteAllBytes(NS::Platform::FileSystem::Combine(dir, "a.scene"), data));
    ASSERT_TRUE(NS::Platform::FileSystem::WriteAllBytes(NS::Platform::FileSystem::Combine(dir, "b.scene"), data));
    ASSERT_TRUE(NS::Platform::FileSystem::WriteAllBytes(NS::Platform::FileSystem::Combine(dir, "c.txt"), data));

    const auto levels = NS::Platform::FileSystem::ListFiles(dir, ".scene");
    EXPECT_EQ(levels.size(), 2u);
    for (const auto& p : levels)
        EXPECT_EQ(NS::Platform::FileSystem::Extension(p), ".scene");

    const auto all = NS::Platform::FileSystem::ListFiles(dir);
    EXPECT_EQ(all.size(), 3u);

    RemoveAllForTest(dir);
}

TEST_F(FileSystemLoggerTest, ListFilesReturnsEmptyForMissingDirectory)
{
    const auto dir = MakeTempPath("listdir_missing");
    const auto files = NS::Platform::FileSystem::ListFiles(dir, ".scene");
    EXPECT_TRUE(files.empty());
}

TEST(NsCoreFileSystem, ListFilesMatchesExtensionCaseInsensitive)
{
    const auto dir = MakeTempPath("listdir_case");
    ASSERT_TRUE(NS::Platform::FileSystem::CreateDirectories(dir));

    const std::vector<std::byte> data = {std::byte{0x01}};
    // Windows のファイルシステムは大文字小文字を区別しないので、列挙の絞り込みも区別しない
    ASSERT_TRUE(NS::Platform::FileSystem::WriteAllBytes(NS::Platform::FileSystem::Combine(dir, "upper.SCENE"), data));
    ASSERT_TRUE(NS::Platform::FileSystem::WriteAllBytes(NS::Platform::FileSystem::Combine(dir, "mixed.Scene"), data));

    EXPECT_EQ(NS::Platform::FileSystem::ListFiles(dir, ".scene").size(), 2u);
    EXPECT_EQ(NS::Platform::FileSystem::ListFilesRecursive(dir, ".scene").size(), 2u);

    RemoveAllForTest(dir);
}

TEST(NsCoreFileSystem, ListFilesRecursiveFindsNestedFiles)
{
    const auto root = MakeTempPath("recurdir");
    ASSERT_TRUE(NS::Platform::FileSystem::CreateDirectories(
        NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::Combine(root, "sub"), "deep")));

    const std::vector<std::byte> data = {std::byte{0x01}};
    ASSERT_TRUE(NS::Platform::FileSystem::WriteAllBytes(NS::Platform::FileSystem::Combine(root, "top.scene"), data));
    ASSERT_TRUE(NS::Platform::FileSystem::WriteAllBytes(
        NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::Combine(root, "sub"), "mid.scene"), data));
    ASSERT_TRUE(NS::Platform::FileSystem::WriteAllBytes(
        NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::Combine(root, "sub"), "deep"),
                                      "low.scene"),
        data));
    ASSERT_TRUE(NS::Platform::FileSystem::WriteAllBytes(
        NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::Combine(root, "sub"), "note.txt"), data));

    const auto scenes = NS::Platform::FileSystem::ListFilesRecursive(root, ".scene");
    EXPECT_EQ(scenes.size(), 3u);
    for (const auto& p : scenes)
        EXPECT_EQ(NS::Platform::FileSystem::Extension(p), ".scene");

    RemoveAllForTest(root);
}

TEST_F(FileSystemLoggerTest, ListFilesRecursiveReturnsEmptyForMissingDirectory)
{
    const auto dir = MakeTempPath("recurdir_missing");
    EXPECT_TRUE(NS::Platform::FileSystem::ListFilesRecursive(dir, ".scene").empty());
}

TEST(NsCoreFileSystem, ListDirectoriesReturnsOnlySubdirectories)
{
    const auto root = MakeTempPath("listdirs");
    ASSERT_TRUE(NS::Platform::FileSystem::CreateDirectories(NS::Platform::FileSystem::Combine(root, "sub1")));
    ASSERT_TRUE(NS::Platform::FileSystem::CreateDirectories(NS::Platform::FileSystem::Combine(root, "sub2")));
    const std::vector<std::byte> data = {std::byte{0x01}};
    ASSERT_TRUE(NS::Platform::FileSystem::WriteAllBytes(NS::Platform::FileSystem::Combine(root, "file.txt"), data));

    const auto dirs = NS::Platform::FileSystem::ListDirectories(root);
    EXPECT_EQ(dirs.size(), 2u);
    for (const auto& p : dirs)
        EXPECT_TRUE(NS::Platform::FileSystem::IsDirectory(p));

    RemoveAllForTest(root);
}

TEST_F(FileSystemLoggerTest, ListDirectoriesReturnsEmptyForMissingDirectory)
{
    const auto dir = MakeTempPath("listdirs_missing");
    EXPECT_TRUE(NS::Platform::FileSystem::ListDirectories(dir).empty());
}

TEST(NsCoreFileSystem, IsDirectoryDistinguishesDirectoryFromFileAndMissing)
{
    const auto root = MakeTempPath("isdir");
    ASSERT_TRUE(NS::Platform::FileSystem::CreateDirectories(root));
    const std::vector<std::byte> data = {std::byte{0x01}};
    ASSERT_TRUE(NS::Platform::FileSystem::WriteAllBytes(NS::Platform::FileSystem::Combine(root, "file.txt"), data));

    EXPECT_TRUE(NS::Platform::FileSystem::IsDirectory(root));
    // 通常ファイルと不存在はともに false。後者は Skybox がフォールバックへ落ちる経路
    EXPECT_FALSE(NS::Platform::FileSystem::IsDirectory(NS::Platform::FileSystem::Combine(root, "file.txt")));
    EXPECT_FALSE(NS::Platform::FileSystem::IsDirectory(NS::Platform::FileSystem::Combine(root, "missing")));

    RemoveAllForTest(root);
}

TEST(NsCoreFileSystemPath, CombineDoesNotDoubleTheSeparator)
{
    EXPECT_EQ(NS::Platform::FileSystem::Combine("Assets", "a.png"), "Assets/a.png");
    EXPECT_EQ(NS::Platform::FileSystem::Combine("Assets/", "a.png"), "Assets/a.png");
    EXPECT_EQ(NS::Platform::FileSystem::Combine("Assets\\", "a.png"), "Assets\\a.png");
    EXPECT_EQ(NS::Platform::FileSystem::Combine("", "a.png"), "a.png");
    EXPECT_EQ(NS::Platform::FileSystem::Combine("Assets", ""), "Assets/");
}

TEST(NsCoreFileSystemPath, CombineTakesTheRightSideWhenItIsAbsolute)
{
    // 右が絶対パスの時の扱いを operator/ に合わせる
    EXPECT_EQ(NS::Platform::FileSystem::Combine("Assets", "C:/tmp/a.png"), "C:/tmp/a.png");
    EXPECT_EQ(NS::Platform::FileSystem::Combine("Assets", "/a.png"), "/a.png");
}

TEST(NsCoreFileSystemPath, CombineMatchesTheStandardOperator)
{
    const std::vector<std::pair<std::string, std::string>> samples = {
        {"Assets", "a.png"},
        {"Assets/", "a.png"},
        {"Assets\\", "a.png"},
        {"", "a.png"},
        {"Assets", ""},
        {"Assets", "sub/b.png"},
        {"Assets", "C:/tmp/a.png"},
        {"Assets", "/a.png"},
        {"C:/NS", "C:x"},
        {"C:/NS", "D:/x"},
        {"C:/NS", "C:/NS/a"},
    };

    const auto toSlash = [](std::string text) {
        for (char& c : text)
        {
            if (c == '\\')
            {
                c = '/';
            }
        }
        return text;
    };

    for (const auto& [base, relative] : samples)
    {
        const std::filesystem::path expected = std::filesystem::path(base) / relative;
        EXPECT_EQ(toSlash(NS::Platform::FileSystem::Combine(base, relative)), toSlash(expected.string()))
            << base << " + " << relative;
    }
}

TEST(NsCoreFileSystemPath, PartsMatchTheStandardPath)
{
    // 標準とずれても例外は出ないので、同じ結果になることをここで試す
    const std::vector<std::string> samples = {
        "Assets/Models/Xbot.glb",
        "Assets/a.tar.gz",
        "Assets/.hidden",
        "a.png",
        "Assets/",
        "C:/x/y.z",
        "Assets/noext",
        "C:/y.z",
        "/a.png",
        "Assets\\b.png",
    };

    for (const std::string& sample : samples)
    {
        const std::filesystem::path expected = sample;
        EXPECT_EQ(NS::Platform::FileSystem::Extension(sample), expected.extension().string()) << sample;
        EXPECT_EQ(NS::Platform::FileSystem::FileName(sample), expected.filename().string()) << sample;
        EXPECT_EQ(NS::Platform::FileSystem::Stem(sample), expected.stem().string()) << sample;
        EXPECT_EQ(NS::Platform::FileSystem::ParentDirectory(sample), expected.parent_path().string()) << sample;
    }
}
