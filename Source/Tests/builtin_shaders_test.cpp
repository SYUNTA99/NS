#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/Renderer.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/Window.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    bool WriteText(const std::string& path, const std::string& text)
    {
        return NS::OS::FileSystem::WriteAllBytes(path, std::as_bytes(std::span<const char>{text.data(), text.size()}));
    }

    std::array<NS::Gfx::D3DShader*, 14> AllShaders(const NS::Gfx::BuiltinShaders& shaders)
    {
        return {shaders.fullscreenVs,
                shaders.fadePs,
                shaders.screenRectVs,
                shaders.screenRectPs,
                shaders.skyboxVs,
                shaders.skyboxPs,
                shaders.debugLineVs,
                shaders.debugLinePs,
                shaders.bloomThresholdPs,
                shaders.bloomDownPs,
                shaders.bloomUpPs,
                shaders.bloomCompositePs,
                shaders.screenDistortionPs,
                shaders.bodyMaskPs};
    }

    std::vector<NS::uint8> DrawFullscreenAndReadCenter(NS::Gfx::Renderer& renderer)
    {
        renderer.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawFullscreenColor(NS::Color{1.0f, 1.0f, 1.0f, 1.0f});
        std::vector<NS::uint8> pixels;
        NS::Size2D size{};
        if (!renderer.ReadBackbufferPixels(pixels, size))
        {
            return {};
        }
        const std::size_t index =
            (static_cast<std::size_t>(size.height / 2) * static_cast<std::size_t>(size.width) +
             static_cast<std::size_t>(size.width / 2)) *
            4;
        return {pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
    }
} // namespace

TEST(BuiltinShaders, ReloadAllShadersReachesTheShadersThePassesDraw)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "描く段のシェーダーの試し", .size = {16, 16}, .visible = false});
    NS::Gfx::Renderer renderer(NS::Gfx::RendererDesc{}, window);
    ASSERT_TRUE(renderer.IsValid());

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("ns_builtin_shaders_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    ASSERT_TRUE(NS::OS::FileSystem::CreateDirectories(directory.generic_string()));
    std::filesystem::copy(std::filesystem::path{NS::OS::FileSystem::ContentRoot()} / "Shaders",
                          directory / "Shaders",
                          std::filesystem::copy_options::recursive,
                          error);
    ASSERT_FALSE(error) << error.message();
    const std::string fadePs = (directory / "Shaders" / "fade.ps.hlsl").generic_string();
    ASSERT_TRUE(WriteText(fadePs, "float4 PSMain() : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }"));

    {
        NS::AssetManager assets{directory.generic_string()};
        const NS::Gfx::BuiltinShaders shaders = assets.LoadBuiltinShaders();
        renderer.SetShaders(shaders);
        const std::vector<NS::uint8> red = DrawFullscreenAndReadCenter(renderer);
        ASSERT_EQ(red.size(), 4u);
        EXPECT_EQ(red[0], 0);
        EXPECT_EQ(red[1], 0);
        EXPECT_EQ(red[2], 255);

        ASSERT_TRUE(WriteText(fadePs, "float4 PSMain() : SV_Target { return float4(0.0, 1.0, 0.0, 1.0); }"));
        assets.ReloadAllShaders();
        const std::vector<NS::uint8> green = DrawFullscreenAndReadCenter(renderer);
        ASSERT_EQ(green.size(), 4u);
        EXPECT_EQ(green[0], 0);
        EXPECT_EQ(green[1], 255);
        EXPECT_EQ(green[2], 0);

        renderer.SetShaders(NS::Gfx::BuiltinShaders{});
    }

    std::filesystem::remove_all(directory, error);
}

TEST(BuiltinShaders, EachFileIsLoadedOnceAndSharedByThePasses)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "描く段のシェーダーの試し", .size = {16, 16}, .visible = false});
    NS::Gfx::Renderer renderer(NS::Gfx::RendererDesc{}, window);
    ASSERT_TRUE(renderer.IsValid());

    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::Gfx::BuiltinShaders first = assets.LoadBuiltinShaders();
    const NS::Gfx::BuiltinShaders second = assets.LoadBuiltinShaders();

    const std::array<NS::Gfx::D3DShader*, 14> firstShaders = AllShaders(first);
    const std::array<NS::Gfx::D3DShader*, 14> secondShaders = AllShaders(second);
    EXPECT_EQ(firstShaders, secondShaders);
    const std::set<NS::Gfx::D3DShader*> distinct(firstShaders.begin(), firstShaders.end());
    EXPECT_EQ(distinct.size(), 14u);
    EXPECT_EQ(distinct.count(nullptr), 0u);
    // 読み直せた本数で、AssetManager が持つシェーダーの数を見る
    // 綴りの違う道で同じ fade.vs.hlsl をもう 1 つ作ると 15 になる
    EXPECT_EQ(assets.ReloadAllShaders(), 14u);
}
