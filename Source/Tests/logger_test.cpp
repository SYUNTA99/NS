#include "NSlib/Core/Logger.h"
#include "NSlib/Windows/Filesystem.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <share.h>
#include <string>
#include <system_error>

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
