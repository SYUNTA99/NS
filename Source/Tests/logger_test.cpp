#include <ns/core/log_categories.h>
#include <ns/core/logger.h>

#include <gtest/gtest.h>

#include <magic_enum/magic_enum.hpp>

#include <string_view>

namespace
{

    class LoggerLifecycleTest : public ::testing::Test
    {
    protected:
        void SetUp() override { ns::core::Logger::Init(); }
        void TearDown() override { ns::core::Logger::Shutdown(); }
    };

    TEST(LoggerInit, MultipleInitsAreIdempotent)
    {
        ns::core::Logger::Init();
        ns::core::Logger::Init();
        ns::core::Logger::Init();
        ns::core::Logger::Shutdown();
    }

    TEST(LoggerInit, ShutdownWithoutInitIsSafe)
    {
        ns::core::Logger::Shutdown();
        ns::core::Logger::Shutdown();
    }

    TEST_F(LoggerLifecycleTest, BasicMacrosDoNotCrash)
    {
        NS_LOG_TRACE(ns::core::LogCat::Core, "trace {}", 1);
        NS_LOG_DEBUG(ns::core::LogCat::Core, "debug {}", 2);
        NS_LOG_INFO(ns::core::LogCat::Core, "info {} {}", 3, "msg");
        NS_LOG_WARN(ns::core::LogCat::Core, "warn {}", 4.5);
        NS_LOG_ERROR(ns::core::LogCat::Core, "error {}", "string");
    }

    TEST_F(LoggerLifecycleTest, AllCategoriesProduceDistinctNames)
    {
        using ::ns::core::LogCat;

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
        using ::ns::core::LogLevel;
        EXPECT_EQ(static_cast<int>(LogLevel::Trace), 0);
        EXPECT_EQ(static_cast<int>(LogLevel::Debug), 1);
        EXPECT_EQ(static_cast<int>(LogLevel::Info), 2);
        EXPECT_EQ(static_cast<int>(LogLevel::Warn), 3);
        EXPECT_EQ(static_cast<int>(LogLevel::Error), 4);
        EXPECT_EQ(static_cast<int>(LogLevel::Fatal), 5);
    }

} // namespace
