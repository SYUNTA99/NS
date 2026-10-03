#include "Runtime/Platform/FileSystem.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

// impact-feel-pass R-8-5: 溜めの土の粉を、狙いの向きの後ろへ飛ばす
// 定義の前後は読み込みで反転されるので、定義のプラスはゲームでは狙いの向きの後ろ、マイナスは前
// (Effekseer が左手系の読み込みで前後の符号を反転し、その後で ChargeEffects::YawToward が前を狙いの向きへ回す)
// 粒の位置を描いて読む試しが無いので、定義の前後の値を読む。書き出した絵が定義と同じかは efkbuild.py の照合が縛る

namespace
{
    using NS::Platform::FileSystem;

    std::string ReadGrindDefinition()
    {
        const std::string path = FileSystem::Combine(
            FileSystem::Combine(FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "Tools"), "effects"),
                                "defs"),
            "charge");
        const std::optional<std::string> text =
            FileSystem::ReadAllText(FileSystem::Combine(path, "charge.grind.efkproj"));
        if (!text.has_value())
        {
            return std::string{};
        }
        return *text;
    }

    // text の [begin, end) で、open の後の最初の <Z> の中の tag の値を読む。見つからなければ空
    std::optional<float> ValueAfter(
        const std::string& text, std::size_t begin, std::size_t end, std::string_view open, std::string_view tag)
    {
        const std::size_t openAt = text.find(open, begin);
        if (openAt == std::string::npos || openAt >= end)
        {
            return std::nullopt;
        }
        const std::size_t zAt = text.find("<Z>", openAt);
        if (zAt == std::string::npos || zAt >= end)
        {
            return std::nullopt;
        }
        const std::string start = "<" + std::string{tag} + ">";
        const std::size_t valueAt = text.find(start, zAt);
        if (valueAt == std::string::npos || valueAt >= end)
        {
            return std::nullopt;
        }
        return std::stof(text.substr(valueAt + start.size()));
    }
} // namespace

// 左右 6 つの節の生む位置と飛ぶ速さが、定義の前後でプラス (ゲームでは狙いの向きの後ろ)
TEST(ChargeGrindDirection, DustAndGritFlyBehindTheAim)
{
    const std::string text = ReadGrindDefinition();
    ASSERT_FALSE(text.empty());
    for (const char* name : {"DustLeft", "DustKickLeft", "GritLeft", "DustRight", "DustKickRight", "GritRight"})
    {
        SCOPED_TRACE(name);
        const std::size_t nodeAt = text.find("<Name>" + std::string{name} + "</Name>");
        ASSERT_NE(nodeAt, std::string::npos);
        std::size_t nodeEnd = text.find("<Name>", nodeAt + 1);
        if (nodeEnd == std::string::npos)
        {
            nodeEnd = text.size();
        }
        const std::size_t valuesAt = text.find("<LocationValues>", nodeAt);
        ASSERT_LT(valuesAt, nodeEnd);
        const std::optional<float> location = ValueAfter(text, valuesAt, nodeEnd, "<Location>", "Center");
        const std::optional<float> slowest = ValueAfter(text, valuesAt, nodeEnd, "<Velocity>", "Min");
        ASSERT_TRUE(location.has_value());
        ASSERT_TRUE(slowest.has_value());
        EXPECT_GT(*location, 0.0f);
        EXPECT_GT(*slowest, 0.0f);
    }
}
