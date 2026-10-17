#include "NSlib/CoreObject/Asset/Shader.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/Renderer.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/Window.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>

namespace
{
    bool WriteText(const std::string& path, const std::string& text)
    {
        return NS::OS::FileSystem::WriteAllBytes(path, std::as_bytes(std::span<const char>{text.data(), text.size()}));
    }
} // namespace

TEST(ShaderAsset, ReloadCompilesTheFileAtItsPath)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "シェーダーの読み直しの試し", .size = {64, 64}, .visible = false});
    NS::Gfx::Renderer renderer(NS::Gfx::RendererDesc{}, window);
    ASSERT_TRUE(renderer.IsValid());

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("ns_shader_asset_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ASSERT_TRUE(NS::OS::FileSystem::CreateDirectories(directory.generic_string()));
    const std::string path = (directory / "reload.ps.hlsl").generic_string();

    ASSERT_TRUE(WriteText(path, "壊れたシェーダー"));
    NS::Shader shader{path};
    EXPECT_EQ(shader.Path(), path);
    EXPECT_TRUE(shader.D3D()->IsUsingFallback());

    ASSERT_TRUE(WriteText(path, "float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }"));
    EXPECT_TRUE(shader.Reload());
    EXPECT_FALSE(shader.D3D()->IsUsingFallback());

    std::error_code error;
    std::filesystem::remove_all(directory, error);
}
