#include <gtest/gtest.h>

#include <Runtime/Core/Filesystem.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Graphics/Skybox.h>
#include <Runtime/Platform/Window.h>

#include <d3d11.h>

#include <filesystem>

namespace
{
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::Skybox;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;

    WindowDesc MakeWindowDesc(const char* title)
    {
        WindowDesc d{};
        d.title = title;
        d.size = NS::Core::Size2D{320, 240};
        d.visible = false;
        return d;
    }

    RendererDesc MakeRendererDesc()
    {
        RendererDesc d{};
        d.vsync = false;
        d.enableDebugLayer = false;
        return d;
    }
} // namespace

class CubemapLoaderTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(CubemapLoaderTest, LoadKurt6FacePngSucceeds)
{
    Window window(MakeWindowDesc("ns_skybox_kurt"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::unique_ptr<Skybox> skyboxHolder = Skybox::Create();
    Skybox& skybox = *skyboxHolder;
    const auto exeDir = NS::Core::FileSystem::GetExeDirectory();
    const auto kurtDir = exeDir / "Assets" / "Skybox" / "kurt";

    if (!NS::Core::FileSystem::Exists(kurtDir / "space_ft.png"))
    {
        GTEST_SKIP() << "skybox kurt PNG 未配置 (exe 隣に Assets 未コピー)";
    }

    const bool ok = skybox.LoadCubemap(kurtDir);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(skybox.IsValid());
    EXPECT_FALSE(skybox.IsUsingFallback());
}

TEST_F(CubemapLoaderTest, LoadMissingPathFallsBack)
{
    Window window(MakeWindowDesc("ns_skybox_missing"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::unique_ptr<Skybox> skyboxHolder = Skybox::Create();
    Skybox& skybox = *skyboxHolder;
    const std::filesystem::path missing = "C:/__nonexistent_ns_skybox__";

    const bool ok = skybox.LoadCubemap(missing);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(skybox.IsUsingFallback());
    // fallback magenta cubemap が常に生成されているので SRV は非 null
    EXPECT_NE(skybox.Srv(), nullptr);
}

TEST_F(CubemapLoaderTest, LoadDdsCubemapReturnsTextureCubeDim)
{
    Window window(MakeWindowDesc("ns_skybox_dds"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::unique_ptr<Skybox> skyboxHolder = Skybox::Create();
    Skybox& skybox = *skyboxHolder;
    const auto exeDir = NS::Core::FileSystem::GetExeDirectory();
    const auto ddsPath = exeDir / "Assets" / "Skybox" / "kurt.dds";

    if (!NS::Core::FileSystem::Exists(ddsPath))
    {
        GTEST_SKIP() << "skybox .dds 未配置 (texassemble 生成は後続タスクで対応)";
    }

    const bool ok = skybox.LoadCubemap(ddsPath);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(skybox.IsValid());

    auto* srv = skybox.Srv();
    ASSERT_NE(srv, nullptr);
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    srv->GetDesc(&desc);
    EXPECT_EQ(desc.ViewDimension, D3D11_SRV_DIMENSION_TEXTURECUBE);
}

TEST_F(CubemapLoaderTest, ConstructedSkyboxHasFallbackSrv)
{
    Window window(MakeWindowDesc("ns_skybox_default_fallback"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    // LoadCubemap 呼び出し前の初期状態でも fallback magenta SRV が用意されており、
    // Render() を即時呼んでもクラッシュしないことを保証する
    std::unique_ptr<Skybox> skyboxHolder = Skybox::Create();
    Skybox& skybox = *skyboxHolder;
    EXPECT_TRUE(skybox.IsValid());
    EXPECT_TRUE(skybox.IsUsingFallback());
    EXPECT_NE(skybox.Srv(), nullptr);
}
