#include <gtest/gtest.h>
#include <magic_enum/magic_enum.hpp>
#include <Runtime/Core/LogCategories.h>
#include <Runtime/Core/Logger.h>

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
        NS_LOG_TRACE(Core, "trace {}", 1);
        NS_LOG_DEBUG(Core, "debug {}", 2);
        NS_LOG_INFO(Core, "info {} {}", 3, "msg");
        NS_LOG_WARN(Core, "warn {}", 4.5);
        NS_LOG_ERROR(Core, "error {}", "string");
    }

    TEST_F(LoggerLifecycleTest, AllCategoriesProduceDistinctNames)
    {
        using ::NS::Core::LogCategory;

        constexpr auto names = magic_enum::enum_names<LogCategory>();
        EXPECT_EQ(names.size(), 8u);

        EXPECT_EQ(magic_enum::enum_name(LogCategory::Core), "Core");
        EXPECT_EQ(magic_enum::enum_name(LogCategory::Platform), "Platform");
        EXPECT_EQ(magic_enum::enum_name(LogCategory::Physics), "Physics");
        EXPECT_EQ(magic_enum::enum_name(LogCategory::Graphics), "Graphics");
        EXPECT_EQ(magic_enum::enum_name(LogCategory::Scene), "Scene");
        EXPECT_EQ(magic_enum::enum_name(LogCategory::App), "App");
        EXPECT_EQ(magic_enum::enum_name(LogCategory::Game), "Game");
        EXPECT_EQ(magic_enum::enum_name(LogCategory::UI), "UI");
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
