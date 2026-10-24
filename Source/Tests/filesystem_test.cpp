#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Types.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

using NS::OS::FileSystem;

namespace
{
    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string u8 = path.generic_u8string();
        return std::string(u8.begin(), u8.end());
    }

    std::span<const std::byte> AsBytes(std::string_view text)
    {
        return std::as_bytes(std::span<const char>(text.data(), text.size()));
    }

    // テストごとに空のフォルダを作り、終わったら消す
    class FileSystemFiles : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_dir = std::filesystem::temp_directory_path() / "ns_filesystem_test" /
                    ::testing::UnitTest::GetInstance()->current_test_info()->name();
            std::error_code error;
            std::filesystem::remove_all(m_dir, error);
            std::filesystem::create_directories(m_dir);
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(m_dir, error);
        }

        [[nodiscard]] std::string PathOf(std::string_view name) const { return ToUtf8(m_dir / name); }

        std::filesystem::path m_dir;
    };
} // namespace

TEST_F(FileSystemFiles, RenameFileReplacesTheTarget)
{
    ASSERT_TRUE(FileSystem::WriteAllBytes(PathOf("from.txt"), AsBytes("new")));
    ASSERT_TRUE(FileSystem::WriteAllBytes(PathOf("to.txt"), AsBytes("old")));

    EXPECT_TRUE(FileSystem::RenameFile(PathOf("from.txt"), PathOf("to.txt")));
    EXPECT_FALSE(FileSystem::Exists(PathOf("from.txt")));
    EXPECT_EQ(FileSystem::ReadAllText(PathOf("to.txt")), std::optional<std::string>{"new"});
}

TEST_F(FileSystemFiles, RenameFileFailsWhenTheSourceIsMissing)
{
    EXPECT_FALSE(FileSystem::RenameFile(PathOf("missing.txt"), PathOf("to.txt")));
    EXPECT_FALSE(FileSystem::Exists(PathOf("to.txt")));
}

TEST_F(FileSystemFiles, FileSizeReturnsTheByteCount)
{
    ASSERT_TRUE(FileSystem::WriteAllBytes(PathOf("five.txt"), AsBytes("12345")));

    EXPECT_EQ(FileSystem::FileSize(PathOf("five.txt")), std::optional<NS::uint64>{5});
    EXPECT_EQ(FileSystem::FileSize(PathOf("missing.txt")), std::nullopt);
}

TEST_F(FileSystemFiles, ReplaceAllBytesReplacesTheContentsAndLeavesNoTemporary)
{
    ASSERT_TRUE(FileSystem::WriteAllBytes(PathOf("data.txt"), AsBytes("old")));

    EXPECT_TRUE(FileSystem::ReplaceAllBytes(PathOf("data.txt"), AsBytes("new")));
    EXPECT_EQ(FileSystem::ReadAllText(PathOf("data.txt")), std::optional<std::string>{"new"});
    EXPECT_FALSE(FileSystem::Exists(PathOf("data.txt.tmp")));
}

TEST_F(FileSystemFiles, FileAppenderAddsToTheEndOfTheFile)
{
    ASSERT_TRUE(FileSystem::WriteAllBytes(PathOf("log.txt"), AsBytes("old")));
    {
        NS::OS::FileAppender appender(PathOf("log.txt"));
        ASSERT_TRUE(appender.IsValid());
        EXPECT_TRUE(appender.Append(AsBytes("new")));
    }
    EXPECT_EQ(FileSystem::ReadAllText(PathOf("log.txt")), std::optional<std::string>{"oldnew"});
}

TEST_F(FileSystemFiles, FileAppenderWritesWhileAnotherHandleHasTheFileOpen)
{
    {
        NS::OS::FileAppender first(PathOf("log.txt"));
        NS::OS::FileAppender second(PathOf("log.txt"));
        ASSERT_TRUE(first.IsValid());
        ASSERT_TRUE(second.IsValid());
        EXPECT_TRUE(first.Append(AsBytes("a")));
        EXPECT_TRUE(second.Append(AsBytes("b")));
    }
    EXPECT_EQ(FileSystem::ReadAllText(PathOf("log.txt")), std::optional<std::string>{"ab"});
}

TEST_F(FileSystemFiles, FileAppenderIsInvalidWhenThePathCannotBeOpened)
{
    NS::OS::FileAppender appender(PathOf("missing/log.txt"));
    EXPECT_FALSE(appender.IsValid());
    EXPECT_FALSE(appender.Append(AsBytes("x")));
}

TEST(FileSystemRelativeUnder, ReturnsThePathBelowTheBase)
{
    EXPECT_EQ(FileSystem::RelativeUnder("C:/NS", "C:/NS/Assets/Materials/stone.mat"),
              std::optional<std::string>{"Assets/Materials/stone.mat"});
    // 大文字と小文字だけが違う根でも、根の下の道として切る。返す相対は道の側の綴りのまま
    EXPECT_EQ(FileSystem::RelativeUnder("c:/ns", "C:/NS/Assets/X.MAT"),
              std::optional<std::string>{"Assets/X.MAT"});
    // 区切りが \ と / で混ざっても同じ答えになる
    EXPECT_EQ(FileSystem::RelativeUnder("C:\\NS\\", "C:/NS\\Assets/Materials\\stone.mat"),
              std::optional<std::string>{"Assets/Materials/stone.mat"});
    // ドライブの根は区切りで終わるので、続く区切りを求めない
    EXPECT_EQ(FileSystem::RelativeUnder("C:/", "C:/Assets/a.mat"), std::optional<std::string>{"Assets/a.mat"});
}

TEST(FileSystemRelativeUnder, RejectsTheBaseItselfAndPathsOutside)
{
    EXPECT_EQ(FileSystem::RelativeUnder("C:/NS", "C:/NS"), std::nullopt);
    EXPECT_EQ(FileSystem::RelativeUnder("C:/NS", "c:\\ns\\"), std::nullopt);
    EXPECT_EQ(FileSystem::RelativeUnder("C:/NS", "C:/Other/a.mat"), std::nullopt);
    // 根の名前を頭に持つ隣のフォルダは、根の下ではない
    EXPECT_EQ(FileSystem::RelativeUnder("C:/NS", "C:/NSEvil/a.mat"), std::nullopt);
    EXPECT_EQ(FileSystem::RelativeUnder("C:/NS", "C:/NS/Assets/../../Other/a.mat"), std::nullopt);
    EXPECT_EQ(FileSystem::RelativeUnder("C:/NS", "Assets/a.mat"), std::nullopt);
}
