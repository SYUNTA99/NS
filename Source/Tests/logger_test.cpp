#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Windows/Filesystem.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <share.h>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    std::filesystem::path TestLogDirectory(const wchar_t* testName)
    {
        wchar_t temp[MAX_PATH] = {};
        ::GetTempPathW(MAX_PATH, temp);
        const std::filesystem::path dir = std::filesystem::path(temp) / L"ns_logger_test" / testName;
        std::error_code error;
        std::filesystem::remove_all(dir, error);
        std::filesystem::create_directories(dir / "logs");
        return dir;
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string u8 = path.generic_u8string();
        return std::string(u8.begin(), u8.end());
    }

    // test_main と同じ設定で、試しの後のログを tests.log へ戻す
    void RestoreTestsLogger()
    {
        ::NS::Logger::Shutdown();
        ::NS::LoggerDesc desc;
        desc.logName = "tests";
        desc.logDirectory = ::NS::OS::FileSystem::Combine(::NS::OS::FileSystem::ContentRoot(), "build");
        ::NS::Logger::Init(desc);
    }
} // namespace

// 別のプログラム (Game と Editor の片方) がログを開いたままだと名前の変更が失敗する。その時も終了しない
TEST(Logger, InitKeepsRunningWhenLogIsHeldByAnotherProgram)
{
    const std::filesystem::path dir = TestLogDirectory(L"held");
    const std::filesystem::path logPath = dir / "logs" / "held.log";
    {
        std::ofstream old(logPath, std::ios::trunc);
        old << "old\n";
    }
    // spdlog が開くのと同じ共有の形 (名前の変更を許さない) で開いておく
    FILE* held = ::_wfsopen(logPath.c_str(), L"ab", _SH_DENYNO);
    ASSERT_NE(held, nullptr);

    ::NS::Logger::Shutdown();
    ::NS::LoggerDesc desc;
    desc.logName = "held";
    desc.rotateOnOpen = true;
    desc.logDirectory = ToUtf8(dir);
    ::NS::Logger::Init(desc);
    NS_LOG_INFO(App, "held");
    ::NS::Logger::Shutdown();

    std::fclose(held);
    RestoreTestsLogger();
    EXPECT_TRUE(std::filesystem::exists(logPath));
    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

TEST(Logger, RotateOnOpenMovesPreviousLogToBackup)
{
    const std::filesystem::path dir = TestLogDirectory(L"rotate");
    const std::filesystem::path logPath = dir / "logs" / "rotate.log";
    const std::filesystem::path backupPath = dir / "logs" / "rotate.1.log";
    {
        std::ofstream old(logPath, std::ios::trunc);
        old << "previous run\n";
    }

    ::NS::Logger::Shutdown();
    ::NS::LoggerDesc desc;
    desc.logName = "rotate";
    desc.rotateOnOpen = true;
    desc.logDirectory = ToUtf8(dir);
    ::NS::Logger::Init(desc);
    ::NS::Logger::Shutdown();
    RestoreTestsLogger();

    std::string line;
    {
        std::ifstream backup(backupPath);
        std::getline(backup, line);
    }
    EXPECT_EQ(line, "previous run");
    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

TEST(Logger, InitCreatesMissingNestedDirectories)
{
    const std::filesystem::path dir = TestLogDirectory(L"nested");
    const std::filesystem::path logPath = dir / "a" / "b" / "logs" / "nested.log";

    ::NS::Logger::Shutdown();
    ::NS::LoggerDesc desc;
    desc.logName = "nested";
    desc.logDirectory = ToUtf8(dir / "a" / "b");
    ::NS::Logger::Init(desc);
    NS_LOG_INFO(App, "nested");
    ::NS::Logger::Shutdown();
    RestoreTestsLogger();

    const std::optional<std::string> text = ::NS::OS::FileSystem::ReadAllText(ToUtf8(logPath));
    ASSERT_TRUE(text.has_value());
    EXPECT_NE(text->find("nested"), std::string::npos);
    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

// ログのファイルの場所にフォルダがあって開けない時も、ファイルへ出さずに続ける
TEST(Logger, InitKeepsRunningWhenLogFileCannotBeOpened)
{
    const std::filesystem::path dir = TestLogDirectory(L"blocked");
    const std::filesystem::path logPath = dir / "logs" / "blocked.log";
    std::filesystem::create_directories(logPath);

    ::NS::Logger::Shutdown();
    ::NS::LoggerDesc desc;
    desc.logName = "blocked";
    desc.logDirectory = ToUtf8(dir);
    ::NS::Logger::Init(desc);
    NS_LOG_INFO(App, "blocked");
    ::NS::Logger::Shutdown();
    RestoreTestsLogger();

    EXPECT_TRUE(std::filesystem::is_directory(logPath));
    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

// エディタのコンソールに出すため、WARN 以上だけを溜め、新しい順に返す
TEST(Logger, RecentWarningsKeepsWarnAndAboveNewestFirst)
{
    const std::filesystem::path dir = TestLogDirectory(L"recent");
    ::NS::Logger::Shutdown();
    ::NS::LoggerDesc desc;
    desc.logName = "recent";
    desc.logDirectory = ToUtf8(dir);
    ::NS::Logger::Init(desc);
    NS_LOG_INFO(App, "info の行");
    NS_LOG_WARN(App, "warn の行");
    NS_LOG_ERROR(App, "error の行");
    const std::vector<::NS::RecentLog> recent = ::NS::Logger::RecentWarnings();
    ::NS::Logger::Shutdown();
    RestoreTestsLogger();

    ASSERT_EQ(recent.size(), 2u);
    EXPECT_EQ(recent[0].level, ::NS::LogLevel::Error);
    EXPECT_NE(recent[0].text.find("error の行"), std::string::npos) << recent[0].text;
    EXPECT_EQ(recent[1].level, ::NS::LogLevel::Warn);
    EXPECT_NE(recent[1].text.find("warn の行"), std::string::npos) << recent[1].text;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

// 溜める数を越えたら古い物から消える
TEST(Logger, RecentWarningsDropsOldestBeyondCapacity)
{
    const std::filesystem::path dir = TestLogDirectory(L"recent_capacity");
    ::NS::Logger::Shutdown();
    ::NS::LoggerDesc desc;
    desc.logName = "recent_capacity";
    desc.logDirectory = ToUtf8(dir);
    ::NS::Logger::Init(desc);
    const std::size_t capacity = ::NS::Logger::k_RecentLogCapacity;
    for (std::size_t i = 0; i <= capacity; ++i)
    {
        NS_LOG_WARN(App, "警告 #{}.", i);
    }
    const std::vector<::NS::RecentLog> recent = ::NS::Logger::RecentWarnings();
    ::NS::Logger::Shutdown();
    RestoreTestsLogger();

    ASSERT_EQ(recent.size(), capacity);
    EXPECT_NE(recent.front().text.find(std::format("警告 #{}.", capacity)), std::string::npos) << recent.front().text;
    EXPECT_NE(recent.back().text.find("警告 #1."), std::string::npos) << recent.back().text;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
}
