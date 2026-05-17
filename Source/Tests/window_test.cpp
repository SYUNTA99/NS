#include <gtest/gtest.h>

#include <ns/core/logger.h>
#include <ns/platform/window.h>

class WindowLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { ns::core::Logger::Init(); }
    void TearDown() override { ns::core::Logger::Shutdown(); }
};

TEST_F(WindowLoggerTest, ConstructsAndDestructsCleanly)
{
    ns::platform::WindowDesc desc{};
    desc.title = "ns_test_construct";
    desc.width = 320;
    desc.height = 240;

    {
        ns::platform::Window window(desc);
        SUCCEED();
    }
}

TEST_F(WindowLoggerTest, WidthHeightMatchesDesc)
{
    ns::platform::WindowDesc desc{};
    desc.title = "ns_test_size";
    desc.width = 640;
    desc.height = 480;

    ns::platform::Window window(desc);
    EXPECT_EQ(window.Width(), 640);
    EXPECT_EQ(window.Height(), 480);
}

TEST_F(WindowLoggerTest, ShouldCloseIsFalseInitially)
{
    ns::platform::WindowDesc desc{};
    desc.title = "ns_test_should_close";

    ns::platform::Window window(desc);
    EXPECT_FALSE(window.ShouldClose());
}

TEST_F(WindowLoggerTest, RequestCloseSetsShouldClose)
{
    ns::platform::WindowDesc desc{};
    desc.title = "ns_test_request_close";

    ns::platform::Window window(desc);
    window.RequestClose();
    window.PollMessages();
    EXPECT_TRUE(window.ShouldClose());
}

TEST_F(WindowLoggerTest, NativeHandleNotNull)
{
    ns::platform::WindowDesc desc{};
    desc.title = "ns_test_handle";

    ns::platform::Window window(desc);
    EXPECT_NE(window.NativeHandle(), nullptr);
}

TEST_F(WindowLoggerTest, IsValidAfterSuccessfulConstruction)
{
    ns::platform::WindowDesc desc{};
    desc.title = "ns_test_is_valid";

    ns::platform::Window window(desc);
    EXPECT_TRUE(window.IsValid());
}
