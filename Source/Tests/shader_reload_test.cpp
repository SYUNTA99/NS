#include <Runtime/Core/Filesystem.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Graphics/Shader.h>
#include <Runtime/Platform/Window.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>

namespace
{
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::Shader;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;

    // 存在しない .ps. パスは magenta フォールバックシェーダになる。ステージはファイル名で判定する
    constexpr const char* k_FallbackPsPath = "C:/nonexistent/__ns_reload_fallback.ps.hlsl";

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

    std::filesystem::path ShaderPath(const char* name)
    {
        return NS::Core::FileSystem::ContentRoot() / "Shaders" / name;
    }
} // namespace

class ShaderReloadTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

// 稼働中の Shader を同じ path から再コンパイルしても Shader* のアドレスが変わらない
TEST_F(ShaderReloadTest, ReloadKeepsPointerIdentity)
{
    Window window(MakeWindowDesc("ns_reload_identity"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    std::unique_ptr<Shader> holder = Shader::Create(ShaderPath("standard.vs.hlsl"));
    Shader* before = holder.get();
    ASSERT_TRUE(holder->IsValid());

    EXPECT_TRUE(holder->Reload());
    EXPECT_EQ(holder.get(), before) << "Reload で Shader* のアドレスが変わった";
    EXPECT_TRUE(holder->IsValid());
}

// 再コンパイルが成功すると IsValid のまま fallback でない実シェーダで描画される
TEST_F(ShaderReloadTest, ReloadSucceedsForValidShader)
{
    Window window(MakeWindowDesc("ns_reload_valid"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    std::unique_ptr<Shader> vs = Shader::Create(ShaderPath("standard.vs.hlsl"));
    std::unique_ptr<Shader> ps = Shader::Create(ShaderPath("player.ps.hlsl"));
    ASSERT_TRUE(vs->IsValid());
    ASSERT_TRUE(ps->IsValid());

    EXPECT_TRUE(vs->Reload());
    EXPECT_TRUE(ps->Reload());
    EXPECT_TRUE(vs->IsValid());
    EXPECT_TRUE(ps->IsValid());
}

// 再コンパイル失敗 (読込不可) では旧 GPU リソースを保持し false を返す。編集中の打ち間違いで画面を壊さない
TEST_F(ShaderReloadTest, ReloadFailureKeepsPreviousObject)
{
    Window window(MakeWindowDesc("ns_reload_fail"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    // 存在しない .ps. なので初回は magenta fallback で IsValid になる
    std::unique_ptr<Shader> holder = Shader::Create(k_FallbackPsPath);
    Shader* before = holder.get();
    ASSERT_TRUE(holder->IsValid());

    // 同じ存在しない path の Reload は実ファイル読込に失敗するので false、 旧物 (fallback) を保持する
    EXPECT_FALSE(holder->Reload());
    EXPECT_EQ(holder.get(), before);
    EXPECT_TRUE(holder->IsValid());
    EXPECT_NE(holder->Native(), nullptr);
}
