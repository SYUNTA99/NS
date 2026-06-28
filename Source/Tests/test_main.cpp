#include "Framework/Core/Logger.h"

#include "Framework/Framework.h"

#include <gtest/gtest.h>

#include <objbase.h>

int main(int argc, char** argv)
{
    // WIC (PNG デコード) は COM ファクトリを要する。 本番は他経路で COM が立つが、
    // テスト単体では明示初期化しないと CreateWICTextureFromMemoryEx が E_NOINTERFACE で落ちる
    const bool comReady = SUCCEEDED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    // game.log と tests.log を物理分離するため、 Tests 側はここで logger 名を先に固定する
    // 各テスト fixture が SetUp で Logger::Init() を呼んでも、 g_initialized の guard で
    // 二度目以降は何もしない、 logger 名は最初の Init で固定される
    ::NS::Core::Logger::SetLogName("tests");
    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    ::NS::Core::Logger::Shutdown();

    if (comReady)
    {
        ::CoUninitialize();
    }
    return result;
}
