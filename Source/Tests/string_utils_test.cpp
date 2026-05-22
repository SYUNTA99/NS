#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Core/StringUtils.h>

class StringUtilsLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST(NsCoreStringUtils, RoundTripAscii)
{
    const std::string original = "Hello, world!";
    const auto wide = NS::Core::StringUtils::WideFromUtf8(original);
    const auto back = NS::Core::StringUtils::Utf8FromWide(wide);
    EXPECT_EQ(back, original);
}

TEST(NsCoreStringUtils, RoundTripJapanese)
{
    const std::string original = "日本語テスト";
    const auto wide = NS::Core::StringUtils::WideFromUtf8(original);
    const auto back = NS::Core::StringUtils::Utf8FromWide(wide);
    EXPECT_EQ(back, original);
}

TEST(NsCoreStringUtils, RoundTripEmoji)
{
    const std::string original = "🎮🚀";
    const auto wide = NS::Core::StringUtils::WideFromUtf8(original);
    const auto back = NS::Core::StringUtils::Utf8FromWide(wide);
    EXPECT_EQ(back, original);
}

TEST(NsCoreStringUtils, RoundTripEmpty)
{
    const auto wide = NS::Core::StringUtils::WideFromUtf8("");
    EXPECT_TRUE(wide.empty());

    const auto back = NS::Core::StringUtils::Utf8FromWide(L"");
    EXPECT_TRUE(back.empty());
}

TEST_F(StringUtilsLoggerTest, InvalidUtf8ReturnsEmpty)
{
    // 孤立した continuation byte と範囲外 lead byte の組合せ
    const std::string invalid = "\xFF\xFE\x80";
    const auto wide = NS::Core::StringUtils::WideFromUtf8(invalid);
    EXPECT_TRUE(wide.empty());
}
