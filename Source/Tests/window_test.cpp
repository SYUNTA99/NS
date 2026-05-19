#include <gtest/gtest.h>

#include <ns/core/logger.h>
#include <ns/platform/window.h>

namespace
{
    ns::platform::WindowDesc MakeDesc(const char* title, int width = 320, int height = 240)
    {
        ns::platform::WindowDesc d{};
        d.title = title;
        d.width = width;
        d.height = height;
        d.visible = false;
        return d;
    }
} // namespace

class WindowLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { ns::core::Logger::Init(); }
    void TearDown() override { ns::core::Logger::Shutdown(); }
};

TEST_F(WindowLoggerTest, ConstructsAndDestructsCleanly)
{
    {
        ns::platform::Window window(MakeDesc("ns_test_construct"));
        ASSERT_TRUE(window.IsValid());
        SUCCEED();
    }
}

TEST_F(WindowLoggerTest, WidthHeightMatchesDesc)
{
    ns::platform::Window window(MakeDesc("ns_test_size", 640, 480));
    ASSERT_TRUE(window.IsValid());
    EXPECT_EQ(window.Width(), 640);
    EXPECT_EQ(window.Height(), 480);
}

TEST_F(WindowLoggerTest, ShouldCloseIsFalseInitially)
{
    ns::platform::Window window(MakeDesc("ns_test_should_close"));
    ASSERT_TRUE(window.IsValid());
    EXPECT_FALSE(window.ShouldClose());
}

TEST_F(WindowLoggerTest, RequestCloseSetsShouldClose)
{
    ns::platform::Window window(MakeDesc("ns_test_request_close"));
    ASSERT_TRUE(window.IsValid());
    window.RequestClose();
    window.PollMessages();
    EXPECT_TRUE(window.ShouldClose());
}

TEST_F(WindowLoggerTest, NativeHandleNotNull)
{
    ns::platform::Window window(MakeDesc("ns_test_handle"));
    ASSERT_TRUE(window.IsValid());
    EXPECT_NE(window.NativeHandle(), nullptr);
}

TEST_F(WindowLoggerTest, IsValidAfterSuccessfulConstruction)
{
    ns::platform::Window window(MakeDesc("ns_test_is_valid"));
    EXPECT_TRUE(window.IsValid());
}
