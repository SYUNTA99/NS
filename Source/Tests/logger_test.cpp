#include <Framework/Core/LogCategories.h>
#include <Framework/Core/Logger.h>

#include <gtest/gtest.h>

#include <magic_enum/magic_enum.hpp>

#include <string_view>

namespace
{

    class LoggerLifecycleTest : public ::testing::Test
    {
    protected:
        void SetUp() override { NS::Core::Logger::Init(); }
        void TearDown() override { NS::Core::Logger::Shutdown(); }
    };

    TEST(LoggerInit, MultipleInitsAreIdempotent)
    {
        NS::Core::Logger::Init();
        NS::Core::Logger::Init();
        NS::Core::Logger::Init();
        NS::Core::Logger::Shutdown();
    }

    TEST(LoggerInit, ShutdownWithoutInitIsSafe)
    {
        NS::Core::Logger::Shutdown();
        NS::Core::Logger::Shutdown();
    }

    TEST_F(LoggerLifecycleTest, BasicMacrosDoNotCrash)
    {
        NS_LOG_TRACE(NS::Core::LogCat::Core, "trace {}", 1);
        NS_LOG_DEBUG(NS::Core::LogCat::Core, "debug {}", 2);
        NS_LOG_INFO(NS::Core::LogCat::Core, "info {} {}", 3, "msg");
        NS_LOG_WARN(NS::Core::LogCat::Core, "warn {}", 4.5);
        NS_LOG_ERROR(NS::Core::LogCat::Core, "error {}", "string");
    }

    TEST_F(LoggerLifecycleTest, AllCategoriesProduceDistinctNames)
    {
        using ::NS::Core::LogCat;

        constexpr auto names = magic_enum::enum_names<LogCat>();
        EXPECT_EQ(names.size(), 5u);

        EXPECT_EQ(magic_enum::enum_name(LogCat::Core), "Core");
        EXPECT_EQ(magic_enum::enum_name(LogCat::Platform), "Platform");
        EXPECT_EQ(magic_enum::enum_name(LogCat::Graphics), "Graphics");
        EXPECT_EQ(magic_enum::enum_name(LogCat::App), "App");
        EXPECT_EQ(magic_enum::enum_name(LogCat::Game), "Game");
    }

    TEST(LoggerLevel, EnumValuesMatchSpec)
    {
        using ::NS::Core::LogLevel;
        EXPECT_EQ(static_cast<int>(LogLevel::Trace), 0);
        EXPECT_EQ(static_cast<int>(LogLevel::Debug), 1);
        EXPECT_EQ(static_cast<int>(LogLevel::Info), 2);
        EXPECT_EQ(static_cast<int>(LogLevel::Warn), 3);
        EXPECT_EQ(static_cast<int>(LogLevel::Error), 4);
        EXPECT_EQ(static_cast<int>(LogLevel::Fatal), 5);
    }

} // namespace
