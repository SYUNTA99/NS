#include "Framework/Core/Logger.h"

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    // game.log と tests.log を物理分離するため、 Tests 側はここで logger 名を先に固定する
    // 各テスト fixture が SetUp で Logger::Init() を呼んでも、 g_initialized の guard で
    // 二度目以降は no-op、 logger 名は最初の Init で固定される
    ::NS::Core::Logger::SetLogName("tests");
    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    ::NS::Core::Logger::Shutdown();
    return result;
}
