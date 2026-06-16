#include "Editor/LevelFilePaths.h"

#include <string>

#include <gtest/gtest.h>

namespace EditorNs = NS::Editor;

TEST(LevelFilePaths, SanitizeAcceptsAlphanumeric)
{
    EXPECT_EQ(EditorNs::SanitizeLevelName("Hello"), "Hello");
    EXPECT_EQ(EditorNs::SanitizeLevelName("Level_01"), "Level_01");
    EXPECT_EQ(EditorNs::SanitizeLevelName("My Level"), "My Level");
    EXPECT_EQ(EditorNs::SanitizeLevelName("a-b-c"), "a-b-c");
}

TEST(LevelFilePaths, SanitizeRejectsDotDot)
{
    EXPECT_EQ(EditorNs::SanitizeLevelName(".."), "");
    EXPECT_EQ(EditorNs::SanitizeLevelName("../escape"), "");
    EXPECT_EQ(EditorNs::SanitizeLevelName("a..b"), "");
}

TEST(LevelFilePaths, SanitizeRejectsPathSeparators)
{
    EXPECT_EQ(EditorNs::SanitizeLevelName("a/b"), "");
    EXPECT_EQ(EditorNs::SanitizeLevelName("a\\b"), "");
    EXPECT_EQ(EditorNs::SanitizeLevelName("/etc/passwd"), "");
    EXPECT_EQ(EditorNs::SanitizeLevelName("C:\\Windows"), "");
}

TEST(LevelFilePaths, SanitizeRejectsReservedNames)
{
    EXPECT_EQ(EditorNs::SanitizeLevelName("CON"), "");
    EXPECT_EQ(EditorNs::SanitizeLevelName("con"), "");
    EXPECT_EQ(EditorNs::SanitizeLevelName("COM1"), "");
    EXPECT_EQ(EditorNs::SanitizeLevelName("NUL"), "");
    EXPECT_EQ(EditorNs::SanitizeLevelName("lpt9"), "");
}

TEST(LevelFilePaths, SanitizeRejectsEmpty)
{
    EXPECT_EQ(EditorNs::SanitizeLevelName(""), "");
}

TEST(LevelFilePaths, SanitizeRejectsTooLong)
{
    std::string longName(201, 'a');
    EXPECT_EQ(EditorNs::SanitizeLevelName(longName), "");
}

TEST(LevelFilePaths, SanitizeAcceptsBoundary200)
{
    std::string boundary(200, 'a');
    EXPECT_EQ(EditorNs::SanitizeLevelName(boundary), boundary);
}

TEST(LevelFilePaths, SanitizeRejectsLeadingTrailingSpace)
{
    EXPECT_EQ(EditorNs::SanitizeLevelName(" abc"), "");
    EXPECT_EQ(EditorNs::SanitizeLevelName("abc "), "");
}

TEST(LevelFilePaths, BuildLevelPathReturnsNulloptOnSanitizeFail)
{
    EXPECT_FALSE(EditorNs::BuildLevelPath("../escape").has_value());
    EXPECT_FALSE(EditorNs::BuildLevelPath("").has_value());
    EXPECT_FALSE(EditorNs::BuildLevelPath("CON").has_value());
}

TEST(LevelFilePaths, BuildLevelPathProducesExpectedShape)
{
    auto p = EditorNs::BuildLevelPath("MyLevel");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->extension(), ".nslvl");
    EXPECT_EQ(p->stem(), "MyLevel");
    EXPECT_EQ(p->parent_path().filename(), "Levels");
}

TEST(LevelFilePaths, EnsureDirectoryCreatesAndIsIdempotent)
{
    EXPECT_TRUE(EditorNs::EnsureLevelsDirectoryExists());
    EXPECT_TRUE(EditorNs::EnsureLevelsDirectoryExists());
}

TEST(LevelFilePaths, EnumerateReturnsSortedSafeNames)
{
    // 副作用: 既存 file の列挙のみ確認、 新 file は作らない
    auto names = EditorNs::EnumerateLevelFiles();
    for (std::size_t i = 1; i < names.size(); ++i)
        EXPECT_LE(names[i - 1], names[i]);
    for (const auto& n : names)
        EXPECT_FALSE(EditorNs::SanitizeLevelName(n).empty());
}
