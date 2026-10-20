#include "NSlib/Windows/Filesystem.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

using NS::OS::FileSystem;

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
