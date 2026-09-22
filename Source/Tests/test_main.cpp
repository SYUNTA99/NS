#include "Runtime/Core/Logger.h"
#include "Runtime/Platform/Filesystem.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <objbase.h>

int main(int argc, char** argv)
{
    // WIC (PNG デコード) は COM ファクトリを要する
    // 明示初期化しないと CreateWICTextureFromMemoryEx が E_NOINTERFACE で落ちる
    const bool comReady = SUCCEEDED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    // game.log と tests.log を物理分離するため、Tests 側はここで先に Init して logger 名を固定する
    // 各テスト fixture が SetUp で Logger::Init() を呼んでも、g_initialized の guard で
    // 二度目以降は何もしない、logger 名は最初の Init で固定される
    ::NS::Core::LoggerDesc desc;
    desc.logName = "tests";
    desc.logDirectory = ::NS::Platform::FileSystem::Combine(::NS::Platform::FileSystem::ContentRoot(), "build");
    ::NS::Core::Logger::Init(desc);
    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    ::NS::Core::Logger::Shutdown();

    if (comReady)
    {
        ::CoUninitialize();
    }
    return result;
}
