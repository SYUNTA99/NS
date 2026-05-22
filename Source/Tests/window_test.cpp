#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Platform/Window.h>

namespace
{
    NS::Platform::WindowDesc MakeDesc(const char* title, int width = 320, int height = 240)
    {
        NS::Platform::WindowDesc d{};
        d.title = title;
        d.size = NS::Core::Size2D{width, height};
        d.visible = false;
        return d;
    }
} // namespace

class WindowLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(WindowLoggerTest, ConstructsAndDestructsCleanly)
{
    {
        NS::Platform::Window window(MakeDesc("ns_test_construct"));
        ASSERT_TRUE(window.IsValid());
        SUCCEED();
    }
}

TEST_F(WindowLoggerTest, SizeMatchesDesc)
{
    NS::Platform::Window window(MakeDesc("ns_test_size", 640, 480));
    ASSERT_TRUE(window.IsValid());
    EXPECT_EQ(window.Size(), (NS::Core::Size2D{640, 480}));
}

TEST_F(WindowLoggerTest, ShouldCloseIsFalseInitially)
{
    NS::Platform::Window window(MakeDesc("ns_test_should_close"));
    ASSERT_TRUE(window.IsValid());
    EXPECT_FALSE(window.ShouldClose());
}

TEST_F(WindowLoggerTest, RequestCloseSetsShouldClose)
{
    NS::Platform::Window window(MakeDesc("ns_test_request_close"));
    ASSERT_TRUE(window.IsValid());
    window.RequestClose();
    window.PollMessages();
    EXPECT_TRUE(window.ShouldClose());
}

TEST_F(WindowLoggerTest, NativeHandleNotNull)
{
    NS::Platform::Window window(MakeDesc("ns_test_handle"));
    ASSERT_TRUE(window.IsValid());
    EXPECT_NE(window.NativeHandle(), nullptr);
}

TEST_F(WindowLoggerTest, IsValidAfterSuccessfulConstruction)
{
    NS::Platform::Window window(MakeDesc("ns_test_is_valid"));
    EXPECT_TRUE(window.IsValid());
}
