#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/Render/Bloom.h"
#include "NSlib/Gfx/Render/BuiltinShaders.h"
#include "NSlib/Gfx/Render/ScreenOverlay.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/Window.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
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

    // バックバッファの中央のピクセルを青・緑・赤・不透明度の順で返す。読めなければ空
    std::vector<NS::uint8> ReadCenter(NS::Gfx::D3DDevice& device)
    {
        std::vector<NS::uint8> pixels;
        NS::Size2D size{};
        if (!device.ReadBackbufferPixels(pixels, size))
        {
            return {};
        }
        const std::size_t index =
            (static_cast<std::size_t>(size.height / 2) * static_cast<std::size_t>(size.width) +
             static_cast<std::size_t>(size.width / 2)) *
            4;
        return {pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
    }

    std::vector<NS::uint8> DrawFullscreenAndReadCenter(NS::Gfx::D3DDevice& device, NS::Gfx::ScreenOverlay& overlay)
    {
        device.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        overlay.DrawFullscreenColor(NS::Color{1.0f, 1.0f, 1.0f, 1.0f});
        return ReadCenter(device);
    }

    std::vector<NS::uint8> DrawBloomAndReadCenter(NS::Gfx::D3DDevice& device, NS::Gfx::Bloom& bloom)
    {
        device.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        bloom.BeginWorld(device, NS::Color{0.0f, 0.0f, 0.0f, 1.0f});
        bloom.EndWorld(device);
        return ReadCenter(device);
    }

    bool IsPink(const std::vector<NS::uint8>& bgra)
    {
        return bgra.size() == 4u && bgra[0] == 255 && bgra[1] == 0 && bgra[2] == 255;
    }

    // ContentRoot の Shaders を一時のフォルダへ写す。写せなければ空の道を返す
    std::filesystem::path CopyShadersToTemp()
    {
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() /
            ("ns_builtin_shaders_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!NS::OS::FileSystem::CreateDirectories(directory.generic_string()))
        {
            return {};
        }
        std::error_code error;
        std::filesystem::copy(std::filesystem::path{NS::OS::FileSystem::ContentRoot()} / "Shaders",
                              directory / "Shaders",
                              std::filesystem::copy_options::recursive,
                              error);
        if (error)
        {
            return {};
        }
        return directory;
    }

    constexpr const char* k_BrokenHlsl = "これは HLSL ではない";
} // namespace

TEST(BuiltinShaders, ReloadAllShadersReachesTheShadersThePassesDraw)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "描く段のシェーダーの試し", .size = {16, 16}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());

    const std::filesystem::path directory = CopyShadersToTemp();
    ASSERT_FALSE(directory.empty());
    const std::string fadePs = (directory / "Shaders" / "fade.ps.hlsl").generic_string();
    ASSERT_TRUE(WriteText(fadePs, "float4 PSMain() : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }"));

    {
        NS::AssetManager assets{directory.generic_string()};
        NS::Gfx::ScreenOverlay overlay(&device, assets.LoadBuiltinShaders());
        const std::vector<NS::uint8> red = DrawFullscreenAndReadCenter(device, overlay);
        ASSERT_EQ(red.size(), 4u);
        EXPECT_EQ(red[0], 0);
        EXPECT_EQ(red[1], 0);
        EXPECT_EQ(red[2], 255);

        ASSERT_TRUE(WriteText(fadePs, "float4 PSMain() : SV_Target { return float4(0.0, 1.0, 0.0, 1.0); }"));
        assets.ReloadAllShaders();
        const std::vector<NS::uint8> green = DrawFullscreenAndReadCenter(device, overlay);
        ASSERT_EQ(green.size(), 4u);
        EXPECT_EQ(green[0], 0);
        EXPECT_EQ(green[1], 255);
        EXPECT_EQ(green[2], 0);
    }

    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

TEST(BuiltinShaders, BloomDrawsPinkWhileCompositeFailsAndRecoversOnReload)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "描く段のシェーダーの試し", .size = {16, 16}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());

    const std::filesystem::path directory = CopyShadersToTemp();
    ASSERT_FALSE(directory.empty());
    const std::string compositePs = (directory / "Shaders" / "bloom_composite.ps.hlsl").generic_string();
    const std::optional<std::vector<std::byte>> original = NS::OS::FileSystem::ReadAllBytes(compositePs);
    ASSERT_TRUE(original.has_value());
    ASSERT_TRUE(WriteText(compositePs, k_BrokenHlsl));

    {
        NS::AssetManager assets{directory.generic_string()};
        NS::Gfx::Bloom bloom(NS::Gfx::BloomDesc{}, assets.LoadBuiltinShaders());
        EXPECT_TRUE(IsPink(DrawBloomAndReadCenter(device, bloom)));

        ASSERT_TRUE(NS::OS::FileSystem::WriteAllBytes(compositePs, *original));
        assets.ReloadAllShaders();
        const std::vector<NS::uint8> recovered = DrawBloomAndReadCenter(device, bloom);
        ASSERT_EQ(recovered.size(), 4u);
        EXPECT_FALSE(IsPink(recovered));
    }

    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

TEST(BuiltinShaders, FullscreenColorDrawsPinkWhileFadeFails)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "描く段のシェーダーの試し", .size = {16, 16}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());

    const std::filesystem::path directory = CopyShadersToTemp();
    ASSERT_FALSE(directory.empty());
    ASSERT_TRUE(WriteText((directory / "Shaders" / "fade.ps.hlsl").generic_string(), k_BrokenHlsl));

    {
        NS::AssetManager assets{directory.generic_string()};
        NS::Gfx::ScreenOverlay overlay(&device, assets.LoadBuiltinShaders());
        EXPECT_TRUE(IsPink(DrawFullscreenAndReadCenter(device, overlay)));
    }

    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

TEST(BuiltinShaders, EachFileIsLoadedOnceAndSharedByThePasses)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "描く段のシェーダーの試し", .size = {16, 16}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());

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
