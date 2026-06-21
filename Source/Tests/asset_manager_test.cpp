#include <gtest/gtest.h>

#include <Framework/Core/Filesystem.h>
#include <Framework/Core/Logger.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Platform/Window.h>
#include <Framework/Scene/AssetManager.h>

#include <filesystem>

namespace
{
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;
    using NS::Scene::AssetManager;

    WindowDesc MakeWindowDesc(const char* title)
    {
        WindowDesc d{};
        d.title = title;
        d.size = NS::Math::Size2D{320, 240};
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

    std::filesystem::path ShaderPath(const char* name)
    {
        return NS::Core::FileSystem::ContentRoot() / "Shaders" / name;
    }

    std::filesystem::path TexturePath(const char* name)
    {
        return NS::Core::FileSystem::ContentRoot() / "Assets" / "Textures" / name;
    }
} // namespace

class AssetManagerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

// 同一 path は dedupe され同一の非 null インスタンスが返る
TEST_F(AssetManagerTest, SamePathReturnsSamePointer)
{
    Window window(MakeWindowDesc("ns_am_dedup"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};

    auto* shaderA = am.GetOrLoadShader(ShaderPath("standard.vs.hlsl"));
    auto* shaderB = am.GetOrLoadShader(ShaderPath("standard.vs.hlsl"));
    ASSERT_NE(shaderA, nullptr);
    EXPECT_EQ(shaderA, shaderB);

    auto* texA = am.GetOrLoadTexture(TexturePath("cube_test.png"));
    auto* texB = am.GetOrLoadTexture(TexturePath("cube_test.png"));
    ASSERT_NE(texA, nullptr);
    EXPECT_EQ(texA, texB);
}

// builtin 名前鍵は同一の非 null StaticMesh を返し、 未登録名は nullptr
TEST_F(AssetManagerTest, BuiltinReturnsSameNonNullPointer)
{
    Window window(MakeWindowDesc("ns_am_builtin"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    am.RegisterBuiltins();

    EXPECT_NE(am.Builtin("cube"), nullptr);
    EXPECT_EQ(am.Builtin("cube"), am.Builtin("cube"));
    EXPECT_NE(am.Builtin("wedge45"), nullptr);
    EXPECT_NE(am.Builtin("pole"), nullptr);
    EXPECT_NE(am.Builtin("shadowQuad"), nullptr);
    EXPECT_EQ(am.Builtin("nonexistent"), nullptr);
}

// Reload は path 鍵の Shader を reload-in-place するのでキャッシュのポインタが不変
TEST_F(AssetManagerTest, ReloadShaderInPlaceKeepsIdentity)
{
    Window window(MakeWindowDesc("ns_am_reload"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    const auto path = ShaderPath("standard.vs.hlsl");
    auto* before = am.GetOrLoadShader(path);
    ASSERT_NE(before, nullptr);

    EXPECT_TRUE(am.Reload(path));
    EXPECT_EQ(am.GetOrLoadShader(path), before);
}

// 未キャッシュ path の Reload は false
TEST_F(AssetManagerTest, ReloadMissReturnsFalse)
{
    Window window(MakeWindowDesc("ns_am_reload_miss"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    EXPECT_FALSE(am.Reload("C:/nonexistent/__ns_am_missing.vs.hlsl"));
}
