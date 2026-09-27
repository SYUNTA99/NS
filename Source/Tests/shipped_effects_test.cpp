#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/EffectScene.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Platform/Filesystem.h>
#include <Runtime/Platform/Window.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using NS::Gfx::EffectScene;
    using NS::Gfx::Renderer;
    using NS::Gfx::RendererDesc;
    using NS::Platform::FileSystem;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;

    constexpr const char* k_EffectExtension = ".efkefc";

    // 検査するのは Assets の実ファイルそのもの。ゲームの SceneRenderer が既定で読む置き場と同じ
    std::string ShippedEffectRoot()
    {
        const std::string assets = FileSystem::Combine(FileSystem::ContentRoot(), "Assets");
        return FileSystem::Combine(assets, "Effects");
    }

    // 置き場の直下の .efkefc から、Preload に渡す拡張子なしの名前を作る
    std::vector<std::string> ShippedEffectNames(const std::string& root)
    {
        std::vector<std::string> names;
        if (!FileSystem::IsDirectory(root))
        {
            return names;
        }
        for (const std::string& path : FileSystem::ListFiles(root, k_EffectExtension))
        {
            const std::string fileName = FileSystem::FileName(path);
            names.push_back(fileName.substr(0, fileName.size() - std::string(k_EffectExtension).size()));
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    WindowDesc MakeWindowDesc()
    {
        WindowDesc d{};
        d.title = "ns_shipped_effects";
        d.size = NS::Core::Size2D{64, 64};
        d.visible = false;
        return d;
    }

    RendererDesc MakeRendererDesc()
    {
        RendererDesc d{};
#ifdef NS_BUILD_DEBUG
        d.enableDebugLayer = true;
#else
        d.enableDebugLayer = false;
#endif
        d.vsync = false;
        return d;
    }
} // namespace

// EffectScene は構築時に Gpu() の device と context を使うので、Renderer より後に作る
class ShippedEffects : public ::testing::Test
{
protected:
    void SetUp() override
    {
        NS::Core::Logger::Init();
        m_window = std::make_unique<Window>(MakeWindowDesc());
        ASSERT_TRUE(m_window->IsValid());
        m_renderer = std::make_unique<Renderer>(MakeRendererDesc(), *m_window);
        ASSERT_TRUE(m_renderer->IsValid());
    }

    void TearDown() override
    {
        m_renderer.reset();
        m_window.reset();
        NS::Core::Logger::Shutdown();
    }

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
};

// 置いた絵が 1 本でもゲームで読めないと、その層は再生の時に Preload していない名前として黙って出ない
// Preload は参照するテクスチャ・モデル・マテリアル・カーブを 1 つでも読めなければ失敗を返すので、欠けも同時に縛る
TEST_F(ShippedEffects, EveryEffectPreloads)
{
    const std::string root = ShippedEffectRoot();
    const std::vector<std::string> names = ShippedEffectNames(root);

    // 置き場が空のまま緑になると、絵を置き忘れても検査が通って見える
    ASSERT_FALSE(names.empty()) << "確かめられなかった: " << root << " に .efkefc が 1 本も無い";

    EffectScene effects(root);
    ASSERT_TRUE(effects.IsValid());
    for (const std::string& name : names)
    {
        EXPECT_TRUE(effects.Preload(name)) << name << ".efkefc を読めないか、参照する素材が欠けている (" << root << ")";
    }
}
